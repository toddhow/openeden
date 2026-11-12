#ifndef OPENEDEN_TESTS_HAVOKPACK_SPLINEGEN_H
#define OPENEDEN_TESTS_HAVOKPACK_SPLINEGEN_H

// Task 0119: our own encoder of hkaSkeleton and hkaSplineCompressedAnimation objects for tests. It writes the layout
// documented in docs/research/0054-provenance.md from first principles (the inverse of the reader), never from a game
// file. The tests that use it say what the decoded values must be by hand arithmetic.

#include "packgen.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <string>
#include <vector>

namespace HavokPackTest
{
    inline void put(std::vector<std::uint8_t>& v, const void* p, std::size_t n)
    {
        const auto* b = static_cast<const std::uint8_t*>(p);
        v.insert(v.end(), b, b + n);
    }

    inline void putFloat(std::vector<std::uint8_t>& v, float f) { put(v, &f, 4); }

    inline void padTo4(std::vector<std::uint8_t>& v, std::size_t blockStart)
    {
        while ((v.size() - blockStart) % 4 != 0)
            v.push_back(0);
    }

    // x y z w with the largest component dropped, 12 bits per kept component around the centre 2047.
    inline std::array<std::uint8_t, 5> packQuaternion40(std::array<float, 4> q)
    {
        std::size_t big = 0;
        for (std::size_t i = 1; i < 4; ++i)
            if (std::fabs(q[i]) > std::fabs(q[big]))
                big = i;
        const bool negative = q[big] < 0;
        const float step = 0.70710678118654752f / 2047.0f;
        std::uint64_t v = 0;
        int slot = 0;
        for (std::size_t i = 0; i < 4; ++i)
        {
            if (i == big)
                continue;
            const long value = std::lround(q[i] / step) + 2047;
            v |= static_cast<std::uint64_t>(std::clamp<long>(value, 0, 4095)) << (12 * slot++);
        }
        v |= static_cast<std::uint64_t>(big) << 36;
        v |= static_cast<std::uint64_t>(negative ? 1 : 0) << 38;
        std::array<std::uint8_t, 5> out{};
        for (int i = 0; i < 5; ++i)
            out[i] = static_cast<std::uint8_t>(v >> (8 * i));
        return out;
    }

    // One vector component (position or scale) of one track in one block.
    struct VecDesc
    {
        // per axis: constant value, or an animated axis with min, max and 16-bit control values
        struct Axis
        {
            enum Kind { None, Constant, Spline } mKind = None;
            float mValue = 0; // Constant
            float mMin = 0, mMax = 1; // Spline
            std::vector<std::uint16_t> mControl; // Spline: one per item
        };
        std::array<Axis, 3> mAxis;
        std::uint8_t mDegree = 1;
        std::vector<std::uint8_t> mKnots; // items + degree + 1
        std::uint8_t mask() const
        {
            std::uint8_t m = 0;
            for (int i = 0; i < 3; ++i)
            {
                if (mAxis[i].mKind == Axis::Constant)
                    m |= 1 << i;
                else if (mAxis[i].mKind == Axis::Spline)
                    m |= 16 << i;
            }
            return m;
        }
        std::size_t items() const
        {
            for (const auto& a : mAxis)
                if (a.mKind == Axis::Spline)
                    return a.mControl.size();
            return 0;
        }
    };

    struct RotDesc
    {
        enum Kind { Identity, Constant, Spline } mKind = Identity;
        std::array<float, 4> mConstant{ 0, 0, 0, 1 };
        std::vector<std::array<float, 4>> mControl;
        std::uint8_t mDegree = 1;
        std::vector<std::uint8_t> mKnots;
        std::uint8_t mask() const { return mKind == Identity ? 0 : (mKind == Constant ? 0x0F : 0xF0); }
    };

    struct TrackDesc
    {
        VecDesc mPos;
        RotDesc mRot;
        VecDesc mScale;
    };

    // One float track of one block (stage 7): mask 0 stores nothing, 0x03 one float, 0x12 a spline of 16-bit values
    // between a min and a max.
    struct FloatDesc
    {
        enum Kind { Zero, Constant, Spline } mKind = Constant;
        float mValue = 0; // Constant
        float mMin = 0, mMax = 1; // Spline
        std::vector<std::uint16_t> mControl; // Spline
        std::uint8_t mDegree = 1;
        std::vector<std::uint8_t> mKnots; // items + degree + 1
        int mRawMask = -1; // >= 0: written as the mask byte instead (to test refusals)
        std::uint8_t mask() const
        {
            if (mRawMask >= 0)
                return static_cast<std::uint8_t>(mRawMask);
            return mKind == Zero ? 0 : (mKind == Constant ? 0x03 : 0x12);
        }
    };

    inline void writeSplineHeader(
        std::vector<std::uint8_t>& d, std::size_t block, std::size_t items, std::uint8_t degree,
        const std::vector<std::uint8_t>& knots)
    {
        padTo4(d, block);
        const std::uint16_t n = static_cast<std::uint16_t>(items - 1);
        put(d, &n, 2);
        d.push_back(degree);
        d.insert(d.end(), knots.begin(), knots.end());
    }

