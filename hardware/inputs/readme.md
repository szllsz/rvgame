# Native800 hardware reconstruction

The frozen recipe targets xc7z020clg400-2, native 800x600 output, and the
105 MHz CPU profile. Use Vivado 2025.2.1 with the existing IP versions.
The historical system640 names are retained for compatibility.

From the repository root, run:

```sh
python hardware/rebuild.py --destination /path/to/new-output --vivado /path/to/Vivado/2025.2/bin/vivado
```

Use a new destination; no original project, old DCP or tool installation is
included. The script reconstructs the project/BD/IP, implements it and exports
bit/XSA/HWH. Frozen acceptance gates remain active. No board is contacted.
See ../recipe.json and ../../verification/hardware for qualified and cold-build
records. Newly generated bits require separate board qualification. The
previous 2024.2/640 instructions described an older source export and do not
describe this selected recipe.
