#include "a2f_rest.hpp"

#include "a2f_rest_table.inc"

#include <cmath>

namespace face_editor
{
    namespace
    {
        constexpr double kMaxMeasureSeconds = 600.0;
    }

    const RestTable& restTable(A2FVariant variant)
    {
        static const RestTable sV30 = [] {
            RestTable t;
            for (const auto& entry : sRestV30)
                t.emplace_back(entry.mName, entry.mValue);
            return t;
        }();
        static const RestTable sV23 = [] {
            RestTable t;
            for (const auto& entry : sRestV23)
                t.emplace_back(entry.mName, entry.mValue);
            return t;
        }();
        return variant == A2FVariant::V30 ? sV30 : sV23;
    }

    std::vector<float> restOffsetsFor(A2FVariant variant, const std::vector<std::string>& names)
    {
        const RestTable& table = restTable(variant);
        std::vector<float> offsets(names.size(), 0.f);
        for (std::size_t i = 0; i < names.size(); ++i)
            for (const auto& entry : table)
                if (entry.first == names[i])
                {
                    offsets[i] = entry.second;
                    break;
                }
        return offsets;
    }

    bool measureRest(const GeneratorModel& model, A2FBackend& backend, double seconds, const GenerateOptions& options,
        RestTable& out, std::string& error)
    {
        out.clear();
        if (!(seconds > 0.0) || !std::isfinite(seconds) || seconds > kMaxMeasureSeconds)
        {
            error = "the silence must be between 0 and 600 seconds long";
            return false;
        }
        GenerateOptions plain = options;
        plain.mRestOffsets.clear();
        const auto samples = static_cast<std::size_t>(std::llround(seconds * static_cast<double>(A2FDims::sSampleRate)));
        const std::vector<float> silence(std::max<std::size_t>(samples, 1), 0.f);
        GeneratedWeights weights;
        if (!generateWeights(model, backend, silence, plain, {}, weights, error))
            return false;
        const std::size_t frames = weights.frameCount();
        if (frames == 0)
        {
            error = "the silence produced no frames";
            return false;
        }
        for (std::size_t pose = 0; pose < weights.mNames.size(); ++pose)
        {
            double sum = 0;
            for (std::size_t f = 0; f < frames; ++f)
                sum += weights.at(f, pose);
            out.emplace_back(weights.mNames[pose], static_cast<float>(sum / static_cast<double>(frames)));
        }
        return true;
    }
}
