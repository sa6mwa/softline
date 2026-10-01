#!/usr/bin/env python3
"""Deterministic hosts/DNS/literal tests for Softline's private resolver."""

import pathlib
import socket
import struct
import subprocess
import sys
import tempfile
import threading
import time


def question(data):
    offset = 12
    labels = []
    while data[offset]:
        size = data[offset]
        labels.append(data[offset + 1:offset + 1 + size].decode().lower())
        offset += size + 1
    offset += 1
    kind, _ = struct.unpack('!HH', data[offset:offset + 4])
    return '.'.join(labels), kind, data[12:offset + 4]


class DNS:
    def __init__(self, family=socket.AF_INET):
        host = '127.0.0.1' if family == socket.AF_INET else '::1'
        self.udp = socket.socket(family, socket.SOCK_DGRAM)
        self.udp.bind((host, 0))
        self.port = self.udp.getsockname()[1]
        self.tcp = socket.socket(family)
        self.tcp.bind((host, self.port))
        self.tcp.listen()
        self.stop = threading.Event()
        self.queries = []
        self.errors = []

    def response(self, data, tcp=False):
        name, kind, q = question(data)
        self.queries.append((name, kind, tcp))
        header = data[:2]
        if name == 'silent.test':
            return None
        if name == 'broken.test':
            # Header claims an answer but provides a truncated RR. The parser
            # must reject it without returning an address or leaving resources.
            return header + struct.pack('!HHHHH', 0x8180, 1, 1, 0, 0) + q + b'\xc0'
        if name == 'tcp.test' and not tcp:
            return header + struct.pack('!HHHHH', 0x8380, 1, 0, 0, 0) + q
        if name not in ('dns.test', 'alias.test', 'target.test', 'tcp.test',
                        'short.suffix.test'):
            return header + struct.pack('!HHHHH', 0x8183, 1, 0, 0, 0) + q
        records = []
        if name == 'alias.test':
            cname = b'\x06target\x04test\x00'
            records.append(b'\xc0\x0c' + struct.pack('!HHIH', 5, 1, 60, len(cname)) + cname)
            owner = cname
        else:
            owner = b'\xc0\x0c'
        address = socket.inet_pton(socket.AF_INET if kind == 1 else socket.AF_INET6,
                                   '192.0.2.51' if kind == 1 else '2001:db8::51')
        records.append(owner + struct.pack('!HHIH', kind, 1, 60, len(address)) + address)
        return header + struct.pack('!HHHHH', 0x8180, 1, len(records), 0, 0) + q + b''.join(records)

    def udp_loop(self):
        self.udp.settimeout(0.1)
        while not self.stop.is_set():
            try:
                data, peer = self.udp.recvfrom(65535)
            except socket.timeout:
                continue
            reply = self.response(data)
            if reply:
                self.udp.sendto(reply, peer)

    @staticmethod
    def read(sock, size):
        data = b''
        while len(data) < size:
            chunk = sock.recv(size - len(data))
            if not chunk:
                return None
            data += chunk
        return data

    def tcp_loop(self):
        self.tcp.settimeout(0.1)
        while not self.stop.is_set():
            try:
                conn, _ = self.tcp.accept()
            except socket.timeout:
                continue
            with conn:
                conn.settimeout(2)
                while not self.stop.is_set():
                    prefix = self.read(conn, 2)
                    if not prefix:
                        break
                    data = self.read(conn, struct.unpack('!H', prefix)[0])
                    reply = self.response(data, tcp=True)
                    if reply:
                        conn.sendall(struct.pack('!H', len(reply)) + reply)

    def guard(self, operation):
        try:
            operation()
        except Exception as error:
            self.errors.append(error)

    def __enter__(self):
        self.threads = [threading.Thread(target=self.guard, args=(method,))
                        for method in (self.udp_loop, self.tcp_loop)]
        for thread in self.threads:
            thread.start()
        return self

    def __exit__(self, *_):
        self.stop.set()
        for thread in self.threads:
            thread.join(3)
            assert not thread.is_alive(), 'DNS fixture thread leaked'
        self.udp.close()
        self.tcp.close()
        assert not self.errors, self.errors


def main():
    fixture = pathlib.Path(sys.argv[1]).resolve()
    build = pathlib.Path(sys.argv[2]).resolve()
    with tempfile.TemporaryDirectory(prefix='resolver-test.', dir=build) as scratch, DNS() as dns:
        root = pathlib.Path(scratch)
        hosts = root / 'hosts'
        hosts.write_text('192.0.2.9 hosts.test alias-host # comment\n'
                         '2001:db8::9 hosts6.test\n192.0.2.10 dns.test\n')
        resolv = root / 'resolv.conf'
        resolv.write_text('nameserver 127.0.0.1\nsearch suffix.test\noptions ndots:1 attempts:1\n')

        def run(name, expected=None, timeout=1200, servers=None):
            start = time.monotonic()
            result = subprocess.run([str(fixture), name, str(hosts), str(resolv),
                                     servers or f'127.0.0.1:{dns.port}', str(timeout)],
                                    capture_output=True, text=True, timeout=4)
            if expected is None:
                assert result.returncode == 1 and not result.stdout, (name, result)
            else:
                assert result.returncode == 0, (name, result.stderr, result.returncode)
                assert set(result.stdout.splitlines()) == {ip + ' 6010' for ip in expected}, (name, result.stdout)
            return time.monotonic() - start

        run('127.0.0.1', ['127.0.0.1'])
        run('::1', ['::1'])
        index = socket.if_nametoindex('lo')
        run('fe80::1%lo', [f'fe80::1%{index}'])
        run(f'fe80::1%{index}', [f'fe80::1%{index}'])
        run('fe80::1%0', ['fe80::1'])
        run('fe80::1%')
        run('fe80::1%4294967296')
        run('fe80::1%no-such-iface')
        run('hosts.test', ['192.0.2.9'])
        run('alias-host', ['192.0.2.9'])
        run('hosts6.test', ['2001:db8::9'])
        run('dns.test', ['192.0.2.10'])
        assert not dns.queries, 'literal/hosts lookup unnecessarily queried DNS'
        hosts.write_text('')
        for name in ('dns.test', 'alias.test', 'short', 'tcp.test'):
            run(name, ['192.0.2.51', '2001:db8::51'])
        assert any(tcp for _, _, tcp in dns.queries), 'TCP fallback not exercised'
        assert any(name == 'short.suffix.test' for name, _, _ in dns.queries)
        run('absent.test')
        run('broken.test', timeout=200)
        elapsed = run('silent.test', timeout=120)
        assert 0.08 <= elapsed < 0.8, ('deadline not enforced', elapsed)
        run('dns.test', timeout=0)
        run('dns.test', servers='not a server')
        with DNS(socket.AF_INET6) as dns6:
            run('dns.test', ['192.0.2.51', '2001:db8::51'],
                servers=f'[::1]:{dns6.port}')
            assert dns6.queries, 'IPv6 DNS transport not exercised'
    print('Resolver: IPv4/IPv6, hosts/aliases/precedence, A/AAAA/CNAME, search, TCP fallback, malformed replies and deadlines passed.')


if __name__ == '__main__':
    main()
