#include "generate_run.hpp"

#ifdef OPENEDEN_FACE_GENERATOR
#include "ort_backend.hpp"
#endif

#include <chrono>

namespace face_editor
{
    namespace fs = std::filesystem;

    bool generationAvailable(std::string& why)
    {
#ifdef OPENEDEN_FACE_GENERATOR
        return ortRuntimeAvailable(why);
#else
        why = "Generating curves needs the ONNX runtime, which is not part of this build (OPENEDEN_BUILD_FACE_GENERATOR).";
        return false;
#endif
    }

#ifndef OPENEDEN_FACE_GENERATOR
    bool runGeneration(const GenerateRequest&, const A2FProgress&, GenerateResult&, std::string& error)
    {
        generationAvailable(error);
        return false;
    }
#else
    namespace
    {
        using Clock = std::chrono::steady_clock;

        double since(Clock::time_point t)
        {
            return std::chrono::duration<double>(Clock::now() - t).count();
        }
    }

    bool runGeneration(const GenerateRequest& request, const A2FProgress& progress, GenerateResult& result,
        std::string& error)
    {
        const auto started = Clock::now();
        result = GenerateResult{};
        result.mVariantId = request.mVariantId;
        auto step = [&](double f, std::string_view text) {
            if (progress && !progress(f, text))
            {
                error = "cancelled";
                return false;
            }
            return true;
        };

        const VariantSpec* spec = nullptr;
        for (const VariantSpec& s : knownVariants())
            if (s.mId == request.mVariantId)
                spec = &s;
        A2FVariant variant;
        if (request.mVariantId == "v3.0")
            variant = A2FVariant::V30;
        else if (request.mVariantId == "v2.3-mark")
            variant = A2FVariant::V23;
        else
        {
            error = "unknown model variant " + request.mVariantId;
            return false;
        }
        if (spec == nullptr || request.mAudio.empty() || request.mAudioRate == 0)
        {
            error = request.mAudio.empty() ? "there is no audio to generate curves for" : "unknown model variant";
            return false;
        }
        result.mAudioSeconds = static_cast<double>(request.mAudio.size()) / request.mAudioRate;

        if (!ortRuntimeAvailable(error))
            return false;
        result.mRuntimeVersion = ortRuntimeVersion();

        // The models folder check gates the run: a missing file, another size or a different hash is refused before
        // anything is loaded.
        const fs::path folder = request.mModelsRoot / request.mVariantId;
        for (const VariantStatus& status : scanModels(request.mModelsRoot))
            if (status.mId == request.mVariantId && status.mState != VariantState::Present
                && status.mState != VariantState::Verified)
            {
                error = "the model " + request.mVariantId + " cannot be used: " + stateText(status.mState) + " "
                    + status.mDetail;
                return false;
            }
        if (!request.mAlreadyVerified)
        {
            if (!step(0.0, "checking the model files"))
                return false;
            result.mVerified = verifyVariant(request.mModelsRoot, *spec,
                [&](double f) { return !progress || progress(0.1 * f, "checking the model files"); });
            if (result.mVerified.mState != VariantState::Verified)
            {
                error = result.mVerified.mState == VariantState::Present
                    ? "cancelled"
                    : "the model " + request.mVariantId + " cannot be used: " + stateText(result.mVerified.mState) + " "
                        + result.mVerified.mDetail;
                return false;
            }
        }
        if (request.mUseEmotionModel)
            result.mWarnings.push_back(
                "Audio2Emotion is not wired in this version of the generator: the neutral emotion was used.");

        if (!step(0.1, "loading the model files"))
            return false;
        GeneratorModel model;
        if (!loadGeneratorModel(folder, variant, model, error))
            return false;

        if (!step(0.15, "loading the network"))
            return false;
        std::string fallback;
        auto backend = OrtBackend::create(folder / "network.onnx", variant, request.mAllowDirectMl, fallback, error);
        if (!backend)
            return false;
        result.mProvider = OrtBackend::providerName(backend->provider());
        result.mFallbackReason = fallback;

        if (!step(0.2, "resampling the audio"))
            return false;
        std::vector<float> audio16 = resampleMono(request.mAudio, request.mAudioRate,
            static_cast<std::uint32_t>(A2FDims::sSampleRate), error);
        if (audio16.empty())
        {
            if (error.empty())
                error = "the audio is empty";
            return false;
        }

        const auto computeStart = Clock::now();
        const bool ok = generateWeights(model, *backend, audio16, request.mOptions,
            [&](double f, std::string_view text) { return !progress || progress(0.2 + 0.8 * f, text); }, result.mWeights,
            error);
        result.mComputeSeconds = since(computeStart);
        result.mTotalSeconds = since(started);
        return ok;
    }
#endif
}
