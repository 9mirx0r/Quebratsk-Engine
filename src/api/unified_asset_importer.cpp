#include "unified_asset_importer.h"

#include "../parsers/goldsrc/bsp30_parser.h"
#include "../parsers/goldsrc/mdl10_parser.h"
#include "../parsers/source1/mdl_source_parser.h"
#include "../parsers/source1/vmt_parser.h"
#include "../parsers/rv_enfusion/p3d_mlod_parser.h"
#include "../parsers/rv_enfusion/wrp_parser.h"
#include "../converters/texture_loader.h"

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
    ClassDB::bind_method(D_METHOD("load_texture", "texture_ref"), &UnifiedAssetImporter::load_texture);
}

void UnifiedAssetImporter::set_vfs(vfs::VFSManager* vfs) {
    m_vfs = vfs;
}

std::vector<std::byte> UnifiedAssetImporter::read_asset_bytes(const String& vfs_uri) const {
    if (!m_vfs) return {};
    return m_vfs->read_owned(vfs_uri.utf8().get_data());
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
            // Pass the loader so $basetexture / $bumpmap actually reach the material.
            converters::TextureLoader loader(m_vfs);
            return converters::MaterialConverter::convert(vmt_res.value(), &loader);
        }
    }

    return {};
}

Ref<Texture2D> UnifiedAssetImporter::load_texture(const String& texture_ref) {
    if (!m_vfs) {
        UtilityFunctions::printerr("[QuebratskImporter] VFSManager not set!");
        return {};
    }

    converters::TextureLoader loader(m_vfs);
    return loader.load(texture_ref.utf8().get_data());
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
