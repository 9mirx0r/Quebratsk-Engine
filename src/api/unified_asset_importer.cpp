#include "unified_asset_importer.h"

#include <cstdio>

#include "../parsers/goldsrc/bsp30_parser.h"
#include "../parsers/goldsrc/mdl10_parser.h"
#include "../parsers/goldsrc/spr_parser.h"
#include "../parsers/source1/mdl_source_parser.h"
#include "../parsers/source1/vmt_parser.h"
#include "../parsers/rv_enfusion/p3d_mlod_parser.h"
#include "../parsers/rv_enfusion/wrp_parser.h"
#include "../converters/animation_converter.h"
#include "../converters/sound_resolver.h"
#include "../converters/texture_loader.h"
#include "../core/math/axis_remap.h"

#include <godot_cpp/classes/animation_library.hpp>
#include <godot_cpp/classes/capsule_shape3d.hpp>
#include <godot_cpp/classes/character_body3d.hpp>
#include <godot_cpp/classes/collision_shape3d.hpp>
#include <godot_cpp/classes/concave_polygon_shape3d.hpp>
#include <godot_cpp/classes/convex_polygon_shape3d.hpp>
#include <godot_cpp/classes/static_body3d.hpp>
#include <godot_cpp/classes/array_mesh.hpp>
#include <godot_cpp/classes/geometry_instance3d.hpp>
#include <godot_cpp/classes/image.hpp>
#include <godot_cpp/classes/image_texture.hpp>
#include <godot_cpp/classes/shader.hpp>
#include <godot_cpp/classes/shader_material.hpp>
#include <godot_cpp/classes/animation_player.hpp>
#include <godot_cpp/classes/mesh_instance3d.hpp>
#include <godot_cpp/core/class_db.hpp>
#include <godot_cpp/core/memory.hpp>
#include <godot_cpp/variant/packed_color_array.hpp>
#include <godot_cpp/variant/packed_float32_array.hpp>
#include <godot_cpp/variant/packed_int32_array.hpp>
#include <godot_cpp/variant/packed_string_array.hpp>
#include <godot_cpp/variant/packed_vector3_array.hpp>
#include <godot_cpp/variant/utility_functions.hpp>

#include <algorithm>
#include <optional>
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

/// A GDScript { "scope": 1 } turned into what the parser wants, lowercased to match.
///
/// A value that is not a number is dropped rather than coerced: `{"scope": "yes"}` is a
/// caller mistake, and 0 is a real answer that would hide it.
std::map<std::string, int32_t> to_body_choices(const Dictionary& from) {
    std::map<std::string, int32_t> out;
    const Array keys = from.keys();
    for (int i = 0; i < keys.size(); ++i) {
        const Variant& key = keys[i];
        const Variant value = from[key];
        if (value.get_type() != Variant::INT && value.get_type() != Variant::FLOAT) continue;
        out[to_lower_ascii(String(key).utf8().get_data())] = static_cast<int32_t>(value);
    }
    return out;
}

/// Which brush models a level's entities ask to be drawn at an opacity of nothing.
///
/// An entity with any `rendermode` but the ordinary one is blended by `renderamt`, and at
/// zero (which is also what a missing `renderamt` means) the engine does not draw it at all
/// (the public Xash3D ref/gl/gl_rmain.c, R_AddEntity). It still blocks. The answer is
/// indexed by the number in the entity's `"model" "*N"`.
struct BrushEntityFacts {
    std::vector<bool> invisible;
    std::vector<Vector3> offsets;
};

/// What a level's entities say about the brush models they point at; see ParsedAssetIR for
/// the offsets.
BrushEntityFacts brush_entity_facts(const std::string& entities) {
    BrushEntityFacts facts;
    std::vector<bool>& out = facts.invisible;
    const auto value_of = [](const std::string& block, const char* key) -> std::string {
        const std::string quoted = std::string("\"") + key + "\"";
        const size_t at = block.find(quoted);
        if (at == std::string::npos) return {};
        const size_t open = block.find('"', at + quoted.size());
        if (open == std::string::npos) return {};
        const size_t close = block.find('"', open + 1);
        if (close == std::string::npos) return {};
        return block.substr(open + 1, close - open - 1);
    };
    size_t pos = 0;
    while (true) {
        const size_t open = entities.find('{', pos);
        if (open == std::string::npos) break;
        const size_t close = entities.find('}', open);
        if (close == std::string::npos) break;
        pos = close + 1;

        const std::string block = entities.substr(open, close - open);
        const std::string model = value_of(block, "model");
        if (model.size() < 2 || model[0] != '*') continue;
        const int index = std::atoi(model.c_str() + 1);
        if (index <= 0 || index > 65535) continue;
        if (const std::string origin = value_of(block, "origin"); !origin.empty()) {
            float x = 0.0f, y = 0.0f, z = 0.0f;
            if (std::sscanf(origin.c_str(), "%f %f %f", &x, &y, &z) == 3) {
                if (facts.offsets.size() <= static_cast<size_t>(index)) {
                    facts.offsets.resize(static_cast<size_t>(index) + 1);
                }
                facts.offsets[static_cast<size_t>(index)] = math::source_to_godot(Vector3(x, y, z));
            }
        }
        if (std::atoi(value_of(block, "rendermode").c_str()) == 0) continue;
        if (std::atof(value_of(block, "renderamt").c_str()) > 0.0) continue;
        if (out.size() <= static_cast<size_t>(index)) out.resize(static_cast<size_t>(index) + 1);
        out[static_cast<size_t>(index)] = true;
    }
    return facts;
}

/// A level's light as shaders: Source light is linear floats, GoldSrc light a picture.
converters::MapLighting map_lighting(const ParsedAssetIR& parsed) {
    if (parsed.source_light.is_valid()) {
        return converters::MapLighting::create_linear(
            parsed.source_light.width, parsed.source_light.height, parsed.source_light.rgb);
    }
    return converters::MapLighting::create(parsed.lightmap);
}

} // namespace

void UnifiedAssetImporter::_bind_methods() {
    ClassDB::bind_method(D_METHOD("set_vfs", "vfs"), &UnifiedAssetImporter::set_vfs);
    ClassDB::bind_method(D_METHOD("load_mesh", "vfs_uri"), &UnifiedAssetImporter::load_mesh);
    ClassDB::bind_method(D_METHOD("load_material", "vfs_uri"), &UnifiedAssetImporter::load_material);
    ClassDB::bind_method(D_METHOD("load_terrain", "vfs_uri"), &UnifiedAssetImporter::load_terrain);
    ClassDB::bind_method(D_METHOD("load_texture", "texture_ref"), &UnifiedAssetImporter::load_texture);
    ClassDB::bind_method(D_METHOD("load_sprite", "vfs_uri"), &UnifiedAssetImporter::load_sprite);
    ClassDB::bind_method(D_METHOD("load_model", "vfs_uri", "pose_name", "animations",
                                  "body_choices", "skin"),
                         &UnifiedAssetImporter::load_model,
                         DEFVAL(String()), DEFVAL(PackedStringArray()), DEFVAL(Dictionary()),
                         DEFVAL(0));
    ClassDB::bind_method(D_METHOD("load_map", "vfs_uri"), &UnifiedAssetImporter::load_map);
    ClassDB::bind_method(D_METHOD("load_map_entities", "vfs_uri"),
                         &UnifiedAssetImporter::load_map_entities);
    ClassDB::bind_method(D_METHOD("load_map_props", "vfs_uri"),
                         &UnifiedAssetImporter::load_map_props);
    ClassDB::bind_method(D_METHOD("load_map_ambient", "vfs_uri"),
                         &UnifiedAssetImporter::load_map_ambient);
    ClassDB::bind_method(D_METHOD("load_character", "vfs_uri", "pose_name", "animations",
                                  "body_choices", "skin"),
                         &UnifiedAssetImporter::load_character,
                         DEFVAL(String()), DEFVAL(PackedStringArray()), DEFVAL(Dictionary()),
                         DEFVAL(0));
    ClassDB::bind_method(D_METHOD("list_poses", "vfs_uri"), &UnifiedAssetImporter::list_poses);
    ClassDB::bind_method(D_METHOD("list_sounds", "vfs_uri", "sequence"),
                         &UnifiedAssetImporter::list_sounds, DEFVAL(String()));
    ClassDB::bind_method(D_METHOD("list_sound_events", "vfs_uri", "sequence"),
                         &UnifiedAssetImporter::list_sound_events, DEFVAL(String()));
    ClassDB::bind_method(D_METHOD("list_body_groups", "vfs_uri"),
                         &UnifiedAssetImporter::list_body_groups);
    ClassDB::bind_method(D_METHOD("list_attachments", "vfs_uri"),
                         &UnifiedAssetImporter::list_attachments);
    ClassDB::bind_method(D_METHOD("resolve_sound", "name", "origin_uri"),
                         &UnifiedAssetImporter::resolve_sound, DEFVAL(String()));
    ClassDB::bind_method(D_METHOD("get_last_error_code"), &UnifiedAssetImporter::get_last_error_code);
    ClassDB::bind_method(D_METHOD("get_last_missing_companions"),
                         &UnifiedAssetImporter::get_last_missing_companions);

    // Without these the codes are bare integers in GDScript and every caller re-invents
    // its own magic numbers.
    ClassDB::bind_integer_constant("UnifiedAssetImporter", "", "ERR_OK", ERR_OK);
    ClassDB::bind_integer_constant("UnifiedAssetImporter", "", "ERR_VFS_NOT_SET", ERR_VFS_NOT_SET);
    ClassDB::bind_integer_constant("UnifiedAssetImporter", "", "ERR_ASSET_UNREADABLE",
                                   ERR_ASSET_UNREADABLE);
    ClassDB::bind_integer_constant("UnifiedAssetImporter", "", "ERR_PARSE_FAILED", ERR_PARSE_FAILED);
}

