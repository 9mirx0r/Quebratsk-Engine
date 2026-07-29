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

            if (b.name_index > 0 && static_cast<size_t>(header->bone_index + i * sizeof(SourceStudioBone) + b.name_index) < mdl_bytes.size()) {
                const char* name_ptr = reinterpret_cast<const char*>(mdl_bytes.data() + header->bone_index + i * sizeof(SourceStudioBone) + b.name_index);
                ir_b.name = std::string(name_ptr);
            } else {
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
