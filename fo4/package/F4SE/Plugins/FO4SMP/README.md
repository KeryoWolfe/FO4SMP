# FO4SMP V1

Faster HDT-SMP (FSMP), the Skyrim skinned-mesh physics plugin, ported to Fallout 4. It simulates the physics that
bodies, outfits, heads and hair describe in an HDT-SMP physics XML: soft bodies, genitals, breasts, hair, cloth.

Source and issues: https://github.com/KeryoWolfe/FO4SMP (branch `fo4-port`). Licence: GPL-3.0 with a modding
exception (LICENSE.txt, EXCEPTIONS.txt).

## Requirements

| | |
|---|---|
| Fallout 4 | **1.11.240** (Steam). Other versions: not supported; the plugin refuses to load on them. |
| F4SE | The build for 1.11.240 (tested: 0.7.9). |
| Address Library for F4SE Plugins | With `version-1-11-240-0.bin`. Without it F4SE disables FO4SMP ("address library needs to be updated" in f4se.log). |
| Mod Configuration Menu | Optional, for the settings page. |
| Something with SMP physics | A body, outfit, head or hair whose mesh names a physics XML: A-Body (male), True Wasteland Body SMP, SMP Head, Modular SMP Hairstyles... FO4SMP brings no meshes or XMLs of its own. |

Run **one** SMP plugin: disable FO4 Faster HDT-SMP (and any other) while FO4SMP is active. FO4SMP writes a warning to
its log if it finds another loaded.

## Install

Install the archive with your mod manager like any other mod. There is no plugin file (.esp) and no load order to set.
Everything goes under `Data`:

- `F4SE\Plugins\FO4SMP.dll`, and `F4SE\Plugins\FO4SMP\` (settings, the XML schema, this readme, licences)
- `MCM\Config\FO4SMP\` (the settings page)
- `Scripts\DynamicHDT.pex` and its source (the DynamicHDT script functions other mods can call)

## Uninstall

Remove the mod. Saves made with it load without it; the only thing FO4SMP keeps in a save is the physics files
other mods swapped through DynamicHDT with "persist" on, and F4SE ignores that data when FO4SMP is gone.

## What it does

- Physics on the player and on NPCs: their body, outfit, head (SMP Head) and hair (SMP hairstyles), from the
  physics XML each mesh names.
- NPCs: the nearest ones within reach are simulated (`maximumActiveSkeletons` in `configs.json`, or the settings
  page). Those beyond keep their bodies' rest shape without physics. NPCs out of the camera's view for a second
  pause and resume when seen again.
- Physics pauses with the game (pause menu, Pip-Boy, other pausing menus).
- Everything is rebuilt when a save loads, when an outfit changes and when a hairstyle changes.
- XMLs and meshes are read loose or from BA2 archives.
- The DynamicHDT script functions of FSMP (`ReloadPhysicsFile`, `SwapPhysicsFile`, `QueryCurrentPhysicsFile`,
  `TogglePhysics`, `ResetPhysics`) and FO4 Faster HDT-SMP's `HasPhysicsSystem` and `IsSoftSuspended`, so mods
  such as 4Stim can swap physics files during scenes.
- Each physics XML is checked against FSMP's schema the first time it's used; problems are listed in the log.

## Settings

- **MCM page "FO4SMP Physics"**: NPC physics on/off, how many NPCs, how far, pause off-screen NPCs, face and hair
  physics, verbose log. A value counts once you change it there; until then `configs.json` decides.
- **`F4SE\Plugins\FO4SMP\configs.json`**: FSMP's settings (solver, wind, `maximumActiveSkeletons`...). Put your own
  changes in `userConfigs.json` beside it (same layout, only the values you change), so updates don't overwrite them.
  Example: `{ "smp": { "maximumActiveSkeletons": 20 } }`.
- **Overrides**: a file at `F4SE\Plugins\FO4SMP\overrides\<path>` is used instead of `Data\<path>`. For example
  `overrides\Meshes\A-Body\body\MaleBody.xml` replaces A-Body's XML without touching A-Body.
- **`defaultBBPs.xml`**: maps shape names to an XML, for meshes that don't name one.

## Known limitations

- **A-Body's penis against the legs** can make the penis chain jitter or fly out. Workaround: an override of
  `MaleBody.xml` with the `VirtualPenis` / `VirtualLegs` collision off (see Overrides). The cause is still being
  looked for.
- **Many NPCs cost frame time**: about 25 ms per physics step with 40 simulated settlers. Lower the NPC count, or
  leave "Pause Off-Screen NPCs" on.
- An NPC gets face or hair physics only if FO4SMP has seen it attach an outfit or body (nearly all do).
- Not ported from FSMP: its console commands (`smp ...`), its in-game menu, its NIF checks, wind (off by default),
  first-person physics.
- Tested on one setup: Fallout 4 1.11.240 (Steam), F4SE, Mod Organizer 2, with LooksMenu, A-Body, TWB SMP, SMP
  Head, Modular SMP Hairstyles and 4Stim. Anything else is untested, not unsupported.

## Reporting a problem

Turn on **Verbose Log** in the settings page, reproduce the problem, and send
`Documents\My Games\Fallout4\F4SE\FO4SMP.log` (and the crash log if the game crashed).

## Credits

See CREDITS.md.
