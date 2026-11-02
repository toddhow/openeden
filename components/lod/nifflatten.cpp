#include "nifflatten.hpp"

#include <algorithm>
#include <array>
#include <cctype>
#include <cmath>
#include <utility>
#include <vector>

#include <components/misc/strings/algorithm.hpp>
#include <components/nif/data.hpp>
#include <components/nif/property.hpp>
#include <components/nif/texture.hpp>

namespace Lod
{
    namespace
    {
        // Row-major 3 x 4, [rotation and scale | translation]: v' = M3 * v + t.
        using Matrix = std::array<double, 12>;

        Matrix identity()
        {
            return { 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0 };
        }

        // outer * inner: the point goes through `inner` first.
        Matrix multiply(const Matrix& outer, const Matrix& inner)
        {
            Matrix result{};
            for (int row = 0; row < 3; ++row)
            {
                for (int col = 0; col < 3; ++col)
                {
                    double sum = 0;
                    for (int k = 0; k < 3; ++k)
                        sum += outer[row * 4 + k] * inner[k * 4 + col];
                    result[row * 4 + col] = sum;
                }
                double translation = outer[row * 4 + 3];
                for (int k = 0; k < 3; ++k)
                    translation += outer[row * 4 + k] * inner[k * 4 + 3];
                result[row * 4 + 3] = translation;
            }
            return result;
        }

        // NiTransform::toMatrix: entry (row i, column j) is mRotation.mValues[i][j] * mScale.
        Matrix fromTransform(const Nif::NiTransform& transform)
        {
            Matrix result{};
            for (int i = 0; i < 3; ++i)
            {
                for (int j = 0; j < 3; ++j)
                    result[i * 4 + j] = static_cast<double>(transform.mRotation.mValues[i][j]) * transform.mScale;
            }
            result[3] = transform.mTranslation.x;
            result[7] = transform.mTranslation.y;
            result[11] = transform.mTranslation.z;
            return result;
        }

        double determinant(const Matrix& m)
        {
            return m[0] * (m[5] * m[10] - m[6] * m[9]) - m[1] * (m[4] * m[10] - m[6] * m[8])
                + m[2] * (m[4] * m[9] - m[5] * m[8]);
        }

        // What a node passes down to its children.
        struct Inherited
        {
            std::string mTexture;
            bool mHasAlpha = false;
            bool mBlend = false;
            bool mTest = false;
            std::uint8_t mThreshold = 0;
            // The blend factors (NiAlphaProperty codes: 6 source alpha, 7 one minus source alpha).
            int mSourceBlend = 6;
            int mDestinationBlend = 7;
            // The material's alpha, 1 when no material property was seen.
            float mMaterialAlpha = 1.f;
        };

        std::uint8_t toByte(float value)
        {
            return static_cast<std::uint8_t>(std::lround(std::clamp(value, 0.f, 1.f) * 255.f));
        }

        class Flattener
        {
        public:
            explicit Flattener(bool blendAsCutout)
                : mBlendAsCutout(blendAsCutout)
            {
            }

            FlattenedModel run(const Nif::NiAVObject& root)
            {
                visit(root, identity(), Inherited{}, 0);
                finish();
                return std::move(mModel);
            }

        private:
            static constexpr int sMaxDepth = 64;

