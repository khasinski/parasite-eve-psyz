# PSY-Z

## Reviewed room-atlas coverage

The SDL GPU renderer accepts optional `<atlas-key>-matte.bin` metadata next to a
room atlas. This affects scaled display coverage only; native VRAM, PS1 blend
classification and semi-transparent primitives retain their original rules.

`PEM1` is exactly four ASCII bytes and selects the whole atlas, preserving the
existing format. `PEM2` starts with its four ASCII bytes followed by a cell-selection
bitmap: exactly `ceil(columns * rows / 8)` bytes, with bit 0 selecting cell 0,
bit 1 selecting cell 1, and cells numbered in material-atlas row order. Unused
high bits in the last byte must be zero. Empty masks, truncated data, trailing
bytes and out-of-range bits fall back to unmarked coverage without partially
applying a selection. No dimensions or lengths are taken from the marker. Each material cell is 68x68
texels including its gutter; the paired reference plane does not add bitmap cells.

At least one material cell must contain authored alpha. Selected static cells
may cross a native staircase only with a reviewed, coherent backdrop. Unselected
cells retain their own original alpha policy; fully opaque unmarked cells keep
legacy feathering. Live-palette witnesses continue to guard black reference
pixels. These markers are not blanket permission to extend moving silhouettes.
Review every animation pose behind an affected static foreground. GPU checkpoint
resource identities include selective masks, so a different selection cannot be
restored as the same material configuration.

## Running Tests

Automated tests to ensure no regressions are introduced between changes.

### Linux

```bash
# run tests + GTE tests using hardware acceleration
make test

# run Windows tests using mingw32 and wine
make test-wine

# consoles and Android, emulated or on real hardware
make test-psp-emu test-psp-hw
make test-ps1-emu test-ps1-hw
make test-android
```

The tests are written with [ztest](../ztest/README.md). Pass options straight to the runner, for example
`python3 ../ztest/zrunner.py native --exe tests/build/sdl3-gpu-hw/psyz_tests --workdir tests --filter='gpu::*'`.
When an image comparison fails, the frame is written to `tests/expected/<name>.<target>.actual.png`.

### Windows

```batch
test.bat
```

Runs tests using Visual Studio as the CMake generator.
