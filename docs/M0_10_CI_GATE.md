# M0.10 CI Gate

This branch exists to exercise the final M0 ARM64 validation workflow through a pull request. M0 is not complete until the native AArch64 and independent cross-build/QEMU gates are green. Unsupported kernel capabilities must remain explicit skips rather than being counted as passes.