            void applyProperty(const Nif::NiProperty& property, Inherited& inherited)
            {
                if (property.mRecordType == Nif::RC_NiTexturingProperty)
                {
                    const auto& texturing = static_cast<const Nif::NiTexturingProperty&>(property);
                    inherited.mTexture.clear();
                    if (!texturing.mTextures.empty())
                    {
                        const auto& base = texturing.mTextures[Nif::NiTexturingProperty::BaseTexture];
                        if (base.mEnabled && !base.mSourceTexture.empty() && base.mSourceTexture->mExternal)
                            inherited.mTexture = base.mSourceTexture->mFile;
                    }
                }
                else if (property.mRecordType == Nif::RC_NiMaterialProperty)
                {
                    inherited.mMaterialAlpha = static_cast<const Nif::NiMaterialProperty&>(property).mAlpha;
                }
                else if (property.mRecordType == Nif::RC_NiAlphaProperty)
                {
                    const auto& alpha = static_cast<const Nif::NiAlphaProperty&>(property);
                    inherited.mHasAlpha = true;
                    inherited.mBlend = alpha.useAlphaBlending();
                    inherited.mTest = alpha.useAlphaTesting();
                    inherited.mThreshold = alpha.mThreshold;
                    inherited.mSourceBlend = alpha.sourceBlendMode();
                    inherited.mDestinationBlend = alpha.destinationBlendMode();
                }
            }

            void visit(const Nif::NiAVObject& node, const Matrix& parent, Inherited inherited, int depth)
            {
                if (depth > sMaxDepth || node.isHidden() || node.mRecordType == Nif::RC_RootCollisionNode
                    || node.mRecordType == Nif::RC_AvoidNode || Misc::StringUtils::ciEqual(node.mName, "Bounding Box"))
                    return;
                if (!node.mController.empty())
                    mModel.mTraits.mAnimated = true;
                if (node.mRecordType == Nif::RC_NiBSAnimationNode)
                    mModel.mTraits.mAnimated = true;
                const Matrix world = multiply(parent, fromTransform(node.mTransform));
                for (const auto& property : node.mProperties)
                    if (!property.empty())
                        applyProperty(*property.getPtr(), inherited);

                switch (node.mRecordType)
                {
                    case Nif::RC_NiTriShape:
                    case Nif::RC_NiTriStrips:
                    case Nif::RC_BSLODTriShape:
                        addGeometry(static_cast<const Nif::NiGeometry&>(node), world, inherited);
                        return;
                    case Nif::RC_NiParticles:
                    case Nif::RC_NiParticleSystem:
                        mModel.mTraits.mHasParticles = true;
                        return;
                    default:
                        break;
                }
                if (const auto* group = dynamic_cast<const Nif::NiNode*>(&node))
                {
                    if (node.mRecordType == Nif::RC_NiBSParticleNode)
                        mModel.mTraits.mHasParticles = true;
                    // A billboard turns to face the camera, so its rest pose is not what is drawn: leave it live.
                    if (node.mRecordType == Nif::RC_NiBillboardNode)
                        mModel.mTraits.mAnimated = true;
                    if (node.mRecordType == Nif::RC_NiSwitchNode)
                    {
                        // A switch draws one child: the one it starts with.
                        const std::size_t index = static_cast<const Nif::NiSwitchNode&>(node).mInitialIndex;
                        if (index < group->mChildren.size() && !group->mChildren[index].empty())
                            visit(*group->mChildren[index].getPtr(), world, inherited, depth + 1);
                        return;
                    }
                    if (node.mRecordType == Nif::RC_NiLODNode)
                    {
                        // A level of detail node draws one child by distance; the first is the most detailed.
                        if (!group->mChildren.empty() && !group->mChildren.front().empty())
                            visit(*group->mChildren.front().getPtr(), world, inherited, depth + 1);
                        return;
                    }
                    for (const auto& child : group->mChildren)
                        if (!child.empty())
                            visit(*child.getPtr(), world, inherited, depth + 1);
                }
            }

