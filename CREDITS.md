# Credits

What this project learned from other people's work, and where each thing came from.

Nothing here is a dependency. Quebratsk ships no third-party code. These are the sources that
answered questions that would otherwise have been answered by guessing, which is worth writing
down because guessing is exactly how several of this project's worst defects got in.

## Formats

**[xash3d-fwgs](https://github.com/FWGS/xash3d-fwgs)** — a maintained reimplementation of the
GoldSrc engine. GPL-2.0, so it was read as a reference for **what the file formats are**, never
copied; every decoder here is written from the layout.

It settled, on 2026-08-01:

- `mstudioseqdesc_t` (176 bytes), `mstudioanim_t`, `mstudioanimvalue_t` and the six-channel
  run-length encoding of a bone track. Half-Life 1 and Counter-Strike 1.6 models had imported
  frozen in their bind pose until then, because nothing here knew a sequence existed.
- `mstudioevent_t` is 76 bytes in GoldSrc and starts with a frame number, where Source's is 80
  and starts with a cycle. Reading the second as the first leaves the first event of every
  sequence correct and turns the rest to noise.
- `R_StudioGetAnim`: sequence groups live in `<name>NN.mdl` beside the model, which is where
  most of a Half-Life character's animations actually are.
- `R_DrawViewModel`: the first-person weapon is a separate entity in its own render pass with
  a compressed depth range, and the player's own body is not drawn. The view here is built
  that way because of this.
- `snd_wav.c` converts 8-bit samples to signed by subtracting 128. Independent confirmation of
  a fix made after an unsigned buffer was played as signed and came out as a saturated blast.

## Movement

**[goldsrc-character-controller](https://github.com/ratmarrow/goldsrc-character-controller)**
by ratmarrow — CC0-1.0, public domain. Its motion component is a clear reading of Quake's
`PM_Accelerate`, `PM_AirAccelerate` and `PM_Friction`, and `demo/sandbox/player.gd` implements
the same algorithm: accelerate by the shortfall along the wished direction measured with a dot
product, cap air acceleration at 30 units per second, and bleed friction against `sv_stopspeed`
rather than against the current speed once slow.

Before this the player's velocity was set straight from the input direction, which starts and
stops instantly and carries no momentum through a turn. Air strafing and bunny hopping are not
features added on top; they fall out of that one clamp.

**[GoldGdt](https://github.com/ratmarrow/GoldGdt)** by the same author, MIT — the larger
project the above came out of, consulted for how the unit conversion is handled.

## Constants

The movement figures in `player.gd` are the engine's own, converted once from Hammer units at
0.0254 m each: `sv_gravity` 800, `sv_stepsize` 18, `sv_friction` 4, `sv_stopspeed` 75, the
250 ups run speed Counter-Strike allows an unencumbered player, and the 268.3 ups jump that
clearing a 45 unit crate requires. They were 22.0, 5.0 and 6.5 before, and those came from
nowhere at all.

Assembled by the project owner from the Half-Life SDK and `ReGameDLL_CS`, and checked here
against the engine sources above. Two of its claims turned out to be wrong and are kept as
errata rather than quietly dropped, because in this domain the error is as instructive as the
fact: one of them would have reintroduced a defect that had already cost a day to find.

## Weapon data

**[ReGameDLL_CS](https://github.com/s1lentq/ReGameDLL_CS)** (MIT) — a reimplementation of the
Counter-Strike game library. `demo/addons/quebratsk_editor/data/cs_weapons.json` is generated
from it by `tools/build_weapon_manifest.py`.

A model does not say which weapon it is. It carries a mesh, a skeleton and some sequences, and
everything that makes an AK an AK rather than a shape is in the game's code: which view model
pairs with which world model, which sounds it makes, how hard it hits. Quebratsk inferred that
from filenames, which held until it did not, and this replaces the inference with the game's
own declaration for 29 weapons.

The same repository answers a second question, through `tools/build_sound_catalogue.py` into
`data/cs_sounds.json`: what a Counter-Strike level sounds like. A `.mdl` names the sounds its
own animations trigger, which covers a gunshot and almost nothing else. Footsteps, the C4
beeping and being planted and going off, a grenade bouncing off a wall, hitting flesh, taking
a fall, drowning, the radio, the hostages — none of that is in any file Quebratsk reads, which
is why an imported character walked across a level in silence. 313 sound names, of which 200
are present in a retail Counter-Strike 1.6 install; the 113 that are not are almost entirely
hostage voice lines that ReGameDLL supports and retail does not ship.

## Shaders

**[godotshaders.com](https://godotshaders.com)** — the site publishes every posted shader
under CC0: *"The shader code and all code snippets in this post are under CC0 license and can
be used freely without the author's permission."* Checked on the water and godray pages, where
the wording is identical, so it is the site's policy rather than one author's.

Nothing from there is copied into Quebratsk today. `demo/lab/storm_sky.gdshader` and
`demo/lab/water.gdshader` are written here. Recorded anyway, because the licence had to be
established before reading the code rather than after, and the answer is worth not looking up
twice.

## Other importers looked at

Read for approach, not copied. None is a dependency.

- **[goldsrc-godot](https://github.com/alanfischer/goldsrc-godot)** by alanfischer — BSP, MDL
  and WAD as a GDExtension, with coplanar face merging and animated textures at the 10 fps the
  SDK specifies.
- **[Godot-GoldSrc-MDL-Importer](https://github.com/DataPlusProgram/Godot-GoldSrc-MDL-Importer)**
  (MIT) — MDL only, including the chrome flag for reflective shading.
- **[godot_bsp_importer](https://github.com/jitspoe/godot_bsp_importer)** (MIT) — Quake-family
  BSP, broader than GoldSrc.

## Games

Every measurement in the changelog was taken against retail installs of Half-Life,
Counter-Strike, Condition Zero, Condition Zero: Deleted Scenes, Half-Life 2 and Garry's Mod.
Their content belongs to Valve and none of it is redistributed here.
