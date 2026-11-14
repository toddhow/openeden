#include "animation.hpp"

#include <algorithm>
#include <cmath>
#include <cstring>

namespace HavokPack
{
    namespace
    {
        // hkaSplineCompressedAnimation member offsets in the 64-bit 2010.2 layout (provenance F17 to F26).
        constexpr std::uint32_t TypeOffset = 16;
        constexpr std::uint32_t DurationOffset = 20;
        constexpr std::uint32_t TransformTracksOffset = 24;
        constexpr std::uint32_t FloatTracksOffset = 28;
        constexpr std::uint32_t NumFramesOffset = 56;
        constexpr std::uint32_t NumBlocksOffset = 60;
        constexpr std::uint32_t MaxFramesPerBlockOffset = 64;
        constexpr std::uint32_t MaskAndQuantizationSizeOffset = 68;
        constexpr std::uint32_t BlockDurationOffset = 72;
        constexpr std::uint32_t FrameDurationOffset = 80;
        constexpr std::uint32_t BlockOffsetsOffset = 88;
        constexpr std::uint32_t FloatBlockOffsetsOffset = 104;
        constexpr std::uint32_t DataOffset = 152;
        constexpr std::uint32_t SplineAnimationType = 5;
        // hkaAnimationBinding (F27 to F29)
        constexpr std::uint32_t BindingSkeletonNameOffset = 16;
        constexpr std::uint32_t BindingAnimationOffset = 24;
        constexpr std::uint32_t BindingTrackToBoneOffset = 32;
        constexpr std::uint32_t BindingFloatSlotsOffset = 48; // provenance F42
        // The only float track masks seen in the installed files (F38): one float, or a spline of 16-bit values between
        // a min and a max. A track with mask 0 stores nothing and is 0 (never seen; the generator of the older tests
        // writes it).
        constexpr std::uint8_t FloatMaskConstant = 0x03;
        constexpr std::uint8_t FloatMaskSpline = 0x12;
        constexpr std::size_t BlockAlignment = 16; // a block with float data ends on a 16 byte boundary (F40)

        // The quantisation byte every one of the 504,241 track blocks of the installed files has (F30): positions and
        // scales in 16 bits, rotations in 40 bits.
        constexpr std::uint8_t SupportedQuantization = 0x45;
        constexpr std::uint8_t MaxDegree = 3;
        // sqrt(2) / 2 spread over the 11 bits either side of the centre value 2047 (F33).
        constexpr float QuaternionStep = 0.70710678118654752f / 2047.0f;

        // Reads the block's data with bounds checks. Offsets in errors are into the animation's data array.
        class Cursor
        {
        public:
            Cursor(std::span<const std::uint8_t> data, std::size_t blockStart, std::size_t position)
                : mData(data)
                , mBlock(blockStart)
                , mPos(position)
            {
            }
            std::size_t position() const { return mPos; }
            [[noreturn]] void fail(const std::string& what) const
            {
                throw Error(what + " (spline data offset " + std::to_string(mPos) + ")");
            }
            void align4()
            {
                const std::size_t r = (mPos - mBlock) % 4;
                if (r != 0)
                    mPos += 4 - r;
            }
            const std::uint8_t* take(std::size_t n)
            {
                if (n > mData.size() || mPos > mData.size() - n)
                    fail("spline data runs past the end of the data array");
                const std::uint8_t* p = mData.data() + mPos;
                mPos += n;
                return p;
            }
            float f32()
            {
                float v;
                std::memcpy(&v, take(4), 4);
                return v;
            }
            // A float of the file that is NaN or infinite is replaced by `fallback` and counted (provenance F36: 59 of
            // the installed clips carry NaN ranges for a track).
            float finiteOr(float fallback)
            {
                const float v = f32();
                if (std::isfinite(v))
                    return v;
                ++mNonFinite;
                return fallback;
            }
            std::uint32_t nonFinite() const { return mNonFinite; }
            std::uint16_t u16()
            {
                std::uint16_t v;
                std::memcpy(&v, take(2), 2);
                return v;
            }

        private:
            std::span<const std::uint8_t> mData;
            std::size_t mBlock;
            std::size_t mPos;
            std::uint32_t mNonFinite = 0;
        };

