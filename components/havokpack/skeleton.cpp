#include "skeleton.hpp"

#include <cmath>

namespace HavokPack
{
    namespace
    {
        // hkaSkeleton member offsets in the 64-bit 2010.2 layout (provenance F13 to F16).
        constexpr std::uint32_t NameOffset = 16;
        constexpr std::uint32_t ParentsOffset = 24;
        constexpr std::uint32_t BonesOffset = 40;
        constexpr std::uint32_t ReferencePoseOffset = 56;
        constexpr std::uint32_t FloatSlotsOffset = 88; // provenance F42: an array of string pointers
        constexpr std::uint32_t BoneStride = 16; // hkaBone: name pointer, lock flag, padding
        constexpr std::uint32_t TransformStride = 48; // hkQsTransform: translation, rotation, scale as 4 floats each

        [[noreturn]] void bad(const Packfile& f, Ref at, const std::string& what)
        {
            (void)f;
            throw Error(what + " (hkaSkeleton at section " + std::to_string(at.mSection) + " offset "
                + std::to_string(at.mOffset) + ")");
        }
    }

    std::vector<Skeleton> readSkeletons(const Packfile& file, const SkeletonLimits& limits)
    {
        std::vector<Skeleton> out;
        for (const Object* object : file.objectsOfClass("hkaSkeleton"))
        {
            const Ref at{ object->mSection, object->mOffset };
            auto field = [&](std::uint32_t offset) { return Ref{ at.mSection, at.mOffset + offset }; };
            Skeleton s;
            if (const auto name = file.pointerAt(field(NameOffset)))
                s.mName = std::string(file.cstring(*name));

            const ArrayRef parents = file.arrayAt(field(ParentsOffset));
            const ArrayRef bones = file.arrayAt(field(BonesOffset));
            const ArrayRef pose = file.arrayAt(field(ReferencePoseOffset));
            if (parents.mSize > limits.mMaxBones)
                bad(file, at, "too many bones (" + std::to_string(parents.mSize) + ")");
            if (bones.mSize != parents.mSize || pose.mSize != parents.mSize)
                bad(file, at, "bone, parent and reference pose counts differ");

            s.mParents.resize(parents.mSize);
            s.mBoneNames.resize(parents.mSize);
            s.mReferencePose.resize(parents.mSize);
            for (std::uint32_t i = 0; i < parents.mSize; ++i)
            {
                const Ref p{ parents.mData->mSection, parents.mData->mOffset + 2 * i };
                s.mParents[i] = static_cast<std::int16_t>(file.u16(p));
                if (s.mParents[i] < -1 || s.mParents[i] >= static_cast<std::int32_t>(i))
                    bad(file, at,
                        "bone " + std::to_string(i) + " has parent " + std::to_string(s.mParents[i])
                            + ", which does not come before it");

                const Ref b{ bones.mData->mSection, bones.mData->mOffset + BoneStride * i };
                const auto name = file.pointerAt(b);
                if (!name)
                    bad(file, at, "bone " + std::to_string(i) + " has no name");
                s.mBoneNames[i] = std::string(file.cstring(*name));

                const Ref t{ pose.mData->mSection, pose.mData->mOffset + TransformStride * i };
                Transform& x = s.mReferencePose[i];
                for (std::uint32_t k = 0; k < 3; ++k)
                {
                    x.mTranslation[k] = file.f32({ t.mSection, t.mOffset + 4 * k });
                    x.mScale[k] = file.f32({ t.mSection, t.mOffset + 32 + 4 * k });
                }
                for (std::uint32_t k = 0; k < 4; ++k)
                    x.mRotation[k] = file.f32({ t.mSection, t.mOffset + 16 + 4 * k });
                for (float v : x.mTranslation)
                    if (!std::isfinite(v))
                        bad(file, at, "bone " + std::to_string(i) + " has a non-finite translation");
                float norm = 0;
                for (float v : x.mRotation)
                    norm += v * v;
                if (!std::isfinite(norm) || std::fabs(norm - 1.0f) > 0.01f)
                    bad(file, at, "bone " + std::to_string(i) + " has a rotation that is not a unit quaternion");
            }
            const ArrayRef slots = file.arrayAt(field(FloatSlotsOffset));
            if (slots.mSize > limits.mMaxFloatSlots)
                bad(file, at, "too many float slots (" + std::to_string(slots.mSize) + ")");
            for (std::uint32_t i = 0; i < slots.mSize; ++i)
            {
                const auto name = file.pointerAt({ slots.mData->mSection, slots.mData->mOffset + 8 * i });
                if (!name)
                    bad(file, at, "float slot " + std::to_string(i) + " has no name");
                s.mFloatSlots.emplace_back(file.cstring(*name));
            }
            out.push_back(std::move(s));
        }
        return out;
    }
}
