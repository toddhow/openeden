#include "scenemodel.hpp"

#include <algorithm>
#include <charconv>
#include <cmath>

namespace CSMMesh
{
    namespace
    {
        constexpr std::array<float, 4> sNeutral{ 0.72f, 0.72f, 0.72f, 1.f };
        // BSLightingShaderType values: 5 skin tint, 6 hair tint.
        constexpr std::uint32_t sSkinTintType = 5;
        constexpr std::uint32_t sHairTintType = 6;
        // The longest first line of a NIF file that is looked at.
        constexpr std::size_t sMaxHeaderLine = 80;
        // The largest number a file may put into a coordinate, a translation, a scale or a matrix entry of the model.
        // A Skyrim world is about 1e5 units across; anything past this is a damaged or hostile file, and a number that
        // size would make the camera framing and the sizes derived from the bounds meaningless.
        constexpr double sMaxCoordinate = 1.0e7;
        // At most this many warnings are kept; a hostile file could make one per node.
        constexpr std::size_t sMaxWarnings = 32;

        struct Pose
        {
            std::array<double, 9> r{ 1, 0, 0, 0, 1, 0, 0, 0, 1 };
            std::array<double, 3> t{};
            double s = 1.0;
        };

        Pose toPose(const NifScene::Transform& source)
        {
            Pose pose;
            for (int i = 0; i < 9; ++i)
                pose.r[i] = source.mRotation[i];
            for (int i = 0; i < 3; ++i)
                pose.t[i] = source.mTranslation[i];
            pose.s = source.mScale;
            return pose;
        }

        std::array<double, 3> apply(const Pose& pose, const std::array<double, 3>& v)
        {
            std::array<double, 3> out{};
            for (int row = 0; row < 3; ++row)
                out[row] = pose.s * (pose.r[row * 3] * v[0] + pose.r[row * 3 + 1] * v[1] + pose.r[row * 3 + 2] * v[2])
                    + pose.t[row];
            return out;
        }

        // The parent's pose applied after the child's.
        Pose compose(const Pose& parent, const Pose& child)
        {
            Pose out;
            for (int row = 0; row < 3; ++row)
                for (int column = 0; column < 3; ++column)
                {
                    double sum = 0.0;
                    for (int k = 0; k < 3; ++k)
                        sum += parent.r[row * 3 + k] * child.r[k * 3 + column];
                    out.r[row * 3 + column] = sum;
                }
            out.s = parent.s * child.s;
            out.t = apply(parent, child.t);
            return out;
        }

        std::array<double, 16> columnMajor(const Pose& pose)
        {
            std::array<double, 16> m{};
            for (int column = 0; column < 3; ++column)
                for (int row = 0; row < 3; ++row)
                    m[column * 4 + row] = pose.s * pose.r[row * 3 + column];
            m[12] = pose.t[0];
            m[13] = pose.t[1];
            m[14] = pose.t[2];
            m[15] = 1.0;
            return m;
        }

        std::array<float, 3> toFloat(const std::array<double, 3>& v)
        {
            return { static_cast<float>(v[0]), static_cast<float>(v[1]), static_cast<float>(v[2]) };
        }

        bool finite(const std::array<float, 3>& v)
        {
            return std::isfinite(v[0]) && std::isfinite(v[1]) && std::isfinite(v[2]);
        }

        bool usable(double v)
        {
            return std::isfinite(v) && std::fabs(v) <= sMaxCoordinate;
        }

        bool usable(const Pose& pose)
        {
            return std::all_of(pose.r.begin(), pose.r.end(), [](double v) { return usable(v); })
                && std::all_of(pose.t.begin(), pose.t.end(), [](double v) { return usable(v); }) && usable(pose.s);
        }

        bool usable(const std::array<double, 16>& matrix)
        {
            return std::all_of(matrix.begin(), matrix.end(), [](double v) { return usable(v); });
        }

        bool usable(const NifScene::Transform& t)
        {
            return usable(toPose(t));
        }
    }

