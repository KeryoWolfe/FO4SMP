# FO4SMP test results

## Milestone 2: 7 November 2026, 19:14 (FO4 Faster HDT-SMP disabled)

| | |
|---|---|
| Build | `fo4-port` at `ee4c59c` (0.2.0), VS 2026 / MSVC 14.51 |
| Game | Fallout 4 1.11.240 (Steam), F4SE, MO2, the owner's full load order; Ryzen 7 2700X, RTX 5070 |
| Steps | Load a save, play about five minutes (walking, Pip-Boy, a cell change), read `FO4SMP.log` |

| Check | Expected | Actual | Status |
|---|---|---|---|
| Hook installed | at the main loop's call to `Main::OnIdle`, +0C315BB | `Frame hook: installed at ... (+0C315BB)`; no other plugin had the call | Pass |
| Stepping starts | after the physics world exists | `Frame step: enabled`, first frame 16 ms later | Pass |
| Once per frame | logged fps close to the game's | 28 to 60 fps logged per 10 s; the owner saw 25 to 45 in game | Pass |
| Step time (empty world) | well under 1 ms | average 0.008 to 0.011 ms; longest 0.548 ms | Pass |
| Stability | no stutter, hang or crash from FO4SMP | none reported | Pass |
| Console | readable, milestone 2 | `FO4SMP 0.2.0: addresses OK (1 hooked by another plugin), physics core running, frame step hooked...` | Pass |

Notes:

- **The first report** (292 frames in 79.6 s, 3.7 fps) covers the save's loading screen: the main loop calls `Main::OnIdle` only a few times a second while loading. The 0.25 s cap keeps a step after a load short.
- **`BipedAnim::AttachSkinnedObject` is hooked by `f4ee.dll`** (LooksMenu), named now that the check follows trampolines. LooksMenu re-applies body morphs and overlays when armor attaches. FO4SMP's own armor-attach hook (milestone 4) must chain with it.
- The logged fps is per 10 s and includes menu time (the Pip-Boy runs faster), so it reads a little higher than the in-game counter.

**Milestone 2: passed.**

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

## Milestone 3, runs 1 and 2 (7 November 2026)

