#pragma once

#include "../core/vfs/vfs_manager.h"

#include <godot_cpp/classes/ref.hpp>
#include <godot_cpp/classes/texture2d.hpp>

#include <string>
#include <vector>

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

    /// Where the asset being loaded says its materials live, e.g. "models/player/police/".
    ///
    /// Set this before loading a model's surfaces and the name from the model is joined to
    /// the directory from the model, which is what the format intends. Without it the only
    /// option is searching the index for anything ending in the bare name, which is how a
    /// model ends up wearing another model's texture, or none.
    void set_search_paths(const std::vector<std::string>& paths) { _search_paths = paths; }

    /// Material references that were looked for and not found, in the order they came up.
    /// A model that renders flat and grey has this list to explain why.
    [[nodiscard]] const std::vector<std::string>& missing() const { return _missing; }

    [[nodiscard]] bool is_valid() const { return _vfs != nullptr; }

private:
    vfs::VFSManager* _vfs = nullptr;
    std::vector<std::string> _search_paths;
    std::vector<std::string> _missing;
};

} // namespace quebratsk::converters