        // The spline header: item count minus one (u16), degree (u8), then the knots.
        Spline readSplineHeader(
            Cursor& c, const ClipLimits& limits, std::uint32_t maxFramesPerBlock, std::uint32_t dims)
        {
            c.align4();
            const std::uint32_t n = c.u16();
            const std::uint8_t degree = *c.take(1);
            Spline s;
            s.mItems = n + 1;
            s.mDims = dims;
            s.mDegree = degree;
            if (degree < 1 || degree > MaxDegree)
                c.fail("unsupported spline degree " + std::to_string(degree));
            if (s.mItems > limits.mMaxSplineItems)
                c.fail("too many spline control points (" + std::to_string(s.mItems) + ")");
            if (s.mItems < static_cast<std::uint32_t>(degree) + 1)
                c.fail("fewer control points than the degree needs");
            const std::size_t knots = static_cast<std::size_t>(s.mItems) + degree + 1;
            const std::uint8_t* k = c.take(knots);
            s.mKnots.assign(k, k + knots);
            for (std::size_t i = 0; i < knots; ++i)
            {
                if (i > 0 && s.mKnots[i] < s.mKnots[i - 1])
                    c.fail("spline knots are not in order");
                if (s.mKnots[i] >= maxFramesPerBlock)
                    c.fail("spline knot beyond the block's frames");
            }
            s.mControl.assign(static_cast<std::size_t>(s.mItems) * dims, 0.0f);
            return s;
        }

        // Position or scale (F31): static axes are floats, animated axes have a min and max and 16-bit control points.
        VectorTrack readVector(
            Cursor& c, std::uint8_t mask, float defaultValue, const ClipLimits& limits, std::uint32_t maxFramesPerBlock)
        {
            VectorTrack t;
            t.mConstant = { defaultValue, defaultValue, defaultValue };
            const std::uint8_t staticAxes = mask & 7;
            const std::uint8_t splineAxes = (mask >> 4) & 7;
            if ((staticAxes & splineAxes) != 0)
                c.fail("an axis is marked both constant and animated");
            if (splineAxes != 0)
            {
                std::uint32_t dims = 0;
                for (int i = 0; i < 3; ++i)
                    dims += (splineAxes >> i) & 1;
                t.mSplineAxes = splineAxes;
                t.mSpline = readSplineHeader(c, limits, maxFramesPerBlock, dims);
                c.align4();
                std::array<std::array<float, 2>, 3> range{};
                // Per axis, in x y z order: a constant axis stores its float, an animated axis its min and max (F32;
                // the order was settled by the constant axes landing on the reference pose, not by block sizes).
                for (int i = 0; i < 3; ++i)
                {
                    if ((staticAxes >> i) & 1)
                        t.mConstant[i] = c.finiteOr(defaultValue);
                    else if ((splineAxes >> i) & 1)
                    {
                        const std::uint32_t before = c.nonFinite();
                        range[i][0] = c.finiteOr(defaultValue);
                        range[i][1] = c.finiteOr(defaultValue);
                        if (c.nonFinite() != before)
                            range[i] = { defaultValue, defaultValue };
                    }
                }
                for (std::uint32_t item = 0; item < t.mSpline.mItems; ++item)
                {
                    std::uint32_t d = 0;
                    for (int i = 0; i < 3; ++i)
                        if ((splineAxes >> i) & 1)
                        {
                            const float q = static_cast<float>(c.u16()) / 65535.0f;
                            t.mSpline.mControl[item * dims + d++] = range[i][0] + (range[i][1] - range[i][0]) * q;
                        }
                }
            }
            else if (staticAxes != 0)
            {
                c.align4();
                for (int i = 0; i < 3; ++i)
                    if ((staticAxes >> i) & 1)
                        t.mConstant[i] = c.finiteOr(defaultValue);
            }
            return t;
        }

        // One float track (F38 to F40): a constant, or a one dimensional spline between a min and a max.
        FloatTrackBlock readFloatTrack(
            Cursor& c, std::uint8_t mask, const ClipLimits& limits, std::uint32_t maxFramesPerBlock)
        {
            FloatTrackBlock f;
            if (mask == 0)
                return f;
            if (mask == FloatMaskConstant)
            {
                c.align4();
                f.mConstant = c.finiteOr(0.0f);
                return f;
            }
            if (mask != FloatMaskSpline)
                c.fail("unsupported float track mask " + std::to_string(mask));
            f.mAnimated = true;
            f.mSpline = readSplineHeader(c, limits, maxFramesPerBlock, 1);
            c.align4();
            const std::uint32_t before = c.nonFinite();
            float lo = c.finiteOr(0.0f);
            float hi = c.finiteOr(0.0f);
            if (c.nonFinite() != before)
                lo = hi = 0.0f;
            for (std::uint32_t item = 0; item < f.mSpline.mItems; ++item)
                f.mSpline.mControl[item] = lo + (hi - lo) * (static_cast<float>(c.u16()) / 65535.0f);
            return f;
        }

