#ifndef OPENEDEN_COMPONENTS_NIFSCENE_VERTEXDECODE_HPP
#define OPENEDEN_COMPONENTS_NIFSCENE_VERTEXDECODE_HPP

// The packed vertex formats of Skyrim SE and later (BSVertexDesc, BSVertexData), decoded to plain arrays.
// Layout and byte mapping per the schema's BSVertexData (research note 0044, items 3 and 4).

#include <array>
#include <cstdint>
#include <span>
#include <vector>

#include <components/nif/node.hpp>

#include "scene.hpp"

namespace NifScene
{
    /// An IEEE 754 half-precision float as a float. Infinities and NaNs stay infinities and NaNs.
    float halfToFloat(std::uint16_t half);

    /// A normal, tangent or bitangent byte as a float: 0 maps to -1 and 255 to 1.
    float normByte(std::uint8_t value);

    /// The number of bytes one packed vertex takes with these vertex attribute flags (Nif::BSVertexDesc::mFlags,
    /// whose Full_Precision bit is set for Skyrim SE), or 0 for a flag combination the format cannot hold.
    std::uint32_t packedVertexStride(std::uint16_t flags);

    struct DecodedVertices
    {
        std::vector<Vec3> mPositions;
        std::vector<Vec3> mNormals;
        std::vector<Vec4> mTangents; // glTF-style, see scene.hpp
        std::vector<Vec2> mUV0;
        std::vector<Vec4> mColors;
        /// Four weights and four bone indices per vertex, as packed; empty unless the Skinned attribute is set.
        std::vector<std::array<float, 4>> mWeights;
        std::vector<std::array<std::uint8_t, 4>> mBoneIndices;
    };

    /// Decodes vertices read with the given attribute flags. An attribute whose flag is not set leaves its array
    /// empty. Never reads beyond the span.
    DecodedVertices decodeVertices(std::uint16_t flags, std::span<const Nif::BSVertexData> vertices);
}

#endif
