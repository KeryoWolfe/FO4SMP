# Test plan

What to run before a release, and where each test stands. Results of every run, with the log lines that show them,
are in `TEST_RESULTS.md`. Status: **Pass** (seen working in game, with the log), **Fail**, **Partial**, **Not run**.
A row passes only on an in-game test of the build being released; a compile or an unchanged log is not a pass.

## Setup

- Fallout 4 1.11.240 (Steam), F4SE for 1.11.240, Address Library with `version-1-11-240-0.bin`.
- A separate Mod Organizer 2 profile with a backup of the saves used.
- FO4 Faster HDT-SMP and any other SMP plugin disabled.
- Content: A-Body (male) with SMP, TWB SMP, SMP Head, Modular SMP Hairstyles; one SMP outfit; LooksMenu; MCM; 4Stim
  for the DynamicHDT swap.
- Logs: `Documents\My Games\Fallout4\F4SE\FO4SMP.log` (Verbose Log on for diagnosis), `f4se.log`, crash logs.

## Matrix

Status column: as of the last build tested (run 45, CommonLibF4 alandtse). **Every row is to be run again on the
`fo4smp/combined` build** (V1), the build being released.

| # | Area | Steps | Expected (log) | Status |
|---|---|---|---|---|
| 1 | Installation | Install the release archive with MO2 into an empty mod; list its files. | `F4SE\Plugins\FO4SMP.dll`, `F4SE\Plugins\FO4SMP\` (configs, schema, docs, licences), `MCM\Config\FO4SMP\`, `Scripts\DynamicHDT.pex` and `.psc`. Nothing else. | Partial: archive contents checked (run 47); installed in MO2 as `FO4SMP-V1`, F4SE sees version 01000000 (run 48); play from it in runs 49-50 |
| 2 | Dependency checks | Start with Address Library removed; then with FO4 Faster HDT-SMP enabled too. | "F4SE: plugin FO4SMP.dll ... disabled, address library needs to be updated" in f4se.log: F4SE refuses the plugin before FO4SMP runs; the game starts. With another SMP plugin: "Another SMP plugin is loaded". | Partial: missing Address Library passes (run 48); other SMP plugin not run |
| 3 | Runtime check | Start on another runtime (if available). | F4SE refuses the plugin, or "Unsupported runtime ... Not loading." | Not run |
| 4 | Plugin loading | Start the game to the main menu. | "Address check: N OK, M hooked by another plugin, 0 failed", both hooks installed, "FO4SMP loaded", console report. | Pass (runs 1-45) |
| 5 | Papyrus | Load a save. Call a DynamicHDT function (4Stim scene start). | "DynamicHDT: 7 of 7 script functions bound"; `SwapPhysicsFile` lines; the body rebuilt with the new XML. | Pass (runs 44-45) |
| 6 | Actor validation | Stand near armor racks with SMP outfits, corpses, NPCs without SMP bodies. | Racks "placed only" (skin bones missing); no build for actors without physics; nothing crashes. | Pass (runs 35-36) |
| 7 | Scene startup | Load a save with the A-Body player and NPCs around. | Player build ~1 s after load, NPC "selected" then built; bodies move with physics. | Pass |
| 8 | Animation and sync | Walk, run, jump, crouch, turn; NPCs walking; 4Stim scene. | Soft parts lag and settle; no stretching, flying off or vibrating; "farthest simulated bone" under ~110 units. | Partial: penis-vs-legs needs the override |
| 9 | Head and hair | Player and an NPC with SMP Head; Cait (or anyone) with an SMP hairstyle. | "face part ... pointed at the actor's own bones", "built"; faces normal, hair moves, nothing follows another actor. | Pass (runs 40, 42) |
| 10 | Outfit change | Put an SMP outfit on and off the player and an NPC. | "dropped ... (a physics outfit attached)", rebuilt from the new model. | Pass (run 45, player) |
| 11 | Cancellation / pause | Pause menu, Pip-Boy, a pausing menu, then resume. | "Frame step: paused" / "resumed" lines; no motion while paused. | Pass (run 41) |
| 12 | Save/load | Load save A, then save B, without quitting; then quit to the menu and load again. | "everything reset (a save is loading)" each time; rebuilt; no stale bodies, no explosions after load. | Pass (run 45) |
| 13 | Cell transitions | Enter and leave interiors with a companion; fast travel. | Companion keeps physics across doors; no "no physics found" for actors whose outfit has it. | Pass (run 35) |
| 14 | Culling | Third person: turn away from a group and back. | "Culling: the camera looks along ..." once; "out of view; physics suspended" / "back in view"; no jump on resume. | Pass (run 45) |
| 15 | UI and controls | MCM page: change each setting; reopen; verbose log on and off. | "Settings: read N value(s)", "Settings: NPC physics ..."; behaviour changes; verbose lines appear and stop. | Pass (runs 50-51: every switch applied within 2 s; the penis/legs switch on and off changes the penis as expected) |
| 16 | BA2 reading | A head part or XML only inside a BA2. | "Game files: read ... (from the game's resource system)". | Pass (run 45) |
| 17 | Validator | Load with A-Body, TWB, SMP Head, an SMP hairstyle. | "Validator: ... passed" or problems with line numbers, once per file. | Pass (run 45) |
| 18 | Compatibility | LooksMenu (open it and change hair), 4Stim, other animation mods in the load order. | LooksMenu's detour reported as hooked, not failed; a new hairstyle rebuilds face physics within ~15 s. | Pass (run 53: no crash, face stays right through the hair list); slow face update after each change fixed, retest |
| 19 | Performance | A settlement with 30+ settlers; note fps and "physics step average" with default settings, then with more NPCs. | Default: physics step a few ms. | Pass at the default (run 56: 2.8-3.2 ms a step with 4 NPCs); 13-19 ms at 20, 24-31 ms at 40 |
| 20 | Packaging | Unpack the archive; compare with the source tree's `package/`; check licences and readme. | Every shipped file accounted for; GPL text, exceptions, schema licence, credits present. | Pass (run 47: 18 files, exactly `package/` plus the DLL and the compiled pex; readme says V1) |

## Failure paths

| Case | How | Expected | Status |
|---|---|---|---|
| Physics XML missing or broken | Point an override at a missing or malformed XML. | "built nothing", validator problems; the actor keeps its rest shape; no crash. | Pass (run 49) |
| Missing schema | Remove `hdtSMP64.xsd`. | "Validator: no schema ...", once; physics unaffected. | Pass (run 49) |
| Script functions off | Create `F4SE\Plugins\FO4SMP\nopapyrus.txt`. | "nopapyrus.txt present"; the game runs; scripts calling DynamicHDT get errors in the Papyrus log only. | Pass (run 49) |
