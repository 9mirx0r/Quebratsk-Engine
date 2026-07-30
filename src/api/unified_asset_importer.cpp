#include "unified_asset_importer.h"

#include "../parsers/goldsrc/bsp30_parser.h"
#include "../parsers/goldsrc/mdl10_parser.h"
#include "../parsers/source1/mdl_source_parser.h"
#include "../parsers/source1/vmt_parser.h"
#include "../parsers/rv_enfusion/p3d_mlod_parser.h"
#include "../parsers/rv_enfusion/wrp_parser.h"
#include "../converters/texture_loader.h"

#include <godot_cpp/classes/mesh_instance3d.hpp>
#include <godot_cpp/core/class_db.hpp>
#include <godot_cpp/core/memory.hpp>
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
    ClassDB::bind_method(D_METHOD("load_model", "vfs_uri"), &UnifiedAssetImporter::load_model);
}

void UnifiedAssetImporter::set_vfs(vfs::VFSManager* vfs) {
    m_vfs = vfs;
}

std::vector<std::byte> UnifiedAssetImporter::read_asset_bytes(const String& vfs_uri) const {
    if (!m_vfs) return {};
    return m_vfs->read_owned(vfs_uri.utf8().get_data());
}

AssetBundleBytes UnifiedAssetImporter::read_asset_bundle(const String& vfs_uri) const {
    AssetBundleBytes bundle;
    if (!m_vfs) return bundle;

    const std::string uri = vfs_uri.utf8().get_data();
    bundle.primary = m_vfs->read_owned(uri);
    if (bundle.primary.empty()) return bundle;

    const std::string uri_lower = to_lower_ascii(uri);
    if (!uri_lower.ends_with(".mdl")) {
        return bundle; // no companions defined for this format
    }

    // Try the sibling next to the .mdl first, then fall back to a suffix search in case
    // the archive lays models out differently.
    const std::string stem = uri.substr(0, uri.size() - 4);
    const std::string stem_lower = uri_lower.substr(0, uri_lower.size() - 4);

    auto read_companion = [&](std::initializer_list<const char*> extensions) {
        for (const char* ext : extensions) {
            if (auto bytes = m_vfs->read_owned(stem + ext); !bytes.empty()) return bytes;

            // find_by_suffix matches on the indexed (lowercase) path.
            const size_t slash = stem_lower.find_last_of('/');
            const std::string base = slash == std::string::npos ? stem_lower
                                                                : stem_lower.substr(slash);
            if (std::string found = m_vfs->find_by_suffix(base + ext); !found.empty()) {
                if (auto bytes = m_vfs->read_owned(found); !bytes.empty()) return bytes;
            }
        }
        return std::vector<std::byte>{};
    };

    bundle.vertices = read_companion({".vvd"});
    // dx90 is the modern index set; the others are legacy fallbacks still found in
    // older addons.
    bundle.indices = read_companion({".dx90.vtx", ".vtx", ".dx80.vtx", ".sw.vtx"});

    return bundle;
}

ParsedAssetIR UnifiedAssetImporter::parse_asset_ir(const AssetBundleBytes& bundle,
                                                  const std::string& lowercase_uri) {
    ParsedAssetIR out;
    const std::span<const std::byte> data(bundle.primary);
    if (data.empty()) {
        return out;
    }

    if (lowercase_uri.ends_with(".mdl")) {
        // GoldSrc and Source share the "IDST" magic, so route on the version field.
        // GoldSrc is self-contained; Source needs its .vvd and .vtx companions.
        if (auto gs_res = parsers::goldsrc::MDL10Parser::parse(data); gs_res.has_value()) {
            out.mesh = std::move(gs_res->mesh_data);
            out.skeleton = std::move(gs_res->skeleton_data);
            return out;
        }

        const parsers::source1::SourceModelBundle src_bundle{
            data,
            std::span<const std::byte>(bundle.vertices),
            std::span<const std::byte>(bundle.indices),
        };
        if (auto src1_res = parsers::source1::SourceMDLParser::parse_bundle(src_bundle);
            src1_res.has_value()) {
            out.mesh = std::move(src1_res->mesh_data);
            out.skeleton = std::move(src1_res->skeleton_data);
            return out;
        }
    } else if (lowercase_uri.ends_with(".bsp")) {
        if (auto bsp_res = parsers::goldsrc::BSP30Parser::parse(data); bsp_res.has_value()) {
            out.mesh = std::move(bsp_res->mesh_data);
            return out;
        }
    } else if (lowercase_uri.ends_with(".p3d")) {
        if (auto p3d_res = parsers::rv_enfusion::P3DMLODParser::parse(data); p3d_res.has_value()) {
            out.mesh = std::move(p3d_res->mesh_data);
            out.skeleton = std::move(p3d_res->skeleton_data);
            return out;
        }
    }

    return out;
}

