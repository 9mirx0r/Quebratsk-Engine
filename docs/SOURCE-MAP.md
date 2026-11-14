# Source — a map of what the engine reads

**Status:** survey, 2026-11-14
**Scope:** the parts of Source 1 the lab has touched so far: the Shipped map (CS:GO port,
VBSP v20) and the PolyArms weapon pack for Garry's Mod.

> **Resumen en castellano.** Source no es "GoldSrc con más detalle": cada cosa está
> partida en varios archivos y hay una capa de materiales entre la textura y el mapa. Este
> documento dice, parte por parte, qué es, qué lee hoy Quebratsk (en C++ o en la prueba
> en GDScript), qué falta, qué problemas ya nos pegaron y qué prueba automática la cuida.
> Hecho: pruebas para VTF y VMT y que el motor lea los materiales "patch". Lo que sigue:
> puntos de los modelos y sus pieles, y pasar el lector de mapas de GDScript a C++.

---

## 1. Ground rules

Same evidence rules as [`GOLDSRC-ENCYCLOPEDIA.md`](GOLDSRC-ENCYCLOPEDIA.md) §2:

- `sdk`: Valve Developer Community pages and the public Source SDK headers they quote
  (`bspfile.h`, `gamebspfile.h`, `studio.h`, `imageformat.h`).
- `observed`: measured on files on this machine.
- No decompiled code, no pseudocode copied from anywhere. Workshop content (PolyArms,
  Shipped) is test material on this machine only: it is not shipped, and its Lua is read
  as data, never run.

Wiki pages used: VTF (Valve Texture Format), BSP (Source), MDL (Source), Patch, PCF.

## 2. How Source differs from GoldSrc

| Thing | GoldSrc | Source |
|---|---|---|
| A model | one `.mdl` (textures may sit in `T.mdl`) | `.mdl` (bones, sequences, names) + `.vvd` (vertices) + `.dx90.vtx` (triangles) + optional `.ani` (long animations) + `.phy` (physics) |
| A texture on a surface | WAD3 miptex, named by the face | the face names a **material** (`.vmt`, text) that names one or more **textures** (`.vtf`) and a shader |
| Map light | one RGB lightmap | two sets of faces and light (LDR and HDR); light samples are RGB + exponent; bumped faces carry 4 samples |
| What a map is made of | brushes, a few models | brushes **plus thousands of static props** stored outside the entity list |
| A weapon | the view model carries its own sounds and muzzle events | in Garry's Mod, a script beside the model says what is reload, what is heard, clip size; the model only moves |
| Muzzle flash, smoke | sprites | particle systems in `.pcf` files |

Most of the problems we hit come from that second row and that fifth row: a missing
material layer feature, or behaviour that lives in a script instead of the model.

## 3. The parts, one by one

Legend: **C++** = engine (`src/`), **GD** = trial script in `demo/lab/`, **—** = not read.

### 3.1 Containers (where files live)

| Container | What it is | Reads today | Missing |
|---|---|---|---|
| VPK v1/v2 (`*_dir.vpk` + `_000.vpk`…) | Valve's game archive. A directory tree of extension → path → name, then 18-byte entries | C++ `parsers/source2/vpk2_parser`, mounted by `vfs_manager.cpp` with its side archives. Pinned by `vpk_parser_test` (v1 and v2 trees, inline + preload, side archive, root file, bad terminator) | — |
| GMA | Garry's Mod addon archive | C++ `vfs_manager.cpp::index_gma` (mounts directly) | Workshop downloads that come LZMA-wrapped (`.bin`) are unpacked by hand today |
| Pakfile lump (BSP lump 40) | A ZIP inside the map with its custom materials, models and cubemaps; wins over outside files (`sdk`) | — (unpacked by a scratch script into a folder, then mounted) | Mount lump 40 as a ZIP container. Its entries can be LZMA-compressed (`sdk`) |

### 3.2 Textures — VTF

