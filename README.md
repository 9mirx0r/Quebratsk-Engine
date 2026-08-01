<p align="center">
  <img src="docs/images/logo.jpg" alt="Quebratsk Engine" width="100%" style="border-radius: 8px;"/>
</p>

<h1 align="center">Quebratsk Engine</h1>

<p align="center">
  <b>Open a game you own. Find a model. Press Add. It is in your scene.</b>
</p>

<p align="center">
  <a href="LICENSE"><img src="https://img.shields.io/badge/License-MIT-blue.svg" alt="MIT License"/></a>
  <a href="https://godotengine.org"><img src="https://img.shields.io/badge/Godot-4.3%2B-blueviolet.svg?logo=godotengine&logoColor=white" alt="Godot 4.3+"/></a>
  <a href="https://en.cppreference.com/w/cpp/23"><img src="https://img.shields.io/badge/C%2B%2B-23-00599C.svg?logo=c%2B%2B&logoColor=white" alt="C++23"/></a>
  <a href="../../actions/workflows/build.yml"><img src="../../actions/workflows/build.yml/badge.svg" alt="Build"/></a>
  <a href="docs/TUTORIAL.md"><img src="https://img.shields.io/badge/Guide-for%20beginners-green.svg" alt="Beginner's guide"/></a>
</p>

Quebratsk is a Godot 4 plugin that reads the files inside Half-Life,
Counter-Strike, Condition Zero, Half-Life 2, Garry's Mod, Arma and DayZ, and
turns them into things Godot understands: meshes, skeletons, animations,
materials, sounds and whole levels.

No exporting. No converting. No command line. You point it at a game that is
already installed and it reads it where it sits.

---

## Why this exists

Valve's games were beautiful, and they were beautiful in a way that came from
care rather than from budget. Someone chose where every light in that corridor
went. Someone decided a scientist should have eleven ways of standing still.
That detail is still in the files, still on the drive, and most of it has not
been looked at in twenty years.

This project started from wanting to bring that work somewhere it could be
handled again, and Godot is the right somewhere: free, open, and simple enough
that opening a model does not require being an engineer first. Partly it is a
tool. Partly it is going back for something from childhood and finding it intact.

Twenty-five years of game content is sitting on people's hard drives in formats
that nothing modern opens. A `.mdl` from 1998 holds a mesh, a skeleton, a hundred
animations and the name of every sound it makes, and there is no way to look at
any of it without either owning a decompiler from a forum thread or being the
sort of person who reads binary layouts for fun.

The tools that do exist are scattered. Each reads one format, most want you to
convert on the command line first, and nearly all of them assume you already know
what a bone weight is.

Quebratsk exists so that opening a Half-Life model in Godot takes the same effort
as opening a PNG.

## What we are aiming at

**That it be usable by someone who has never opened Godot.** That is the whole
brief and every decision gets measured against it. If a step needs a tutorial,
the step is wrong.

**That an import be correct, not approximately correct.** A model should arrive
with the animations it actually has, wearing the textures it was built with,
making the sound its own firing sequence names. Not a plausible substitute.

**That claims come with numbers.** Almost every serious defect in this project was
invisible to compiling and to code review, and only showed when the code ran
against a retail install and someone counted the result. So nothing here is called
working without a measurement beside it, and those are in
[CHANGELOG.md](CHANGELOG.md).

## How this is built

This repository is written in pair-programming with **Claude Code** and **Google Antigravity
IDE**, under human direction throughout, and we would rather say so plainly than have anybody
wonder.

Open-source developers are right to be wary of projects that generate code nobody read and
flood a repository with boilerplate that has never run. This one works the other way round:

- **A human decides what gets built and judges whether it worked.** Every feature here was
  asked for, and every claim of success was accepted or rejected by someone looking at the
  result on their own machine.
- **Verification is against retail installs, by hand as well as by harness.** The most
  valuable checks in this project's history were screenshots. Four separate defects survived
  every automated test and fell to somebody noticing that a character was standing sideways or
  a body was buried to the waist. Two unit tests had the bug written into them as the expected
  answer.
- **Structures are pinned, not assumed.** `#pragma pack(push, 1)` with `static_assert` on
  every offset the code indexes by, so a layout that drifts stops compiling rather than
  quietly reading the wrong bytes.
