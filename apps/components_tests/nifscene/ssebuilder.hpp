#ifndef OPENEDEN_TEST_SUITE_NIFSCENE_SSEBUILDER_H
#define OPENEDEN_TEST_SUITE_NIFSCENE_SSEBUILDER_H

// Generated Skyrim SE (20.2.0.7, user version 12, stream version 100) blocks for the scene tests (task 0082).
// Layouts follow Nif::*::read in components/nif and the schema's BSVertexData, written here by hand.

#include <array>
#include <cstdint>
#include <string>
#include <vector>

#include "../nif/nifbuilder.hpp"

namespace NifScene::Testing
{
    using Nif::Testing::Block;
    using Nif::Testing::ByteWriter;
    using Nif::Testing::GamebryoFile;
    using Nif::Testing::toHalf;

    // Vertex attribute flags (the schema's VertexAttribute bits).
    constexpr std::uint16_t VF_Vertex = 0x1;
    constexpr std::uint16_t VF_UVs = 0x2;
    constexpr std::uint16_t VF_Normals = 0x8;
    constexpr std::uint16_t VF_Tangents = 0x10;
    constexpr std::uint16_t VF_Colors = 0x20;
    constexpr std::uint16_t VF_Skinned = 0x40;
    constexpr std::uint16_t VF_Eye = 0x100;

    // The bytes one SSE vertex takes: positions are three floats and a fourth dword (bitangent X or unused).
    inline std::uint32_t sseStride(std::uint16_t flags)
    {
        std::uint32_t size = 0;
        if (flags & VF_Vertex)
            size += 16;
        if (flags & VF_UVs)
            size += 4;
        if (flags & VF_Normals)
            size += 4;
        if ((flags & (VF_Normals | VF_Tangents)) == (VF_Normals | VF_Tangents))
            size += 4;
        if (flags & VF_Colors)
            size += 4;
        if (flags & VF_Skinned)
            size += 12;
        if (flags & VF_Eye)
            size += 4;
        return size;
    }

    inline std::uint64_t vertexDesc(std::uint16_t flags, std::uint32_t strideBytes)
    {
        return static_cast<std::uint64_t>(strideBytes / 4) | (static_cast<std::uint64_t>(flags) << 44);
    }

    struct Vertex
    {
        std::array<float, 3> mPosition{};
        float mBitangentX = 0.f;
        std::array<float, 2> mUV{};
        std::array<std::uint8_t, 3> mNormal{ 128, 128, 255 };
        std::uint8_t mBitangentY = 128;
        std::array<std::uint8_t, 3> mTangent{ 255, 128, 128 };
        std::uint8_t mBitangentZ = 128;
        std::array<std::uint8_t, 4> mColor{ 255, 255, 255, 255 };
        std::array<float, 4> mWeights{};
        std::array<std::uint8_t, 4> mBones{};
    };

    inline void writeVertex(ByteWriter& out, std::uint16_t flags, const Vertex& v)
    {
        if (flags & VF_Vertex)
        {
            out.floats({ v.mPosition[0], v.mPosition[1], v.mPosition[2] });
            out.put(v.mBitangentX); // bitangent X when the Tangents bit is set, an unused dword otherwise
        }
        if (flags & VF_UVs)
        {
            out.put(toHalf(v.mUV[0]));
            out.put(toHalf(v.mUV[1]));
        }
        if (flags & VF_Normals)
        {
            for (std::uint8_t b : v.mNormal)
                out.put(b);
            out.put(v.mBitangentY);
            if (flags & VF_Tangents)
            {
                for (std::uint8_t b : v.mTangent)
                    out.put(b);
                out.put(v.mBitangentZ);
            }
        }
        if (flags & VF_Colors)
        {
            for (std::uint8_t b : v.mColor)
                out.put(b);
        }
        if (flags & VF_Skinned)
        {
            for (float w : v.mWeights)
                out.put(toHalf(w));
            for (std::uint8_t b : v.mBones)
                out.put(b);
        }
        if (flags & VF_Eye)
            out.put(0.f);
    }