- Run 1: all 40 skinned shapes on the player read (A-Body body, `Virtual*` shapes, head, hair), but no physics built: the `HDT Skinned Mesh Physics Object` extra data on `MaleBody.nif`'s root is gone once the outfit is attached (as in Skyrim; read in the attach hook in milestone 4). Fixed for now by mapping `BaseMaleBody:0Full` in the shipped `defaultBBPs.xml`.
- Run 2: `MaleBody.xml` built (110 bones, 22 simulated, 11 collision bodies; rebuilt after an outfit change, 140 bones), physics step 3.5 ms average. In game: huge stretched triangles from the body. Cause found in the engine: Fallout 4 stores rotation matrices transposed relative to Skyrim (its NiTransform multiply, 1.11.240 +0344820, computes world.rotate = local.rotate * parent.rotate), so every rotation handed to Bullet was inverted. Fixed in `Fo4ConvertNi.cpp` and `Fo4NiUtils.h` (checked numerically against the disassembled multiply). Also: the build stalled the game 2.5 s, mostly uncached RTTI checks in node searches; now cached per vtable.
- Run 3 (rotation fix): build now 17 ms; the body mostly holds, but simulated bones still get flung now and then (long beams from the shoulders and thighs), with physics steps of 80-110 ms at those moments. Checked the skin data convention: `BSSkin::BoneData` transforms are loaded by the same NiTransform loader as node transforms (1.11.240 +16B6CE0, which transposes the file's matrix), so they need no extra handling. Cause found instead: FO4SMP's physics world never applied FSMP's `SkyrimPhysicsWorld` constructor settings: gravity (Bullet's default is along -Y, 10 units/s²), friction 0, split impulse, ERP2, CFM, no warm starting. Ported.
- Run 4 (sunlight video): the face beam is SMP Head's head physics (`AHead\XML\Lewd_Head.xml`), which FO4SMP doesn't build yet (head parts, milestone 5). Body still shows beams, a light band down the torso and a flap at the hip.
- Run 5 (guard build): A-Body's SMP bones are parented to `Root` (at the feet), so the 100-unit guard rejected normal poses (penis bones about 105 units above Root) thousands of times. Real explosions also logged: Penis_Offset_SMP 222,000 units, breasts and belly 11,000-75,000 units, a thigh bone 120,000 units. Guard widened to 500 units; geometry dump added (collision bodies and simulated bones in the root's frame) to find what they collide with.
- Run 7 (geometry dump from skin data): skin data reads plausibly (VirtualGround's skin-space box is a 135-unit square at the Root bone's bind height; per-bone bind positions sensible). But A-Body's *_SMP bone nodes sat within a few units of the Root bone (at the feet) instead of at body height, and the Root bone's copy inside FSMP was 70,000 units from its node (at the world origin), dragging every body weighted to it (VirtualGround, parts of the others) far away. Likely cause: FO4SMP's `updateTransformUpDown` recomputed the whole skeleton's world transforms from NiNode::local at build and reset, but Fallout 4 drives skeleton bones through BSFlattenedBoneTree, so those locals aren't the engine's. Made it a no-op.
- Runs 9-11 (added-bone placement): A-Body's bones that the skeleton lacks are attached under Root with an identity local transform, so they sat at the feet. FO4SMP now places each skinned one from the bind data beside the nearest real bone (Root's real child is COM_Offset). Run 11: 23 bones moved (breasts 106 units, buttocks/belly/genitals 60-72, thighs 54-56); the body now looks like FO4 Faster HDT-SMP's in the user's comparison video, apart from SMP Head's face beam. Remaining: genital bones still get flung now and then (up to about 490 units, a few dozen rejected poses per 10 s); physics step 3-8 ms average. Next: place the simulated bones' kinematic partners (Penis_01, Penis_Offset...), which have no vertices and so no bind data.
- Run 12 (freeze.txt, physics built but not stepped; the user's comparison with FO4 Faster HDT-SMP had shown a twisted torso, pecs sunken/swung out, testicles above the penis, twisted thighs): with nothing simulated, the penis was gone and the buttocks bulged; every placed bone read exactly 120 degrees from its rest pose, i.e. a zero rotation matrix (trace 0). Cause: `Inverse()` used `RE::NiTransform{}` as the identity, but CommonLibF4's NiTransform has no constructor, so its rotation is zero; every bone placed from the bind data got a zero rotation (positions were right), and the physics started from garbage orientations. Fixed with an explicit identity.
- Run 13 (identity fix; frozen and running shots against FO4 Faster HDT-SMP's): the body now matches closely, frozen and running (user: "so close"; abs and front thigh muscles slightly softer than Bingle's). Big remaining issue: the penis goes rogue while moving. Likely cause: A-Body's kinematic added bones (Penis_01_Offset > Penis_01, Penis_Balls_01, Anus_00_SMP > Anus_0x) hang off Root, so they follow the feet instead of the hips, and the simulated genital bones are tied to them. They now follow the nearest real bone each frame.
- Runs 14-15 (follow fix; Release /Ob3 build; worldonly.txt): the follow fix caught only Anus_00_SMP (the genitals' partners already hang off real bones), so it isn't the penis's problem. With worldonly.txt the buttocks still jiggle while running: the frame is drawn from FO4SMP's world writes, so the Root-parenting lag hypothesis is ruled out. Release /Ob3 barely changed the cost: 2.4-2.6 ms standing, 12-17 ms running (about 21-24 fps), with penis bones flung 300-480 units and 7-41 rejected poses per 10 s while running. Suspect: BSSkin::BoneData bounding spheres used as bone-space spheres by FSMP's collision mid-phase; spheres are now computed from each bone's vertices (bone space), and the log compares the skin data's centers in both spaces.
- Run 16 (bone spheres from vertices): BSSkin::BoneData's sphere centers were already in bone space (0.1-5 units from the vertices' centers; 21-80 units off as mesh space), so not the cause. Still 11-14 ms per step and 15-48 rejected poses per 10 s while running, 2.4-2.9 ms standing with none. Added a contact report (bone pairs with the most contacts and the deepest, per 10 s) to see what the genitals collide with while running.
- Run 19 (weights normalized): the count showed the real fault: almost every vertex of A-Body's Virtual* collision shapes had weight on bones FO4SMP didn't know (VirtualPecs: all 430 short, up to 99.8%; VirtualArms: 924 of 1,116 with no known bone; VirtualHands 1,418; VirtualLegs 899). The skin's bone list has null entries for these undrawn shapes, while the world-transform pointers are filled; FSMP's code skips null bones, so the collision surfaces were built from the few known bones (until run 18, collapsed toward the frame origin; with normalization, distorted: the arm surface now sits in the belly, 600 contacts per 10 s, "lower abs freaking out"). This is very likely what has been driving the genital explosions and the standing twitch. Null entries are now resolved through the world-transform pointer (NiAVObject::world at +0x70, checked by RTTI); the log counts resolved and unknown bones per shape.
- Run 20 (null-bone resolution): no null bone had a world pointer either (0 resolved; VirtualHands 36 of 38 unknown, VirtualArms 18 of 28, VirtualPecs 11 of 14), so nothing changed; with normalized weights the shapes sat on the few known bones, Belly_skin_SMP was stuck 413 units out and about 1,900 poses per 10 s were rejected; a big sheet appeared after stopping. Run 2's second build (after an outfit change) had found bones its first build hadn't, so the game likely fills these entries in later: FO4SMP now waits (up to 30 s) until the shapes it builds from have no unset bones, and logs how long it took.
- Run 21 (prepared, not run): the null skin bones are now filled by name from the outfit's NIF. A minimal reader for FO4 NIFs (20.2.0.7, BS 130: header, block types/sizes, string table; BSTriShape family -> BSSkin::Instance bone refs -> node names) was checked offline against the user's A-Body MaleBody.nif: 15 skinned shapes, every Virtual* shape's bone list read (e.g. VirtualGround: LArm_Collarbone_skin, Chest_skin, Neck_skin, Neck_Low_skin, Root; VirtualHands 38 bones). The NIF is found from the physics XML's name (A-Body: Meshes\Actors\Character\CharacterAssets\MaleBody.nif) until the attach hook knows the model path.
- Run 21 (NIF bone names): all 92 null skin bones of the 11 collision shapes found by name, 0 missing; the weight-sum warnings are gone, and every collision shape now sits where the body is (pecs ~100 up, belly ~79, legs 10-63, hands ~63 at the sides, penis ~61). Contacts are now plausible (fingers against thighs, buttocks). Still wrong: the penis flips between poses from frame to frame even standing (frame-by-frame crops), with 8-123 rejected poses per 10 s and penis bones flung up to ~490 units; it rarely appears in the contact lists, so it looks like the constraint chain itself. Removed run 13's "kinematic partner" move (it shifted Penis_Offset 2.8 units off the place A-Body's constraints were authored against); the dump now logs each simulated bone's mass, inverse inertia, scales and speed, over the first two minutes.
- Run 22 (partner move removed; mass/inertia dump): masses, inertias and scales are all as A-Body's XML says (scale 1.000 everywhere), but the penis segments move at 15-170 units/s while standing, the tip fastest: the chain itself is unstable. Without the partner move, rejections jumped from 8-123 to 2,600-2,900 per 10 s and a buttock sank into the floor, so the move is restored. Rejection now puts every simulated bone back on its rest pose beside its anchor (instead of resetting one bone onto its last pose), and the log counts those resets.
