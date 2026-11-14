#!/usr/bin/env python3
"""A stand-in for RetroAchievements' site over TLS, for
tests/retroarch_tls_test.sh: it answers the calls a game's start makes
(login2, gameid, patch, startsession, awardachievement, ping) and the
pictures, and writes a line per request with the TLS it came over. A client
that hangs up on the certificate is a line too (REFUSED). Neither the
password nor the token is written."""
import argparse
import base64
import json
import ssl
import sys
import threading
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
from urllib.parse import parse_qs, urlsplit

PNG = base64.b64decode(
    "iVBORw0KGgoAAAANSUhEUgAAAAEAAAABCAYAAAAfFcSJAAAADUlEQVR42mNkYPhfDwAChwGA60e6kgAAAABJRU5ErkJggg==")
MEDIA = "https://media.retroachievements.org"
USER, PASSWORD, TOKEN = "exampleplayer", "Key456example", "Tok123exampleABC"


def achievement(number, memory, title):
    return {"ID": number, "MemAddr": memory, "Title": title, "Description": "x" * 900, "Points": 1,
            "Author": "example", "Modified": 1700000000, "Created": 1700000000, "BadgeName": "%05d" % number,
            "Flags": 3, "Type": None, "Rarity": 10.0, "RarityHardcore": 5.0,
            "BadgeURL": "%s/Badge/%05d.png" % (MEDIA, number),
            "BadgeLockedURL": "%s/Badge/%05d_lock.png" % (MEDIA, number)}


def answer(form):
    call = form.get("r", [""])[0]
    if call == "login2":
        if form.get("p", [""])[0] != PASSWORD and form.get("t", [""])[0] != TOKEN:
            return {"Success": False, "Error": "Invalid User/Password combination. Please try again",
                    "Code": "invalid_credentials", "Status": 401}
        return {"Success": True, "User": USER, "AvatarUrl": MEDIA + "/UserPic/%s.png" % USER, "Token": TOKEN,
                "Score": 10, "SoftcoreScore": 0, "Messages": 0, "Permissions": 1, "AccountType": "Registered"}
    if call == "gameid":
        return {"Success": True, "GameID": 1}
    if call == "patch":
        # One achievement the test core earns, and enough others for an
        # answer of some 50 KB: several TLS records.
        every = [achievement(1, "0xH0000=1", "The First Byte")]
        every += [achievement(n, "0xH0001=99", "Never %d" % n) for n in range(2, 40)]
        return {"Success": True, "PatchData": {
            "ID": 1, "Title": "TLS Test Game", "ConsoleID": 1, "ImageIcon": "/Images/000001.png",
            "ImageIconURL": MEDIA + "/Images/000001.png", "RichPresencePatch": "Display:\nTrying TLS",
            "Achievements": every, "Leaderboards": []}}
    if call == "startsession":
        return {"Success": True, "Unlocks": [], "HardcoreUnlocks": [], "ServerNow": 1700000000}
    if call == "awardachievement":
        return {"Success": True, "Score": 11, "SoftcoreScore": 1, "AchievementID": int(form.get("a", ["0"])[0]),
                "AchievementsRemaining": 38}
    return {"Success": True}


class Handler(BaseHTTPRequestHandler):
    protocol_version = "HTTP/1.1"
    # The headers and the start of the body in one write, as a web server
    # sends them: RetroArch 1.20.0 waits for ever for a chunked body whose
    # first length comes apart from the headers, over plain HTTP too.
    wbufsize = 65536

    def note(self, what, size):
        over = "%s %s" % (self.connection.version(), self.connection.cipher()[0])
        with self.server.lock:
            self.server.out.write("%s %s host=%s size=%d\n" % (over, what, self.headers.get("Host", ""), size))
            self.server.out.flush()

    def send(self, body, kind):
        self.send_response(200)
        self.send_header("Content-Type", kind)
        self.send_header("Connection", "close")
        if self.server.chunked:
            self.send_header("Transfer-Encoding", "chunked")
            self.end_headers()
            for at in range(0, len(body), 7000):
                piece = body[at:at + 7000]
                self.wfile.write(b"%x\r\n" % len(piece) + piece + b"\r\n")
            self.wfile.write(b"0\r\n\r\n")
        else:
            self.send_header("Content-Length", str(len(body)))
            self.end_headers()
            self.wfile.write(body)
        self.close_connection = True

    def do_GET(self):
        url = urlsplit(self.path)
        if url.path.endswith(".png"):
            self.note("GET " + url.path, len(PNG))
            self.send(PNG, "image/png")
        else:
            self.api(parse_qs(url.query))

    def do_POST(self):
        form = parse_qs(urlsplit(self.path).query)
        form.update(parse_qs(self.rfile.read(int(self.headers.get("Content-Length", "0"))).decode()))
        self.api(form)

    def api(self, form):
        body = json.dumps(answer(form)).encode()
        shown = "&".join("%s=%s" % (key, "..." if key in ("p", "t") else value[0][:40])
                         for key, value in sorted(form.items()))
        self.note("API " + shown, len(body))
        self.send(body, "application/json")

    def log_message(self, *args):
        pass


class Server(ThreadingHTTPServer):
    daemon_threads = True

    def get_request(self):
        plain, address = self.socket.accept()
        # The handshake is the handler's thread's (finish_request), so that
        # a client that never answers holds no one else up.
        return self.context.wrap_socket(plain, server_side=True, do_handshake_on_connect=False), address

    def finish_request(self, request, address):
        request.settimeout(20)
        request.do_handshake()
        super().finish_request(request, address)

    def handle_error(self, request, address):
        kind, error = sys.exc_info()[:2]
        with self.lock:
            self.out.write("REFUSED %s: %s\n" % (kind.__name__, error))
            self.out.flush()


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--port", type=int, default=443)
    parser.add_argument("--cert", required=True, help="the certificate and those above it, in one file")
    parser.add_argument("--key", required=True)
    parser.add_argument("--chunked", action="store_true", help="answer without a length, in chunks")
    parser.add_argument("--log", required=True)
    args = parser.parse_args()
    server = Server(("127.0.0.1", args.port), Handler)
    server.lock = threading.Lock()
    server.chunked = args.chunked
    server.out = open(args.log, "a")
    server.context = ssl.SSLContext(ssl.PROTOCOL_TLS_SERVER)
    server.context.minimum_version = ssl.TLSVersion.TLSv1_2
    server.context.load_cert_chain(args.cert, args.key)
    server.serve_forever()
