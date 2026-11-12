#include "flatten.hpp"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <limits>
#include <map>
#include <set>
#include <string_view>

namespace NifScene::Flat
{
    namespace
    {
        constexpr std::uint32_t sHiddenNodeFlag = 1u; // NiAVObject flags, bit 0
        constexpr std::string_view sEditorMarkerPrefix = "editormarker";

        using Vec3d = std::array<double, 3>;

        struct Pose
        {
            std::array<double, 9> r{ 1, 0, 0, 0, 1, 0, 0, 0, 1 };
            Vec3d t{ 0, 0, 0 };
            double s = 1.0;
        };

        Pose toPose(const Transform& source)
        {
            Pose pose;
            for (int i = 0; i < 9; ++i)
                pose.r[i] = source.mRotation[i];
            for (int i = 0; i < 3; ++i)
                pose.t[i] = source.mTranslation[i];
            pose.s = source.mScale;
            return pose;
        }

        // A local transform with a value that is not a number is taken as no transform at all (a damaged node must not
        // take the whole model with it). Large finite values are kept: they show up as bad vertices later.
        bool finite(const Pose& pose)
        {
            return std::all_of(pose.r.begin(), pose.r.end(), [](double v) { return std::isfinite(v); })
                && std::all_of(pose.t.begin(), pose.t.end(), [](double v) { return std::isfinite(v); })
                && std::isfinite(pose.s);
        }

