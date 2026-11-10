# Fallout 4 addresses (OG 1.10.163, NG 1.10.984, AE 1.11.240)

V1 supported AE only; the AE sections below are how its addresses were found. **V1.1** added OG and NG: see
"OG and NG" at the end. `src/Offsets.h` holds every ID as `{ OG, NG, AE }`.

Found 7 November 2026 by matching functions between two executables, both from the owner's machine:

- **1.10.163**: GOG `Fallout4.exe` (`G:\Fallout 4`). Its layout matches Steam 1.10.163: all seven addresses the 2020 port names are function starts in it, and Address Library's `version-1-10-163-0.bin` has an ID for each.
- **1.11.240**: Steam `Fallout4.exe` (FileVersion 1.11.240.0, `.text` not encrypted), with Address Library's `version-1-11-240-0.bin`.

Leads came from two earlier ports:

- **The 2020 port**, github.com/ericncream/hdtSMP64 `hdtSMP64/Offsets.h` (MIT): 1.10.163 addresses.
- **FO4 Faster HDT-SMP 1.31** (Bingle, Nexus 107209, GPL-3; no source published). Its PDB names the game functions it uses (`Smp::Address::*`), and its initializers hold each one's Address Library IDs for 1.10.163 and next-gen. Only those names and ID numbers were taken, as leads: no code. `tools/leads.py` lists them.

Every lead was then checked here, in the two executables, before being trusted. The scripts are in `tools/` (Python: `pefile`, `capstone`, `numpy`).

Nothing below has been hooked or run in game. `src/AddressCheck.cpp` checks every verified one at load (one log line each).

## Verified (by two independent checks each)

