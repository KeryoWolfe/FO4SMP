# Credits

FO4SMP is a port of **Faster HDT-SMP** (github.com/DaymareOn/hdtSMP64) to Fallout 4.

## FSMP and its lineage

- **DaymareOn** (DaydreamingDay): Faster HDT-SMP, its physics core, XML format and DynamicHDT API. FSMP's full list
  of contributors and thanks: the "Hall of Fame" page of its wiki (github.com/DaymareOn/hdtSMP64/wiki).
- FSMP's contributors in its history include James Matthews, Alan Tse, asdasdduck, aers, KaninHop, SesamePaste233,
  jgernandt, usernameak, blake-fm, Backporter and others.
- **HydrogensaysHDT**: the original HDT-SMP. **aers** and **Karonar1**: the versions FSMP descends from.
- **SesamePaste233**: the original DynamicHDT (MIT; DynamicHDTOriginalWorkLicense.txt). FO4SMP's `DynamicHDT.psc`
  follows FSMP's GPL-3 version.
- **FSMP-Validator** (DaymareOn, MIT): the physics XML schema `hdtSMP64.xsd` shipped here
  (hdtSMP64.xsd.LICENSE.txt).

## Fallout 4

- **KeryoWolfe**: FO4SMP.
- **The F4SE team** (ianpatt and others): F4SE; FO4SMP uses addresses and layouts from its 1.11.240 headers.
- **meh321** and contributors: Address Library for F4SE Plugins.
- **CommonLibF4** and its forks (Ryan-rsm-McKenzie's original, Dear Modding, libxse, alandtse and the others gathered
  in github.com/KeryoWolfe/CommonLibF4): FO4SMP builds on the `fo4smp/combined` branch.
- **Bingle**'s FO4 Faster HDT-SMP and **ericncream**'s 2020 port: names and addresses from them were used as leads,
  each one checked independently in the game executable. No code from either is used.

## Libraries

Bullet Physics (zlib, with FSMP's changes), oneTBB (Apache-2.0), spdlog (MIT), pugixml (MIT), RapidJSON (MIT).
