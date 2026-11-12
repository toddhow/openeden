#pragma once

// The face editor's key model (task 0147): opas.facecurves stores one integer per frame, the canvas edits sparse keys.
// sparsify() turns frames into keys that resample() turns back into EXACTLY the same integers, so a channel nobody edits
// is never rewritten and an edited one is resampled from its keys. Pure; standard library only.

#include <openeden/editor_module/types.hpp>

#include <cstdint>
#include <vector>

namespace face_editor
{
    /// Quantised units per 1.0 (components/face: weight = v / 255).
    inline constexpr double kQuantScale = 255.0;
    /// The longest run of frames one segment may span (bounds the exactness check to O(frames * this)).
    inline constexpr std::size_t kMaxSegmentFrames = 256;

    /// Frames (quantised integers) to keys: time = frame / frameRate, value = v / 255. The first and last frame are
    /// always keys; interior frames are dropped when linear interpolation between the neighbouring kept keys, rounded,
    /// gives the original integer for every frame in between.
    std::vector<openeden::editor::CurveKey> sparsify(const std::vector<std::int16_t>& frames, std::uint32_t frameRate);

    /// Keys (sorted by time) to frameCount integers clamped to [lo, hi]: linear between keys, held before the first and
    /// after the last, 0 with no keys. Non-finite keys are ignored.
    std::vector<std::int16_t> resample(const std::vector<openeden::editor::CurveKey>& keys, std::uint32_t frameRate,
        std::uint32_t frameCount, int lo, int hi);

    /// Inserts a key keeping time order (an equal time goes after the existing ones). Returns its index.
    std::size_t insertKey(std::vector<openeden::editor::CurveKey>& keys, const openeden::editor::CurveKey& key);
}
