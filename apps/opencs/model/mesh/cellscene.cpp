#include "cellscene.hpp"

#include <algorithm>
#include <cmath>
#include <string_view>

namespace CSMMesh
{
    namespace sdkgame = openeden::sdk::game;

    namespace
    {
        // The sign of each Euler angle (x, y, z) of a placement. Negative: the Gamebryo family turns objects clockwise
        // for a positive angle, as Eden Studio's own cell editor does for Morrowind (InstanceMath::eulerToQuat). Z was
        // checked by frame on a furnished interior (the shell's wall pieces close with -1 and open up with +1, task
        // 0086 stage log). X and Y were settled by terrain: placements on sloped Tamriel ground align with its normal
        // best with all three negated and X applied first (stage log, rotation convention).
        constexpr double kAngleSign[3] = { -1.0, -1.0, -1.0 };

        // Beyond this a placement is not a Skyrim world (about 1e5 units across), so it is refused as hostile.
        constexpr double kLimit = 1.0e7;
        constexpr double kMaxScale = 1.0e3;

        bool usable(double v)
        {
            return std::isfinite(v) && std::abs(v) <= kLimit;
        }

        // How much of the base kind's tint a shape keeps: a flat grey takes a good deal of it (it is how the kinds are
        // told apart), a shape with its texture's own colour only a trace.
        constexpr float kTintShare = 0.45f;
        constexpr float kTintShareTextured = 0.12f;

        std::array<float, 4> shade(std::array<float, 4> colour, const std::array<float, 4>& tint, bool textured)
        {
            const float share = textured ? kTintShareTextured : kTintShare;
            for (int i = 0; i < 3; ++i)
                colour[i] = colour[i] * (1.f - share) + tint[i] * share;
            colour[3] = 1.f;
            return colour;
        }

        std::array<double, 3> apply(const std::array<double, 16>& m, const std::array<float, 3>& p)
        {
            return { m[0] * p[0] + m[4] * p[1] + m[8] * p[2] + m[12], m[1] * p[0] + m[5] * p[1] + m[9] * p[2] + m[13],
                m[2] * p[0] + m[6] * p[1] + m[10] * p[2] + m[14] };
        }

        // The rotation part applied to a direction, then normalised (a uniform scale does not change a direction).
        std::array<float, 3> applyDirection(const std::array<double, 16>& m, const std::array<float, 3>& n)
        {
            std::array<double, 3> r = { m[0] * n[0] + m[4] * n[1] + m[8] * n[2],
                m[1] * n[0] + m[5] * n[1] + m[9] * n[2], m[2] * n[0] + m[6] * n[1] + m[10] * n[2] };
            const double length = std::sqrt(r[0] * r[0] + r[1] * r[1] + r[2] * r[2]);
            if (length > 0.0)
                for (double& v : r)
                    v /= length;
            return { static_cast<float>(r[0]), static_cast<float>(r[1]), static_cast<float>(r[2]) };
        }

        // One object as the builder needs it, whichever model it came from (the WorldView content or the neutral
        // WorldCell): the checks are the same for both, so they are written once.
        struct Item
        {
            bool initiallyDisabled = false;
            std::string_view modelPath;
            std::string_view name;
            sdkgame::BaseKind kind = sdkgame::BaseKind::Unknown;
            bool hasBounds = false;
            float boundsMin[3] = { 0.f, 0.f, 0.f };
            float boundsMax[3] = { 0.f, 0.f, 0.f };
            bool placementOk = false;
            bool boundsOk = false;
            double scale = 1.0;
            std::array<double, 16> world{};
        };

        void addNode(CellScene& scene, int parent, std::string name, const char* type,
            const std::array<double, 16>& world, double scale, int shape, std::uint32_t vertices,
            std::uint32_t triangles)
        {
            SceneModelNode node;
            node.name = std::move(name);
            node.type = type;
            node.parent = parent;
            node.translation = { world[12], world[13], world[14] };
            node.scale = { scale, scale, scale };
            node.world = world;
            node.shape = shape;
            node.vertexCount = vertices;
            node.triangleCount = triangles;
            scene.model.nodes.push_back(std::move(node));
        }

