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

Requirements: Visual Studio 2022 or 2026 (C++ desktop workload, which brings CMake and Ninja), vcpkg with `VCPKG_ROOT` set (Visual Studio's bundled vcpkg works). Dependencies are built with the newest MSVC installed (`cmake/triplets`; FSMP's own triplets pin 14.44, which VS 2026 doesn't have), so build from that Visual Studio's x64 Native Tools prompt.

```
git submodule update --init extern/CommonLibF4
cd fo4
cmake --preset release -DFO4SMP_DEPLOY_DIR="D:/Naked Commonwealth/mods/FO4SMP"
cmake --build --preset release
```

Run from the "x64 Native Tools Command Prompt" of the newest Visual Studio installed. The first configure builds FSMP's patched Bullet and oneTBB through vcpkg (several minutes). With `FO4SMP_DEPLOY_DIR` set, each build copies `F4SE\Plugins\FO4SMP.dll`, its PDB and `F4SE\Plugins\FO4SMP\configs.json` / `defaultBBPs.xml` into that folder; without it the DLL is `fo4/build/release/FO4SMP.dll` and the config files are in `fo4/package/`.

## Test (milestone 1)

1. Enable the `FO4SMP` mod in MO2. FO4 Faster HDT-SMP can stay enabled (FO4SMP installs no hooks, and warns about it), but for a full address check run once with it disabled.
2. Start the game and load a save.
3. In the console, expect one line: `FO4SMP 0.1.0: addresses OK, physics core running. No physics on actors yet (milestone 1).`
4. Open `Documents\My Games\Fallout4\F4SE\FO4SMP.log`. Expected, in order:
   - `FO4SMP 0.1.0 loading on Fallout 4 1.11.240.0`
   - 24 `Address check:` lines, then a summary (`N OK, N hooked by another plugin, 0 failed`) and `Address check passed`. With FO4 Faster HDT-SMP enabled, the functions it hooks show as `HOOKED by FO4FasterHdtSMP-AVX2.dll` (its jumps replace their first bytes, so they can't be compared); with it disabled, all 24 should be `OK`.
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
