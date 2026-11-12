#pragma once

// The rest offset of the Audio2Face networks (task 0147 slice 3, toddhow 2026-11-12: "subtract"). On silence both
// networks still open the jaw by about 0.09 to 0.1 (task 0071); the generator subtracts a per-model, per-pose constant
// from every generated weight and clamps at 0.
//
// The constants are MEASURED, not chosen: measureRest generates on silence with the fixed seed and takes each pose's
// mean weight over all frames. restTable() holds the result of that measurement for each model variant (how it was
// measured is recorded beside the numbers in a2f_rest.cpp; a gated test re-measures with the real models and
// compares). Standard library only.

#include "a2f_generate.hpp"

#include <string>
#include <utility>
#include <vector>

namespace face_editor
{
    /// The length of the silence the table was measured on.
    inline constexpr double kRestSilenceSeconds = 10.0;

    /// Pose name (lower case ARKit) and its mean weight on silence.
    using RestTable = std::vector<std::pair<std::string, float>>;

    /// The measured constants of \a variant; empty when none were measured.
    const RestTable& restTable(A2FVariant variant);

    /// One offset per name in \a names (0 for a name the table lacks), in that order.
    std::vector<float> restOffsetsFor(A2FVariant variant, const std::vector<std::string>& names);

    /// Generates on \a seconds of silence with \a options (its mRestOffsets are ignored) and returns every pose's mean
    /// weight over all frames. False with \a error set when the run fails.
    bool measureRest(const GeneratorModel& model, A2FBackend& backend, double seconds, const GenerateOptions& options,
        RestTable& out, std::string& error);
}