        // A box of the bounds, placed by `world`. Outward counter-clockwise triangles, per-face normals.
        SceneModelShape makeBox(const Item& o, const std::array<double, 16>& world, int node)
        {
            const float lo[3] = { std::min(o.boundsMin[0], o.boundsMax[0]), std::min(o.boundsMin[1], o.boundsMax[1]),
                std::min(o.boundsMin[2], o.boundsMax[2]) };
            const float hi[3] = { std::max(o.boundsMin[0], o.boundsMax[0]), std::max(o.boundsMin[1], o.boundsMax[1]),
                std::max(o.boundsMin[2], o.boundsMax[2]) };
            // Six faces, each four corners (counter-clockwise seen from outside) and its outward normal.
            struct Face
            {
                int corner[4][3]; // 0 = lo, 1 = hi per axis
                float normal[3];
            };
            static const Face faces[6] = {
                { { { 1, 0, 0 }, { 1, 1, 0 }, { 1, 1, 1 }, { 1, 0, 1 } }, { 1, 0, 0 } },
                { { { 0, 1, 0 }, { 0, 0, 0 }, { 0, 0, 1 }, { 0, 1, 1 } }, { -1, 0, 0 } },
                { { { 1, 1, 0 }, { 0, 1, 0 }, { 0, 1, 1 }, { 1, 1, 1 } }, { 0, 1, 0 } },
                { { { 0, 0, 0 }, { 1, 0, 0 }, { 1, 0, 1 }, { 0, 0, 1 } }, { 0, -1, 0 } },
                { { { 0, 0, 1 }, { 1, 0, 1 }, { 1, 1, 1 }, { 0, 1, 1 } }, { 0, 0, 1 } },
                { { { 0, 1, 0 }, { 1, 1, 0 }, { 1, 0, 0 }, { 0, 0, 0 } }, { 0, 0, -1 } },
            };
            SceneModelShape shape;
            shape.node = node;
            shape.colour = shade({ 0.72f, 0.72f, 0.72f, 1.f }, kindColour(o.kind), false);
            for (const Face& face : faces)
            {
                const std::uint32_t first = static_cast<std::uint32_t>(shape.mesh.positions.size());
                for (const auto& corner : face.corner)
                {
                    const std::array<float, 3> local
                        = { corner[0] ? hi[0] : lo[0], corner[1] ? hi[1] : lo[1], corner[2] ? hi[2] : lo[2] };
                    const std::array<double, 3> p = apply(world, local);
                    shape.mesh.positions.push_back(
                        { static_cast<float>(p[0]), static_cast<float>(p[1]), static_cast<float>(p[2]) });
                    shape.mesh.normals.push_back(
                        applyDirection(world, { face.normal[0], face.normal[1], face.normal[2] }));
                }
                for (const std::uint32_t i : { 0u, 1u, 2u, 0u, 2u, 3u })
                    shape.mesh.indices.push_back(first + i);
            }
            return shape;
        }
    }

    std::array<double, 16> placementMatrix(
        const sdkgame::WorldVec3& position, const sdkgame::WorldVec3& rotation, double scale)
    {
        const double ax = kAngleSign[0] * rotation.x, ay = kAngleSign[1] * rotation.y, az = kAngleSign[2] * rotation.z;
        const double cx = std::cos(ax), sx = std::sin(ax), cy = std::cos(ay), sy = std::sin(ay), cz = std::cos(az),
                     sz = std::sin(az);
        // R = Rz * Ry * Rx (a vector is rotated about X first), row-major here, written to the columns below.
        const double r00 = cz * cy, r01 = cz * sy * sx - sz * cx, r02 = cz * sy * cx + sz * sx;
        const double r10 = sz * cy, r11 = sz * sy * sx + cz * cx, r12 = sz * sy * cx - cz * sx;
        const double r20 = -sy, r21 = cy * sx, r22 = cy * cx;
        return { r00 * scale, r10 * scale, r20 * scale, 0.0, r01 * scale, r11 * scale, r21 * scale, 0.0, r02 * scale,
            r12 * scale, r22 * scale, 0.0, position.x, position.y, position.z, 1.0 };
    }

