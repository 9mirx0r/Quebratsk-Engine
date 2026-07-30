extends Node3D

var vfs: VFSManager
var importer: UnifiedAssetImporter
var progress_tracker: TaskProgressTracker

func _ready() -> void:
	print("==================================================")
	print("   🚀 QUEBRATSK ENGINE DEMO - AUTOMATED TEST")
	print("==================================================")
	
	# 1. Initialize Core C++ Subsystems
	vfs = VFSManager.new()
	importer = UnifiedAssetImporter.new()
	importer.set_vfs(vfs)
	progress_tracker = TaskProgressTracker.new()
	
	# 2. Test Feature 3: Steam Library Auto-Detection
	var installed_games: Dictionary = SteamLibraryDetector.detect_installed_games()
	print("\n🎮 [Steam Library Auto-Detector] Installed Games Found:")
	if installed_games.is_empty():
		print("   (No games auto-detected in standard Steam paths)")
	else:
		for game_name in installed_games:
			print("   - ", game_name, " => ", installed_games[game_name])

	# 3. Test Feature 2: One-Click Performance Presets
	ImportPresets.apply_preset(ImportPresets.MAX_PERFORMANCE)
	print("\n⚡ [Import Presets] Applied 'MAX_PERFORMANCE' preset to ProjectSettings.")

	# 4. Test Feature 4: Task Progress Tracker
	progress_tracker.set_progress("Initializing VFS Memory Layers", 0.5)
	print("📊 [Progress Tracker] Current Status: ", progress_tracker.get_status_text(), " (", progress_tracker.get_progress() * 100, "%)")
	progress_tracker.set_progress("Ready", 1.0)

	# 5. Test Feature 5: Fuzzy Material Fixer
	var dummy_missing := "cstrike_wall_01"
	var best_match: String = FuzzyMaterialFixer.find_best_matching_texture(dummy_missing, vfs)
	print("🕷️ [Fuzzy Material Fixer] Best match for '", dummy_missing, "': ", (best_match if not best_match.is_empty() else "None found in empty VFS"))

	# 6. Test Feature 10: Obsidian Documentation Exporter
	var markdown_doc: String = ObsidianDocExporter.generate_vault_markdown_doc(vfs, self)
	print("\n📓 [Obsidian Doc Exporter] Markdown Preview Generated:\n")
	print(markdown_doc)

	# 7. Demo: How to load and spawn a 3D Model in scene
	print("\n🧊 [3D Mesh Loader Demo] Example usage:")
	print("   1. Mount package: vfs.mount_container(\"my_pack\", \"C:/path/to/archive.gma\")")
	print("   2. Load mesh: var mesh: ArrayMesh = importer.load_mesh(\"vfs://my_pack/models/model.mdl\")")
	print("   3. Add to scene: var mi = MeshInstance3D.new(); mi.mesh = mesh; add_child(mi)")
	
	# Connect Godot 4 native file drop signal
	get_window().files_dropped.connect(_on_files_dropped)
	
	print("\n==================================================")
	print("   ✅ ALL SUB-SYSTEMS INITIALIZED SUCCESSFULLY!")
	print("==================================================")

func _on_files_dropped(files: PackedStringArray) -> void:
	if files.size() > 0:
		var mounted: bool = VFSDropHandler.handle_dropped_files(files, vfs)
		if mounted:
			print("🖱️ [VFSDropHandler] Dropped archive files mounted successfully!")
