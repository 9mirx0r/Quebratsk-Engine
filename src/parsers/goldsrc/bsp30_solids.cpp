#include "bsp30_solids.h"

#include "../../core/math/axis_remap.h"

#include <algorithm>
#include <cmath>
#include <cstdint>

namespace quebratsk::parsers::goldsrc {

namespace {

constexpr int32_t kHoldsNothing = -1;
constexpr int32_t kHoldsSolid = -2;
constexpr int32_t kHoldsSky = -6;

/// A point this close to a plane (map units) is on it. Map vertices sit on a grid of whole
/// units and planes are stored as floats, so real distances are either far larger or noise.
constexpr double kOnPlane = 0.01;
/// Blocks thinner than this (cubic units) are slivers left by two planes that almost
/// coincide. They stop nothing and a physics engine rejects them.
constexpr double kMinVolume = 1.0;
/// Deeper than any retail map (84 levels at most) and shallow enough to recurse safely. A
/// tree that goes further has a loop in it.
constexpr int kMaxDepth = 512;

struct Point {
    double x = 0.0, y = 0.0, z = 0.0;
};

Point operator-(const Point& a, const Point& b) { return {a.x - b.x, a.y - b.y, a.z - b.z}; }
double dot(const Point& a, const Point& b) { return a.x * b.x + a.y * b.y + a.z * b.z; }
Point cross(const Point& a, const Point& b) {
    return {a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x};
}
bool same_point(const Point& a, const Point& b) {
    return std::fabs(a.x - b.x) < kOnPlane && std::fabs(a.y - b.y) < kOnPlane &&
           std::fabs(a.z - b.z) < kOnPlane;
}

/// A convex block, kept as its faces so that it can be cut again.
struct Block {
    std::vector<std::vector<Point>> faces;
    [[nodiscard]] bool empty() const { return faces.empty(); }
};

Block box_block(const double lo[3], const double hi[3]) {
    const auto corner = [&](int i) {
        return Point{(i & 1) ? hi[0] : lo[0], (i & 2) ? hi[1] : lo[1], (i & 4) ? hi[2] : lo[2]};
    };
    static constexpr int kFaces[6][4] = {{0, 2, 6, 4}, {1, 5, 7, 3}, {0, 4, 5, 1},
                                         {2, 3, 7, 6}, {0, 1, 3, 2}, {4, 6, 7, 5}};
    Block block;
    for (const auto& face : kFaces) {
        block.faces.push_back({corner(face[0]), corner(face[1]), corner(face[2]), corner(face[3])});
    }
    return block;
}

/// The part of `block` on one side of a plane: in front of it (where normal . p >= dist)
/// when `front`, behind it otherwise. Empty when nothing is left.
Block cut(const Block& block, const Point& normal, double dist, bool front) {
    const double sign = front ? 1.0 : -1.0;
    const auto side = [&](const Point& p) { return sign * (dot(normal, p) - dist); };

    bool any_kept = false;
    bool any_cut = false;
    for (const auto& face : block.faces) {
        for (const auto& p : face) {
            const double d = side(p);
            if (d > kOnPlane) any_kept = true;
            if (d < -kOnPlane) any_cut = true;
        }
    }
    if (!any_kept) return {};
    if (!any_cut) return block;

    Block out;
    std::vector<Point> rim;   // where the plane crosses the block: the outline of the new face
    const auto add_rim = [&](const Point& p) {
        for (const auto& q : rim) {
            if (same_point(p, q)) return;
        }
        rim.push_back(p);
    };

    for (const auto& face : block.faces) {
        std::vector<Point> kept;
        for (size_t i = 0; i < face.size(); ++i) {
            const Point& a = face[i];
            const Point& b = face[(i + 1) % face.size()];
            const double da = side(a);
            const double db = side(b);
            if (da >= -kOnPlane) {
                kept.push_back(a);
                if (da <= kOnPlane) add_rim(a);
            }
            if ((da > kOnPlane && db < -kOnPlane) || (da < -kOnPlane && db > kOnPlane)) {
                const double t = da / (da - db);
                const Point hit{a.x + (b.x - a.x) * t, a.y + (b.y - a.y) * t, a.z + (b.z - a.z) * t};
                kept.push_back(hit);
                add_rim(hit);
            }
        }
        if (kept.size() >= 3) out.faces.push_back(std::move(kept));
    }

    // Close the hole the cut left. The rim of a convex block is a convex outline, so its
    // points can be put in order by their angle around their own middle.
    if (rim.size() >= 3) {
        Point middle;
        for (const auto& p : rim) {
            middle.x += p.x;
            middle.y += p.y;
            middle.z += p.z;
        }
        const double inv = 1.0 / static_cast<double>(rim.size());
        middle = {middle.x * inv, middle.y * inv, middle.z * inv};

        // Two directions lying in the plane.
        const Point helper = std::fabs(normal.x) < 0.9 ? Point{1, 0, 0} : Point{0, 1, 0};
        const Point u = cross(normal, helper);
        const Point v = cross(normal, u);
        std::sort(rim.begin(), rim.end(), [&](const Point& a, const Point& b) {
            const Point ra = a - middle;
            const Point rb = b - middle;
            return std::atan2(dot(ra, v), dot(ra, u)) < std::atan2(dot(rb, v), dot(rb, u));
        });
        out.faces.push_back(std::move(rim));
    }
    return out;
}

/// A tree in the one form both kinds reduce to: a plane and two children, where a child
/// below zero is what fills that side (the contents value itself) and anything else is the
/// index of another node.
struct TreeNode {
    int32_t plane = -1;
    int32_t children[2] = {kHoldsNothing, kHoldsNothing};
};

class SolidBuilder {
public:
    SolidBuilder(std::vector<TreeNode> nodes, std::span<const BSPPlane> planes)
        : m_nodes(std::move(nodes)), m_planes(planes), m_kind(m_nodes.size(), Kind::Unknown) {}

