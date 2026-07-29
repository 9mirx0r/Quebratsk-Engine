# Changelog

All notable changes to **Quebratsk Engine** are documented in this file.

The format is based on [Keep a Changelog](https://keepachangelog.com/en/1.1.0/),
and this project adheres to [Semantic Versioning](https://semver.org/spec/v2.0.0.html).

---

## [Unreleased]

### Added
- **GoldSrc Engine Parsers (Phase 3a)**:
  - `WAD3Parser`: Decodes Miptex 8-bit paletted textures into uncompressed RGBA8 pixel buffers in `src/parsers/goldsrc/wad3_parser.h/.cpp`.
  - `BSP30Parser`: Parses GoldSrc map geometry (vertices, faces, surfedges, texinfo, lightmaps) and clipnodes into `IRMeshData` and `IRCollisionData` in `src/parsers/goldsrc/bsp30_parser.h/.cpp`.
  - `MDL10Parser`: Parses StudioMDL v10 bone hierarchies, local rest poses, bodyparts, and submeshes into `IRSkeletonData` and `IRMeshData` in `src/parsers/goldsrc/mdl10_parser.h/.cpp`.
  - `SPRParser`: Decodes GoldSrc `.spr` sprite frames and 256-color palettes into RGBA8 frames in `src/parsers/goldsrc/spr_parser.h/.cpp`.
- **Intermediate Representation (IR) Layer**: Complete C++20 IR headers (`IRMeshData`, `IRSkeletonData`, `IRMaterialData`, `IRAnimationData`, `IRCollisionData`, `IRTerrainData`).
- **Coordinate Conversion & Axis Remap Module**: Vector, normal, quaternion, and matrix transforms for $Z$-Up $\to$ $Y$-Up conversion.

## [0.1.0-alpha] - 2026-07-29

### Added
- **Core Subsystem Architecture**: Technical design for universal asset interoperability in Godot 4 via GDExtension C++20.
- **Virtual File System (VFS) Specification**: Zero-copy RAM layer with on-the-fly LZSS/LZO decompressors.