    std::array<float, 4> kindColour(sdkgame::BaseKind kind)
    {
        using K = sdkgame::BaseKind;
        switch (kind)
        {
            case K::Static:
                return { 0.62f, 0.62f, 0.66f, 1.f };
            case K::MovableStatic:
                return { 0.70f, 0.58f, 0.40f, 1.f };
            case K::Furniture:
                return { 0.80f, 0.55f, 0.25f, 1.f };
            case K::Door:
                return { 0.55f, 0.35f, 0.20f, 1.f };
            case K::Activator:
                return { 0.35f, 0.65f, 0.80f, 1.f };
            case K::Container:
                return { 0.85f, 0.75f, 0.25f, 1.f };
            case K::Flora:
                return { 0.35f, 0.70f, 0.30f, 1.f };
            case K::Tree:
                return { 0.20f, 0.55f, 0.20f, 1.f };
            case K::Light:
                return { 1.00f, 0.95f, 0.55f, 1.f };
            case K::Item:
                return { 0.85f, 0.40f, 0.60f, 1.f };
            default:
                return { 0.60f, 0.60f, 0.60f, 1.f };
        }
    }

    std::array<double, 16> placementMatrix(const sdkgame::WorldTransform& t)
    {
        double x = t.rotation.x, y = t.rotation.y, z = t.rotation.z, w = t.rotation.w;
        const double length = std::sqrt(x * x + y * y + z * z + w * w);
        if (length > 0.0)
        {
            x /= length;
            y /= length;
            z /= length;
            w /= length;
        }
        const double s = t.scale;
        const double r00 = 1 - 2 * (y * y + z * z), r01 = 2 * (x * y - z * w), r02 = 2 * (x * z + y * w);
        const double r10 = 2 * (x * y + z * w), r11 = 1 - 2 * (x * x + z * z), r12 = 2 * (y * z - x * w);
        const double r20 = 2 * (x * z - y * w), r21 = 2 * (y * z + x * w), r22 = 1 - 2 * (x * x + y * y);
        return { r00 * s, r10 * s, r20 * s, 0.0, r01 * s, r11 * s, r21 * s, 0.0, r02 * s, r12 * s, r22 * s, 0.0,
            t.position.x, t.position.y, t.position.z, 1.0 };
    }

    namespace
    {
        bool boundsUsable(const float* lo, const float* hi)
        {
            return usable(lo[0]) && usable(lo[1]) && usable(lo[2]) && usable(hi[0]) && usable(hi[1]) && usable(hi[2]);
        }

