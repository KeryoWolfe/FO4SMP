# Fallout 4 1.11.240 addresses

Found 7 November 2026 by matching functions between two executables, both from the owner's machine:

- **1.10.163**: GOG `Fallout4.exe` (`G:\Fallout 4`). Its layout matches Steam 1.10.163: all seven addresses the 2020 port names are function starts in it, and Address Library's `version-1-10-163-0.bin` has an ID for each.
- **1.11.240**: Steam `Fallout4.exe` (FileVersion 1.11.240.0, `.text` not encrypted), with Address Library's `version-1-11-240-0.bin`.

The leads are the 1.10.163 addresses in the 2020 port, github.com/ericncream/hdtSMP64 `hdtSMP64/Offsets.h` (MIT). The scripts are in `tools/` (Python: `pefile`, `capstone`, `numpy`).

Nothing below has been hooked or run in game. `src/AddressCheck.cpp` checks the verified ones at load.

## Verified (by two independent checks each)

| What | 1.10.163 | 1.11.240 | ID (1.11.240) | How it was matched |
|---|---|---|---|---|
| `BSFaceGenNiNode` vtable | `0x2CEB648` | `0x25072F0` | 539845 | RTTI name `.?AVBSFaceGenNiNode@@` in both; the 1.11.240 database keeps the 1.10.163 ID for it (CommonLibF4's `VTABLE::BSFaceGenNiNode` uses the same). |
| `BSFaceGenNiNode::SkinAllGeometry` | `0x0685720` | `0x06E8320` | 2209478 | Vtable slot 0x43, the last one, in both; the same loop over the node's children at `+0x128` / `+0x132`. |
| `TESNPC::GetFaceGeomPath` | `0x05B8F90` | `0x0658E60` | 2207453 | The only function referencing `Meshes\Actors\Character\FaceGenData\FaceGeom\%s\%08X.NIF` in each; same size (0xB8) and the same code. |

## Gone in 1.11.240

| What | 1.10.163 | Finding |
|---|---|---|
| `BSFaceGenNiNode::SkinSingleGeometry` | `0x0685980` | In 1.10.163, `SkinAllGeometry` calls it once per child geometry. In 1.11.240 that call is gone: `SkinAllGeometry` reads the geometry's skin instance (`+0x140`) itself and calls `0x6E90E0` with a closure. FSMP's per-geometry hook has no target; head-part SMP has to hook `SkinAllGeometry` (through the vtable) only, or the new callee once it is understood. |

## Unresolved

| What | 1.10.163 | Status |
|---|---|---|
| Armor attach (`ArmorAttachFunction`) | `0x01C3A10` (ID 1575810) | Its signature doesn't match in 1.11.240. One function shares its distinctive `add rcx, 0x1E50`, `0x035C300` (ID 2194363), but it takes different arguments and has different callers: **not** a match. Next: match through its three 1.10.163 callers, or through `BipedAnim`. |
| Game loop (`GameLoopFunction`) | `0x2042430` (ID 239710) | The 2020 port marks it unsure. Its call-site signature matches once in 1.11.240 (call at `0x0BD554E`, target `0x1A80610`, ID 2284754), unconfirmed. Another 1.10.163 candidate, `0x0D38E60` (the function logging "Hitched from shader compile"), is `0x0C339D0` (ID 2228917) in 1.11.240 by its string. Which one FSMP's per-frame step belongs in needs checking. |
| Game shutdown | `0x27D4DF0` (ID 213588) | The signature matches a 13-byte chunk in 1.11.240 (`0x2180C50`): not trusted. |
| Slow-time multiplier | `0x5B5B6D0` (data) | The 2020 port marks it "maybe?"; the signature doesn't match. |
| FaceGen bone limit | `0x06935DF` | A Skyrim engine fix; signature doesn't match. Probably not needed. |

## Reproducing

The scripts are helpers used from an interactive Python session, not a one-command tool (`sig.py` still has the 1.11.240 path written in). `decode.py` reads an Address Library `.bin` (u64 count, then u64 id / u64 offset pairs). `bin.py` loads an executable (functions from `.pdata`), disassembles, finds string references, callers and masked byte signatures. `vt.py` finds a class's vtables from its RTTI name.
