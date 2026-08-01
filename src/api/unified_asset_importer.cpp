#include "unified_asset_importer.h"

#include "../parsers/goldsrc/bsp30_parser.h"
#include "../parsers/goldsrc/mdl10_parser.h"
#include "../parsers/source1/mdl_source_parser.h"
#include "../parsers/source1/vmt_parser.h"
#include "../parsers/rv_enfusion/p3d_mlod_parser.h"
#include "../parsers/rv_enfusion/wrp_parser.h"
#include "../converters/animation_converter.h"
#include "../converters/texture_loader.h"

#include <godot_cpp/classes/animation_library.hpp>
#include <godot_cpp/classes/capsule_shape3d.hpp>
#include <godot_cpp/classes/character_body3d.hpp>
#include <godot_cpp/classes/collision_shape3d.hpp>
#include <godot_cpp/classes/concave_polygon_shape3d.hpp>
#include <godot_cpp/classes/static_body3d.hpp>
#include <godot_cpp/classes/animation_player.hpp>
#include <godot_cpp/classes/mesh_instance3d.hpp>
#include <godot_cpp/core/class_db.hpp>
#include <godot_cpp/core/memory.hpp>
#include <godot_cpp/variant/packed_string_array.hpp>
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
    ClassDB::bind_method(D_METHOD("load_model", "vfs_uri", "pose_name", "animations"),
                         &UnifiedAssetImporter::load_model,
                         DEFVAL(String()), DEFVAL(PackedStringArray()));
    ClassDB::bind_method(D_METHOD("load_map", "vfs_uri"), &UnifiedAssetImporter::load_map);
    ClassDB::bind_method(D_METHOD("load_character", "vfs_uri", "pose_name", "animations"),
                         &UnifiedAssetImporter::load_character,
                         DEFVAL(String()), DEFVAL(PackedStringArray()));
    ClassDB::bind_method(D_METHOD("list_poses", "vfs_uri"), &UnifiedAssetImporter::list_poses);
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
                                                  const std::vector<std::string>& animate) {
    ParsedAssetIR out;
    const std::span<const std::byte> data(bundle.primary);
    if (data.empty()) {
        return out;
    }

    if (lowercase_uri.ends_with(".mdl")) {
        // GoldSrc and Source share the "IDST" magic, so route on the version field.
        // GoldSrc is self-contained; Source needs its .vvd and .vtx companions.
        if (auto gs_res = parsers::goldsrc::MDL10Parser::parse(
                data, std::span<const std::byte>(bundle.textures));
            gs_res.has_value()) {
            out.mesh = std::move(gs_res->mesh_data);
            out.skeleton = std::move(gs_res->skeleton_data);
            return out;
        }

        parsers::source1::SourceModelBundle src_bundle{
            data,
            std::span<const std::byte>(bundle.vertices),
            std::span<const std::byte>(bundle.indices),
            std::span<const std::byte>(bundle.animations),
            animate,
            {},
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
    loader.set_search_paths(parsed.mesh.material_search_paths);
    return converters::MeshConverter::convert(parsed.mesh, &loader);
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

Node3D* UnifiedAssetImporter::load_model(const String& vfs_uri, const String& pose_name,
                                         const PackedStringArray& animations) {
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
    ParsedAssetIR parsed = parse_asset_ir(bundle, uri_lower, /*pose_names_only=*/false, animate);

    return build_model_node(parsed, pose_name);
}

Node3D* UnifiedAssetImporter::build_model_node(const ParsedAssetIR& parsed,
                                               const String& pose_name) {
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
        if (converters::SkeletonConverter::apply_pose(skeleton, pose)) {
            UtilityFunctions::print("[QuebratskImporter] Pose '", String(pose.name.c_str()),
                                    "' applied (", static_cast<int64_t>(parsed.skeleton.poses.size()),
                                    " available)");
        }
    }

    attach_animations(skeleton, parsed);

    if (StaticBody3D* body = capsule_body(mesh->get_aabb()); body != nullptr) {
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
                                             const PackedStringArray& animations) {
    Node3D* model = load_model(vfs_uri, pose_name, animations);
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

Node3D* UnifiedAssetImporter::load_map(const String& vfs_uri) {
    Ref<ArrayMesh> mesh = load_mesh(vfs_uri);
    if (mesh.is_null()) return nullptr;

    MeshInstance3D* instance = memnew(MeshInstance3D);
    instance->set_mesh(mesh);
    String node_name = vfs_uri.get_file().get_basename().validate_node_name();
    if (node_name.is_empty()) node_name = "Map";
    instance->set_name(node_name);

    if (StaticBody3D* body = trimesh_body(mesh); body != nullptr) {
        instance->add_child(body);
    } else {
        UtilityFunctions::printerr("[QuebratskImporter] Map has no collidable geometry: ",
                                   vfs_uri);
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

    UtilityFunctions::print("[QuebratskImporter] ", added, " animation(s) attached");
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
