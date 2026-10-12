"""Run actual geometry, pixel copy and touch state code for both supported AMOLED modules."""
from pathlib import Path
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[1]
KERNEL = ROOT.parents[1] / "kernel"
with tempfile.TemporaryDirectory(prefix="q2-panel-profiles-") as tmp:
    tmp = Path(tmp)
    (tmp / "sdkconfig.h").write_text("#define CONFIG_Q2_AM178_INITIAL_BRIGHTNESS 25\n")
    # Compile the real flush body, substituting only the SDK transport with a panel-RAM sink.
    source = (ROOT / "main/am178.c").read_text()
    start = source.index("static q2_result_t flush(")
    end = source.index("\n/* Dump the real table", start)
    (tmp / "am178_flush_under_test.inc").write_text(source[start:end])
    for panel in (0, 1):
        for case in ("geometry", "touch", "pixel", "flush"):
            for rgb888 in (0,):
                exe = tmp / f"test-{panel}-{case}-{rgb888}"
                command = ["cc", "-std=c11", "-Wall", "-Wextra", "-Werror", "-fsanitize=undefined,address",
                           f"-DQ2_PANEL_AM213={panel}", 
                           "-I" + str(tmp), "-I" + str(ROOT / "main"), "-I" + str(KERNEL / "include"),
                           str(ROOT / "tests" / f"test_{case}.c"), str(ROOT / "main/am178_geometry.c")]
                if case == "touch":
                    command.append(str(ROOT / "main/am178_touch.c"))
                subprocess.run(command + ["-o", str(exe)], check=True)
                subprocess.run([str(exe)], check=True)
print("PASS: AM178 and AM213 rotations, touch IRQ/retry/release, RGB565 frame boundaries and actual panel flush")
