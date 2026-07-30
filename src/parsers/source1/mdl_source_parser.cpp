#include "mdl_source_parser.h"
#include "../../core/math/axis_remap.h"
#include <cstring>

namespace quebratsk::parsers::source1 {

std::expected<ParsedSourceMDLModel, SourceMDLParseError> SourceMDLParser::parse(
    std::span<const std::byte> mdl_bytes
) {
    if (mdl_bytes.size() < sizeof(SourceStudioHeader)) {
        return std::unexpected(SourceMDLParseError::InvalidHeader);
    }

    auto* header = reinterpret_cast<const SourceStudioHeader*>(mdl_bytes.data());
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
    result.mesh_data.name = std::string(header->name, strnlen(header->name, 64));

    result.skeleton_data.source_engine = ir::SourceEngine::Source1;
    result.skeleton_data.name = result.mesh_data.name;

    // Parse Bone Hierarchy
    if (header->num_bones > 0 && header->bone_index > 0 &&
        static_cast<size_t>(header->bone_index) + header->num_bones * sizeof(SourceStudioBone) <= mdl_bytes.size()) {

        auto* bones = reinterpret_cast<const SourceStudioBone*>(mdl_bytes.data() + header->bone_index);
        for (int32_t i = 0; i < header->num_bones; ++i) {
            const auto& b = bones[i];
            ir::IRBone ir_b;

            // name_index is relative to the start of this bone record. Validating only
            // the start offset is not enough: std::string(const char*) runs to the first
            // NUL, which may lie past the end of the mapping. Bound the scan explicitly.
            const size_t bone_base = static_cast<size_t>(header->bone_index) +
                                     static_cast<size_t>(i) * sizeof(SourceStudioBone);
            bool named = false;

            if (b.name_index > 0 && bone_base < mdl_bytes.size() &&
                static_cast<size_t>(b.name_index) < mdl_bytes.size() - bone_base) {
                const size_t name_ofs = bone_base + static_cast<size_t>(b.name_index);
                const char* base = reinterpret_cast<const char*>(mdl_bytes.data());
                const size_t max_len = mdl_bytes.size() - name_ofs;
                const size_t len = ::strnlen(base + name_ofs, max_len);
                if (len < max_len) { // a real terminator was found inside the buffer
                    ir_b.name.assign(base + name_ofs, len);
                    named = true;
                }
            }

            if (!named) {
                ir_b.name = "ValveBiped.Bip01_Bone_" + std::to_string(i);
            }

            ir_b.parent_index = b.parent;

            godot::Vector3 pos_raw(b.pos[0], b.pos[1], b.pos[2]);
            ir_b.position = math::source_to_godot(pos_raw);

            godot::Quaternion rot_raw(b.rot[0], b.rot[1], b.rot[2], b.rot[3]);
            ir_b.rotation = math::source_quat_to_godot(rot_raw);

            result.skeleton_data.bones.push_back(ir_b);
        }
    }

    return result;
}

} // namespace quebratsk::parsers::source1
