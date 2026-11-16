#ifndef OPENEDEN_TEST_SUITE_NIFSCENE_SEQBUILDER_H
#define OPENEDEN_TEST_SUITE_NIFSCENE_SEQBUILDER_H

// Task 0232: generated NiControllerSequence blocks (20.2.0.7, stream 100) for the sequence tests. Layouts follow
// Nif::NiControllerSequence, NiSequence, ControlledBlock, TypedNiInterpolator and NiKeyframeData in components/nif.

#include <array>
#include <cstdint>
#include <string>
#include <vector>

#include "ssebuilder.hpp"

namespace NifScene::Testing
{
    struct TransKey
    {
        float mTime;
        std::array<float, 3> mValue;
    };
    struct RotKey
    {
        float mTime;
        std::array<float, 4> mXyzw;
    };

    // NiTransformData with linear keys (interpolation type 1); a channel with no keys writes a zero count.
    inline Block transformData(const std::vector<RotKey>& rotations, const std::vector<TransKey>& translations)
    {
        Block block;
        block.mType = "NiTransformData";
        block.mData.put(static_cast<std::uint32_t>(rotations.size()));
        if (!rotations.empty())
        {
            block.mData.put(static_cast<std::uint32_t>(1));
            for (const RotKey& key : rotations)
                block.mData.put(key.mTime).floats({ key.mXyzw[3], key.mXyzw[0], key.mXyzw[1], key.mXyzw[2] });
        }
        block.mData.put(static_cast<std::uint32_t>(translations.size()));
        if (!translations.empty())
        {
            block.mData.put(static_cast<std::uint32_t>(1));
            for (const TransKey& key : translations)
                block.mData.put(key.mTime).floats({ key.mValue[0], key.mValue[1], key.mValue[2] });
        }
        block.mData.put(static_cast<std::uint32_t>(0)); // scale keys
        return block;
    }

    // NiTransformData whose rotation is three angle tracks (interpolation type 4, XYZ); each axis is a list of
    // (time, radians) linear keys, empty for an axis with none.
    inline Block transformDataEuler(
        const std::array<std::vector<std::array<float, 2>>, 3>& axes, const std::vector<TransKey>& translations)
    {
        Block block;
        block.mType = "NiTransformData";
        block.mData.put(static_cast<std::uint32_t>(1)); // rotation key count (the keys themselves are not stored)
        block.mData.put(static_cast<std::uint32_t>(4)); // XYZ
        for (const auto& axis : axes)
        {
            block.mData.put(static_cast<std::uint32_t>(axis.size()));
            if (!axis.empty())
            {
                block.mData.put(static_cast<std::uint32_t>(1));
                for (const auto& key : axis)
                    block.mData.put(key[0]).put(key[1]);
            }
        }
        block.mData.put(static_cast<std::uint32_t>(translations.size()));
        if (!translations.empty())
        {
            block.mData.put(static_cast<std::uint32_t>(1));
            for (const TransKey& key : translations)
                block.mData.put(key.mTime).floats({ key.mValue[0], key.mValue[1], key.mValue[2] });
        }
        block.mData.put(static_cast<std::uint32_t>(0)); // scale keys
        return block;
    }

    // NiTransformInterpolator: the default value (translation, rotation w x y z, scale) and the data reference.
    inline Block transformInterpolator(std::int32_t data)
    {
        Block block;
        block.mType = "NiTransformInterpolator";
        block.mData.floats({ 0.f, 0.f, 0.f, 1.f, 0.f, 0.f, 0.f, 1.f });
        block.mData.put(data);
        return block;
    }

    struct ControlledSpec
    {
        std::string mNode;
        std::int32_t mInterpolator = -1;
    };

    inline Block controllerSequence(GamebryoFile& file, const std::string& name, float start, float stop,
        float frequency, const std::vector<ControlledSpec>& blocks)
    {
        Block block;
        block.mType = "NiControllerSequence";
        ByteWriter& out = block.mData;
        out.put(file.string(name));
        out.put(static_cast<std::uint32_t>(blocks.size()));
        out.put(static_cast<std::uint32_t>(blocks.size())); // array grow by
        for (const ControlledSpec& spec : blocks)
        {
            out.put(spec.mInterpolator);
            out.put(static_cast<std::int32_t>(-1)); // controller
            out.put(static_cast<std::uint8_t>(0)); // priority
            out.put(file.string(spec.mNode));
            out.put(file.string("")); // property type
            out.put(file.string("NiTransformController"));
            out.put(file.string("")); // controller id
            out.put(file.string("")); // interpolator id
        }
        out.put(1.f); // weight
        out.put(static_cast<std::int32_t>(-1)); // text keys
        out.put(static_cast<std::uint32_t>(2)); // extrapolation: clamp
        out.put(frequency);
        out.put(start);
        out.put(stop);
        out.put(static_cast<std::int32_t>(-1)); // manager
        out.put(file.string("")); // accum root
        out.put(static_cast<std::uint16_t>(0)); // anim notes
        return block;
    }
}

#endif