    NifReader chooseNifReader(std::string_view fileStart)
    {
        constexpr std::string_view key = "Version ";
        const std::size_t end = std::min(fileStart.find('\n'), std::min(fileStart.size(), sMaxHeaderLine));
        const std::string_view line = fileStart.substr(0, end);
        // A real header line is printable text.
        if (std::any_of(line.begin(), line.end(), [](char c) { return c < 0x20 || c > 0x7e; }))
            return NifReader::Engine;
        const std::size_t at = line.find(key);
        if (at == std::string_view::npos)
            return NifReader::Engine;
        std::string_view rest = line.substr(at + key.size());
        std::array<unsigned, 4> parts{};
        for (std::size_t i = 0; i < 4; ++i)
        {
            const auto result = std::from_chars(rest.data(), rest.data() + rest.size(), parts[i]);
            if (result.ec != std::errc())
                return NifReader::Engine;
            rest.remove_prefix(static_cast<std::size_t>(result.ptr - rest.data()));
            if (i < 3)
            {
                if (rest.empty() || rest.front() != '.')
                    return NifReader::Engine;
                rest.remove_prefix(1);
            }
        }
        if (!rest.empty())
            return NifReader::Engine;
        return parts == std::array<unsigned, 4>{ 20, 2, 0, 7 } ? NifReader::Scene : NifReader::Engine;
    }

    std::array<float, 4> materialColour(const NifScene::ShaderMaterial* material)
    {
        if (!material)
            return sNeutral;
        std::array<float, 3> tint{};
        if (material->mKind == NifScene::ShaderKind::Effect)
            tint = { material->mBaseColor[0], material->mBaseColor[1], material->mBaseColor[2] };
        else if (material->mShaderType == sSkinTintType)
            tint = material->mSkinTintColor;
        else if (material->mShaderType == sHairTintType)
            tint = material->mHairTintColor;
        if (!finite(tint) || (tint[0] == 0.f && tint[1] == 0.f && tint[2] == 0.f))
            return sNeutral;
        return { std::clamp(tint[0], 0.f, 1.f), std::clamp(tint[1], 0.f, 1.f), std::clamp(tint[2], 0.f, 1.f), 1.f };
    }

    std::array<float, 3> bindBonePosition(const NifScene::Transform& bind)
    {
        if (bind.mScale == 0.f || !std::isfinite(bind.mScale))
            return { 0.f, 0.f, 0.f };
        std::array<float, 3> out{};
        for (int column = 0; column < 3; ++column)
        {
            double sum = 0.0;
            for (int row = 0; row < 3; ++row)
                sum += static_cast<double>(bind.mRotation[row * 3 + column]) * bind.mTranslation[row];
            out[column] = static_cast<float>(-sum / bind.mScale);
        }
        // A rotation or translation that is not a number gives no position.
        return finite(out) ? out : std::array<float, 3>{ 0.f, 0.f, 0.f };
    }

