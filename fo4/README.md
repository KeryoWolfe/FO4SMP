# FO4SMP (developer notes)

Faster HDT-SMP (FSMP) ported to Fallout 4 1.11.240 (Steam) with F4SE and Address Library. This folder holds the
Fallout 4 build; FSMP's physics core (`../src/hdtSkinnedMesh`, XML reader, configs, patterns, the validator's XSD
check) is used from the upstream tree unchanged apart from one compile switch and the schema path.

- Player documentation (shipped in the archive): `package/F4SE/Plugins/FO4SMP/README.md`, `CREDITS.md`.
- Changes: `CHANGELOG.md`. Tests: `TEST_PLAN.md` (what to run) and `TEST_RESULTS.md` (every run, with log evidence).
- Addresses and how each was verified: `ADDRESSES.md` (plus `src/Offsets.h`, checked by `src/AddressCheck.cpp` at load).
- Plan and audit: the claude.ai project doc `FSMP_FO4_PORT.md` and `../FO4_PORT.md`.

## State: V1 release candidate (milestones 1-5 done)

| Milestone | |
|---|---|
| 1. Loads | Address check, configs, physics world. |
| 2. Frame step | Hook on the main loop's call to `Main::OnIdle`; physics stepped every frame, paused with the game (`UI::numPauseGame`). |
| 3. A-Body on the player | FSMP's system builder ported (`src/game/Fo4System.cpp`), skin data from `BSSkin::Instance` / `BoneData` and the vertex buffer, outfit-added bones placed from the NIF. |
| 4. Armor and actors | Call-site hooks on `BipedAnim::AttachSkinnedObject`; NPC selection by distance with hysteresis, placement tier, staged quarantine of exploding NPCs, culling by camera view, reset on save load. |
| 5. Heads, hair, scripts, settings | Per-actor face bone nodes and head part physics (SMP Head, SMP hair), BA2 reading, DynamicHDT natives with co-save persistence, MCM page, XSD validation. |
| 6. Release | This document set, quiet logging (verbose switch), release archive target. Open: an MSVC build of the `fo4smp/combined` CommonLibF4, the test plan's full pass. |

Open problems: penis-vs-legs collision instability (stopgap override), physics step cost with dozens of NPCs.

## Layout

| Path | |
|---|---|
| `src/main.cpp` | Plugin entry, logging, F4SE messages (game data ready, save loading). |
| `src/FrameHook.*` | Main loop hook, pause detection, step and timing report. |
| `src/AttachHook.*` | Armor attach call sites; per-actor, per-slot physics attach records. |
| `src/PlayerPhysics.*` | Actors: selection, building, placement, faces, culling, explosions, DynamicHDT back end, settings. |
| `src/PhysicsWorld.*` | FSMP's world driver, contact diagnostics. |
| `src/Papyrus.*`, `src/Settings.*`, `src/Validation.*` | DynamicHDT natives and co-save, MCM settings file, XSD check. |
| `src/game/` | Fallout 4 layer: system builder, bones, bodies, skin reading, NIF reader, node creation, resource streams. |
| `package/` | Everything copied into the mod folder: configs, schema, docs, licences, MCM page, `DynamicHDT.psc`. |

## Build

Requirements: Visual Studio 2022 or 2026 (C++ desktop workload, with CMake and Ninja), vcpkg with `VCPKG_ROOT` set.
Build from the newest Visual Studio's x64 Native Tools prompt (`cmake/triplets` builds dependencies with it).

```
git submodule update --init --recursive
cd fo4
cmake --preset release -DFO4SMP_DEPLOY_DIR="<mod folder>" -DFO4SMP_FALLOUT4_DIR="<Fallout 4 folder>"
cmake --build --preset release
```

- `extern/CommonLibF4` is KeryoWolfe/CommonLibF4, branch `fo4smp/combined`, with two submodules of its own
  (commonlib-shared, dearmoddingui-api). After pulling a change of submodule URL: `git submodule sync --recursive`.
- `FO4SMP_DEPLOY_DIR` (optional): each build copies the DLL, PDB and `package/` there.
- `FO4SMP_FALLOUT4_DIR` (optional): the game folder with the Creation Kit's `Papyrus Compiler` and
  `Data\Scripts\Source` (with `Base`); compiles `DynamicHDT.pex`, copied into the deploy folder's `Scripts`.
- Release archive: `cmake --build --preset release --target release_zip` writes `build/release/FO4SMP-V<n>.zip`
  (mod folder layout) and `FO4SMP-V<n>-pdb.zip`. Releases are V1, V2...; a fix after a release adds .1 (V1.1): set `FO4SMP_RELEASE_NAME` and the numeric `VERSION` (1.1.0) in `CMakeLists.txt`.

The first configure builds FSMP's patched Bullet, oneTBB and spdlog (with `wchar`) through vcpkg.

## Checks without the game

- `tests/run-config-tests.sh`: FSMP's GlobalConfig suite plus a parse of the shipped `configs.json`.
- `tools/core-check/`: the physics core needs no game headers and nothing from the PCH beyond `src/PCH.h`.
- Syntax check against CommonLibF4: clang `-target x86_64-w64-windows-gnu -fsyntax-only` with MinGW's headers over
  every `src` file (see `TEST_RESULTS.md`, library swap). MSVC-only SIMD code in Bullet and FSMP's helpers can't be
  checked this way.

## Logging

`Documents\My Games\Fallout4\F4SE\FO4SMP.log`. By default: startup, builds, drops, selections, problems (a warning
repeated word for word is logged once), a timing line a minute. The MCM switch "Verbose Log" (`bVerboseLog` in
`MCM\Settings\FO4SMP.ini`) adds debug lines (every bone and constraint built) and the diagnostics from milestones
3-5 (geometry dumps, motion tracking, the player's bounds watch, every attach).