void UnifiedAssetImporter::set_vfs(vfs::VFSManager* vfs) {
    m_vfs = vfs;
    // What it indexed belongs to the old mount set.
    m_sounds.reset();
}

std::vector<std::byte> UnifiedAssetImporter::read_asset_bytes(const String& vfs_uri) const {
    if (!m_vfs) return {};
    return m_vfs->read_owned(vfs_uri.utf8().get_data());
}

AssetBundleBytes UnifiedAssetImporter::read_asset_bundle(const String& vfs_uri,
                                                         bool with_geometry) const {
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

    // Probing for a companion that does not exist is normal — a GoldSrc model has no
    // .vvd, a Source model has no T.mdl — so check existence first rather than letting
    // read_owned() log a "file not found" error for each miss.
    auto read_companion = [&](std::initializer_list<const char*> extensions) {
        for (const char* ext : extensions) {
            const std::string direct = stem + ext;
            if (m_vfs->file_exists(String(direct.c_str()))) {
                if (auto bytes = m_vfs->read_owned(direct); !bytes.empty()) return bytes;
            }

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

    // Source companions. Skipped wholesale for pose-only callers: these two are almost
    // all of a model's bytes and neither contributes a single sequence.
    if (with_geometry) {
        bundle.vertices = read_companion({".vvd"});
        // dx90 is the modern index set; the others are legacy fallbacks still found in
        // older addons.
        bundle.indices = read_companion({".dx90.vtx", ".vtx", ".dx80.vtx", ".sw.vtx"});

        // Record what was wanted and not found, so a failed import can name the file
        // instead of saying "nothing could be decoded". A Source .mdl carries no vertex
        // data at all, and the usual cause of a failure here is that the user mounted the
        // archive holding the .mdl but not the one holding its geometry — which is an
        // actionable problem the moment they are told which file is absent.
        m_last_missing.clear();

        // GoldSrc v10 shares the "IDST" magic and needs neither companion, so the version
        // field is what distinguishes a model that genuinely wants them.
        bool is_source_mdl = false;
        if (bundle.primary.size() >= 8 &&
            std::memcmp(bundle.primary.data(), "IDST", 4) == 0) {
            int32_t version = 0;
            std::memcpy(&version, bundle.primary.data() + 4, sizeof(version));
            is_source_mdl = version >= parsers::source1::kSourceMdlMinVersion &&
                            version <= parsers::source1::kSourceMdlMaxVersion;
        }

        if (is_source_mdl) {
            const std::string base = String(stem.c_str()).get_file().utf8().get_data();
            if (bundle.vertices.empty()) m_last_missing.push_back(String((base + ".vvd").c_str()));
            if (bundle.indices.empty()) m_last_missing.push_back(String((base + ".dx90.vtx").c_str()));
        }
    }

    // GoldSrc texture companion. Much of the stock Counter-Strike 1.6 content declares
    // zero textures in the model and keeps them in "<name>T.mdl" next to it.
    bundle.textures = read_companion({"T.mdl", "t.mdl"});

    // Resolve a path the model states relative to the engine root ("models/foo.mdl"):
    // same archive first, then any mount that carries it.
    auto read_engine_path = [&](const std::string& rel) {
        std::string wanted = to_lower_ascii(rel);
        std::replace(wanted.begin(), wanted.end(), '\\', '/');

        const size_t root = uri.find('/', std::string("vfs://").size());
        if (root != std::string::npos) {
            const std::string candidate = uri.substr(0, root + 1) + wanted;
            if (m_vfs->file_exists(String(candidate.c_str()))) {
                if (auto bytes = m_vfs->read_owned(candidate); !bytes.empty()) return bytes;
            }
        }
        if (std::string found = m_vfs->find_by_suffix("/" + wanted); !found.empty()) {
            return m_vfs->read_owned(found);
        }
        return std::vector<std::byte>{};
    };

    // Animation blocks. studiomdl moves long sequences out of the .mdl entirely, so a
    // model can name dozens of stances it does not contain a single frame of. The header
    // gives the path; a sibling ".ani" is the fallback for models that leave it blank.
    auto read_anim_blocks = [&](std::span<const std::byte> mdl_bytes,
                                const std::string& mdl_stem) {
        const std::string named = parsers::source1::SourceMDLParser::anim_block_name(mdl_bytes);
        if (named.empty()) return std::vector<std::byte>{}; // everything is stored inline

        if (auto bytes = read_engine_path(named); !bytes.empty()) return bytes;
        if (!mdl_stem.empty()) {
            const std::string sibling = mdl_stem + ".ani";
            if (m_vfs->file_exists(String(sibling.c_str()))) {
                return m_vfs->read_owned(sibling);
            }
        }
        return std::vector<std::byte>{};
    };

    bundle.animations = read_anim_blocks(bundle.primary, stem);

    // GoldSrc sidecar animation files. The model says how many it spreads its sequences
    // over, and the names follow from its own: player.mdl, player01.mdl, player02.mdl. That
    // is how the engine builds them too, deliberately, because the path written inside the
    // file is stale the moment someone renames a model without recompiling it.
    if (const int32_t groups = parsers::goldsrc::MDL10Parser::sequence_group_count(bundle.primary);
        groups > 1) {
        for (int32_t g = 1; g < groups && g < 100; ++g) {
            char suffix[8];
            std::snprintf(suffix, sizeof(suffix), "%02d.mdl", g);
            std::vector<std::byte> bytes = read_companion({suffix});
            if (bytes.empty()) {
                UtilityFunctions::printerr("[QuebratskImporter] ", vfs_uri,
                                           " keeps sequences in a file that is not here: ",
                                           String((stem + suffix).c_str()));
            }
            bundle.sequence_groups.push_back(std::move(bytes));
        }
    }

    // Source animation models. Their paths live inside the .mdl header, so this is a
    // second pass: read the includemodel table, then fetch what it names. A GMod player
    // model ships only a "ragdoll" sequence of its own and borrows every real stance —
    // idle, walk, weapon-holding — from the shared model listed here.
    for (const auto& rel : parsers::source1::SourceMDLParser::list_include_models(bundle.primary)) {
        std::vector<std::byte> bytes = read_engine_path(rel);
        if (bytes.empty()) {
            UtilityFunctions::printerr("[QuebratskImporter] includemodel not found: ",
                                       String(rel.c_str()));
            continue;
        }

        IncludedModelBytes inc;
        inc.ani = read_anim_blocks(bytes, {});
        inc.mdl = std::move(bytes);
        bundle.include_models.push_back(std::move(inc));
    }

    return bundle;
}

ParsedAssetIR UnifiedAssetImporter::parse_asset_ir(const AssetBundleBytes& bundle,
                                                  const std::string& lowercase_uri,
                                                  bool pose_names_only,
                                                  const std::vector<std::string>& animate,
                                                  const std::map<std::string, int32_t>& body_choices,
                                                  int32_t skin) {
    ParsedAssetIR out;
    const std::span<const std::byte> data(bundle.primary);
    if (data.empty()) {
        return out;
    }

    if (lowercase_uri.ends_with(".mdl")) {
        // GoldSrc and Source share the "IDST" magic, so route on the version field.
        // GoldSrc is self-contained; Source needs its .vvd and .vtx companions.
        std::vector<std::span<const std::byte>> groups;
        groups.reserve(bundle.sequence_groups.size());
        for (const auto& group : bundle.sequence_groups) {
            groups.emplace_back(group);
        }

        if (auto gs_res = parsers::goldsrc::MDL10Parser::parse(
                data, std::span<const std::byte>(bundle.textures), animate, groups,
                body_choices, skin);
            gs_res.has_value()) {
            out.mesh = std::move(gs_res->mesh_data);
            out.skeleton = std::move(gs_res->skeleton_data);
            out.animations = std::move(gs_res->animations);
            return out;
        }

        parsers::source1::SourceModelBundle src_bundle{
            data,
            std::span<const std::byte>(bundle.vertices),
            std::span<const std::byte>(bundle.indices),
            std::span<const std::byte>(bundle.animations),
            animate,
            {},
            skin,
        };
        src_bundle.include_models.reserve(bundle.include_models.size());
        for (const auto& inc : bundle.include_models) {
            src_bundle.include_models.push_back({std::span<const std::byte>(inc.mdl),
                                                std::span<const std::byte>(inc.ani)});
        }
        const auto detail = pose_names_only ? parsers::source1::PoseDetail::NamesOnly
                                           : parsers::source1::PoseDetail::Full;
        if (auto src1_res = parsers::source1::SourceMDLParser::parse_bundle(src_bundle, detail);
            src1_res.has_value()) {
            out.mesh = std::move(src1_res->mesh_data);
            out.skeleton = std::move(src1_res->skeleton_data);
            out.animations = std::move(src1_res->animations);
            return out;
        }
    } else if (lowercase_uri.ends_with(".bsp")) {
        // Source and GoldSrc share the extension and nothing else: a Source level opens with
        // "VBSP", a GoldSrc one with its version number.
        if (parsers::source1::VBSPParser::is_vbsp(data)) {
            auto vbsp_res = parsers::source1::VBSPParser::parse(data);
            if (!vbsp_res.has_value() || vbsp_res->brush_models.empty()) return out;
            out.mesh = std::move(vbsp_res->brush_models[0].mesh);
            // A level names its materials from the materials folder down.
            out.mesh.material_search_paths = {"materials/"};
            // [0] is the world, which is `mesh` above. No solid blocks are read yet, so
            // everything collides by its drawn triangles.
            out.brush_meshes.emplace_back();
            for (size_t i = 1; i < vbsp_res->brush_models.size(); ++i) {
                out.brush_meshes.push_back(std::move(vbsp_res->brush_models[i].mesh));
                out.brush_meshes.back().material_search_paths = {"materials/"};
            }
            BrushEntityFacts facts = brush_entity_facts(vbsp_res->entity_string);
            out.brush_invisible = std::move(facts.invisible);
            out.brush_offsets = std::move(facts.offsets);
            out.source_light = std::move(vbsp_res->lightmap);
            // The sky is not set here: a Source sky is six materials under skybox/, which the
            // GoldSrc sky box below does not look for. The world entity's "skyname" says which.
            return out;
        }
        if (auto bsp_res = parsers::goldsrc::BSP30Parser::parse(data); bsp_res.has_value()) {
            out.mesh = std::move(bsp_res->mesh_data);
            for (auto& model : bsp_res->brush_models) {
                out.brush_meshes.push_back(std::move(model.mesh));
                out.brush_solids.push_back(std::move(model.solids));
                out.brush_player_solids.push_back(std::move(model.player_solids));
            }
            out.lightmap = std::move(bsp_res->lightmap);
            BrushEntityFacts facts = brush_entity_facts(bsp_res->entity_string);
            out.brush_invisible = std::move(facts.invisible);
            out.brush_offsets = std::move(facts.offsets);
            if (bsp_res->sky_faces > 0) {
                // The first entity is the world, and `skyname` is one of its keys. A map
                // that does not say gets the engine's own default, `desert`.
                out.sky_name = "desert";
                const std::string& text = bsp_res->entity_string;
                const size_t world_end = text.find('}');
                const size_t key = text.find("\"skyname\"");
                if (key != std::string::npos && key < world_end) {
                    const size_t open = text.find('"', key + 9);
                    const size_t close =
                        open == std::string::npos ? open : text.find('"', open + 1);
                    if (close != std::string::npos && close > open + 1) {
                        out.sky_name = to_lower_ascii(text.substr(open + 1, close - open - 1));
                    }
                }
            }
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
        m_last_error_code = 1; // File/VFS Not Set
        UtilityFunctions::printerr("[QuebratskImporter] VFSManager not set!");
        return {};
    }

    const AssetBundleBytes bundle = read_asset_bundle(vfs_uri);
    if (bundle.empty()) {
        m_last_error_code = 2; // Asset Unreadable
        UtilityFunctions::printerr("[QuebratskImporter] Empty or unreadable asset: ", vfs_uri);
        return {};
    }

    const std::string uri_lower = to_lower_ascii(vfs_uri.utf8().get_data());
    ParsedAssetIR parsed = parse_asset_ir(bundle, uri_lower);

    if (parsed.mesh.surfaces.empty()) {
        m_last_error_code = 3; // Parse Failed / No Surfaces
        UtilityFunctions::printerr("[QuebratskImporter] No mesh surfaces decoded from: ", vfs_uri);
        return {};
    }

    m_last_error_code = 0; // OK
    converters::TextureLoader loader(m_vfs);
    loader.set_origin(vfs_uri.utf8().get_data());
    loader.set_search_paths(parsed.mesh.material_search_paths);
    const converters::MapLighting lighting = map_lighting(parsed);
    return converters::MeshConverter::convert(parsed.mesh, &loader, &lighting);
}

PackedStringArray UnifiedAssetImporter::list_poses(const String& vfs_uri) {
    PackedStringArray pose_names;
    if (!m_vfs) {
        m_last_error_code = ERR_VFS_NOT_SET;
        return pose_names;
    }

    const AssetBundleBytes bundle = read_asset_bundle(vfs_uri, /*with_geometry=*/false);
    if (bundle.empty()) {
        m_last_error_code = ERR_ASSET_UNREADABLE;
        return pose_names;
    }

    const std::string uri_lower = to_lower_ascii(vfs_uri.utf8().get_data());
    const ParsedAssetIR ir = parse_asset_ir(bundle, uri_lower, /*pose_names_only=*/true);

    for (const auto& pose : ir.skeleton.poses) {
        if (!pose.name.empty()) {
            pose_names.append(String(pose.name.c_str()));
        }
    }

    m_last_error_code = ERR_OK;
    return pose_names;
}

PackedStringArray UnifiedAssetImporter::list_sounds(const String& vfs_uri,
                                                    const String& sequence) {
    PackedStringArray out;
    if (!m_vfs) {
        m_last_error_code = ERR_VFS_NOT_SET;
        return out;
    }

    // Events live beside the sequences, so the pose-only read is enough: no vertex data is
    // touched to answer this.
    const AssetBundleBytes bundle = read_asset_bundle(vfs_uri, /*with_geometry=*/false);
    if (bundle.empty()) {
        m_last_error_code = ERR_ASSET_UNREADABLE;
        return out;
    }

    const std::string uri_lower = to_lower_ascii(vfs_uri.utf8().get_data());
    const ParsedAssetIR ir = parse_asset_ir(bundle, uri_lower, /*pose_names_only=*/true);

    // What the model names and what can be played are two different things: a Source event
    // says "Weapon_357.Single", which is an entry in a soundscript rather than a file. The
    // caller wants something it can read, so the names are followed here rather than left
    // for every caller to work out on its own.
    if (!m_sounds) m_sounds = std::make_unique<converters::SoundResolver>(m_vfs);
    converters::SoundResolver& resolver = *m_sounds;
    const size_t missing_before = resolver.missing().size();

    const std::string wanted = sequence.utf8().get_data();
    for (const auto& pose : ir.skeleton.poses) {
        if (!wanted.empty() && pose.name != wanted) continue;
        for (const auto& sound : pose.sounds) {
            for (const auto& uri : resolver.resolve(sound, vfs_uri.utf8().get_data())) {
                out.push_back(String(uri.c_str()));
            }
        }
    }

    // Only what this call could not find; the resolver keeps the whole history.
    for (size_t i = missing_before; i < resolver.missing().size(); ++i) {
        UtilityFunctions::printerr("[QuebratskImporter] ", vfs_uri, " asks for the sound \"",
                                   String(resolver.missing()[i].c_str()),
                                   "\", which is not on this machine");
    }

    m_last_error_code = ERR_OK;
    return out;
}

Array UnifiedAssetImporter::list_sound_events(const String& vfs_uri, const String& sequence) {
    Array out;
    if (!m_vfs) {
        m_last_error_code = ERR_VFS_NOT_SET;
        return out;
    }

    const AssetBundleBytes bundle = read_asset_bundle(vfs_uri, /*with_geometry=*/false);
    if (bundle.empty()) {
        m_last_error_code = ERR_ASSET_UNREADABLE;
        return out;
    }

    const std::string uri_lower = to_lower_ascii(vfs_uri.utf8().get_data());
    const ParsedAssetIR ir = parse_asset_ir(bundle, uri_lower, /*pose_names_only=*/true);

    if (!m_sounds) m_sounds = std::make_unique<converters::SoundResolver>(m_vfs);
    converters::SoundResolver& resolver = *m_sounds;

    const std::string wanted = sequence.utf8().get_data();
    for (const auto& pose : ir.skeleton.poses) {
        if (!wanted.empty() && pose.name != wanted) continue;

        for (size_t i = 0; i < pose.sounds.size(); ++i) {
            // A name the model wrote that resolves to nothing is left out rather than
            // published with no file, because a caller asked what it can play.
            for (const auto& uri : resolver.resolve(pose.sounds[i],
                                                     vfs_uri.utf8().get_data())) {
                Dictionary event;
                event["sequence"] = String(pose.name.c_str());
                event["sound"] = String(uri.c_str());
                event["time"] = i < pose.sound_times.size() ? pose.sound_times[i] : 0.0f;
                out.append(event);
                break;
            }
        }
    }

    m_last_error_code = ERR_OK;
    return out;
}

Array UnifiedAssetImporter::list_body_groups(const String& vfs_uri) {
    Array out;
    if (!m_vfs) {
        m_last_error_code = ERR_VFS_NOT_SET;
        return out;
    }
    // Body groups live in the bodypart table, which is read whether or not geometry is built,
    // so this costs a parse and not a mesh.
    const AssetBundleBytes bundle = read_asset_bundle(vfs_uri, /*with_geometry=*/false);
    if (bundle.empty()) {
        m_last_error_code = ERR_ASSET_UNREADABLE;
        return out;
    }

    const std::string uri_lower = to_lower_ascii(vfs_uri.utf8().get_data());
    const ParsedAssetIR ir = parse_asset_ir(bundle, uri_lower, /*pose_names_only=*/true);

    for (const auto& group : ir.mesh.body_groups) {
        Dictionary entry;
        entry["name"] = String(group.name.c_str());
        entry["chosen"] = group.chosen;
        PackedStringArray options;
        for (const auto& option : group.options) options.push_back(String(option.c_str()));
        entry["options"] = options;
        out.push_back(entry);
    }
    m_last_error_code = ERR_OK;
    return out;
}

Array UnifiedAssetImporter::list_attachments(const String& vfs_uri) {
    Array out;
    if (!m_vfs) {
        m_last_error_code = ERR_VFS_NOT_SET;
        return out;
    }
    const AssetBundleBytes bundle = read_asset_bundle(vfs_uri, /*with_geometry=*/false);
    if (bundle.empty()) {
        m_last_error_code = ERR_ASSET_UNREADABLE;
        return out;
    }

    const std::string uri_lower = to_lower_ascii(vfs_uri.utf8().get_data());
    const ParsedAssetIR ir = parse_asset_ir(bundle, uri_lower, /*pose_names_only=*/true);

    for (const auto& attachment : ir.mesh.attachments) {
        Dictionary entry;
        entry["name"] = String(attachment.name.c_str());
        entry["bone"] = attachment.bone_index;
        entry["position"] = attachment.position;
        entry["basis"] = attachment.basis;
        out.push_back(entry);
    }
    m_last_error_code = ERR_OK;
    return out;
}

PackedStringArray UnifiedAssetImporter::resolve_sound(const String& name,
                                                      const String& origin_uri) {
    PackedStringArray out;
    if (!m_vfs) {
        m_last_error_code = ERR_VFS_NOT_SET;
        return out;
    }
    if (!m_sounds) m_sounds = std::make_unique<converters::SoundResolver>(m_vfs);

    for (const auto& uri : m_sounds->resolve(name.utf8().get_data(),
                                             origin_uri.utf8().get_data())) {
        out.push_back(String(uri.c_str()));
    }
    m_last_error_code = ERR_OK;
    return out;
}

Node3D* UnifiedAssetImporter::load_model(const String& vfs_uri, const String& pose_name,
                                         const PackedStringArray& animations,
                                         const Dictionary& body_choices, int32_t skin) {
    if (!m_vfs) {
        m_last_error_code = ERR_VFS_NOT_SET;
        UtilityFunctions::printerr("[QuebratskImporter] VFSManager not set!");
        return nullptr;
    }

    const AssetBundleBytes bundle = read_asset_bundle(vfs_uri);
    if (bundle.empty()) {
        m_last_error_code = ERR_ASSET_UNREADABLE;
        UtilityFunctions::printerr("[QuebratskImporter] Empty or unreadable asset: ", vfs_uri);
        return nullptr;
    }

    std::vector<std::string> animate;
    animate.reserve(static_cast<size_t>(animations.size()));
    for (int i = 0; i < animations.size(); ++i) {
        animate.push_back(animations[i].utf8().get_data());
    }

    const std::string uri_lower = to_lower_ascii(vfs_uri.utf8().get_data());
    ParsedAssetIR parsed = parse_asset_ir(bundle, uri_lower, /*pose_names_only=*/false, animate,
                                         to_body_choices(body_choices), skin);

    return build_model_node(parsed, pose_name, vfs_uri);
}

Node3D* UnifiedAssetImporter::build_model_node(const ParsedAssetIR& parsed,
                                               const String& pose_name,
                                               const String& origin_uri) {
    if (!m_vfs) {
        m_last_error_code = ERR_VFS_NOT_SET;
        UtilityFunctions::printerr("[QuebratskImporter] VFSManager not set!");
        return nullptr;
    }

    if (parsed.mesh.surfaces.empty()) {
        m_last_error_code = ERR_PARSE_FAILED;
        UtilityFunctions::printerr("[QuebratskImporter] No mesh surfaces decoded from: ",
                                   String(parsed.mesh.name.c_str()));
        return nullptr;
    }

    converters::TextureLoader loader(m_vfs);
    loader.set_origin(origin_uri.utf8().get_data());
    loader.set_search_paths(parsed.mesh.material_search_paths);
    Ref<ArrayMesh> mesh = converters::MeshConverter::convert(parsed.mesh, &loader);
    if (mesh.is_null()) {
        m_last_error_code = ERR_PARSE_FAILED;
        return nullptr;
    }

    // Materials the model asked for and did not get. A model that renders flat and grey
    // looks identical to one that has no textures at all, and a user cannot tell the two
    // apart or do anything about either without being told which files were wanted.
    for (const auto& absent : loader.missing()) {
        m_last_missing.push_back(String(absent.c_str()));
    }
    if (!loader.missing().empty()) {
        UtilityFunctions::printerr("[QuebratskImporter] ",
                                   static_cast<int64_t>(loader.missing().size()),
                                   " material(s) not found for ", String(parsed.mesh.name.c_str()),
                                   "; first: ", String(loader.missing().front().c_str()));
    }

    // Model names are compile-time paths ("C:\SIERRA\Half-Life\models\x.mdl"), and
    // Godot rejects '.', ':', '/', '\' and '@' in node names.
    const String raw_name(parsed.mesh.name.c_str());
    String node_name = raw_name.get_file().get_basename().validate_node_name();
    if (node_name.is_empty()) {
        node_name = "Model";
    }

    MeshInstance3D* mesh_instance = memnew(MeshInstance3D);
    mesh_instance->set_mesh(mesh);
    mesh_instance->set_name(node_name);
    // How many sets of textures the model carries and which one it is wearing, so a caller
    // can tell a skin that was asked for and not there from one that was built.
    mesh_instance->set_meta("quebratsk_skins", parsed.mesh.skin_families);
    mesh_instance->set_meta("quebratsk_skin", parsed.mesh.skin);

    if (parsed.skeleton.bones.empty()) {
        // A prop with no bones never moves, so its own triangles are the honest collider.
        if (StaticBody3D* body = trimesh_body(mesh); body != nullptr) {
            mesh_instance->add_child(body);
        }
        m_last_error_code = ERR_OK;
        return mesh_instance; // static geometry, nothing to bind
    }

    // MeshInstance3D defaults its skeleton_path to "..", so parenting it to the
    // Skeleton3D is all the wiring the skin needs.
    Skeleton3D* skeleton = converters::SkeletonConverter::convert(parsed.skeleton);
    skeleton->set_name(node_name + "_Skeleton");
    skeleton->add_child(mesh_instance);

    if (Ref<Skin> skin = converters::SkeletonConverter::make_skin(parsed.skeleton); skin.is_valid()) {
        mesh_instance->set_skin(skin);
    }

    // What the sequence being shown says about where the body reaches, used for the collider
    // further down. Filled once which sequence that is has been decided.
    std::optional<AABB> shown_bounds;

    // Prefer a real stance over the bind pose. Source models ship in a T-pose, which is
    // a modelling artefact the game never shows; the sequences carry the poses players
    // actually see. Fall back to the first sequence when no idle-like one is named.
    if (!parsed.skeleton.poses.empty()) {
        // Publish the catalogue so a user can see what else this model can stand in
        // without having to open it in a model viewer first.
        PackedStringArray available;
        for (const auto& p : parsed.skeleton.poses) {
            available.push_back(String(p.name.c_str()));
        }
        skeleton->set_meta("quebratsk_poses", available);

        int32_t idx = -1;
        if (!pose_name.is_empty()) {
            const std::string wanted = pose_name.utf8().get_data();
            idx = parsed.skeleton.find_pose_exact(wanted);
            if (idx < 0) idx = parsed.skeleton.find_pose(wanted);
            if (idx < 0) {
                UtilityFunctions::printerr("[QuebratskImporter] pose not found: ", pose_name,
                                           " (", static_cast<int64_t>(available.size()),
                                           " available, see the \"quebratsk_poses\" metadata)");
            }
        }
        if (idx < 0) {
            for (const char* fallback : {"idle", "Idle", "ragdoll"}) {
                idx = parsed.skeleton.find_pose(fallback);
                if (idx >= 0) break;
            }
        }
        if (idx < 0) idx = 0;

        const ir::IRPose& pose = parsed.skeleton.poses[static_cast<size_t>(idx)];
        // The collider describes the body in the stance it is standing in, so this is the
        // sequence whose declaration counts.
        if (pose.has_bounds) shown_bounds = pose.bounds;
        if (converters::SkeletonConverter::apply_pose(skeleton, pose)) {
            UtilityFunctions::print_verbose("[QuebratskImporter] Pose '", String(pose.name.c_str()),
                                    "' applied (", static_cast<int64_t>(parsed.skeleton.poses.size()),
                                    " available)");
        }
    }

    attach_animations(skeleton, parsed);

    // Where the model says its own body is, when the model says. Not the mesh's bounding box.
    //
    // A GoldSrc entity's origin is the centre of its hull and not the soles of its feet: a
    // player is 72 units tall and stands with its origin 36 units up, and every sequence is
    // authored around that. The bind pose is authored standing on the origin instead, so the
    // two are 36 units — 0.91 m — apart, and a collider measured from the mesh describes a
    // body that is nowhere near the one being drawn.
    //
    // That gap is what put weapons in the sky and made characters hover: it read as several
    // unrelated faults and was one. An earlier attempt widened the mesh box to swallow the
    // posed bones, which produced a 2.67 m capsule for a 1.8 m person and moved the problem
    // rather than solving it.
    //
    // Each sequence declares its own bbmin and bbmax and the parser unions them.
    AABB bounds;
    if (shown_bounds.has_value()) {
        bounds = *shown_bounds;
    } else {
        // Nothing declared. The posed skeleton is the next best description, because whatever
        // else is true the mesh is skinned to those bones and follows them.
        bounds = AABB(skeleton->get_bone_global_pose(0).origin, Vector3());
        for (int b = 1; b < skeleton->get_bone_count(); ++b) {
            bounds = bounds.expand(skeleton->get_bone_global_pose(b).origin);
        }
        if (skeleton->get_bone_count() == 0) bounds = mesh->get_aabb();
    }

    // A Source prop is a model like any other and carries one bone to hang its mesh on. One
    // bone is one point: bounds with no size, and so no collider at all. Nothing with a single
    // bone bends, so it is a prop in everything but name and its own triangles describe it.
    StaticBody3D* body = nullptr;
    if (!shown_bounds.has_value() && skeleton->get_bone_count() <= 1) {
        body = trimesh_body(mesh);
    }
    if (body == nullptr) {
        if (bounds.size.y <= 0.0f) bounds = mesh->get_aabb();
        body = capsule_body(bounds);
    }
    if (body != nullptr) {
        skeleton->add_child(body);
    }

    m_last_error_code = ERR_OK;
    return skeleton;
}

StaticBody3D* UnifiedAssetImporter::trimesh_body(const Ref<Mesh>& mesh) {
    if (mesh.is_null()) return nullptr;

    Ref<ConcavePolygonShape3D> shape = mesh->create_trimesh_shape();
    if (shape.is_null() || shape->get_faces().is_empty()) return nullptr;

    StaticBody3D* body = memnew(StaticBody3D);
    body->set_name("StaticBody3D");

    CollisionShape3D* collider = memnew(CollisionShape3D);
    collider->set_name("CollisionShape3D");
    collider->set_shape(shape);
    body->add_child(collider);
    return body;
}

MeshInstance3D* UnifiedAssetImporter::sky_box(const std::string& name,
                                              const std::string& origin) const {
    if (!m_vfs || name.empty()) return nullptr;

    // The engine's own table: which way each picture faces, and which way is up on it.
    // Face n looks along the axis marked 3 (negative = the other way), its picture runs to
    // the right along the axis marked 1 and upwards along the one marked 2. Axes are the
    // map's: 1 = x, 2 = y, 3 = z.
    struct Side {
        const char* suffix;
        int axes[3];
    };
    static constexpr Side kSides[6] = {
        {"rt", {3, -1, 2}}, {"lf", {-3, 1, 2}}, {"bk", {1, 3, 2}},
        {"ft", {-1, -3, 2}}, {"up", {-2, -1, 3}}, {"dn", {2, -1, -3}},
    };

    Ref<Shader> shader;
    shader.instantiate();
    // The box turns with the level but never moves with it, so the eye is always at its
    // centre, and it is put at the very back of what the camera can see so that anything at
    // all is in front of it.
    shader->set_code(
        "shader_type spatial;\n"
        "render_mode unshaded, cull_disabled, depth_draw_never, skip_vertex_transform;\n"
        "uniform sampler2D albedo_texture : source_color, filter_linear, repeat_disable;\n"
        "void vertex() {\n"
        "    VERTEX = mat3(VIEW_MATRIX) * (mat3(MODEL_MATRIX) * VERTEX);\n"
        "    POSITION = PROJECTION_MATRIX * vec4(VERTEX, 1.0);\n"
        "    POSITION.z = 0.0;\n"
        "}\n"
        "void fragment() {\n"
        "    ALBEDO = texture(albedo_texture, UV).rgb;\n"
        "}\n");

    Ref<ArrayMesh> mesh;
    mesh.instantiate();
    for (const Side& side : kSides) {
        const std::string label = name + side.suffix;
        Ref<Image> image;
        for (const char* ext : {".tga", ".bmp"}) {
            const std::string uri = m_vfs->find_by_suffix("/gfx/env/" + label + ext, origin);
            if (uri.empty()) continue;
            const std::vector<std::byte> bytes = m_vfs->read_owned(uri);
            if (bytes.empty()) continue;
            PackedByteArray buffer;
            buffer.resize(static_cast<int64_t>(bytes.size()));
            std::memcpy(buffer.ptrw(), bytes.data(), bytes.size());
            Ref<Image> candidate;
            candidate.instantiate();
            const Error err = ext[1] == 't' ? candidate->load_tga_from_buffer(buffer)
                                            : candidate->load_bmp_from_buffer(buffer);
            if (err == Error::OK && !candidate->is_empty()) {
                image = candidate;
                break;
            }
        }
        // A sky with a side missing is not a sky; better none than five sixths of one.
        if (image.is_null()) return nullptr;

        PackedVector3Array vertices;
        PackedVector2Array uvs;
        static constexpr float kCorners[4][2] = {{-1, -1}, {1, -1}, {1, 1}, {-1, 1}};
        for (const auto& corner : kCorners) {
            const float b[3] = {corner[0], corner[1], 1.0f};
            float v[3];
            for (int j = 0; j < 3; ++j) {
                const int k = side.axes[j];
                v[j] = k < 0 ? -b[-k - 1] : b[k - 1];
            }
            // The map's axes to Godot's, as everywhere else; size does not matter here.
            vertices.push_back(Vector3(-v[1], v[2], -v[0]));
            uvs.push_back(Vector2((corner[0] + 1.0f) * 0.5f, 1.0f - (corner[1] + 1.0f) * 0.5f));
        }
        PackedInt32Array indices;
        for (const int i : {0, 1, 2, 0, 2, 3}) indices.push_back(i);

        Array arrays;
        arrays.resize(Mesh::ARRAY_MAX);
        arrays[Mesh::ARRAY_VERTEX] = vertices;
        arrays[Mesh::ARRAY_TEX_UV] = uvs;
        arrays[Mesh::ARRAY_INDEX] = indices;
        mesh->add_surface_from_arrays(Mesh::PRIMITIVE_TRIANGLES, arrays);

        Ref<ShaderMaterial> material;
        material.instantiate();
        material->set_shader(shader);
        material->set_shader_parameter("albedo_texture", ImageTexture::create_from_image(image));
        material->set_name(String(label.c_str()));
        mesh->surface_set_material(mesh->get_surface_count() - 1, material);
    }

    MeshInstance3D* sky = memnew(MeshInstance3D);
    sky->set_mesh(mesh);
    sky->set_name("Sky");
    sky->set_cast_shadows_setting(GeometryInstance3D::SHADOW_CASTING_SETTING_OFF);
    // It is drawn where the camera is, not where the node is, so it must never be judged
    // out of sight by where the node is.
    sky->set_extra_cull_margin(16384.0f);
    return sky;
}

StaticBody3D* UnifiedAssetImporter::solid_body(const std::vector<ir::IRConvexHull>& solids) {
    StaticBody3D* body = nullptr;
    int64_t count = 0;
    for (const auto& solid : solids) {
        if (solid.vertices.size() < 4) continue;
        PackedVector3Array points;
        points.resize(static_cast<int64_t>(solid.vertices.size()));
        std::copy(solid.vertices.begin(), solid.vertices.end(), points.ptrw());

        Ref<ConvexPolygonShape3D> shape;
        shape.instantiate();
        shape->set_points(points);

        if (body == nullptr) {
            body = memnew(StaticBody3D);
            body->set_name("StaticBody3D");
        }
        CollisionShape3D* collider = memnew(CollisionShape3D);
        // The first keeps the plain name every other collider here has, so a caller that
        // looks for "StaticBody3D/CollisionShape3D" finds one whatever built it.
        collider->set_name(count == 0 ? String("CollisionShape3D")
                                      : String("CollisionShape3D_") + String::num_int64(count));
        collider->set_shape(shape);
        body->add_child(collider);
        ++count;
    }
    return body;
}

StaticBody3D* UnifiedAssetImporter::capsule_body(const AABB& bounds) {
    const Vector3 size = bounds.size;
    if (size.y <= 0.0f) return nullptr;

    Ref<CapsuleShape3D> shape;
    shape.instantiate();
    // The narrower ground axis, not the wider one. A model's rest pose has its arms out, so
    // the wide axis is its wingspan: taking that gave a person a 1.1 m thick capsule, which
    // lands on a floor perfectly well and then cannot fit through anything, including the
    // gap it is standing in. Front to back is the measurement that describes a body.
    const float radius = std::max(std::min(size.x, size.z) * 0.5f, 0.05f);
    shape->set_radius(radius);
    // A Godot capsule's height includes its two hemispherical caps, so a shorter model than
    // twice the radius has no cylindrical part left and the height cannot go below that.
    shape->set_height(std::max(size.y, radius * 2.0f));

    StaticBody3D* body = memnew(StaticBody3D);
    body->set_name("StaticBody3D");

    CollisionShape3D* collider = memnew(CollisionShape3D);
    collider->set_name("CollisionShape3D");
    collider->set_shape(shape);
    // Centred on the mesh rather than on the origin: a character's origin is at its feet.
    collider->set_position(bounds.position + size * 0.5f);
    body->add_child(collider);
    return body;
}

Node3D* UnifiedAssetImporter::load_character(const String& vfs_uri, const String& pose_name,
                                             const PackedStringArray& animations,
                                             const Dictionary& body_choices, int32_t skin) {
    Node3D* model = load_model(vfs_uri, pose_name, animations, body_choices, skin);
    if (model == nullptr) return nullptr;

    Skeleton3D* skeleton = Object::cast_to<Skeleton3D>(model);
    if (skeleton == nullptr) {
        // No bones, so there is no character here. Handing back a CharacterBody3D wrapped
        // around a crate would be a promise the asset cannot keep.
        UtilityFunctions::printerr("[QuebratskImporter] Not a skinned model, so not a "
                                   "character: ", vfs_uri);
        return model;
    }

    // The capsule moves to the root. Leaving it on the skeleton would give the character a
    // static obstacle riding inside its own body, which is exactly the shape that makes a
    // mover collide with itself and refuse to go anywhere.
    StaticBody3D* rider = Object::cast_to<StaticBody3D>(skeleton->get_node_or_null("StaticBody3D"));
    Ref<Shape3D> shape;
    Vector3 shape_offset;
    if (rider != nullptr) {
        if (auto* collider = Object::cast_to<CollisionShape3D>(
                rider->get_node_or_null("CollisionShape3D"))) {
            shape = collider->get_shape();
            shape_offset = collider->get_position();
        }
        skeleton->remove_child(rider);
        memdelete(rider);
    }

    CharacterBody3D* body = memnew(CharacterBody3D);
    body->set_name(skeleton->get_name());
    skeleton->set_name("Skeleton3D");

    if (shape.is_valid()) {
        CollisionShape3D* collider = memnew(CollisionShape3D);
        collider->set_name("CollisionShape3D");
        collider->set_shape(shape);
        collider->set_position(shape_offset);
        body->add_child(collider);
    }
    body->add_child(skeleton);

    m_last_error_code = ERR_OK;
    return body;
}

Array UnifiedAssetImporter::load_map_entities(const String& vfs_uri) {
    Array out;
    if (!m_vfs) {
        m_last_error_code = ERR_VFS_NOT_SET;
        return out;
    }

    const std::vector<std::byte> bytes = read_asset_bytes(vfs_uri);
    if (bytes.empty()) {
        m_last_error_code = ERR_ASSET_UNREADABLE;
        return out;
    }

    // Only the entity text and each brush model's box are needed, from either kind of level.
    struct Box {
        godot::Vector3 mins;
        godot::Vector3 size;
        godot::Vector3 centre;
    };
    std::string text;
    std::vector<Box> boxes;
    if (parsers::source1::VBSPParser::is_vbsp(bytes)) {
        auto parsed = parsers::source1::VBSPParser::parse(bytes);
        if (!parsed.has_value()) {
            m_last_error_code = ERR_PARSE_FAILED;
            return out;
        }
        text = std::move(parsed->entity_string);
        for (const auto& model : parsed->brush_models) {
            boxes.push_back({model.mins, model.size(), model.centre()});
        }
    } else {
        auto parsed = parsers::goldsrc::BSP30Parser::parse(bytes);
        if (!parsed.has_value()) {
            m_last_error_code = ERR_PARSE_FAILED;
            return out;
        }
        text = std::move(parsed->entity_string);
        for (const auto& model : parsed->brush_models) {
            boxes.push_back({model.mins, model.size(), model.centre()});
        }
    }

    // The lump is a flat run of brace-delimited blocks of quoted key/value pairs:
    //
    //   { "classname" "info_player_start" "origin" "128 -64 32" "angles" "0 90 0" }
    //
    // Parsed by hand rather than with a KeyValues reader because the format has no nesting,
    // no comments and no escapes to speak of, and because a malformed lump has to end the
    // walk rather than throw: these files are twenty-five years old and some of them are
    // hand-edited.
    size_t pos = 0;
    while (true) {
        const size_t open = text.find('{', pos);
        if (open == std::string::npos) break;
        const size_t close = text.find('}', open);
        if (close == std::string::npos) break;

        Dictionary entity;
        size_t cursor = open + 1;
        while (cursor < close) {
            const size_t key_start = text.find('"', cursor);
            if (key_start == std::string::npos || key_start > close) break;
            const size_t key_end = text.find('"', key_start + 1);
            if (key_end == std::string::npos || key_end > close) break;

            const size_t value_start = text.find('"', key_end + 1);
            if (value_start == std::string::npos || value_start > close) break;
            const size_t value_end = text.find('"', value_start + 1);
            if (value_end == std::string::npos || value_end > close) break;

            const std::string key = text.substr(key_start + 1, key_end - key_start - 1);
            const std::string value = text.substr(value_start + 1, value_start == value_end
                                                                      ? 0
                                                                      : value_end - value_start - 1);
            entity[String(key.c_str())] = String(value.c_str());
            cursor = value_end + 1;
        }

        // "origin" is three numbers in the source engine's units and axes. Converting it
        // here is what lets a caller drop something at that point in the scene that
        // load_map() built, which is the entire use for this.
        if (entity.has("origin")) {
            const PackedStringArray parts = String(entity["origin"]).split(" ", false);
            if (parts.size() >= 3) {
                const godot::Vector3 raw(parts[0].to_float(), parts[1].to_float(),
                                         parts[2].to_float());
                entity["position"] = math::source_to_godot(raw);
            }
        }

        // A brush entity mostly has no origin: it has "model" "*71", and where it is IS that
        // model. The ones that do have one (a hinged door) are stored around their pivot,
        // and the origin is where the pivot goes, so the two add up.
        // Half a map is built this way - doors, lifts, triggers, water - and reading only
        // origins leaves every one of them placeless. On de_aztec that is both of the volumes
        // that set off the thunder, the one that holds the rain, and all five bodies of water.
        if (entity.has("model")) {
            const String model = entity["model"];
            if (model.begins_with("*")) {
                const int64_t index = model.substr(1).to_int();
                if (index >= 0 && static_cast<size_t>(index) < boxes.size()) {
                    const Box& box = boxes[static_cast<size_t>(index)];
                    // Published under the same key a point entity uses, so a caller does not
                    // have to know which kind it is holding to ask where it is.
                    const godot::Vector3 pivot =
                        entity.has("position") ? godot::Vector3(entity["position"]) : godot::Vector3();
                    entity["position"] = box.centre + pivot;
                    entity["bounds"] = AABB(box.mins + pivot, box.size);
                }
            }
        }

        if (!entity.is_empty()) out.append(entity);
        pos = close + 1;
    }

    m_last_error_code = ERR_OK;
    return out;
}

Array UnifiedAssetImporter::load_map_props(const String& vfs_uri) {
    Array out;
    if (!m_vfs) {
        m_last_error_code = ERR_VFS_NOT_SET;
        return out;
    }
    const std::vector<std::byte> bytes = read_asset_bytes(vfs_uri);
    if (bytes.empty()) {
        m_last_error_code = ERR_ASSET_UNREADABLE;
        return out;
    }
    if (!parsers::source1::VBSPParser::is_vbsp(bytes)) {
        // A GoldSrc level has no such list; its models are entities.
        m_last_error_code = ERR_OK;
        return out;
    }
    auto parsed = parsers::source1::VBSPParser::parse(bytes);
    if (!parsed.has_value()) {
        m_last_error_code = ERR_PARSE_FAILED;
        return out;
    }
    for (const auto& prop : parsed->props) {
        Dictionary entry;
        entry["model"] = String(prop.model.c_str());
        entry["skin"] = prop.skin;
        entry["solid"] = prop.solid;
        entry["transform"] = prop.transform;
        out.append(entry);
    }
    m_last_error_code = ERR_OK;
    return out;
}

Dictionary UnifiedAssetImporter::load_map_ambient(const String& vfs_uri) {
    Dictionary out;
    if (!m_vfs) {
        m_last_error_code = ERR_VFS_NOT_SET;
        return out;
    }
    const std::vector<std::byte> bytes = read_asset_bytes(vfs_uri);
    if (bytes.empty()) {
        m_last_error_code = ERR_ASSET_UNREADABLE;
        return out;
    }
    if (!parsers::source1::VBSPParser::is_vbsp(bytes)) {
        // A GoldSrc level keeps no such light; its floor's light is what lights people.
        m_last_error_code = ERR_OK;
        return out;
    }
    auto parsed = parsers::source1::VBSPParser::parse(bytes);
    if (!parsed.has_value()) {
        m_last_error_code = ERR_PARSE_FAILED;
        return out;
    }
    const auto& ambient = parsed->ambient;
    m_last_error_code = ERR_OK;
    if (!ambient.is_valid()) return out;

    PackedFloat32Array splits;
    PackedInt32Array children;
    splits.resize(static_cast<int64_t>(ambient.splits.size()) * 4);
    children.resize(static_cast<int64_t>(ambient.splits.size()) * 2);
    for (size_t i = 0; i < ambient.splits.size(); ++i) {
        const auto& split = ambient.splits[i];
        const auto at = static_cast<int64_t>(i);
        splits.set(at * 4, split.normal.x);
        splits.set(at * 4 + 1, split.normal.y);
        splits.set(at * 4 + 2, split.normal.z);
        splits.set(at * 4 + 3, split.dist);
        children.set(at * 2, split.front);
        children.set(at * 2 + 1, split.back);
    }
    PackedInt32Array leaves;
    leaves.resize(static_cast<int64_t>(ambient.leaves.size()) * 2);
    for (size_t i = 0; i < ambient.leaves.size(); ++i) {
        leaves.set(static_cast<int64_t>(i) * 2, static_cast<int32_t>(ambient.leaves[i].first));
        leaves.set(static_cast<int64_t>(i) * 2 + 1, static_cast<int32_t>(ambient.leaves[i].count));
    }
    PackedVector3Array spots;
    spots.resize(static_cast<int64_t>(ambient.spots.size()));
    for (size_t i = 0; i < ambient.spots.size(); ++i) spots.set(static_cast<int64_t>(i), ambient.spots[i]);
    PackedColorArray colours;
    colours.resize(static_cast<int64_t>(ambient.colours.size()));
    for (size_t i = 0; i < ambient.colours.size(); ++i) colours.set(static_cast<int64_t>(i), ambient.colours[i]);

    out["root"] = ambient.root;
    out["splits"] = splits;
    out["children"] = children;
    out["leaves"] = leaves;
    out["spots"] = spots;
    out["colours"] = colours;
    out["hdr"] = ambient.hdr;
    return out;
}

Node3D* UnifiedAssetImporter::load_map(const String& vfs_uri) {
    const AssetBundleBytes bundle = read_asset_bundle(vfs_uri);
    if (bundle.empty()) {
        m_last_error_code = ERR_ASSET_UNREADABLE;
        return nullptr;
    }
    const std::string uri_lower = to_lower_ascii(vfs_uri.utf8().get_data());
    ParsedAssetIR parsed = parse_asset_ir(bundle, uri_lower);
    if (parsed.mesh.surfaces.empty()) {
        m_last_error_code = ERR_PARSE_FAILED;
        return nullptr;
    }

    converters::TextureLoader loader(m_vfs);
    loader.set_origin(uri_lower);
    loader.set_search_paths(parsed.mesh.material_search_paths);

    // One light atlas and one pair of shaders for the whole level, brush entities included.
    const converters::MapLighting lighting = map_lighting(parsed);

    Ref<ArrayMesh> mesh = converters::MeshConverter::convert(parsed.mesh, &loader, &lighting);
    if (mesh.is_null()) return nullptr;

    MeshInstance3D* instance = memnew(MeshInstance3D);
    instance->set_mesh(mesh);
    String node_name = vfs_uri.get_file().get_basename().validate_node_name();
    if (node_name.is_empty()) node_name = "Map";
    instance->set_name(node_name);

    // The map's own solid blocks where it has them; its visible triangles only for a file
    // that carries no tree to read them from.
    const bool has_solids = !parsed.brush_solids.empty();
    StaticBody3D* world_body =
        has_solids ? solid_body(parsed.brush_solids[0]) : trimesh_body(mesh);
    if (world_body != nullptr) {
        instance->add_child(world_body);
    } else {
        UtilityFunctions::printerr("[QuebratskImporter] Map has no collidable geometry: ",
                                   vfs_uri);
    }

    // The level as the game's own player meets it, clip brushes and all. It is fattened by
    // half a player, so it sits on a layer of its own that nothing looks at unless asked:
    // the only thing that may be tested against it is the point at a player's middle.
    const auto player_clip = [&](size_t index) -> StaticBody3D* {
        if (index >= parsed.brush_player_solids.size()) return nullptr;
        StaticBody3D* body = solid_body(parsed.brush_player_solids[index]);
        if (body == nullptr) return nullptr;
        body->set_name("PlayerClip");
        body->set_collision_layer(kPlayerClipLayer);
        body->set_collision_mask(0);
        return body;
    };
    if (StaticBody3D* clip = player_clip(0); clip != nullptr) instance->add_child(clip);

    // Every brush entity as a node of its own, named for the model an entity points at, so a
    // caller holding `"model" "*71"` can find the thing and move it. A door that is welded
    // into the world is a wall for ever.
    //
    // Each keeps its own collider. Whether it should collide at all is the entity's business
    // — a func_illusionary is walked straight through, and cs_siege has seventy-three of them
    // — so that decision belongs to whoever reads the classnames, not here.
    int64_t built = 0;
    int64_t hidden = 0;
    for (size_t i = 1; i < parsed.brush_meshes.size(); ++i) {
        if (parsed.brush_meshes[i].surfaces.empty()) continue;

        // A texture the level carries inside itself is kept once, with the world, and a
        // brush entity's surfaces point into that same list. Lent to it for the conversion:
        // without this every door, crate and lift in a level that ships its own textures
        // came out blank, which on cs_siege was ninety-eight surfaces.
        std::swap(parsed.brush_meshes[i].embedded_textures, parsed.mesh.embedded_textures);
        Ref<ArrayMesh> brush =
            converters::MeshConverter::convert(parsed.brush_meshes[i], &loader, &lighting);
        std::swap(parsed.brush_meshes[i].embedded_textures, parsed.mesh.embedded_textures);
        if (brush.is_null()) continue;

        MeshInstance3D* node = memnew(MeshInstance3D);
        node->set_mesh(brush);
        node->set_name(String("brush_") + String::num_int64(static_cast<int64_t>(i)));
        // An unseen wall: there to be bumped into, so it keeps its collider below and is
        // only hidden, which also leaves it one click away for whoever wants to see it.
        // Stored around its pivot; the entity says where the pivot goes. The colliders are
        // children, so they go with it.
        if (i < parsed.brush_offsets.size()) node->set_position(parsed.brush_offsets[i]);
        if (i < parsed.brush_invisible.size() && parsed.brush_invisible[i]) {
            node->set_visible(false);
            ++hidden;
        }
        // A brush with nothing solid in it (water, mostly) gets no collider at all: its
        // triangles used to make a pool something to stand on.
        StaticBody3D* body = (has_solids && i < parsed.brush_solids.size())
                                 ? solid_body(parsed.brush_solids[i])
                                 : (has_solids ? nullptr : trimesh_body(brush));
        if (body != nullptr) node->add_child(body);
        if (StaticBody3D* clip = player_clip(i); clip != nullptr) node->add_child(clip);
        instance->add_child(node);
        ++built;
    }
    if (built > 0) {
        UtilityFunctions::print("[QuebratskImporter] ", built,
                                " brush entity/entities built as their own nodes, ", hidden,
                                " of them hidden as the level asks");
    }

    // What is seen through the level's sky faces. Its own node, so a game that brings its
    // own sky can simply delete it.
    if (!parsed.sky_name.empty()) {
        if (MeshInstance3D* sky = sky_box(parsed.sky_name, uri_lower); sky != nullptr) {
            instance->add_child(sky);
        } else {
            UtilityFunctions::print("[QuebratskImporter] Sky '", String(parsed.sky_name.c_str()),
                                    "' is not installed; the level has no sky");
        }
    }

    m_last_error_code = ERR_OK;
    return instance;
}

void UnifiedAssetImporter::attach_animations(Skeleton3D* skeleton,
                                             const ParsedAssetIR& parsed) {
    if (parsed.animations.empty()) return;

    Ref<AnimationLibrary> library;
    library.instantiate();

    int64_t added = 0;
    for (const auto& ir_anim : parsed.animations) {
        // The AnimationPlayer's root is the skeleton itself, so a bone track addresses it
        // as ":BoneName" — an empty node part means "the root node".
        Ref<Animation> anim = converters::AnimationConverter::convert(ir_anim, String(),
                                                                      skeleton);
        if (anim.is_null() || anim->get_track_count() == 0) continue;
        if (library->add_animation(String(ir_anim.name.c_str()), anim) == Error::OK) {
            ++added;
        }
    }
    if (added == 0) return;

    AnimationPlayer* player = memnew(AnimationPlayer);
    player->set_name("AnimationPlayer");
    skeleton->add_child(player);

    // Relative to the player, ".." is the Skeleton3D it hangs from. Leaving this at its
    // default would resolve tracks against whatever the model is later parented to.
    player->set_root_node(NodePath(".."));

    // The empty library name is what lets an animation be addressed by its own label,
    // player.play("walk_all_01"), rather than "library_name/walk_all_01".
    player->add_animation_library("", library);

    UtilityFunctions::print_verbose("[QuebratskImporter] ", added, " animation(s) attached");
}

Ref<StandardMaterial3D> UnifiedAssetImporter::load_material(const String& vfs_uri) {
    if (!m_vfs) return {};

    const std::vector<std::byte> data = read_asset_bytes(vfs_uri);
    if (data.empty()) return {};

    const std::string uri_lower = to_lower_ascii(vfs_uri.utf8().get_data());
    if (uri_lower.ends_with(".vmt")) {
        converters::TextureLoader loader(m_vfs);
        const std::string uri = vfs_uri.utf8().get_data();
        const parsers::source1::VMTParser::IncludeReader read_include =
            [&loader, &uri](std::string_view path) {
                return loader.read_material_include(path, uri);
            };
        if (auto vmt_res = parsers::source1::VMTParser::parse(data, read_include);
            vmt_res.has_value()) {
            // Pass the loader so $basetexture / $bumpmap actually reach the material.
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

Dictionary UnifiedAssetImporter::load_sprite(const String& vfs_uri) {
    Dictionary out;
    if (!m_vfs) return out;

    const std::vector<std::byte> data = read_asset_bytes(vfs_uri);
    if (data.empty()) return out;

    auto parsed = parsers::goldsrc::SPRParser::parse(data);
    if (!parsed.has_value()) return out;

    Array frames;
    for (const auto& frame : parsed->frames) {
        if (frame.width <= 0 || frame.height <= 0) continue;
        PackedByteArray pixels;
        pixels.resize(static_cast<int64_t>(frame.rgba8_pixels.size()));
        std::memcpy(pixels.ptrw(), frame.rgba8_pixels.data(), frame.rgba8_pixels.size());
        Ref<Image> image = Image::create_from_data(frame.width, frame.height, false,
                                                   Image::FORMAT_RGBA8, pixels);
        if (image.is_null()) continue;
        frames.push_back(ImageTexture::create_from_image(image));
    }
    if (frames.is_empty()) return out;

    out["frames"] = frames;
    out["width"] = parsed->max_width;
    out["height"] = parsed->max_height;
    out["type"] = parsed->type;
    out["format"] = parsed->tex_format;
    return out;
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
