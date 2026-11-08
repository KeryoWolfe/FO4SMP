# FO4SMP

Faster HDT-SMP (FSMP) ported to Fallout 4. Target: Fallout 4 1.11.240 (Steam), F4SE, Address Library. Plan and audit: `../FO4_PORT.md`. Addresses: `ADDRESSES.md`.

## State: milestone 3 (written, not yet built)

Milestones 1 and 2 passed (7 November 2026, `TEST_RESULTS.md`): the plugin loads on 1.11.240, checks its addresses, links FSMP's physics core, reads its configs, and steps the physics world every frame from the game's main loop.

Milestone 3 puts **SMP on the player's body**, with no armor hooks yet:

- Each frame, before the physics step, FO4SMP looks at the player's third-person 3D. When it changes (a save loads, the 3D is rebuilt), the old physics is dropped and, a second later, the new 3D is scanned.
- Every node carrying the `HDT Skinned Mesh Physics Object` string extra data names a physics XML (A-Body's `MaleBody.nif` names `Meshes\A-Body\body\MaleBody.xml`). `defaultBBPs.xml` can map shape names to an XML for meshes without it.
- FSMP's system builder (ported as `src/game/Fo4System.cpp`) reads the XML: bones, collision shapes from the mesh (`VirtualPenis`, `VirtualBelly`...), constraints. Fallout 4's skin data (bones, bind poses, vertices, triangles) is read from `BSSkin::Instance` / `BSSkin::BoneData` and the renderer's vertex buffer (`src/game/Fo4Skin.cpp`), each layout checked before use.
- Each frame: read the animated bones, step, and write the simulated bones back. FO4SMP steps after the game's frame, so it writes each simulated bone's **local** transform (the one that gives the simulated world pose under its parent): the game's next update keeps it. That puts the physics one frame behind the parent's own motion.
- If the outfit changes so the physics' shapes are no longer attached, the physics is dropped and the 3D scanned again.

Not yet: other actors, armor and clothing physics through the attach hooks (milestone 4), hair, Havok's reference pose when building (shapes are built from the current pose), FSMP's player-only exceptions (weapon drawn, first person), BA2-packed physics XML (loose files only).

## Build

Requirements: Visual Studio 2022 or 2026 (C++ desktop workload, which brings CMake and Ninja), vcpkg with `VCPKG_ROOT` set (Visual Studio's bundled vcpkg works). Dependencies are built with the newest MSVC installed (`cmake/triplets`; FSMP's own triplets pin 14.44, which VS 2026 doesn't have), so build from that Visual Studio's x64 Native Tools prompt.

```
git submodule update --init extern/CommonLibF4
cd fo4
cmake --preset release -DFO4SMP_DEPLOY_DIR="D:/Naked Commonwealth/mods/FO4SMP"
cmake --build --preset release
```

Run from the "x64 Native Tools Command Prompt" of the newest Visual Studio installed. The first configure builds FSMP's patched Bullet and oneTBB through vcpkg (several minutes). With `FO4SMP_DEPLOY_DIR` set, each build copies `F4SE\Plugins\FO4SMP.dll`, its PDB and `F4SE\Plugins\FO4SMP\configs.json` / `defaultBBPs.xml` into that folder; without it the DLL is `fo4/build/release/FO4SMP.dll` and the config files are in `fo4/package/`.

## Test (milestone 3)

Setup: FO4 Faster HDT-SMP **disabled**; FO4SMP enabled; the player uses A-Body (male) with its SMP physics.

1. Load a save. Have the A-Body body visible: naked, or in an outfit that keeps the body.
2. About a second after loading, the console should print `FO4SMP: physics on the player from Meshes\A-Body\body\MaleBody.xml (N bones simulated)`.
3. Move: walk, run, stop suddenly, jump, turn quickly, crouch. The SMP parts (genitals, belly, buttocks, breasts' soft tissue) should lag and settle, not stay rigid, stretch, fly off or vibrate.
4. Change outfit once (to something that hides the body, then back): the log should show the physics dropped and rebuilt.
5. Send `FO4SMP.log`, and the crash log if the game crashes. What to look for in it:
   - `Player physics: scanning the player's 3D ...`, then one `shape '...'` line per skinned shape, with its bone and vertex counts or the reason it can't be read;
   - `building Meshes\A-Body\body\MaleBody.xml from extra data on ...`, then `built in X ms: N bones (M simulated), K collision bodies`;
   - `Frame step` lines: the physics step time now that there's something to simulate.

If the log says `no physics found on the player`, the extra data didn't survive the outfit being attached: uncomment the A-Body line in `F4SE\Plugins\FO4SMP\defaultBBPs.xml` and try again.

## Checks that run without the game

- `tests/run-config-tests.sh`: FSMP's GlobalConfig suite plus a parse of the shipped `configs.json` (g++, rapidjson, doctest). Passes: 11 cases, 48 assertions.
- `tools/core-check/check-core-includes.sh <bullet3>`: the core needs no game headers.
- `tools/core-check/check-core-names.sh <bullet3>`: the core needs nothing from the PCH beyond what `src/PCH.h` supplies (MSVC-only SIMD errors ignored, as clang can't compile them).

`<bullet3>` is Bullet 3.25 with FSMP's patches: `cmake -DSOURCE_PATH=<bullet3> -P` on the patch section of `../cmake/ports/bullet3/portfile.cmake`.
