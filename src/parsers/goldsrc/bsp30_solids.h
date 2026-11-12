#pragma once

#include "../../core/ir/ir_collision_data.h"
#include "structs/bsp30_structs.h"

#include <span>
#include <vector>

namespace quebratsk::parsers::goldsrc {

/// The solid parts of one brush model, as convex blocks.
///
/// A GoldSrc map does not store its walls as shapes. It stores a tree: every node is a plane
/// that cuts space in two, and at the end of every branch is a leaf that says what fills
/// that piece of space: nothing, water, or solid. Follow a branch to a solid leaf and the
/// planes passed on the way are the sides of one convex block of solid. All the blocks
/// together are exactly what the level is made of, including what cannot be seen: the
/// thickness behind a wall, a face the compiler removed because nobody could look at it,
/// the box of sky around an outdoor level.
///
/// That is what a collider built from the visible triangles cannot give. Triangles are
/// one-sided sheets, and only where something is drawn: nothing stops a body at a sky
/// surface or from the back of a wall. A block is solid whichever way it is approached.
///
/// How a block is found: start from the model's bounding box and, going down the tree, cut
/// it by each plane in turn, keeping the side the branch leads into. Cutting a box by a plane
/// costs the faces it crosses, so a whole map takes milliseconds. Where everything below a
/// node is solid the box is kept whole instead of being cut further, which gives fewer,
/// larger blocks.
///
/// Sky counts as solid here. The game's own traces let a point pass into a sky volume and
/// leave what happens next to its movement code; a Godot body that got in would simply fall
/// out of the level.
///
/// NOT covered: clip brushes, the invisible walls a mapper places to keep players off a
/// ledge or out of a gap. They are absent from this tree. They exist only in the three other
/// trees of the map (lump 9), which hold the level already fattened by the size of a
/// standing, crouching or large character and cannot be turned back into plain shapes. See
/// build_clip_solids() for those.
///
/// `mins` and `maxs` are the model's box from lump 14, in map units; `margin` widens it, so
/// that the solid behind the world's outermost walls keeps some thickness. The returned
/// vertices are in Godot's axes and metres, like the map's mesh.
[[nodiscard]] std::vector<ir::IRConvexHull> build_model_solids(
    std::span<const BSPNode> nodes,
    std::span<const BSPLeaf> leaves,
    std::span<const BSPPlane> planes,
    int32_t head_node,
    const float mins[3],
    const float maxs[3],
    float margin);

/// The same, for one of the map's player trees (lump 9), which is where clip brushes live.
///
/// The game never asks whether a player-shaped box fits somewhere. Its compiler fattened
/// every solid by half a player in advance and stored the result as its own tree, and the
/// game moves a single point through that. Tree 1 is fattened for a standing player (16
/// units to each side, 36 up and down), tree 2 for a large creature (32 each way), tree 3
/// for a crouching player (16 to each side, 18 up and down).
///
/// So these blocks are NOT the level at its real size, and are of no use against an
/// ordinary body: a wall here starts 16 units before the wall that is drawn. They are right
/// for exactly one thing, which is the thing the game does: testing the point at the middle
/// of a character of that size. Used that way they include every clip brush.
///
/// `head_node` is the model's head for the tree wanted (BSPModel::head_nodes[1..3]). The
/// box should be widened by at least the fattening above, since the solid reaches that far
/// past the model's own.
[[nodiscard]] std::vector<ir::IRConvexHull> build_clip_solids(
    std::span<const BSPClipNode> clipnodes,
    std::span<const BSPPlane> planes,
    int32_t head_node,
    const float mins[3],
    const float maxs[3],
    float margin);

} // namespace quebratsk::parsers::goldsrc
