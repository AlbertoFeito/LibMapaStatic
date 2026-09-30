import http.server, socketserver
class H(http.server.BaseHTTPRequestHandler):
    def do_GET(self): self.send_response(500); self.end_headers()
    def log_message(self,*a): pass
socketserver.TCPServer.allow_reuse_address=True
socketserver.TCPServer(("127.0.0.1",8890),H).serve_forever()