    std::vector<ir::IRConvexHull> take() { return std::move(m_out); }

    void descend(int32_t child, const Block& block, int depth) {
        if (block.empty()) return;
        const Kind kind = classify(child, depth);
        if (kind == Kind::Open) return;
        if (kind == Kind::Solid) {
            emit(block);
            return;
        }
        // Mixed: only a node can be, so `child` is a valid node index here.
        const TreeNode& node = m_nodes[static_cast<size_t>(child)];
        if (node.plane < 0 || static_cast<size_t>(node.plane) >= m_planes.size()) return;
        const BSPPlane& plane = m_planes[static_cast<size_t>(node.plane)];
        const Point normal{plane.normal[0], plane.normal[1], plane.normal[2]};
        descend(node.children[0], cut(block, normal, plane.distance, true), depth + 1);
        descend(node.children[1], cut(block, normal, plane.distance, false), depth + 1);
    }

private:
    enum class Kind : uint8_t { Unknown, Visiting, Solid, Open, Mixed };

    /// Whether everything below `child` is solid, nothing is, or some of it.
    Kind classify(int32_t child, int depth) {
        if (child < 0) {
            return (child == kHoldsSolid || child == kHoldsSky) ? Kind::Solid : Kind::Open;
        }
        const size_t index = static_cast<size_t>(child);
        if (index >= m_nodes.size() || depth > kMaxDepth) return Kind::Open;
        Kind& known = m_kind[index];
        if (known == Kind::Visiting) return Kind::Open;   // a loop: a corrupt tree
        if (known != Kind::Unknown) return known;

        known = Kind::Visiting;
        const Kind a = classify(m_nodes[index].children[0], depth + 1);
        const Kind b = classify(m_nodes[index].children[1], depth + 1);
        const Kind result = (a == b && a != Kind::Mixed) ? a : Kind::Mixed;
        m_kind[index] = result;
        return result;
    }

