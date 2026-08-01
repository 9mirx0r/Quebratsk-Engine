# Changelog

All notable changes to **Quebratsk Engine** are documented in this file.

The format is based on [Keep a Changelog](https://keepachangelog.com/en/1.1.0/),
and this project adheres to [Semantic Versioning](https://semver.org/spec/v2.0.0.html).

---

## [Unreleased]

### Changed

- **The sandbox is gone; there is a lab.** `demo/sandbox/` had grown to thirteen hundred lines
  of patches on patches, and its two scripts were where most of this session's defects lived.
  `demo/lab/` replaces it in about six hundred, because what it used to work out for itself now
  comes from `res://presets`: which of a model's 111 sequences is walking, what a weapon sounds
  like, how fast it lets you move.

  It opens **de_aztec**, not de_dust2. Of the 25 maps Counter-Strike ships, exactly five
  declare no sound at all and de_dust2 is one of them — so testing on it meant the ambient
  sound path was never once exercised and its silence read as a defect. Measured across every
  map: de_torn declares 100 `ambient_generic`, de_piranesi 96, cs_backalley 48, cs_italy an
  opera and some chickens, de_aztec rain and thunder.

  On de_aztec the lab reports **5 ambient sounds playing**, 34 spawn points, and every body
  standing on the floor with an animation running.

### Fixed

- **Weapons floated above people's heads.** An enemy's weapon was positioned by hand relative
  to the body, and a GoldSrc body's origin is the centre of its hull rather than its hands. It
  now hangs from the right hand's bone through a `BoneAttachment3D`, and a model with no such
  bone says so instead of carrying a rifle in the sky.

- **Characters snapped to a T-pose after moving.** Not a missing animation — an ended one.
  `jump` is two seconds long and does not loop, so when it finished `current_animation` went
  empty, the AnimationMixer stopped writing to the skeleton, and the model reverted to its bind
  pose. And `jump` was being chosen constantly, because walking over the ridges of a real map
  loses floor contact for a frame at a time and every one of those started it again.

  Two fixes: a stance is replayed when nothing is playing and not only when the name changes,
  and the feet may leave the ground for 0.12 s before it counts as being in the air.

  The first measurement of this missed it entirely. A single snapshot found all four bodies
  animating and reported the problem solved; the T-pose is a transition, and it took sampling
  **360 physics ticks** to see. Now: **0 frames with nothing playing**, on every body.

- **Automatic weapons fired one shot per click.** Firing was tied to the mouse button going
  down rather than being held, which turns an AK into a semi-automatic. The rate is governed by
  the weapon's own cycle time from the game's tables. Which weapons are genuinely
  semi-automatic is in the game's code and is not read yet; until it is, everything holds.

- **Characters floated above the floor, and the collider was a metre too tall.** Measured on a
  running scene: a 1.8 m person had a **2.67 m capsule** and stood with its feet **1.14 m above
  the bottom of it**.

  The cause is a difference between two spaces that look like one. A GoldSrc entity's origin is
  the centre of its hull, not the soles of its feet: a player is 72 units tall and stands with
  its origin 36 units — 0.91 m — up, and every sequence is authored around that. The bind pose
  is authored standing on the origin instead. So a collider measured from the mesh's bounding
  box describes a body that is nowhere near the one being drawn.

  An earlier attempt widened the mesh box until it swallowed the posed bones, which is where
  the 2.67 m came from: it moved the problem instead of solving it.

  Each sequence declares its own `bbmin` and `bbmax`, so the collider now comes from the
  sequence being shown. Not the union of all of them — that gave a standing character a 3.48 m
  capsule 1.7 m wide, which is the smallest box containing every way the model can ever lie on
  the floor, death animations included.

  After: **capsule 1.54 m, feet 4 mm from the bottom of it.**

  The offsets for `motion_type`, `motion_bone`, `bbmin` and `bbmax` now carry `static_assert`s.
  A first pass at reading them by hand was one field early and returned a motion bone index of
  1115684864, which is the bit pattern of the float 64.0.

- **The player flew.** The body slid around the level frozen in one pose, with no walk, no run,
  no crouch and nothing in the air. The sandbox imported the stances and then never handed them
  over: `setup()` takes a `moves` dictionary as its fifth argument and was called with four, so
  `_drive_animation()` returned on its first line every frame.

  That same early return is what swallowed the audio player, because it was being created at
  the end of the same function. One defect, two symptoms, and neither was visible from any
  check that reads files.

- **Nothing was crouching.** Ctrl scaled the speed by 0.333 and changed nothing else, so
  crouching read as walking slowly. The hull now shrinks from the engine's 72 units to its 36
  and the eye drops from `VEC_VIEW` 64 to `VEC_DUCK_VIEW` 28, with the soles staying put.

- **Two sequences in one model may share a name, and the second was being dropped.** GoldSrc
  addresses sequences by index and never by name, so nothing makes the labels unique:
  `v_tmp.mdl` calls three different firing animations "shoot" and `v_scout.mdl` calls two.
  Godot's `AnimationLibrary` refuses a duplicate, so the extras vanished while `list_poses()`
  went on advertising all six. The reader now disambiguates with ` #2`, ` #3`, and a check
  across all 31 Counter-Strike view models reports **0 sequences lost** between being declared
  and being imported.

- **R did nothing, and two thirds of every firing animation was never imported.** The sandbox
  asked for one sequence per role, which took `shoot1` from a rifle and left `shoot2`, `shoot3`
  and `reload` behind. A view model carries six sequences where a player model carries a
  hundred and eleven, so it now takes all of them.

  Reloading then needed the game's own spelling. The pump shotguns have no sequence called
  `reload`: the M3 and the XM1014 load a shell at a time as `start_reload`, `insert` per shell,
  `after_reload`. And a knife has no `shoot` at all — it has `slash1`, `midslash1`, `stab` and
  `stab_miss`. Measured across every weapon: **27 of 31 have an attack** (the four without are
  the C4 and the three grenades, which are thrown) and **25 of 25 that should reload, do**.

- **The player fired in silence.** `_ready()` had gone missing from the sandbox player: the
  lines that create its audio player and capture the mouse had ended up glued to the tail of
  `_drive_animation()`, which returns early whenever the body has no matching stance. So the
  audio player was either never created — and `_shoot()` checks for it and says nothing — or
  created **once per frame** and hung off the player node.

  A missing sound is invisible to every check that reads files, which is why this survived the
  work that resolved the sounds correctly in the first place. `_shoot()` now warns once when
  it has nothing to play instead of going quiet.

### Added

- **Presets: a model that knows how to act, not just how to be drawn.** Importing gives you
  geometry and a skeleton. What it does not give you is the model being a character — which of
  its 111 sequences is standing and which is running, which of its parts have a second version,
  what it sounds like, how fast the weapon in its hands lets it move. Every caller was working
  that out again from scratch, the sandbox one way and the dock another.

  A preset is that answer, derived from the game and written to `res://presets/<game>/`. **31
  weapons and 9 characters for Counter-Strike 1.6**, generated by
  `demo/tests/build_presets.tscn` and committed, so they can be read, corrected and diffed.
  Derived rather than authored on purpose: a hand-written table is a claim about the game, and
  claims about these games have been wrong more often than not here.

  Presets record the game's own path (`models/player/guerilla/guerilla.mdl`) and never the
  mount they were built under, which is a fact about one session and would produce a file that
  works only in the project that made it.

- **The sounds a level makes that no model records.** A `.mdl` names the sounds its own
  animations trigger. That covers a gunshot. Footsteps, the C4 beeping and being planted and
  going off, a grenade bouncing off a wall, hitting flesh, taking a fall, drowning, the radio,
  the hostages — all of it lives in the game library, so an imported character walked across
  de_dust2 in perfect silence and nothing that reads files could have noticed.

  `tools/build_sound_catalogue.py` reads **313 sound names** out of ReGameDLL_CS and groups
  them by what they are for. Checked against a retail install: **200 are present**, and every
  group that matters is complete — 43 of 43 footsteps, 12 of 12 pain, 5 of 5 death, the C4's
  beep, plant, defuse and explosion, and both grenade groups. The 113 absent are almost
  entirely hostage voice lines that ReGameDLL supports and retail 1.6 does not ship.

  The player has footsteps now, on Counter-Strike's own rules: no step in the air, silence
  while crouched or holding Shift, and an interval measured in distance covered — 220 units
  walking, 300 running — rather than in time, which is why they stay even whatever the
  animation is doing.

- **The sandbox is one game at a time, and that game is Counter-Strike 1.6.** Steam keeps
  Half-Life, Counter-Strike, Condition Zero and its Deleted Scenes inside a single folder
  called Half-Life, so mounting "the installed game" mounted four of them and produced a
  Condition Zero soldier holding a Half-Life crowbar on a Counter-Strike map. Every part of
  that is meant to work and none of it can be judged. `ONLY_GAME` in the sandbox pins it;
  set it to `""` for everything installed.

  Startup went from 330 mounted containers to 23.

- **Crouched movement is a stance the importer knows about.** Counter-Strike calls it
  `crouchrun` and has no crouched walk at all, so a character that could stand, walk and crouch
  still slid the moment it crouched and moved.

- **A model says which of its pieces you are not seeing.** A GoldSrc model can have parts that
  come in several versions: `v_357.mdl` has a part called *scope* that is either blank or a
  laser sight, and `v_9mmhandgun.mdl` has a silencer. Only the first version is built, which is
  what the game does at any given moment, and the problem was never that choice. It was the
  silence: somebody hunting for the pistol with the silencer had no way to know it was in
  there.

  `list_body_groups()` reports them and `list_attachments()` reports the named points on the
  skeleton where a muzzle flash or a shell ejection belongs, which nothing else in the file
  records. Across forty weapon and player models, three hide a piece each and **30 carry
  attachment points**, none of which were read before.

- **And you can ask for the other one.** `load_model()` and `load_character()` take a
  `body_choices` dictionary keyed by the part's own name — `{"scope": 1}` builds the .357 with
  the laser sight — and the dock grows a dropdown per part, so a model with something to
  choose says so where you are looking at it. A part left out keeps what the game shows, so
  nothing about an ordinary import changes.

  Verified by counting vertices rather than by looking, because these are small pieces on
  models with thousands of triangles. Eight of eight models with alternatives built different
  geometry when asked: the laser sight is +98 vertices on CS 1.6's `v_357.mdl` and +174 on
  Condition Zero's, the silencer is +59 on `v_9mmhandgun.mdl`, and `arctic.mdl` — the player
  model — turned out to carry the **C4 backpack** as the alternative version of its back, at
  +42. That is the pack that marks the bomb carrier, and there was no way to ask for it.

- **A level says what is in it.** A `.bsp` showed a filename and a size, which tells you
  nothing about whether you are looking at a bomb site or a corridor. The map's own entity
  list is the only description of it that exists and it was being read and discarded.

  Picking a level now reports what it contains: *21 Player deathmatch start, 20 Wall, 15
  Player 1 start, 10 Trigger player hurt, 9 Ladder, 8 Plays Sounds/Sentences*. Those names
  come from `data/goldsrc_entities.json`, generated from the FGD files the level editors
  used: **520 entity classes** across Half-Life, Counter-Strike, Condition Zero and its
  Deleted Scenes, Opposing Force, Blue Shift and Day of Defeat.

  The schema caught a defect on the day it arrived. How far an `ambient_generic` carries is a
  spawnflag, not a key, and the sandbox read a `radius` key that no GoldSrc entity has, so
  every map sound played at the default reach regardless of what the mapper chose.

- **A weapon knows what it is.** A model does not say which weapon it is: it carries a mesh,
  a skeleton and some sequences, and everything that makes an AK an AK rather than a shape
  lives in the game's code. Quebratsk inferred it from filenames, and the rule for firing
  sounds, `v_famas.mdl` to `weapons/famas-1.wav`, is true and is still a guess.

  `data/cs_weapons.json` carries the answer instead, generated from ReGameDLL_CS by
  `tools/build_weapon_manifest.py`: **29 weapons** with their view, player and world models,
  their sounds, their damage, clip size, price, weight and the speed each one allows a player
  carrying it. Reachable from any of a weapon's faces, so a caller holding `w_ak47.mdl` can
  ask what it fires without knowing the answer lives on a different file.

  Measured against installed games: **29 of 29 view models and 27 of 27 firing sounds resolve
  to a real file.** The weapons with no damage or clip are the knife, the C4 and the three
  grenades, and that absence is the answer rather than a gap.


## [2.1.0] "Volvokjark" - 2026-08-01

The release where Half-Life 1 and Counter-Strike stopped standing still, and where four
separate things that had been quietly wrong for months were found by looking at the screen
rather than at the code.

### Added

- **Half-Life 1 and Counter-Strike 1.6 models animate.** The GoldSrc reader had no notion of
  a sequence at all: it read the bone table and stopped, so every model from those games
  imported frozen in the modelling T-stance and asking for "run" changed nothing. It now
  reads the sequence table, decodes the run-length encoded bone tracks and reports the sound
  events. `arctic.mdl`, the Counter-Strike terrorist, went from **1 pose to 111**, and its
  Condition Zero counterpart to 494. Of ten models carrying more than one sequence, **ten
  have one that moves a bone.**

  A model can also spread its sequences across several files, keeping only the first few in
  itself and the rest in `<name>01.mdl` beside it. That is most of a Half-Life character:
  `hgrunt.mdl` has 82 sequences and `hostage.mdl` 129, and almost none of them are in the
  model. Those files are fetched and read too, so **6 of 6 models with a sidecar animate out
  of it.** One that is not on disk leaves its sequences named, with their sounds, standing in
  the bind pose, and says which file was wanted.

- **A weapon's sound is the one the weapon names.** A model's animation events are the only
  record in these files of which noise belongs to which action, and they are now read and
  followed to a real file. Source does not name a file there: it names a soundscript entry,
  "Weapon_357.Single", which the games resolve through `scripts/game_sounds_*.txt`. Those
  scripts ship as plain KeyValues text inside the VPKs and are now read as well: **15,145
  entries from 450 script files**, with every name the sampled models asked for resolving.

- **A first-person view with a body under it.** The player was a capsule with a camera on
  top, so looking down showed nothing and the weapon was pinned to the camera at a fixed
  offset, which is what made it look pasted onto the screen. The player is now a real
  character model, the view sits where that model's own head bone is, and the weapon hangs
  off its right hand. Looking down shows the legs and arms that are actually standing there.

  The camera follows the head bone's position but keeps its own aim. Parenting it to the bone
  would hand it that bone's orientation, which in these models points wherever the artist
  left it, and every nod of the walk cycle would end up steering the crosshair.

  Measured across eight character models rather than judged from a screenshot: **8 of 8 have
  a head bone findable by name, place it in the top fifth of the figure, move it while the
  animation plays, and carry a right hand bone an arm's length away.** Both naming
  conventions are handled, `ValveBiped.Bip01_Head1` and `Bip01 Head`.

- **A model can be brought in able to do more than one thing.** The dock imported one
  sequence, so a character arrived able to stand still or able to walk, never both, and
  anyone wanting idle, walk and run had to import the same model three times and reconcile
  three scenes afterwards. The engine could always do more; nothing asked it to.

  "Bring it in moving" now offers the usual moves as well as the single chosen one: idle,
  walk, run, crouch, jump, shoot, reload and die, found by name out of the hundreds a model
  carries. A Condition Zero player model has 494 sequences and almost all of them are
  situational. Across six models, **38 roles were recognised, all 38 imported, and all 38
  move a bone.** No sequence is allowed to stand for two roles, since one file imported
  under two names looks like a model that can both walk and run when it can only do one.

  The naming knowledge lives in one place shared by the dock and the sandbox, which is what
  keeps them from disagreeing the first time one learns a convention the other has not.

- **A saved scene weighs what it should.** Everything an import builds lives only in memory,
  so packing it wrote all of it into the scene file: a model with two 1024-pixel textures came
  out as a **six megabyte .tscn**, most of it one image spelled out as text. Fifty models would
  have been three hundred megabytes of scene files that Godot cannot compress and parses as
  text on load. Textures and meshes are written as their own resource files beside the scene
  and referenced, which also means two models sharing a texture share the file. The same model
  is now **118 KiB**, and four of four saved scenes reload with every surface, texture, bone
  and animation key they had.

- **Saving printed an error on every attempt.** The scene root was told to own itself, which
  Godot refuses. Harmless to the result and noisy enough to hide something that was not.

- **The game dropdown lists the games, by the names they call themselves.** It listed what
  mounting a folder happened to be called, and Steam keeps four games in the folder it calls
  Half-Life: Half-Life, Counter-Strike, Condition Zero and its Deleted Scenes. Picking
  "Half-Life" handed back all four mixed together with no way to say which was meant.

  The names come from each game's own manifest rather than from a table somebody would have
  to keep, so a mod is named by what it calls itself too. Where two games agree on a name,
  as Episode One and Episode Two both do by calling themselves HALF-LIFE 2, the folder is
  added to tell them apart. Searching can now be restricted to a game rather than to a mount,
  which are not the same thing: one game is several mounts and one mount can hold four games.

  Measured on one folder holding eight games: **all five sampled narrow the search and none
  return a file from another game.** Garry's Mod and Half-Life 2 together account for 41,969
  files and split exactly, with nothing counted twice.

- **A map plays the sounds it declares.** A level is not only a shape: it says where the
  generator hums, where the wind blows, where water runs, as `ambient_generic` entities
  carrying a sound name and a position. Every import read those off disk and threw them away.
  They are placed now, each as a positional source at its own origin, with the volume, pitch
  and radius the mapper set. Across six maps, **27 of 27 declared sounds reach a real file.**

  A map uses that same entity for one-shot dialogue a script fires later, and the difference
  is a spawnflag rather than anything about the sound. Those are left alone: the tram ride
  announcer at the start of Half-Life is one, and looping it the moment a level opens is
  worse than leaving it out.

- **A world model borrows the sound of the weapon it is.** A `w_` model is the shape a gun
  has in someone's hand and carries no sequences at all, so it names no sound. The firing
  sound lives on the view model of the same weapon, and that is where it is read from now:
  `w_smg_mp5.mdl` plays what `v_smg_mp5.mdl` says it plays. The twin is resolved against the
  asking model, so a Counter-Strike world model finds the Counter-Strike view model.

### Fixed

- **A map's spoken lines are found, not just its noises.** A sound name beginning with `!` is
  not a file: it is a sentence in `sound/sentences.txt`, a line naming a directory and then a
  list of single words that are stitched together in order, so `HG_ALERT1` is three separate
  clips. Those are read now, 2,407 of them across seven files, and a bare group name resolves
  to its first numbered member. Across eight Condition Zero maps, declared sounds that reach a
  file went from **238 of 253 to 244**.

- **The player moves the way Quake moves.** Velocity was set straight from the input direction,
  which starts and stops instantly and carries no momentum through a turn. Acceleration is now
  the engine's own: add only the shortfall along the wished direction, measured with a dot
  product, capped in the air at 30 units per second. Air strafing and bunny hopping fall out of
  that one clamp rather than being features on top of it.

  The constants are the engine's too, converted once from Hammer units: `sv_gravity` 800 for
  20.32 m/s², a 250 ups run for 6.35 m/s, and a jump of 268.3 ups because that is what clearing
  Half-Life's 45 unit crate requires. They were 22.0, 5.0 and 6.5, and those came from nowhere.

- **The first-person view is built the way these games build it.** A weapon was hung off the
  player model's hand bone and the player's own body was left in front of the camera, which is
  not how any of this works. In GoldSrc the view model is a separate entity drawn in a pass of
  its own after the world, with the depth range squeezed into the front of the buffer so the
  barrel never pokes through a wall, and the player's own body is not drawn at all.

  There is now a second camera with its own depth buffer seeing only the weapon, composited on
  top, which is what that depth-range trick stands in for and the usual way to do it in Godot.
  The model is a `v_` one, which is arms and gun together with its own draw and fire sequences
  and the sound events that belong to them. The body stays in the scene casting its shadow.

  Worth stating plainly: **body awareness of the kind DayZ has cannot be built from these
  assets.** No model in Half-Life or Counter-Strike shows your own body from your own eyes. A
  `v_` model is arms only, a `p_` model is what other players see, and a GoldSrc mesh is a
  single surface so a head cannot be hidden to reuse one from the inside.

- **NPCs turned their backs.** The yaw aimed +Z at the player and Godot's front is -Z. It was
  written to compensate for the broken axis remap below, so it read as ninety degrees off
  before that was fixed and as a clean hundred and eighty after.

- **A character could hang out of its own collider.** The capsule was measured from the mesh's
  rest-pose bound, and the pose actually applied need not fit inside it. One Condition Zero
  model came out with its bones 0.93 m below its capsule, which looks exactly like a figure
  sunk to the waist in the floor. Measured across five characters, four agreed to within a
  centimetre and one did not, so it was the model rather than the arithmetic. The capsule now
  covers the pose it was given, and all five agree.

- **Every imported model faced ninety degrees away from Godot's front.** The axis remap sent
  Valve's forward to Godot's right instead of to Godot's forward. Both mappings are orthogonal
  and both preserve triangle winding, so geometry and entities agreed with each other and maps
  looked correct. It only showed when something was asked to face: an NPC aiming at the player
  with `look_at()` stood there sideways, and a first-person camera offset along the body's
  facing walked into the model's own shoulder.

  Four verification harnesses missed it because all of them measured counts, heights and
  distances, and a rotation changes none of those. A screenshot found it. `verify_first_person`
  now measures the line between a figure's shoulders, which is the one thing that betrays a
  rotation without depending on the pose, and all eight sampled models pass.

- **A body vanished after it fell.** An NPC played its death animation and was then deleted two
  seconds later, so the animation ran and the evidence of it was removed. Corpses stay; they
  stop being something to walk into without ceasing to be something to look at.

- **No weapon at all on a GoldSrc map.** Weapons were searched for under `models/weapons/`,
  which is where Source keeps them and where GoldSrc keeps nothing. GoldSrc files them loose in
  `models/` behind a one-letter prefix, so `models/p_ak47.mdl`. Both layouts are searched now,
  preferring the `p_` model, which is the one made to be held.

- **The scene said everything came from Half-Life.** The label read the folder a file was
  mounted under, and Steam keeps four games in the folder it calls Half-Life. It reads the
  game now, so a Condition Zero map says Condition Zero.

- **A lookup answered with whatever the hash table reached first.** A model names its textures
  as bare fragments, "metal/metalwall001a", which only a search can resolve, and with a dozen
  games mounted many files answer to the same fragment. The search returned the first entry
  the bucket layout happened to yield, so which game a Counter-Strike model was dressed from
  was decided by the hash. Nothing about the result looked wrong from the outside.

  Every Valve game ships a manifest saying which other games it draws on, and those are now
  read: `liblist.gam` for GoldSrc, `gameinfo.txt` for Source. A lookup runs in the asking
  asset's own archive first, then its game, then the games that game falls back to, then
  everything else, with the mount order as a final tie-break so an answer never depends on
  chance. Eight games were found on the test machine with their chains intact, among them
  `cstrike` to `valve` and `ep2` to `episodic` to `hl2`. Of **281 lookups for paths that
  exist in more than one game, 281 now answer from the asking game.**

  Two later corrections came from installing four more games. A mounted directory can hold
  several games at once, which is exactly how Steam lays out GoldSrc: Half-Life, Counter-
  Strike, Condition Zero and its Deleted Scenes all sit in one folder. Attributing files by
  the archive they came from put **fourteen thousand loose files in no game at all**, and
  since GoldSrc keeps nearly everything loose, that was most of GoldSrc. The game is resolved
  per file now, cached per directory. The measurement went from 281 lookups over 135 contested
  paths to **898 over 356**, all of them answered from the asking game.

  The other is that Valve ships a stray `liblist.gam` inside `scripts/` in more than one game,
  which made a folder called "scripts" a game of its own and took every soundscript out of the
  search order of the game shipping it. The outermost manifest wins, and a folder with a
  reserved content name is never a game.

  Two things the manifests do not say outright had to be handled. Counter-Strike 1.6 declares
  no fallback at all, because GoldSrc takes the base game as given, so a GoldSrc mod falls
  back to `valve` unless it says otherwise. And Counter-Strike 1.6 and Counter-Strike Source
  both live in a folder called `cstrike`, so a game is identified by its directory rather than
  by its name; identifying by name made the two one game that could read each other's files.

- **Source animation events were read at the wrong stride.** `mstudioevent_t` is 80 bytes,
  not 76: the trailing name index is easy to miss and missing it is not harmless. The first
  event of a sequence still read correctly and every one after it was garbage, so a weapon
  whose gunshot is its second event looked like a weapon with no sound at all.

- **Sound events were picked out by looking at their text.** A firing sequence carries
  "357 muzzle" and "Weapon_357.Single" side by side and neither looks like a filename, so a
  filter on `.wav` rejected both. Events are now identified by what they are, through the
  event name for models compiled with named events and through the event id for older ones.

- **A model with an unnamed bone built a scrambled skeleton.** Godot rejects an empty bone
  name, and a rejected bone is not skipped: it shifts every parent index after it. Several
  stock GoldSrc models have them. Bone names are now made acceptable in one place, applied
  to the skeleton and to the animation tracks alike so the two cannot disagree.

- **A map's tool brushes were drawn as walls.** `AAATRIGGER` is the texture Half-Life puts
  on trigger volumes so a mapper can see them in the editor, and the engine never draws it.
  It is a 16x16 magenta tile with a lambda on it, and a map with a few triggers came out
  with solid magenta slabs standing in rooms that are open in the game. `CLIP`, `ORIGIN`,
  `HINT`, `SKIP` and the sky brushes are all skipped now.

  Two hypotheses died first. It was not a missing `.wad`: de_survivor embeds all 95 of its
  textures. It was not the masked `{` family either: those decode correctly, and 12 of its
  surfaces come back with working transparency, one with 4,096 transparent pixels out of
  4,096. The answer only appeared when the textures were written to disk and looked at.

- **A downloaded map's `.wad` files were indexed rather than opened.** Mounting a folder
  indexes a `.wad` as one file; it does not read the textures inside. A map bundle whose
  textures live in its own archives therefore reported every `.wad` present and rendered
  more than half of itself blank. `de_caminito` went from **68 of 123 surfaces textured to
  122 of 123** once its four archives were opened as containers.

### Added

- **A map brings its own sky.** `worldspawn` names it as `skyname` and the six images sit
  in `gfx/env`. None of that was read, so every level rendered against flat grey. The sky
  also lights the scene now, rather than a level being lit by one directional light.

- **`load_map_entities()`**, which parses the entity lump the parser had always read and
  thrown away. One Dictionary per entity with the map's own keys, plus `position` converted
  to Godot's axes so it lines up with the geometry.

  Surveyed rather than assumed: Counter-Strike 1.6 writes `info_player_start` for
  counter-terrorists and `info_player_deathmatch` for terrorists, about 13 of each in all 12
  maps read. Half-Life uses the first for its single player and the second only in its three
  deathmatch maps. Half-Life 2 and Garry's Mod write neither that this can read, because
  their maps are VBSP.

- **`list_sounds()`**, the sounds a named sequence plays. A model's sequences carry an event
  list, and a sound event holds the filename in its options field. That is the only record
  in any of these files of which noise belongs to which action: a weapon's firing animation
  names its own gunshot, and picking one any other way is picking at random.

- **`load_character()`**, a `CharacterBody3D` with the capsule at the root and the skeleton
  beneath it, which is the layout Godot expects of something that moves.

- **Materials carry the name of the texture they came from**, so a surface that looks wrong
  can be traced to a file instead of showing an unnamed material.


- **Source models imported without their textures, and nothing said so.** Two links in the
  same chain were missing, and each one hid the other.

  A model states its materials in two halves: the names (`police`) in one table, and the
  directories to look for them in (`models/player/police/`) in another. The second was never
  read, so the loader had a bare name and searched the whole index for anything ending in
  it, which finds another model's texture as readily as the right one, and often finds
  nothing.

  With the directories read the search landed on the correct file and still produced
  nothing, because a `.vmt` is a material script rather than a picture: it *names* an image.
  Those bytes were being handed to an image decoder. The reference is followed now.

  Measured over 120 models drawn from every mounted game: **155 of 155 character surfaces,
  56 of 56 weapon surfaces and 44 of 44 prop surfaces come back with an image**, where
  before they came back flat. `demo/tests/verify_materials.gd` counts it, because a model
  that renders grey and a model with no textures look identical from the outside and both
  look like a successful import.

- **A material that cannot be found is now named.** `get_last_missing_companions()` covered
  the `.vvd` and `.vtx` a model cannot be built without; it also lists every material the
  model asked for and did not get, so a flat-looking import can say which files are absent
  rather than leaving it to be guessed.

- **The gunshot that could deafen you.** RIFF stores 8-bit samples unsigned and Godot's
  8-bit format wants them signed, so every sample was inverted around the midpoint and
  played back as a full-amplitude blast instead of a sound. Most GoldSrc audio is 8-bit,
  which made it almost everything Half-Life and Counter-Strike have, including in the dock's
  preview.


- **Every file inside a Real Virtuality PBO that was not at the archive root was
  unreachable by path.** PBOs store their paths with backslashes, and `index_pbo()` was the
  one container indexer that did not convert them, so a DayZ texture was filed under a name
  containing \\ separators. Anything that normalised a path before looking it up then missed:
  `find_by_suffix()` could not match a nested file, the dock's folder categories never
  matched Arma or DayZ content, and the texture loader converted the separators and was told
  the file did not exist. Only files sitting at a PBO's root, with no separator to disagree
  about, ever resolved. All five container indexers now file paths the same way, through one
  function that states the convention.

- **`get_file_size()` reported 0 for most of a DayZ install.** A PBO entry only fills in
  its uncompressed size when it is compressed; an uncompressed one leaves the field at zero
  and its stored size is its real size. Taking the field at face value meant the dock showed
  "0 B" beside files it could read perfectly well, including a 213 MB terrain.

- **The dock's texture preview never showed anything.** `load_texture()` only knew how to
  resolve a texture *reference*, the kind a material names, by searching the index for a
  matching suffix. Handed a full `vfs://` URI, which is what every caller holding a search
  result actually has, the search asked for an entry *ending in* `/vfs://mount/path` and
  could never match. It returned null without a word, and the preview silently drew nothing.

### Changed

- **The maps row claimed Source, which was never true.** It listed BSP30 for both GoldSrc
  and Source 1. Half-Life 2 and Garry's Mod maps are VBSP version 19 and 20, and all 81 of
  them decode to nothing. GoldSrc's 150 do work, and now come in with collision.

- **The `.wrp` row in the supported table now says what it actually covers.** It claimed
  Arma and DayZ terrain. DayZ's `chernarusplus.wrp` and `enoch.wrp` are OPRW version 29, and
  the reader recognises the magic and then decodes nothing from them. Older Arma terrain may
  work, but there is no Arma install here to say so, and a parser that compiles is not what
  this project calls supported.

### Added

- **Imported things are solid.** A map arrived as geometry and nothing else, so you walked
  straight through it; a model was equally intangible. `load_map()` returns a
  `MeshInstance3D` carrying a `StaticBody3D` with a trimesh collider, which is the same
  structure Godot's own "Create Trimesh Static Body" produces, so anyone who opens the node
  later finds something they recognise. Models get a capsule sized from their bounds, since a
  trimesh collider on a skinned mesh would stay frozen in the rest pose while the mesh
  animates away from it.

  Verified with physics rather than by reading the tree: a ball dropped from 15.6 m above
  `as_oilrig.bsp` comes to rest at y=10.33 on a 16,309-triangle collider. A player model
  produces a capsule 1.79 m tall and 0.55 m across, centred at hip height.

- **`load_character()`**, which returns a `CharacterBody3D` with the capsule at the root and
  the skeleton beneath it. That is the layout Godot expects of something that moves, so a
  script on the root has `move_and_slide()` without any rearranging. `load_model()` still
  gives a static body, which is right for scenery and wrong for anything meant to walk.

  Verified by walking one: an imported model spawned on an imported GoldSrc map lands on the
  floor and travels 2.00 m in a second of being pushed at 2 m/s, which is unobstructed
  movement rather than the 0.00 m of something wedged.

  Getting there turned up two things worth writing down. The capsule was sized from the
  model's **wider** ground axis, which in a rest pose is the arm span: that gave a person a
  1.1 m thick collider that lands fine and then cannot fit through anything. And a character
  dropped in from above a GoldSrc map lands on the *outside* of it, because a BSP is a sealed
  box, so a spawn point has to be found by casting a ray downward from inside.

- **Category icons, a backdrop behind the 3D preview, and an illustration for the empty
  state**, plus the checkerboard stand-in every engine here uses for a texture it could not
  read. Cosmetic. The empty-state art went back once for having the word AMMO legible on
  its crates, which a translated interface cannot use.

- **A game to search in.** With more than one game mounted, "police" returned hits from all
  of them mixed together and the origin column only told you where something came from once
  you had already found it. A dropdown beside the category picker narrows the search to one
  game, and it appears only when there is more than one to choose between.

  The filter is applied in the engine rather than to the page, for the same reason the
  companion-file exclusion is: filtering afterwards would hand the caller a page drawn from
  every game and leave whatever survived.

- **Real Virtuality textures import.** `.paa` in DXT1, DXT5 and the packed ARGB and
  grey-plus-alpha layouts, including the LZO-compressed mipmaps that nearly all of them use.
  Listed in the supported table, because it earned it: against a retail DayZ install with
  eight workshop mods, 29,018 files reachable and **1,499 of a 1,500-file sample decoded**.
  The single refusal is a genuinely truncated file.

  The first run of this reader decoded 42 of 1,500. **1,457 of them store their mipmaps
  LZO-compressed**, so a reader without the decompressor would have worked on 3% of what it
  was pointed at, which is why it shipped in 2.0.2 unlisted.

- **An LZO1X decompressor**, in `src/core/vfs/decompressors/`. Distinct from the LZSS beside
  it, which is what PBO entries use; the two get confused because both arrive in Bohemia
  containers, and they share nothing.

  It is written in the reference decoder's shape, gotos included, because that algorithm's
  states genuinely jump into each other's middles and every restructuring produces a subtly
  different decoder that still handles most inputs. What was added is the bounds checking the
  reference leaves to its caller: every read, every write, and every back-reference.

  The output size is a parameter rather than something the stream declares, because the
  caller always knows it, a DXT payload's size follows from the mipmap's dimensions. That
  turns the size into the test. A decompressor that has gone subtly wrong essentially never
  lands on exactly the right byte count, and this one landed on it 1,456 times out of 1,457.

---

## [2.0.2] "Herkav" - 2026-07-31

Releases carry a name from here on. This one is Herkav.

Two things it commits the project to. The roadmap now names the engines this is going
after rather than gesturing at them, and it says which of them is hard and why. And every
capability added here has to be reachable by someone who has never opened Godot: an engine
feature nobody can find is not finished.

### Added

- **Animations import and play.** Until now a model came in wearing one frozen frame of one
  sequence. Naming sequences brings them in as `Animation` resources on an `AnimationPlayer`
  attached to the skeleton, under the labels the game itself uses:

  ```gdscript
  var npc := importer.load_model(uri, "idle_all_01", PackedStringArray(["walk_all"]))
  npc.get_node("AnimationPlayer").play("walk_all")
  ```

  In the dock this is the **Bring it in moving** tickbox beside the pose list.

  The decoder already took a frame index and handled the run-length walk; what was missing
  was that both call sites passed a hard-coded zero, sections were resolved for frame 0
  only, and `AnimationConverter` had never had a producer — it wrote a track path,
  `%GeneralSkeleton:`, that matches nothing this project builds.

  Nothing decodes unless it is named. A model carries hundreds of sequences and each is one
  keyframe per bone per frame, so importing them all would cost far more than any caller
  wants. Tracks whose value never changes collapse to a single key, which is most bones in
  any one sequence.

- **Sounds are things you can keep.** They were found, listed and playable, and that was
  the end of it: the dock marked them unplaceable, so neither *Add to scene* nor *Save* did
  anything with one.

  Picking a sound now enables both. *Add to scene* drops an `AudioStreamPlayer3D` at the
  origin, positioned rather than flat, because a footstep or a door belongs somewhere.
  *Save* writes the audio file itself into `res://imported/` rather than a scene wrapping
  it: the bytes cross untouched and Godot imports them with its own importer, which is
  lossless and leaves you with a file the rest of your project can use.

  Playback also reaches past WAV. `.mp3` and `.ogg` are handed to Godot's own loaders,
  which take the file's bytes directly. Verified against 9,465 WAV and 338 MP3 files across
  Half-Life 2 and Garry's Mod; neither game ships an `.ogg`, so that path is written but
  has not been run against real data.

- **A reader for the Real Virtuality `.paa` texture container**, in
  `src/parsers/rv_enfusion/paa_parser.cpp`: the format word, the tagged-record chain, an
  optional palette and the mipmap table, decoding DXT1, DXT5 and the packed ARGB and
  grey-plus-alpha layouts. The DXT payload goes to the block decoder this project already
  had.

  **Not yet listed as supported, and not wired into the texture loader.** It has been read
  only against files this project built from its own reading of the format, which shows the
  reader agrees with the test and nothing more. A retail install settles it; a DayZ one
  carries 11,405 `.paa` files, and that check is the next thing to run.

  What the test does settle is refusal. A file that is not a PAA, an empty buffer, a mipmap
  claiming sixteen megabytes inside a forty-byte file, and every one of the 46 truncations
  of a valid file are all rejected rather than read past the end of the buffer. That sweep
  caught a real defect while it was being written: the mipmap chain ended quietly when it
  ran out of bytes, so a half-written file decoded whatever had reached the first mipmap
  and reported success. The terminator is required now.

  Mipmaps flagged compressed are refused with an error saying exactly that, rather than
  handed to a block decoder that would render them as noise. Which files those are, and how
  many, is a question for the retail check.

### Performance

- **Adding a game got five times faster, and reopening the editor four.** Indexing a folder
  of loose files cost 300 ms for 1,660 files, while the 24,330 entries of the two archives
  beside it cost 27 ms between them: 180 microseconds per loose file against 1.1 per archive
  entry.

  Two calls per file were doing filesystem work the walk had already done.
  `std::filesystem::relative()` resolves both of its arguments through `weakly_canonical`,
  chasing symlinks that cannot be there when the walk is rooted at the directory being
  subtracted; `lexically_relative()` is pure string arithmetic. And `file_size()` re-opened
  each file to learn a size the `directory_entry` had cached during enumeration.

  | | before | after |
  |---|---|---|
  | `mount_directory`, 1,660 files | 300 ms | **19 ms** |
  | Adding Garry's Mod | 363 ms | **68 ms** |
  | Adding Half-Life 2 | 279 ms | **104 ms** |
  | Reopening the editor with both restored | 588 ms | **143 ms** |

  Found by measuring rather than reading. Two earlier guesses at where this time went, the
  index hash map and a companion-file cache, were both wrong and both worth about nothing.
  `demo/tests/verify_dock.gd` prints the split per call now, so the next guess has to argue
  with it.

- **The results list has an order.** `find_files()` iterated a hash map, so which 400 of
  41,969 matches you were shown was whatever the bucket layout gave, it changed whenever the
  map was resized, and *"Showing the first 400 of 41,969"* named a first that did not exist.
  The same search twice could return different rows.

  Matches are now sorted by filename, with the full path as the tiebreak. On the filename
  rather than the whole URI, because that is what puts Garry's Mod's `police.mdl` next to
  Half-Life 2's; sorting by URI groups by mount prefix, so a 400-row page would come
  entirely from whichever game sorts first and the other one's matches would never appear.

  Only the page is sorted, not all 41,969 matches, and the filename offset is carried
  alongside each match rather than rescanned on every comparison. The whole thing costs
  about half a millisecond: a list refresh went from 15 ms to 17 ms.

- **`VFSEntry` no longer carries a copy of its own key.** Every indexed file stored the
  `vfs://` URI it is filed under, character for character: 60,584 redundant string
  allocations for a two-game setup, rebuilt on every mount. Its one reader, `unmount()`,
  has the key in hand already.

### Fixed

- **Save ignored the pose you picked.** *Add to scene* adopts the previewed node, so it
  arrives in the chosen stance, but *Save* rebuilt the model from scratch with no pose and
  no animation. The two buttons produced different models from the same selection. Saving a
  multi-selection still uses each model's own default, since a sequence label picked against
  one model rarely exists in another.

- **85 animation sequences were missing from every Garry's Mod player model.** A section
  whose data begins at the first byte of its animation block records an offset of zero, and
  the resolver rejected zero everywhere as invalid. It is only invalid for data stored
  inline, where offset zero from the descriptor *is* the descriptor. `list_poses()` on a
  player model returns 426 sequences where it returned 341, and long sequences no longer
  stop at whichever section happened to land on a block boundary — a 9.3 second idle
  decoded 240 of its 280 frames.

  Found by counting keyframes against the declared duration rather than by reading the
  code, which is also why the animation's length is now taken from the frames actually
  decoded: a partial decode used to claim the full duration and leave the model frozen
  through the part that was never read.

---

## [2.0.1] - 2026-07-31

Everything since `0.7.0-alpha`. Version 2.0.0 was tagged and published without its own
entry here, so this section covers that release as well as the work that followed it —
rather than reconstructing a split after the fact and getting it subtly wrong.

### Added

- **A 3D preview before importing.** The dock shows the picked model or map in a rotatable
  viewport — drag to turn it, wheel to zoom. Until now the only way to know whether you had
  the right `police.mdl` was to import it, look, and undo.

  The camera frames whatever is there rather than sitting at a fixed distance: a character
  is under two metres and a map is tens, so the distance comes from the asset's bounding
  box, and the pivot is centred on it so orbiting turns the model in place instead of
  swinging it around a corner of its bounds.

  Loading is debounced by 250 ms. A full import is ~230 ms against ~70 ms for the pose list
  alone, so arrow-keying down 400 results would otherwise rebuild a model per keypress.
  Changing the standing pose rebuilds the preview, which is the point of having one.

  **Add to scene adopts the previewed node** instead of importing a second time. It is
  already built with the pose the user settled on, so re-importing would repeat the work to
  produce the same thing — and risk handing over something other than what they were
  looking at.

  This is also the first consumer of `MapPreviewViewport`, a registered class that until now
  nothing used.

- **`QuebratskSettings.max_background_threads`**, defaulting to cores − 1 clamped to 1–8,
  read at the point of use so a change takes effect without a restart. It is the only
  setting the engine declares, because it is the only one the engine reads.

- **Community templates and a contributing guide** — `.github/ISSUE_TEMPLATE/`,
  `.github/PULL_REQUEST_TEMPLATE.md`, `CONTRIBUTING.md`.

  The bug form requires the four things without which an import report cannot be acted
  on: Godot version, plugin version, the exact game and file that failed, and the
  untruncated Output panel text. It also states up front which formats are *not*
  supported, so those never become bug reports.

  `CONTRIBUTING.md` carries the rule this project learned the hard way: **a format is not
  documented as supported until it has been run against a retail game installation.** Both
  the PR checklist and the guide restate it.

- **Spanish (Río de la Plata) translation of the dock** —
  `demo/addons/quebratsk_editor/i18n/dock.csv`. 60 strings, English source as the key so a
  missing translation falls back to English rather than showing a symbol. Not yet wired:
  the dock still needs `tr()` and a Localization entry, so the file is inert.

- **Editor icons** — `demo/addons/quebratsk_editor/icons/`. Twelve monochrome 16×16 SVGs
  (six asset types, three engines, add, remove) plus the plugin icon. Monochrome and
  `currentColor` so the editor tints them per theme; a coloured icon breaks in light mode.
  Not yet wired either — the dock still resolves icons through Godot's built-in
  `EditorIcons`.

- **Ready-made categories**, so content can be browsed without knowing what to search for.
  Both GoldSrc and Source lay assets out by convention, and the folder says far more about
  what a thing *is* than its extension does — `.mdl` only means "model". Categories match
  on folder as well as type:

  | Category | Matches | Garry's Mod + Half-Life 2 |
  |---|---|---|
  | Characters & people | `models/player`, `/humans/`, `/npc`, `/zombie`, `/combine_`, `/police` | 333 |
  | Weapons | `/weapons/`, `/w_`, `/v_`, `/shells/` | 234 |
  | Vehicles | `vehicle`, `/cars/`, `/car_`, `/airboat`, `/buggy`, `/jeep` | 56 |
  | Props & scenery | `/props`, `/furniture`, `/gibs/` | 2,130 |
  | All models · Maps & terrain · Textures & materials · Sounds | by type | 4,264 · 82 · 23,937 · 9,803 |

- **Each result names the game it came from**, in a second column: `police.mdl / Garry's
  Mod`. Both Garry's Mod and Half-Life 2 ship a `police.mdl`, and with several games added
  the filename alone does not say which one is about to be imported. The same origin shows
  in the picked-item line: `Model · 26.8 KB · Garry's Mod / models/player`.

- **The search box waits for a pause before refiltering.** Two mounted games is ~42,000
  pickable entries and up to 400 rebuilt rows, which was about 120 ms per keystroke and made
  typing feel like it was fighting back. The refresh is far cheaper now (see *Performance*),
  but rebuilding the whole list on every letter is still work nobody asked for.

### Changed

- **The roadmap image distinguishes shipped from planned.** It read as a flat capability
  diagram and said C++20 where the project is C++23. Now *Shipping now* is ticked in cyan
  and *Planned roadmap* is greyed with hourglasses, so the right-hand column cannot be
  mistaken for finished work.

- **The dock reads like a tool instead of a debug panel.** Everything ran together in one
  column with technical labels; it is now three headed sections following the order the
  work happens — *Your games*, *Find something*, *What you picked* — with consistent
  margins and dimmed secondary text.

  The wording rule throughout: say what a thing **is**, not what the file format calls it.

  | Before | After |
  |---|---|
  | Three rows — `fallbacks_dir`, `garrysmod_dir`, `garry's_mod` — for one game the user added | One row: **Garry's Mod — 25,990 files**. Mounts are grouped by the thing the user chose, however many archives it took internally |
  | `garrysmod_dir/models/player/police.mdl` | **police.mdl**, with a per-type icon; the full path moves to the tooltip |
  | "Models" / "Maps" / ".vtf" | "Characters & props" / "Maps & terrain" / "Texture" |
  | "Showing 400 of 25990 matches" | "Showing the first 400 of 18,090. Keep typing to narrow it down." |
  | "Pose" → "Automatic" | "Standing pose" → "Whatever the game uses by default" |
  | "VTF files can be browsed but not placed in a scene yet." | "Textures can be found here, but they are used by models and maps rather than placed on their own." |

- **Companion files are no longer listed.** A Source model is split across `police.mdl` +
  `police.vvd` + `police.dx90.vtx` + `police.ani`, and the importer resolves the
  companions itself. Searching "police" returned 36 rows where the user wanted one, and
  picking any of the extra three did nothing. `.vvd`, `.vtx`, `.ani` and `.phy` are hidden
  — 25,990 indexed files, 18,090 of them actually pickable.

- A restored session now says "Picking up where you left off." The greeting used to be
  written and then immediately overwritten by the search-result count.

- **`NeuralMaterialTranslator` is now `MaterialHeuristics`.** There is no machine learning
  in it and never was — it matches keywords in the material name. Breaking rename, which
  is what a major version is for.

- **The repository holds the project and nothing else.** Seven packaged releases (~11 MB)
  and the compiled extension were committed; between them they were 186 MB of a 95 MB
  history, since every rebuild stored a fresh 7 MB DLL. Both are purged from history and
  ignored going forward — CI builds the binary and publishes it as an artifact, and users
  install from Releases. `.git` went from **95 MB to 3.3 MB**. The seven release archives
  remain downloadable from their tags, which is where they always belonged.

  A fresh clone therefore contains no binary. Build before opening `demo/` in Godot;
  `CONTRIBUTING.md` says so.

- **The five test suites are CMake targets**, so `ctest -C Debug` runs all of them. Each
  was previously buildable only by copying a hand-written `cl` command line out of a
  comment at the top of the file — five different command lines, none verified by
  anything.

### Removed

- **Seven registered classes and ten parser stubs that did not do what their names said.**
  A registered class is a promise to whoever calls it, and these were promises the project
  could not keep:

  | Class | What it actually did |
  |---|---|
  | `UAssetMeshExtractor`, `BundleMeshExtractor` | Returned a mesh carrying a name and a material and **zero vertices**. Unreal and Unity "support" was that |
  | `VulkanRTBuilder` | Reported `is_built: true` for an acceleration structure Godot 4.3 has no API to build |
  | `P2PVFSStreamer` | Contained no networking whatsoever |
  | `BSPMapRenderer` | Not implemented; `UnifiedAssetImporter.load_mesh()` already did the job |
  | `VFSFileTree` | Returned one hardcoded row |
  | `DependencyGraphBuilder` | Returned an always-empty dependency list |
  | `ImportPresets` | Wrote three settings nothing read |

  The parser stubs — `uasset_parser`, `bundle_parser`, `vmdl_parser`, `xob_parser`,
  `paa_decoder`, `audio_decoder` and four others, 16 to 32 lines each — were never reached
  by the importer at all. They existed to make a format table look longer.

  Registered classes: 23 → 15.

### Fixed

- The README and the beginner's guide told users to press **Add game content**. The button
  has been called **Add a game** since the dock was restructured.

- **`P3DMLODParser` returned success for a model it had not read.** It checked the magic
  and handed back a `ParsedP3DModel` named `"BohemiaModel"` with zero surfaces and zero
  bones, so every caller saw a valid parse of an empty model and had no way to tell that
  nothing had been decoded. It also accepted ODOL, a different container entirely. It
  reports an error now, and the dock says Arma and DayZ models cannot be imported yet
  rather than letting the user press Add and get a failure they cannot act on.

  `docs/API.md` had listed this as "⚠️ MLOD only", which was wrong — the row was written
  from the parser's shape rather than from running it. Corrected, and the file now records
  P3D as the cautionary example.

- **`AsyncAssetImporter` spawned an unbounded thread per call.** Importing a folder of 200
  models from a loop created 200 threads at once, each holding a decoded copy of its
  asset. It honours `quebratsk/performance/max_background_threads` now — which until this
  release was a Project Setting nothing read.

### Performance

- **`VFSManager.find_files()`** searches the index in the engine and returns only the page
  that will be drawn, plus the full match count. The dock used to call `list_files()` — all
  60,584 URIs of a Half-Life 2 plus Garry's Mod install copied into GDScript, several
  megabytes allocated to build 400 rows — and then sift them with `get_extension()` and
  `to_lower()` per entry, per keystroke.

  Producing the same 102 results: **8.8 ms against 55.5 ms**. A whole list refresh, engine
  scan plus 400 `Tree` rows, went from ~50 ms to 11–25 ms depending on the category.
  `demo/tests/verify_dock.gd` times both ways in the same run, so the comparison stays
  honest as the code changes.

  The exclusion list is a parameter rather than the caller's job, because filtering after
  the limit both truncates the page and inflates the total: the caller only ever sees the
  part it was given. Doing it in GDScript is what made the dock read "showing the first 297
  of 60,481" when the truth was 400 of 41,969.

- **`list_poses()` no longer decodes the poses it is only naming.** It ran the full parse:
  for each of a player model's 359 sequences it seeded two 68-element bone arrays, decoded
  every track and converted the result into Godot's axes, and then the caller read the name
  and threw the rest away.

  It still has to establish which sequences are *readable* — a descriptor pointing at data
  the model does not ship is not a pose it can stand in, and dropping that check would put
  dead entries in the dock's dropdown. So the same walk runs, stopping at the first bone
  that proves the sequence decodes. Identical list, a fraction of the work: 65 ms → 26 ms
  in a debug build, and the cost of naming poses fell from half a full import to a fifth of
  one.

  In a release build the remaining ~7 ms is almost entirely one read: the 7.1 MB
  `m_anm.ani` that every Garry's Mod player model borrows its stances from. That one is not
  wasted work, it is a file that has to be read — but it is read again for the import that
  follows, and again on each pose change, which is where the next gain is.

- **Which game a result came from is a hash lookup.** `_game_of()` walked every source and
  its nested mount array on each call, once per row while drawing 400 of them. The map is
  rebuilt when the mounts change, which is the only thing that invalidates it.

- **"Have you added a game yet" no longer scans the index.** The empty-state check asked the
  VFS, so every keystroke walked all 60,584 entries to learn something the dock already
  knew about its own mounts.

---

## [0.7.0-alpha] - 2026-07-30

The plugin becomes usable without writing code. Previously every capability in this
project was reachable only from GDScript; the editor addon was a `Label` reading
"Quebratsk VFS Explorer Dock — Active".

### Added

- **A working editor dock** — `demo/addons/quebratsk_editor/`.

  Pick an installed game, search, choose a pose, press Add. No URIs to type, no extraction
  step, no conversion tools, and nothing to know about `.vvd` companions or animation
  sequences up front.

  | Step | What happens |
  |---|---|
  | **Add game content** | Lists Steam games found on this machine, plus *Browse for a folder* and *Open a single archive*. Choosing a game scans it, mounts every archive it holds, and indexes its loose files — Counter-Strike 1.6 keeps its maps as plain `.bsp` on disk, so archives alone are not enough |
  | **Search + type filter** | Capped at 400 rows with a "narrow the search" hint. `list_files()` over a Half-Life 2 plus Garry's Mod setup returns six figures, and building a `Tree` from that locks the editor |
  | **Selecting a `.mdl`** | Populates a pose dropdown from `list_poses()` — 342 entries for a Garry's Mod player model — so it can be placed in a real stance instead of a T-pose |
  | **Add to scene** | Goes through `EditorUndoRedoManager`, sets `owner` on every descendant so the branch survives a save, and selects the new node |

  Failures are explained rather than logged. A Source `.mdl` that decodes to nothing now
  says its geometry lives in a separate `.vvd` and `.vtx` and to add that archive too,
  instead of pushing an error to a console the user is not looking at.

  Mounts persist across restarts in `user://quebratsk_mounts.cfg` — `user://` rather than
  the project, because Steam paths are machine-specific — and a game uninstalled between
  sessions is skipped silently rather than erroring.

- `demo/verify_dock.gd` — drives the whole flow headlessly (detect, mount, search, pose,
  persist), because "it compiles" is not evidence that a UI works.

- `MountedContainer::is_directory`, surfaced as `is_directory` in `get_mounts_info()`. A
  caller restoring saved mounts needs it: a directory must be remounted with
  `mount_directory()` and an archive with `mount_container()`.

### Fixed

- **`mount_directory()` never registered a container.** It inserted index entries and
  nothing else, so every one of them kept `VFSEntry::container_index` at its default of
  `0` and was attributed to whatever archive happened to occupy slot 0 — mounting Garry's
  Mod showed `fallbacks_dir` with 10,666 files instead of its real 9,006, and the 1,660
  loose files had no mount of their own at all. They were therefore invisible to
  `get_mounts_info()`, could not be unmounted, and were silently dropped when the dock
  saved and restored its mounts. For Counter-Strike 1.6, whose maps are loose `.bsp`
  files, that meant the maps disappeared on every editor restart.

  Container occupancy was also being inferred from `mapped_file.is_valid()`, which assumed
  every mount is backed by an archive. A directory mount has no mapped file, so its slot
  read as free and the next `mount_container()` would have recycled it out from under a
  live mount. Occupancy is now an explicit flag.

### Removed

- `import_plugin.gd`. An `EditorImportPlugin` operates on files inside `res://`, and game
  archives are gigabytes living in a Steam folder — nobody copies `hl2_textures_dir.vpk`
  into their project. It was also never registered in `plugin.cfg`, so it had never run.

---

## [0.6.1-alpha] - 2026-07-30

### Added

The six APIs `docs/API.md` §6 asked for, so the editor addon can be built against a stable
surface. All are exercised by `demo/verify_api.gd` against a real Steam install.

- **`AsyncAssetImporter.load_model_async()`** — decodes on a worker thread and constructs
  the `Node3D` in a main-thread continuation. The VFS read stays on the main thread
  because the VFS is main-thread-owned; what moves off it is the decode, ~100 ms of the
  ~170 ms a Garry's Mod player model costs.
- **`UnifiedAssetImporter.list_poses()`** — every sequence label a model carries. Skips the
  `.vvd` and `.vtx` entirely via `read_asset_bundle(uri, with_geometry: false)`, since
  poses live in the `.mdl` and its `.ani`.
- **`UnifiedAssetImporter.build_model_node()`** — the main-thread half of `load_model()`,
  taking an already-parsed IR. Split out so the async path has something to construct
  from.
- **`VFSManager.get_mounts_info()`** — one row per mounted prefix:
  `{prefix, real_path, engine, file_count, archive_count}`.
- **`VFSManager.scan_game_directory()`** — `{total_archives, archives, loose_models,
  loose_maps, loose_textures}` for a setup wizard.
- **`UnifiedAssetImporter.get_last_error_code()`** with named constants bound into
  GDScript: `ERR_OK`, `ERR_VFS_NOT_SET`, `ERR_ASSET_UNREADABLE`, `ERR_PARSE_FAILED`.
- **`SteamLibraryDetector`** now also finds Half-Life 2, Episode One, Episode Two, Portal,
  Portal 2, Team Fortress 2 and Left 4 Dead 2.
- `demo/verify_api.gd` — a harness that exercises all six against retail assets and prints
  what each actually returns, rather than asserting they were written.

### Fixed

Defects in the first cut of the six APIs above, all caught by running them:

- **`load_model_async()` always delivered `null`.** The continuation called
  `load_model(parsed_ir.mesh.name, …)`, but `mesh.name` is the model's *internal* header
  name (`"player/lowpoly/usarmy_2000.mdl"`), not a `vfs://` URI, so the lookup always
  missed. It also discarded the IR the worker had just produced and re-parsed on the main
  thread — so the call was never actually asynchronous either.
- **`get_last_error_code()` reported `ERR_OK` after a failed `load_model()`.** Only
  `load_mesh()` and `list_poses()` set the code; the most important entry point left
  whatever the previous call had stored. A UI checking it would have been told a failed
  import succeeded. Every `load_*` path now sets it, on success as well as failure.
- **`list_poses()` read and decoded the full mesh** despite being documented as
  header-only — it went through the standard bundle read, pulling in the `.vvd` and
  `.vtx`. Now ~70 ms instead of ~100 ms against ~170 ms for a full import.
- **`get_mounts_info()` reported 7 mounts for 3.** A VPK's numbered side archives are
  separate internal containers under the same prefix, and each was listed as its own mount
  with a `file_count` of 0 or 1. Rows are now grouped by prefix, with `archive_count`
  carrying the number of real files. Counting is also a single pass over the index rather
  than one pass per mount — it was O(mounts × index).
- **`scan_game_directory()` could call `terminate()`.** It iterated with a range-`for`,
  whose `operator++` throws on an I/O error; godot-cpp is built with exceptions disabled,
  so an unreadable path partway through a scan would have taken the editor down. Now uses
  `increment(ec)` and reports partial results. It also counted VPK side archives as
  separately mountable, and its `total_models` keys implied it saw inside archives — they
  are now named `loose_*`, because a modern Source game keeps everything in VPKs and the
  honest answer for `GarrysMod/` is 1 loose model.
- **The release DLL was a copy of the debug build** — byte-identical, 7.4 MB instead of
  768 KB. This is the same defect fixed in `v0.3.0-alpha`, regressed. Rebuilt from
  `build-release/`.
- **`quebratsk-engine-v0.2.0-alpha-windows.zip` was overwritten** in the repository
  (1,523,555 → 11,149,851 bytes). Restored from `29b1369`; verified byte-identical to the
  asset still published on the v0.2.0-alpha release, which was never touched.

---

## [0.6.0-alpha] - 2026-07-30

Reads Valve's VPK archives, and finishes the animation work `v0.5.0-alpha` started: a
Garry's Mod player model now stands in the same stance Half-Life 2 shows it in, sourced
from the shared animation model it declares and the multi-megabyte `.ani` that model
defers its sequences to.

Verified end to end in Godot 4.7.1 against retail Half-Life 2, Garry's Mod and
Counter-Strike 1.6 installs — see *Verification*.

### Added

- **Source VPK archives (v1 and v2)** — `src/parsers/source2/vpk2_parser.{h,cpp}`,
  `structs/vpk2_structs.h`, wired into `VFSManager`.

  This was the largest remaining content gap: Half-Life 2, Team Fortress 2, Counter-Strike
  Source and Garry's Mod ship essentially everything inside VPKs, so without a reader the
  engine could only see loose files that modern Source games no longer install.

  The directory is a three-level tree of NUL-terminated runs — extension, then path, then
  filename — and every entry is followed by a `0xFFFF` terminator that the parser now
  verifies rather than assumes. Mounting a `_dir.vpk` pulls in its numbered side archives
  (`<base>_003.vpk`) automatically; entries flagged `0x7FFF` are read inline from the
  directory itself.

- **External animation blocks (`.ani`)** — `SourceAnimBlock`, `SourceAnimSection`, and
  `SourceMDLParser::anim_block_name()`.

  `studiomdl` moves long sequences out of the `.mdl` entirely. Half-Life 2's shared male
  animation model is 0.9 MB of `m_anm.mdl` against **7.1 MB of `m_anm.ani`**, so a parser
  that only reads the `.mdl` finds almost nothing: every blocked descriptor was skipped
  and the model fell back to a single `ragdoll` frame.

  Resolving frame 0 takes two hops, and both are now implemented. A long animation is cut
  into sections, so the descriptor's own `anim_block`/`anim_index` are decoys and section
  0 must be consulted instead; whichever block that yields, a non-zero one means the bytes
  live in the companion at `data_start + anim_index`. Neither mistake crashes — they just
  silently yield zero poses.

  **341 stances recovered per model**, up from 1.

- **Pose selection** — `UnifiedAssetImporter.load_model(vfs_uri, pose_name = "")`.

  Picks a sequence by exact label first and then as a substring, so `"idle_smg1"` gets the
  standing SMG stance rather than `cidle_smg1`, its crouched namesake. The full catalogue
  of labels is published on the returned node as the `quebratsk_poses` metadata, so a user
  can see what a model can do without opening it in a separate viewer.

- `IRSkeletonData::find_pose_exact()`, alongside the existing substring `find_pose()`.

- **Source animation sequences are decoded, and models no longer import in a T-pose.**
  `src/parsers/source1/anim_decoder.{h,cpp}`, `structs/anim_structs.h`,
  `IRPose`, `SkeletonConverter::apply_pose()`.

  A bind pose is a modelling artefact the game never displays. The real stances live in
  the animation sequences, where rotations arrive either quantised (Quaternion48 packs
  16/16/15 bits plus a sign; Quaternion64 packs 21/21/21) or as three run-length encoded
  Euler tracks scaled by the bone's `rot_scale`. A track cannot be indexed by frame
  number: each run is a `{valid, total}` header where `valid` frames are stored and the
  remainder repeat the last sample.

- **`includemodel` support.** A Garry's Mod player model carries only a `ragdoll` sequence
  of its own and borrows every real stance from a shared model named in its
  `mstudiomodelgroup_t` table. Those bone tables are ordered independently, so poses are
  remapped **by bone name** — matching by index scrambles the skeleton.

- `tests/source_mdl_test.cpp` — external animation blocks: name lookup, a blocked sequence
  skipped when the companion is absent and recovered when it is present, the section-0
  redirect taking precedence over the descriptor, an out-of-range block refused, and the
  exact-versus-substring distinction.

- `tests/anim_decoder_test.cpp` — 40 assertions over the quantised formats and RLE
  sampling, including the unit-constraint clamp before `sqrt` when recovering `w`.

- `tests/byte_reader_test.cpp` — 54 assertions over the reader's invariant, including
  `size_t` overflow in both the offset and the count, unterminated strings, cursor
  position after a *failed* read, and construction past the end of the buffer.

### Changed

- **All binary parsers now read through a single bounds-checked primitive**,
  `io::ByteReader` (`src/core/io/byte_reader.h`).

  Every parser had been hand-rolling the same "check, then memcpy, then advance"
  sequence, and it had drifted into **four subtly different implementations** of the same
  bounds test — three copies of `fits(offset, count, elem_size, total)` plus a separate
  `range_fits(offset, length, size)`. Four of the twelve critical findings in the original
  engineering audit (C3, C5, C6, C8) were unchecked reads or overflowing offset arithmetic
  in exactly that pattern.

  `ByteReader` holds one invariant, and it is total: **no operation can leave the cursor
  past the end of the buffer, and no read can return data that is not fully inside it.**
  There is no partial-read path. Offset arithmetic is done with subtraction and division
  so it cannot wrap, which is the failure the audit kept finding — `offset + count * size`
  overflows for values taken from an untrusted file and then reports success.

  Migrated: `bsp30_parser`, `mdl10_parser`, `mdl_source_parser`, `vvd_parser`,
  `vfs_manager`. The local `fits()` / `range_fits()` copies are gone. Behaviour-preserving:
  retail assets (`cs_assault`, `de_dust2`, `arcticorange`, HL2 `envballs`) produce
  **byte-identical output**.

### Fixed

- **Rebuilds silently did nothing.** `CMakeLists.txt` set `RUNTIME_OUTPUT_DIRECTORY` to
  `demo/bin`, but Visual Studio is a multi-config generator and appends the configuration
  name to it. The DLL landed in `demo/bin/Debug/` while `quebratsk.gdextension` points at
  `demo/bin/`, so Godot kept loading whatever stale binary was left at the top level.

  Every C++ change made in a session could compile cleanly, link, report success, and
  never reach the engine. Wrapping the path in a generator expression suppresses the
  per-config subdirectory.

- `VFSManager::index_vpk()` copied the mount prefix and byte span it needed **before**
  mounting side archives, because placing a container can reallocate `m_containers` and
  invalidate any reference held into it.

### Verification

| Check | Result |
|---|---|
| `tests/source_mdl_test.cpp` (+5 assertions) | pass |
| Half-Life 2 + Garry's Mod VPKs mounted | 14 directories, ~100k files across 70 side archives |
| `models/m_anm.mdl` + `models/m_anm.ani` resolved | 878,964 B + 7,122,528 B |
| Poses per Workshop player model | **341** (was 1) |
| `cs_assault` + 7 Workshop soldiers in Godot 4.7.1 | 149/149 surfaces textured, 68 bones, skin bound, each soldier in a distinct weapon stance |

---

## [0.5.0-alpha] - 2026-07-30

Closes the two gaps `v0.4.0-alpha` left open: skinned models now bind to a real
`Skeleton3D`, and Source 1 models import with geometry.

**First release verified by running it.** Everything below was checked against retail
Counter-Strike 1.6 and Half-Life 2 assets inside Godot 4.7.1, not only by unit tests.
That surfaced two defects nothing else would have caught — see *Fixed*.

### Added

- **`VFSManager::mount_directory()`** — index a directory tree of loose files. The VFS
  could only read container archives, which excluded the most common case by far:
  extracted asset folders. Files are indexed by relative path and read on demand, so a
  large tree costs no memory mappings or OS handles.
- **GoldSrc BSP embedded textures.** Compiled GoldSrc maps normally store their textures
  inside lump 2 rather than referencing an external WAD. The parser read names and
  dimensions from there but never decoded the pixels, so a map imported almost entirely
  untextured even with every WAD mounted. Embedded miptex data is now decoded through
  the existing WAD3 decoder. On `de_dust2` this took textured surfaces from 21/36 to
  34/36 — the remainder are sky brushes, which have no renderable texture.
- **GoldSrc `<name>T.mdl` companion textures.** Much of the stock Counter-Strike 1.6
  content declares zero textures in the model and keeps them in a separate file. Those
  models previously imported with placeholder material names, no texture, and UVs
  normalized against a fallback 64x64 instead of the real dimensions.
- `demo/verify.tscn` + `verify.gd`, a scene that loads real assets through the importer
  and reports what it got, for visual verification.

### Verified against retail assets

Measured in Godot 4.7.1, not asserted:

| Asset | Result |
|---|---|
| `cs_assault.bsp` (CS 1.6) | 149 surfaces, **149 textured**, 8237 tris, correct metric scale |
| `de_dust2.bsp` (CS 1.6) | 34/36 textured; the 2 remaining are sky brushes, which have none |
| `player/urban.mdl` (CS 1.6) | **53 bones**, skinned, textured, correct rest pose |
| `arcticorange.mdl` (CS 1.6) | 7 bones, 3 materials from its `T.mdl` companion |
| `envballs.mdl/.vvd/.vtx` (HL2) | 950 vertices, 978 tris, 6 materials, 1:1 vertex mapping |
| `characters_belts.pbo` (DayZ) | 121 entries indexed |

Standing inside a loaded map shows solid walls rather than see-through ones, which
confirms the winding fix from the first audit pass — that had been argued from the
transformation's determinant but never actually seen.

- **Skinning bound to `Skeleton3D`.** The IR carried bone indices and weights but
  nothing consumed them, so models imported as static geometry. Now complete:
  - `MeshConverter` emits `ARRAY_BONES` / `ARRAY_WEIGHTS` (Godot requires both together
    at 4 entries per vertex; emitting one alone makes the surface fail).
  - `SkeletonConverter::make_skin()` builds the `Skin`, binding each bone with the
    inverse of its global rest transform. Mesh vertices are emitted in model space with
    the rest baked in, so without this the skinned mesh collapses to the origin at
    runtime — a failure invisible until the model is in a scene.
  - `ir::compute_global_rest_transforms()` composes model-space rests from the
    parent-relative locals. Pure IR math, no Godot Object, safe on any thread.
  - `UnifiedAssetImporter::load_model()` returns a `Skeleton3D` with a skinned
    `MeshInstance3D` child, or a bare `MeshInstance3D` when the asset has no bones.
    Exposed to GDScript.
- **Source 1 mesh extraction from `.mdl` + `.vvd` + `.dx90.vtx`.** A Source `.mdl`
  contains no vertex data at all, so all three files are read together:
  - `vvd_structs.h` / `vtx_structs.h` with every struct size pinned by `static_assert`.
  - `VVDParser::load_vertices()` applies the **fixup table**. When fixups are present
    the on-disk vertex array is not in mesh order and must be rebuilt by copying the
    runs it describes; reading it linearly yields a scrambled mesh.
  - `SourceMDLParser::parse_bundle()` walks body part → model → LOD 0 → mesh → strip
    group → strips, resolving the three-hop index chain (strip index → strip-group
    `Vertex_t` → `orig_mesh_vert_id` + mesh vertex offset + model vertex base) and
    handling both triangle lists and strips. Bone weights (up to 3 influences) carry
    through to the IR.
  - Material names resolved from the `.mdl` texture table.
  - Checksums are cross-checked across all three files, so a `.mdl` paired with someone
    else's companions is rejected rather than decoded into nonsense.
- **`AssetBundleBytes` + `UnifiedAssetImporter::read_asset_bundle()`.** Companion files
  are resolved and read on the main thread, then handed to the parser as plain buffers.
  This keeps `parse_asset_ir()` pure and off-thread, preserving the threading guarantee
  the async path depends on. Tries the sibling next to the `.mdl` first, then falls back
  to a VFS suffix search; `.dx90.vtx`, `.vtx`, `.dx80.vtx` and `.sw.vtx` are all accepted.
- **`tests/source_mdl_test.cpp`**: 26 assertions covering VVD with and without fixups,
  rejection of bad magic / version / overrunning fixup runs, the full three-file bundle
  with its relative-offset traversal, checksum mismatch, and the skeleton-only fallback.
- Skin bind-pose assertions added to `tests/mdl10_parser_test.cpp` (now 36).

### Fixed

- **[CRITICAL] The extension could not load at all.** `quebratsk.gdextension` used `#`
  for comments. Godot parses that file with `ConfigFile`, where comments start with `;`,
  so the `#` lines were a parse error that dropped `compatibility_minimum` and made the
  whole library fail to load: *"GDExtension configuration file must contain a
  configuration/compatibility_minimum key"*. This was introduced in `v0.3.0-alpha` while
  correcting that very field, which means **`v0.3.0-alpha` and `v0.4.0-alpha` shipped a
  manifest that cannot load**. No test caught it; only running the extension in Godot did.
- **[HIGH] Real Virtuality PBO archives indexed zero files.** The "Vers" entry is
  followed by NUL-terminated key/value property strings terminated by an empty string,
  not by a `data_size`-sized blob. Skipping by `data_size` (which is 0) left the cursor
  on the first property key, so `product` was read as a filename and the index collapsed
  immediately. Every DayZ and Arma PBO begins with one of these blocks, so none of them
  worked. A DayZ `characters_belts.pbo` now indexes 121 entries (29 `.p3d`, 44 `.paa`).
- **[CRITICAL] `SourceStudioHeader` was missing three fields**, so `num_bodyparts` and
  `bodypart_index` were reading `num_skin_ref` and `num_skin_families`. Any body-part
  traversal would have walked garbage offsets. This was reported as M3 in the original
  audit and had not been applied; it blocked the Source mesh work entirely. Added
  `num_skin_ref`, `num_skin_families` and `skin_index`, plus `offsetof` assertions
  pinning the six header fields the parser indexes by against the Source SDK layout.
- **`SkeletonConverter` could make a bone its own parent.** The guard compared
  `parent_index` against `get_bone_count()`, which already counts the bone just added.
  Now compared against the bone's own index.

### Changed

- `parse_mesh_ir()` became `parse_asset_ir()` and returns the skeleton alongside the
  mesh. It previously discarded the skeleton, which is why nothing downstream could
  build a `Skin`.
- `SourceMesh`, `SourceModel`, `SourceBodyPart` and `SourceTexture` structs added, with
  sizes pinned (148, 116, 16 and 64 bytes respectively).

---

## [0.4.0-alpha] - 2026-07-30

GoldSrc `.mdl` models now import with geometry. Previously `MDL10Parser` extracted only
the skeleton, so every Half-Life / Counter-Strike 1.6 model imported as an empty mesh.

### Added

- **GoldSrc StudioMDL v10 mesh extraction (`mdl10_parser.cpp`)**. The full path is now
  implemented: body parts to models to meshes to the triangle command stream.
  - **Bone-space resolution.** GoldSrc stores every vertex in the local space of the bone
    it is attached to, so the bone hierarchy has to be composed (`parent.world * local`)
    and each vertex transformed by its owning bone's rest transform. Without this the
    mesh decodes as a cloud of fragments scattered around the origin. Normals are rotated
    by the basis only, never translated.
  - **Triangle command stream decoding.** An `int16` count followed by that many 4×`int16`
    corners; negative means fan, positive means strip, zero terminates. Strips alternate
    orientation, so odd-numbered triangles swap their first two corners to keep winding
    consistent with the `GL_TRIANGLE_STRIP` convention the original renderer used.
  - **UV normalization** against the real texture dimensions from the model's texture
    table, resolved through the skin table (`skin_ref` to skin family to texture index).
  - **Embedded texture decoding.** GoldSrc models carry their own 8-bit palettized
    textures; these are decoded to RGBA8 and surfaced through the new
    `IRMeshData::embedded_textures` / `IRSurface::embedded_texture_index`, so models
    import fully textured with no VFS lookup at all. `STUDIO_NF_MASKED` (flag `0x40`)
    maps palette index 255 to transparent.
  - Corner deduplication keyed on (vertex, normal, s, t), and rigid one-bone-per-vertex
    skinning weights recorded in the IR.
- **`StudioMesh` and `StudioTexture` structs**, plus `static_assert`s pinning every
  StudioMDL struct size and — because `StudioHeader` is deliberately truncated and
  `sizeof` therefore proves nothing — `offsetof` assertions on the eight header fields
  the parser actually indexes by.
- **`tests/mdl10_parser_test.cpp`**: builds a synthetic `.mdl` in memory and verifies
  header offsets, bone hierarchy composition, bone-local to model-space transformation,
  strip expansion, UV normalization and embedded texture decoding. 30 assertions, all
  passing. Build instructions in the file header.
- `MeshConverter` prefers an embedded texture over a VFS lookup when the surface has one.

### Fixed

- **[HIGH] GoldSrc models were routed to the Source parser and silently discarded.**
  `SourceMDLParser` validated only the `"IDST"` magic — which GoldSrc StudioMDL uses too —
  so a GoldSrc `.mdl` was accepted there, returned an empty mesh, and
  `UnifiedAssetImporter` never fell through to `MDL10Parser`. The Source parser now
  requires version 44–49, and the importer routes GoldSrc first.

### Changed

- **`SourceMDLParser` scope documented.** Unlike GoldSrc, a Source `.mdl` contains no
  vertex data whatsoever: positions, UVs and weights live in a companion `.vvd` and the
  index/strip data in a `.dx90.vtx`. Producing Source mesh output requires reading three
  files together, which this parser does not do — `mesh_data` is always empty, and the
  header now says so rather than leaving callers to discover it.

---

## [0.3.0-alpha] - 2026-07-30

First release in which imported GoldSrc maps are actually textured. Supersedes
`v0.2.0-alpha`, which remains published for reference.

### Added

- **BSP30 UV generation (`bsp30_parser.cpp`)**: face UVs are now projected from the
  `BSPTexInfo` S/T axes that were sitting unused right next to the face loop:
  `u = (dot(vertex, vector_s.xyz) + vector_s.w) / texture_width`, likewise for `v`.
  The projection vectors are expressed in the map's original Hammer-unit space, so the
  raw vertex is used rather than the Godot-remapped one. Every vertex previously
  received a hardcoded `(0, 0)`.
- **BSP30 per-face normals**: taken from the face's plane and negated when `plane_side`
  is set, then remapped to Godot space. Every vertex previously received a hardcoded
  `(0, 1, 0)`, which made all map lighting flat.
- **BSP30 texture directory (lump 2)**: parsed for real texture names and dimensions.
  Names are required to normalize UVs and to let `TextureLoader` resolve the surface
  against mounted WAD archives; `IRSurface::material_name` used to be a synthetic
  `"texture_<n>"` that could never match anything in the VFS. Entries with a `-1` offset
  (texture stored in an external WAD) are handled.
- **`MeshConverter` attaches materials**: each surface's `material_name` is resolved
  through the VFS and bound as a `StandardMaterial3D` albedo texture. Legacy content is
  given `TEXTURE_FILTER_NEAREST_WITH_MIPMAPS` (bilinear turns 64x64 GoldSrc textures to
  mush) and GoldSrc `{`-prefixed names get alpha-scissor transparency. Without this the
  new UVs had nothing to sample.
- **`tests/dxt_decoder_test.cpp`**: standalone verification of the DXT decoders, with no
  engine, no godot-cpp and no test framework. Covers both DXT1 colour modes (including
  punch-through transparency), DXT5 interpolated alpha, blocks clipped by
  non-multiple-of-4 dimensions, and rejection of truncated input. 17 assertions, all
  passing. Build instructions are in the file header.
- `static_assert`s pinning `BSPPlane`, `BSPEdge` and `BSPTexInfo` sizes, which the struct
  header declared but never verified.

### Fixed

- **`AsyncAssetImporter` produced untextured meshes** where the synchronous path produced
  textured ones. The pending job now carries the importer's `ObjectID` (not a raw
  pointer, which could dangle if the node were freed mid-decode); it is re-resolved on
  the main thread to build the texture loader.
- Duplicate load of BSP lump 1: the planes lump was read twice, once for collision and
  now once for normals. Read once and reused.

---

## Audit Follow-Up — Texture Pipeline, Correctness & Cleanup — 2026-07-30

Second pass over the findings the first audit pass left open. The headline item was not
on the original list: while wiring the remaining fixes it turned out that **no texture
pipeline existed at all**, so every asset imported untextured regardless of how well the
decoders worked.

### Added

- **Texture pipeline (`texture_converter`, `texture_loader`, `ir_texture_data`, `dxt_decoder`)**:
  the missing bridge from decoded pixels to Godot materials. Previously the only code in
  the entire project that constructed an `ImageTexture` was `TextureUpscalerPipeline`,
  which takes an already-loaded `Image` from GDScript; `VMTParser` wrote
  `IRMaterialData::albedo_texture` that nothing ever read; `MaterialConverter` never
  called `set_texture()`; and `VTFParser` / `WAD3Parser` / `PAADecoder` produced RGBA8
  buffers with no consumer. New pieces:
  - `ir::IRTextureData` — one decoded-image type all texture decoders converge on, free
    of Godot types so decoding stays thread-safe.
  - `image::decode_dxt1` / `decode_dxt5` — real BC1/BC3 block decoders (correct RGB565
    endpoint expansion, DXT1 punch-through alpha, DXT5 interpolated alpha tables), sized
    in `size_t` throughout.
  - `converters::TextureConverter` — `IRTextureData` to `ImageTexture`.
  - `converters::TextureLoader` — resolves extension-less legacy references
    ("metal/metalwall001a", WAD3 lump names) against the VFS by suffix, decodes, and
    caches. Failed lookups are cached too, so a missing texture is searched for once.
  - `VFSManager::find_by_suffix()` and `VFSManager::read_owned()`, the latter now the
    single place that knows how to read both mapped and compressed entries.
  - `UnifiedAssetImporter::load_texture()`, exposed to GDScript.
- **`MaterialConverter::convert()` binds textures**: albedo, normal (enabling
  `FEATURE_NORMAL_MAPPING`) and emission, plus additive blend support.
- **`WindingVisualizer::flip_normals_and_winding()` now actually flips**: reverses index
  winding, negates normals and inverts tangent handedness across every surface. It
  previously only cleared the material override despite its name and its ClassDB binding.

### Fixed

- **[HIGH] `VTFParser` decoded every compressed texture into noise**: the parser used
  `header_size` as the pixel-data offset — which points at the low-resolution thumbnail —
  and then `memcpy`'d `width * height * 4` raw bytes regardless of the declared format.
  Since DXT1/DXT5 covers the overwhelming majority of Source assets, every such texture
  imported as garbage. Rewritten with real format dispatch (RGBA8888, BGRA8888, BGRX8888,
  RGB888, BGR888, I8, IA88, A8, DXT1, DXT5), correct highest-mip location (VTF stores mips
  smallest-first, so level 0 is at the end of the file), and an explicit
  `UnsupportedFormat` error instead of a silent wrong answer.
- **[HIGH] Euler order mismatch on GoldSrc bones (`mdl10_parser.cpp`)**: StudioMDL stores
  XYZ Euler angles, but `Quaternion(Vector3)` in Godot applies YXZ. Poses were wrong as
  soon as more than one axis was non-zero. The rotation is now composed explicitly in the
  source frame before the basis change.
- **[HIGH] `SkeletalRetargeter` could not satisfy `SkeletonProfileHumanoid`**:
  `LeftShoulder` / `RightShoulder` are required by the profile and were absent from both
  the bone-name list and the mapping table. Added, along with `UpperChest` and toes.
- **[HIGH] Bohemia leg mapping was inverted**: the Arma/DayZ rig uses the Max Biped
  convention where `<side>UpLeg` is the thigh and `<side>Leg` is the calf. The table
  mapped `leftleg` to the *upper* leg and referenced a `leftlegup` bone that does not
  exist, so both leg joints were wrong.
- **[MEDIUM] `FuzzyMaterialFixer` returned near-random matches**: Levenshtein distance was
  computed against the full VFS URI, so the shared path prefix dominated the score. It now
  compares lowercased basenames, prunes candidates by length difference before paying for
  the O(n·m) table, and applies a maximum-distance threshold — previously it always
  returned *something*, however unrelated. Also removed undefined behaviour from passing
  raw `char` to `std::tolower`.
- **[MEDIUM] `VMTParser` mis-parsed keys and values**: `contains("$basetexture")` also
  matched `$basetexturetransform` and `$basetexture2`, storing transform arguments as a
  texture path; boolean tests used `contains("1")`, which matched a "1" anywhere on the
  line. Replaced with identifier-boundary key matching and proper value extraction. The
  parser now also tracks brace depth (so `Proxies{}` contents are not read as material
  parameters), strips `//` comments, captures the shader name into
  `IRMaterialData::shader_name` (never populated before), and handles `$normalmap`,
  `$envmapmask`, `$selfillummask`, `$detail`, `$alphatest` and `$additive`.
- **[MEDIUM] `NeuralMaterialTranslator` made most props chrome**: `VertexLitGeneric` — the
  *default* Source model shader, used by wood, cloth, skin and plastic — was classified as
  metal with `metallic = 0.85`. Removed that rule, added case normalization (so "Metal"
  matches at all) and more material families.
- **[MEDIUM] `TextureUpscalerPipeline` accepted any scale factor**: zero or negative
  produced an invalid resize and large values overflowed `int` or exhausted memory. Now
  bounded to [1, 8] with a 16384 px output ceiling, and compressed images are decompressed
  first since `resize()` fails on them.
- **[MEDIUM] `FileWatcher` could abort the process**: it used the throwing
  `std::filesystem` overloads, which call `terminate()` under `_HAS_EXCEPTIONS=0`, and a
  watched file can legitimately disappear between `exists()` and `last_write_time()`. Now
  uses `error_code` throughout, holds a mutex (it had none), stores timestamps in the
  filesystem's own clock instead of hand-rolling a clock conversion, and returns the list
  of changed paths rather than taking a `VFSManager*` it never used.
- **[MEDIUM] LZSS masked corruption**: a truncated or corrupt entry was zero-padded up to
  the expected size and reported as success, so a partially blank asset was
  indistinguishable from a valid one. Short output is now a `CorruptedData` error, and
  `VFSManager::read_file()` logs the failure.
- **[MEDIUM] `SteamLibraryDetector` used a throwing `exists()`** on paths taken from
  `libraryfolders.vdf`. Switched to the `error_code` overload; also dropped an unused
  `<iostream>` include.
- **[LOW] `TaskProgressTracker` published inconsistent state**: the atomic percentage was
  stored outside the mutex guarding the task name, so a reader could see the new value
  paired with the previous label.
- **[LOW] `WAD3Parser` accepted invalid dimensions**: the mip-chain size arithmetic assumes
  both dimensions are multiples of 16 (which GoldSrc guarantees) but never checked it.
- **[LOW] `CollisionConverter` rebuilt its point array per vertex**: replaced `append()` in
  a loop with a single `resize()` + `memcpy`, and documented that it merges all hulls into
  one shape — correct only because `VHACDDecomposer` calls it once per hull.

### Changed

- **Mocks now report failure instead of success.** These are registered in `ClassDB` and
  callable from GDScript, so returning `true` from a function that does nothing actively
  misled callers. `BSPMapRenderer::load_map()` (which printed "Successfully mounted and
  parsed map faces & PVS leaves" without parsing anything), `BSPMapRenderer::perform_pvs_culling()`,
  `VulkanRTBuilder::register_tlas_instance()` (Godot 4.3 exposes no ray-tracing API at
  all), `P2PVFSStreamer::start_streaming()` (no networking exists in that class) and
  `ShaderPrecacher::precache_all_materials()` (built and immediately destroyed one dummy
  material per path, precaching nothing) now push an error and return failure.
- **`PAADecoder` no longer fabricates output**: it returned a hardcoded 512x512 white image
  regardless of input, so every Arma/DayZ texture imported as a blank square that looked
  like a successful decode. It now returns `UnsupportedFormat` until a real decoder exists.
- **`VHACDDecomposer` documented honestly**: it merges all vertices into a single convex
  hull, which is the opposite of an approximate convex decomposition, and ignores its
  `params`.
- **`SkeletalRetargeter::retarget_to_humanoid()` scope documented**: it performs renaming
  only. Rest-pose alignment and `IRBone::pose_to_bone` (declared but never written) are
  still missing; renaming is a prerequisite for retargeting, not a substitute.
- **`WAD3Parser::parse_miptex` returns `ir::IRTextureData`** instead of the identical but
  incompatible `DecodedMiptex`, so decoded textures flow into the shared pipeline.

### Removed

- **`core/math/simd_math.{h,cpp}`**: its AVX2 branches contained only comments and always
  fell through to scalar code, it duplicated `axis_remap.h` while silently omitting the
  Hammer-units scale, and it had zero call sites. Keeping it invited using the wrong one
  of two conflicting implementations.
- **`core/math/winding_order.h`**: fully unreachable after the winding-inversion fix, and
  its presence invited reintroducing that bug.

### Performance

- `SkeletalRetargeter::map_bone_name()` is a single hash lookup against a pre-lowercased
  table; it previously lowercased every key in the map on every miss.
- `TextureLoader` caches decoded textures (and failed lookups) in `TextureCache`, so a
  texture shared by many materials is decoded once.

---

## Engineering Audit Pass — 2026-07-30

A full file-by-file audit of all 180 sources in `src/` (~7,000 lines). Every item below
was verified against the code, not inferred. Twelve crash- or memory-safety defects were
reachable from ordinary user input; several were reachable from a malicious archive.

### Security

- **[CRITICAL] Integer overflow in `WRPParser::parse` (`wrp_parser.cpp`)**: `grid_width * grid_height * sizeof(float)` wrapped around `size_t` for crafted headers (e.g. `0x7FFFFFFF` x `0x7FFFFFFF`), causing the bounds check to pass and `heightmap.assign()` to read gigabytes past the end of the mapping. Grid dimensions are now validated against a hard ceiling and the bounds check uses division instead of multiplication.
- **[CRITICAL] Undefined behaviour in `BSP30Parser::get_lump` (`bsp30_parser.cpp`)**: Lump offsets and lengths are `int32_t` on disk; negative values cast to huge `size_t` values and `ofs + len` wrapped, so `std::span::subspan()` was invoked with `offset > size()` — undefined behaviour, not a throw. Negative values are now rejected and the range check uses subtraction.
- **[CRITICAL] Out-of-bounds read via `texinfo_index` (`bsp30_parser.cpp`)**: `face.texinfo_index` was checked for `>= 0` but never against the size of lump 6, allowing a 40-byte read far outside the lump (and a null dereference when lump 6 was absent). The index is now bounded by the computed element count.
- **[CRITICAL] Unbounded string scan past the memory mapping (`vfs_manager.cpp`)**: `index_gma()` constructed `std::string` from a raw pointer after a scan that could terminate at end-of-buffer without finding a NUL, walking off the end of the mapped file. Replaced with a shared `read_cstr_bounded()` helper that fails on a missing terminator.
- **[CRITICAL] Unbounded bone-name scan (`mdl_source_parser.cpp`)**: The guard validated only the start offset of the name string; `std::string(const char*)` then ran to the first NUL, potentially past the end of the file. Now uses `strnlen()` with an explicit remaining-bytes limit.
- **[CRITICAL] Integer overflow in VFS entry bounds checks (`vfs_manager.cpp`)**: `entry.offset + entry.disk_size > size()` wrapped for archives declaring huge file sizes, letting `get_raw_span()` and `read_file()` build out-of-range spans. Added an overflow-safe `range_fits()` helper, applied at index time in `index_wad3`/`index_gma`/`index_pbo` and again at read time.
- **[CRITICAL] Stack buffer over-read in `SteamLibraryDetector` (`steam_library_detector.cpp`)**: `RegQueryValueExA` does not guarantee NUL termination for `REG_SZ`; a value exactly filling `MAX_PATH` caused `std::string(path)` to read past the array. Switched to `RegGetValueA` with `RRF_RT_REG_SZ`, which guarantees termination and validates the value type.
- **[CRITICAL] Attacker-controlled allocation in `EnfusionPakParser` (`enfusion_pak_parser.cpp`)**: `entries.reserve(header->entry_count)` accepted an unvalidated 32-bit count from the file. With exceptions disabled (godot-cpp sets `_HAS_EXCEPTIONS=0`) the resulting `length_error` became `terminate()`. The count is now bounded by what the file can physically contain.
- **[CRITICAL] Integer overflow on sprite frame dimensions (`spr_parser.cpp`)**: Negative `int32_t` width/height cast to huge `size_t` values, wrapping the pixel count and driving both the `resize()` and the decode loop out of bounds. Dimensions are now validated and the range check uses subtraction.
- **[HIGH] Signed overflow on `INT32_MIN` surfedge (`bsp30_parser.cpp`)**: Negating `INT32_MIN` is undefined behaviour. The magnitude is now taken in unsigned arithmetic.
- **[HIGH] Off-by-one in clipnode plane bounds (`bsp30_parser.cpp`)**: The check compared the plane's *start* offset against the lump size, permitting a read that began inside the lump and ran up to 19 bytes past its end. Now compares the index against the element count.

### Fixed

- **[CRITICAL] Guaranteed process abort in `UnifiedAssetImporter::load_mesh` (`unified_asset_importer.cpp`)**: `get_raw_span()` returns `nullopt` for every compressed entry; the fallback read into a `PackedByteArray` that was immediately discarded, then `raw_span.value()` was called on the empty optional. Because godot-cpp compiles with `_HAS_EXCEPTIONS=0`, `std::bad_optional_access` became `std::terminate()` — any asset inside a compressed PBO killed the editor. Rewrote the read path around a new `read_asset_bytes()` that returns an owned buffer for both the mapped and the decompressed case. The same latent bug is fixed in `load_material()` and `load_terrain()`.
- **[CRITICAL] Godot `Resource` allocation on a detached worker thread (`async_asset_importer.cpp`)**: `load_mesh_async()` ran `importer->load_mesh()` inside `std::thread(...).detach()`, which called `ArrayMesh::instantiate()` and `add_surface_from_arrays()` off the main thread, captured the importer as a raw pointer (use-after-free if the node was freed mid-flight), and left a detached thread running across library unload. Rewritten into a three-phase pipeline: VFS read on the main thread, pure-IR decode on a tracked `std::jthread`, and `ArrayMesh` construction on the main thread via `call_deferred`. Workers are now joined in the destructor.
- **[CRITICAL] Crash on editor shutdown (`register_types.cpp`, `texture_cache.cpp`)**: `TextureCache`'s function-local static holds `Ref<StandardMaterial3D>` values and is destroyed at library unload — after Godot has torn down `ClassDB` and the `RenderingServer`. `uninitialize_quebratsk_module()` now clears the cache while the servers are still alive, resolving the long-standing `TODO B1`.
- **[CRITICAL] Inverted geometry on every imported map (`bsp30_parser.cpp`, `axis_remap.h`)**: The parser called `invert_winding_order()` after `source_to_godot()`. That basis change is `M = [1 0 0; 0 0 1; 0 -1 0]` with `det(M) = +1`, so it *preserves* orientation and the GoldSrc winding was already correct. The extra inversion flipped every triangle and made maps render inside-out under backface culling. Removed the call and documented the determinant invariant on `source_to_godot()`.
- **[HIGH] `unmount()` never released the mapping (`vfs_manager.cpp`)**: Only index entries were erased. The `MemoryMappedFile` stayed alive, keeping the OS file handle open (locking the archive on Windows) and leaking the address-space reservation, so repeated mount/unmount cycles grew without bound. `unmount()` now closes the mapping and marks the container slot reusable, and `mount_container()` recycles free slots. Also fixed a prefix-normalization mismatch that made `unmount("vfs://name")` silently match nothing.
- **[HIGH] Deadlock risk and off-thread scene access in `BatchingManager::flush` (`batching_manager.cpp`)**: `memnew()`, `add_child()` and `set_owner()` ran while holding `_mutex`; any callback re-entering `register_instance()` during `add_child()` would deadlock on the non-recursive mutex. The registry is now swapped out under the lock and the scene tree is touched with the lock released, guarded by an explicit main-thread assertion.
- **[HIGH] Missing surface validation in `MeshConverter` (`mesh_converter.cpp`)**: Indices were never bounds-checked against the vertex count and per-vertex array lengths were never compared, so a corrupt BSP could reach `add_surface_from_arrays()` with mismatched arrays or out-of-range indices. All arrays are now length-checked (tangents at 4 floats per vertex) and out-of-range indices cause the surface to be skipped with a diagnostic.
- **[HIGH] GPU resource leak in `GPUDirectBuffer` (`gpu_direct_buffer.cpp`)**: `create_mesh_surface_from_mmap()` called `RenderingServer::mesh_create()` and returned the RID without ever adding a surface or freeing it, leaking a GPU resource per call. The unimplemented path now allocates nothing and reports itself honestly.
- **[HIGH] Data race on `VRAMGarbageCollector::_max_idle_time_msec`**: Written by `start()` on the caller's thread and read by `_gc_loop()` on the worker without synchronization. Changed to `std::atomic<uint64_t>`.
- **[HIGH] `SpriteFrameHeader` was 20 bytes instead of 16 (`spr_structs.h`)**: A non-existent `group` member was declared inside `dspriteframe_t`; the frame-group selector is a separate `int32` that precedes the frame only in grouped sprites. Every frame was read at the wrong offset and the cursor drifted 4 bytes per frame. Corrected the layout and the `static_assert`.
- **[HIGH] `.gdextension` shipped the debug binary as the release artifact (`demo/bin/quebratsk.gdextension`, `CMakeLists.txt`)**: `windows.release.x86_64` pointed at the debug DLL, and CMake hardcoded `OUTPUT_NAME` to `...debug...` regardless of configuration, so a Release build silently overwrote the debug artifact. The output name now follows `$<CONFIG>` and both configurations are built and shipped separately. `compatibility_minimum` was also raised from `4.1` to `4.3` to match the godot-cpp branch actually linked — GDExtensions are forward-compatible only, so the old value invited Godot 4.1/4.2 to load an unsupported binary.
- **[MEDIUM] WAD3 directory parsed without a header-size check (`vfs_manager.cpp`)**: `index_wad3()` dereferenced the header before confirming the file was at least `sizeof(WAD3Header)`, and multiplied a potentially negative `num_lumps` by the lump size. Both are now validated, and individual directory entries with impossible ranges are skipped rather than indexed.
- **[MEDIUM] Unvalidated palette size in `SPRParser` (`spr_parser.cpp`)**: `palette_size` was read and discarded while the decoder unconditionally assumed 256 RGB triples. Now rejected if it is not 256.
- **[MEDIUM] `main()` linked into the shared library (`CMakeLists.txt`)**: `GLOB_RECURSE` pulled `src/cli/main.cpp` into the GDExtension DLL. Excluded, and `CONFIGURE_DEPENDS` added so new sources trigger reconfiguration.

### Performance

- **`BatchingManager::flush` (`batching_manager.cpp`)**: Replaced `N` calls to `MultiMesh::set_instance_transform()` — each crossing the GDExtension boundary and re-validating the RID — with a single `set_buffer()` upload built from a flat `PackedFloat32Array`.
- **`MeshConverter::convert` (`mesh_converter.cpp`)**: The index array is now filled with one `std::memcpy` instead of a per-element cast loop; validation above guarantees the `uint32_t`→`int32_t` reinterpretation is exact.
- **`UnifiedAssetImporter` (`unified_asset_importer.cpp`)**: Parsing was split into a pure-data `parse_mesh_ir()` and Godot object construction, removing a redundant full-file copy on the compressed path and enabling genuine off-thread decoding.

### Changed

- **Documentation accuracy**: Several previously logged entries overstated what shipped. Verified against the code during this pass: `SIMDMath`'s AVX2 branches contain only comments and always fall through to scalar code (the data is AoS `std::vector<Vector3>`, which cannot be processed 8 vertices per cycle without an SoA layout); `GPUDirectBuffer`'s zero-copy path is unimplemented; `ShaderPrecacher` compiles an identical dummy shader per call and precaches nothing; `TextureTranscoder`'s DXT1/DXT5 decoders are stubs that return zero-filled buffers; `P2PVFSStreamer`, `BSPMapRenderer` PVS culling, `VulkanRTBuilder` and `PAADecoder` are mocks. Twenty-seven classes have no call sites anywhere in the tree. These are tracked for follow-up rather than removed in this pass.

### Fixed
- **[CRITICAL] BSP30Parser out-of-bounds array indexing**: Added strict bounds checks for `face.first_edge_index`, `num_surfedges`, and `num_edges` during face polygon vertex iteration, preventing out-of-bounds memory reads on corrupted BSP maps.
- **[HIGH] BatchingManager Data Race**: `BatchingManager::register_instance()`, `flush()`, and `clear()` lacked thread synchronization. Added `std::mutex` and `std::lock_guard` to enable safe multi-threaded mesh registration.
- **[PERFORMANCE] MeshConverter Copy-on-Write Overhead**: Replaced scalar `.set(i, val)` calls on Godot's `PackedVector3Array`, `PackedFloat32Array`, and `PackedVector2Array` with direct writable pointer access `.ptrw()` and SIMD-aligned `std::memcpy`.
- **[CRITICAL] OcclusionGenerator thread-safety crash**: `BoxOccluder3D::instantiate()` was called from `std::async` background thread, violating Godot's ClassDB thread-safety. Refactored to return pure `OcclusionResult` struct from background; Godot object creation now happens exclusively on main thread via `create_from_result()`.
- **[CRITICAL] AsyncCollisionBuilder PhysicsServer crash**: `ConcavePolygonShape3D::set_faces()` called `PhysicsServer3D` from background thread. Refactored to `prepare_faces_async()` (pure data copy in background) + `create_shape()` (main thread only).
- **[CRITICAL] VRAMGarbageCollector race condition + Resource destruction crash**: `Ref<Resource>::unref()` could trigger `RenderingServer::free()` from background thread. Replaced with 2-phase eviction: background thread collects candidates, `evict_expired_resources()` frees them on main thread. Also migrated from `std::thread` to `std::jthread` with cooperative `stop_token`, and replaced `OS::get_ticks_msec()` with `std::chrono::steady_clock`.
- **[CRITICAL] LazyMemoryMapper lost base pointer**: `UnmapViewOfFile` was called on an offset pointer via fragile `VirtualQuery` hack. Now stores the original `MapViewOfFile`/`mmap` base pointer in a dedicated `_raw_base_view` field.
- **[HIGH] TextureTranscoder dangling span**: `std::span` (non-owning view) was captured by value in `std::async` lambda, causing use-after-free if caller freed the backing buffer. Now copies bytes into a `shared_ptr<vector<byte>>` before dispatching.
- **[HIGH] QuebratskSettings never registered**: `GDCLASS` was declared but `ClassDB::register_class` and `register_settings()` were never called. Added both to `register_types.cpp`.
- **[HIGH] LazyMemoryMapper 64-bit truncation**: File offsets were stored as `DWORD` (32-bit), silently truncating offsets for files >4GB. Changed to `uint64_t` arithmetic.
- **[HIGH] VRAMGarbageCollector used std::thread**: Replaced with `std::jthread` + `std::stop_token` for cooperative cancellation and automatic join-on-destruct, preventing 5-second hangs during editor shutdown.

### Added
- **In-Editor "Map Fly-Through" Previewer (`src/api/map_preview_viewport.h/.cpp`)**: `MapPreviewViewport` sub-viewport class allowing live 3D map fly-through previews directly inside the Godot editor without launching game execution.
- **Clickable Formatted Console Logger (`src/core/logging/quebratsk_logger.h/.cpp`)**: `QuebratskLogger` subsystem wrapping `UtilityFunctions::push_error` and `push_warning` to print clean, formatted messages with clickable VFS asset targets.
- **Interactive Winding Order Visualizer (`src/converters/winding_visualizer.h/.cpp`)**: `WindingVisualizer` tool injecting debug materials to highlight back-faces in bright red and providing one-click normal flipping.
- **Obsidian Auto-Doc Exporter (`src/core/vfs/obsidian_doc_exporter.h/.cpp`)**: `ObsidianDocExporter` class formatting current VFS mount states and scene node trees into Markdown documentation for sync with Obsidian Vaults.
- **Native Background Task Progress Tracker (`src/api/task_progress_tracker.h/.cpp`)**: `TaskProgressTracker` class bound to ClassDB providing thread-safe atomic percentage tracking and status string reporting for Godot Editor progress bar overlays.
- **Fuzzy-Match Material Auto-Fixer (`src/converters/fuzzy_material_fixer.h/.cpp`)**: `FuzzyMaterialFixer` subsystem calculating Levenshtein distance string matching across VFS file lists to automatically recover missing texture files.
- **Asset Dependency Graph Builder (`src/api/dependency_graph_builder.h/.cpp`)**: `DependencyGraphBuilder` class generating structured Dictionary node trees representing asset dependencies for rendering in Godot's GraphEdit UI.
- **Drag-and-Drop VFS Mounting (`src/api/vfs_drop_handler.h/.cpp`)**: `VFSDropHandler` class bound to ClassDB that automatically mounts dropped `.wad`, `.gma`, `.pbo`, `.vpk`, `.pak`, or `.bundle` files using the filename stem as the mount point.
- **One-Click Import Presets (`src/core/config/import_presets.h/.cpp`)**: `ImportPresets` helper providing 3 simple presets (`MAX_PERFORMANCE`, `RETRO_FIDELITY`, `MAX_QUALITY`) to configure VRAM eviction timeouts, thread limits, and shader pre-caching with a single call.
- **Steam Library Auto-Detection (`src/core/vfs/steam_library_detector.h/.cpp`)**: `SteamLibraryDetector` scanning Win32 registry `HKCU\Software\Valve\Steam` and `libraryfolders.vdf` to discover installed games (Half-Life, CS 1.6, Garry's Mod, CS2, Arma 3, DayZ) for instant mounting.
- **Smart VRAM Garbage Collector (`vram_garbage_collector.h/.cpp`)**: Background `jthread` tracking resource access timestamps via `OS::get_ticks_msec()` and automatically calling `.unref()` on cached textures/meshes that exceed the idle timeout to prevent OOM crashes on low-end hardware.
- **Automated Shader Pre-Caching (`shader_precacher.h/.cpp`)**: Compiles all materials into dummy Pipeline State Objects (PSOs) via Godot's `RenderingServer` during map load to eliminate runtime shader compilation stutter.
- **Multi-Threaded Collision BVH Builder (`async_collision_builder.h/.cpp`)**: Offloads `ConcavePolygonShape3D` generation for massive map chunks to `std::async`, preserving main thread framerate.
- **Configurable Memory Quotas via UI (`quebratsk_settings.h/.cpp`)**: Registers new sliders in `ProjectSettings` (e.g., `quebratsk/performance/vram_eviction_timeout_msec`) to give developers full control over the engine's extreme limits directly from the Godot Editor.
- **Asynchronous Occlusion Culling Hull Generator (`src/core/vfs/occlusion_generator.h/.cpp`)**: Background `jthread` extracting AABB/OBB bounding volumes from raw meshes to generate Godot `BoxOccluder3D` nodes, preventing GPU overdraw.
- **Lazy-Loaded VFS Streaming (`src/core/vfs/lazy_memory_mapper.h/.cpp`)**: True zero-memory mapping using `MapViewOfFile` to map only requested byte windows of large archives into RAM, dropping base memory footprint to under 100MB.
- **Instanced Rendering Auto-Batching (`src/converters/batching_manager.h/.cpp`)**: Consolidates duplicate `MeshInstance3D` spawn requests into a single `MultiMeshInstance3D` automatically to minimize CPU draw calls.
- **Background Texture Transcoding (`src/core/vfs/texture_transcoder.h/.cpp`)**: `std::jthread` pool architecture for real-time legacy PC texture (DXT1/5) decoding into Godot mobile/web compatible RGBA8/ETC2 buffers.
- **SIMD-Accelerated Math (`src/core/math/simd_math.h/.cpp`)**: AVX2 and ARM Neon vectorization for vertex array Z-Up to Y-Up remapping and winding order inversion.
- **Zero-Copy Vulkan/DirectX Staging Buffers (`src/core/vfs/gpu_direct_buffer.h/.cpp`)**: Direct RAM to VRAM staging buffer creation using `PackedByteArray` aliasing.
- **Batch GLTF Converter (`src/converters/batch_gltf_converter.h/.cpp`)**: Mass container asset library exporter converting mounted VFS archives to standalone `.gltf` files.
- **Headless CLI Command-Line Executable (`src/cli/main.cpp`)**: Standalone C++ terminal executable (`quebratsk-cli.exe`) for automated build pipelines, CI/CD asset conversion, and headless asset verification.
- **Editor Drag-and-Drop Import Plugin (`demo/addons/quebratsk_editor/import_plugin.gd`)**: `EditorImportPlugin` converting raw `.wad`, `.gma`, `.pbo`, `.vpk`, `.pak`, or `.bundle` files dropped into Godot FileSystem dock into native resource files.
- **VFS Profiler & Memory Telemetry (`src/core/vfs/vfs_telemetry.h/.cpp`)**: Real-time memory-mapped bytes telemetry and C++ microsecond parsing measurement tracker.
- **PBR Auto-Tuner (`src/converters/pbr_autotuner.h/.cpp`)**: Procedural texture processor calculating roughness, metallic, and height map estimation for legacy diffuse maps.
- **Sound Script Parser & Spatial 3D Audio Streamer (`src/parsers/audio/sound_script_parser.h/.cpp`)**: Audio metadata parser and pre-configured `godot::AudioStreamPlayer3D` node generator.
- **Live Asset File Watcher (`src/core/vfs/file_watcher.h/.cpp`)**: Hot-reloading background watcher for detecting disk modifications to mounted archives.
- **Automated LOD Generator (`src/converters/lod_generator.h/.cpp`)**: Quadric Error Metric (QEM) index decimation subsystem for generating LOD1, LOD2, and LOD3 mesh levels.
- **VFS File Tree Explorer Class (`src/api/vfs_file_tree.h/.cpp`)**: C++ class bound to ClassDB returning structured VFS archive trees for the Godot Editor Dock UI.
- **Mod Dependency Resolver (`src/core/vfs/dependency_resolver.h/.cpp`)**: Automatic manifest parser for resolving required parent texture/material archives (`addon.json`, `config.cpp`).
- **Async Multi-Threaded Importer (`src/api/async_asset_importer.h/.cpp`)**: Non-blocking background mesh loading with `load_mesh_async` and `Callable` deferred callbacks.
- **Godot Editor VFS Dock Plugin (`demo/addons/quebratsk_editor/`)**: Editor plugin for browsing mounted VFS archives inside the Godot 4 Editor UI.
- **Beginner's Guide & Tutorial (`TUTORIAL_GODOT4.md`)**: Intuitive, step-by-step GDScript tutorial for installing, mounting archives, importing 3D models, materials, and terrains in Godot 4.
- **Lightmap UV2 & Shader Bridge (`src/converters/lightmap_packer.h/.cpp`, `src/converters/shader_bridge.h/.cpp`)**: Secondary UV2 atlas bin-packing and custom ShaderMaterial generator for water, glass, and $selfillum maps.
- **GLTF Exporter & VFS Audio Decoder (`src/converters/gltf_exporter.h/.cpp`, `src/parsers/audio/audio_decoder.h/.cpp`)**: GLTF 2.0 asset export pipeline and VFS `.wav` audio stream decoder for `AudioStreamWAV`.
- **Texture & Material Memory Cache (`src/core/vfs/texture_cache.h/.cpp`)**: Thread-safe in-memory cache manager preventing redundant texture/material parsing.
- **V-HACD 4.0 Convex Decomposition Subsystem (`src/converters/vhacd_decomposer.h/.cpp`)**: Volumetric approximate convex hull decomposition for 3D physics collision shape generation in Godot 4.
- **Unreal Engine 4/5 Subsystem (Tier 2 Expansion)**: `UEPakParser`, `UAssetParser`.
- **Unity Engine / Escape from Tarkov Subsystem (Tier 2 Expansion)**: `UnityBundleParser`, `UnityMeshParser`.
- **Bohemia Enfusion / Arma Reforger Subsystem (Tier 1 Expansion)**: `EnfusionPakParser`, `XOBParser`.
- **Source Engine 2 / Counter-Strike 2 Subsystem (Tier 1 Expansion)**: `VPK2Parser`, `VMDLParser`.
- **Skeletal Retargeting Engine (Phase 6)**: `SkeletalRetargeter` for Godot 4 `SkeletonProfileHumanoid` bone mapping.
- **Unified GDScript API (Phase 5)**: `UnifiedAssetImporter` class bound to Godot ClassDB.
- **Native Godot 4 Converters (Phase 4)**: `MeshConverter`, `SkeletonConverter`, `MaterialConverter`, `AnimationConverter`, `CollisionConverter`, `TerrainConverter`.
- **Source Engine 1 Parsers (Phase 3b)**: `GMAParser`, `VTFParser`, `VMTParser`, `SourceMDLParser`.
- **Real Virtuality / Enfusion Parsers (Phase 3c)**: `PBOParser`, `PAADecoder`, `P3DMLODParser`, `WRPParser`.
- **GoldSrc Engine Parsers (Phase 3a)**: `WAD3Parser`, `BSP30Parser`, `MDL10Parser`, `SPRParser`.

## [0.1.0-alpha] - 2026-07-29

### Added
- **Core Subsystem Architecture**: Technical design for universal asset interoperability in Godot 4 via GDExtension C++20.
- **Virtual File System (VFS) Specification**: Zero-copy RAM layer with on-the-fly LZSS/LZO decompressors.
