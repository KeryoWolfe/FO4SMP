# FSMP for Fallout 4: port audit and plan

Written 7 November 2026. Branch `fo4-port` of github.com/KeryoWolfe/FO4SMP, a fork of github.com/DaymareOn/hdtSMP64 (Faster HDT-SMP, "FSMP") from upstream `dev` at `e52ad96` (26 August 2026), with upstream's full history. A project of its own, separate from 4Stim. Nothing here has been built or run.

## Source of truth

| | |
|---|---|
| Upstream | DaymareOn/hdtSMP64, branch `dev`, `e52ad96`. Skyrim SE / AE / VR. |
| Licence | GPL-3 with the same two exceptions 4Stim uses (modding exception; linking exception for modding libraries). A Fallout 4 port must stay GPL-3 and ship its source. |
| Build | CMake + vcpkg (overlay port for a patched Bullet 3), MSVC. Dependencies: CommonLibSSE (submodule), SKSE Menu Framework, nifly, Bullet 3, oneTBB, DirectXTK, fmt, spdlog, pugixml, rapidjson, rapidcsv, detours, xbyak, miniz, doctest. |
| Size | About 24,600 lines of C++ in `src/` (plus `Validator/`, `XmlInspector/`). |
| Target | Fallout 4 1.11.240 (Steam), F4SE, Address Library, CommonLibF4 (alandtse fork, `4379567`, 28 October 2026), as 4Stim. |

## Prior work: an FSMP port for Fallout 4 already exists

**FO4 Faster HDT-SMP** (Nexus 107209, by Bingle, July 2026) is a port of FSMP 3.30. Its author says it was tested on 1.10.163 and AE 1.11.221 and will not support next-gen (1.10.984). No source or licence is published. FSMP is GPL-3, so a binary derived from it must come with its source on request. Asking Bingle for it is the fastest route: it would carry the Fallout 4 addresses, the skinning code and the hooks this plan has to find from scratch. The binary itself is not used as a source: decompiled code isn't a substitute for the source the GPL entitles us to.

**An earlier, unfinished port:** github.com/ericncream/hdtSMP64 (ericnam808, March to May 2020), from aers' fork, MIT licence (HydrogensaysHDT, 2019). Built on raw F4SE for 1.10.163. Its last commit reports headless bodies "due to lack of skinning", so the skinning rewrite was never finished. Its `Offsets.h` names 1.10.163 addresses for the game loop, game shutdown, armor attach, `BSFaceGenNiNode::SkinAllGeometry` / `SkinSingleGeometry`, `TESNPC::GetFaceGeomPath` and the FaceGen bone limit. These are leads, not facts: Address Library's 1.10.163 offsets give their IDs, and each must be checked in 1.11.240 before use.

## What the code is made of

Engine ties counted per file (`RE::`, `SKSE::`, `REL::` uses).

| Part | Files | Engine ties | Class |
|---|---|---|---|
| Physics core: bodies, shapes, collision, constraints, world (`hdtSkinnedMesh/`) | 30 | Only `BSFixedString`, `BSTSmartPointer`, `BSIntrusiveRefCounted` | **Reusable.** CommonLibF4 defines all three under the same names. |
| Patched Bullet 3 (non-Hookean springs, oneTBB, profiler gate), `cmake/ports/bullet3` | overlay port | none | **Reusable** as is. |
| XML loading, pattern expansion, config (`XmlReader`, `Patterns/`, `XmlInspector/`, `config`, `GlobalConfig`) | ~10 | none | **Reusable.** The physics XML format stays the same, so Fallout 4 SMP files keep working. |
| Validator (`smp report`) | 38 | 5 files | **Adaptable.** Reads NIFs through nifly, which handles Fallout 4 NIFs; the skin-instance checks need Fallout 4's `BSSkin::Instance` / `BSSkin::BoneData`. |
| Mesh and skeleton binding (`hdtSkyrimSystem`, `hdtSkyrimBone`, `hdtSkyrimBody`, `hdtConvertNi`) | 8 | ~110 | **Rewrite.** Reads skinning from `NiSkinInstance` / `NiSkinData` / `NiSkinPartition`; Fallout 4 skins with `BSSkin::Instance` and `BSSkin::BoneData` on `BSTriShape` / `BSSubIndexTriShape`. |
| Actor tracking (`ActorManager`) | 2 | ~165 | **Rewrite.** Armor attach / detach, head parts, skeleton scans, culling. |
| Game hooks (`Hooks`) | 2 | ~70 | **Rewrite.** Every address is Skyrim's (see below). |
| Physics world driver (`hdtSkyrimPhysicsWorld`), wind (`WeatherManager`) | 4 | ~25 | **Adaptable.** Logic portable; the frame source, camera and weather calls change. |
| Papyrus (`DynamicHDT` functions) | 2 | ~40 | **Adaptable.** Fallout 4's Papyrus binds natives differently (F4SE's VM interface). |
| Save data (`hdtSerialization`) | 2 | ~10 | **Adaptable.** F4SE has a serialization interface. |
| In-game menu (`UI/FSMPMenu`, SKSE Menu Framework) | 4 | few | **Rewrite or drop.** No Fallout 4 equivalent of SKSE Menu Framework; MCM (ini/json) is the candidate, as 4Stim. |
| Entry point, messaging, console commands (`main.cpp`) | 1 | ~45 | **Rewrite** against F4SE's interfaces. |

