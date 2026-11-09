#ifndef OPENEDEN_COMPONENTS_FACE_FACEIMPORT_H
#define OPENEDEN_COMPONENTS_FACE_FACEIMPORT_H

// OpenEden facial speech animation, task 0071 stage c: importers that turn a generator's plain output into
// opas.facecurves. Nothing from NVIDIA is read here (the Audio2Face importer waits for the author's SDK output
// format, which is not documented in this repository).
//
//  * importCsv: one header row of channel names, one row per frame, values 0..1 (or -1..1 for the channels named in
//    `signedChannels`), separated by commas; blank lines and lines starting with '#' are skipped.
//  * importRhubarbTsv: Rhubarb Lip Sync's TSV output, "seconds<TAB>shape" per line (shapes A..H and X), each shape
//    holds until the next line; becomes nine weight channels viseme_a..viseme_h and viseme_x sampled at frameRate.
//    The frame count comes from the audio duration in the meta.

#include "facecurves.hpp"

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace Face
{
    struct ImportMeta
    {
        std::string mAudioName;
        double mAudioDurationSeconds = 0.0;
        std::string mAudioSha256;
        std::string mGeneratorName;
        std::string mGeneratorVersion;
        std::int64_t mSeed = 0;
        std::string mEmotion;
        std::uint32_t mFrameRate = 30;
    };

    std::optional<Curves> importCsv(std::string_view text, std::string_view fileName, const ImportMeta& meta,
        const std::vector<std::string>& signedChannels, LoadError& error);

    std::optional<Curves> importRhubarbTsv(
        std::string_view text, std::string_view fileName, const ImportMeta& meta, LoadError& error);
}

#endif
