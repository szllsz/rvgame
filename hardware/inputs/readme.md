# Hardware source reconstruction

Use Vivado 2025.2.1 on Windows, target xc7z020clg400-2, top module top.
The delivered RTL, IP, block design, constraints and memory initialization files
are the inputs. No original XPR or previous implementation DCP is required.
`sources.json` records their provenance and current hashes. The block design
originated in Vivado 2024.2; the reconstruction script migrates its IP into the
2025.2 tool family and regenerates all products.

From the bundle root, select a new output directory:

```text
python hardware/rebuild.py --destination <new_hardware_dir> --vivado <vivado_2025.2.1>
```

This entry runs project reconstruction, platform completion, synthesis,
implementation and bit/XSA/HWH export in order. The implementation gates check
setup/hold timing, audio output timing, DRC and methodology. Logs and generated
reports are written beneath the chosen output directory.

`verification/reproduction.json` records the source build and its tool versions;
`verification/hardware/rebuilt/` contains the resource, timing and DRC reports.
The accompanying board measurements remain bound to the qualified bit/HWH in
`runtime/package/`. Rebuilding is a host operation and does not program a board.