## The Skyrim hooks and what each needs in Fallout 4

All are `REL::VariantID` (SE, AE, VR) addresses; none carries over. Each needs the matching Fallout 4 function found in 1.11.240 and its Address Library ID.

| Hook | What FSMP uses it for | Fallout 4 |
|---|---|---|
| `Main::Update` call sites (2) | Step the physics once per frame, sync after render | Unresolved: the frame loop call site. Lead: ericncream's `GameLoopFunction` (1.10.163). |
| `BipedAnim` attach (armor 3D attached) | Load SMP for worn armor / bodies | Unresolved: `BipedAnim` has no layout in CommonLibF4 (forward-declared only). Lead: ericncream's `ArmorAttachFunction` (1.10.163). |
| `ActorEquipManager::EquipObject` | Rescan on equip | Unresolved. |
| `BSFaceGenNiNode` skin calls, vtable slot `0x3E` | Hair and head-part SMP | CommonLibF4 has the vtable / RTTI IDs only, no class layout. |
| Face-morph producer patch, geometry bone-limit fix | Skyrim engine bugs | Probably not needed; check whether Fallout 4 has the same faults. |
| `Actor::CalculateLOS`, `TESNPC::GetFaceGeomPath`, `NiStream` ctor / dtor | Culling, face-geometry reload | Unresolved. |

## Types CommonLibF4 is missing

Checked in CommonLibF4 at `4379567`: `BSSkin::Instance` (forward declaration only), `BSSkin::BoneData`, `BipedAnim` (forward only), `BSFaceGenNiNode` (IDs only). These layouts have to be reversed from 1.11.240 (or taken from F4SE's own headers / Bingle's source) before the rewrite parts can compile.

## Risks

1. **Skinning model.** Fallout 4's skinning (`BSSkin`, GPU skin data, `BSSubIndexTriShape` segments) differs from Skyrim's `NiSkin*`. The skinned-mesh body reads bone weights from it every load; getting it wrong gives no physics or a crash.
2. **Addresses.** Six hook sites and four game functions to find in 1.11.240, with no Skyrim address meaningful there.
3. **Two SMP plugins at once.** Users with FO4 Faster HDT-SMP must not run both; the port needs its own plugin name and a load-time check.
4. **Head parts.** Fallout 4's face generation differs; hair SMP may need its own path.
5. **Performance.** Fallout 4's frame budget and Havok threads differ; the physics thread pool size and timing need measuring in game.

## Progress (7 November 2026)

