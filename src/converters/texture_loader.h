#pragma once

#include "../core/vfs/vfs_manager.h"

#include <godot_cpp/classes/ref.hpp>
#include <godot_cpp/classes/texture2d.hpp>

#include <string>

namespace quebratsk::converters {

/// Resolves a legacy material's texture reference against the VFS and returns a Godot
/// texture.
///
/// Legacy formats store texture references as bare, extension-less path fragments
/// ("metal/metalwall001a" in a VMT, a 16-char lump name in a BSP), with no mount
/// prefix and no file extension. Resolution therefore has to search the VFS index by
/// suffix rather than look up an exact key.
///
/// MAIN THREAD ONLY: decoded pixels become an ImageTexture, which touches the
/// RenderingServer.
class TextureLoader {
public:
    explicit TextureLoader(vfs::VFSManager* vfs) : _vfs(vfs) {}

    /// Load the texture named by a VMT-style reference. Returns an empty Ref when the
    /// texture cannot be found or decoded; failures are cached so a missing texture is
    /// only searched for once.
    [[nodiscard]] godot::Ref<godot::Texture2D> load(const std::string& texture_ref);

    [[nodiscard]] bool is_valid() const { return _vfs != nullptr; }

private:
    vfs::VFSManager* _vfs = nullptr;
};

} // namespace quebratsk::converters
