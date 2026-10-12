"""Verify real-time idle and complete Web wake-gesture consumption through the actual x86 runner."""

import argparse
import json
from pathlib import Path
import re
import subprocess
import sys
import tempfile
import time
import urllib.request


def check(runner, app_root, idle_ms):
    sys.path.insert(0, str(app_root / "scripts"))
    from q2app import pack

    with tempfile.TemporaryDirectory(prefix="q2-system-ui-") as directory:
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
            "100000",
            "--web-port",
            "0",
            "--key",
            "POWER",
        ]
        log = work / "runner.log"
        with log.open("w", encoding="utf8") as output:
            process = subprocess.Popen(args, stdout=output, stderr=output)
            try:
                deadline = time.monotonic() + 10
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

                def request(path, post=False):
                    headers = {"X-Q2-Token": token} if post else {}
                    req = urllib.request.Request(
                        url + path, headers=headers, method="POST" if post else "GET"
                    )
                    with urllib.request.urlopen(req, timeout=3) as response:
                        return json.load(response)

                state = request("/api/state")
                token = state["token"]
                gap = state["timing"]["double_gap_ms"] / 1000
                hold = state["timing"]["long_press_ms"] / 1000 + 0.08
                short = max(0.07, state["timing"]["single_min_ms"] / 1000 + 0.03)
                assert state["content_height"] == 378 and state["status_bar_height"] == 32

                def edge(value):
                    assert request("/api/key?name=POWER&state=" + value, True)["ok"]

                def click():
                    edge("down")
                    time.sleep(short)
                    edge("up")

                def wait_idle():
                    start = time.monotonic()
                    while time.monotonic() - start < idle_ms / 1000 + 3:
                        state = request("/api/state")
                        if state["screensaver"]:
                            return
                        time.sleep(0.12)
                    raise AssertionError("No idle screensaver: " + str(state))

                for count, gesture in enumerate(("single", "double", "long")):
                    wait_idle()
                    assert request("/api/state")["reports"] == count
                    if gesture == "long":
                        edge("down")
                        deadline = time.monotonic() + hold
                        while time.monotonic() < deadline:
                            request("/api/heartbeat", True)
                            time.sleep(0.1)
                        edge("up")
                    else:
                        click()
                        if gesture == "double":
                            click()
                    time.sleep(gap + 0.1)
                    state = request("/api/state")
                    assert not state["screensaver"] and state["reports"] == count, state
                    # A second independent gesture is delivered once, with the same Lua instance alive.
                    click()
                    time.sleep(gap + 0.15)
                    state = request("/api/state")
                    assert state["reports"] == count + 1 and state["last_action"] == 1, state
                    print("PASS: " + gesture + " wakes only; next gesture reaches APP", flush=True)
                text = log.read_text()
                for count in (1, 2, 3):
                    assert f"KEY POWER=SINGLE total={count}" in text, text
                assert "failed result=" not in text
            finally:
                if process.poll() is None:
                    process.terminate()
                process.wait(timeout=5)
        print("PASS: real idle timer, single/double/long wake filtering and preserved Lua state")


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--runner", type=Path, required=True)
    parser.add_argument("--app-root", type=Path, required=True)
    parser.add_argument("--idle-ms", type=int, default=30000)
    args = parser.parse_args()
    check(args.runner.resolve(), args.app_root.resolve(), args.idle_ms)
