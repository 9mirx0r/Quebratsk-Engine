#include "mdl10_parser.h"
#include "../../core/math/axis_remap.h"

#include <cstring>

namespace quebratsk::parsers::goldsrc {

std::expected<ParsedMDL10Model, MDL10ParseError> MDL10Parser::parse(
    std::span<const std::byte> mdl_bytes
) {
    if (mdl_bytes.size() < sizeof(StudioHeader)) {
        return std::unexpected(MDL10ParseError::InvalidHeader);
    }

    auto* header = reinterpret_cast<const StudioHeader*>(mdl_bytes.data());
    if (std::memcmp(header->magic, kMdl10Magic.data(), 4) != 0 || header->version != kMdl10Version) {
        return std::unexpected(MDL10ParseError::VersionMismatch);
    }

    ParsedMDL10Model result;
    result.mesh_data.source_engine = ir::SourceEngine::GoldSrc;
    result.mesh_data.name = std::string(header->name, strnlen(header->name, 64));

    result.skeleton_data.source_engine = ir::SourceEngine::GoldSrc;
    result.skeleton_data.name = result.mesh_data.name;

    // Parse Skeleton Bones
    if (header->num_bones > 0 && header->bone_index > 0 &&
        static_cast<size_t>(header->bone_index) + header->num_bones * sizeof(StudioBone) <= mdl_bytes.size()) {

        auto* bones = reinterpret_cast<const StudioBone*>(mdl_bytes.data() + header->bone_index);
        for (int32_t i = 0; i < header->num_bones; ++i) {
            const auto& b = bones[i];
            ir::IRBone ir_b;
            ir_b.name = std::string(b.name, strnlen(b.name, 32));
            ir_b.parent_index = b.parent;

            godot::Vector3 pos_raw(b.value[0], b.value[1], b.value[2]);
            ir_b.position = math::source_to_godot(pos_raw);

            // StudioMDL stores XYZ Euler angles in radians, applied X then Y then Z in
            // the model's own Z-up frame. Godot's Quaternion(Vector3) constructor uses
            // YXZ order, so feeding it these values silently produced wrong poses as
            // soon as more than one axis was non-zero. Compose the rotation explicitly
            // in the source frame, then change basis.
            const godot::Quaternion qx(godot::Vector3(1, 0, 0), b.value[3]);
            const godot::Quaternion qy(godot::Vector3(0, 1, 0), b.value[4]);
            const godot::Quaternion qz(godot::Vector3(0, 0, 1), b.value[5]);
            ir_b.rotation = math::source_quat_to_godot(qz * qy * qx);

            result.skeleton_data.bones.push_back(ir_b);
        }
    }

    return result;
}

} // namespace quebratsk::parsers::goldsrc