- **Nothing is called working without a number.** Those numbers are in the changelog beside
  the change that produced them.

Modern C++23, memory-mapped I/O, zero-copy `std::span` over the mapped bytes, and bounds
checks written by subtraction so an offset cannot overflow its way past them.

---

<p align="center">
  <img src="docs/images/roadmap.jpg" alt="Roadmap" width="100%" style="border-radius: 8px;"/>
</p>

## Roadmap

Five engines, left to right in the image above, lit by how far along they are.

### 1. GoldSrc — shipped

*Half-Life, Counter-Strike, Condition Zero, Opposing Force, Blue Shift.*

Maps arrive with their geometry, their entities, their skybox and the ambient
sound they declare. Models arrive with their skeleton, their textures and their
animations, including the sequences kept in sidecar files, which is where most of
a Half-Life character's stances actually live. Sounds resolve through the model's
own events and through `sentences.txt`.

Measured against retail installs: **122 of 123 surfaces textured** on a downloaded
map, **111 sequences** on the Counter-Strike terrorist where there used to be one,
**244 of 253** sounds a map declares reaching a real file.

### 2. Source — models shipped, maps in progress

*Half-Life 2 and its episodes, Garry's Mod, Counter-Strike Source.*

Models, materials, animations and soundscripts are done: **40 of 40 models come in
fully textured**, with their animation sets and the right firing sounds resolved
through the games' own soundscripts.

Maps are the piece being built now. Source levels are VBSP v19 and v20, a different
format from the GoldSrc BSP already shipping, and the reader for them is next on
the bench.

### 3. Real Virtuality — in progress

*Arma, DayZ.*

Archives open and textures decode, so the material side is working. Two pieces are
still being written: retail ships models in compiled ODOL form where only the
editable MLOD form reads today, and terrain is `OPRW v29`, which follows the model
work.

### 4. BeamNG.drive — planned

On the roadmap, not yet started. The format work begins once Source maps land.

### 5. Grand Theft Auto, San Andreas onward — planned

On the roadmap, not yet started. The furthest out and the most interesting: a
different lineage entirely, which is rather the point of going there.

---

## Getting started

### Install

1. Download the latest release and unzip it.
2. Copy the `addons/quebratsk_editor` folder into your Godot project.
3. In Godot, open **Project → Project Settings → Plugins** and tick **Quebratsk**.

A panel appears on the left. That is the whole installation.

### Add a game

Press **Add a game**. Quebratsk lists what it found on your machine, including
Steam libraries on other drives. Pick one.

If a game is not listed, choose **Browse for a folder** and point it at the folder
yourself. Extracted asset folders and single archives work too.

Adding a game reads its index, not its contents, so it is quick and costs no disk
space.

**Workshop content is included.** Steam keeps subscribed mods outside the game
folder entirely, under `steamapps/workshop/content`, so scanning a game directory
finds none of them and most tools never see them at all. Quebratsk mounts them
alongside the game, which on a well-used Garry's Mod install is several hundred
archives and the large majority of what that person actually owns. A model from a
workshop addon behaves exactly like one that shipped with the game.

### Find something

Type into the search box. The categories beside it narrow things down without your
having to know what a file extension means:

| Category | What it holds |
|---|---|
| Characters and people | Player models, NPCs, anything with a skeleton |
| Weapons | Guns, knives, the things characters hold |
| Props and scenery | Crates, furniture, vegetation, machinery |
| Levels | Whole maps you can walk around in |
| Sounds | Weapon fire, footsteps, ambience, speech |
| Textures | Surfaces on their own |

The dropdown beside them limits the search to one game, workshop content
included. Steam keeps four games in the folder it calls Half-Life, and each is
listed separately under its own name, so asking for Counter-Strike gives you
Counter-Strike.

### Bring it in

Select something and it previews on the right. Then either:

- **Add to scene** puts it in the scene you have open, where you are standing.
- **Save** writes it to `res://imported/` as a reusable scene you can drag in
  anywhere, version and edit.