        Vec3d apply(const Pose& pose, const Vec3d& v)
        {
            Vec3d out{};
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

        // The node's parent when it is a node that is walked and not the node itself.
        std::uint32_t validParent(const Node& node, std::size_t self, std::size_t walked)
        {
            if (node.mParent < walked && node.mParent != self)
                return node.mParent;
            return sNone;
        }

        struct NodeInfo
        {
            Pose world;
            bool hidden = false;
        };

        // World pose and hidden state of every walked node. A parent chain that loops is cut where it comes back to
        // itself; a node whose parent is outside the scene is a root.
        std::vector<NodeInfo> walkNodes(const Scene& scene, std::size_t walked)
        {
            std::vector<NodeInfo> info(walked);
            std::vector<char> state(walked, 0); // 0 not done, 1 on the chain being resolved, 2 done
            std::vector<std::uint32_t> chain;
            for (std::size_t start = 0; start < walked; ++start)
            {
                if (state[start] == 2)
                    continue;
                chain.clear();
                auto at = static_cast<std::uint32_t>(start);
                std::uint32_t above = sNone; // the first node above the chain whose world is known
                while (true)
                {
                    state[at] = 1;
                    chain.push_back(at);
                    const std::uint32_t parent = validParent(scene.mNodes[at], at, walked);
                    if (parent == sNone || state[parent] == 1)
                        break; // a root, or a loop: cut here
                    if (state[parent] == 2)
                    {
                        above = parent;
                        break;
                    }
                    at = parent;
                }
                // Resolve from the top of the chain down. The chain's last element is the top.
                for (std::size_t k = chain.size(); k-- > 0;)
                {
                    const std::uint32_t id = chain[k];
                    const Node& node = scene.mNodes[id];
                    Pose local = toPose(node.mTransform);
                    if (!finite(local))
                        local = Pose{};
                    const bool ownHidden = (node.mFlags & sHiddenNodeFlag) != 0 || isEditorMarkerName(node.mName);
                    std::uint32_t parent = sNone;
                    if (k + 1 < chain.size())
                        parent = chain[k + 1];
                    else
                        parent = above;
                    if (parent == sNone)
                    {
                        info[id].world = local;
                        info[id].hidden = ownHidden;
                    }
                    else
                    {
                        info[id].world = compose(info[parent].world, local);
                        info[id].hidden = ownHidden || info[parent].hidden;
                    }
                    state[id] = 2;
                }
            }
            return info;
        }

        // A coordinate the renderer can hold.
        bool usable(double v, double limit)
        {
            return std::isfinite(v) && std::fabs(v) <= limit;
        }

        struct Bounds
        {
            std::array<float, 3> min{ std::numeric_limits<float>::max(), std::numeric_limits<float>::max(),
                std::numeric_limits<float>::max() };
            std::array<float, 3> max{ std::numeric_limits<float>::lowest(), std::numeric_limits<float>::lowest(),
                std::numeric_limits<float>::lowest() };
            bool any = false;
            void add(const std::array<float, 3>& p)
            {
                any = true;
                for (int i = 0; i < 3; ++i)
                {
                    min[i] = std::min(min[i], p[i]);
                    max[i] = std::max(max[i], p[i]);
                }
            }
        };

        std::array<float, 3> normalized(double x, double y, double z, bool& ok)
        {
            const double length = std::sqrt(x * x + y * y + z * z);
            if (!(length > 1e-12) || !std::isfinite(length))
            {
                ok = false;
                return { 0.f, 0.f, 1.f };
            }
            ok = true;
            return { static_cast<float>(x / length), static_cast<float>(y / length), static_cast<float>(z / length) };
        }

        // Area weighted vertex normals of the kept triangles.
        std::vector<std::array<float, 3>> makeNormals(
            const std::vector<std::array<float, 3>>& positions, const std::vector<std::uint32_t>& indices)
        {
            std::vector<Vec3d> sums(positions.size(), Vec3d{ 0, 0, 0 });
            for (std::size_t i = 0; i + 2 < indices.size(); i += 3)
            {
                const auto& a = positions[indices[i]];
                const auto& b = positions[indices[i + 1]];
                const auto& c = positions[indices[i + 2]];
                const double ux = double(b[0]) - a[0], uy = double(b[1]) - a[1], uz = double(b[2]) - a[2];
                const double vx = double(c[0]) - a[0], vy = double(c[1]) - a[1], vz = double(c[2]) - a[2];
                const Vec3d n{ uy * vz - uz * vy, uz * vx - ux * vz, ux * vy - uy * vx };
                for (std::size_t k = 0; k < 3; ++k)
                    for (std::size_t axis = 0; axis < 3; ++axis)
                        sums[indices[i + k]][axis] += n[axis];
            }
            std::vector<std::array<float, 3>> out(positions.size());
            for (std::size_t i = 0; i < out.size(); ++i)
            {
                bool ok = false;
                out[i] = normalized(sums[i][0], sums[i][1], sums[i][2], ok);
            }
            return out;
        }

        std::string slotPath(const Scene& scene, const ShaderMaterial& material, std::size_t slot, const Limits& limits)
        {
            if (material.mTextureSet >= scene.mTextureSets.size())
                return {};
            const auto& set = scene.mTextureSets[material.mTextureSet].mTextures;
            if (slot >= set.size() || set[slot].empty() || set[slot].size() > limits.mMaxPathLength)
                return {};
            return set[slot];
        }

        float finiteOr(float v, float fallback)
        {
            return std::isfinite(v) ? v : fallback;
        }

        // Task 0130. The tangents are turned with the node like the normals (a direction: no scale, no translation)
        // and made unit length; the sign (handedness) stays. Any bad tangent drops the whole geometry's normal map.
        void collectSurfaceMaps(const Scene& scene, const Geometry& geometry, const ShaderMaterial& material,
            const Pose& pose, std::size_t vertexCount, bool hasUV, const Limits& limits, MaterialKey& key,
            std::vector<std::array<float, 4>>& tangents, Report& report)
        {
            if (material.mKind != ShaderKind::Lighting)
                return;
            key.mShaderType = material.mShaderType;

            const std::string normal = slotPath(scene, material, 1, limits);
            if (!normal.empty() && hasUV)
            {
                if ((material.mFlags1 & ShaderFlags::sModelSpaceNormals) != 0)
                    ++report.mNormalMapsModelSpace;
                else
                {
                    bool good = geometry.mTangents.size() == vertexCount;
                    std::vector<std::array<float, 4>> turned;
                    if (good)
                    {
                        turned.resize(vertexCount);
                        for (std::size_t v = 0; v < vertexCount && good; ++v)
                        {
                            const auto& t = geometry.mTangents[v];
                            if (!std::isfinite(t[0]) || !std::isfinite(t[1]) || !std::isfinite(t[2])
                                || !std::isfinite(t[3]))
                            {
                                good = false;
                                break;
                            }
                            double r[3];
                            for (int row = 0; row < 3; ++row)
                                r[row] = pose.r[row * 3] * t[0] + pose.r[row * 3 + 1] * t[1] + pose.r[row * 3 + 2] * t[2];
                            bool ok = false;
                            const auto unit = normalized(r[0], r[1], r[2], ok);
                            if (!ok)
                            {
                                good = false;
                                break;
                            }
                            turned[v] = { unit[0], unit[1], unit[2], t[3] < 0.f ? -1.f : 1.f };
                        }
                    }
                    if (good)
                    {
                        key.mNormal = normal;
                        tangents = std::move(turned);
                        ++report.mNormalMaps;
                    }
                    else
                        ++report.mNormalMapsNoTangents;
                }
            }

            if ((material.mFlags2 & ShaderFlags::sGlowMap) != 0 && hasUV)
            {
                const std::string glow = slotPath(scene, material, 2, limits);
                if (!glow.empty())
                {
                    key.mGlow = glow;
                    ++report.mGlowMaps;
                }
            }

            if ((material.mFlags1 & ShaderFlags::sSpecular) != 0)
            {
                const float strength = std::clamp(finiteOr(material.mSpecularStrength, 0.f), 0.f, 16.f);
                for (std::size_t c = 0; c < 3; ++c)
                    key.mSpecularColor[c] = std::clamp(finiteOr(material.mSpecularColor[c], 0.f), 0.f, 1.f) * strength;
                key.mGlossiness = std::clamp(finiteOr(material.mGlossiness, 0.f), 0.f, 1024.f);
            }

            // A shape with a glow map gets its emission from the map (the engine adds the map's texels as they are; the
            // shader's emissive colour times multiple, 2.3 on average for the glow shader type, is not applied: it would
            // light the whole mesh). Others (lamps, candle flames without a map) emit their colour all over.
            if (key.mGlow.empty())
            {
                const float multiple = std::clamp(finiteOr(material.mEmissiveMultiple, 0.f), 0.f, 16.f);
                for (std::size_t c = 0; c < 3; ++c)
                    key.mEmissive[c] = std::clamp(finiteOr(material.mEmissiveColor[c], 0.f), 0.f, 16.f) * multiple;
            }
        }

        float clamp01(float v)
        {
            if (!std::isfinite(v))
                return 1.f;
            return std::clamp(v, 0.f, 1.f);
        }

        struct MergeKey
        {
            MaterialKey key;
            bool hasUV = false;
            friend auto operator<=>(const MergeKey&, const MergeKey&) = default;
        };
    }