| What | 1.10.163 | 1.11.240 | ID (1.11.240) | How it was matched |
|---|---|---|---|---|
| `BSFaceGenNiNode` vtable | `0x2CEB648` | `0x25072F0` | 539845 | RTTI name `.?AVBSFaceGenNiNode@@` in both; the 1.11.240 database keeps the 1.10.163 ID for it (CommonLibF4's `VTABLE::BSFaceGenNiNode` uses the same). |
| `BSFaceGenNiNode::SkinAllGeometry` | `0x0685720` | `0x06E8320` | 2209478 | Vtable slot 0x43, the last one, in both; the same loop over the node's children at `+0x128` / `+0x132`. |
| `TESNPC::GetFaceGeomPath` | `0x05B8F90` | `0x0658E60` | 2207453 | The only function referencing `Meshes\Actors\Character\FaceGenData\FaceGeom\%s\%08X.NIF` in each; same size (0xB8) and the same code. |

## Verified: Bingle's leads (two independent checks each)

For each: the 1.10.163 ID resolves to a function start in the GOG 1.10.163 exe and the next-gen ID to a function start in 1.11.240; the two functions' instruction sequences match (similarity below, 1.00 = identical mnemonics over the first 0x200 bytes); and their callers among this set agree in both versions (`BipedAnimApplySkinnedObjects` calls `AttachSkinnedObject` in both, `AddHeadPartOnActor` calls the other head-part functions in both, and so on). `src/Offsets.h` has the table and `src/AddressCheck.cpp` checks each one's first bytes in game.

| Function | 1.10.163 ID | 1.11.240 ID | 1.11.240 | Similarity | Note |
|---|---|---|---|---|---|
| `Main::OnIdle` | 633524 | 2228917 | `0x0C339D0` | 0.45 | Also found independently by its string ("Hitched from shader compile"); the 2020 port's alternative game-loop lead. Where FSMP steps physics. |
| `BipedAnim::AttachSkinnedObject` | 1575810 | 2194388 | `0x035E960` | 0.86 | **Armor attach.** The 2020 port's `ArmorAttachFunction`; called from `ApplySkinnedObjects` in both. |
| `BipedAnim::ApplySkinnedObjects` | 224320 | 2194381 | `0x035DC20` | 0.94 | |
| `BipedAnim::AttachToParent` | 1370428 | 2194378 | `0x035D4F0` | 0.91 | References the same `UPB` string in both. |
| `BipedAnim::RemovePart` | 575576 | 2194342 | `0x0358AB0` | 0.91 | Armor detach. |
| `Reset3D` | 302888 | 2229913 | `0x0C742F0` | 0.98 | |
| `BSSkinInstance::GetFlattenedBoneReference` | 73083 | 2270469 | `0x16D81F0` | 0.86 | Fallout 4's skinning: how bones are found. |
| `BSFlattenedBoneTree::Find` | 1435518 | 2270080 | `0x16C7110` | 1.00 | |
| `BSFlattenedBoneTree::GetOrCreateBoneNode` | 865291 | 2270071 | `0x16C6D70` | 0.95 | |
| `Actor::CalculateLOS` | 1324305 | 2230201 | `0x0C97BF0` | 0.80 | FSMP's culling. |
| `Actor::GetCollisionFilter` | 1474995 | 2229991 | `0x0C81140` | 1.00 | |
| `BSFaceGen::AddHeadPartOnActor` | 913780 | 2209545 | `0x06EF1B0` | 0.84 | Head parts (hair). |
| `BSFaceGen::PrepareHeadPart` | 840416 | 2209534 | `0x06EE0E0` | 0.90 | |
| `BSFaceGenModelExtraData::SetBoneName` | 1278503 | 2209387 | `0x06E4000` | 1.00 | Leaf code with no unwind entry in either exe; references the same `FMD` string. |
| `TESNPC::GetAlternateHeadPartList` | 437572 | 2207501 | `0x065EEF0` | 0.97 | |
| `TESNPC::GetAlternateHeadPartListSize` | 517331 | 2207502 | `0x065EFA0` | 0.97 | |
| `TESObjectREFR::FixDisplayedHeadParts` | 789414 | 2200988 | `0x0506460` | 0.87 | |
| `LooksMenuUtils::ShowLooksMenu` (the game's, despite the name) | 411372 | 2223366 | `0x0A85EE0` | 0.90 | |
| `FirstThirdPersonSwitch` event source | 930195 | 2233725 | `0x0D92B20` | 0.41 | **Weak**: low similarity and no caller agreement. Checked in game as "weak match"; confirm before relying on it. |

Why the earlier search for armor attach failed: in 1.11.240 the function no longer starts with the 1.10.163 prologue and its `add rcx, 0x1E50` is gone, so neither signature found it. `0x035C300`, which shares that `add`, is a different function.

## Console

| What | ID (1.11.240) | 1.11.240 | How it was found |
|---|---|---|---|
| `ConsoleLog` singleton (data) | 4797437 | `0x30F83B8` | CommonLibF4's next-gen ID for it, 2690148, is **not in Address Library 1.11.240**. 976 calls to `ConsoleLog::Print` (2248591) and `AddString` (2248593) load `rcx` from this one pointer just before the call. |
| `ConsoleLog::Print` | 2248591 | `0x103C0C0` | CommonLibF4's ID; present in 1.11.240, but here it's the game's **variadic** printf-style print `(log, format, ...)` (it spills rdx/r8/r9 on entry; the game calls it with formats like `Exported Reference: %s(%08x)`). CommonLibF4 declares it as `Print(format, va_list)`, so `PrintLine` passes a `va_list` and prints garbage: the first test showed "5" and four boxes. FO4SMP calls it directly. |

**CommonLibF4 and 1.11.240:** of the 2,720 next-gen IDs CommonLibF4 (`4379567`) names, **305 are missing** from Address Library's 1.11.240 database. Any CommonLibF4 function used here has to be checked first; a missing ID throws when first used. (This may apply to 4Stim too, which uses CommonLibF4.)

## Gone in 1.11.240

| What | 1.10.163 | Finding |
|---|---|---|
| `BSFaceGenNiNode::SkinSingleGeometry` | `0x0685980` | In 1.10.163, `SkinAllGeometry` calls it once per child geometry. In 1.11.240 that call is gone: `SkinAllGeometry` reads the geometry's skin instance (`+0x140`) itself and calls `0x6E90E0` with a closure. FSMP's per-geometry hook has no target; head-part SMP has to hook `SkinAllGeometry` (through the vtable) only, or the new callee once it is understood. |

## Unresolved

| What | 1.10.163 | Status |
|---|---|---|
| Game loop (`GameLoopFunction`) | `0x2042430` (ID 239710) | Superseded: the per-frame step goes in `Main::OnIdle` (above). The 2020 port's other lead matched once in 1.11.240 (call at `0x0BD554E`, target `0x1A80610`, ID 2284754); unused. |
| Game shutdown | `0x27D4DF0` (ID 213588) | The signature matches a 13-byte chunk in 1.11.240 (`0x2180C50`): not trusted. |
| Slow-time multiplier | `0x5B5B6D0` (data) | The 2020 port marks it "maybe?"; the signature doesn't match. |
| FaceGen bone limit | `0x06935DF` | A Skyrim engine fix; signature doesn't match. Probably not needed. |

## OG and NG (V1.1, 10 November 2026)

Files: the GOG 1.10.163 executable (above), Address Library `version-1-10-163-0.bin`; the Steam 1.10.984 executable
(FileVersion 1.10.984.0, SHA-1 `2cabde48...`) and Address Library `version-1-10-984-0.bin` (Nexus file 1715667020);
F4SE's source at 0.6.23 (ianpatt/f4se `4d7ba86`, runtime 1.10.163) and 0.7.2 (`7c1bf3e`, 1.10.984).

**The NG executable's code is encrypted on disk** (SteamStub: `.text` entropy 8.0, a `.bind` section). FO4SMP's
work does not remove that. Its headers, unwind table (`.pdata`, `.rdata`) and data are readable, so NG was checked
without disassembly, and the in-game checks cover the rest (below).

### OG

| What | OG ID | How |
|---|---|---|
| Functions in the table (`kFunctions`) | as in `Offsets.h` | V1's matching (above) already had each OG ID. Their first 8 bytes in the 1.10.163 exe are now in the table and checked in game on OG. |
| `ConsoleLog::Print` (variadic) | **1065300** | Not 799546 (Bingle's and CommonLibF4's "Print"): 799546 is `Print(format, va_list)`. 1065300 spills rdx/r8/r9 and calls it; 762 of the 1,367 code references to the console singleton (689441) are followed by a call to 1065300. AE merged the two into 2248591. |
| Heap allocate/free, NiNode constructor, resource stream ctor/dtor/read/IsValid, LookupREFRByHandle | 652767, 1582181, 20633, 1198116, 1516202, 424286, 1502562, 542603 | F4SE 0.6.23's addresses, looked up in the OG database; each is a function start in the exe (IsValid, a leaf, has no unwind entry) with the first bytes in the table. |
| g_player, g_ui, g_playerCamera, g_mainHeap, g_dataHandler | 303410, 548587, 1171980, 42693, 711558 | F4SE 0.6.23's addresses; the last four equal CommonLibF4's OG IDs. |
| `Main::Run`, the call to `Main::OnIdle` | 1125396, +0xBB | The only caller of OnIdle (633524) in the exe; CommonLibF4's `Main::Run` OG ID agrees. |
| QGameSystemsShouldUpdate, BSTimer | 779552, 1256126 | CommonLibF4's; 779552 is the byte before QGameDataLoaded (1004331), as in AE. BSTimer: delta +0x10 and real delta +0x14 in both (267/269 and 86/88 code references); `disableCounter` +0x38 in OG and NG, +0x48 in AE (CommonLibF4 `BSTimer::QDisableCounter`). |
| Armor attach call sites | ApplySkinnedObjects +0x182; helper 973253 +0x62 | OG has three calls to AttachSkinnedObject: in ApplySkinnedObjects (parent given), in the helper it calls for an object's children (parent null: AE inlines this, its first call site), and in a functor nobody calls directly (no AE counterpart; not hooked). |

Layouts FO4SMP reads are the same in OG (F4SE 0.6.23's headers, CommonLibF4): BipedAnim (slots of 0x58 from +0x10, the
model at +0x20 of each, `actorRef` +0x1E50: OG's AttachSkinnedObject does `add rcx, 0x1E50`), `loadedData` +0xF0,
UI menu count +0x1E0, the camera node +0x20, the data handler's head part array +0x1D0, BGSHeadPart +0x38/+0xA0/+0x170,
NiNode 0x140 bytes, the skin instance, and BSGeometry +0x148: OG's `BSGeometryData` {vertexDesc, vertex data*, triangle
data*} with the CPU copy at +0x08 of each matches AE's `BSGraphics::TriShape` {vertexDesc, VertexBuffer*,
IndexBuffer*} with `pData` at +0x08. BSDynamicTriShape +0x170/+0x180 in both.

### NG

- **IDs.** Address Library 1.10.984 has every NG/AE function ID FO4SMP uses, and for each one F4SE 0.7.2 names, at
  F4SE's address (heap, NiNode constructor, resource stream, LookupREFRByHandle, g_player). The AE-only data IDs
  (47xxxxx) have NG IDs from F4SE 0.7.2 that equal CommonLibF4's (UI 2689028, camera 2688801, data handler 2688883,
  heap 2688720, console 2690148); `Main::Run` NG 2718225 (CommonLibF4).
