#include "mdl10_parser.h"
#include "../../core/io/byte_reader.h"
#include "../../core/math/axis_remap.h"

#include <godot_cpp/variant/basis.hpp>
#include <godot_cpp/variant/transform3d.hpp>

#include <algorithm>
#include <cstring>
#include <string>
#include <unordered_map>
#include <vector>

namespace quebratsk::parsers::goldsrc {

using io::ByteReader;

namespace {

/// StudioMDL stores XYZ Euler angles applied X, then Y, then Z, matching Valve's
/// AngleQuaternion(). Godot's Basis/Quaternion Euler constructors use YXZ, so the
/// composition has to be written out explicitly.
godot::Basis studio_euler_to_basis(float rx, float ry, float rz) {
    const godot::Quaternion qx(godot::Vector3(1, 0, 0), rx);
    const godot::Quaternion qy(godot::Vector3(0, 1, 0), ry);
    const godot::Quaternion qz(godot::Vector3(0, 0, 1), rz);
    return godot::Basis(qz * qy * qx);
}

/// Rest transform of every bone in MODEL space, still in the source Z-up frame.
///
/// This is not optional bookkeeping: GoldSrc stores each vertex in the local space of
/// the bone it is attached to, so without walking the hierarchy the mesh comes out
/// scattered around the origin.
std::vector<godot::Transform3D> build_bone_rest_transforms(const StudioBone* bones, int32_t count) {
    std::vector<godot::Transform3D> world(static_cast<size_t>(count));

    for (int32_t i = 0; i < count; ++i) {
        const auto& b = bones[i];
        const godot::Transform3D local(
            studio_euler_to_basis(b.value[3], b.value[4], b.value[5]),
            godot::Vector3(b.value[0], b.value[1], b.value[2]));

        // StudioMDL orders bones parents-first; the bounds test also guards a corrupt
        // or cyclic hierarchy, which would otherwise read an uninitialised transform.
        if (b.parent >= 0 && b.parent < i) {
            world[static_cast<size_t>(i)] = world[static_cast<size_t>(b.parent)] * local;
        } else {
            world[static_cast<size_t>(i)] = local;
        }
    }
    return world;
}

/// One channel of one bone, sampled at a frame.
///
/// The samples are run-length encoded, so a track cannot be indexed: reaching frame 40 means
/// walking the runs until the one that contains it. A run is a {valid, total} header followed
/// by `valid` samples covering `total` frames, with the remainder repeating the last sample,
/// which is how a bone that holds still for two seconds costs four bytes.
///
/// An offset of zero means the channel never moves, and the bone's rest value stands in.
float sample_channel(const ByteReader& mdl, size_t anim_offset, const StudioAnim& anim,
                     int channel, int32_t frame, float base, float scale) {
    if (anim.offset[channel] == 0) return base;

    size_t at = anim_offset + anim.offset[channel];
    int32_t left = frame;

    auto run = mdl.read_at<StudioAnimValue>(at);
    if (!run.has_value()) return base;

    // Walk to the run holding this frame. A `total` of zero would consume no frames and
    // never advance, so it ends the walk; every other step moves `at` forward by at least
    // one value, which is what guarantees this terminates on a corrupt file.
    while (run->num.total <= left) {
        if (run->num.total == 0) return base;
        left -= run->num.total;
        at += static_cast<size_t>(run->num.valid + 1) * sizeof(StudioAnimValue);
        run = mdl.read_at<StudioAnimValue>(at);
        if (!run.has_value()) return base;
    }

    // Inside the run. Frames past the stored samples hold the last one.
    const int32_t index = (run->num.valid > left) ? (left + 1) : run->num.valid;
    const auto sample =
        mdl.read_at<StudioAnimValue>(at + static_cast<size_t>(index) * sizeof(StudioAnimValue));
    if (!sample.has_value()) return base;

    return base + static_cast<float>(sample->value) * scale;
}

/// The bind pose as an IRPose: every bone at its own rest value.
///
/// Stands in for a sequence whose tracks are in a sidecar file this parser was not given.
/// The alternative is a pose with no transforms in it, which every consumer would have to
/// special-case.
ir::IRPose rest_pose(const StudioBone* bones, int32_t num_bones) {
    ir::IRPose pose;
    pose.positions.reserve(static_cast<size_t>(num_bones));
    pose.rotations.reserve(static_cast<size_t>(num_bones));

    for (int32_t b = 0; b < num_bones; ++b) {
        const StudioBone& bone = bones[b];
        pose.positions.push_back(
            math::source_to_godot(godot::Vector3(bone.value[0], bone.value[1], bone.value[2])));
        pose.rotations.push_back(math::source_quat_to_godot(
            godot::Quaternion(studio_euler_to_basis(bone.value[3], bone.value[4], bone.value[5]))));
    }
    return pose;
}

/// One frame of a sequence, as parent-relative transforms in Godot's axes.
///
/// Channels 0 to 2 are position and 3 to 5 are Euler rotation in radians, each stored as a
/// signed offset from the bone's rest value in units of that bone's own scale. The scale is
/// per bone and per channel, which is how the format fits a whole skeleton into 16-bit
/// samples without the fingers inheriting the hips' precision.
ir::IRPose sample_pose(const ByteReader& mdl, size_t anim_offset, const StudioBone* bones,
                       int32_t num_bones, int32_t frame) {
    ir::IRPose pose;
    pose.positions.reserve(static_cast<size_t>(num_bones));
    pose.rotations.reserve(static_cast<size_t>(num_bones));

    for (int32_t b = 0; b < num_bones; ++b) {
        const StudioBone& bone = bones[b];
        const size_t here = anim_offset + static_cast<size_t>(b) * sizeof(StudioAnim);
        const auto anim = mdl.read_at<StudioAnim>(here);

        float channel[6];
        for (int c = 0; c < 6; ++c) {
            channel[c] = anim.has_value()
                             ? sample_channel(mdl, here, *anim, c, frame, bone.value[c],
                                              bone.scale[c])
                             : bone.value[c];
        }

        pose.positions.push_back(
            math::source_to_godot(godot::Vector3(channel[0], channel[1], channel[2])));
        pose.rotations.push_back(math::source_quat_to_godot(
            godot::Quaternion(studio_euler_to_basis(channel[3], channel[4], channel[5]))));
    }
    return pose;
}

/// A sequence longer than this is taken as a corrupt frame count rather than an animation.
/// The longest sequences in the stock games are a few hundred frames.
constexpr int32_t kMaxSequenceFrames = 4096;

/// Read every sequence: its label, the sounds it plays, and a pose for it.
///
/// Until this existed, every Half-Life 1 and Counter-Strike 1.6 model imported frozen in its
/// bind pose, because the bind pose was the only pose the parser could produce. The stances a
/// game actually shows all live here.
void read_sequences(const ByteReader& mdl, const StudioHeader& header, const StudioBone* bones,
                    ir::IRSkeletonData& skeleton,
                    std::vector<ir::IRAnimationData>& animations,
                    const std::vector<std::string>& animate) {
    if (bones == nullptr || header.num_bones <= 0 || header.num_seq <= 0 || header.seq_index <= 0) {
        return;
    }
    const auto sequences = mdl.array_at<StudioSeqDesc>(static_cast<size_t>(header.seq_index),
                                                       static_cast<size_t>(header.num_seq));
    if (sequences.empty()) return;

    for (size_t s = 0; s < sequences.size(); ++s) {
        const StudioSeqDesc& seq = sequences[s];

        std::string label(seq.label, strnlen(seq.label, sizeof(seq.label)));
        if (label.empty()) label = "sequence_" + std::to_string(s);

        // What the sequence plays. GoldSrc writes the path itself here, relative to sound/,
        // rather than the soundscript entry name Source uses.
        std::vector<std::string> sounds;
        if (seq.num_events > 0 && seq.event_index > 0) {
            const auto events = mdl.array_at<StudioEvent>(static_cast<size_t>(seq.event_index),
                                                          static_cast<size_t>(seq.num_events));
            for (const StudioEvent& ev : events) {
                if (ev.event != kEventClientSound && ev.event != kEventScriptSound
                    && ev.event != kEventScriptSoundVoice) {
                    continue;
                }
                const size_t len = strnlen(ev.options, sizeof(ev.options));
                if (len > 0) sounds.emplace_back(ev.options, len);
            }
        }

        // Where the bone tracks are. Group 0 means this file; anything else means a sidecar
        // "<name>01.mdl" that this parser was not handed, so those sequences keep their name
        // and their sounds and stand in the bind pose.
        const size_t anim_offset = static_cast<size_t>(seq.anim_index);
        const bool tracks_here =
            seq.seq_group == 0 && seq.anim_index > 0 &&
            !mdl.array_at<StudioAnim>(anim_offset, static_cast<size_t>(header.num_bones)).empty();

        ir::IRPose pose = tracks_here
                              ? sample_pose(mdl, anim_offset, bones, header.num_bones, 0)
                              : rest_pose(bones, header.num_bones);
        pose.name = std::move(label);
        pose.sounds = std::move(sounds);

        const bool wanted = std::find(animate.begin(), animate.end(), pose.name) != animate.end();

        if (wanted && tracks_here && seq.num_frames > 0 && seq.num_frames <= kMaxSequenceFrames) {
            ir::IRAnimationData anim;
            anim.source_engine = ir::SourceEngine::GoldSrc;
            anim.name = pose.name;
            anim.fps = seq.fps > 0.0f ? seq.fps : 30.0f;
            anim.looping = (seq.flags & kSeqLooping) != 0;
            // A one-frame sequence is a still, but a zero-length animation is not something a
            // player can hold, so it gets one frame's worth of time.
            anim.duration = static_cast<float>(std::max(seq.num_frames - 1, 1)) / anim.fps;

            anim.bone_tracks.resize(static_cast<size_t>(header.num_bones));
            for (int32_t b = 0; b < header.num_bones; ++b) {
                anim.bone_tracks[static_cast<size_t>(b)].bone_name = ir::sanitise_bone_name(
                    std::string(bones[b].name, strnlen(bones[b].name, sizeof(bones[b].name))),
                    static_cast<size_t>(b));
            }

            for (int32_t f = 0; f < seq.num_frames; ++f) {
                const ir::IRPose frame = sample_pose(mdl, anim_offset, bones, header.num_bones, f);
                const float time = static_cast<float>(f) / anim.fps;

                for (size_t b = 0; b < frame.positions.size(); ++b) {
                    ir::IRAnimKeyframe key;
                    key.time = time;
                    key.position = frame.positions[b];
                    key.rotation = frame.rotations[b];
                    anim.bone_tracks[b].keyframes.push_back(key);
                }
            }
            animations.push_back(std::move(anim));
        }

        skeleton.poses.push_back(std::move(pose));
    }
}

/// One corner emitted by the triangle command stream.
struct TriCorner {
    int16_t vertex = 0;
    int16_t normal = 0;
    int16_t s = 0;
    int16_t t = 0;
};

uint64_t corner_key(const TriCorner& c) {
    return (static_cast<uint64_t>(static_cast<uint16_t>(c.vertex)) << 48) |
           (static_cast<uint64_t>(static_cast<uint16_t>(c.normal)) << 32) |
           (static_cast<uint64_t>(static_cast<uint16_t>(c.s)) << 16) |
           (static_cast<uint64_t>(static_cast<uint16_t>(c.t)));
}

/// Decode the 8-bit palettized pixels embedded in the model into RGBA8.
/// Layout at `tex.index`: width*height indices, followed by a 256-entry RGB palette.
bool decode_embedded_texture(const ByteReader& mdl, const StudioTexture& tex,
                             ir::IRTextureData& out) {
    if (tex.index <= 0 || tex.width <= 0 || tex.height <= 0 ||
        tex.width > 4096 || tex.height > 4096) {
        return false;
    }

    const size_t offset = static_cast<size_t>(tex.index);
    const size_t pixels = static_cast<size_t>(tex.width) * static_cast<size_t>(tex.height);
    constexpr size_t kPaletteBytes = 256 * 3;

    const auto payload = mdl.array_at<uint8_t>(offset, pixels + kPaletteBytes);
    if (payload.empty()) return false;

    const uint8_t* indices = payload.data();
    const uint8_t* palette = indices + pixels;

    out.name.assign(tex.name, strnlen(tex.name, sizeof(tex.name)));
    out.width = static_cast<uint32_t>(tex.width);
    out.height = static_cast<uint32_t>(tex.height);

    // Flag 0x40 (STUDIO_NF_MASKED) marks palette index 255 as transparent, the same
    // convention WAD3 uses for '{'-prefixed textures.
    const bool masked = (tex.flags & 0x40) != 0;
    out.has_alpha = masked;

    out.rgba8_pixels.resize(pixels * 4);
    for (size_t i = 0; i < pixels; ++i) {
        const uint8_t idx = indices[i];
        out.rgba8_pixels[i * 4 + 0] = palette[idx * 3 + 0];
        out.rgba8_pixels[i * 4 + 1] = palette[idx * 3 + 1];
        out.rgba8_pixels[i * 4 + 2] = palette[idx * 3 + 2];
        out.rgba8_pixels[i * 4 + 3] = (masked && idx == 255) ? 0 : 255;
    }
    return true;
}

} // namespace

std::expected<ParsedMDL10Model, MDL10ParseError> MDL10Parser::parse(
    std::span<const std::byte> mdl_bytes,
    std::span<const std::byte> texture_mdl_bytes,
    const std::vector<std::string>& animate
) {
    const ByteReader mdl(mdl_bytes);

    const auto header_span = mdl.array_at<StudioHeader>(0, 1);
    if (header_span.empty()) {
        return std::unexpected(MDL10ParseError::InvalidHeader);
    }
    const StudioHeader* header = header_span.data();

    if (std::memcmp(header->magic, kMdl10Magic.data(), 4) != 0 || header->version != kMdl10Version) {
        return std::unexpected(MDL10ParseError::VersionMismatch);
    }

    ParsedMDL10Model result;
    result.mesh_data.source_engine = ir::SourceEngine::GoldSrc;
    result.mesh_data.name = std::string(header->name, strnlen(header->name, sizeof(header->name)));

    result.skeleton_data.source_engine = ir::SourceEngine::GoldSrc;
    result.skeleton_data.name = result.mesh_data.name;

    // ---------------------------------------------------------------- Bones ----
    const StudioBone* bones = nullptr;
    std::vector<godot::Transform3D> bone_rest; // model space, source Z-up frame

    if (header->num_bones > 0 && header->bone_index > 0) {
        const auto bone_span = mdl.array_at<StudioBone>(static_cast<size_t>(header->bone_index),
                                                        static_cast<size_t>(header->num_bones));
        if (!bone_span.empty()) {
        bones = bone_span.data();
        bone_rest = build_bone_rest_transforms(bones, header->num_bones);

        for (int32_t i = 0; i < header->num_bones; ++i) {
            const auto& b = bones[i];
            ir::IRBone ir_b;
            ir_b.name = ir::sanitise_bone_name(
                std::string(b.name, strnlen(b.name, sizeof(b.name))), static_cast<size_t>(i));
            ir_b.parent_index = b.parent;
            ir_b.position = math::source_to_godot(godot::Vector3(b.value[0], b.value[1], b.value[2]));
            ir_b.rotation = math::source_quat_to_godot(
                godot::Quaternion(studio_euler_to_basis(b.value[3], b.value[4], b.value[5])));
            result.skeleton_data.bones.push_back(std::move(ir_b));
        }

        read_sequences(mdl, *header, bones, result.skeleton_data, result.animations, animate);
        }
    }

    // ------------------------------------------------------------- Textures ----
    // Embedded textures let the model import fully textured with no VFS lookup.
    // When num_textures is 0 they live in a companion "<name>T.mdl" instead, which
    // this parser does not load — surfaces then carry only a material name.
    const StudioTexture* textures = nullptr;
    size_t num_textures = 0;

    // Textures live either in this file or in the companion "<name>T.mdl", which has
    // the same header layout with its own texture table. Pick whichever declares them.
    ByteReader tex_source = mdl;
    const StudioHeader* tex_header = header;

    if (header->num_textures <= 0 && !texture_mdl_bytes.empty()) {
        const ByteReader companion(texture_mdl_bytes);
        const auto t_span = companion.array_at<StudioHeader>(0, 1);
        if (!t_span.empty() &&
            std::memcmp(t_span[0].magic, kMdl10Magic.data(), 4) == 0 &&
            t_span[0].num_textures > 0) {
            tex_source = companion;
            tex_header = t_span.data();
        }
    }

    if (tex_header->num_textures > 0 && tex_header->texture_index > 0) {
        const auto tex_span = tex_source.array_at<StudioTexture>(
            static_cast<size_t>(tex_header->texture_index),
            static_cast<size_t>(tex_header->num_textures));

        if (!tex_span.empty()) {
            textures = tex_span.data();
            num_textures = tex_span.size();

            result.mesh_data.embedded_textures.resize(num_textures);
            for (size_t i = 0; i < num_textures; ++i) {
                ir::IRTextureData decoded;
                if (decode_embedded_texture(tex_source, textures[i], decoded)) {
                    result.mesh_data.embedded_textures[i] = std::move(decoded);
                }
            }
        }
    }

    // The skin table always comes from the model file, never the texture companion.

    // Skin table: int16[num_skin_families][num_skin_ref]. A mesh's skin_ref indexes a
    // row of family 0 to reach the real texture.
    const int16_t* skin_table = nullptr;
    size_t num_skin_ref = 0;

    if (header->num_skin_ref > 0 && header->skin_index > 0) {
        const auto skin_span = mdl.array_at<int16_t>(static_cast<size_t>(header->skin_index),
                                                     static_cast<size_t>(header->num_skin_ref));
        if (!skin_span.empty()) {
            skin_table = skin_span.data();
            num_skin_ref = skin_span.size();
        }
    }

    auto resolve_texture = [&](int32_t skin_ref) -> int32_t {
        if (skin_ref < 0) return -1;
        size_t tex_idx = static_cast<size_t>(skin_ref);
        if (skin_table && tex_idx < num_skin_ref) {
            const int16_t mapped = skin_table[tex_idx];
            if (mapped < 0) return -1;
            tex_idx = static_cast<size_t>(mapped);
        }
        return tex_idx < num_textures ? static_cast<int32_t>(tex_idx) : -1;
    };

    // ------------------------------------------------------------ Body parts ----
    if (header->num_bodyparts <= 0 || header->bodypart_index <= 0) {
        return result; // skeleton-only model
    }
    const auto bodypart_span = mdl.array_at<StudioBodyPart>(
        static_cast<size_t>(header->bodypart_index),
        static_cast<size_t>(header->num_bodyparts));
    if (bodypart_span.empty()) {
        return result; // corrupt bodypart table
    }
    const StudioBodyPart* bodyparts = bodypart_span.data();

    // One surface per texture, so the mesh ends up with as few draw calls as the
    // source material set allows.
    std::unordered_map<int32_t, ir::IRSurface> surface_map;
    std::unordered_map<int32_t, std::unordered_map<uint64_t, uint32_t>> dedup;

    for (int32_t bp = 0; bp < header->num_bodyparts; ++bp) {
        const auto& body = bodyparts[bp];

        // Only the first model of each body part is imported: the remaining ones are
        // alternates selected at runtime by the "body" value (e.g. weapon variants),
        // and importing all of them would stack overlapping geometry.
        if (body.num_models <= 0 || body.model_index <= 0 ||
            mdl.array_at<StudioModel>(static_cast<size_t>(body.model_index), 1).empty()) {
            continue;
        }

        const auto* model = mdl.array_at<StudioModel>(static_cast<size_t>(body.model_index), 1).data();

        if (model->num_verts <= 0 || model->vert_index <= 0) continue;
        const auto vert_span = mdl.array_at<float>(static_cast<size_t>(model->vert_index),
                                                   static_cast<size_t>(model->num_verts) * 3);
        if (vert_span.empty()) continue;

        const float* verts = vert_span.data();
        const size_t num_verts = static_cast<size_t>(model->num_verts);

        const float* norms = nullptr;
        size_t num_norms = 0;
        if (model->num_norms > 0 && model->norm_index > 0) {
            const auto norm_span = mdl.array_at<float>(static_cast<size_t>(model->norm_index),
                                                       static_cast<size_t>(model->num_norms) * 3);
            if (!norm_span.empty()) {
                norms = norm_span.data();
                num_norms = static_cast<size_t>(model->num_norms);
            }
        }

        // One bone index per vertex; vertices are stored in that bone's local space.
        const uint8_t* vert_bones = nullptr;
        if (model->vert_info_index > 0) {
            const auto span = mdl.array_at<uint8_t>(static_cast<size_t>(model->vert_info_index),
                                                    num_verts);
            if (!span.empty()) vert_bones = span.data();
        }

        const uint8_t* norm_bones = nullptr;
        if (model->norm_info_index > 0 && num_norms > 0) {
            const auto span = mdl.array_at<uint8_t>(static_cast<size_t>(model->norm_info_index),
                                                    num_norms);
            if (!span.empty()) norm_bones = span.data();
        }

        if (model->num_mesh <= 0 || model->mesh_index <= 0) continue;
        const auto mesh_span = mdl.array_at<StudioMesh>(static_cast<size_t>(model->mesh_index),
                                                        static_cast<size_t>(model->num_mesh));
        if (mesh_span.empty()) continue;
        const StudioMesh* meshes = mesh_span.data();

        for (int32_t m = 0; m < model->num_mesh; ++m) {
            const auto& mesh = meshes[m];
            if (mesh.tri_index <= 0 || static_cast<size_t>(mesh.tri_index) >= mdl.size()) {
                continue;
            }

            const int32_t tex_index = resolve_texture(mesh.skin_ref);

            float tex_w = 64.0f;
            float tex_h = 64.0f;
            std::string tex_name = "mdl_texture_" + std::to_string(mesh.skin_ref);
            if (tex_index >= 0 && textures) {
                const auto& td = textures[tex_index];
                if (td.width > 0 && td.height > 0) {
                    tex_w = static_cast<float>(td.width);
                    tex_h = static_cast<float>(td.height);
                }
                tex_name.assign(td.name, strnlen(td.name, sizeof(td.name)));
            }

            auto& surf = surface_map[tex_index];
            auto& surf_dedup = dedup[tex_index];
            if (surf.material_name.empty()) {
                surf.material_name = tex_name;
                surf.embedded_texture_index =
                    (tex_index >= 0 && static_cast<size_t>(tex_index) < result.mesh_data.embedded_textures.size() &&
                     result.mesh_data.embedded_textures[tex_index].is_valid())
                        ? tex_index : -1;
            }

            // Append a corner, reusing an identical one already emitted.
            auto push_corner = [&](const TriCorner& c) -> uint32_t {
                const uint64_t key = corner_key(c);
                if (auto it = surf_dedup.find(key); it != surf_dedup.end()) {
                    return it->second;
                }

                const size_t vi = static_cast<size_t>(static_cast<uint16_t>(c.vertex));
                godot::Vector3 pos_local(0, 0, 0);
                if (vi < num_verts) {
                    pos_local = godot::Vector3(verts[vi * 3 + 0], verts[vi * 3 + 1], verts[vi * 3 + 2]);
                }

                // Bone-local -> model space, still Z-up.
                if (vert_bones && vi < num_verts) {
                    const size_t bone = vert_bones[vi];
                    if (bone < bone_rest.size()) {
                        pos_local = bone_rest[bone].xform(pos_local);
                    }
                }

                godot::Vector3 normal_local(0, 0, 1);
                const size_t ni = static_cast<size_t>(static_cast<uint16_t>(c.normal));
                if (norms && ni < num_norms) {
                    normal_local = godot::Vector3(norms[ni * 3 + 0], norms[ni * 3 + 1], norms[ni * 3 + 2]);
                    if (norm_bones) {
                        const size_t bone = norm_bones[ni];
                        if (bone < bone_rest.size()) {
                            // Rotate only: a normal must not pick up the translation.
                            normal_local = bone_rest[bone].basis.xform(normal_local);
                        }
                    }
                }

                const uint32_t index = static_cast<uint32_t>(surf.positions.size());
                surf.positions.push_back(math::source_to_godot(pos_local));
                surf.normals.push_back(math::transform_normal_zup_to_yup(normal_local));
                surf.uv0.emplace_back(static_cast<float>(c.s) / tex_w,
                                      static_cast<float>(c.t) / tex_h);

                // Always push, so the arrays stay parallel with positions. GoldSrc is
                // rigid-skinned: exactly one bone per vertex at full weight.
                const int32_t bone_id = (vert_bones && vi < num_verts)
                                      ? static_cast<int32_t>(vert_bones[vi]) : 0;
                surf.bone_indices.push_back({bone_id, 0, 0, 0});
                surf.bone_weights.push_back({1.0f, 0.0f, 0.0f, 0.0f});

                surf_dedup.emplace(key, index);
                return index;
            };

            // Triangle command stream: an int16 count, then abs(count) corners of four
            // int16 each. A negative count is a fan, positive is a strip, 0 terminates.
            ByteReader tris(mdl_bytes, static_cast<size_t>(mesh.tri_index));
            std::vector<TriCorner> run;

            while (true) {
                const auto cmd_opt = tris.read<int16_t>();
                if (!cmd_opt) break;
                const int16_t cmd = *cmd_opt;
                if (cmd == kTriCommandEnd) break;

                const bool is_fan = cmd < 0;
                // Promote before negating: -INT16_MIN does not fit in int16_t.
                const size_t count = static_cast<size_t>(is_fan ? -static_cast<int>(cmd)
                                                                : static_cast<int>(cmd));

                // Each corner is four int16: vertex, normal, s, t.
                const auto corners = tris.read_array<TriCorner>(count);
                if (corners.empty() && count != 0) break;

                run.assign(corners.begin(), corners.end());

                if (run.size() < 3) continue;

                if (is_fan) {
                    const uint32_t hub = push_corner(run[0]);
                    for (size_t k = 1; k + 1 < run.size(); ++k) {
                        surf.indices.push_back(hub);
                        surf.indices.push_back(push_corner(run[k]));
                        surf.indices.push_back(push_corner(run[k + 1]));
                    }
                } else {
                    for (size_t k = 0; k + 2 < run.size(); ++k) {
                        // Strips alternate orientation; swapping the first two corners
                        // on odd triangles keeps the winding consistent, matching the
                        // GL_TRIANGLE_STRIP convention the original renderer used.
                        const uint32_t a = push_corner(run[k]);
                        const uint32_t b = push_corner(run[k + 1]);
                        const uint32_t c = push_corner(run[k + 2]);
                        if (k % 2 == 0) {
                            surf.indices.push_back(a);
                            surf.indices.push_back(b);
                            surf.indices.push_back(c);
                        } else {
                            surf.indices.push_back(b);
                            surf.indices.push_back(a);
                            surf.indices.push_back(c);
                        }
                    }
                }
            }
        }
    }

    for (auto& [tex_idx, surf] : surface_map) {
        if (surf.positions.empty() || surf.indices.empty()) continue;
        result.mesh_data.surfaces.push_back(std::move(surf));
    }

    return result;
}

} // namespace quebratsk::parsers::goldsrc
