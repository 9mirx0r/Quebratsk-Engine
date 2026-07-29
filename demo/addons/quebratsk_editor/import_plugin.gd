@tool
extends EditorImportPlugin

func _get_importer_name() -> String:
    return "quebratsk.archive.importer"

func _get_visible_name() -> String:
    return "Quebratsk Game Archive"

func _get_recognized_extensions() -> PackedStringArray:
    return PackedStringArray(["wad", "gma", "pbo", "vpk", "pak", "bundle"])

func _get_save_extension() -> String:
    return "res"

func _get_resource_type() -> String:
    return "Resource"

func _get_preset_count() -> int:
    return 1

func _get_preset_name(preset_index: int) -> String:
    return "Default Archive Import"

func _get_import_options(path: String, preset_index: int) -> Array[Dictionary]:
    return [
        {"name": "auto_mount", "default_value": true}
    ]

func _get_option_visibility(path: String, option_name: StringName, options: Dictionary) -> bool:
    return true

func _import(source_file: String, save_path: String, options: Dictionary, platform_variants: Array[String], gen_files: Array[String]) -> Error:
    print("[QuebratskImportPlugin] Auto-importing archive: ", source_file)
    return OK