**Save is the one you want for a real project.** A saved scene is an ordinary Godot
scene that loads without Quebratsk and without the game installed, so whoever plays
your game needs neither. Textures and meshes are written beside it as their own
resources, which keeps the scene small and lets two models that share a texture
share the file.

### Make it move

Models arrive standing in a pose. Tick **Bring it in moving** and choose:

- **Just this one** brings in the sequence picked in the pose list.
- **The usual moves** finds idle, walking, running, crouching, jumping, shooting,
  reloading and dying among the hundreds a model carries, and brings them in
  together as an `AnimationPlayer` you can play by name.

A Condition Zero player model has 494 sequences and almost all of them are
situational. The second option exists so you never have to read that list.

### Try the sandbox

The repository includes a playable scene at `demo/sandbox/`. Open the `demo`
project, press **F5**, and you are standing in a map from a game you own, with
characters and a weapon from that same game.

It exists to prove the pipeline works end to end, and it has found more defects
than any other check in this project.

---

## Contributing

Contributions are welcome. One thing is worth knowing before you start, because it is
unusual.

### Measure it against a real game

Compiling proves nothing here and neither does review. The harnesses in
`demo/tests/` run against whatever games are installed on your machine:

```bash
godot --headless --path demo res://tests/verify_materials.tscn
```

Two habits that have earned their place:

- **Measure something that could come out wrong.** "It loaded" is not a result.
  "10 of 10 models with more than one sequence move a bone" is.
- **When a check reports a failure, suspect the check first.** More than once the
  first draft of a harness measured its own setup rather than the thing under test.
  One reported everybody standing correctly on the floor while the screenshots
  showed them buried to the waist.

### Building

CMake and a C++23 compiler. `godot-cpp` at `4.3-stable` is a submodule.

```bash
cmake -S . -B build
cmake --build build --config Release
```

---

## Standing on other people's work

Quebratsk ships no third-party code. These are the projects that answered questions
which would otherwise have been answered by guessing, and guessing is how several of
this project's worst defects got in. Full detail, including which licence permitted
what, is in [CREDITS.md](CREDITS.md).

- **[xash3d-fwgs](https://github.com/FWGS/xash3d-fwgs)** (GPL-2.0) — a maintained
  GoldSrc reimplementation, read as a reference for **what the formats are**, never
  copied. It settled the sequence structures, the two different event layouts, where
  sidecar animations live, and how the first-person view is actually drawn.
- **[goldsrc-character-controller](https://github.com/ratmarrow/goldsrc-character-controller)**
  by ratmarrow (CC0-1.0) — a clear reading of Quake's acceleration and friction,
  which the sandbox's movement follows.
- **[GoldGdt](https://github.com/ratmarrow/GoldGdt)** (MIT) — the larger project that
  came from, consulted on unit conversion.
- **[goldsrc-godot](https://github.com/alanfischer/goldsrc-godot)**,
  **[Godot-GoldSrc-MDL-Importer](https://github.com/DataPlusProgram/Godot-GoldSrc-MDL-Importer)**
  and **[godot_bsp_importer](https://github.com/jitspoe/godot_bsp_importer)** — read
  for approach.

---

## A note on what you make with this

The content inside these games belongs to Valve, Bohemia Interactive and their
authors. Quebratsk reads what you already own and redistributes nothing.

Reading those assets to prototype, to learn, or to build something for yourself is
one thing. Shipping them inside a game you release is another, and owning the game
does not permit it. If you are heading for release, the usual paths are to publish as
a mod that requires the base game, or to use these assets to block out your idea and
replace them before you ship.

Worth saying plainly, because the second path is more achievable than it sounds: the
look these games have is low polygon counts and small textures, and that is among the
easier aesthetics to reproduce with assets of your own.

---

## Documentation

| | |
|---|---|
| [CHANGELOG.md](CHANGELOG.md) | What changed, with the measurement that proved it |
| [docs/API.md](docs/API.md) | Every class and method, for scripting against it |
| [docs/TUTORIAL.md](docs/TUTORIAL.md) | Longer walkthroughs |
| [CREDITS.md](CREDITS.md) | Who answered what |
| [CONTRIBUTING.md](CONTRIBUTING.md) | Building, testing, and what bites |

## Licence

See [LICENSE](LICENSE).
