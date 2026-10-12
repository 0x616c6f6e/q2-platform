"""Real native HTTP backend -> Kernel -> Lua integration against an isolated local HTTP service."""

import argparse
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
import json
from pathlib import Path
import subprocess
import tempfile
import threading
import time


class Handler(BaseHTTPRequestHandler):
    def log_message(self, *args):
        pass

    def do_GET(self):
        if self.path == "/slow":
            time.sleep(0.8)
        status = 404 if self.path == "/missing" else 302 if self.path == "/redirect" else 200
        body = b"hello-" + b"a" * 700 if self.path == "/health?x=1" else b"diagnostic"
        if self.path == "/large":
            body = b"a" * 262145
        if self.path == "/binary":
            body = b"a\x00b"
        try:
            self.send_response(status)
            if status == 302:
                self.send_header("Location", "/health?x=1")
            self.send_header("Content-Length", str(len(body)))
            self.end_headers()
            self.wfile.write(body)
        except (BrokenPipeError, ConnectionResetError, ConnectionAbortedError):
            pass

    def do_POST(self):
        size = int(self.headers.get("Content-Length", "0"))
        request = self.rfile.read(size)
        # Echo exact bytes: verifies copied uploads, JSON header and no implicit cookies/auth.
        valid = (self.path == "/echo" and self.headers.get("Content-Type") == "application/json"
                 and not self.headers.get("Cookie") and not self.headers.get("Authorization"))
        self.send_response(200 if valid else 400)
        self.send_header("Content-Length", str(len(request)))
        self.end_headers()
        self.wfile.write(request)


def tls_server(work):
    """Make an ephemeral self-signed server; never install test certificates in the OS store."""
    import datetime
    import ipaddress
    import ssl
    from cryptography import x509
    from cryptography.hazmat.primitives import hashes, serialization
    from cryptography.hazmat.primitives.asymmetric import rsa
    from cryptography.x509.oid import NameOID

    key = rsa.generate_private_key(public_exponent=65537, key_size=2048)
    name = x509.Name([x509.NameAttribute(NameOID.COMMON_NAME, "localhost")])
    now = datetime.datetime.now(datetime.timezone.utc)
    certificate = (
        x509.CertificateBuilder()
        .subject_name(name)
        .issuer_name(name)
        .public_key(key.public_key())
        .serial_number(x509.random_serial_number())
        .not_valid_before(now - datetime.timedelta(minutes=1))
        .not_valid_after(now + datetime.timedelta(days=1))
        .add_extension(
            x509.SubjectAlternativeName([x509.IPAddress(ipaddress.ip_address("127.0.0.1"))]), critical=False
        )
        .sign(key, hashes.SHA256())
    )
    cert_path, key_path = work / "server.pem", work / "key.pem"
    cert_path.write_bytes(certificate.public_bytes(serialization.Encoding.PEM))
    key_path.write_bytes(
        key.private_bytes(
            serialization.Encoding.PEM, serialization.PrivateFormat.PKCS8, serialization.NoEncryption()
        )
    )
    context = ssl.SSLContext(ssl.PROTOCOL_TLS_SERVER)
    context.load_cert_chain(cert_path, key_path)
    server = ThreadingHTTPServer(("127.0.0.1", 0), Handler)
    server.socket = context.wrap_socket(server.socket, server_side=True)
    threading.Thread(target=server.serve_forever, daemon=True).start()
    return server