    // A NiNode (or the BSFadeNode type name if asked) with its children.
    inline Block node(GamebryoFile& file, const std::string& name, const std::vector<std::int32_t>& children,
        const std::array<float, 3>& translation = {}, float scale = 1.f, const std::string& type = "NiNode")
    {
        Block block;
        block.mType = type;
        Nif::Testing::writeAvObject(block.mData, file.string(name), translation, scale);
        block.mData.put(static_cast<std::uint32_t>(children.size()));
        for (std::int32_t child : children)
            block.mData.put(child);
        block.mData.put(static_cast<std::uint32_t>(0)); // effects
        return block;
    }

    struct ShapeSpec
    {
        std::string mName = "Shape";
        std::string mType = "BSTriShape";
        std::int32_t mSkin = -1;
        std::uint16_t mFlags = VF_Vertex;
        // 0 means "use sseStride(mFlags)"; tests set it to write a descriptor that disagrees with the flags.
        std::uint32_t mDeclaredStride = 0;
        std::vector<Vertex> mVertices;
        std::vector<std::array<std::uint16_t, 3>> mTriangles;
        // Write the vertices and triangles even if there are none (Data Size is then the byte count of both).
        bool mStoreData = true;
        std::vector<std::array<float, 4>> mDynamic; // BSDynamicTriShape's positions
        std::array<float, 3> mTranslation{};
    };

    inline Block shape(GamebryoFile& file, const ShapeSpec& spec)
    {
        Block block;
        block.mType = spec.mType;
        ByteWriter& out = block.mData;
        Nif::Testing::writeAvObject(out, file.string(spec.mName), spec.mTranslation);
        out.floats({ 0.f, 0.f, 0.f, 1.f }); // bounding sphere
        out.put(spec.mSkin);
        out.put(static_cast<std::int32_t>(-1)); // shader property
        out.put(static_cast<std::int32_t>(-1)); // alpha property
        const std::uint32_t stride = spec.mDeclaredStride != 0 ? spec.mDeclaredStride : sseStride(spec.mFlags);
        out.put(vertexDesc(spec.mFlags, stride));
        out.put(static_cast<std::uint16_t>(spec.mTriangles.size()));
        out.put(static_cast<std::uint16_t>(spec.mVertices.size()));
        const std::uint32_t dataSize = spec.mStoreData
            ? static_cast<std::uint32_t>(spec.mVertices.size() * sseStride(spec.mFlags) + spec.mTriangles.size() * 6)
            : 0;
        out.put(dataSize);
        if (dataSize > 0)
        {
            for (const Vertex& v : spec.mVertices)
                writeVertex(out, spec.mFlags, v);
            for (const auto& triangle : spec.mTriangles)
            {
                for (std::uint16_t index : triangle)
                    out.put(index);
            }
        }
        out.put(static_cast<std::uint32_t>(0)); // particle data size
        if (spec.mType == "BSDynamicTriShape")
        {
            out.put(static_cast<std::uint32_t>(spec.mDynamic.size() * 16));
            for (const auto& p : spec.mDynamic)
                out.floats({ p[0], p[1], p[2], p[3] });
        }
        else if (spec.mType == "BSSubIndexTriShape")
        {
            out.put(static_cast<std::uint32_t>(0)); // segments
        }
        return block;
    }

    struct PartitionSpec
    {
        std::vector<std::uint16_t> mBones;
        std::vector<std::uint16_t> mVertexMap; // partition vertex to global vertex
        std::vector<std::array<std::uint16_t, 3>> mTriangles; // global indices (the "true" triangles)
        std::uint8_t mLodLevel = 0;
        bool mGlobalVB = true;
    };