Ref<ArrayMesh> UnifiedAssetImporter::load_mesh(const String& vfs_uri) {
    if (!m_vfs) {
        UtilityFunctions::printerr("[QuebratskImporter] VFSManager not set!");
        return {};
    }

    const AssetBundleBytes bundle = read_asset_bundle(vfs_uri);
    if (bundle.empty()) {
        UtilityFunctions::printerr("[QuebratskImporter] Empty or unreadable asset: ", vfs_uri);
        return {};
    }

    const std::string uri_lower = to_lower_ascii(vfs_uri.utf8().get_data());
    ParsedAssetIR parsed = parse_asset_ir(bundle, uri_lower);

    if (parsed.mesh.surfaces.empty()) {
        UtilityFunctions::printerr("[QuebratskImporter] No mesh surfaces decoded from: ", vfs_uri);
        return {};
    }

    // Resolve each surface's texture against the mounted archives.
    converters::TextureLoader loader(m_vfs);
    return converters::MeshConverter::convert(parsed.mesh, &loader);
}

Node3D* UnifiedAssetImporter::load_model(const String& vfs_uri) {
    if (!m_vfs) {
        UtilityFunctions::printerr("[QuebratskImporter] VFSManager not set!");
        return nullptr;
    }

    const AssetBundleBytes bundle = read_asset_bundle(vfs_uri);
    if (bundle.empty()) {
        UtilityFunctions::printerr("[QuebratskImporter] Empty or unreadable asset: ", vfs_uri);
        return nullptr;
    }

    const std::string uri_lower = to_lower_ascii(vfs_uri.utf8().get_data());
    ParsedAssetIR parsed = parse_asset_ir(bundle, uri_lower);

    if (parsed.mesh.surfaces.empty()) {
        UtilityFunctions::printerr("[QuebratskImporter] No mesh surfaces decoded from: ", vfs_uri);
        return nullptr;
    }

    converters::TextureLoader loader(m_vfs);
    Ref<ArrayMesh> mesh = converters::MeshConverter::convert(parsed.mesh, &loader);
    if (mesh.is_null()) {
        return nullptr;
    }

    MeshInstance3D* mesh_instance = memnew(MeshInstance3D);
    mesh_instance->set_mesh(mesh);
    mesh_instance->set_name(String(parsed.mesh.name.c_str()));

    if (parsed.skeleton.bones.empty()) {
        return mesh_instance; // static geometry, nothing to bind
    }

    // MeshInstance3D defaults its skeleton_path to "..", so parenting it to the
    // Skeleton3D is all the wiring the skin needs.
    Skeleton3D* skeleton = converters::SkeletonConverter::convert(parsed.skeleton);
    skeleton->set_name(String((parsed.mesh.name + "_Skeleton").c_str()));
    skeleton->add_child(mesh_instance);

    if (Ref<Skin> skin = converters::SkeletonConverter::make_skin(parsed.skeleton); skin.is_valid()) {
        mesh_instance->set_skin(skin);
    }

    return skeleton;
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