        RotationTrack readRotation(
            Cursor& c, std::uint8_t mask, const ClipLimits& limits, std::uint32_t maxFramesPerBlock)
        {
            RotationTrack r;
            if ((mask >> 4) != 0)
            {
                r.mKind = RotationTrack::Kind::Spline;
                r.mSpline = readSplineHeader(c, limits, maxFramesPerBlock, 4);
                for (std::uint32_t item = 0; item < r.mSpline.mItems; ++item)
                {
                    auto q = unpackQuaternion40(c.take(5));
                    float* previous = item > 0 ? &r.mSpline.mControl[(item - 1) * 4] : nullptr;
                    if (previous != nullptr
                        && previous[0] * q[0] + previous[1] * q[1] + previous[2] * q[2] + previous[3] * q[3] < 0.0f)
                        for (float& v : q)
                            v = -v;
                    std::copy(q.begin(), q.end(), &r.mSpline.mControl[item * 4]);
                }
            }
            else if ((mask & 15) != 0)
            {
                r.mKind = RotationTrack::Kind::Constant;
                c.align4();
                r.mConstant = unpackQuaternion40(c.take(5));
            }
            return r;
        }
    }

    std::array<float, 4> unpackQuaternion40(const std::uint8_t* five)
    {
        std::uint64_t v = 0;
        for (int i = 0; i < 5; ++i)
            v |= static_cast<std::uint64_t>(five[i]) << (8 * i);
        const std::uint32_t dropped = static_cast<std::uint32_t>((v >> 36) & 3);
        const bool negative = ((v >> 38) & 1) != 0;
        float component[3];
        float sum = 0;
        for (int i = 0; i < 3; ++i)
        {
            component[i] = (static_cast<float>((v >> (12 * i)) & 0xFFF) - 2047.0f) * QuaternionStep;
            sum += component[i] * component[i];
        }
        float w = std::sqrt(std::max(0.0f, 1.0f - sum));
        if (negative)
            w = -w;
        std::array<float, 4> q{};
        int next = 0;
        for (std::uint32_t i = 0; i < 4; ++i)
            q[i] = i == dropped ? w : component[next++];
        return q;
    }

