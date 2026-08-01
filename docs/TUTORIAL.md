# Beginner's guide

This walks you from a fresh Godot install to a Half-Life 2 character standing in your
scene. It assumes no Godot experience and no programming.

You need Godot 4.3 or newer, on Windows, and at least one supported game installed —
Half-Life, Counter-Strike 1.6, Half-Life 2, Garry's Mod, Portal or Team Fortress 2.

---

## 1. Install the plugin

1. Download the latest `quebratsk-engine-*.zip` from the
   [Releases page](https://github.com/9mirx0r/Quebratsk-Engine/releases).
2. Open your Godot project folder — the one with `project.godot` in it.
3. Extract the archive there. You should end up with two new folders next to
   `project.godot`:

   ```
   your-project/
     project.godot
     bin/                      <- the engine itself
     addons/quebratsk_editor/  <- the panel you will use
   ```

4. Close Godot and open the project again. This is required: Godot only loads native
   extensions at startup.
5. Go to **Project → Project Settings → Plugins** and tick **Enable** next to
   *Quebratsk Engine*.

A **Quebratsk** tab appears in the panel on the left, next to *Scene* and *Import*.

> If you do not see it, the tab strip may be too narrow — use the small arrows at its
> right edge to scroll along.

---

## 2. Add a game

Open the **Quebratsk** tab and press **Add a game**.

The menu lists the games it found installed on your computer. Pick one — Half-Life 2, say.

Nothing is copied. The game's archives are read where they already are, so adding
Half-Life 2 costs no disk space and takes a second or two even though it is about 100,000
files.

Your game now appears in the list at the top with a file count. The trash icon removes it.

You can also use **Choose a game folder** for a game that was not detected, or for an
extracted mod folder, and **Open one archive file** for a single `.vpk`, `.wad`, `.gma` or
`.pbo`.

---

## 3. Find something

Three ways, and you can combine them:

- **The category dropdown** — *Characters & people*, *Weapons*, *Vehicles*,
  *Props & scenery*, *Maps & terrain*, *Textures & materials*, *Sounds*. Use this when you
  want to browse rather than search for something specific. *Favourites* and *Recently
  imported* are there too, once you have starred or imported anything.
- **The game dropdown** beside it narrows everything to one game.
- **The search box** — type part of a name, like `police` or `dust`.

Each result shows what the file is called and, after a slash, which game it came from. That
second part matters more than it looks: both Half-Life 2 and Garry's Mod ship a `police.mdl`,
and they are different models.

The game dropdown lists games by the names they call themselves, which is not the same as the
folders they live in. Steam keeps Half-Life, Counter-Strike, Condition Zero and its Deleted
Scenes in one folder called Half-Life, and each of those is a separate entry here, so asking
for Counter-Strike gives you Counter-Strike and nothing else.

---

## 4. Put it in your scene

1. Open or create a 3D scene first. Godot needs somewhere to put the model — if you forget,
   the panel will tell you.
2. Click a result. The bottom section shows what you picked.
3. If it is a character or a prop, a **Standing pose** dropdown appears. Leave it on
   *Whatever the game uses by default*, or choose one — a Garry's Mod player model offers
   342, the same ones the game itself uses. `idle_smg1` is a soldier holding a rifle;
   `crouch_walk_pistol` is what it sounds like.
4. Press **Add to scene**.

The model appears in your scene tree, textured, with its skeleton, standing in the pose you
chose. From there it is a normal Godot node: move it, rotate it, attach a script.

**Ctrl+Z** undoes the import like any other editor action.

### Bringing it in moving

A pose is one frozen frame. Tick **Bring it in moving** to get the animation instead, and
choose how much:

- **Just this one** imports the sequence selected in the pose list.
- **The usual moves** finds idle, walking, running, crouching, jumping, shooting, reloading
  and dying among the hundreds a model carries, and brings them in together.

Either way they arrive as an `AnimationPlayer` on the model, and you play them by name. A
Condition Zero player model has 494 sequences and nearly all are situational, so the second
option exists to spare you reading that list.

### Save, if this is for a real project

**Add to scene** is a one-off. **Save** writes the import to `res://imported/` as a scene you
can drag in anywhere, version and edit.

Save is the one that matters for anything you intend to ship. A saved scene is an ordinary
Godot scene: it opens without Quebratsk and without the game installed, so whoever plays your
game needs neither. Textures and meshes are written beside it as their own resources, which
keeps the scene small and lets two models that share a texture share one copy of it.

Sounds save the same way and land beside the scenes.

---

## What you can and cannot import

| Works today | Being built |
|---|---|
| Models from Half-Life, Counter-Strike 1.6, Condition Zero, Half-Life 2, Garry's Mod, and workshop addons | Maps from Source games. Those are VBSP v19 and v20, a different format from the GoldSrc BSP already working |
| Maps from the GoldSrc games, with their entities, skybox and ambient sound | Arma and DayZ models. Retail ships them compiled and only the editable form reads so far |
| Sounds, which import as well as preview | Arma and DayZ terrain, which is `OPRW v29` |
| Textures, which come in attached to whatever uses them | |

If an import fails, the panel says why in plain language rather than logging to a console
you are not watching. The most common cause is a Source model whose shape lives in
companion files inside a different archive: add the rest of that game's archives and try
again.

---

## Doing it from code

Everything the panel does is available from GDScript. See [API.md](API.md) for the full
reference — mounting, searching, listing poses, async loading and error codes.

```gdscript
var vfs := VFSManager.new()
add_child(vfs)

var importer := UnifiedAssetImporter.new()
add_child(importer)
importer.set_vfs(vfs)

# Mount the _dir.vpk only; it pulls in its own numbered side archives.
vfs.mount_container("hl2", "C:/.../half-life 2/hl2/hl2_misc_dir.vpk")

var npc := importer.load_model("vfs://hl2/models/police.mdl", "idle_smg1")
add_child(npc)
```

---

## A note on ownership

This plugin ships no game content. It reads files you already own, from where you already
installed them. What you may do with imported assets is governed by each game's own EULA —
redistributing them in a commercial project generally is not allowed.
