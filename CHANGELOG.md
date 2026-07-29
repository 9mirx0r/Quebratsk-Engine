# Changelog

All notable changes to **Quebratsk Engine** are documented in this file.

The format is based on [Keep a Changelog](https://keepachangelog.com/en/1.1.0/),
and this project adheres to [Semantic Versioning](https://semver.org/spec/v2.0.0.html).

---

## [Unreleased]

### Added
- **5 Advanced Quality & Performance Subsystems (Roadmap Expansion)**:
  - V-HACD 4.0 Convex Hull Physics & BSP Entity Trigger Exporter specification.
  - Automated Lightmap UV2 2D Bin-Packing & Specialized Shader Bridge for GoldSrc/Source.
  - Async Multi-Threaded Ingestion Pipeline (`std::jthread`) & SIMD vectorization for coordinate transforms.
  - Native GLTF / OBJ Exporter & Audio Bank VFS Decoder (`.wav`, `.ogg`).
  - Godot Editor Dock Plugin & In-Memory Texture/Material Cache Manager.
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
