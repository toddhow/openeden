#include "vertexdecode.hpp"

#include <cstring>

namespace NifScene
{
    namespace
    {
        // The schema's VertexAttribute bits (Nif::BSVertexDesc::VertexAttribute).
        constexpr std::uint16_t sVertex = 0x0001;
        constexpr std::uint16_t sUVs = 0x0002;
        constexpr std::uint16_t sUVs2 = 0x0004;
        constexpr std::uint16_t sNormals = 0x0008;
        constexpr std::uint16_t sTangents = 0x0010;
        constexpr std::uint16_t sColors = 0x0020;
        constexpr std::uint16_t sSkinned = 0x0040;
        constexpr std::uint16_t sLandData = 0x0080;
        constexpr std::uint16_t sEyeData = 0x0100;
        constexpr std::uint16_t sInstance = 0x0200;
        constexpr std::uint16_t sFullPrecision = 0x0400;

        std::uint8_t byteOf(char value)
        {
            // The vertex data keeps bytes in std::array<char, 4>; char is signed on some platforms.
            return static_cast<std::uint8_t>(value);
        }

        Vec3 cross(const Vec3& a, const Vec3& b)
        {
            return { a[1] * b[2] - a[2] * b[1], a[2] * b[0] - a[0] * b[2], a[0] * b[1] - a[1] * b[0] };
        }

        float dot(const Vec3& a, const Vec3& b)
        {
            return a[0] * b[0] + a[1] * b[1] + a[2] * b[2];
        }
    }

    float halfToFloat(std::uint16_t half)
    {
        const std::uint32_t sign = static_cast<std::uint32_t>(half & 0x8000u) << 16;
        const std::uint32_t exponent = (half >> 10) & 0x1Fu;
        const std::uint32_t mantissa = half & 0x3FFu;
        std::uint32_t bits;
        if (exponent == 0)
        {
            if (mantissa == 0)
            {
                bits = sign;
            }
            else
            {
                // A subnormal half is a normal float: shift the mantissa up until its leading one is the implicit one.
                std::uint32_t shifted = mantissa;
                std::int32_t floatExponent = 127 - 15 + 1;
                while ((shifted & 0x400u) == 0)
                {
                    shifted <<= 1;
                    --floatExponent;
                }
                bits = sign | (static_cast<std::uint32_t>(floatExponent) << 23) | ((shifted & 0x3FFu) << 13);
            }
        }
        else if (exponent == 31)
        {
            bits = sign | 0x7F800000u | (mantissa << 13);
        }
        else
        {
            bits = sign | ((exponent + 127 - 15) << 23) | (mantissa << 13);
        }
        float result;
        std::memcpy(&result, &bits, sizeof(result));
        return result;
    }

    float normByte(std::uint8_t value)
    {
        return static_cast<float>(value) / 127.5f - 1.f;
    }

    std::uint32_t packedVertexStride(std::uint16_t flags)
    {
        // Attributes the vertex struct has no field for, and tangents without normals, cannot be stored.
        if ((flags & (sUVs2 | sLandData | sInstance)) != 0)
            return 0;
        if ((flags & sTangents) != 0 && (flags & sNormals) == 0)
            return 0;
        std::uint32_t size = 0;
        if (flags & sVertex)
            size += (flags & sFullPrecision) ? 16 : 8; // three coordinates and the bitangent X or an unused slot
        if (flags & sUVs)
            size += 4;
        if (flags & sNormals)
            size += 4; // three bytes and bitangent Y
        if (flags & sTangents)
            size += 4; // three bytes and bitangent Z
        if (flags & sColors)
            size += 4;
        if (flags & sSkinned)
            size += 12; // four half weights and four byte indices
        if (flags & sEyeData)
            size += 4;
        return size;
    }

    DecodedVertices decodeVertices(std::uint16_t flags, std::span<const Nif::BSVertexData> vertices)
    {
        DecodedVertices out;
        const bool hasVertex = (flags & sVertex) != 0;
        const bool fullPrecision = (flags & sFullPrecision) != 0;
        const bool hasNormals = (flags & sNormals) != 0;
        const bool hasTangents = hasNormals && (flags & sTangents) != 0;
        const std::size_t count = vertices.size();
        if (hasVertex)
            out.mPositions.reserve(count);
        if (flags & sUVs)
            out.mUV0.reserve(count);
        if (hasNormals)
            out.mNormals.reserve(count);
        if (hasTangents)
            out.mTangents.reserve(count);
        if (flags & sColors)
            out.mColors.reserve(count);
        if (flags & sSkinned)
        {
            out.mWeights.reserve(count);
            out.mBoneIndices.reserve(count);
        }

        for (const Nif::BSVertexData& v : vertices)
        {
            float bitangentX = 0.f;
            if (hasVertex)
            {
                if (fullPrecision)
                {
                    out.mPositions.push_back({ v.mVertex.x, v.mVertex.y, v.mVertex.z });
                    bitangentX = v.mVertex.w;
                }
                else
                {
                    out.mPositions.push_back({ halfToFloat(v.mHalfVertex[0]), halfToFloat(v.mHalfVertex[1]),
                        halfToFloat(v.mHalfVertex[2]) });
                    bitangentX = halfToFloat(v.mHalfVertex[3]);
                }
                // The fourth slot is the bitangent X only with the Tangents attribute; it is unused otherwise.
                if (!hasTangents)
                    bitangentX = 0.f;
            }
            if (flags & sUVs)
                out.mUV0.push_back({ halfToFloat(v.mUV[0]), halfToFloat(v.mUV[1]) });
            Vec3 normal{};
            if (hasNormals)
            {
                normal = { normByte(byteOf(v.mNormal[0])), normByte(byteOf(v.mNormal[1])),
                    normByte(byteOf(v.mNormal[2])) };
                out.mNormals.push_back(normal);
            }
            if (hasTangents)
            {
                // The NIF's bitangent is the glTF tangent. The sign makes cross(normal, tangent) * sign point the
                // way the NIF's own tangent points.
                const Vec3 gltfTangent{ bitangentX, normByte(byteOf(v.mNormal[3])), normByte(byteOf(v.mTangent[3])) };
                const Vec3 nifTangent{ normByte(byteOf(v.mTangent[0])), normByte(byteOf(v.mTangent[1])),
                    normByte(byteOf(v.mTangent[2])) };
                const float sign = dot(cross(normal, gltfTangent), nifTangent) < 0.f ? -1.f : 1.f;
                out.mTangents.push_back({ gltfTangent[0], gltfTangent[1], gltfTangent[2], sign });
            }
            if (flags & sColors)
            {
                out.mColors.push_back({ static_cast<float>(byteOf(v.mVertColor[0])) / 255.f,
                    static_cast<float>(byteOf(v.mVertColor[1])) / 255.f,
                    static_cast<float>(byteOf(v.mVertColor[2])) / 255.f,
                    static_cast<float>(byteOf(v.mVertColor[3])) / 255.f });
            }
            if (flags & sSkinned)
            {
                out.mWeights.push_back({ halfToFloat(v.mBoneWeights[0]), halfToFloat(v.mBoneWeights[1]),
                    halfToFloat(v.mBoneWeights[2]), halfToFloat(v.mBoneWeights[3]) });
                out.mBoneIndices.push_back({ byteOf(v.mBoneIndices[0]), byteOf(v.mBoneIndices[1]),
                    byteOf(v.mBoneIndices[2]), byteOf(v.mBoneIndices[3]) });
            }
        }
        return out;
    }
}
