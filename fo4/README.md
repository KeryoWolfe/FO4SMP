# FO4SMP

Faster HDT-SMP (FSMP) ported to Fallout 4. Target: Fallout 4 1.11.240 (Steam), F4SE, Address Library. Plan and audit: `../FO4_PORT.md`. Addresses: `ADDRESSES.md`.

## State: milestone 1 (written, not yet built)

The plugin loads on 1.11.240 only, writes `FO4SMP.log`, checks the addresses it will hook (one line each, `Address check: ... OK` or `FAILED`), looks for its config folder `Data\F4SE\Plugins\FO4SMP\`, and warns if FO4 Faster HDT-SMP is loaded too. **No hooks and no physics yet.**

## Build

Requirements: Visual Studio 2022 (C++ desktop workload), CMake 3.25+, Ninja, vcpkg with `VCPKG_ROOT` set, and the CommonLibF4 submodule:

```
git submodule update --init extern/CommonLibF4
cd fo4
cmake --preset release
cmake --build --preset release
```

Run from a "x64 Native Tools Command Prompt for VS 2022". The DLL is `fo4/build/release/FO4SMP.dll`. To copy it into an MO2 mod folder after each build, configure with `-DFO4SMP_DEPLOY_DIR="D:/Naked Commonwealth/mods/FO4SMP"`.

## Test (milestone 1)

1. Install as a mod (`F4SE\Plugins\FO4SMP.dll`). FO4 Faster HDT-SMP can stay enabled for this test: FO4SMP installs no hooks, and it should warn about it.
2. Start the game to the main menu, then load a save.
3. Open `Documents\My Games\Fallout4\F4SE\FO4SMP.log`. Expected:
   - `FO4SMP 0.1.0 loading on Fallout 4 1.11.240.0`
   - 22 `Address check: ... OK` lines (one says "weak match"), then `Address check passed`
   - `Another SMP plugin is loaded: FO4FasterHdtSMP-AVX2.dll ...` (if it is enabled)
   - `Game data ready`, then a `Config folder ... not found` warning (expected: no configs ship yet)
4. Send the log either way. A `FAILED` line means that address is wrong.