    SceneModel buildSceneModel(const NifScene::Scene& scene)
    {
        SceneModel model;
        model.warnings = scene.mWarnings;
        const std::size_t count = scene.mNodes.size();
        std::size_t droppedWarnings = 0;
        auto warn = [&](std::string text) {
            if (model.warnings.size() < scene.mWarnings.size() + sMaxWarnings)
                model.warnings.push_back(std::move(text));
            else
                ++droppedWarnings;
        };

        // Children lists from the parent links; roots are the nodes with no valid parent.
        std::vector<std::vector<std::uint32_t>> children(count);
        std::vector<std::uint32_t> roots;
        for (std::size_t i = 0; i < count; ++i)
        {
            const std::uint32_t parent = scene.mNodes[i].mParent;
            if (parent < count && parent != i)
                children[parent].push_back(static_cast<std::uint32_t>(i));
            else
                roots.push_back(static_cast<std::uint32_t>(i));
        }

        std::vector<int> modelIndex(count, -1);
        std::vector<Pose> worlds(count);
        std::vector<char> done(count, 0);
        struct Item
        {
            std::uint32_t node;
            int parent;
        };
        std::vector<Item> stack;
        auto walk = [&](std::uint32_t root) {
            stack.push_back({ root, -1 });
            while (!stack.empty())
            {
                const Item item = stack.back();
                stack.pop_back();
                if (done[item.node])
                    continue;
                done[item.node] = 1;
                const NifScene::Node& source = scene.mNodes[item.node];
                Pose local = toPose(source.mTransform);
                if (!usable(local))
                {
                    warn("node \"" + source.mName
                        + "\" has a transform that is not a usable number; it is placed as if it had none");
                    local = Pose{};
                }
                Pose world = item.parent < 0 ? local : compose(worlds[source.mParent], local);
                if (!usable(world) || !usable(columnMajor(world)))
                {
                    warn("node \"" + source.mName
                        + "\" ends up outside the range of the viewer; it is placed with its parent");
                    world = item.parent < 0 ? Pose{} : worlds[source.mParent];
                }
                worlds[item.node] = world;
                SceneModelNode node;
                node.name = source.mName;
                node.parent = item.parent;
                node.world = columnMajor(worlds[item.node]);
                node.translation = { node.world[12], node.world[13], node.world[14] };
                for (int c = 0; c < 3; ++c)
                    node.scale[c] = std::sqrt(node.world[c * 4] * node.world[c * 4]
                        + node.world[c * 4 + 1] * node.world[c * 4 + 1]
                        + node.world[c * 4 + 2] * node.world[c * 4 + 2]);
                node.type = source.mGeometry < scene.mGeometries.size() ? "Geometry" : "Transform";
                modelIndex[item.node] = static_cast<int>(model.nodes.size());
                model.nodes.push_back(std::move(node));
                // Pushed in reverse so that children are visited in file order.
                for (auto it = children[item.node].rbegin(); it != children[item.node].rend(); ++it)
                    stack.push_back({ *it, modelIndex[item.node] });
            }
        };
        for (std::uint32_t root : roots)
            walk(root);
        // Nodes only reachable through a cycle of parent links (a malformed file) are still shown.
        for (std::size_t i = 0; i < count; ++i)
            if (!done[i])
            {
                warn("node \"" + scene.mNodes[i].mName + "\" is in a parent cycle");
                walk(static_cast<std::uint32_t>(i));
            }

        std::vector<std::string> textures;
        for (std::size_t i = 0; i < count; ++i)
        {
            const NifScene::Node& source = scene.mNodes[i];
            if (source.mGeometry >= scene.mGeometries.size() || modelIndex[i] < 0)
                continue;
            const NifScene::Geometry& geometry = scene.mGeometries[source.mGeometry];
            SceneModelNode& node = model.nodes[modelIndex[i]];
            if (geometry.mNoVertexData || geometry.mPositions.empty())
            {
                warn("shape \"" + geometry.mName + "\" has no vertex data");
                continue;
            }
            // Triangles: three indices each, all inside the vertex arrays; the draw code trusts this.
            const bool badTriangles = geometry.mTriangles.size() % 3 != 0
                || std::any_of(geometry.mTriangles.begin(), geometry.mTriangles.end(),
                    [&](std::uint32_t index) { return index >= geometry.mPositions.size(); });
            if (badTriangles)
            {
                warn("shape \"" + geometry.mName
                    + "\" has a triangle list that does not fit its vertices; it is left out");
                continue;
            }
            SceneModelShape shape;
            shape.node = modelIndex[i];
            shape.skinned = geometry.mSkin < scene.mSkins.size();
            const Pose& pose = worlds[i];
            // Skinned or not, the shape node world transform places the vertices (the bind transforms are in the same
            // frame, see bindBonePosition).
            bool badVertex = false;
            for (const auto& p : geometry.mPositions)
            {
                const std::array<double, 3> placed = apply(pose, { p[0], p[1], p[2] });
                if (!usable(placed[0]) || !usable(placed[1]) || !usable(placed[2]))
                {
                    badVertex = true;
                    break;
                }
                shape.mesh.positions.push_back(toFloat(placed));
            }
            if (badVertex)
            {
                warn("shape \"" + geometry.mName + "\" has a vertex that is not a usable number; it is left out");
                continue;
            }
            if (geometry.mNormals.size() == geometry.mPositions.size())
                for (const auto& n : geometry.mNormals)
                {
                    // The rotation only; a uniform scale does not change a direction.
                    std::array<float, 3> r{};
                    for (int row = 0; row < 3; ++row)
                        r[row] = static_cast<float>(
                            pose.r[row * 3] * n[0] + pose.r[row * 3 + 1] * n[1] + pose.r[row * 3 + 2] * n[2]);
                    const float length = std::sqrt(r[0] * r[0] + r[1] * r[1] + r[2] * r[2]);
                    if (length > 0.f)
                        for (float& v : r)
                            v /= length;
                    shape.mesh.normals.push_back(r);
                }
            if (!std::all_of(
                    shape.mesh.normals.begin(), shape.mesh.normals.end(), [](const auto& n) { return finite(n); }))
            {
                warn("shape \"" + geometry.mName + "\" has normals that are not numbers; they are not used");
                shape.mesh.normals.clear();
            }
            if (geometry.mColors.size() == geometry.mPositions.size())
                shape.mesh.colours = geometry.mColors;
            shape.mesh.indices = geometry.mTriangles;
            shape.mesh.topology = Topology::TriangleList;

            const NifScene::ShaderMaterial* material
                = geometry.mMaterial < scene.mMaterials.size() ? &scene.mMaterials[geometry.mMaterial] : nullptr;
            shape.colour = materialColour(material);
            if (material && material->mTextureSet < scene.mTextureSets.size())
            {
                const auto& set = scene.mTextureSets[material->mTextureSet].mTextures;
                if (!set.empty() && !set[0].empty())
                {
                    node.textures.push_back(set[0]);
                    if (std::find(textures.begin(), textures.end(), set[0]) == textures.end())
                        textures.push_back(set[0]);
                }
            }

            node.vertexCount = static_cast<std::uint32_t>(geometry.mPositions.size());
            node.triangleCount = static_cast<std::uint32_t>(triangles(shape.mesh).size());
            node.flags = std::string("triangle list") + (shape.skinned ? ", skinned" : "")
                + (shape.mesh.normals.empty() ? "" : ", normals")
                + (shape.mesh.colours.empty() ? "" : ", vertex colours");
            node.shape = static_cast<int>(model.shapes.size());
            for (const auto& p : shape.mesh.positions)
                expand(model.bounds, { p[0], p[1], p[2] });

            if (shape.skinned)
                for (const NifScene::Transform& bind : scene.mSkins[geometry.mSkin].mBindTransforms)
                {
                    if (!usable(bind))
                    {
                        warn(
                            "a bind transform of \"" + geometry.mName + "\" is not a usable number; it gives no joint");
                        continue;
                    }
                    const auto local = bindBonePosition(bind);
                    const std::array<double, 3> placed = apply(pose, { local[0], local[1], local[2] });
                    if (!usable(placed[0]) || !usable(placed[1]) || !usable(placed[2]))
                        continue;
                    model.bonePoints.push_back(toFloat(placed));
                }
            model.shapes.push_back(std::move(shape));
        }
        model.textureNames = std::move(textures);
        if (droppedWarnings > 0)
            model.warnings.push_back("and " + std::to_string(droppedWarnings) + " more warnings");

        for (const SceneModelNode& node : model.nodes)
        {
            if (node.parent < 0 || node.type != "Transform" || model.nodes[node.parent].type != "Transform")
                continue;
            const SceneModelNode& parent = model.nodes[node.parent];
            model.hierarchyLines.push_back(
                { std::array<float, 3>{ static_cast<float>(parent.translation[0]),
                      static_cast<float>(parent.translation[1]), static_cast<float>(parent.translation[2]) },
                    std::array<float, 3>{ static_cast<float>(node.translation[0]),
                        static_cast<float>(node.translation[1]), static_cast<float>(node.translation[2]) } });
        }
        for (const auto& line : model.hierarchyLines)
            for (const auto& p : line)
                expand(model.bounds, { p[0], p[1], p[2] });
        for (const auto& p : model.bonePoints)
            expand(model.bounds, { p[0], p[1], p[2] });
        return model;
    }
}
