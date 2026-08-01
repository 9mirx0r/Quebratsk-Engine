# Quebratsk Engine 2.1.0 "Volvokjark"

The release where Half-Life 1 and Counter-Strike stopped standing still, and where four
separate things that had been quietly wrong for months were found by looking at the screen
rather than at the code.

## Half-Life and Counter-Strike models animate

The GoldSrc reader stopped at the bone table. `num_seq` and `seq_index` sat in the header
struct and nothing used them, so every model from those games imported frozen in its
modelling T-stance and asking for "run" changed nothing.

It reads sequences now, decodes the run-length encoded bone tracks, and follows the sidecar
files a model spreads its animations across, which is where most of a Half-Life character's
stances actually live.

- `arctic.mdl`, the Counter-Strike terrorist: **1 pose to 111**. Its Condition Zero
  counterpart carries 494.
- Of ten models with more than one sequence, **ten have one that moves a bone**.
- **6 of 6** models with a sidecar animate out of it.

## Assets come from the game that asked for them

A model names its textures as bare fragments like `metal/metalwall001a`, which only a search
can answer, and with a dozen games mounted many files answer to one fragment. The search
returned whichever entry the hash table reached first, so which game a Counter-Strike model
was dressed from was decided by chance. Nothing about the result looked wrong.

Every Valve game ships a manifest saying which other games it draws on, and those are read
now. A lookup runs in the asking asset's own archive first, then its game, then the games
that game falls back to.

- **898 of 898** lookups for paths held by more than one game answer from the asking game.
- Eight games found with their chains intact, `czero` to `cstrike` to `valve` among them.

Installing Counter-Strike, Condition Zero and Deleted Scenes then showed the measurement had
been taken over Source archives only: Steam keeps all four GoldSrc games in one folder, and
attributing files by their archive put **fourteen thousand loose files in no game at all**.

## Weapons make their own noise

A model's animation events are the only record in these files of which sound belongs to which
action, and they are read and followed to a real file now.

- Source names a soundscript entry rather than a file. Those scripts ship as KeyValues text
  inside the VPKs: **15,145 entries from 450 files**, every sampled name resolving.
- A name beginning with `!` is a sentence, assembled from single-word clips listed in
  `sound/sentences.txt`. **2,407 sentences** read.
- Map sounds reaching a real file went from 238 of 253 to **244**.

## Maps have weather and something happening in them

A level says where the generator hums and where the wind blows, as `ambient_generic` entities
carrying a sound and a position, and those were read off disk and thrown away.

They are placed now with the volume, pitch and radius the mapper set: **27 of 27** declared
sounds across six maps reach a real file. Cues a script fires later are left alone, since
looping the tram announcer the moment Half-Life opens is worse than leaving it out.

## A first-person view built the way these games build it

The weapon is a separate render pass with its own depth buffer, so it never pokes through a
wall, and it is a `v_` model, arms and gun together with its own firing sequences. The player
moves with Quake's acceleration, the constants converted from the engine's own: `sv_gravity`
800, a 250 ups run, and a jump of 268.3 because that is what clearing Half-Life's 45 unit
crate requires. They had been 22.0, 5.0 and 6.5, and those came from nowhere.

## Four things that had been wrong all along

Each was invisible to compiling, to review, and to every automated check.

**Every model faced ninety degrees away from Godot's front.** The axis remap sent Valve's
forward to Godot's right. Both mappings are orthogonal and both preserve winding, so geometry
and entities agreed with each other and maps looked correct. It only showed when something was
asked to face: an NPC aiming at the player stood there sideways.

Two unit tests then failed, and the reason is the point: they had the old mapping hardcoded as
the expected answer. They had not merely missed the defect, they certified it.

**Source animation events were read at 76 bytes and are 80.** The first event of every
sequence read correctly and every one after it was noise.

**A saved scene weighed six megabytes.** Everything an import builds lives only in memory, so
packing wrote all of it into the scene file, most of it one image spelled out as text.
Textures and meshes are written beside the scene now: the same model is **118 KiB**.

**Bodies stood inside the floor.** Three attempts, each wrong in a way the previous
measurement could not see. The first compared a body against itself and reported zero. The
second ran before the physics world existed. The third put the origin on the ground, and a
character's origin is not its lowest point.

## Also

- The dock lists games by the names they call themselves. Steam keeps four in the folder it
  calls Half-Life, and each is now its own entry.
- A model can be brought in able to do more than one thing: idle, walk, run, crouch, jump,
  shoot, reload and die, found by name among the hundreds a model carries.
- Workshop content is mounted alongside the game it belongs to.
- The repository dropped from 10.4 MB tracked to 3.0 MB.

## Installing

Unzip, copy `addons/quebratsk_editor` and `bin` into your Godot 4.3+ project, and enable
**Quebratsk** in Project Settings. Nothing else is required.

## Upgrading from 2.0.x

If you saved scenes with an earlier version, re-import them. Everything imported before this
release is rotated ninety degrees about the vertical axis, and the fix is in the importer
rather than in the saved scene.

Anything scripting against `list_sounds()` needs a look. It used to return the raw names out
of a model, which are not files; it now returns VFS URIs, already resolved.