    inline void writeVec(std::vector<std::uint8_t>& d, std::size_t block, const VecDesc& v)
    {
        const std::uint8_t mask = v.mask();
        if ((mask >> 4) != 0)
        {
            writeSplineHeader(d, block, v.items(), v.mDegree, v.mKnots);
            padTo4(d, block);
            for (const auto& a : v.mAxis)
            {
                if (a.mKind == VecDesc::Axis::Constant)
                    putFloat(d, a.mValue);
                else if (a.mKind == VecDesc::Axis::Spline)
                {
                    putFloat(d, a.mMin);
                    putFloat(d, a.mMax);
                }
            }
            for (std::size_t item = 0; item < v.items(); ++item)
                for (const auto& a : v.mAxis)
                    if (a.mKind == VecDesc::Axis::Spline)
                        put(d, &a.mControl[item], 2);
        }
        else if ((mask & 7) != 0)
        {
            padTo4(d, block);
            for (const auto& a : v.mAxis)
                if (a.mKind == VecDesc::Axis::Constant)
                    putFloat(d, a.mValue);
        }
    }

    // The data array of one block: masks, then the tracks; returns the offset where the float tracks would start.
    inline std::size_t writeBlock(std::vector<std::uint8_t>& d, const std::vector<TrackDesc>& tracks, std::size_t maskSize,
        const std::vector<FloatDesc>* floats = nullptr)
    {
        const std::size_t block = d.size();
        for (const auto& t : tracks)
        {
            d.push_back(0x45);
            d.push_back(t.mPos.mask());
            d.push_back(t.mRot.mask());
            d.push_back(t.mScale.mask());
        }
        d.resize(block + maskSize, 0);
        if (floats != nullptr)
            for (std::size_t i = 0; i < floats->size(); ++i)
                d[block + 4 * tracks.size() + i] = (*floats)[i].mask();
        for (const auto& t : tracks)
        {
            writeVec(d, block, t.mPos);
            if (t.mRot.mKind == RotDesc::Spline)
            {
                writeSplineHeader(d, block, t.mRot.mControl.size(), t.mRot.mDegree, t.mRot.mKnots);
                for (const auto& q : t.mRot.mControl)
                {
                    const auto b = packQuaternion40(q);
                    d.insert(d.end(), b.begin(), b.end());
                }
            }
            else if (t.mRot.mKind == RotDesc::Constant)
            {
                padTo4(d, block);
                const auto b = packQuaternion40(t.mRot.mConstant);
                d.insert(d.end(), b.begin(), b.end());
            }
            writeVec(d, block, t.mScale);
        }
        padTo4(d, block);
        const std::size_t floatsAt = d.size() - block;
        if (floats != nullptr)
        {
            for (const auto& f : *floats)
            {
                if (f.mKind == FloatDesc::Constant && f.mRawMask < 0)
                {
                    padTo4(d, block);
                    putFloat(d, f.mValue);
                }
                else if (f.mKind == FloatDesc::Spline && f.mRawMask < 0)
                {
                    writeSplineHeader(d, block, f.mControl.size(), f.mDegree, f.mKnots);
                    padTo4(d, block);
                    putFloat(d, f.mMin);
                    putFloat(d, f.mMax);
                    for (std::uint16_t q : f.mControl)
                        put(d, &q, 2);
                }
            }
            while ((d.size() - block) % 16 != 0) // a block ends on a 16 byte boundary when it has float tracks
                d.push_back(0);
        }
        return floatsAt;
    }

    struct ClipDesc
    {
        std::vector<std::vector<TrackDesc>> mBlocks; // [block][track]
        std::uint32_t mNumFrames = 2;
        std::uint32_t mMaxFramesPerBlock = 256;
        float mFrameDuration = 1.0f / 30.0f;
        std::uint32_t mFloatTracks = 0;
        std::vector<std::int16_t> mTrackToBone; // empty: identity
        // [block][float track]. When not empty these are written (masks and data) and mFloatTracks is ignored; when
        // empty the legacy output (zero masks, zero bytes for mFloatTracks tracks) is unchanged.
        std::vector<std::vector<FloatDesc>> mFloats;
        std::vector<std::int16_t> mFloatSlots; // empty: float track i drives slot i
        std::uint32_t mFloatTailExtra = 0; // extra bytes after a block's padded float data (to test refusals)
        std::string mSkeletonName = "Skeleton";
        std::uint8_t mType = 5;
    };

