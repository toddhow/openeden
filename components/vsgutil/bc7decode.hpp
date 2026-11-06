#ifndef OPENEDEN_VSGUTIL_BC7DECODE_H
#define OPENEDEN_VSGUTIL_BC7DECODE_H

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <utility>

/*
 * Task 0089: a BC7 (BPTC) block decoder, so BC7 terrain layers can join the layer arrays. Pure function, no vsg types.
 *
 * The layout is the one of the Khronos Data Format Specification (BPTC) and Microsoft's BC7 format documentation:
 * a unary mode prefix, then the partition, rotation and index selection fields, the endpoint colours channel by
 * channel, the alpha endpoints, the p bits and the index data (the index at each subset's anchor texel has its most
 * significant bit left out). The partition tables below were read out of the D3D11 reference decoder (blocks that
 * show each texel's subset), not copied from a source file; the decoder as a whole is checked against that decoder
 * over random blocks of every mode (apps/components_tests/vsgutil/testbc7decode.cpp).
 */
namespace vsgUtil::Bc7
{
    constexpr size_t blockBytes = 16;

    namespace detail
    {
        // Two bits a texel (texel 0 in the lowest bits): the subset of each texel, for the 64 two subset partitions.
        inline constexpr uint32_t partition2[64] = {
#include "bc7partition2.inc"
        };
        // The same for the 64 three subset partitions (mode 0 uses the first 16).
        inline constexpr uint32_t partition3[64] = {
#include "bc7partition3.inc"
        };

        // The anchor texel of the second subset of a two subset partition.
        inline constexpr uint8_t anchor2[64] = { 15, 15, 15, 15, 15, 15, 15, 15, 15, 15, 15, 15, 15, 15, 15, 15, 15, 2,
            8, 2, 2, 8, 8, 15, 2, 8, 2, 2, 8, 8, 2, 2, 15, 15, 6, 8, 2, 8, 15, 15, 2, 8, 2, 2, 2, 15, 15, 6, 6, 2, 6, 8,
            15, 15, 2, 2, 15, 15, 15, 15, 15, 2, 2, 15 };
        // The anchor texels of the second and third subsets of a three subset partition.
        inline constexpr uint8_t anchor3a[64] = { 3, 3, 15, 15, 8, 3, 15, 15, 8, 8, 6, 6, 6, 5, 3, 3, 3, 3, 8, 15, 3, 3,
            6, 10, 5, 8, 8, 6, 8, 5, 15, 15, 8, 15, 3, 5, 6, 10, 8, 15, 15, 3, 15, 5, 15, 15, 15, 15, 3, 15, 5, 5, 5, 8,
            5, 10, 5, 10, 8, 13, 15, 12, 3, 3 };
        inline constexpr uint8_t anchor3b[64] = { 15, 8, 8, 3, 15, 15, 3, 8, 15, 15, 15, 15, 15, 15, 15, 8, 15, 8, 15,
            3, 15, 8, 15, 8, 3, 15, 6, 10, 15, 15, 10, 8, 15, 3, 15, 10, 10, 8, 9, 10, 6, 15, 8, 15, 3, 6, 6, 8, 15, 3,
            15, 15, 15, 15, 15, 15, 15, 15, 15, 15, 3, 15, 15, 8 };

        struct ModeInfo
        {
            uint8_t subsets;
            uint8_t partitionBits;
            uint8_t rotationBits;
            uint8_t indexSelectionBits;
            uint8_t colourBits;
            uint8_t alphaBits;
            uint8_t endpointPBits; // a p bit for each endpoint
            uint8_t sharedPBits; // a p bit for each subset, shared by its two endpoints
            uint8_t indexBits;
            uint8_t secondIndexBits;
        };

        inline constexpr ModeInfo modes[8] = {
            { 3, 4, 0, 0, 4, 0, 1, 0, 3, 0 },
            { 2, 6, 0, 0, 6, 0, 0, 1, 3, 0 },
            { 3, 6, 0, 0, 5, 0, 0, 0, 2, 0 },
            { 2, 6, 0, 0, 7, 0, 1, 0, 2, 0 },
            { 1, 0, 2, 1, 5, 6, 0, 0, 2, 3 },
            { 1, 0, 2, 0, 7, 8, 0, 0, 2, 2 },
            { 1, 0, 0, 0, 7, 7, 1, 0, 4, 0 },
            { 2, 6, 0, 0, 5, 5, 1, 0, 2, 0 },
        };

        inline constexpr uint8_t weights2[4] = { 0, 21, 43, 64 };
        inline constexpr uint8_t weights3[8] = { 0, 9, 18, 27, 37, 46, 55, 64 };
        inline constexpr uint8_t weights4[16] = { 0, 4, 9, 13, 17, 21, 26, 30, 34, 38, 43, 47, 51, 55, 60, 64 };

        inline uint8_t interpolate(int e0, int e1, int weight)
        {
            return static_cast<uint8_t>((e0 * (64 - weight) + e1 * weight + 32) >> 6);
        }

        inline int weightOf(int bits, int index)
        {
            return bits == 2 ? weights2[index] : bits == 3 ? weights3[index] : weights4[index];
        }

        struct BitReader
        {
            const uint8_t* bytes;
            int position = 0;
            uint32_t read(int count)
            {
                uint32_t value = 0;
                for (int i = 0; i < count; ++i, ++position)
                {
                    value |= static_cast<uint32_t>((bytes[position >> 3] >> (position & 7)) & 1) << i;
                }
                return value;
            }
        };

