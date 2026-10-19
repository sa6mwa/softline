"""Supply small truncated/complete HTTP fixture payloads; no test driver."""
import http.server
import pathlib
import sys

root = pathlib.Path(sys.argv[1])
payload = b'verified archive fixture'

class Data(http.server.BaseHTTPRequestHandler):
    count = 0

    def do_GET(self):
        Data.count += 1
        (root / 'requests').write_text(str(Data.count))
        self.send_response(200)
        self.send_header('Content-Length', str(len(payload)))
        self.end_headers()
        self.wfile.write(payload[:3] if Data.count < 3 else payload)
        self.close_connection = True

    def log_message(self, *args):
        pass

server = http.server.HTTPServer(('127.0.0.1', 0), Data)
(root / 'address').write_text(f'http://127.0.0.1:{server.server_port}/archive')
server.serve_forever()
