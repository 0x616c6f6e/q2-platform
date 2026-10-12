# Historical display bring-up (2026-10-11)

AM213 was confirmed working on hardware. These files preserve the previous RGB/area/pixel-pattern diagnostics and alternative SPI transport for reference only. They are not compiled or exposed through menuconfig. The source snapshots are not an independently buildable firmware.

Production retains the confirmed ESP LCD transport, RGB565, DMA tile lifetime, software rotation, vendor initialization and touch IRQ/retry queue. Regression tests for these contracts remain in `../../tests/`.
