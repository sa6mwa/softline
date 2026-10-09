"""Serve deterministic DNS packets and log fixture inputs; no test execution."""
import pathlib
import select
import socket
import struct
import sys

root = pathlib.Path(sys.argv[1])
sockets = []
for family, host, filename in ((socket.AF_INET, '127.0.0.1', 'port'),
                               (socket.AF_INET6, '::1', 'port6')):
    udp = socket.socket(family, socket.SOCK_DGRAM)
    udp.bind((host, 0))
    tcp = socket.socket(family, socket.SOCK_STREAM)
    tcp.bind((host, udp.getsockname()[1]))
    tcp.listen()
    sockets += [udp, tcp]
    (root / filename).write_text(str(udp.getsockname()[1]))

def response(data, tcp):
    offset, labels = 12, []
    while data[offset]:
        size = data[offset]
        labels.append(data[offset + 1:offset + 1 + size].decode().lower())
        offset += size + 1
    offset += 1
    kind = struct.unpack_from('!H', data, offset)[0]
    name = '.'.join(labels)
    with (root / 'queries').open('a') as log:
        log.write(f'{name} {kind} {int(tcp)}\n')
    question = data[12:offset + 4]
    def packet(flags, count, records=b''):
        return data[:2] + struct.pack('!HHHHH', flags, 1, count, 0, 0) + question + records
    if name == 'silent.test':
        return None
    if name == 'broken.test':
        return packet(0x8180, 1, b'\xc0')
    if name == 'tcp.test' and not tcp:
        return packet(0x8380, 0)
    if name not in ('dns.test', 'alias.test', 'target.test', 'tcp.test', 'short.suffix.test'):
        return packet(0x8183, 0)
    owner, records, count = b'\xc0\x0c', b'', 1
    if name == 'alias.test':
        owner = b'\x06target\x04test\x00'
        records = b'\xc0\x0c' + struct.pack('!HHIH', 5, 1, 60, len(owner)) + owner
        count += 1
    address = socket.inet_pton(socket.AF_INET if kind == 1 else socket.AF_INET6,
                              '192.0.2.51' if kind == 1 else '2001:db8::51')
    records += owner + struct.pack('!HHIH', kind, 1, 60, len(address)) + address
    return packet(0x8180, count, records)

clients = {}
while True:
    for sock in select.select(sockets + list(clients), [], [])[0]:
        if sock in sockets:
            if sock.type == socket.SOCK_DGRAM:
                data, peer = sock.recvfrom(65535)
                reply = response(data, False)
                if reply: sock.sendto(reply, peer)
            else:
                client, _ = sock.accept()
                clients[client] = b''
        else:
            chunk = sock.recv(65535)
            if not chunk:
                del clients[sock]
                sock.close()
                continue
            clients[sock] += chunk
            while len(clients[sock]) >= 2:
                length = struct.unpack('!H', clients[sock][:2])[0]
                if len(clients[sock]) < length + 2: break
                data, clients[sock] = clients[sock][2:length + 2], clients[sock][length + 2:]
                reply = response(data, True)
                if reply: sock.sendall(struct.pack('!H', len(reply)) + reply)