| | |
|---|---|
| Layout (`sdk`) | header (7.3+ adds resource entries) → small thumbnail → image data **for each mip, smallest to largest → each frame → each face → each depth slice**. So the full-size image is at the end of the file |
| Reads today (C++ `vtf_parser.cpp`) | full-size mip, frame 0, face 0, slice 0, found by walking forward from the start of the image data: the resource list for 7.3+, else after the header and the DXT1 thumbnail. Formats: RGBA8888, ABGR8888, ARGB8888, BGRA8888, BGRX8888, RGB888, BGR888, I8, IA88, A8, DXT1, DXT1 one-bit alpha (format 20), DXT3, DXT5 |
| Cube maps | flag 0x4000. Six faces per frame, plus a seventh (a sphere map) in files before 7.5 **unless** the first frame is -1. The wiki wording reads the other way round; all 96 cube maps of Shipped (7.4, first frame 0) have seven, as VTFLib counts them |
| Missing | RGB565/BGR565, BGRA4444/5551 (the bit order is not pinned yet, so they stay refused), UV88 (water normals), **RGBA16161616F** (HDR cubemaps, `sdk`: the 48 `.hdr.vtf` of Shipped are refused, their LDR twins read). Faces other than 0 are not handed out yet |
| HDR (`sdk`) | compressed HDR is an 8-bit texture with the scale in alpha: `RGB × (A × 16)`. Only the sky shader reads it that way. Our sky loader does this (`sky_loader.gd`) |
| Already hit | ABGR8888 decoded as white → white hands (fixed 2026-11-14). Skybox side faces are 512×256: only the top half is stored. A format with a known size and no decoder (ARGB8888, RGB565, P8) came back as a white picture instead of failing (fixed 2026-11-14, found by the test). DXT1 one-bit alpha had the number 26 (UVLX8888) instead of 20 (fixed 2026-11-14). The image was found by walking back from the end of the file, which breaks on cube maps, volume textures and 7.3+ files with data after the image (fixed 2026-11-14: the 407 textures of Shipped and PolyArms come out exactly at their file size) |
| Test | `vtf_parser_test` (done): one hand-built 2×2 VTF per decoded format behind a 1×1 decoy mip, a 2-frame one, DXT1, DXT1 one-bit alpha, DXT3, cube maps with six and seven faces (7.2, 7.4, 7.5), a 7.2 file with a thumbnail, a 7.3 file with a resource list and data after the image, and refusals (bad magic, truncated, no decoder, empty). Given a folder (`vtf_parser_test <dir>`), it reads every `.vtf` in it and lists the refused ones by format |

### 3.3 Materials — VMT

| | |
|---|---|
| What it is | KeyValues text: a shader name and `$parameters` |
| Reads today (C++ `vmt_parser.cpp`) | `$basetexture`, `$bumpmap`/`$normalmap`, `$envmapmask`, `$selfillummask`, `$detail`, `$translucent`, `$alphatest`, `$nocull`, `$additive` |
| `patch` (`sdk`) | a material that inherits everything from the `include` file and changes what is in `insert` / `replace`. It cannot change the shader. Used a lot by map-packed and model materials |
| Reads `patch` | C++ (`VMTParser::parse(bytes, read_include)`): the include is followed (a chain too, up to 8), `insert` adds or overrides and `""` removes, `replace` changes only keys the base has, the shader is the base's. `load_material` and `TextureLoader` find the include by its path in the mounted files, the patch's own archive first. `source_map.gd` now just asks the importer. Shipped: 199 of its 208 patches reach their base here; the other 9 name a base that is not in the folder |
| Missing | shaders that matter for looks: `LightmappedGeneric` vs `VertexLitGeneric` vs `UnlitGeneric`, `WorldVertexTransition` (two textures blended on displacements), `Water`, `$envmap` (cubemap reflections), `$phong`, `$color`/`$color2` tints, `$basetexturetransform` |
| Already hit | The reader went line by line and took the rest of the line as the value, so a material written on one line got `sprites/glow $additive 1 }` as its texture. It now reads words and braces, so layout does not matter (fixed 2026-11-14, found by the test; the 482 loose materials of Shipped and PolyArms read the same base texture as before) |
| Test | `vmt_parser_test` (done): quoted/unquoted keys, mixed case, comments, prefix keys, Proxies ignored, one-line material, zero flags, empty file. Patches: replace, insert overriding and removing, replace not adding, a chain, a loop, a missing base, no reader, `include` inside `insert`. Given a folder (`vmt_parser_test <dir>`), it also cross-checks every `.vmt` in it, following patches to their base in the same folder |

### 3.4 Models — MDL / VVD / VTX / ANI