    inline Block skinPartition(
        const std::vector<Vertex>& vertices, std::uint16_t flags, const std::vector<PartitionSpec>& partitions)
    {
        Block block;
        block.mType = "NiSkinPartition";
        ByteWriter& out = block.mData;
        const std::uint32_t stride = sseStride(flags);
        out.put(static_cast<std::uint32_t>(partitions.size()));
        out.put(static_cast<std::uint32_t>(vertices.size() * stride));
        out.put(stride);
        out.put(vertexDesc(flags, stride));
        for (const Vertex& v : vertices)
            writeVertex(out, flags, v);
        for (const PartitionSpec& p : partitions)
        {
            const std::uint16_t numVertices = static_cast<std::uint16_t>(p.mVertexMap.size());
            const std::uint16_t numTriangles = static_cast<std::uint16_t>(p.mTriangles.size());
            const std::uint16_t bonesPerVertex = 4;
            out.put(numVertices);
            out.put(numTriangles);
            out.put(static_cast<std::uint16_t>(p.mBones.size()));
            out.put(static_cast<std::uint16_t>(0)); // strips
            out.put(bonesPerVertex);
            for (std::uint16_t b : p.mBones)
                out.put(b);
            out.put(static_cast<std::uint8_t>(1)); // has vertex map
            for (std::uint16_t v : p.mVertexMap)
                out.put(v);
            out.put(static_cast<std::uint8_t>(1)); // has weights
            for (std::size_t i = 0; i < static_cast<std::size_t>(numVertices) * bonesPerVertex; ++i)
                out.put(0.f);
            out.put(static_cast<std::uint8_t>(1)); // has faces: partition-local triangles
            for (const auto& t : p.mTriangles)
            {
                for (std::uint16_t index : t)
                {
                    // Local index: the position of the global vertex in the vertex map.
                    std::uint16_t local = 0;
                    for (std::size_t i = 0; i < p.mVertexMap.size(); ++i)
                    {
                        if (p.mVertexMap[i] == index)
                            local = static_cast<std::uint16_t>(i);
                    }
                    out.put(local);
                }
            }
            out.put(static_cast<std::uint8_t>(1)); // has bone indices
            for (std::size_t i = 0; i < static_cast<std::size_t>(numVertices) * bonesPerVertex; ++i)
                out.put(static_cast<std::uint8_t>(0));
            out.put(p.mLodLevel);
            out.put(static_cast<std::uint8_t>(p.mGlobalVB ? 1 : 0));
            out.put(vertexDesc(flags, stride));
            for (const auto& t : p.mTriangles)
            {
                for (std::uint16_t index : t)
                    out.put(index);
            }
        }
        return block;
    }

    // NiSkinData with n bones, bind translations (i, 0, 0), no per-bone vertex weights.
    inline Block skinData(std::size_t bones)
    {
        Block block;
        block.mType = "NiSkinData";
        ByteWriter& out = block.mData;
        out.floats({ 1, 0, 0, 0, 1, 0, 0, 0, 1 }); // skin transform: rotation
        out.floats({ 0.f, 0.f, 0.5f }); // translation
        out.put(1.f); // scale
        out.put(static_cast<std::uint32_t>(bones));
        out.put(static_cast<std::uint8_t>(0)); // has vertex weights
        for (std::size_t i = 0; i < bones; ++i)
        {
            out.floats({ 1, 0, 0, 0, 1, 0, 0, 0, 1 });
            out.floats({ static_cast<float>(i), 0.f, 0.f });
            out.put(1.f);
            out.floats({ 0.f, 0.f, 0.f, 1.f }); // bounding sphere
            out.put(static_cast<std::uint16_t>(0)); // vertices
        }
        return block;
    }

    inline Block skinInstance(
        std::int32_t data, std::int32_t partitions, std::int32_t root, const std::vector<std::int32_t>& bones)
    {
        Block block;
        block.mType = "BSDismemberSkinInstance";
        ByteWriter& out = block.mData;
        out.put(data);
        out.put(partitions);
        out.put(root);
        out.put(static_cast<std::uint32_t>(bones.size()));
        for (std::int32_t bone : bones)
            out.put(bone);
        out.put(static_cast<std::uint32_t>(1)); // body parts
        out.put(static_cast<std::uint16_t>(1));
        out.put(static_cast<std::uint16_t>(32));
        return block;
    }
}

#endif
