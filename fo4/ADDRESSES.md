# Fallout 4 1.11.240 addresses

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
| `ConsoleLog::Print` | 2248591 | `0x103C0C0` | CommonLibF4's ID; present in 1.11.240. |

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

## Reproducing

The scripts are helpers used from an interactive Python session, not a one-command tool (`sig.py` still has the 1.11.240 path written in). `decode.py` reads an Address Library `.bin` (u64 count, then u64 id / u64 offset pairs). `bin.py` loads an executable (functions from `.pdata`), disassembles, finds string references, callers and masked byte signatures. `vt.py` finds a class's vtables from its RTTI name.