            void addGeometry(const Nif::NiGeometry& geometry, const Matrix& world, const Inherited& inherited)
            {
                if (geometry.mData.empty())
                    return;
                if (!geometry.mSkin.empty())
                {
                    mModel.mTraits.mSkinned = true;
                    return;
                }
                const Nif::NiGeometryData& data = *geometry.mData.getPtr();
                std::vector<std::uint32_t> triangles;
                if (data.mRecordType == Nif::RC_NiTriShapeData)
                {
                    for (const unsigned short index : static_cast<const Nif::NiTriShapeData&>(data).mTriangles)
                        triangles.push_back(index);
                }
                else if (data.mRecordType == Nif::RC_NiTriStripsData)
                {
                    for (const auto& strip : static_cast<const Nif::NiTriStripsData&>(data).mStrips)
                    {
                        for (std::size_t i = 0; i + 2 < strip.size(); ++i)
                        {
                            const std::uint32_t a = strip[i], b = strip[i + 1], c = strip[i + 2];
                            if (a == b || b == c || a == c)
                                continue;
                            triangles.push_back(a);
                            triangles.push_back(i % 2 == 0 ? b : c);
                            triangles.push_back(i % 2 == 0 ? c : b);
                        }
                    }
                }
                else
                    return;
                if (triangles.empty() || data.mVertices.empty())
                    return;
                for (const std::uint32_t index : triangles)
                    if (index >= data.mVertices.size())
                        return;

                // Geometry may carry its own alpha property (a later format); it overrides the one the nodes passed
                // down.
                Inherited state = inherited;
                if (!geometry.mAlphaProperty.empty())
                    applyProperty(*geometry.mAlphaProperty.getPtr(), state);
                // Only the ordinary blend (source alpha, one minus source alpha) is a cutout; an additive or other
                // blend (glows, light shafts) would draw as solid sheets.
                if (state.mBlend && !state.mTest && mBlendAsCutout && state.mSourceBlend == 6
                    && state.mDestinationBlend == 7 && !state.mTexture.empty() && state.mMaterialAlpha >= 0.99f
                    && !textureLooksTranslucent(state.mTexture))
                {
                    state.mTest = true;
                    state.mThreshold = blendCutoutThreshold;
                }
                if (state.mBlend && !state.mTest)
                {
                    mModel.mTraits.mAlphaBlended = true;
                    return;
                }

                SourcePiece piece;
                piece.mTexture = state.mTexture;
                piece.mFlags = state.mTest ? clusterFlagAlphaTest : 0;
                piece.mAlphaCutoff = state.mTest ? static_cast<float>(state.mThreshold) / 255.f : 0.f;
                const bool mirrored = determinant(world) < 0;
                piece.mVertices.resize(data.mVertices.size());
                for (std::size_t i = 0; i < data.mVertices.size(); ++i)
                {
                    const vsg::vec3& v = data.mVertices[i];
                    SourceVertex& out = piece.mVertices[i];
                    for (int row = 0; row < 3; ++row)
                        out.mPosition[row] = static_cast<float>(world[row * 4] * v.x + world[row * 4 + 1] * v.y
                            + world[row * 4 + 2] * v.z + world[row * 4 + 3]);
                    if (i < data.mNormals.size())
                    {
                        const vsg::vec3& n = data.mNormals[i];
                        double nx = 0, ny = 0, nz = 0;
                        nx = world[0] * n.x + world[1] * n.y + world[2] * n.z;
                        ny = world[4] * n.x + world[5] * n.y + world[6] * n.z;
                        nz = world[8] * n.x + world[9] * n.y + world[10] * n.z;
                        const double length = std::sqrt(nx * nx + ny * ny + nz * nz);
                        if (length > 1e-12)
                        {
                            out.mNormal[0] = static_cast<float>(nx / length);
                            out.mNormal[1] = static_cast<float>(ny / length);
                            out.mNormal[2] = static_cast<float>(nz / length);
                        }
                    }
                    if (!data.mUVList.empty() && i < data.mUVList[0].size())
                    {
                        out.mUv[0] = data.mUVList[0][i].x;
                        out.mUv[1] = data.mUVList[0][i].y;
                    }
                    if (i < data.mColors.size())
                    {
                        out.mColor[0] = toByte(data.mColors[i].r);
                        out.mColor[1] = toByte(data.mColors[i].g);
                        out.mColor[2] = toByte(data.mColors[i].b);
                        out.mColor[3] = toByte(data.mColors[i].a);
                    }
                }
                // A mirror in the node transforms turns the triangles inside out; the winding follows it so that faces
                // keep their front (the builder does the same for the placed object's own transform).
                if (mirrored)
                    for (std::size_t i = 0; i + 2 < triangles.size(); i += 3)
                        std::swap(triangles[i + 1], triangles[i + 2]);
                piece.mIndices = std::move(triangles);
                mModel.mPieces.push_back(std::move(piece));
            }

