"""Exercise HTTP device overrides through real Lua get/subscriptions, with isolated files."""

import argparse
import json
from pathlib import Path
import re
import subprocess
import tempfile
import time
import urllib.error
import urllib.request

TOPICS = [
    "power.battery.percent",
    "power.battery.voltage_mv",
    "power.battery.charging",
    "wireless.wifi.connected",
    "wireless.wifi.rssi_dbm",
    "wireless.ble.connected",
    "wireless.ble.rssi_dbm",
    "wireless.bt.connected",
]


def check(runner):
    with tempfile.TemporaryDirectory(prefix="q2-web-data-") as directory:
        work = Path(directory)
        manifest = {
            "format": 2,
            "id": "99b891dd-cd4d-4ddc-b855-920f1886d47c",
            "name": "Data probe",
            "description": "Temporary integration observer",
            "version": "1.0.0",
            "type": "app",
            "entry": "main.lua",
            "api": 2,
            "capabilities": {
                "required": ["data.read", "power.read", "wireless.read", "system.log"],
                "optional": [],
            },
            "resources": {"lua_heap_kib": 64},
        }
        (work / "manifest.json").write_text(json.dumps(manifest), encoding="utf8")
        lua = """local q = require("q2")
local topics = TOPICS
return {
    on_start = function()
        for _, key in ipairs(topics) do assert(q.data.subscribe(key)) end
    end,
    on_event = function(e)
        if e.type ~= "data.changed" then return end
        local value, err = q.data.get(e.key)
        assert(value == e.value, "get/event mismatch")
        if e.valid then assert(err == nil) else assert(err.code == e.status) end
        assert(q.os.log("info", "SAMPLE " .. e.key .. "=" .. tostring(value) .. ":" .. e.status))
    end
}
""".replace(
            "TOPICS", "{" + ",".join(json.dumps(t) for t in TOPICS) + "}"
        )
        (work / "main.lua").write_text(lua, encoding="utf8")
        args = [
            str(runner),
            str(work / "manifest.json"),
            str(work / "main.lua"),
            "--headless",
            "--frames",
            "1200",
            "--web-port",
            "0",
        ]
        for cap in manifest["capabilities"]["required"]:
            args += ["--grant", cap]
        with (work / "runner.log").open("w", encoding="utf8") as output:
            process = subprocess.Popen(args, stdout=output, stderr=output)
            try:

                def logs():
                    return (work / "runner.log").read_text(encoding="utf8")

                def until(predicate):
                    deadline = time.monotonic() + 6
                    while time.monotonic() < deadline:
                        if predicate():
                            return
                        assert process.poll() is None, logs()
                        time.sleep(0.03)
                    raise AssertionError(logs())

                until(lambda: re.search(r"http://127\.0\.0\.1:\d+/", logs()))
                url = re.search(r"http://127\.0\.0\.1:\d+/", logs())[0].rstrip("/")

                def request(path, method="GET", token=None, headers=None, expected=200):
                    h = dict(headers or {})
                    if token:
                        h["X-Q2-Token"] = token
                    req = urllib.request.Request(url + path, method=method, headers=h)
                    try:
                        with urllib.request.urlopen(req, timeout=3) as response:
                            status, body = response.status, response.read()
                    except urllib.error.HTTPError as error:
                        status, body = error.code, error.read()
                    assert status == expected, (path, status, body)
                    return json.loads(body)

                state = lambda: request("/api/state")["simulation"]
                token = request("/api/state")["token"]
                assert state()["enabled"]
                assert all(state()[group]["mode"] == "off" for group in ("battery", "wifi", "ble"))

                def sample(key, value, status="ok"):
                    until(lambda: f"SAMPLE {key}={value}:{status}" in logs())

                for topic in TOPICS:
                    sample(topic, "nil", "unsupported")

                def apply(query, **kwargs):
                    return request("/api/device?" + query, "POST", token, **kwargs)

                query = "group=battery&mode=value&percent=0&voltage_mv=3900&charging=0"
                request("/api/device?" + query, "POST", expected=403)
                apply(query, headers={"Origin": "http://evil.invalid"}, expected=403)
                apply(query)
                sample(TOPICS[0], "0")
                sample(TOPICS[2], "false")
                apply("group=battery&mode=value&percent=100&voltage_mv=4200&charging=1")
                sample(TOPICS[0], "100")
                sample(TOPICS[1], "4200")
                sample(TOPICS[2], "true")
                for group in ("wifi", "ble"):
                    apply(f"group={group}&mode=value&connected=1&rssi_dbm=-65")
                    sample(f"wireless.{group}.connected", "true")
                    sample(f"wireless.{group}.rssi_dbm", "-65")
                # Bad inputs must not mutate any group or sneak through integer overflow.
                before = state()
                for invalid in [
                    query.replace("percent=0", "percent=101"),
                    query.replace("voltage_mv=3900", "voltage_mv=-1"),
                    query.replace("percent=0", "percent=999999999999999999999999999999"),
                    query.replace("percent=0", "percent=1.5"),
                    query.replace("charging=0", "charging=2"),
                    query + "&percent=1",
                    query.replace("percent=0&", ""),
                    "group=wifi&mode=value&connected=1&rssi_dbm=-128",
                    "group=ble&mode=value&connected=2&rssi_dbm=0",
                    "group=wifi&mode=value&connected=1&rssi_dbm=1",
                    "group=wifi&mode=off&unexpected=1",
                    "group=bt&mode=off",
                    "group=ble&mode=oops",
                    "group=ble&group=wifi",
                    "group=ble&mode=off&",
                ]:
                    apply(invalid, expected=400)
                    assert state() == before, invalid
                # Repeated polling refreshes radio TTL without flooding Lua with unchanged samples.
                count = logs().count("SAMPLE wireless.ble.rssi_dbm=-65:ok")
                time.sleep(5.2)
                assert logs().count("SAMPLE wireless.ble.rssi_dbm=-65:ok") == count
                assert "SAMPLE wireless.ble.rssi_dbm=nil:timeout" not in logs()
                apply("group=wifi&mode=value&connected=0&rssi_dbm=-65")
                sample("wireless.wifi.connected", "false")
                sample("wireless.wifi.rssi_dbm", "nil", "invalid_state")
                assert state()["ble"]["connected"] is True
                for mode, status in [("error", "io_error"), ("timeout", "timeout"), ("off", "unsupported")]:
                    apply(f"group=battery&mode={mode}")
                    sample(TOPICS[0], "nil", status)
                    assert state()["battery"]["mode"] == mode
                apply("group=ble&mode=off")
                assert state()["ble"]["mode"] == "off"
                assert "SAMPLE wireless.bt.connected=true" not in logs()
                process.wait(timeout=25)
                assert process.returncode == 0, logs()
                assert "get/event mismatch" not in logs()
                print(
                    "PASS: HTTP -> typed Lua get/subscriptions; battery/Wi-Fi/BLE; invalid data, auth, TTL, isolation"
                )
            finally:
                if process.poll() is None:
                    process.terminate()
                    process.wait(timeout=5)


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--runner", type=Path, required=True)
    check(parser.parse_args().runner.resolve())