    std::vector<Clip> readClips(const Packfile& file, const ClipLimits& limits)
    {
        std::vector<Clip> clips;
        for (const Object* object : file.objectsOfClass("hkaSplineCompressedAnimation"))
        {
            const std::uint32_t sec = object->mSection;
            auto at = [&](std::uint32_t offset) { return Ref{ sec, object->mOffset + offset }; };
            auto fail = [&](const std::string& what) {
                throw Error(what + " (hkaSplineCompressedAnimation at section " + std::to_string(sec) + " offset "
                    + std::to_string(object->mOffset) + ")");
            };
            if (file.u32(at(TypeOffset)) != SplineAnimationType)
                fail("animation type is not spline compressed");

            Clip clip;
            clip.mDuration = file.f32(at(DurationOffset));
            clip.mNumTransformTracks = file.u32(at(TransformTracksOffset));
            clip.mNumFloatTracks = file.u32(at(FloatTracksOffset));
            clip.mNumFrames = file.u32(at(NumFramesOffset));
            const std::uint32_t numBlocks = file.u32(at(NumBlocksOffset));
            clip.mMaxFramesPerBlock = file.u32(at(MaxFramesPerBlockOffset));
            const std::uint32_t maskSize = file.u32(at(MaskAndQuantizationSizeOffset));
            clip.mBlockDuration = file.f32(at(BlockDurationOffset));
            clip.mFrameDuration = file.f32(at(FrameDurationOffset));

            if (clip.mNumTransformTracks > limits.mMaxTracks || clip.mNumFloatTracks > limits.mMaxTracks)
                fail("too many tracks");
            if (numBlocks == 0 || numBlocks > limits.mMaxBlocks)
                fail("bad block count " + std::to_string(numBlocks));
            if (clip.mMaxFramesPerBlock < 2 || clip.mMaxFramesPerBlock > limits.mMaxFramesPerBlock)
                fail("bad frames per block " + std::to_string(clip.mMaxFramesPerBlock));
            if (clip.mNumFrames == 0 || clip.mNumFrames > limits.mMaxFrames)
                fail("bad frame count " + std::to_string(clip.mNumFrames));
            if (!(clip.mFrameDuration > 0.0f) || !std::isfinite(clip.mFrameDuration) || !std::isfinite(clip.mDuration)
                || clip.mDuration < 0.0f)
                fail("bad durations");
            if (maskSize < 4ull * clip.mNumTransformTracks)
                fail("mask and quantisation size is smaller than the track masks");
            if (static_cast<std::uint64_t>(numBlocks) * (clip.mMaxFramesPerBlock - 1) + 1 < clip.mNumFrames)
                fail("the blocks cannot hold all the frames");

            const ArrayRef offsets = file.arrayAt(at(BlockOffsetsOffset));
            const ArrayRef floatOffsets = file.arrayAt(at(FloatBlockOffsetsOffset));
            const ArrayRef data = file.arrayAt(at(DataOffset));
            if (offsets.mSize != numBlocks || floatOffsets.mSize != numBlocks)
                fail("block offset counts differ from the block count");
            std::span<const std::uint8_t> bytes;
            if (data.mData)
                bytes = file.bytes(*data.mData, data.mSize);

            if (clip.mNumFloatTracks != 0 && maskSize < 4ull * clip.mNumTransformTracks + clip.mNumFloatTracks)
                fail("mask and quantisation size is smaller than the track and float track masks");
            clip.mBlocks.resize(numBlocks);
            if (clip.mNumFloatTracks != 0)
                clip.mFloatBlocks.resize(numBlocks);
            for (std::uint32_t b = 0; b < numBlocks; ++b)
            {
                const std::uint32_t start = file.u32({ offsets.mData->mSection, offsets.mData->mOffset + 4 * b });
                const std::uint32_t floatsAt
                    = file.u32({ floatOffsets.mData->mSection, floatOffsets.mData->mOffset + 4 * b });
                if (start > bytes.size() || bytes.size() - start < maskSize)
                    fail("block " + std::to_string(b) + " starts outside the data");
                if (b > 0 && start < file.u32({ offsets.mData->mSection, offsets.mData->mOffset + 4 * (b - 1) }))
                    fail("block offsets are not in order");
                Cursor c(bytes, start, static_cast<std::size_t>(start) + maskSize);
                auto& tracks = clip.mBlocks[b];
                tracks.resize(clip.mNumTransformTracks);
                for (std::uint32_t t = 0; t < clip.mNumTransformTracks; ++t)
                {
                    const std::uint8_t* m = bytes.data() + start + 4 * static_cast<std::size_t>(t);
                    if (m[0] != SupportedQuantization)
                        throw Error("unsupported quantisation byte " + std::to_string(m[0]) + " (spline data offset "
                            + std::to_string(start + 4 * t) + ")");
                    tracks[t].mPosition = readVector(c, m[1], 0.0f, limits, clip.mMaxFramesPerBlock);
                    tracks[t].mRotation = readRotation(c, m[2], limits, clip.mMaxFramesPerBlock);
                    tracks[t].mScale = readVector(c, m[3], 1.0f, limits, clip.mMaxFramesPerBlock);
                }
                c.align4();
                clip.mNonFiniteValues += c.nonFinite();
                if (c.position() != static_cast<std::size_t>(start) + floatsAt)
                    throw Error("track data of block " + std::to_string(b) + " ends at offset "
                        + std::to_string(c.position() - start) + " but the file says " + std::to_string(floatsAt)
                        + " (an unknown layout; spline data offset " + std::to_string(c.position()) + ")");
                if (clip.mNumFloatTracks != 0)
                {
                    auto& floats = clip.mFloatBlocks[b];
                    const std::uint32_t transformNonFinite = c.nonFinite();
                    floats.resize(clip.mNumFloatTracks);
                    bool stored = false;
                    for (std::uint32_t i = 0; i < clip.mNumFloatTracks; ++i)
                    {
                        const std::uint8_t mask
                            = bytes[start + 4 * static_cast<std::size_t>(clip.mNumTransformTracks) + i];
                        floats[i] = readFloatTrack(c, mask, limits, clip.mMaxFramesPerBlock);
                        stored = stored || mask != 0;
                    }
                    clip.mNonFiniteValues += c.nonFinite() - transformNonFinite;
                    // With float data present the block ends on a 16 byte boundary, and that is where the next block
                    // (or the end of the data) starts: the size invariant for the float tracks.
                    if (stored)
                    {
                        const std::size_t used = c.position() - start;
                        const std::size_t padded = (used + BlockAlignment - 1) / BlockAlignment * BlockAlignment;
                        const std::size_t end = b + 1 < numBlocks
                            ? file.u32({ offsets.mData->mSection, offsets.mData->mOffset + 4 * (b + 1) })
                            : bytes.size();
                        if (end < start || end - start != padded)
                            throw Error("float track data of block " + std::to_string(b) + " ends at offset "
                                + std::to_string(used) + " but the block is " + std::to_string(end - start)
                                + " bytes, not that rounded up to 16 (an unknown layout; spline data offset "
                                + std::to_string(c.position()) + ")");
                    }
                }
            }
            clips.push_back(std::move(clip));
        }

        // Bindings: which animation, which skeleton, which bone each track drives.
        for (const Object* object : file.objectsOfClass("hkaAnimationBinding"))
        {
            const Ref at{ object->mSection, object->mOffset };
            const auto target = file.pointerAt({ at.mSection, at.mOffset + BindingAnimationOffset });
            if (!target)
                continue;
            const auto animations = file.objectsOfClass("hkaSplineCompressedAnimation");
            for (std::size_t i = 0; i < animations.size() && i < clips.size(); ++i)
            {
                if (animations[i]->mSection != target->mSection || animations[i]->mOffset != target->mOffset)
                    continue;
                if (const auto name = file.pointerAt({ at.mSection, at.mOffset + BindingSkeletonNameOffset }))
                    clips[i].mSkeletonName = std::string(file.cstring(*name));
                const ArrayRef slots = file.arrayAt({ at.mSection, at.mOffset + BindingFloatSlotsOffset });
                if (slots.mSize != 0)
                {
                    if (slots.mSize != clips[i].mNumFloatTracks)
                        throw Error("binding maps " + std::to_string(slots.mSize)
                            + " float tracks but the animation has " + std::to_string(clips[i].mNumFloatTracks)
                            + " (hkaAnimationBinding at offset " + std::to_string(at.mOffset) + ")");
                    clips[i].mFloatTrackToSlot.resize(slots.mSize);
                    for (std::uint32_t k = 0; k < slots.mSize; ++k)
                    {
                        clips[i].mFloatTrackToSlot[k] = static_cast<std::int16_t>(
                            file.u16({ slots.mData->mSection, slots.mData->mOffset + 2 * k }));
                        if (clips[i].mFloatTrackToSlot[k] < 0)
                            throw Error("binding names a negative float slot (hkaAnimationBinding at offset "
                                + std::to_string(at.mOffset) + ")");
                    }
                }
                const ArrayRef map = file.arrayAt({ at.mSection, at.mOffset + BindingTrackToBoneOffset });
                if (map.mSize != 0)
                {
                    if (map.mSize != clips[i].mNumTransformTracks)
                        throw Error("binding maps " + std::to_string(map.mSize) + " tracks but the animation has "
                            + std::to_string(clips[i].mNumTransformTracks) + " (hkaAnimationBinding at offset "
                            + std::to_string(at.mOffset) + ")");
                    clips[i].mTrackToBone.resize(map.mSize);
                    for (std::uint32_t k = 0; k < map.mSize; ++k)
                    {
                        clips[i].mTrackToBone[k]
                            = static_cast<std::int16_t>(file.u16({ map.mData->mSection, map.mData->mOffset + 2 * k }));
                        if (clips[i].mTrackToBone[k] < 0)
                            throw Error("binding names a negative bone index (hkaAnimationBinding at offset "
                                + std::to_string(at.mOffset) + ")");
                    }
                }
            }
        }
        return clips;
    }