def check(runner, tls=False, transport_runner=None):
    server = ThreadingHTTPServer(("127.0.0.1", 0), Handler)
    thread = threading.Thread(target=server.serve_forever, daemon=True)
    thread.start()
    secure = None
    try:
        if transport_runner:
            subprocess.run([str(transport_runner), f"http://127.0.0.1:{server.server_port}"],
                           check=True, timeout=20)
        if runner is None:
            return
        with tempfile.TemporaryDirectory(prefix="q2-network-") as directory:
            work = Path(directory)
            if tls:
                secure = tls_server(work)
            manifest = {
                "format": 2,
                "id": "d11ba672-5daf-4e42-93d0-ac185d94b039",
                "name": "Network probe",
                "version": "1.0.0",
                "type": "app",
                "entry": "main.lua",
                "api": 2,
                "capabilities": {"required": ["network.http", "system.log"], "optional": []},
                "resources": {"lua_heap_kib": 64},
            }
            (work / "manifest.json").write_text(json.dumps(manifest), encoding="utf8")
            lua = r"""local q = require("q2")
local base = "BASE"
local cases = {
    {"/health?x=1", 200}, {"/missing", 404}, {"/redirect", 302},
    {"/large", "quota_exceeded"}, {"/binary", "invalid_format"}, {"/slow", "timeout"}, {"/echo", 200, "post"}TLS_CASE
}
local index, handle, ticks, started, waited = 0, nil, 0, 0, 0
local function next_case(now)
    if handle then assert(q.network.close(handle)); handle = nil end
    index = index + 1
    if index > #cases then
        -- Closing a pending request invalidates its handle immediately, without waiting for the worker.
        local id = assert(q.network.get(base .. "/slow", 1000))
        assert(q.network.close(id))
        local value, err = q.network.status(id)
        assert(value == nil and err.code == "not_found")
        assert(q.os.log("info", "NETWORK_INTEGRATION_PASS ticks=" .. ticks))
        q.os.exit(); return
    end
    local path = cases[index][1]
    local url = path:sub(1, 5) == "https" and path or base .. path
    if cases[index][3] == "post" then
        handle = assert(q.network.post(url, 3000))
        assert(q.network.write(handle, '{"message":"'))
        assert(q.network.write(handle, 'test"}'))
        assert(q.network.send(handle))
    else
        handle = assert(q.network.get(url, index == 6 and 150 or 3000))
    end
    started, waited = now, 0
end
return {
    on_start = function()
        local id, err = q.network.get("file:///etc/passwd")
        assert(id == nil and err.code == "invalid_argument")
    end,
    on_tick = function(now)
        ticks = ticks + 1
        if index == 0 then next_case(now); return end
        if not handle then return end
        waited = waited + 1
        local status, err = q.network.status(handle)
        if status == "pending" then return end
        if type(cases[index][2]) == "string" then
            assert(status == nil and err.code == cases[index][2], err and err.code or status)
            if index == 6 then assert(waited >= 4 and now - started < 600, "UI stalled on timeout") end
        else
            assert(status == "done", err and err.code or status)
            assert(q.network.code(handle) == cases[index][2])
            local text, offset = "", 0
            while true do
                local chunk = assert(q.network.read(handle, offset, 113))
                if #chunk == 0 then break end
                text, offset = text .. chunk, offset + #chunk
            end
            if cases[index][3] == "post" then
                assert(text == '{"message":"test"}', "POST copy/header failure")
                assert(q.network.json(handle, "/message") == "test")
            else
                assert(#text == (index == 1 and 706 or 10), "chunking or redirect failure")
            end
        end
        next_case(now)
    end
}
""".replace(
                "BASE", f"http://127.0.0.1:{server.server_port}"
            )
            lua = lua.replace(
                "TLS_CASE",
                (
                    ', {"https://127.0.0.1:' + str(secure.server_port) + '/health", "permission_denied"}'
                    if secure
                    else ""
                ),
            )
            (work / "main.lua").write_text(lua, encoding="utf8")
            result = subprocess.run(
                [
                    str(runner),
                    str(work / "manifest.json"),
                    str(work / "main.lua"),
                    "--grant",
                    "network.http",
                    "--grant",
                    "system.log",
                    "--headless",
                    "--frames",
                    "900",
                ],
                capture_output=True,
                text=True,
                timeout=25,
            )
            output = result.stdout + result.stderr
            assert result.returncode == 0 and "NETWORK_INTEGRATION_PASS" in output, output
            if secure:
                print("PASS: self-signed TLS certificate rejected without weakening validation")
            print(
                "PASS: real transport/Lua GET + POST, query, chunk reads, 404, redirects disabled, size/binary rejection, timeout and cancellation"
            )
    finally:
        if secure:
            secure.shutdown()
            secure.server_close()
        server.shutdown()
        server.server_close()
        thread.join(timeout=3)


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--runner", type=Path)
    parser.add_argument("--transport-runner", type=Path)
    parser.add_argument("--serve", action="store_true", help="Run an interactive local test server")
    parser.add_argument("--port", type=int, default=18080)
    parser.add_argument(
        "--tls", action="store_true", help="Also reject self-signed HTTPS (requires cryptography)"
    )
    args = parser.parse_args()
    if args.serve:
        with ThreadingHTTPServer(("127.0.0.1", args.port), Handler) as server:
            print(f"Network test server: http://127.0.0.1:{server.server_port}/health", flush=True)
            try:
                server.serve_forever()
            except KeyboardInterrupt:
                pass
    elif args.runner or args.transport_runner:
        check(args.runner.resolve() if args.runner else None, args.tls,
              args.transport_runner.resolve() if args.transport_runner else None)
    else:
        parser.error("Use --runner EXE or --serve")