    bool isEditorMarkerName(const std::string& name)
    {
        if (name.size() < sEditorMarkerPrefix.size())
            return false;
        for (std::size_t i = 0; i < sEditorMarkerPrefix.size(); ++i)
            if (std::tolower(static_cast<unsigned char>(name[i])) != sEditorMarkerPrefix[i])
                return false;
        return true;
    }

    FlatModel flatten(const Scene& scene, const Limits& limits)
    {
        FlatModel model;
        Report& report = model.mReport;
        report.mGeometries = static_cast<std::uint32_t>(std::min<std::size_t>(scene.mGeometries.size(), 0xFFFFFFFFu));
        const std::size_t walked = std::min(scene.mNodes.size(), limits.mMaxNodes);
        report.mNodesOverCap = scene.mNodes.size() > limits.mMaxNodes;
        const std::vector<NodeInfo> nodes = walkNodes(scene, walked);

        std::map<MergeKey, std::size_t> open; // the shape a new geometry with this key is merged into
        std::size_t acceptedVertices = 0;
        std::size_t acceptedTriangles = 0;
        std::size_t acceptedShapes = 0;
        Bounds bounds;

        for (std::size_t n = 0; n < walked; ++n)
        {
            const Node& node = scene.mNodes[n];
            if (node.mGeometry >= scene.mGeometries.size())
                continue;
            const Geometry& geometry = scene.mGeometries[node.mGeometry];
            if (nodes[n].hidden)
            {
                ++report.mSkippedHidden;
                continue;
            }
            if (geometry.mSkin < scene.mSkins.size())
            {
                ++report.mSkippedSkinned;
                continue;
            }
            if (geometry.mMaterial >= scene.mMaterials.size())
            {
                ++report.mSkippedNoMaterial;
                continue;
            }
            const ShaderMaterial& material = scene.mMaterials[geometry.mMaterial];
            if (material.mKind == ShaderKind::Effect
                || (material.mFlags1 & (ShaderFlags::sRefraction | ShaderFlags::sFireRefraction)) != 0)
            {
                ++report.mSkippedEffect;
                continue;
            }
            const std::size_t vertexCount = geometry.mPositions.size();
            const std::size_t fullTriangles = geometry.mTriangles.size() / 3;
            if (geometry.mNoVertexData || vertexCount == 0 || fullTriangles == 0)
            {
                ++report.mSkippedNoGeometry;
                continue;
            }
            if (acceptedShapes >= limits.mMaxShapes || acceptedVertices + vertexCount > limits.mMaxVertices
                || acceptedTriangles + fullTriangles > limits.mMaxTriangles)
            {
                ++report.mSkippedOverCap;
                continue;
            }

            // Positions, baked into the model's frame.
            const Pose& pose = nodes[n].world;
            std::vector<std::array<float, 3>> positions(vertexCount);
            std::vector<char> bad(vertexCount, 0);
            std::uint32_t badCount = 0;
            for (std::size_t v = 0; v < vertexCount; ++v)
            {
                const auto& p = geometry.mPositions[v];
                const Vec3d placed = apply(pose, { p[0], p[1], p[2] });
                if (usable(placed[0], limits.mMaxCoordinate) && usable(placed[1], limits.mMaxCoordinate)
                    && usable(placed[2], limits.mMaxCoordinate))
                    positions[v] = { static_cast<float>(placed[0]), static_cast<float>(placed[1]),
                        static_cast<float>(placed[2]) };
                else
                {
                    positions[v] = { 0.f, 0.f, 0.f };
                    bad[v] = 1;
                    ++badCount;
                }
            }
            report.mBadVertices += badCount;

            // Triangles that can be drawn.
            std::vector<std::uint32_t> indices;
            indices.reserve(fullTriangles * 3);
            std::uint32_t dropped = geometry.mTriangles.size() % 3 != 0 ? 1u : 0u;
            for (std::size_t t = 0; t < fullTriangles; ++t)
            {
                const std::uint32_t a = geometry.mTriangles[t * 3];
                const std::uint32_t b = geometry.mTriangles[t * 3 + 1];
                const std::uint32_t c = geometry.mTriangles[t * 3 + 2];
                if (a >= vertexCount || b >= vertexCount || c >= vertexCount || bad[a] || bad[b] || bad[c])
                {
                    ++dropped;
                    continue;
                }
                indices.insert(indices.end(), { a, b, c });
            }
            report.mDroppedTriangles += dropped;
            if (indices.empty())
            {
                ++report.mSkippedNoGeometry;
                continue;
            }

            // Normals: the file's, turned with the node (a uniform scale does not change a direction), or made.
            std::vector<std::array<float, 3>> normals;
            if (geometry.mNormals.size() == vertexCount)
            {
                normals.resize(vertexCount);
                bool allGood = true;
                for (std::size_t v = 0; v < vertexCount && allGood; ++v)
                {
                    const auto& nv = geometry.mNormals[v];
                    if (!std::isfinite(nv[0]) || !std::isfinite(nv[1]) || !std::isfinite(nv[2]))
                    {
                        allGood = false;
                        break;
                    }
                    double r[3];
                    for (int row = 0; row < 3; ++row)
                        r[row] = pose.r[row * 3] * nv[0] + pose.r[row * 3 + 1] * nv[1] + pose.r[row * 3 + 2] * nv[2];
                    normals[v] = normalized(r[0], r[1], r[2], allGood);
                }
                if (!allGood)
                    normals.clear();
            }
            if (normals.empty())
                normals = makeNormals(positions, indices);

            // Texture coordinates with the shader's offset and scale baked in.
            std::vector<std::array<float, 2>> uvs;
            if (geometry.mUV0.size() == vertexCount)
            {
                uvs.resize(vertexCount);
                for (std::size_t v = 0; v < vertexCount; ++v)
                {
                    const float u = geometry.mUV0[v][0] * material.mUVScale[0] + material.mUVOffset[0];
                    const float w = geometry.mUV0[v][1] * material.mUVScale[1] + material.mUVOffset[1];
                    uvs[v] = { std::isfinite(u) ? u : 0.f, std::isfinite(w) ? w : 0.f };
                }
            }

            // Vertex colours only when the shader uses them.
            const bool wantColors = (material.mFlags2 & ShaderFlags::sVertexColors) != 0;
            const bool vertexAlpha = (material.mFlags1 & ShaderFlags::sVertexAlpha) != 0;
            std::vector<std::array<float, 4>> colors;
            // Task 0136: a Tree_Anim shape keeps the file's vertex alpha, which is its per-vertex flexibility (the
            // Vertex_Alpha flag is not what says so; see docs/tasks/0136-skyrim-tree-sway.md).
            const bool treeSway = limits.mTreeSway && wantColors && geometry.mColors.size() == vertexCount
                && (material.mFlags2 & ShaderFlags::sTreeAnim) != 0;
            if (wantColors && geometry.mColors.size() == vertexCount)
            {
                colors.resize(vertexCount);
                for (std::size_t v = 0; v < vertexCount; ++v)
                {
                    const auto& c = geometry.mColors[v];
                    colors[v] = { clamp01(c[0]), clamp01(c[1]), clamp01(c[2]),
                        (vertexAlpha || treeSway) ? clamp01(c[3]) : 1.f };
                }
            }

            MaterialKey key;
            key.mDoubleSided = (material.mFlags2 & ShaderFlags::sDoubleSided) != 0;
            key.mVertexColors = !colors.empty();
            key.mSway = treeSway;
            key.mClampMode = static_cast<std::uint8_t>(material.mClampMode & 3u);
            key.mAlpha = std::isfinite(material.mAlpha) ? std::clamp(material.mAlpha, 0.f, 1.f) : 1.f;
            if (!uvs.empty() && material.mTextureSet < scene.mTextureSets.size())
            {
                const auto& set = scene.mTextureSets[material.mTextureSet].mTextures;
                if (!set.empty() && !set[0].empty() && set[0].size() <= limits.mMaxPathLength)
                    key.mDiffuse = set[0];
            }
            if (geometry.mAlpha < scene.mAlphaProperties.size())
            {
                const AlphaProperty& alpha = scene.mAlphaProperties[geometry.mAlpha];
                key.mBlend = alpha.mBlend;
                key.mTest = alpha.mTest;
                if (alpha.mBlend)
                {
                    key.mSourceBlend = alpha.mSourceBlend;
                    key.mDestinationBlend = alpha.mDestinationBlend;
                    key.mNoSorter = alpha.mNoSorter;
                }
                if (alpha.mTest)
                {
                    key.mTestMode = alpha.mTestMode;
                    key.mThreshold = alpha.mThreshold;
                }
            }

            // Task 0130: the maps and values beyond the diffuse texture, only on request.
            std::vector<std::array<float, 4>> tangents;
            if (limits.mSurfaceMaps)
                collectSurfaceMaps(scene, geometry, material, pose, vertexCount, !uvs.empty(), limits, key, tangents, report);

            // Bounds of the vertices the kept triangles use.
            for (std::uint32_t index : indices)
                bounds.add(positions[index]);

            // Merge into the open shape with the same key, or start one.
            const MergeKey mergeKey{ key, !uvs.empty() };
            auto found = open.find(mergeKey);
            Shape* shape = nullptr;
            if (found != open.end())
            {
                Shape& candidate = model.mShapes[found->second];
                if (candidate.mPositions.size() + vertexCount <= limits.mMaxMergedVertices)
                    shape = &candidate;
            }
            if (!shape)
            {
                model.mShapes.emplace_back();
                shape = &model.mShapes.back();
                shape->mKey = key;
                shape->mName = geometry.mName;
                shape->mMergedFrom = 0;
                shape->mFlags1 = material.mFlags1;
                shape->mFlags2 = material.mFlags2;
                open[mergeKey] = model.mShapes.size() - 1;
            }
            const auto base = static_cast<std::uint32_t>(shape->mPositions.size());
            shape->mPositions.insert(shape->mPositions.end(), positions.begin(), positions.end());
            shape->mNormals.insert(shape->mNormals.end(), normals.begin(), normals.end());
            shape->mUV.insert(shape->mUV.end(), uvs.begin(), uvs.end());
            shape->mColors.insert(shape->mColors.end(), colors.begin(), colors.end());
            shape->mTangents.insert(shape->mTangents.end(), tangents.begin(), tangents.end());
            for (std::uint32_t index : indices)
                shape->mIndices.push_back(base + index);
            ++shape->mMergedFrom;

            acceptedVertices += vertexCount;
            acceptedTriangles += fullTriangles;
            ++acceptedShapes;
            ++report.mDrawn;
            report.mSwayShapes += treeSway ? 1u : 0u;
        }

        std::set<std::string> textures;
        for (const Shape& shape : model.mShapes)
        {
            report.mVertices += static_cast<std::uint32_t>(shape.mPositions.size());
            report.mTriangles += static_cast<std::uint32_t>(shape.mIndices.size() / 3);
            if (!shape.mKey.mDiffuse.empty())
                textures.insert(shape.mKey.mDiffuse);
        }
        report.mTexturesNamed = static_cast<std::uint32_t>(textures.size());
        if (bounds.any)
        {
            model.mBoundsMin = bounds.min;
            model.mBoundsMax = bounds.max;
        }
        return model;
    }
}
