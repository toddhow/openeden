#pragma once

// One whole generation run for the face editor (task 0147 slice 2): check the models folder, resample the audio, load
// the model files and the network, run the generator. Everything the module's background task does, without any
// Studio type, so it can be called from the module, from a gated test and from a check. Standard library only (plus
// ONNX Runtime when configured, behind ort_backend).

#include "a2f_generate.hpp"
#include "models_folder.hpp"

#include <filesystem>
#include <string>
#include <vector>

namespace face_editor
{
    struct GenerateRequest
    {
        std::filesystem::path mModelsRoot; ///< <user data>/models/audio2face
        std::string mVariantId; ///< "v3.0" or "v2.3-mark"
        bool mAllowDirectMl = true;
        bool mUseEmotionModel = false; ///< Audio2Emotion is not wired in this slice: neutral emotion is used and said
        bool mAlreadyVerified = false; ///< the network's SHA-256 was checked earlier in this session
        std::vector<float> mAudio; ///< mono
        std::uint32_t mAudioRate = 0;
        GenerateOptions mOptions;
    };

    struct GenerateResult
    {
        GeneratedWeights mWeights;
        std::string mVariantId;
        std::string mProvider; ///< "DirectML" or "CPU"
        std::string mRuntimeVersion; ///< ONNX Runtime
        std::string mFallbackReason; ///< why DirectML was not used, empty when it was or was not wanted
        std::vector<std::string> mWarnings;
        VariantStatus mVerified; ///< the hash check, when it ran (mId empty otherwise)
        double mAudioSeconds = 0;
        double mComputeSeconds = 0; ///< the network and the solve, without loading
        double mTotalSeconds = 0;
    };

    /// \a progress as in A2FProgress (false cancels). On failure \a error is set ("cancelled" for a cancel).
    bool runGeneration(const GenerateRequest& request, const A2FProgress& progress, GenerateResult& result,
        std::string& error);

    /// Whether this build can run the networks at all (ONNX Runtime linked in and loadable); \a why says what is missing.
    bool generationAvailable(std::string& why);
}