    void emit(const Block& block) {
        std::vector<Point> corners;
        for (const auto& face : block.faces) {
            for (const auto& p : face) {
                const bool seen = std::any_of(corners.begin(), corners.end(),
                                              [&](const Point& q) { return same_point(p, q); });
                if (!seen) corners.push_back(p);
            }
        }
        if (corners.size() < 4) return;

        // Volume as a sum of pyramids from the block's middle to each face.
        Point middle;
        for (const auto& p : corners) {
            middle.x += p.x;
            middle.y += p.y;
            middle.z += p.z;
        }
        const double inv = 1.0 / static_cast<double>(corners.size());
        middle = {middle.x * inv, middle.y * inv, middle.z * inv};
        double volume = 0.0;
        for (const auto& face : block.faces) {
            for (size_t i = 1; i + 1 < face.size(); ++i) {
                volume += std::fabs(dot(face[0] - middle,
                                        cross(face[i] - middle, face[i + 1] - middle))) / 6.0;
            }
        }
        if (volume < kMinVolume) return;

        ir::IRConvexHull hull;
        hull.vertices.reserve(corners.size());
        for (const auto& p : corners) {
            hull.vertices.push_back(math::source_to_godot(godot::Vector3(
                static_cast<float>(p.x), static_cast<float>(p.y), static_cast<float>(p.z))));
        }
        m_out.push_back(std::move(hull));
    }

    std::vector<TreeNode> m_nodes;
    std::span<const BSPPlane> m_planes;
    std::vector<Kind> m_kind;
    std::vector<ir::IRConvexHull> m_out;
};

std::vector<ir::IRConvexHull> build(std::vector<TreeNode> tree, std::span<const BSPPlane> planes,
                                    int32_t head, const float mins[3], const float maxs[3],
                                    float margin) {
    double lo[3];
    double hi[3];
    for (int i = 0; i < 3; ++i) {
        lo[i] = static_cast<double>(std::min(mins[i], maxs[i])) - margin;
        hi[i] = static_cast<double>(std::max(mins[i], maxs[i])) + margin;
        if (!std::isfinite(lo[i]) || !std::isfinite(hi[i]) || hi[i] - lo[i] < kOnPlane) return {};
    }
    SolidBuilder builder(std::move(tree), planes);
    builder.descend(head, box_block(lo, hi), 0);
    return builder.take();
}

} // namespace

std::vector<ir::IRConvexHull> build_model_solids(std::span<const BSPNode> nodes,
                                                 std::span<const BSPLeaf> leaves,
                                                 std::span<const BSPPlane> planes,
                                                 int32_t head_node, const float mins[3],
                                                 const float maxs[3], float margin) {
    // A child below zero names a leaf, and the leaf says what is there.
    const auto resolve = [&](int32_t child) -> int32_t {
        if (child >= 0) return child;
        const size_t leaf = static_cast<size_t>(-1 - child);
        if (leaf >= leaves.size()) return kHoldsNothing;
        const int32_t contents = leaves[leaf].contents;
        return contents < 0 ? contents : kHoldsNothing;
    };
    std::vector<TreeNode> tree(nodes.size());
    for (size_t i = 0; i < nodes.size(); ++i) {
        tree[i].plane = nodes[i].planenum;
        tree[i].children[0] = resolve(nodes[i].children[0]);
        tree[i].children[1] = resolve(nodes[i].children[1]);
    }
    return build(std::move(tree), planes, resolve(head_node), mins, maxs, margin);
}

std::vector<ir::IRConvexHull> build_clip_solids(std::span<const BSPClipNode> clipnodes,
                                                std::span<const BSPPlane> planes,
                                                int32_t head_node, const float mins[3],
                                                const float maxs[3], float margin) {
    // Here a child below zero already is the contents.
    std::vector<TreeNode> tree(clipnodes.size());
    for (size_t i = 0; i < clipnodes.size(); ++i) {
        tree[i].plane = clipnodes[i].plane_index;
        tree[i].children[0] = clipnodes[i].children[0];
        tree[i].children[1] = clipnodes[i].children[1];
    }
    return build(std::move(tree), planes, head_node, mins, maxs, margin);
}

} // namespace quebratsk::parsers::goldsrc
