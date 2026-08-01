#include "mdl_source_parser.h"
#include "anim_decoder.h"
#include "vvd_parser.h"
#include "../../core/io/byte_reader.h"
#include "../../core/math/axis_remap.h"

#include <godot_cpp/variant/utility_functions.hpp>

#include <algorithm>
#include <cctype>
#include <cstring>
#include <limits>
#include <optional>
#include <unordered_map>
#include <vector>

namespace quebratsk::parsers::source1 {

using io::ByteReader;

namespace {

/// Every *_offset in a VTX file is relative to the address of the struct holding it,
/// never to the start of the file. This helper makes that explicit at each use and
/// keeps the relative-to-absolute arithmetic overflow-safe in one place.
template <typename T>
const T* at_relative(const ByteReader& reader, size_t struct_offset, int32_t rel,
                     size_t count = 1) {
    if (rel < 0) return nullptr;
    // struct_offset is always <= size (it came from a checked read), so this cannot wrap.
    if (struct_offset > reader.size()) return nullptr;
    if (static_cast<size_t>(rel) > reader.size() - struct_offset) return nullptr;

    const auto span = reader.array_at<T>(struct_offset + static_cast<size_t>(rel), count);
    return span.empty() ? nullptr : span.data();
}

/// Bone hierarchy is not needed here: unlike GoldSrc, Source stores vertices already in
/// model space, so positions can be used directly.
void fill_skeleton(const ByteReader& mdl, const SourceStudioHeader* header,
                   ir::IRSkeletonData& out) {
    if (header->num_bones <= 0 || header->bone_index <= 0) return;

    const auto bones = mdl.array_at<SourceStudioBone>(static_cast<size_t>(header->bone_index),
                                                      static_cast<size_t>(header->num_bones));
    if (bones.empty()) return;

    for (int32_t i = 0; i < header->num_bones; ++i) {
        const auto& b = bones[static_cast<size_t>(i)];
        ir::IRBone ir_b;

        // name_index is relative to the start of this bone record.
        const size_t bone_base = static_cast<size_t>(header->bone_index) +
                                 static_cast<size_t>(i) * sizeof(SourceStudioBone);
        bool named = false;
        if (b.name_index > 0 && bone_base <= mdl.size() &&
            static_cast<size_t>(b.name_index) <= mdl.size() - bone_base) {
            if (auto name = mdl.cstr_at(bone_base + static_cast<size_t>(b.name_index));
                name.has_value() && !name->empty()) {
                ir_b.name = std::move(*name);
                named = true;
            }
        }
        if (!named) ir_b.name = "ValveBiped.Bip01_Bone_" + std::to_string(i);

        ir_b.parent_index = b.parent;
        ir_b.position = math::source_to_godot(godot::Vector3(b.pos[0], b.pos[1], b.pos[2]));
        ir_b.rotation = math::source_quat_to_godot(
            godot::Quaternion(b.rot[0], b.rot[1], b.rot[2], b.rot[3]));

        out.bones.push_back(std::move(ir_b));
    }
}

/// How many mstudioanimvalue_t to hand sample_track() so it can reach `frame`.
///
/// The encoding gives no way to know a track's length without decoding it, so this reads a
/// window and lets sample_track() stop where it needs to. Worst case a run covers a single
/// frame and costs two entries, a header and a sample, so 2·frame always reaches it. A
/// fixed 256 was enough only because nothing ever asked past frame 0.
std::span<const SourceAnimValue> rle_window(const ByteReader& data, size_t offset,
                                            int32_t frame) {
    if (offset >= data.size()) return {};
    const size_t available = (data.size() - offset) / sizeof(SourceAnimValue);
    const size_t wanted = 256 + static_cast<size_t>(frame < 0 ? 0 : frame) * 2;
    return data.array_at<SourceAnimValue>(offset, std::min(wanted, available));
}

/// Walk the linked list of per-bone tracks starting at `start` and overwrite the entries
/// of `src_pos`/`src_rot` it mentions. Returns true if any bone was touched.
///
/// `data` is not necessarily the .mdl: for a blocked animation the very same layout is
/// read out of the companion .ani instead, which is why the reader is a parameter.
///
/// Rotations arrive either raw and quantised (Quaternion48/64) or as three run-length
/// encoded Euler tracks scaled by the bone's rot_scale and added to its rest angles;
/// positions work the same way. Bones absent from the chain keep their rest transform,
/// which is what the format intends.
///
/// Passing null for both outputs asks only whether the chain decodes at all, which is the
/// question PoseDetail::NamesOnly needs answered. The decode still runs — the answer
/// depends on it — but only until the first bone that succeeds, and nothing is stored. The
/// probe therefore accepts and rejects exactly what the full walk does, which a separate
/// cheaper test could not promise.
bool walk_bone_tracks(const ByteReader& data, size_t start, int32_t frame,
                      std::span<const SourceStudioBone> bones,
                      std::vector<godot::Vector3>* src_pos,
                      std::vector<godot::Quaternion>* src_rot) {
    size_t cursor = start;
    int guard = 0;
    bool any = false;

    while (guard++ < 1024) {
        const auto ba_span = data.array_at<SourceBoneAnim>(cursor, 1);
        if (ba_span.empty()) break;
        const SourceBoneAnim ba = ba_span[0];
        if (ba.bone >= bones.size()) break;

        const SourceStudioBone& bone = bones[ba.bone];
        size_t payload = cursor + sizeof(SourceBoneAnim);

        // --- rotation, which always precedes position in the payload ---
        if (ba.flags & kAnimRawRot) {
            if (const auto q = data.array_at<SourceQuat48>(payload, 1); !q.empty()) {
                const auto d = AnimDecoder::decode_quat48(q[0]);
                if (src_rot) (*src_rot)[ba.bone] = godot::Quaternion(d[0], d[1], d[2], d[3]);
                any = true;
            }
            payload += sizeof(SourceQuat48);
        } else if (ba.flags & kAnimRawRot2) {
            if (const auto q = data.array_at<SourceQuat64>(payload, 1); !q.empty()) {
                const auto d = AnimDecoder::decode_quat64(q[0]);
                if (src_rot) (*src_rot)[ba.bone] = godot::Quaternion(d[0], d[1], d[2], d[3]);
                any = true;
            }
            payload += sizeof(SourceQuat64);
        } else if (ba.flags & kAnimAnimRot) {
            const auto vp = data.array_at<SourceAnimValuePtr>(payload, 1);
            if (!vp.empty()) {
                float angle[3];
                for (int axis = 0; axis < 3; ++axis) {
                    angle[axis] = bone.radrot[axis];
                    if (vp[0].offset[axis] == 0) continue;
                    const size_t track_ofs = payload + static_cast<size_t>(vp[0].offset[axis]);
                    const auto track = rle_window(data, track_ofs, frame);
                    if (auto v = AnimDecoder::sample_track(track, frame); v.has_value()) {
                        angle[axis] += static_cast<float>(*v) * bone.rot_scale[axis];
                    }
                }
                // StudioMDL Euler order is X then Y then Z, same as the bind pose.
                const godot::Quaternion qx(godot::Vector3(1, 0, 0), angle[0]);
                const godot::Quaternion qy(godot::Vector3(0, 1, 0), angle[1]);
                const godot::Quaternion qz(godot::Vector3(0, 0, 1), angle[2]);
                if (src_rot) (*src_rot)[ba.bone] = qz * qy * qx;
                any = true;
            }
            payload += sizeof(SourceAnimValuePtr);
        }

        // --- position ---
        if (ba.flags & kAnimRawPos) {
            if (const auto v = data.array_at<SourceVec48>(payload, 1); !v.empty()) {
                const auto d = AnimDecoder::decode_vec48(v[0]);
                if (src_pos) (*src_pos)[ba.bone] = godot::Vector3(d[0], d[1], d[2]);
                any = true;
            }
        } else if (ba.flags & kAnimAnimPos) {
            const auto vp = data.array_at<SourceAnimValuePtr>(payload, 1);
            if (!vp.empty()) {
                float p[3];
                for (int axis = 0; axis < 3; ++axis) {
                    p[axis] = bone.pos[axis];
                    if (vp[0].offset[axis] == 0) continue;
                    const size_t track_ofs = payload + static_cast<size_t>(vp[0].offset[axis]);
                    const auto track = rle_window(data, track_ofs, frame);
                    if (auto v = AnimDecoder::sample_track(track, frame); v.has_value()) {
                        p[axis] += static_cast<float>(*v) * bone.pos_scale[axis];
                    }
                }
                if (src_pos) (*src_pos)[ba.bone] = godot::Vector3(p[0], p[1], p[2]);
                any = true;
            }
        }

        // Probing only needs one bone to prove the sequence is readable.
        if (any && !src_pos && !src_rot) return true;

        if (ba.next_offset <= 0) break;
        cursor += static_cast<size_t>(ba.next_offset);
    }
    return any;
}

/// Where the track chain for one frame of an animation descriptor actually begins.
struct TrackLocation {
    bool external = false;  // true -> offset is into the .ani, false -> into the .mdl
    size_t offset = 0;
    int32_t local_frame = 0; // frame index within the section the offset points at
};

/// Resolve `adesc` and a frame to a concrete byte offset, following section and block
/// indirection.
///
/// Two levels of indirection stand between a descriptor and its data. A long animation is
/// chopped into fixed-length sections, each with its own chain, so a frame must be looked
/// up through the section that holds it and then addressed relative to that section's
/// start. Whatever block that yields, a non-zero one means the bytes live in the companion
/// .ani at `data_start + anim_index` instead of next to the descriptor.
std::optional<TrackLocation> locate_tracks(const ByteReader& mdl, size_t adesc_ofs,
                                           const SourceAnimDesc& adesc,
                                           std::span<const SourceAnimBlock> blocks,
                                           size_t ani_size,
                                           int32_t frame = 0) {
    int32_t block = adesc.anim_block;
    int32_t index = adesc.anim_index;
    int32_t local_frame = frame;

    if (adesc.section_frames > 0 && adesc.section_index > 0) {
        const int32_t section_no = frame / adesc.section_frames;
        local_frame = frame % adesc.section_frames;

        // studiomdl writes numframes/sectionframes + 2 sections: one past the last full
        // one, plus a single-frame tail. Reading beyond that is reading whatever follows.
        const int32_t section_count = adesc.num_frames / adesc.section_frames + 2;
        if (section_no < 0 || section_no >= section_count) return std::nullopt;

        // section_index is relative to the descriptor, and the sections are contiguous.
        const size_t rel = static_cast<size_t>(adesc.section_index) +
                           static_cast<size_t>(section_no) * sizeof(SourceAnimSection);
        if (rel > static_cast<size_t>(std::numeric_limits<int32_t>::max())) return std::nullopt;

        const auto section = at_relative<SourceAnimSection>(mdl, adesc_ofs,
                                                            static_cast<int32_t>(rel), 1);
        if (!section) return std::nullopt;
        block = section->anim_block;
        index = section->anim_index;
    }
    // Zero is a real offset when the data lives in a block: the section simply starts at
    // the block's first byte. Only inline data can never sit at zero, because offset zero
    // from the descriptor is the descriptor itself. Rejecting it everywhere cut long
    // sequences off at whichever section happened to land on a block boundary, and dropped
    // outright any sequence whose very first section did.
    if (index < 0 || (index == 0 && block == 0)) return std::nullopt;

    if (block == 0) {
        if (adesc_ofs > mdl.size()) return std::nullopt;
        if (static_cast<size_t>(index) > mdl.size() - adesc_ofs) return std::nullopt;
        return TrackLocation{false, adesc_ofs + static_cast<size_t>(index), local_frame};
    }

    if (block < 0 || static_cast<size_t>(block) >= blocks.size()) return std::nullopt;
    const SourceAnimBlock& blk = blocks[static_cast<size_t>(block)];
    if (blk.data_start < 0 || blk.data_end <= blk.data_start) return std::nullopt;
    if (static_cast<size_t>(blk.data_start) >= ani_size) return std::nullopt;
    // anim_index is relative to the block, not to the file.
    if (static_cast<size_t>(index) > ani_size - static_cast<size_t>(blk.data_start)) {
        return std::nullopt;
    }
    return TrackLocation{true,
                         static_cast<size_t>(blk.data_start) + static_cast<size_t>(index),
                         local_frame};
}

/// Decode every frame of one sequence into an IRAnimationData.
///
/// Frame 0 is what extract_poses() already lifts; this walks the rest. Each frame is
/// resolved separately because a long animation is split into sections with their own
/// chains, so the byte offset is not a function of the descriptor alone.
///
/// Bones the sequence never mentions are still written, holding their rest transform.
/// Godot interpolates between the keys it is given, so a track that omits an unanimated
/// bone would let a neighbouring animation's last value stand — the model would keep an
/// arm raised through an idle it was never posed by.
ir::IRAnimationData decode_animation(const ByteReader& mdl, const ByteReader& ani,
                                     const ir::IRSkeletonData& skel,
                                     std::span<const SourceStudioBone> bones,
                                     std::span<const SourceAnimBlock> blocks,
                                     size_t adesc_ofs, const SourceAnimDesc& adesc,
                                     const std::string& name, bool looping) {
    ir::IRAnimationData anim;
    anim.source_engine = ir::SourceEngine::Source1;
    anim.name = name;
    anim.fps = adesc.fps > 0.0f ? adesc.fps : 30.0f;
    anim.looping = looping;

    const int32_t frames = adesc.num_frames;
    int32_t decoded = 0;

    anim.bone_tracks.resize(bones.size());
    for (size_t b = 0; b < bones.size(); ++b) {
        anim.bone_tracks[b].bone_name = b < skel.bones.size() ? skel.bones[b].name : std::string();
        anim.bone_tracks[b].keyframes.reserve(static_cast<size_t>(frames));
    }

    std::vector<godot::Vector3> src_pos(bones.size());
    std::vector<godot::Quaternion> src_rot(bones.size());

    for (int32_t f = 0; f < frames; ++f) {
        const auto loc = locate_tracks(mdl, adesc_ofs, adesc, blocks, ani.size(), f);
        if (!loc.has_value()) break;

        // Reseed every frame: the chain only mentions bones it animates, and leaving the
        // previous frame's values in place would smear one bone's motion onto another.
        for (size_t b = 0; b < bones.size(); ++b) {
            src_pos[b] = godot::Vector3(bones[b].pos[0], bones[b].pos[1], bones[b].pos[2]);
            src_rot[b] = godot::Quaternion(bones[b].rot[0], bones[b].rot[1],
                                           bones[b].rot[2], bones[b].rot[3]);
        }

        const ByteReader& data = loc->external ? ani : mdl;
        if (!walk_bone_tracks(data, loc->offset, loc->local_frame, bones, &src_pos, &src_rot)) {
            break;
        }
        ++decoded;

        const float t = static_cast<float>(f) / anim.fps;
        for (size_t b = 0; b < bones.size(); ++b) {
            ir::IRAnimKeyframe kf;
            kf.time = t;
            kf.position = math::source_to_godot(src_pos[b]);
            kf.rotation = math::source_quat_to_godot(src_rot[b]);
            anim.bone_tracks[b].keyframes.push_back(kf);
        }
    }

    // The duration comes from what was decoded, not from what the header claims. A
    // sequence whose data runs out part way is a shorter animation, and claiming the full
    // length would leave the model frozen through frames that were never read.
    anim.duration = static_cast<float>(decoded > 1 ? decoded - 1 : 0) / anim.fps;
    if (decoded < frames) {
        godot::UtilityFunctions::printerr(
            "[QuebratskParser] '", name.c_str(), "': ", decoded, " of ", frames,
            " frames decoded; the rest could not be reached");
    }

    return anim;
}

/// Lift frame 0 of every animation sequence out of the model, and every frame of the
/// sequences named in `animate`.
///
/// The chain is: sequence -> blend grid -> animation descriptor -> section -> block ->
/// a linked list of per-bone tracks. `ani` holds the companion animation-block file and
/// may be empty, in which case blocked sequences are skipped.
void extract_poses(const ByteReader& mdl, const ByteReader& ani,
                   const SourceStudioHeader* header,
                   std::span<const SourceStudioBone> bones,
                   ir::IRSkeletonData& out,
                   PoseDetail detail,
                   std::span<const std::string> animate = {},
                   std::vector<ir::IRAnimationData>* animations = nullptr) {
    if (bones.empty()) return;
    if (header->num_local_seq <= 0 || header->local_seq_index <= 0) return;
    if (header->num_local_anim <= 0 || header->local_anim_index <= 0) return;

    const auto seqs = mdl.array_at<SourceSeqDesc>(static_cast<size_t>(header->local_seq_index),
                                                  static_cast<size_t>(header->num_local_seq));
    const auto anims = mdl.array_at<SourceAnimDesc>(static_cast<size_t>(header->local_anim_index),
                                                    static_cast<size_t>(header->num_local_anim));
    if (seqs.empty() || anims.empty()) return;

    std::span<const SourceAnimBlock> blocks;
    if (!ani.empty() && header->num_anim_blocks > 0 && header->anim_block_index > 0) {
        blocks = mdl.array_at<SourceAnimBlock>(static_cast<size_t>(header->anim_block_index),
                                               static_cast<size_t>(header->num_anim_blocks));
    }

    for (size_t s = 0; s < seqs.size(); ++s) {
        const SourceSeqDesc& seq = seqs[s];
        const size_t seq_ofs = static_cast<size_t>(header->local_seq_index) +
                               s * sizeof(SourceSeqDesc);

        // The blend grid is an int16 array; the first entry is the base animation.
        const auto blend = at_relative<int16_t>(mdl, seq_ofs, seq.anim_index_index, 1);
        if (!blend) continue;
        const int16_t anim_id = *blend;
        if (anim_id < 0 || static_cast<size_t>(anim_id) >= anims.size()) continue;

        const SourceAnimDesc& adesc = anims[static_cast<size_t>(anim_id)];
        if (adesc.num_frames <= 0) continue;

        const size_t adesc_ofs = static_cast<size_t>(header->local_anim_index) +
                                 static_cast<size_t>(anim_id) * sizeof(SourceAnimDesc);

        const auto loc = locate_tracks(mdl, adesc_ofs, adesc, blocks, ani.size());
        if (!loc.has_value()) continue;

        ir::IRPose pose;
        if (auto n = mdl.cstr_at(seq_ofs + static_cast<size_t>(seq.name_index));
            n.has_value() && !n->empty()) {
            pose.name = std::move(*n);
        } else {
            pose.name = "sequence_" + std::to_string(s);
        }

        const ByteReader& track_data = loc->external ? ani : mdl;

        if (detail == PoseDetail::NamesOnly) {
            // Seeding the rest transforms and converting them back out is the bulk of the
            // per-sequence cost, and a caller after names alone throws all of it away.
            if (walk_bone_tracks(track_data, loc->offset, loc->local_frame,
                                 bones, nullptr, nullptr)) {
                out.poses.push_back(std::move(pose));
            }
            continue;
        }

        // Accumulate in the SOURCE frame; the axis remap is applied once at the end so
        // IRPose lands in the same space as IRBone.
        std::vector<godot::Vector3> src_pos;
        std::vector<godot::Quaternion> src_rot;
        src_pos.reserve(bones.size());
        src_rot.reserve(bones.size());
        for (const auto& b : bones) {
            src_pos.push_back(godot::Vector3(b.pos[0], b.pos[1], b.pos[2]));
            src_rot.push_back(godot::Quaternion(b.rot[0], b.rot[1], b.rot[2], b.rot[3]));
        }

        const bool any = walk_bone_tracks(track_data, loc->offset, loc->local_frame,
                                          bones, &src_pos, &src_rot);
        if (any) {
            pose.positions.reserve(bones.size());
            pose.rotations.reserve(bones.size());
            for (size_t b = 0; b < bones.size(); ++b) {
                pose.positions.push_back(math::source_to_godot(src_pos[b]));
                pose.rotations.push_back(math::source_quat_to_godot(src_rot[b]));
            }

            // Only sequences the caller asked for: a player model carries 359 of them,
            // and decoding every frame of all of them would be tens of megabytes of
            // keyframes for a caller that wanted one walk cycle.
            if (animations != nullptr && adesc.num_frames > 1 &&
                std::find(animate.begin(), animate.end(), pose.name) != animate.end()) {
                animations->push_back(decode_animation(mdl, ani, out, bones, blocks,
                                                       adesc_ofs, adesc, pose.name,
                                                       (seq.flags & kSeqLooping) != 0));
            }

            out.poses.push_back(std::move(pose));
        }
    }
}

} // namespace

std::vector<std::string> SourceMDLParser::list_include_models(
    std::span<const std::byte> mdl_bytes
) {
    std::vector<std::string> out;
    const ByteReader mdl(mdl_bytes);

    const auto hdr = mdl.array_at<SourceStudioHeader>(0, 1);
    if (hdr.empty()) return out;
    const SourceStudioHeader& h = hdr[0];

    if (std::memcmp(h.magic, kSourceMdlMagic.data(), 4) != 0) return out;
    if (h.version < kSourceMdlMinVersion || h.version > kSourceMdlMaxVersion) return out;
    if (h.num_include_models <= 0 || h.include_model_index <= 0) return out;

    const auto groups = mdl.array_at<SourceModelGroup>(
        static_cast<size_t>(h.include_model_index), static_cast<size_t>(h.num_include_models));

    for (size_t i = 0; i < groups.size(); ++i) {
        const size_t base = static_cast<size_t>(h.include_model_index) +
                            i * sizeof(SourceModelGroup);
        if (groups[i].name_index <= 0) continue;
        if (base > mdl.size() || static_cast<size_t>(groups[i].name_index) > mdl.size() - base) continue;

        if (auto name = mdl.cstr_at(base + static_cast<size_t>(groups[i].name_index));
            name.has_value() && !name->empty()) {
            out.push_back(std::move(*name));
        }
    }
    return out;
}

std::string SourceMDLParser::anim_block_name(std::span<const std::byte> mdl_bytes) {
    const ByteReader mdl(mdl_bytes);

    const auto hdr = mdl.array_at<SourceStudioHeader>(0, 1);
    if (hdr.empty()) return {};
    const SourceStudioHeader& h = hdr[0];

    if (std::memcmp(h.magic, kSourceMdlMagic.data(), 4) != 0) return {};
    if (h.version < kSourceMdlMinVersion || h.version > kSourceMdlMaxVersion) return {};
    if (h.num_anim_blocks <= 0 || h.anim_block_name_index <= 0) return {};

    // Unlike most indices in this format, this one is from the file start.
    auto name = mdl.cstr_at(static_cast<size_t>(h.anim_block_name_index));
    if (!name.has_value()) return {};
    return std::move(*name);
}

std::expected<ParsedSourceMDLModel, SourceMDLParseError> SourceMDLParser::parse(
    std::span<const std::byte> mdl_bytes
) {
    return parse_bundle(SourceModelBundle{mdl_bytes, {}, {}, {}, {}});
}

std::expected<ParsedSourceMDLModel, SourceMDLParseError> SourceMDLParser::parse_bundle(
    const SourceModelBundle& bundle,
    PoseDetail poses
) {
    const ByteReader mdl(bundle.mdl);

    const auto header_span = mdl.array_at<SourceStudioHeader>(0, 1);
    if (header_span.empty()) {
        return std::unexpected(SourceMDLParseError::InvalidHeader);
    }
    const SourceStudioHeader* header = header_span.data();

    if (std::memcmp(header->magic, kSourceMdlMagic.data(), 4) != 0) {
        return std::unexpected(SourceMDLParseError::InvalidHeader);
    }

    // GoldSrc StudioMDL uses the SAME "IDST" magic, so the magic alone cannot tell the
    // two apart. Without this check a GoldSrc model was accepted here, returned an empty
    // mesh, and the caller never fell through to MDL10Parser.
    if (header->version < kSourceMdlMinVersion || header->version > kSourceMdlMaxVersion) {
        return std::unexpected(SourceMDLParseError::VersionMismatch);
    }

    ParsedSourceMDLModel result;
    result.mesh_data.source_engine = ir::SourceEngine::Source1;
    result.mesh_data.name = std::string(header->name, ::strnlen(header->name, sizeof(header->name)));
    result.skeleton_data.source_engine = ir::SourceEngine::Source1;
    result.skeleton_data.name = result.mesh_data.name;

    fill_skeleton(mdl, header, result.skeleton_data);

    // Lift a real stance out of the animation sequences. Without this the model stands
    // in its bind pose, which is a modelling artefact the game never shows.
    if (header->num_bones > 0 && header->bone_index > 0) {
        const auto bone_span = mdl.array_at<SourceStudioBone>(
            static_cast<size_t>(header->bone_index), static_cast<size_t>(header->num_bones));
        if (!bone_span.empty()) {
            extract_poses(mdl, ByteReader(bundle.ani), header, bone_span,
                          result.skeleton_data, poses, bundle.animate, &result.animations);
        }
    }

    // Sequences borrowed from the shared animation models this one includes. Their bone
    // tables are ordered independently, so poses are remapped by bone NAME rather than
    // index — matching by index would scramble the skeleton.
    for (const auto& inc : bundle.include_models) {
        if (inc.mdl.empty()) continue;

        const ByteReader inc_mdl(inc.mdl);
        const auto inc_hdr = inc_mdl.array_at<SourceStudioHeader>(0, 1);
        if (inc_hdr.empty()) continue;
        if (std::memcmp(inc_hdr[0].magic, kSourceMdlMagic.data(), 4) != 0) continue;

        ir::IRSkeletonData inc_skel;
        fill_skeleton(inc_mdl, inc_hdr.data(), inc_skel);
        if (inc_skel.bones.empty()) continue;

        const auto inc_bones = inc_mdl.array_at<SourceStudioBone>(
            static_cast<size_t>(inc_hdr[0].bone_index),
            static_cast<size_t>(inc_hdr[0].num_bones));
        if (inc_bones.empty()) continue;

        // Animations borrowed from the shared model are keyed by bone NAME, same as the
        // poses below, so they need no remapping: a track naming a bone this skeleton does
        // not have simply resolves to nothing.
        extract_poses(inc_mdl, ByteReader(inc.ani), inc_hdr.data(), inc_bones, inc_skel,
                      poses, bundle.animate, &result.animations);

        for (const auto& src_pose : inc_skel.poses) {
            ir::IRPose mapped;
            mapped.name = src_pose.name;
            if (poses == PoseDetail::NamesOnly) {
                // Nothing to remap: the borrowed pose carries no transforms either.
                result.skeleton_data.poses.push_back(std::move(mapped));
                continue;
            }
            mapped.positions.reserve(result.skeleton_data.bones.size());
            mapped.rotations.reserve(result.skeleton_data.bones.size());

            for (const auto& bone : result.skeleton_data.bones) {
                const int32_t src = inc_skel.find_bone(bone.name);
                if (src >= 0 && static_cast<size_t>(src) < src_pose.positions.size()) {
                    mapped.positions.push_back(src_pose.positions[static_cast<size_t>(src)]);
                    mapped.rotations.push_back(src_pose.rotations[static_cast<size_t>(src)]);
                } else {
                    // Bone the animation model does not know: keep our rest transform.
                    mapped.positions.push_back(bone.position);
                    mapped.rotations.push_back(bone.rotation);
                }
            }
            result.skeleton_data.poses.push_back(std::move(mapped));
        }
    }

    // Without the companions there is nothing more to recover: a .mdl has no vertices.
    if (bundle.vvd.empty() || bundle.vtx.empty()) {
        return result;
    }

    // ------------------------------------------------------------------ .vvd ----
    auto vertices_res = VVDParser::load_vertices(bundle.vvd, 0);
    if (!vertices_res.has_value()) {
        return std::unexpected(SourceMDLParseError::CorruptedData);
    }
    const std::vector<VVDVertex>& vertices = vertices_res.value();
    if (vertices.empty()) {
        return result;
    }

    // The three files are only consistent as a set; a mismatched checksum means the
    // caller paired a .mdl with someone else's .vvd/.vtx.
    if (auto vvd_sum = VVDParser::read_checksum(bundle.vvd);
        vvd_sum.has_value() && *vvd_sum != header->checksum) {
        return std::unexpected(SourceMDLParseError::ChecksumMismatch);
    }

    // ------------------------------------------------------------------ .vtx ----
    const ByteReader vtx(bundle.vtx);
    const auto vtx_header_span = vtx.array_at<VTXHeader>(0, 1);
    if (vtx_header_span.empty()) {
        return std::unexpected(SourceMDLParseError::CorruptedData);
    }
    const VTXHeader* vtx_header = vtx_header_span.data();
    if (vtx_header->version != kVtxVersion) {
        return std::unexpected(SourceMDLParseError::VersionMismatch);
    }
    if (vtx_header->checksum != header->checksum) {
        return std::unexpected(SourceMDLParseError::ChecksumMismatch);
    }

    // ------------------------------------------------- material search paths ----
    //
    // The other half of a material reference. A model states the names of its materials in
    // one table and the directories they live in in another, and both are needed to find a
    // file: "police" plus "models/player/police/" is a path, and "police" on its own is a
    // guess.
    if (header->num_cdtextures > 0 && header->cdtexture_index > 0) {
        const auto dirs = mdl.array_at<int32_t>(static_cast<size_t>(header->cdtexture_index),
                                                static_cast<size_t>(header->num_cdtextures));
        for (const int32_t offset : dirs) {
            if (offset <= 0) continue;
            auto dir = mdl.cstr_at(static_cast<size_t>(offset));
            if (!dir.has_value() || dir->empty()) continue;

            std::string path = std::move(*dir);
            std::replace(path.begin(), path.end(), '\\', '/');
            std::transform(path.begin(), path.end(), path.begin(),
                           [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
            if (!path.ends_with('/')) path.push_back('/');
            result.mesh_data.material_search_paths.push_back(std::move(path));
        }
    }

    // -------------------------------------------------------- material names ----
    std::vector<std::string> material_names;
    if (header->num_textures > 0 && header->texture_index > 0) {
        const auto textures = mdl.array_at<SourceTexture>(
            static_cast<size_t>(header->texture_index), static_cast<size_t>(header->num_textures));

        material_names.reserve(textures.size());
        for (size_t i = 0; i < textures.size(); ++i) {
            const size_t base = static_cast<size_t>(header->texture_index) +
                                i * sizeof(SourceTexture);
            std::string name;
            if (textures[i].name_index > 0 && base <= mdl.size() &&
                static_cast<size_t>(textures[i].name_index) <= mdl.size() - base) {
                if (auto n = mdl.cstr_at(base + static_cast<size_t>(textures[i].name_index))) {
                    name = std::move(*n);
                }
            }
            material_names.push_back(name.empty() ? ("material_" + std::to_string(i)) : name);
        }
    }

    // ------------------------------------------------- body parts / geometry ----
    if (header->num_bodyparts <= 0 || header->bodypart_index <= 0 ||
        mdl.array_at<SourceBodyPart>(static_cast<size_t>(header->bodypart_index),
                                     static_cast<size_t>(header->num_bodyparts)).empty()) {
        return result;
    }
    if (vtx_header->num_body_parts <= 0) {
        return result;
    }

    std::unordered_map<int32_t, ir::IRSurface> surface_map;

    const int32_t body_part_count = header->num_bodyparts < vtx_header->num_body_parts
                                  ? header->num_bodyparts : vtx_header->num_body_parts;

    for (int32_t bp = 0; bp < body_part_count; ++bp) {
        const size_t mdl_bp_ofs = static_cast<size_t>(header->bodypart_index) +
                                  static_cast<size_t>(bp) * sizeof(SourceBodyPart);
        const auto mdl_bp_span = mdl.array_at<SourceBodyPart>(mdl_bp_ofs, 1);
        if (mdl_bp_span.empty()) continue;
        const SourceBodyPart* mdl_bp = mdl_bp_span.data();

        if (vtx_header->body_part_offset < 0) continue;
        const size_t vtx_bp_ofs = static_cast<size_t>(vtx_header->body_part_offset) +
                                  static_cast<size_t>(bp) * sizeof(VTXBodyPart);
        const auto vtx_bp_span = vtx.array_at<VTXBodyPart>(vtx_bp_ofs, 1);
        if (vtx_bp_span.empty()) continue;
        const VTXBodyPart* vtx_bp = vtx_bp_span.data();

        // Only the first model of a body part is imported; the rest are runtime
        // alternates (weapon variants and similar) and stacking them would overlap.
        if (mdl_bp->num_models <= 0 || vtx_bp->num_models <= 0) continue;

        const auto* mdl_model = at_relative<SourceModel>(mdl, mdl_bp_ofs, mdl_bp->model_index);
        const auto* vtx_model = at_relative<VTXModel>(vtx, vtx_bp_ofs, vtx_bp->model_offset);
        if (!mdl_model || !vtx_model) continue;

        // LOD 0 is the highest detail level.
        if (vtx_model->num_lods <= 0) continue;
        const size_t vtx_model_ofs = vtx_bp_ofs + static_cast<size_t>(vtx_bp->model_offset);
        const auto* vtx_lod = at_relative<VTXModelLOD>(vtx, vtx_model_ofs, vtx_model->lod_offset);
        if (!vtx_lod) continue;

        if (mdl_model->num_meshes <= 0 || vtx_lod->num_meshes <= 0) continue;

        const size_t mdl_model_ofs = mdl_bp_ofs + static_cast<size_t>(mdl_bp->model_index);
        const size_t vtx_lod_ofs = vtx_model_ofs + static_cast<size_t>(vtx_model->lod_offset);

        // A model's vertices start at a BYTE offset into the .vvd array.
        const size_t model_vertex_base =
            static_cast<size_t>(mdl_model->vertex_index) / sizeof(VVDVertex);

        const int32_t mesh_count = mdl_model->num_meshes < vtx_lod->num_meshes
                                 ? mdl_model->num_meshes : vtx_lod->num_meshes;

        for (int32_t mi = 0; mi < mesh_count; ++mi) {
            const auto* mdl_mesh = at_relative<SourceMesh>(
                mdl, mdl_model_ofs, mdl_model->mesh_index + static_cast<int32_t>(mi * sizeof(SourceMesh)));
            const auto* vtx_mesh = at_relative<VTXMesh>(
                vtx, vtx_lod_ofs, vtx_lod->mesh_offset + static_cast<int32_t>(mi * sizeof(VTXMesh)));
            if (!mdl_mesh || !vtx_mesh) continue;

            const int32_t material = mdl_mesh->material;
            auto& surf = surface_map[material];
            if (surf.material_name.empty()) {
                surf.material_name =
                    (material >= 0 && static_cast<size_t>(material) < material_names.size())
                        ? material_names[material]
                        : ("material_" + std::to_string(material));
            }

            const size_t vtx_mesh_ofs = vtx_lod_ofs + static_cast<size_t>(vtx_lod->mesh_offset) +
                                        static_cast<size_t>(mi) * sizeof(VTXMesh);

            // Absolute index of this mesh's first vertex in the flattened .vvd array.
            const size_t mesh_vertex_base = model_vertex_base +
                                            static_cast<size_t>(mdl_mesh->vertex_offset);

            for (int32_t sg = 0; sg < vtx_mesh->num_strip_groups; ++sg) {
                const auto* group = at_relative<VTXStripGroup>(
                    vtx, vtx_mesh_ofs,
                    vtx_mesh->strip_group_header_offset + static_cast<int32_t>(sg * sizeof(VTXStripGroup)));
                if (!group) continue;

                const size_t group_ofs = vtx_mesh_ofs +
                                         static_cast<size_t>(vtx_mesh->strip_group_header_offset) +
                                         static_cast<size_t>(sg) * sizeof(VTXStripGroup);

                if (group->num_verts <= 0 || group->num_indices <= 0) continue;

                const auto* group_verts = at_relative<VTXVertex>(
                    vtx, group_ofs, group->vert_offset, static_cast<size_t>(group->num_verts));
                const auto* group_indices = at_relative<uint16_t>(
                    vtx, group_ofs, group->index_offset, static_cast<size_t>(group->num_indices));
                if (!group_verts || !group_indices) continue;

                // Map a strip-group vertex onto the surface, reusing it if already added.
                std::unordered_map<uint16_t, uint32_t> emitted;
                auto emit_vertex = [&](uint16_t group_vert_idx) -> int64_t {
                    if (group_vert_idx >= group->num_verts) return -1;

                    if (auto it = emitted.find(group_vert_idx); it != emitted.end()) {
                        return it->second;
                    }

                    // Three hops: strip index -> strip-group Vertex_t -> the mesh's
                    // vertex range in the flattened .vvd array.
                    const size_t vvd_index =
                        mesh_vertex_base + group_verts[group_vert_idx].orig_mesh_vert_id;
                    if (vvd_index >= vertices.size()) return -1;

                    const VVDVertex& v = vertices[vvd_index];
                    const uint32_t out_index = static_cast<uint32_t>(surf.positions.size());

                    // Source already stores vertices in model space; unlike GoldSrc no
                    // bone transform has to be applied here.
                    surf.positions.push_back(math::source_to_godot(
                        godot::Vector3(v.position[0], v.position[1], v.position[2])));
                    surf.normals.push_back(math::transform_normal_zup_to_yup(
                        godot::Vector3(v.normal[0], v.normal[1], v.normal[2])));
                    surf.uv0.emplace_back(v.tex_coord[0], v.tex_coord[1]);

                    std::array<int32_t, 4> bone_idx{0, 0, 0, 0};
                    std::array<float, 4> bone_w{0.0f, 0.0f, 0.0f, 0.0f};
                    const uint8_t influences = v.bone_weights.num_bones <= 3
                                             ? v.bone_weights.num_bones : uint8_t{3};
                    for (uint8_t k = 0; k < influences; ++k) {
                        bone_idx[k] = static_cast<int32_t>(v.bone_weights.bone[k]);
                        bone_w[k] = v.bone_weights.weight[k];
                    }
                    if (influences == 0) bone_w[0] = 1.0f;
                    surf.bone_indices.push_back(bone_idx);
                    surf.bone_weights.push_back(bone_w);

                    emitted.emplace(group_vert_idx, out_index);
                    return out_index;
                };

                for (int32_t st = 0; st < group->num_strips; ++st) {
                    const auto* strip = at_relative<VTXStrip>(
                        vtx, group_ofs, group->strip_offset + static_cast<int32_t>(st * sizeof(VTXStrip)));
                    if (!strip || strip->num_indices < 3) continue;

                    if (strip->index_offset < 0 ||
                        static_cast<size_t>(strip->index_offset) + static_cast<size_t>(strip->num_indices)
                            > static_cast<size_t>(group->num_indices)) {
                        continue;
                    }

                    const uint16_t* strip_indices = group_indices + strip->index_offset;

                    if (strip->flags & kVtxStripIsTriStrip) {
                        for (int32_t k = 0; k + 2 < strip->num_indices; ++k) {
                            const int64_t a = emit_vertex(strip_indices[k]);
                            const int64_t b = emit_vertex(strip_indices[k + 1]);
                            const int64_t c = emit_vertex(strip_indices[k + 2]);
                            if (a < 0 || b < 0 || c < 0) continue;
                            // Strips alternate orientation; swap on odd triangles to
                            // keep the winding consistent.
                            if (k % 2 == 0) {
                                surf.indices.push_back(static_cast<uint32_t>(a));
                                surf.indices.push_back(static_cast<uint32_t>(b));
                                surf.indices.push_back(static_cast<uint32_t>(c));
                            } else {
                                surf.indices.push_back(static_cast<uint32_t>(b));
                                surf.indices.push_back(static_cast<uint32_t>(a));
                                surf.indices.push_back(static_cast<uint32_t>(c));
                            }
                        }
                    } else {
                        // Plain triangle list.
                        for (int32_t k = 0; k + 2 < strip->num_indices; k += 3) {
                            const int64_t a = emit_vertex(strip_indices[k]);
                            const int64_t b = emit_vertex(strip_indices[k + 1]);
                            const int64_t c = emit_vertex(strip_indices[k + 2]);
                            if (a < 0 || b < 0 || c < 0) continue;
                            surf.indices.push_back(static_cast<uint32_t>(a));
                            surf.indices.push_back(static_cast<uint32_t>(b));
                            surf.indices.push_back(static_cast<uint32_t>(c));
                        }
                    }
                }
            }
        }
    }

    for (auto& [material, surf] : surface_map) {
        if (surf.positions.empty() || surf.indices.empty()) continue;
        result.mesh_data.surfaces.push_back(std::move(surf));
    }

    return result;
}

} // namespace quebratsk::parsers::source1