- **Sizes.** From the unwind table: 27 of the 30 functions have the same total size and number of chunks in NG as
  in AE (`tools/ngsize.py`), so their code is very likely the same. The three that differ: `Main::Run`, `Main::OnIdle`,
  `AttachSkinnedObject`.
- **In game.** NG's first bytes aren't known, so the address check compares AE's and, when they differ, accepts the
  address only if NG's unwind table has a function starting there. The two hooks find their call sites by scanning
  the caller's code for a call to the verified target (`src/CodeScan.cpp`), so a wrong ID for `Main::Run`, OnIdle,
  ApplySkinnedObjects or AttachSkinnedObject shows as "found 0 calls" and the hook isn't installed.

### Call sites found at load (all runtimes)

`CodeScan` reads the executable's unwind table in memory, collects a function's chunks (following chained unwind
info) and lists every `E8 rel32` in them that lands on the target. Offline, the same algorithm (`tools/chain.py`) finds
exactly AE's +0x192/+0x1BB (ApplySkinnedObjects) and +0xCB (Main::Run), V1's fixed offsets, and OG's +0x182, +0x62
(helper) and +0xBB. When a scan finds nothing (another plugin redirected the call first), the known OG/AE offsets are
used, as in V1.

## Reproducing

The scripts are helpers used from an interactive Python session, not a one-command tool (`sig.py` still has the 1.11.240 path written in). `decode.py` reads an Address Library `.bin` (u64 count, then u64 id / u64 offset pairs). `bin.py` loads an executable (functions from `.pdata`), disassembles, finds string references, callers and masked byte signatures. `vt.py` finds a class's vtables from its RTTI name.