    // Adds the animation and its binding to the pack; returns the animation object's offset.
    inline std::uint32_t addSplineClip(PackGen& g, const ClipDesc& c)
    {
        const std::uint32_t tracks = static_cast<std::uint32_t>(c.mBlocks.front().size());
        const bool realFloats = !c.mFloats.empty();
        const std::uint32_t floatTracks = realFloats ? static_cast<std::uint32_t>(c.mFloats.front().size()) : c.mFloatTracks;
        const std::uint32_t maskSize = realFloats ? (4 * tracks + floatTracks + 3) / 4 * 4 : 4 * tracks + c.mFloatTracks;
        std::vector<std::uint8_t> data;
        std::vector<std::uint32_t> offsets, floatOffsets;
        for (std::size_t bi = 0; bi < c.mBlocks.size(); ++bi)
        {
            offsets.push_back(static_cast<std::uint32_t>(data.size()));
            floatOffsets.push_back(static_cast<std::uint32_t>(
                writeBlock(data, c.mBlocks[bi], maskSize, realFloats ? &c.mFloats[bi] : nullptr)));
            if (!realFloats)
                data.resize(data.size() + 4 * c.mFloatTracks, 0); // the float tracks' bytes (unused by the legacy tests)
            else
                data.resize(data.size() + c.mFloatTailExtra, 0);
        }
        const std::uint32_t anim = g.addObject("hkaSplineCompressedAnimation", 176);
        const std::uint32_t blocks = static_cast<std::uint32_t>(c.mBlocks.size());
        const float frameDur = c.mFrameDuration;
        g.put32(anim + 16, c.mType);
        g.putF32(anim + 20, (c.mNumFrames - 1) * frameDur);
        g.put32(anim + 24, tracks);
        g.put32(anim + 28, floatTracks);
        g.put32(anim + 56, c.mNumFrames);
        g.put32(anim + 60, blocks);
        g.put32(anim + 64, c.mMaxFramesPerBlock);
        g.put32(anim + 68, maskSize);
        g.putF32(anim + 72, (c.mMaxFramesPerBlock - 1) * frameDur);
        g.putF32(anim + 76, 1.0f / ((c.mMaxFramesPerBlock - 1) * frameDur));
        g.putF32(anim + 80, frameDur);
        auto words = [&](const std::vector<std::uint32_t>& w) {
            std::vector<std::uint8_t> b;
            for (std::uint32_t x : w)
                put(b, &x, 4);
            return g.addBytes(b);
        };
        g.array(anim + 88, words(offsets), blocks);
        g.array(anim + 104, words(floatOffsets), blocks);
        g.array(anim + 152, g.addBytes(data), static_cast<std::uint32_t>(data.size()));

        const std::uint32_t binding = g.addObject("hkaAnimationBinding", 112);
        g.pointer(binding + 16, g.addCString(c.mSkeletonName));
        g.pointer(binding + 24, anim);
        if (!c.mTrackToBone.empty())
        {
            std::vector<std::uint8_t> b;
            for (std::int16_t x : c.mTrackToBone)
                put(b, &x, 2);
            g.array(binding + 32, g.addBytes(b), static_cast<std::uint32_t>(c.mTrackToBone.size()));
        }
        if (!c.mFloatSlots.empty())
        {
            std::vector<std::uint8_t> b;
            for (std::int16_t x : c.mFloatSlots)
                put(b, &x, 2);
            g.array(binding + 48, g.addBytes(b), static_cast<std::uint32_t>(c.mFloatSlots.size()));
        }
        return anim;
    }

    struct SkeletonDesc
    {
        std::string mName = "Skel";
        std::vector<std::string> mBones;
        std::vector<std::int16_t> mParents;
        // x y z, qx qy qz qw, sx sy sz
        std::vector<std::array<float, 10>> mPose;
        std::vector<std::string> mFloatSlots; // written at +88 when not empty
    };

    inline std::uint32_t addSkeleton(PackGen& g, const SkeletonDesc& s)
    {
        const std::uint32_t n = static_cast<std::uint32_t>(s.mBones.size());
        const std::uint32_t obj = g.addObject("hkaSkeleton", 128);
        g.pointer(obj + 16, g.addCString(s.mName));
        std::vector<std::uint8_t> parents;
        for (std::int16_t p : s.mParents)
            put(parents, &p, 2);
        g.array(obj + 24, g.addBytes(parents), n);
        std::vector<std::uint8_t> bones(16 * n, 0);
        const std::uint32_t boneArray = g.addBytes(bones);
        for (std::uint32_t i = 0; i < n; ++i)
            g.pointer(boneArray + 16 * i, g.addCString(s.mBones[i]));
        g.array(obj + 40, boneArray, n);
        std::vector<std::uint8_t> pose;
        for (const auto& t : s.mPose)
        {
            for (int k = 0; k < 3; ++k)
                putFloat(pose, t[k]);
            putFloat(pose, 0);
            for (int k = 3; k < 7; ++k)
                putFloat(pose, t[k]);
            for (int k = 7; k < 10; ++k)
                putFloat(pose, t[k]);
            putFloat(pose, 0);
        }
        g.array(obj + 56, g.addBytes(pose), n);
        if (!s.mFloatSlots.empty())
        {
            const std::uint32_t count = static_cast<std::uint32_t>(s.mFloatSlots.size());
            const std::uint32_t slots = g.addBytes(std::vector<std::uint8_t>(8 * count, 0));
            for (std::uint32_t i = 0; i < count; ++i)
                g.pointer(slots + 8 * i, g.addCString(s.mFloatSlots[i]));
            g.array(obj + 88, slots, count);
        }
        return obj;
    }
}

#endif