            void finish()
            {
                std::uint32_t triangles = 0;
                bool any = false;
                float low[3] = { 0, 0, 0 };
                float high[3] = { 0, 0, 0 };
                for (const SourcePiece& piece : mModel.mPieces)
                {
                    triangles += static_cast<std::uint32_t>(piece.mIndices.size() / 3);
                    for (const SourceVertex& v : piece.mVertices)
                    {
                        for (int k = 0; k < 3; ++k)
                        {
                            low[k] = any ? std::min(low[k], v.mPosition[k]) : v.mPosition[k];
                            high[k] = any ? std::max(high[k], v.mPosition[k]) : v.mPosition[k];
                        }
                        any = true;
                    }
                }
                mModel.mTraits.mKind = ObjectKind::Static;
                mModel.mTraits.mTriangleCount = triangles;
                if (!any)
                    return;
                const float centre[3]
                    = { 0.5f * (low[0] + high[0]), 0.5f * (low[1] + high[1]), 0.5f * (low[2] + high[2]) };
                float radiusSquared = 0;
                for (const SourcePiece& piece : mModel.mPieces)
                    for (const SourceVertex& v : piece.mVertices)
                    {
                        float distanceSquared = 0;
                        for (int k = 0; k < 3; ++k)
                            distanceSquared += (v.mPosition[k] - centre[k]) * (v.mPosition[k] - centre[k]);
                        radiusSquared = std::max(radiusSquared, distanceSquared);
                    }
                mModel.mTraits.mRadius = std::sqrt(radiusSquared);
            }

            FlattenedModel mModel;
            bool mBlendAsCutout;
        };
    }

    bool textureLooksTranslucent(std::string_view texture)
    {
        // The base name: after the last slash, before the last dot.
        const std::size_t slash = texture.find_last_of("/\\");
        std::string_view name = slash == std::string_view::npos ? texture : texture.substr(slash + 1);
        const std::size_t dot = name.find_last_of('.');
        if (dot != std::string_view::npos)
            name = name.substr(0, dot);

        static constexpr std::string_view exact[] = { "ray", "rays", "mist", "fog", "rope", "ropes", "web", "webs",
            "window", "windows", "bottle", "bottles", "ghost", "bubble", "bubbles", "smoke", "steam", "glass", "glow",
            "cloud", "clouds", "water", "ice" };
        static constexpr std::string_view prefixes[] = { "water", "ice", "glass", "glow", "smoke", "steam", "cloud",
            "ghost", "bubble" };

        std::string word;
        const auto matches = [](const std::string& token) {
            if (token.empty())
                return false;
            for (const std::string_view keyword : exact)
                if (token == keyword)
                    return true;
            for (const std::string_view prefix : prefixes)
                if (token.size() >= prefix.size() && token.compare(0, prefix.size(), prefix) == 0)
                    return true;
            return false;
        };
        for (const char c : name)
        {
            const auto byte = static_cast<unsigned char>(c);
            if (byte < 0x80 && std::isalpha(byte))
                word += static_cast<char>(std::tolower(byte));
            else
            {
                if (matches(word))
                    return true;
                word.clear();
            }
        }
        return matches(word);
    }

    FlattenedModel flattenNifModel(const Nif::NiAVObject& root, bool blendAsCutout)
    {
        return Flattener(blendAsCutout).run(root);
    }
}
