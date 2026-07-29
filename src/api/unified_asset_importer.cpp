#include "unified_asset_importer.h"

#include "../parsers/goldsrc/bsp30_parser.h"
#include "../parsers/goldsrc/mdl10_parser.h"
#include "../parsers/source1/mdl_source_parser.h"
#include "../parsers/source1/vmt_parser.h"
#include "../parsers/rv_enfusion/p3d_mlod_parser.h"
#include "../parsers/rv_enfusion/wrp_parser.h"

#include <godot_cpp/core/class_db.hpp>
#include <godot_cpp/variant/utility_functions.hpp>

namespace quebratsk::api {

using namespace godot;

void UnifiedAssetImporter::_bind_methods() {
    ClassDB::bind_method(D_METHOD("set_vfs", "vfs"), &UnifiedAssetImporter::set_vfs);
    ClassDB::bind_method(D_METHOD("load_mesh", "vfs_uri"), &UnifiedAssetImporter::load_mesh);
    ClassDB::bind_method(D_METHOD("load_material", "vfs_uri"), &UnifiedAssetImporter::load_material);
    ClassDB::bind_method(D_METHOD("load_terrain", "vfs_uri"), &UnifiedAssetImporter::load_terrain);
}

void UnifiedAssetImporter::set_vfs(vfs::VFSManager* vfs) {
    m_vfs = vfs;
}

Ref<ArrayMesh> UnifiedAssetImporter::load_mesh(const String& vfs_uri) {
    if (!m_vfs) {
        UtilityFunctions::printerr("[QuebratskImporter] VFSManager not set!");
        return {};
    }

    std::string uri_std = vfs_uri.utf8().get_data();
    auto raw_span = m_vfs->get_raw_span(uri_std);
    if (!raw_span.has_value()) {
        // Attempt reading via PackedByteArray if raw span is null
        PackedByteArray bytes = m_vfs->read_file(vfs_uri);
        if (bytes.is_empty()) return {};
    }

    std::span<const std::byte> data = raw_span.value();

    // Auto-detect format by extension
    if (uri_std.ends_with(".mdl")) {
        // Try Source 1 MDL first
        auto src1_res = parsers::source1::SourceMDLParser::parse(data);
        if (src1_res.has_value()) {
            return converters::MeshConverter::convert(src1_res->mesh_data);
        }

        // Fallback to GoldSrc MDL 10
        auto gs_res = parsers::goldsrc::MDL10Parser::parse(data);
        if (gs_res.has_value()) {
            return converters::MeshConverter::convert(gs_res->mesh_data);
        }
    } else if (uri_std.ends_with(".bsp")) {
        auto bsp_res = parsers::goldsrc::BSP30Parser::parse(data);
        if (bsp_res.has_value()) {
            return converters::MeshConverter::convert(bsp_res->mesh_data);
        }
    } else if (uri_std.ends_with(".p3d")) {
        auto p3d_res = parsers::rv_enfusion::P3DMLODParser::parse(data);
        if (p3d_res.has_value()) {
            return converters::MeshConverter::convert(p3d_res->mesh_data);
        }
    }

    UtilityFunctions::printerr("[QuebratskImporter] Failed to import mesh from: ", vfs_uri);
    return {};
}

Ref<StandardMaterial3D> UnifiedAssetImporter::load_material(const String& vfs_uri) {
    if (!m_vfs) return {};

    std::string uri_std = vfs_uri.utf8().get_data();
    auto raw_span = m_vfs->get_raw_span(uri_std);
    if (!raw_span.has_value()) return {};

    if (uri_std.ends_with(".vmt")) {
        auto vmt_res = parsers::source1::VMTParser::parse(raw_span.value());
        if (vmt_res.has_value()) {
            return converters::MaterialConverter::convert(vmt_res.value());
        }
    }

    return {};
}

Ref<HeightMapShape3D> UnifiedAssetImporter::load_terrain(const String& vfs_uri) {
    if (!m_vfs) return {};

    std::string uri_std = vfs_uri.utf8().get_data();
    auto raw_span = m_vfs->get_raw_span(uri_std);
    if (!raw_span.has_value()) return {};

    if (uri_std.ends_with(".wrp")) {
        auto wrp_res = parsers::rv_enfusion::WRPParser::parse(raw_span.value());
        if (wrp_res.has_value()) {
            return converters::TerrainConverter::convert(wrp_res.value());
        }
    }

    return {};
}

} // namespace quebratsk::api
