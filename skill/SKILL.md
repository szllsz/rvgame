---
name: pynq-evidence-check
description: Bind PYNQ or FPGA artifacts to a specific build and compare captured binary output with an independent golden reference. Use for offline validation, reproducibility checks, or planning safe DMA-owner retirement; hardware commands require the target project's actual interface contract.
---

# PYNQ evidence and artifact checks

Read [README.md](README.md) for the manifest schema, executable commands, known failure cases and limitations. `verify_artifacts.py` uses only Python's standard library and performs no board operations.

1. Identify the build, bit/HWH pair, program, input trace, capture format and measurement window. A filename containing `latest` is not version evidence.
2. Construct the manifest from the intended immutable inputs. Compare actual output with an independently defined reference; copying actual output to the reference proves only copying.
3. Run the checker and retain both success and failure JSON. Full comparisons must be nonempty and equal length; partial comparisons require justified offsets and bounds.
4. Interpret the result narrowly. A hash match binds bytes; it does not prove current remote ownership, semantic correctness, audio quality or physical boot.

For DMA lifetime or interrupted Overlay work, read [owner_lifecycle.md](owner_lifecycle.md). Do not paste addresses, PIDs or stop sequences from another board. Unknown drain state is not a reason to free DMA buffers or load a second Overlay.

For reuse, copy this directory and supply a manifest for the new project's artifacts. No rvgame source tree, account, SSH credential or fixed memory address is required. Actual cross-board operation has not been tested; the portable offline behavior is tested directly.
