#pragma once

#include "../vfs/vfs_manager.h"

#include <godot_cpp/classes/rendering_server.hpp>
#include <godot_cpp/classes/ref.hpp>
#include <godot_cpp/variant/rid.hpp>

#include <span>

namespace quebratsk::vfs {

class GPUDirectBuffer {
public:
    /// Create Vulkan/DirectX staging buffer directly from memory-mapped bytes (Zero-Copy CPU allocation)
    [[nodiscard]] static godot::RID create_mesh_surface_from_mmap(
        std::span<const std::byte> mmap_vertex_data,
        std::span<const std::byte> mmap_index_data,
        uint32_t vertex_count,
        uint32_t index_count
    );
};

} // namespace quebratsk::vfs
