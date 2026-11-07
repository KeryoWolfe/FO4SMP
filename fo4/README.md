# FO4SMP

Faster HDT-SMP (FSMP) ported to Fallout 4. Target: Fallout 4 1.11.240 (Steam), F4SE, Address Library. Plan and audit: `../FO4_PORT.md`. Addresses: `ADDRESSES.md`.

## State: milestone 1 (written, not yet built)

On 1.11.240 only, the plugin:

- writes `FO4SMP.log`;
- checks the 24 game addresses later milestones hook (one `Address check` line each);
- warns if FO4 Faster HDT-SMP is loaded too;
- links FSMP's physics core (Bullet, the skinned-mesh world) and creates the physics world;
- reads `F4SE\Plugins\FO4SMP\configs.json` (and `userConfigs.json` on top, if present) into it, as FSMP does;
- reads `F4SE\Plugins\FO4SMP\defaultBBPs.xml`;
- steps the empty physics world once and logs the time;
- prints its version and status in the console after a save loads.

**No hooks and no physics on actors yet.** The config's `logLevel` isn't applied in this milestone (the shipped 0 would hide the test's own lines).

## Build

Requirements: Visual Studio 2022 (C++ desktop workload, MSVC 14.44, which FSMP's triplets pin), CMake 3.25+, Ninja, vcpkg with `VCPKG_ROOT` set.

```
git submodule update --init extern/CommonLibF4
cd fo4
cmake --preset release -DFO4SMP_DEPLOY_DIR="D:/Naked Commonwealth/mods/FO4SMP"
cmake --build --preset release
```

Run from an "x64 Native Tools Command Prompt for VS 2022". The first configure builds FSMP's patched Bullet and oneTBB through vcpkg (several minutes). With `FO4SMP_DEPLOY_DIR` set, each build copies `F4SE\Plugins\FO4SMP.dll`, its PDB and `F4SE\Plugins\FO4SMP\configs.json` / `defaultBBPs.xml` into that folder; without it the DLL is `fo4/build/release/FO4SMP.dll` and the config files are in `fo4/package/`.

## Test (milestone 1)

1. Enable the `FO4SMP` mod in MO2. FO4 Faster HDT-SMP can stay enabled: FO4SMP installs no hooks, and it should warn about it.
2. Start the game and load a save.
3. In the console, expect one line: `FO4SMP 0.1.0: addresses OK, physics core running. No physics on actors yet (milestone 1).`
4. Open `Documents\My Games\Fallout4\F4SE\FO4SMP.log`. Expected, in order:
   - `FO4SMP 0.1.0 loading on Fallout 4 1.11.240.0`
   - 24 `Address check: ... OK` lines (one says "weak match"), then `Address check passed`
   - `Another SMP plugin is loaded: FO4FasterHdtSMP-AVX2.dll ...` (if it is enabled)
   - `Game data ready`
   - `Config: read Data/F4SE/Plugins/FO4SMP/configs.json`, `Config: no ...userConfigs.json`, five `Config:` lines with the values (`solver numIterations=16 erp=0.2 min-fps=60 maxSubSteps=4`, ...)
   - `Default physics: read ...defaultBBPs.xml: 0 shape map(s), 0 remap(s)`
   - `Physics simulation is using N threads` (FSMP's own line), then `Physics world created: 16 solver iterations, erp 0.2, time tick 0.0167 s (60 fps), up to 4 substeps, wind on`
   - `Physics world: one empty step took ... ms`
   - `Console: printed "..."`
5. Send the log either way. A `FAILED` line names the address that's wrong; `Physics world could not start` means the core failed.

## Checks that run without the game

- `tests/run-config-tests.sh`: FSMP's GlobalConfig suite plus a parse of the shipped `configs.json` (g++, rapidjson, doctest). Passes: 11 cases, 48 assertions.
- `tools/core-check/check-core-includes.sh <bullet3>`: the core needs no game headers.
- `tools/core-check/check-core-names.sh <bullet3>`: the core needs nothing from the PCH beyond what `src/PCH.h` supplies (MSVC-only SIMD errors ignored, as clang can't compile them).

`<bullet3>` is Bullet 3.25 with FSMP's patches: `cmake -DSOURCE_PATH=<bullet3> -P` on the patch section of `../cmake/ports/bullet3/portfile.cmake`.
