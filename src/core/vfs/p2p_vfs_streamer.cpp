#include "p2p_vfs_streamer.h"
#include <godot_cpp/variant/utility_functions.hpp>

namespace quebratsk::vfs {

using namespace godot;

void P2PVFSStreamer::_bind_methods() {
    ClassDB::bind_method(D_METHOD("start_streaming", "server_url", "vfs_mount_prefix"), &P2PVFSStreamer::start_streaming);
    ClassDB::bind_method(D_METHOD("get_download_progress"), &P2PVFSStreamer::get_download_progress);
}

bool P2PVFSStreamer::start_streaming(const String& server_url, const String& vfs_mount_prefix) {
    if (server_url.is_empty() || vfs_mount_prefix.is_empty()) return false;

    UtilityFunctions::print("[P2PVFSStreamer] Initiating chunked P2P network stream from: ", server_url, " to vfs://", vfs_mount_prefix);
    _download_progress = 1.0f; // Mock stream completion
    return true;
}

float P2PVFSStreamer::get_download_progress() const {
    return _download_progress;
}

} // namespace quebratsk::vfs
