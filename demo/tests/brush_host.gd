extends Node3D

## What a game hands to the brush entities: where files come from and what reads them.
## The least a host has to be, for verify_brush_entities.gd.

var vfs: VFSManager
var importer: UnifiedAssetImporter
