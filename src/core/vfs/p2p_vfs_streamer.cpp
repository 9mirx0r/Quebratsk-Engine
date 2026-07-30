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

    // NOT IMPLEMENTED. There is no networking in this class at all: the previous body
    // logged a message and set progress to 1.0, so callers saw a completed download
    // that never happened.
    UtilityFunctions::push_error(
        "[P2PVFSStreamer] start_streaming() is not implemented; no data is transferred.");
    _download_progress = 0.0f;
    return false;
}

float P2PVFSStreamer::get_download_progress() const {
    return _download_progress;
}

} // namespace quebratsk::vfs
