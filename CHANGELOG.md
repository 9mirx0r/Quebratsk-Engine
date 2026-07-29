# Changelog

All notable changes to **Quebratsk Engine** are documented in this file.

The format is based on [Keep a Changelog](https://keepachangelog.com/en/1.1.0/),
and this project adheres to [Semantic Versioning](https://semver.org/spec/v2.0.0.html).

---

## [Unreleased]

### Added
- **Native Godot 4 Converters (Phase 4)**:
  - `MeshConverter`: Converts Intermediate Representation `IRMeshData` directly to native Godot 4 `godot::ArrayMesh` in `src/converters/mesh_converter.h/.cpp`.
  - `SkeletonConverter`: Converts `IRSkeletonData` to native `godot::Skeleton3D` with parent bone indices and rest transforms in `src/converters/skeleton_converter.h/.cpp`.
  - `MaterialConverter`: Converts `IRMaterialData` to `godot::StandardMaterial3D` with metallic, roughness, and transparency settings in `src/converters/material_converter.h/.cpp`.
  - `AnimationConverter`: Converts `IRAnimationData` position/rotation tracks to native `godot::Animation` resource in `src/converters/animation_converter.h/.cpp`.
  - `CollisionConverter`: Converts `IRCollisionData` to `godot::ConvexPolygonShape3D` in `src/converters/collision_converter.h/.cpp`.
  - `TerrainConverter`: Converts `IRTerrainData` elevation maps to native `godot::HeightMapShape3D` in `src/converters/terrain_converter.h/.cpp`.
- **Source Engine 1 Parsers (Phase 3b)**: `GMAParser`, `VTFParser`, `VMTParser`, `SourceMDLParser`.
- **Real Virtuality / Enfusion Parsers (Phase 3c)**: `PBOParser`, `PAADecoder`, `P3DMLODParser`, `WRPParser`.
- **GoldSrc Engine Parsers (Phase 3a)**: `WAD3Parser`, `BSP30Parser`, `MDL10Parser`, `SPRParser`.

## [0.1.0-alpha] - 2026-07-29

### Added
- **Core Subsystem Architecture**: Technical design for universal asset interoperability in Godot 4 via GDExtension C++20.
- **Virtual File System (VFS) Specification**: Zero-copy RAM layer with on-the-fly LZSS/LZO decompressors.
