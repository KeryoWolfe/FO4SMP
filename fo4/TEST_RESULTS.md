# FO4SMP test results

## Milestone 1, run 3: 7 November 2026, 18:46 (FO4 Faster HDT-SMP disabled)

Same build and setup as run 2, FO4 Faster HDT-SMP **disabled**.

| Check | Expected | Actual | Status |
|---|---|---|---|
| Addresses | 24 OK | 23 OK, 1 HOOKED, 0 failed. `BSFaceGenNiNode` vtable slot 0x43 points at `SkinAllGeometry` | Pass |
| Rest (configs, default physics, core, console) | as run 2 | as run 2; empty step 0.014 ms | Pass |

The 9 functions FO4 Faster HDT-SMP had jumped over all compare OK with it off, so their addresses are confirmed. **`BipedAnim::AttachSkinnedObject` is still hooked**, by another plugin in the load order (its jump lands in trampoline memory, which this build couldn't name; the milestone 2 build follows trampolines to the module). Armor attach is where FO4SMP's own hook goes in milestone 4, so it will have to chain with that plugin's.

**Milestone 1: passed.**

## Milestone 1, run 2: 7 November 2026, 18:07 (owner's machine)

| | |
|---|---|
| Build | `fo4-port` at `0bc412b` (with the console fix `affa915`), VS 2026 / MSVC 14.51, vcpkg triplet `x64-windows-static-md-avx2` (fo4/cmake/triplets) |
| Game | Fallout 4 1.11.240 (Steam), F4SE `f4se_1_11_240.dll`, MO2, the owner's full load order |
| Other SMP | FO4 Faster HDT-SMP 1.31 (AVX2) **enabled** |
| Steps | Enable `FO4SMP` in MO2, launch through F4SE, load a save, open the console, read `FO4SMP.log` |

| Check | Expected | Actual | Status |
|---|---|---|---|
| Loads on 1.11.240 | `loading on Fallout 4 1.11.240.0` | as expected | Pass |
| Addresses | 24 checked, none failed | 14 OK, 10 HOOKED, 0 failed | Pass, partly: see below |
| Other SMP warning | names FO4 Faster HDT-SMP | `Another SMP plugin is loaded: FO4FasterHdtSMP-AVX2.dll` | Pass |
| configs.json | read, shipped values | read (847 bytes); 16 iterations, erp 0.2, 60 fps, 4 substeps, wind on | Pass |
| defaultBBPs.xml | read, 0 maps, 0 remaps | as expected | Pass |
| Physics core | world created, one empty step | 16 threads; empty step 0.016 ms | Pass |
| Console | one readable status line | `FO4SMP 0.1.0: addresses OK (10 hooked by another plugin), physics core running. No physics on actors yet (milestone 1).` | Pass |

**The 10 hooked addresses** are the functions FO4 Faster HDT-SMP patches: `SkinAllGeometry` (through vtable slot 0x43, named as `FO4FasterHdtSMP-AVX2.dll`), and through jumps into trampoline memory (so no module name): `BSFaceGenModelExtraData::SetBoneName`, `BSFaceGen::PrepareHeadPart`, `BipedAnim` `ApplySkinnedObjects` / `AttachSkinnedObject` / `AttachToParent` / `RemovePart`, `LooksMenuUtils::ShowLooksMenu`, `Reset3D`, `TESObjectREFR::FixDisplayedHeadParts`. That FO4 Faster HDT-SMP hooks those same addresses agrees with them, but their code wasn't compared. **Still to do:** one run with FO4 Faster HDT-SMP disabled, expecting 24 OK.

## Milestone 1, run 1: 7 November 2026, 17:54

Same setup, build before `affa915` and `0bc412b`. Physics core, configs and default physics as in run 2. Two faults, both fixed:

- The console showed "5" and four boxes: CommonLibF4's `ConsoleLog::PrintLine` passes a `va_list` to a function that is variadic in 1.11.240 (fixed in `affa915`).
- The same 10 addresses were reported FAILED: the check didn't recognise another plugin's hooks (fixed in `0bc412b`).

## Build history (7 November 2026)

1. vcpkg: FSMP's triplets pin MSVC 14.44; VS 2026 has 14.51 (fixed: fo4/cmake/triplets, unpinned).
2. Configure: CommonLibF4 needs `rapidcsv` (added to vcpkg.json).
3. Compile: `hdtSkinnedMeshWorld.cpp(76)` needed FSMP's smart-pointer-to-raw-pointer `operator==` (added to the PCH).
4. Built and linked, no warnings in the log.
