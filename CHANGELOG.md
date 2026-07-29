# Changelog

All notable changes to **Quebratsk Engine** are documented in this file.

The format is based on [Keep a Changelog](https://keepachangelog.com/en/1.1.0/),
and this project adheres to [Semantic Versioning](https://semver.org/spec/v2.0.0.html).

---

## [Unreleased]

### Added
- **Source Engine 1 Parsers (Phase 3b)**:
  - `GMAParser`: Garry's Mod Addon (`.gma`) container indexer and binary stream reader in `src/parsers/source1/gma_parser.h/.cpp`.
  - `VTFParser`: Valve Texture Format (`.vtf` v7.0-7.5) image header and payload decoder in `src/parsers/source1/vtf_parser.h/.cpp`.
  - `VMTParser`: Valve Material Type (`.vmt`) KeyValues material parser in `src/parsers/source1/vmt_parser.h/.cpp`.
  - `SourceMDLParser`: Source 1 StudioMDL (`.mdl` v44-49) skeleton and mesh parser into `IRSkeletonData` and `IRMeshData` in `src/parsers/source1/mdl_source_parser.h/.cpp`.
- **Real Virtuality / Enfusion Parsers (Phase 3c)**:
  - `PBOParser`: Bohemia Interactive `.pbo` archive directory indexer and CPRS LZSS block reader in `src/parsers/rv_enfusion/pbo_parser.h/.cpp`.
  - `PAADecoder`: Bohemia `.paa` / `.pac` texture mipmap decoder into RGBA8 pixel streams in `src/parsers/rv_enfusion/paa_decoder.h/.cpp`.
  - `P3DMLODParser`: Bohemia `.p3d` (MLOD/ODOL) 3D model parser into `IRMeshData` and `IRSkeletonData` in `src/parsers/rv_enfusion/p3d_mlod_parser.h/.cpp`.
  - `WRPParser`: Bohemia `.wrp` (OPRW/8WVR) terrain map heightmap parser into `IRTerrainData` in `src/parsers/rv_enfusion/wrp_parser.h/.cpp`.
- **GoldSrc Engine Parsers (Phase 3a)**: `WAD3Parser`, `BSP30Parser`, `MDL10Parser`, `SPRParser`.
- **Intermediate Representation (IR) Layer (Phase 2)**: Complete C++20 IR headers (`IRMeshData`, `IRSkeletonData`, `IRMaterialData`, `IRAnimationData`, `IRCollisionData`, `IRTerrainData`).
- **Coordinate Conversion & Axis Remap Module (Phase 2)**: Vector, normal, quaternion, and matrix transforms for $Z$-Up $\to$ $Y$-Up conversion.

## [0.1.0-alpha] - 2026-07-29

### Added
- **Core Subsystem Architecture**: Technical design for universal asset interoperability in Godot 4 via GDExtension C++20.
- **Virtual File System (VFS) Specification**: Zero-copy RAM layer with on-the-fly LZSS/LZO decompressors.
