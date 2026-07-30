#pragma once

#include <godot_cpp/classes/node.hpp>
#include <godot_cpp/core/class_db.hpp>
#include "../vfs/vfs_manager.h"
#include <string>

namespace quebratsk::vfs {

class P2PVFSStreamer : public godot::Node {
    GDCLASS(P2PVFSStreamer, godot::Node)

protected:
    static void _bind_methods();

public:
    P2PVFSStreamer() = default;
    ~P2PVFSStreamer() override = default;

    /// Connects to a P2P / HTTP asset host and streams VFS container chunk by chunk
    bool start_streaming(const godot::String& server_url, const godot::String& vfs_mount_prefix);

    /// Get current network download progress (0.0 to 1.0)
    float get_download_progress() const;

private:
    float _download_progress = 0.0f;
};

} // namespace quebratsk::vfs
