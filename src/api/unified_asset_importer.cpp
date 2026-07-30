#include "unified_asset_importer.h"

#include "../parsers/goldsrc/bsp30_parser.h"
#include "../parsers/goldsrc/mdl10_parser.h"
#include "../parsers/source1/mdl_source_parser.h"
#include "../parsers/source1/vmt_parser.h"
#include "../parsers/rv_enfusion/p3d_mlod_parser.h"
#include "../parsers/rv_enfusion/wrp_parser.h"

#include <godot_cpp/core/class_db.hpp>
#include <godot_cpp/variant/utility_functions.hpp>

#include <algorithm>
#include <cctype>
#include <cstring>

namespace quebratsk::api {

using namespace godot;

namespace {

std::string to_lower_ascii(std::string_view str) {
    std::string result(str);
    std::transform(result.begin(), result.end(), result.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return result;
}

} // namespace

void UnifiedAssetImporter::_bind_methods() {
    ClassDB::bind_method(D_METHOD("set_vfs", "vfs"), &UnifiedAssetImporter::set_vfs);
    ClassDB::bind_method(D_METHOD("load_mesh", "vfs_uri"), &UnifiedAssetImporter::load_mesh);
    ClassDB::bind_method(D_METHOD("load_material", "vfs_uri"), &UnifiedAssetImporter::load_material);
    ClassDB::bind_method(D_METHOD("load_terrain", "vfs_uri"), &UnifiedAssetImporter::load_terrain);
}

void UnifiedAssetImporter::set_vfs(vfs::VFSManager* vfs) {
    m_vfs = vfs;
}

std::vector<std::byte> UnifiedAssetImporter::read_asset_bytes(const String& vfs_uri) const {
    std::vector<std::byte> owned;
    if (!m_vfs) {
        return owned;
    }

    // Fast path: uncompressed entries can be copied straight out of the mapping.
    // The span is only valid while the VFS index is untouched, so copy immediately
    // rather than handing it to a caller that may outlive the mount.
    const std::string uri_std = vfs_uri.utf8().get_data();
    if (auto raw_span = m_vfs->get_raw_span(uri_std); raw_span.has_value()) {
        owned.assign(raw_span->begin(), raw_span->end());
        return owned;
    }

    // Slow path: compressed entries (PBO "Cprs") must go through read_file().
    const PackedByteArray bytes = m_vfs->read_file(vfs_uri);
    if (bytes.is_empty()) {
        return owned;
    }
    owned.resize(static_cast<size_t>(bytes.size()));
    std::memcpy(owned.data(), bytes.ptr(), owned.size());
    return owned;
}

ir::IRMeshData UnifiedAssetImporter::parse_mesh_ir(std::span<const std::byte> data,
                                                  const std::string& lowercase_uri) {
    if (data.empty()) {
        return {};
    }

    if (lowercase_uri.ends_with(".mdl")) {
        // Try Source 1 MDL first, then fall back to GoldSrc MDL v10.
        if (auto src1_res = parsers::source1::SourceMDLParser::parse(data); src1_res.has_value()) {
            return std::move(src1_res->mesh_data);
        }
        if (auto gs_res = parsers::goldsrc::MDL10Parser::parse(data); gs_res.has_value()) {
            return std::move(gs_res->mesh_data);
        }
    } else if (lowercase_uri.ends_with(".bsp")) {
        if (auto bsp_res = parsers::goldsrc::BSP30Parser::parse(data); bsp_res.has_value()) {
            return std::move(bsp_res->mesh_data);
        }
    } else if (lowercase_uri.ends_with(".p3d")) {
        if (auto p3d_res = parsers::rv_enfusion::P3DMLODParser::parse(data); p3d_res.has_value()) {
            return std::move(p3d_res->mesh_data);
        }
    }

    return {};
}

Ref<ArrayMesh> UnifiedAssetImporter::load_mesh(const String& vfs_uri) {
    if (!m_vfs) {
        UtilityFunctions::printerr("[QuebratskImporter] VFSManager not set!");
        return {};
    }

    const std::vector<std::byte> data = read_asset_bytes(vfs_uri);
    if (data.empty()) {
        UtilityFunctions::printerr("[QuebratskImporter] Empty or unreadable asset: ", vfs_uri);
        return {};
    }

    const std::string uri_lower = to_lower_ascii(vfs_uri.utf8().get_data());
    ir::IRMeshData ir_mesh = parse_mesh_ir(data, uri_lower);

    if (ir_mesh.surfaces.empty()) {
        UtilityFunctions::printerr("[QuebratskImporter] No mesh surfaces decoded from: ", vfs_uri);
        return {};
    }

    return converters::MeshConverter::convert(ir_mesh);
}

Ref<StandardMaterial3D> UnifiedAssetImporter::load_material(const String& vfs_uri) {
    if (!m_vfs) return {};

    const std::vector<std::byte> data = read_asset_bytes(vfs_uri);
    if (data.empty()) return {};

    const std::string uri_lower = to_lower_ascii(vfs_uri.utf8().get_data());
    if (uri_lower.ends_with(".vmt")) {
        if (auto vmt_res = parsers::source1::VMTParser::parse(data); vmt_res.has_value()) {
            return converters::MaterialConverter::convert(vmt_res.value());
        }
    }

    return {};
}

Ref<HeightMapShape3D> UnifiedAssetImporter::load_terrain(const String& vfs_uri) {
    if (!m_vfs) return {};

    const std::vector<std::byte> data = read_asset_bytes(vfs_uri);
    if (data.empty()) return {};

    const std::string uri_lower = to_lower_ascii(vfs_uri.utf8().get_data());
    if (uri_lower.ends_with(".wrp")) {
        if (auto wrp_res = parsers::rv_enfusion::WRPParser::parse(data); wrp_res.has_value()) {
            return converters::TerrainConverter::convert(wrp_res.value());
        }
    }

    return {};
}

} // namespace quebratsk::api
