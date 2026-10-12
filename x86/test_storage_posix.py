"""Exercise Linux LittleFS persistence, exclusive locks and invalid-file preservation in the real runner."""
import argparse
import fcntl
import json
from pathlib import Path
import subprocess
import tempfile


def check(runner):
    with tempfile.TemporaryDirectory(prefix="q2 linux storage ") as directory:
        root = Path(directory)
        manifest = {
            "format": 2, "id": "d272eb0c-3832-4420-b5a7-580ba9301098",
            "name": "Storage probe", "description": "Linux persistence check",
            "version": "1.0.0", "type": "app", "entry": "main.lua", "api": 2,
            "capabilities": {"required": ["storage.private", "system.log"], "optional": []},
            "resources": {"lua_heap_kib": 64},
        }
        (root / "manifest.json").write_text(json.dumps(manifest))
        source = root / "main.lua"
        source.write_text('''local q = require("q2")
return {on_start = function()
    local old = q.storage.get("probe")
    assert(q.os.log("info", "PERSIST=" .. tostring(old)))
    assert(q.storage.put("probe", "saved"))
end}
''')
        media = root / "private.lfs"
        args = [str(runner), str(root / "manifest.json"), str(source), "--headless", "--frames", "2",
                "--storage-file", str(media), "--grant", "storage.private", "--grant", "system.log"]
        def run():
            return subprocess.run(args, text=True, capture_output=True, timeout=15)
        first = run()
        assert first.returncode == 0, first.stderr
        assert "PERSIST=nil" in first.stderr, first.stderr
        second = run()
        assert second.returncode == 0 and "PERSIST=saved" in second.stderr, second.stderr
        assert media.stat().st_size == 256 * 1024
        before = media.read_bytes()
        with media.open("r+b") as stream:
            fcntl.flock(stream, fcntl.LOCK_EX | fcntl.LOCK_NB)
            locked = run()
            assert locked.returncode != 0, locked.stderr
            assert media.read_bytes() == before
        media.write_bytes(b"keep this invalid file")
        invalid = run()
        assert invalid.returncode != 0 and "format" in invalid.stderr, invalid.stderr
        assert media.read_bytes() == b"keep this invalid file"
        media.write_bytes(bytes(256 * 1024))
        corrupt = run()
        assert corrupt.returncode != 0, corrupt.stderr
        assert media.read_bytes() == bytes(256 * 1024)
        print("PASS: Linux private storage persists, locks exclusively, and never formats existing invalid media")


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--runner", type=Path, required=True)
    check(parser.parse_args().runner.resolve())
