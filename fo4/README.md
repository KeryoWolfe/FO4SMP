# FO4SMP

Faster HDT-SMP (FSMP) ported to Fallout 4. Target: Fallout 4 1.11.240 (Steam), F4SE, Address Library. Plan and audit: `../FO4_PORT.md`. Addresses: `ADDRESSES.md`.

## State: milestone 2 (written, not yet built)

Milestone 1 (tested 7 November 2026, `TEST_RESULTS.md`), on 1.11.240 only:

- writes `FO4SMP.log`; checks the 24 game addresses later milestones hook (one `Address check` line each);
- warns if FO4 Faster HDT-SMP is loaded too;
- links FSMP's physics core and creates the physics world; reads `F4SE\Plugins\FO4SMP\configs.json` (and `userConfigs.json`) and `defaultBBPs.xml`; steps the empty world once;
- prints its status in the console after a save loads.

Milestone 2 adds **the first hook**: the game's main loop calls `Main::OnIdle` once per iteration, and FO4SMP redirects that one call to its own function, which calls `Main::OnIdle` (or whatever was there) and then steps the physics world through the frame's time. The world is still empty: **nothing is attached to actors yet**. Every 10 seconds the log reports frames, frame rate and the physics step's average and longest time.

## Build

Requirements: Visual Studio 2022 or 2026 (C++ desktop workload, which brings CMake and Ninja), vcpkg with `VCPKG_ROOT` set (Visual Studio's bundled vcpkg works). Dependencies are built with the newest MSVC installed (`cmake/triplets`; FSMP's own triplets pin 14.44, which VS 2026 doesn't have), so build from that Visual Studio's x64 Native Tools prompt.

```
git submodule update --init extern/CommonLibF4
cd fo4
cmake --preset release -DFO4SMP_DEPLOY_DIR="D:/Naked Commonwealth/mods/FO4SMP"
cmake --build --preset release
```

Run from the "x64 Native Tools Command Prompt" of the newest Visual Studio installed. The first configure builds FSMP's patched Bullet and oneTBB through vcpkg (several minutes). With `FO4SMP_DEPLOY_DIR` set, each build copies `F4SE\Plugins\FO4SMP.dll`, its PDB and `F4SE\Plugins\FO4SMP\configs.json` / `defaultBBPs.xml` into that folder; without it the DLL is `fo4/build/release/FO4SMP.dll` and the config files are in `fo4/package/`.

## Test (milestone 2)

1. **Disable FO4 Faster HDT-SMP** in MO2 for this test (it may hook the same main-loop call; FO4SMP would chain to it, but the timing is clearer alone). Enable `FO4SMP`.
2. Start the game, load a save, then play normally for a minute or two: walk around, open and close the Pip-Boy, change cells once.
3. Console, after the save loads: `FO4SMP 0.2.0: addresses OK, physics core running, frame step hooked. No physics on actors yet (milestone 2).`
4. In `FO4SMP.log`, expect:
   - `Frame hook: installed at the main loop's call to Main::OnIdle (+0C315BB)`, right after `Address check passed`
   - `Frame step: enabled` after `Physics world: one empty step ...`
   - `Frame step: first frame seen; stepping from the next one`
   - every 10 seconds: `Frame step: N frames in 10.0 s (X fps); physics step average ... ms, longest ... ms`
5. Things to compare: the logged fps against what the game shows (they should be close, if `Main::OnIdle` runs once per frame), and the step times (they should stay well under a millisecond with an empty world). Also any stutter, hang or crash that doesn't happen with FO4SMP disabled.
6. If something else hooked the same call first, the log says `Frame hook: the main loop's call already points at ...` and FO4SMP calls it in turn.

## Checks that run without the game

- `tests/run-config-tests.sh`: FSMP's GlobalConfig suite plus a parse of the shipped `configs.json` (g++, rapidjson, doctest). Passes: 11 cases, 48 assertions.
- `tools/core-check/check-core-includes.sh <bullet3>`: the core needs no game headers.
- `tools/core-check/check-core-names.sh <bullet3>`: the core needs nothing from the PCH beyond what `src/PCH.h` supplies (MSVC-only SIMD errors ignored, as clang can't compile them).

`<bullet3>` is Bullet 3.25 with FSMP's patches: `cmake -DSOURCE_PATH=<bullet3> -P` on the patch section of `../cmake/ports/bullet3/portfile.cmake`.