    namespace
    {
        // Cox-de Boor on the knots as frame numbers. `dims` <= 4, degree <= MaxDegree, so the arrays are fixed.
        void evaluate(const Spline& s, float u, float* out)
        {
            const std::uint32_t degree = s.mDegree;
            const std::uint32_t items = s.mItems;
            u = std::clamp(u, static_cast<float>(s.mKnots.front()), static_cast<float>(s.mKnots.back()));
            std::uint32_t k = degree;
            while (k < items - 1 && u >= static_cast<float>(s.mKnots[k + 1]))
                ++k;
            float d[MaxDegree + 1][4];
            for (std::uint32_t j = 0; j <= degree; ++j)
                for (std::uint32_t i = 0; i < s.mDims; ++i)
                    d[j][i] = s.mControl[(j + k - degree) * s.mDims + i];
            for (std::uint32_t r = 1; r <= degree; ++r)
                for (std::uint32_t j = degree; j >= r; --j)
                {
                    const float lo = static_cast<float>(s.mKnots[j + k - degree]);
                    const float hi = static_cast<float>(s.mKnots[j + 1 + k - r]);
                    const float alpha = hi == lo ? 0.0f : (u - lo) / (hi - lo);
                    for (std::uint32_t i = 0; i < s.mDims; ++i)
                        d[j][i] = (1 - alpha) * d[j - 1][i] + alpha * d[j][i];
                }
            for (std::uint32_t i = 0; i < s.mDims; ++i)
                out[i] = d[degree][i];
        }

