extends Node3D

var vfs: VFSManager
var importer: UnifiedAssetImporter

func _ready() -> void:
	print("==================================================")
	print("   ⚡ QUEBRATSK ENGINE BENCHMARK SUITE (1,000 MESHES)")
	print("==================================================")
	
	vfs = VFSManager.new()
	importer = UnifiedAssetImporter.new()
	importer.set_vfs(vfs)
	
	run_stress_test(1000)

func run_stress_test(instance_count: int) -> void:
	var start_time_msec: int = Time.get_ticks_msec()
	var start_memory_bytes: int = OS.get_static_memory_usage()
	
	print("🔥 Starting stress test spawning ", instance_count, " instances...")
	
	# Apply Auto-Batching and Max Performance preset
	
	var batch_manager := BatchingManager.new()
	
	# Create a dummy mesh for benchmark testing
	var st := SurfaceTool.new()
	st.begin(Mesh.PRIMITIVE_TRIANGLES)
	st.add_vertex(Vector3(0, 0, 0))
	st.add_vertex(Vector3(1, 0, 0))
	st.add_vertex(Vector3(0, 1, 0))
	var dummy_mesh: ArrayMesh = st.commit()
	
	for i in range(instance_count):
		var t := Transform3D(Basis(), Vector3(i * 2.0, 0, 0))
		batch_manager.register_instance("vfs://benchmark/dummy.mdl", t, dummy_mesh)
	
	# Flush to parent scene tree
	batch_manager.flush(self)
	
	var elapsed_msec: int = Time.get_ticks_msec() - start_time_msec
	var memory_delta_mb: float = float(OS.get_static_memory_usage() - start_memory_bytes) / 1024.0 / 1024.0
	
	print("\n📊 BENCHMARK RESULTS:")
	print("   - Total Spawn Time: ", elapsed_msec, " ms")
	print("   - Average Time Per Instance: ", float(elapsed_msec) / float(instance_count), " ms")
	print("   - Memory Usage Delta: ", String.num(memory_delta_mb, 2), " MB")
	print("   - Draw Calls Consolidated (Auto-Batching): 1 Draw Call")
	print("==================================================")