- **Milestone 1 written, not yet built:** `fo4/`. On 1.11.240 the plugin logs, checks 24 addresses, warns if FO4 Faster HDT-SMP is loaded, links FSMP's physics core, creates the physics world, reads `configs.json` (+ `userConfigs.json`) and `defaultBBPs.xml` from `F4SE\Plugins\FO4SMP\`, steps the empty world once, and prints its status in the console after a save loads. No hooks. Build and test: `fo4/README.md`.
- **Checked here, without the game:** FSMP's config tests plus a parse of the shipped `configs.json` build and pass (11 cases, 48 assertions); the core needs no game headers, and nothing from the PCH beyond what `fo4/src/PCH.h` supplies (MSVC-only SIMD code can't be compiled off Windows, so a full compile is the owner's build).
- **Addresses:** 24 checked in 1.11.240: every hook FSMP needs (frame step, armor attach and detach, skinning, head parts, culling), the console, and the earlier three; one weak (first/third-person switch event source). `SkinSingleGeometry` is inlined away. Leads came from the 2020 port and from FO4 Faster HDT-SMP's PDB (names and ID numbers only); every one was checked in both executables. Details: `fo4/ADDRESSES.md`.
- **CommonLibF4 and 1.11.240:** 305 of CommonLibF4's 2,720 next-gen IDs aren't in Address Library 1.11.240 (its `ConsoleLog` singleton among them; FO4SMP uses its own). Every CommonLibF4 call has to be checked before use.
- **Shared code changed in the Skyrim tree** (meant to keep Skyrim behaviour the same; not built for Skyrim): the wind switch and per-system wind factor moved into the core classes; `RESET_PHYSICS` moved into `hdtSkinnedMeshWorld.h`; `defaultBBPs.xml` parsing moved out of `DefaultBBP` into `hdtDefaultBBPList.{h,cpp}` (added to the Skyrim CMake list).
- **FO4 Faster HDT-SMP:** no source in its download, and the owner expects none. Its PDB and code supplied leads (function names and Address Library IDs), all verified independently. Its module list (`Fo4MeshExtractor`, `Fo4SkinnedMeshSystem`, `Fo4PhysicsWorld`, `ArmorBoneReference`, `HeadPartModelSourceCache`...) is a useful map of what the Fallout 4 layer needs. Its Nexus page describes it as vibe-coded.

## Proposed milestones

1. **Loads.** F4SE plugin with the reusable core linked: logs, reads `configs.json` and the default physics XML (`defaultBBPs.xml`), reports its version in the console. No hooks. *(Passed 7 November 2026, fo4/TEST_RESULTS.md: 23 of 24 addresses compare OK; `BipedAnim::AttachSkinnedObject` is hooked by another plugin in the owner's load order.)*
2. **Frame step.** Hook the frame loop; step an empty physics world; log the step time. *(Passed 7 November 2026, fo4/TEST_RESULTS.md: the main loop's call to `Main::OnIdle`, ID 4484191 +0xCB, redirected; once per frame; empty step about 0.01 ms.)*
3. **One body.** SMP on the player's body (A-Body with its SMP XML), no armor hooks: proves the `BSSkin` binding. *(Written 7 November 2026, not yet built: the player's 3D is scanned when it changes; FSMP's system builder ported as fo4/src/game; skin data read from `BSSkin::Instance` / `BoneData` and the renderer's vertex buffer, using F4SE's 1.11.240 layouts, each checked at run time; simulated bones written as local transforms because the step runs after the game's frame. The player pointer is F4SE's g_player, Address Library 2698073; CommonLibF4's ID for it isn't in 1.11.240. `BipedAnim::AttachSkinnedObject` is hooked by LooksMenu, `f4ee.dll`, in the owner's load order; the milestone 4 hook must chain with it.)*
4. **Armor and actors.** Attach / detach hooks, NPCs, culling, cell changes, save / load.
5. **Head parts, Papyrus, MCM, validator.**

## 4Stim implications

Kept separate, but 4Stim depends on SMP: it swaps A-Body's physics XML at scene start and end (`MaleBodyHard.xml`) and relies on SMP for erections on SMP bodies. Whatever this port changes in how physics files load, reload or are swapped has to keep working with 4Stim's swap, and a DynamicHDT-style API here would let 4Stim swap physics directly instead of through files.

## Decided by the owner (7 November 2026)

- Repository: KeryoWolfe/FO4SMP, branch `fo4-port`.
- Separate project from 4Stim.
- Bingle's port is GPL-3: its source is to be obtained and checked.

## Still needed

- Bingle's source (from the mod's download if it ships there, or on request).
- Address work: the owner's Fallout4.exe 1.11.240 is reachable through the device bridge (`G:\SteamLibrary\steamapps\common\Fallout 4`).
