"""Exercise the board's embedded Lua fixture through the real desktop Kernel/LVGL runtime."""

import argparse
import json
from pathlib import Path
import subprocess
import tempfile
from PIL import Image


def check(runner, output):
    output.mkdir(parents=True, exist_ok=True)
    source = Path(__file__).resolve().parents[1] / "main/brightness_demo.lua"
    manifest = {
        "format": 2,
        "id": "a6014dd9-ce1f-46b9-9e77-260d837df49e",
        "name": "AM178 test",
        "version": "1.0.0",
        "entry": "main.lua",
        "api": 2,
        "type": "app",
        "capabilities": {"required": ["display.ui"], "optional": []},
        "resources": {"lua_heap_kib": 64},
    }
    with tempfile.TemporaryDirectory(prefix="q2-am178-") as folder:
        work = Path(folder)
        (work / "manifest.json").write_text(json.dumps(manifest), encoding="utf8")
        (work / "main.lua").write_bytes(source.read_bytes())
        for w, h in ((368, 448), (448, 368)):
            for name, x, expected in (
                ("red", 68, (128, 0, 0)),
                ("green", 170, (0, 80, 0)),
                ("blue", 270, (0, 0, 128)),
            ):
                frame = output / f"{w}x{h}-{name}.bmp"
                # The desktop runner adds a 32px status bar; APP buttons use local coordinates.
                args = [
                    str(runner),
                    str(work / "manifest.json"),
                    str(work / "main.lua"),
                    "--headless",
                    "--width",
                    str(w),
                    "--height",
                    str(h),
                    "--frames",
                    "50",
                    "--grant",
                    "display.ui",
                    "--capture",
                    str(frame),
                    "--click",
                    str(x),
                    str(h - 80),
                ]
                run = subprocess.run(args, capture_output=True, text=True, timeout=20)
                assert run.returncode == 0, run.stdout + run.stderr
                image = Image.open(frame).convert("RGB")
                assert image.getpixel((w // 2, h - 15)) == expected
                image.save(frame.with_suffix(".png"))
                print(f"PASS: {w}x{h} Lua touch {name}")


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--runner", required=True, type=Path)
    parser.add_argument("--artifacts", required=True, type=Path)
    args = parser.parse_args()
    check(args.runner.resolve(), args.artifacts.resolve())
