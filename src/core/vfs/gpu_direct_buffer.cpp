#include "gpu_direct_buffer.h"

#include <godot_cpp/core/error_macros.hpp>

namespace quebratsk::vfs {

using namespace godot;

RID GPUDirectBuffer::create_mesh_surface_from_mmap(
    std::span<const std::byte> mmap_vertex_data,
    std::span<const std::byte> mmap_index_data,
    uint32_t vertex_count,
    uint32_t index_count
) {
    // NOT IMPLEMENTED. The previous body called rs->mesh_create() and returned the RID
    // without ever adding a surface or freeing it, so every call leaked a GPU resource.
    // Until the zero-copy surface upload exists, allocate nothing.
    //
    // TODO: alias a PackedByteArray onto mmap_vertex_data and feed it to
    // RenderingServer::mesh_add_surface() to achieve the intended zero-copy path.
    ERR_PRINT_ONCE("[GPUDirectBuffer] Zero-copy mesh upload is not implemented; "
                   "use converters::MeshConverter instead.");
    return RID();
}

} // namespace quebratsk::vfs