        // The one builder. `objects` is any sequence, `toItem` turns one element into an Item. `refused` objects were
        // never handed over by the source (the neutral adapter's droppedObjects): they count as objects and as invalid,
        // as the WorldView path counts an object it cannot use.
        template <class Objects, class ToItem>
        CellScene build(const std::string& cellName, const Objects& objects, std::size_t refused, ToItem&& toItem,
            const MeshProvider& provider, const CellSceneLimits& limits)
        {
            CellScene scene;
            CellSceneStats& stats = scene.stats;
            stats.objects = objects.size() + refused;
            stats.skippedInvalid = refused;
            addNode(scene, -1, cellName.empty() ? "cell" : cellName, "Transform",
                { 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1 }, 1.0, -1, 0, 0);

            std::size_t looked = 0;
            for (const auto& sourceObject : objects)
            {
                const Item o = toItem(sourceObject);
                if (looked++ >= limits.maxObjects)
                {
                    ++stats.skippedByBudget;
                    continue;
                }
                if (o.initiallyDisabled)
                {
                    ++stats.skippedDisabled;
                    continue;
                }
                const bool hasModel = !o.modelPath.empty();
                if (!hasModel && !o.hasBounds)
                {
                    ++stats.skippedNoGeometry;
                    continue;
                }
                if (!o.placementOk || !o.boundsOk)
                {
                    ++stats.skippedInvalid;
                    continue;
                }
                const std::array<double, 16>& world = o.world;

                const SceneModel* mesh = nullptr;
                if (hasModel && provider)
                {
                    mesh = provider(std::string(o.modelPath));
                    if (mesh != nullptr && mesh->shapes.empty())
                        mesh = nullptr;
                    if (mesh == nullptr)
                        ++stats.meshMisses;
                }
                else if (hasModel)
                    ++stats.meshMisses;

                std::size_t meshTriangles = 0, meshVertices = 0;
                bool meshFits = false;
                if (mesh != nullptr)
                {
                    for (const SceneModelShape& s : mesh->shapes)
                    {
                        meshVertices += s.mesh.positions.size();
                        meshTriangles += s.mesh.indices.size() / 3;
                    }
                    meshFits = meshVertices <= limits.maxVerticesPerMesh
                        && stats.triangles + meshTriangles <= limits.maxTriangles
                        && stats.vertices + meshVertices <= limits.maxVertices;
                }

                const int parent = static_cast<int>(scene.model.nodes.size());
                const std::string name = o.name.empty() ? std::string("object") : std::string(o.name);
                if (mesh != nullptr && meshFits)
                {
                    addNode(scene, 0, name, "Transform", world, o.scale, -1, 0, 0);
                    for (const SceneModelShape& source : mesh->shapes)
                    {
                        SceneModelShape shape;
                        shape.colour = shade(source.colour, kindColour(o.kind), source.textured);
                        shape.textured = source.textured;
                        shape.diffuse = source.diffuse;
                        shape.skinned = source.skinned;
                        shape.mesh.topology = source.mesh.topology;
                        shape.mesh.indices = source.mesh.indices;
                        shape.mesh.positions.reserve(source.mesh.positions.size());
                        for (const auto& p : source.mesh.positions)
                        {
                            const std::array<double, 3> w = apply(world, p);
                            shape.mesh.positions.push_back(
                                { static_cast<float>(w[0]), static_cast<float>(w[1]), static_cast<float>(w[2]) });
                        }
                        shape.mesh.normals.reserve(source.mesh.normals.size());
                        for (const auto& n : source.mesh.normals)
                            shape.mesh.normals.push_back(applyDirection(world, n));
                        shape.mesh.colours = source.mesh.colours;
                        shape.node = static_cast<int>(scene.model.nodes.size());
                        addNode(scene, parent, name, "Geometry", world, o.scale,
                            static_cast<int>(scene.model.shapes.size()),
                            static_cast<std::uint32_t>(shape.mesh.positions.size()),
                            static_cast<std::uint32_t>(shape.mesh.indices.size() / 3));
                        stats.vertices += shape.mesh.positions.size();
                        stats.triangles += shape.mesh.indices.size() / 3;
                        for (const auto& p : shape.mesh.positions)
                            expand(scene.model.bounds, { p[0], p[1], p[2] });
                        scene.model.shapes.push_back(std::move(shape));
                    }
                    ++stats.drawnAsMesh;
                    continue;
                }

                // A box, if the bounds are known and the budget allows 8 vertices and 12 triangles. A light's bounds
                // are its reach, not a thing, so a light with no model is left out.
                if (o.hasBounds && o.kind != sdkgame::BaseKind::Light && stats.triangles + 12 <= limits.maxTriangles
                    && stats.vertices + 24 <= limits.maxVertices)
                {
                    addNode(scene, 0, name, "Transform", world, o.scale, -1, 0, 0);
                    SceneModelShape shape = makeBox(o, world, static_cast<int>(scene.model.nodes.size()));
                    addNode(scene, parent, name, "Geometry", world, o.scale,
                        static_cast<int>(scene.model.shapes.size()),
                        static_cast<std::uint32_t>(shape.mesh.positions.size()), 12);
                    stats.vertices += shape.mesh.positions.size();
                    stats.triangles += 12;
                    for (const auto& p : shape.mesh.positions)
                        expand(scene.model.bounds, { p[0], p[1], p[2] });
                    scene.model.shapes.push_back(std::move(shape));
                    ++stats.drawnAsBox;
                    continue;
                }
                if (mesh != nullptr || (o.hasBounds && o.kind != sdkgame::BaseKind::Light))
                    ++stats.skippedByBudget;
                else
                    ++stats.skippedNoGeometry;
            }
            return scene;
        }
    }