| | |
|---|---|
| Versions read | MDL 44–49, VVD 4, VTX 7 (C++ `mdl_source_parser`, `vvd_parser`, `anim_decoder`) |
| Reads today | bones, meshes from VVD+VTX (LOD 0 with fixups), material names (`cdtextures`), sequences with their compressed rotations/positions, `.ani` animation blocks, `includemodel` (animations borrowed from another model), sound events. **Attachments** (`sdk` `mstudioattachment_t`, 92 bytes): name, bone, and the point's place and turn on that bone, so `list_attachments` gives a muzzle with the way it points (−z of its `basis`). **Skin families** through the skin table (`sdk`): a mesh names a slot and the family row says which texture fills it; a family the model lacks falls back to 0, as the game does. `load_model(..., skin)` picks one; the model node carries `quebratsk_skin` / `quebratsk_skins` |
| Missing | Body groups. Hitboxes. `.phy` (props use our own collision). Flex (faces). IK rules and pose parameters. LODs above 0 |
| Already hit | View-model bones: the importer already turns them to look down −z; adding a turn put the weapon off-screen. Hands (Garry's Mod `c_` arms) are a separate model with the same bone names; the weapon's skeleton lacks some of them (Ulna, Wrist), so they map to the nearest existing parent — done in `player.gd::_put_hands_on`, in GDScript |
| Not run | the Lua that says which sequence is the reload, the sounds and their times, clip size. Copied as plain values into `demo/lab/source_weapons.gd` |
| Test | `source_mdl_test` checks skin families (choice, fallback, a truncated table ignored) and attachments (name, bone, place, which way it points, a point on a missing bone dropped). `source_mdl_test <folder>` reads every model under it: on the Shipped map's 267 models none is refused, and 60 of the 61 with more than one family wear something else in another (the last has the same row twice in the file) |
| Test to add | a small check that the hands' bones all find a home on the weapon skeleton |

### 3.5 Maps — VBSP (BSP v20)

| | |
|---|---|
| Header (`sdk`) | `"VBSP"`, version, 64 lumps of {offset, length, version, fourCC}, map revision |
| Reads today | **C++** `parsers/source1/vbsp_parser` (since 2026-11-14), through the importer: `load_map` builds the faces, displacements and brush entities with their baked light, `load_map_entities` the entities, `load_map_props` the static props as {model, skin, solid, transform}. `demo/lab/source_map.gd` (480 lines of reading before) is now a thin wrapper: which brush entities are shown and stood on, and props loaded a few at a time |
| Lumps used | 0 entities, 1 planes, 2 texdata, 3 vertices, 6 texinfo, 7 faces (or 58 HDR faces), 8 light (or 53 HDR light), 12 edges, 13 surfedges, 14 models, 26/33 displacements, 35 game lump (`sprp` static props), 40 pakfile, 43/44 texture names |
| Light (`sdk`) | each sample is `r, g, b, exponent`; colour × 2^exponent. Up to 4 light styles per face. Bumped faces store 4 samples per luxel. We read style 0, first sample: correct for the flat look. The HDR light is preferred when the level has it. It stays linear floats and becomes a half-float texture (`MapLighting::create_linear`), since it runs past white. A displacement's light is laid out on its grid, not on the face's texture axes |
| Static props (`sdk`) | dictionary of 128-byte names, a leaf list, then one record per prop: origin, angles, model index, solidity, flags, **skin**, fade distances, then more per version. Our offsets match v4+ |
| Pakfile (lump 40) | a zip stored inside the level: its own materials, cubemaps and patches. `parsers/source1/zip_directory` reads its file list (PKWARE APPNOTE layout); `VFSManager::mount_container(prefix, level.bsp)` mounts it, and a plain `.zip` the same way. Only files stored as they are can be read (no inflater or LZMA here); the level compiler stores them that way: Shipped packs 10,234 files, all stored. The lab mounts it as `mappak_<name>`. It ranks after the level's own folder, where the game searches it first |
| Ambient light (`sdk`, done 2026-11-14) | the light for moving things: lumps 55/51 (HDR) or 56/52 (LDR), HDR preferred. Per leaf an index {count, first}; each sample is a cube of six `r, g, b, exponent` colours (+X, -X, +Y, -Y, +Z, -Z) and a spot inside the leaf as fractions of its box /255. Leaves of version 0 carry one cube inline instead. `VBSPAmbient` keeps the split planes (lump 5 nodes, lump 1 planes, from model 0's head node) so a point finds its leaf; `light_at` blends the leaf's samples by 1/distance². `load_map_ambient` hands it to GDScript; `floor_light.gd` lights people, the weapon and each prop by it (one material per prop, six instance colours). **Shipped's stored light is near black** (brightest 0.006 against walls around 1.0: the CS:GO original lit models with the live sun), so the lab refuses a level whose brightest sample is under 1/20 of its walls' 90th percentile (`AMBIENT_FLOOR`) and keeps the floor light |
| Missing | **prop light** (`.vhv` files), **cubemaps** (lump 42), **overlays** (lump 45: decals like road lines), two-texture blend on displacements (`WorldVertexTransition`), water as water (it is drawn and stood on now), LZMA-compressed lumps (`sdk`: newer TF2 and console maps; counted, not opened), the Source sky built by the importer (the lab's `sky_loader.gd` does it), visibility, the map's own collision (lump 29: brush entities use their mesh) |
| Already hit | Faces split into LDR/HDR sets: the HDR face list must be paired with the HDR light. Brush entities (`func_*`) drawn at their entity origin. A face's side flag does not flip its normal. Materials whose `$translucent`/`$alphatest` says so are drawn cut out (`TextureLoader::is_masked`). Most materials share their picture's name (`de_shipped/deck01` names `de_shipped/deck01`); the loader took that for the material naming itself and drew 17 of Shipped's wall and deck materials white (fixed 2026-11-14). Shipped's sunlit walls look very bright; the old GD reader's picture is the same, so it is the level's HDR light under our exposure, not the reader |
| Test | `vbsp_parser_test` (done): a hand-built level with one lit face, one displacement, one sky face, one prop and a world entity; given a level or folder, every face drawn or accounted for, normals, displacement edges meeting, light inside its block. `zip_directory_test` (done): stored files, a local header longer than the directory's copy, a level's pakfile, broken directories. Lab on Shipped: 766 entities, 25 brush entities, 3726 of 3736 props from 226 models, as with the GD reader |

### 3.6 Light and sky

| | |
|---|---|
| Skybox (`sdk`) | `sv_skyname` in the world entity → `materials/skybox/<name><rt/lf/bk/ft/up/dn>.vmt/.vtf`. `_hdr` versions use the alpha-scale HDR above |
| Reads today | **GD** `addons/quebratsk_editor/sky_loader.gd`: Source faces via the importer, HDR scale, half-height faces padded |
| Missing | 3D skybox (`sky_camera`: a small copy of the far world drawn around the map), sun from `light_environment` |
| Already hit | No sky → no ambient light → black weapon. The ambient cube is read (3.5), but a level ported from CS:GO may store it near black. |

### 3.7 Particles — PCF

| | |
|---|---|
| What it is (`sdk`) | binary DMX: a header string such as `<!-- dmx encoding binary 5 format pcf 2 -->` (CS:GO, Portal 2), a string dictionary, an element dictionary, then the attribute data. Each particle system is a tree of emitters, initializers, operators and renderers |
| Why it matters | the real muzzle flash, smoke, sparks and impacts of Source weapons live here |
| Binary DMX (wiki DMX/Binary, `sdk` dmattributetypes.h) | encodings 1 to 5. Strings go into a table from 2; element names and single string values use it from 4, indexes are 32-bit from 5; strings inside arrays are always written in place. Attribute types 1–14 (element, int, float, bool, string, void, time, color, vector2/3/4, qangle, quaternion, matrix), an array is the type + 14. Time is tenths of a millisecond (before 3 that slot was a 16-byte object id). Colour is 4 bytes. An element link is its place in the list, -1 for none, -2 for one kept elsewhere (its id follows as text) |
| Reads today | **C++** `parsers/source1/dmx_reader` (any binary DMX) and `pcf_reader` (since 2026-11-14): every `DmeParticleSystemDefinition` as a system, its emitters, initializers, operators, renderers, forces and constraints in order (each by `functionName`), and its children with their delay. A file cut short anywhere is refused rather than half read. The importer's `load_particles` hands each system to GDScript as dictionaries, values in game units. **GD** `demo/lab/source_particles.gd` builds one `GPUParticles3D` per system that draws, children placed under it with their delay. It follows: lifetime, radius and rotation at random, the "remap initial scalar" steps that set lifetime or radius from when a particle was made, "remap scalar to vector" that strings a burst along a line, both emitters, position in a sphere, basic movement, radius scale over life, colour and alpha at random, colour fade and alpha fades. Any other step is named once in the log |
| Muzzle flash | the weapon card names its system and its point (`muzzle_particle`, `muzzle`); `player.gd` hangs the effect on that point's bone and fires it with each shot. The PolyArms AK numbers its points: 1 sits on the barrel bone, the furthest forward. CS:S's `muzzleflash_ak47` is not on this machine, so HL2's `hunter_muzzle_flash` stands in (blue, not orange). Its files come from Garry's Mod's copy of HL2, mounted behind the game: `content_hl2_dir.vpk` (the particle file and its burst), `hl2_misc_dir.vpk` (the streak's material), `hl2_textures_dir.vpk` (the streak's picture) |
| Missing | other operators (trails, sprites that face along their movement, noise, collisions, control points beyond the first); sheets (several frames in one picture) are drawn whole; the text DMX encoding (keyvalues2); lights a system asks for |
| Test | `pcf_reader_test` (done): a hand-built file written in each of the five encodings, links to elements kept elsewhere, cut-short files refused at every length, an unknown attribute type, a text header, a model file. Given a folder, every `.pcf` in it: GMod's HL2 and CS:S files, 20 files and 417 systems, none refused. `demo/tests/verify_particles.tscn`: the stand-in flash has a picture on every part and lasts 0.1 s, the AK has its point, and it saves a picture of the flash fired |
| Already hit | A material path in a particle file uses backslashes and may carry `.vmt`. The hunter flash sets its lifetime and stretch through remap steps, not "Lifetime Random": read as a 1 s blob before those were followed |

### 3.8 Sounds

| | |
|---|---|
| Reads today | `.wav` and `.ogg` by path (GD `SoundLoader`). **Soundscripts** (C++ `converters/sound_resolver.cpp`): a model's event names an entry like `Weapon_357.Single`, and the `scripts/*sound*.txt` files say which file it is; `list_sounds` follows the name to the file |
| Missing | volume, pitch and channel from the soundscript entry (the files are found, `rndwave` takes included, but how loud and how high is not passed on); CS:GO's operator stacks. Garry's Mod weapons name their sounds in Lua instead, so for them the times live in `source_weapons.gd` |
| Test to add | a resolver test with a two-entry soundscript text, checking the files it returns |

## 4. Order of work

1. **Tests first** for what is already read: `vtf_parser_test`, `vmt_parser_test`, a VPK/GMA directory test. Small hand-built files, no game content in the repo.
2. **Small C++ fixes those tests will pin**: DXT3, ARGB8888, cubemap faces, VMT `patch`. *(done 2026-11-14)*
3. **Model attachments and skin families** in `mdl_source_parser` (muzzle point, prop skins). *(done 2026-11-14; GoldSrc skins fixed on the way, see below)*
4. **Move the map reader to C++** (`parsers/source1/vbsp_parser`), lump by lump, with the current GD trial as the reference picture; add displacements, ambient light and the pakfile on the way. *(done 2026-11-14)*
5. **PCF reader**, then the muzzle flash. *(done 2026-11-14 with HL2's hunter flash standing in; not yet seen in the lab, which cannot build its player while Half-Life / CS 1.6 are missing from this machine)*

Each step ends with its test passing and the lab still opening both the GoldSrc map and
Shipped.

### Found on the way: GoldSrc skins

Doing step 3 showed the GoldSrc reader had the same gap, and one more. It read the skin
table only from the main `.mdl`, but a model split in two (`foo.mdl` + `fooT.mdl`) keeps its
textures in the T file, and in Half-Life's own folders 195 of the 204 split models keep the
skin table only there (9 have it in both, none only in the main file). The HL SDK model
viewer (`utils/mdlviewer/studio_render.cpp`) reads it from the texture header too.
`mdl10_parser` now reads it from the T file when that declares one, and picks the family
row the same way as Source. `mdl10_parser_test <folder>` checks real files: on all of
Half-Life (2724 models, none refused), 97 of the 107 with more than one family wear
something else in another; the other 10 have identical rows in the file itself. In the lab,
cs_italy's hostages ask for skins 2, 3, 1 and 0; `hostage.mdl` has two, so they wear 0, 0, 1
and 0, which is what the game does with a family the model lacks.