        void evaluateVector(const VectorTrack& t, float u, std::array<float, 3>& out)
        {
            out = t.mConstant;
            if (t.mSplineAxes == 0)
                return;
            float v[4];
            evaluate(t.mSpline, u, v);
            std::uint32_t d = 0;
            for (int i = 0; i < 3; ++i)
                if ((t.mSplineAxes >> i) & 1)
                    out[i] = v[d++];
        }
    }

    void sampleFloats(const Clip& clip, float time, std::span<float> out)
    {
        if (clip.mFloatBlocks.empty())
            return;
        const float frame = std::clamp(time, 0.0f, clip.mDuration) / clip.mFrameDuration;
        const float blockFrames = static_cast<float>(clip.mMaxFramesPerBlock - 1);
        const std::uint32_t block = std::min(
            static_cast<std::uint32_t>(frame / blockFrames), static_cast<std::uint32_t>(clip.mFloatBlocks.size() - 1));
        const float u = frame - static_cast<float>(block) * blockFrames;
        const auto& tracks = clip.mFloatBlocks[block];
        const std::size_t n = std::min(out.size(), tracks.size());
        for (std::size_t i = 0; i < n; ++i)
        {
            if (!tracks[i].mAnimated)
            {
                out[i] = tracks[i].mConstant;
                continue;
            }
            float v[4];
            evaluate(tracks[i].mSpline, u, v);
            out[i] = v[0];
        }
    }

    void sample(const Clip& clip, float time, std::span<Transform> out)
    {
        const float clamped = std::clamp(time, 0.0f, clip.mDuration);
        const float frame = clamped / clip.mFrameDuration;
        const float blockFrames = static_cast<float>(clip.mMaxFramesPerBlock - 1);
        const std::uint32_t block = std::min(
            static_cast<std::uint32_t>(frame / blockFrames), static_cast<std::uint32_t>(clip.mBlocks.size() - 1));
        const float u = frame - static_cast<float>(block) * blockFrames;
        const auto& tracks = clip.mBlocks[block];
        const std::size_t n = std::min(out.size(), tracks.size());
        for (std::size_t i = 0; i < n; ++i)
        {
            const TrackBlock& t = tracks[i];
            Transform& x = out[i];
            evaluateVector(t.mPosition, u, x.mTranslation);
            evaluateVector(t.mScale, u, x.mScale);
            switch (t.mRotation.mKind)
            {
                case RotationTrack::Kind::Identity:
                    x.mRotation = { 0, 0, 0, 1 };
                    break;
                case RotationTrack::Kind::Constant:
                    x.mRotation = t.mRotation.mConstant;
                    break;
                case RotationTrack::Kind::Spline:
                {
                    float v[4];
                    evaluate(t.mRotation.mSpline, u, v);
                    const float norm = std::sqrt(v[0] * v[0] + v[1] * v[1] + v[2] * v[2] + v[3] * v[3]);
                    const float inv = norm > 0.0f ? 1.0f / norm : 1.0f;
                    x.mRotation = { v[0] * inv, v[1] * inv, v[2] * inv, norm > 0.0f ? v[3] * inv : 1.0f };
                    break;
                }
            }
        }
    }
}
