# Changelog

All notable changes to **Quebratsk Engine** are documented in this file.

The format is based on [Keep a Changelog](https://keepachangelog.com/en/1.1.0/),
and this project adheres to [Semantic Versioning](https://semver.org/spec/v2.0.0.html).

---

## [Unreleased]

### Added
- **Source Engine 2 / Counter-Strike 2 Subsystem (Tier 1 Expansion)**:
  - `VPK2Parser` (`src/parsers/source2/vpk2_parser.h/.cpp`): Source 2 VPK v2 package indexer.
  - `VMDLParser` (`src/parsers/source2/vmdl_parser.h/.cpp`): Source 2 `.vmdl_c` compiled 3D model parser into `IRMeshData` & `IRSkeletonData`.
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