        inline int subsetOf(const ModeInfo& mode, int partition, int texel)
        {
            if (mode.subsets == 1)
            {
                return 0;
            }
            const uint32_t table = mode.subsets == 2 ? partition2[partition] : partition3[partition];
            return static_cast<int>((table >> (2 * texel)) & 3);
        }

        inline bool isAnchor(const ModeInfo& mode, int partition, int texel)
        {
            if (texel == 0)
            {
                return true;
            }
            if (mode.subsets == 2)
            {
                return anchor2[partition] == texel;
            }
            if (mode.subsets == 3)
            {
                return anchor3a[partition] == texel || anchor3b[partition] == texel;
            }
            return false;
        }
    }

    /// Decodes one 16 byte block to 16 texels of RGBA8 (row-major inside the block, 64 bytes).
    inline void decodeBlock(const uint8_t* block, uint8_t* rgba)
    {
        int modeNumber = 0;
        while (modeNumber < 8 && !((block[0] >> modeNumber) & 1))
        {
            ++modeNumber;
        }
        if (modeNumber == 8)
        {
            // A reserved mode: all zero, alpha included.
            std::memset(rgba, 0, 64);
            return;
        }
        const detail::ModeInfo& mode = detail::modes[modeNumber];
        detail::BitReader reader{ block };
        reader.position = modeNumber + 1;
        const int partition = static_cast<int>(reader.read(mode.partitionBits));
        const int rotation = static_cast<int>(reader.read(mode.rotationBits));
        const int indexSelection = static_cast<int>(reader.read(mode.indexSelectionBits));

        const int endpoints = mode.subsets * 2;
        int endpoint[6][4];
        for (int channel = 0; channel < 3; ++channel)
        {
            for (int e = 0; e < endpoints; ++e)
            {
                endpoint[e][channel] = static_cast<int>(reader.read(mode.colourBits));
            }
        }
        for (int e = 0; e < endpoints; ++e)
        {
            endpoint[e][3] = mode.alphaBits ? static_cast<int>(reader.read(mode.alphaBits)) : 255;
        }
        // The p bits become the lowest bit of the endpoints (of the alpha too, where there is alpha).
        int pBit[6] = { 0, 0, 0, 0, 0, 0 };
        if (mode.endpointPBits)
        {
            for (int e = 0; e < endpoints; ++e)
            {
                pBit[e] = static_cast<int>(reader.read(1));
            }
        }
        else if (mode.sharedPBits)
        {
            for (int subset = 0; subset < mode.subsets; ++subset)
            {
                pBit[2 * subset] = pBit[2 * subset + 1] = static_cast<int>(reader.read(1));
            }
        }
        const bool hasP = mode.endpointPBits || mode.sharedPBits;
        const int colourBits = mode.colourBits + (hasP ? 1 : 0);
        const int alphaBits = mode.alphaBits + (hasP && mode.alphaBits ? 1 : 0);
        auto expand = [](int value, int bits) { return (value << (8 - bits)) | (value >> (2 * bits - 8)); };
        for (int e = 0; e < endpoints; ++e)
        {
            for (int channel = 0; channel < 3; ++channel)
            {
                const int value = hasP ? (endpoint[e][channel] << 1) | pBit[e] : endpoint[e][channel];
                endpoint[e][channel] = expand(value, colourBits);
            }
            if (mode.alphaBits)
            {
                const int value = hasP ? (endpoint[e][3] << 1) | pBit[e] : endpoint[e][3];
                endpoint[e][3] = expand(value, alphaBits);
            }
        }

        // Modes 4 and 5 have a second set of indices for the alpha channel.
        const bool twoIndexSets = mode.secondIndexBits != 0;
        int firstIndex[16];
        int secondIndex[16];
        for (int texel = 0; texel < 16; ++texel)
        {
            const bool anchor = detail::isAnchor(mode, partition, texel);
            firstIndex[texel] = static_cast<int>(reader.read(mode.indexBits - (anchor ? 1 : 0)));
        }
        if (twoIndexSets)
        {
            for (int texel = 0; texel < 16; ++texel)
            {
                const bool anchor = detail::isAnchor(mode, partition, texel);
                secondIndex[texel] = static_cast<int>(reader.read(mode.secondIndexBits - (anchor ? 1 : 0)));
            }
        }

        for (int texel = 0; texel < 16; ++texel)
        {
            const int subset = detail::subsetOf(mode, partition, texel);
            const int* e0 = endpoint[2 * subset];
            const int* e1 = endpoint[2 * subset + 1];
            int colourIndex = firstIndex[texel];
            int colourIndexBits = mode.indexBits;
            int alphaIndex = firstIndex[texel];
            int alphaIndexBits = mode.indexBits;
            if (twoIndexSets)
            {
                alphaIndex = secondIndex[texel];
                alphaIndexBits = mode.secondIndexBits;
                if (indexSelection)
                {
                    std::swap(colourIndex, alphaIndex);
                    std::swap(colourIndexBits, alphaIndexBits);
                }
            }
            const int colourWeight = detail::weightOf(colourIndexBits, colourIndex);
            const int alphaWeight = detail::weightOf(alphaIndexBits, alphaIndex);
            uint8_t texelValue[4];
            for (int channel = 0; channel < 3; ++channel)
            {
                texelValue[channel] = detail::interpolate(e0[channel], e1[channel], colourWeight);
            }
            texelValue[3] = detail::interpolate(e0[3], e1[3], alphaWeight);
            if (rotation)
            {
                std::swap(texelValue[3], texelValue[rotation - 1]);
            }
            std::memcpy(rgba + 4 * texel, texelValue, 4);
        }
    }
}

#endif