    CellScene buildCellScene(
        const sdkgame::WorldCellContent& cell, const MeshProvider& provider, const CellSceneLimits& limits)
    {
        return build(
            cell.cell.editorId, cell.objects, 0,
            [](const sdkgame::PlacedObject& o) {
                Item item;
                item.initiallyDisabled = o.initiallyDisabled;
                item.modelPath = o.modelPath;
                item.name = o.baseEditorId;
                item.kind = o.baseKind;
                item.hasBounds = o.hasBounds;
                item.boundsMin[0] = o.boundsMin.x;
                item.boundsMin[1] = o.boundsMin.y;
                item.boundsMin[2] = o.boundsMin.z;
                item.boundsMax[0] = o.boundsMax.x;
                item.boundsMax[1] = o.boundsMax.y;
                item.boundsMax[2] = o.boundsMax.z;
                item.placementOk = usable(o.position.x) && usable(o.position.y) && usable(o.position.z)
                    && std::isfinite(o.rotation.x) && std::isfinite(o.rotation.y) && std::isfinite(o.rotation.z)
                    && std::isfinite(o.scale) && o.scale > 0.f && o.scale <= kMaxScale;
                item.boundsOk = !o.hasBounds || boundsUsable(item.boundsMin, item.boundsMax);
                item.scale = o.scale;
                item.world = placementMatrix(o.position, o.rotation, o.scale);
                return item;
            },
            provider, limits);
    }

    CellScene buildCellScene(
        const sdkgame::WorldCell& cell, const MeshProvider& provider, const CellSceneLimits& limits)
    {
        return build(
            cell.editorId, cell.objects, cell.droppedObjects,
            [](const sdkgame::WorldObject& o) {
                Item item;
                item.initiallyDisabled = o.initiallyDisabled;
                item.modelPath = o.modelPath;
                item.name = o.baseEditorId;
                item.kind = o.kind;
                item.hasBounds = o.collision.hasBounds;
                // The bounds were floats in the data; narrowing them again gives the same numbers.
                item.boundsMin[0] = static_cast<float>(o.collision.boundsMin.x);
                item.boundsMin[1] = static_cast<float>(o.collision.boundsMin.y);
                item.boundsMin[2] = static_cast<float>(o.collision.boundsMin.z);
                item.boundsMax[0] = static_cast<float>(o.collision.boundsMax.x);
                item.boundsMax[1] = static_cast<float>(o.collision.boundsMax.y);
                item.boundsMax[2] = static_cast<float>(o.collision.boundsMax.z);
                const sdkgame::WorldTransform& t = o.transform;
                item.placementOk = usable(t.position.x) && usable(t.position.y) && usable(t.position.z)
                    && std::isfinite(t.rotation.x) && std::isfinite(t.rotation.y) && std::isfinite(t.rotation.z)
                    && std::isfinite(t.rotation.w) && std::isfinite(t.scale) && t.scale > 0.0 && t.scale <= kMaxScale;
                item.boundsOk = !item.hasBounds || boundsUsable(item.boundsMin, item.boundsMax);
                item.scale = t.scale;
                item.world = placementMatrix(t);
                return item;
            },
            provider, limits);
    }
}
