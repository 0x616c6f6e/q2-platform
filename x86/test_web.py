"""Verify loopback HTTP -> named-key provider -> real Key Demo Lua, using temporary packages."""

from pathlib import Path
import argparse
import json
import re
import socket
import subprocess
import tempfile
import time
import urllib.request
import urllib.error
import sys


def check(runner, app_root, capture=None):
    sys.path.insert(0, str(app_root / "scripts"))
    from q2app import pack

    with tempfile.TemporaryDirectory(prefix="q2-web-") as directory:
        work = Path(directory)
        pack(app_root / "apps/key-demo", work / "key-demo.q2app")
        args = [
            str(runner),
            str(work / "key-demo.run/manifest.json"),
            str(work / "key-demo.run/main.lua"),
            "--grant",
            "display.ui",
            "--grant",
            "input.keys",
            "--grant",
            "system.log",
            "--headless",
            "--frames",
            "900",
            "--web-port",
            "0",
            "--key",
            "POWER",
            "--key",
            "CUSTOM_1",
        ]
        if capture:
            args += ["--capture", str(capture)]
        log = work / "runner.log"
        with log.open("w", encoding="utf8") as output:
            process = subprocess.Popen(args, stdout=output, stderr=output)
            try:
                deadline = time.monotonic() + 8
                url = None
                while time.monotonic() < deadline:
                    text = log.read_text(encoding="utf8")
                    match = re.search(r"http://127\.0\.0\.1:(\d+)/", text)
                    if match:
                        url = match.group(0).rstrip("/")
                        break
                    assert process.poll() is None, text
                    time.sleep(0.04)
                assert url, log.read_text()

                def request(path, method="GET", token=None, headers=None, expected=200):
                    h = dict(headers or {})
                    if token:
                        h["X-Q2-Token"] = token
                    req = urllib.request.Request(url + path, method=method, headers=h)
                    try:
                        with urllib.request.urlopen(req, timeout=3) as r:
                            code, data = r.status, r.read()
                    except urllib.error.HTTPError as e:
                        code, data = e.code, e.read()
                    assert code == expected, (path, code, data)
                    return data

                page = request("/").decode("utf-8-sig")
                assert "Key Lab" in page and "/api/frame" not in page and "/api/pointer" not in page
                state = json.loads(request("/api/state"))
                token = state["token"]
                assert [k["name"] for k in state["keys"]] == ["POWER", "CUSTOM_1"]
                assert all(k["last_action"] == 0 for k in state["keys"])
                timing = state["timing"]
                short = max(0.06, timing["single_min_ms"] / 1000 + 0.03)
                gap = timing["double_gap_ms"] / 1000
                long = timing["long_press_ms"] / 1000

                def edge(name, level):
                    body = json.loads(request(f"/api/key?name={name}&state={level}", "POST", token))
                    assert body["ok"]

                def click(name):
                    edge(name, "down")
                    time.sleep(short)
                    edge(name, "up")

                def wait_reports(count):
                    deadline = time.monotonic() + max(gap, long) + 2
                    while time.monotonic() < deadline:
                        snapshot = json.loads(request("/api/state"))
                        if snapshot["reports"] >= count:
                            assert snapshot["reports"] == count, snapshot
                            return snapshot
                        time.sleep(0.03)
                    raise AssertionError(snapshot)

                click("POWER")
                wait_reports(1)
                click("POWER")
                wait_reports(2)
                click("POWER")
                click("POWER")
                wait_reports(3)
                edge("POWER", "down")
                # Keep the raw hold alive; recognition happens before releasing it.
                until = time.monotonic() + long + 0.08
                while time.monotonic() < until:
                    request("/api/heartbeat", "POST", token)
                    time.sleep(0.1)
                state = wait_reports(4)
                assert state["keys"][0]["pressed"] and state["keys"][0]["last_action"] == 3, state
                edge("POWER", "up")
                for _ in range(40):
                    if "KEY POWER=LONG total=4" in log.read_text():
                        break
                    time.sleep(0.02)
                text = log.read_text()
                for name, count in [("SINGLE", 1), ("SINGLE", 2), ("DOUBLE", 3), ("LONG", 4)]:
                    assert f"KEY POWER={name} total={count}" in text, text
                assert [event["action"] for event in state["events"]] == [1, 1, 2, 3]
                assert all(event["result"] == 0 for event in state["events"])
                edge("POWER", "down")
                edge("POWER", "cancel")
                time.sleep(gap + 0.05)
                assert json.loads(request("/api/state"))["reports"] == 4
                request("/api/key?name=POWER&state=down", "POST", expected=403)
                request(
                    "/api/key?name=POWER&state=down", "POST", token, {"Origin": "https://example.com"}, 403
                )
                request("/api/state", headers={"Host": "example.com"}, expected=400)
                request("/api/key?name=UNKNOWN&state=down", "POST", token, expected=404)
                request("/api/key?name=POWER&state=invalid", "POST", token, expected=400)
                request("/api/key?name=POWER&action=single", "POST", token, expected=400)
                request("/api/frame.bmp", expected=404)
                request("/api/pointer?x=0&y=0&state=1", "POST", token, expected=400)
                # Split headers across receives, then verify a valid request still works.
                port = int(url.rsplit(":", 1)[1])
                with socket.create_connection(("127.0.0.1", port), timeout=3) as s:
                    s.sendall(b"GET /api/state HTTP/1.1\r\n")
                    time.sleep(0.03)
                    s.sendall(f"Host: 127.0.0.1:{port}\r\nConnection: close\r\n\r\n".encode())
                    data = b""
                    while chunk := s.recv(4096):
                        data += chunk
                    assert data.startswith(b"HTTP/1.1 200"), data
                assert json.loads(request("/api/state"))["reports"] == 4
                click("CUSTOM_1")
                click("CUSTOM_1")
                wait_reports(5)
                # Disconnect while held: one LONG is allowed, but the key must be unstuck by the lease.
                edge("POWER", "down")
                time.sleep(2.2)
                state = json.loads(request("/api/state"))
                assert not state["keys"][0]["pressed"], state
                assert state["reports"] == 6 and state["last_action"] == 3, state
                # Let the normal owner loop terminate, exercising orderly server/provider teardown.
                assert process.wait(timeout=18) == 0, log.read_text()
                assert "KEY CUSTOM_1=DOUBLE total=5" in log.read_text()
            finally:
                if process.poll() is None:
                    process.terminate()
                    process.wait(timeout=5)
        print(
            "PASS: raw edges -> x86 timing -> real Lua; single/double/hold/cancel/lease/custom keys, HTTP rejection, no screen/touch endpoints, clean shutdown"
        )


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--runner", required=True, type=Path)
    parser.add_argument("--app-root", required=True, type=Path)
    parser.add_argument("--capture", type=Path, help="Optional BMP snapshot of Key Demo after the events")
    args = parser.parse_args()
    check(args.runner.resolve(), args.app_root.resolve(), args.capture.resolve() if args.capture else None)
