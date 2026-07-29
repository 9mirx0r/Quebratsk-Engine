# Changelog

All notable changes to **Quebratsk Engine** are documented in this file.

The format is based on [Keep a Changelog](https://keepachangelog.com/en/1.1.0/),
and this project adheres to [Semantic Versioning](https://semver.org/spec/v2.0.0.html).

---

## [0.1.0-alpha] - 2026-07-29

### Added
- **Core Subsystem Architecture**: Initial technical design for universal asset interoperability in Godot 4 via GDExtension C++20.
- **Virtual File System (VFS) Specification**: Memory-mapped I/O (`mmap`) zero-copy RAM layer with on-the-fly LZSS/LZO decompressors and URI path scheme (`vfs://`).
- **Intermediate Representation (IR)**: Decoupled C++ data layer defining unified schemas for meshes (`IRMeshData`), skeletons (`IRSkeletonData`), materials (`IRMaterialData`), animations, physics hulls, and heightmap terrains.
- **Coordinate & Spatial Normalization Pipeline**:
  - $Z$-Up to $Y$-Up axis remapping matrices.
  - Scale factor conversions ($1\text{ HU} = 0.0254\text{m}$).
  - Face winding order inversion (CW to CCW) to ensure correct back-face culling in Forward+/Mobile renderers.
- **Skeletal Retargeting Engine Design**: Bone name translation mapping and A-Pose/T-Pose rest angle normalization for `SkeletonProfileHumanoid`.
- **Material & Shader Converter Specification**: DXT5nm normal map Z-channel reconstruction ($Z = \sqrt{1 - X^2 - Y^2}$) and lightmap atlas UV2 packing.
- **Format Support Targets**: Detailed specs for GoldSrc (`.wad`, `.bsp`, `.mdl`), Source Engine 1 (`.gma`, `.bsp`, `.mdl`, `.vtf`, `.vmt`), and Real Virtuality / Enfusion (`.pbo`, `.p3d`, `.wrp`, `.paa`, `.rvmat`).
