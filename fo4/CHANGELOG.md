# Changelog

FO4SMP, Faster HDT-SMP for Fallout 4. Releases are numbered V1, V2, V3...; a fix after a release adds .1, .2... (V1.1). Dates are when a change was tested in game (`TEST_RESULTS.md`).

## V1 (not yet released)

First release. Fallout 4 1.11.240 (Steam) only.

### Physics

- FSMP's physics core and XML format on Fallout 4: bodies, outfits, heads and hair from the physics XML their mesh
  names (`HDT Skinned Mesh Physics Object`), or from `defaultBBPs.xml`.
- The player and NPCs. NPCs: the nearest up to `maximumActiveSkeletons` - 1 within 2,000 units are simulated, with
  hysteresis so they don't drop in and out; others within 8,000 units keep their bodies' rest shape.
- Head parts: SMP Head and SMP hairstyles, on bones made for each actor (the game shares face bones between actors).
- Off-screen NPCs pause after a second out of view and resume when seen.
- Paused with the game's pausing menus.
- Rebuilt on outfit, body or hairstyle change, and from scratch when a save loads.
- Protection against explosions: a bone flung far from its parent is put back; an NPC that keeps exploding is
  rebuilt without collisions, then left without physics for a while. Ground planes collide only with their own
  skeleton. Armor racks and other skeletons without the body's bones keep their rest shape.

### Files

- Physics XMLs and meshes read loose or from BA2 archives.
- User overrides: `F4SE\Plugins\FO4SMP\overrides\<path>` replaces `Data\<path>`.
- `userConfigs.json` beside `configs.json` for your own settings.
- Each physics XML checked against FSMP-Validator's schema (shipped); problems logged once.

### Scripts and settings

- DynamicHDT: `ReloadPhysicsFile`, `SwapPhysicsFile`, `QueryCurrentPhysicsFile`, `TogglePhysics`, `ResetPhysics`,
  `HasPhysicsSystem`, `IsSoftSuspended`. Swaps made with "persist" are kept in the save.
- MCM page: NPC physics, NPC count and distance, pausing off-screen NPCs, face and hair physics, the A-Body
  penis/legs workaround (on by default), verbose log.

### Known limitations

See the readme: A-Body's penis-vs-legs collision (worked around by a setting, on by default), frame time with dozens of NPCs, FSMP
features not ported (console commands, in-game menu, NIF checks, wind, first person).

## Development history

0.1-0.3 (7-8 November 2026): milestones 1-5, 45 test runs; every run, what it showed and what changed after it is in
`TEST_RESULTS.md`.
