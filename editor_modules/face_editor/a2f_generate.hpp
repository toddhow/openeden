#pragma once

// Audio2Face generation for the face editor (task 0147 slice 2): from a voice line's audio to blendshape weights,
// through a network runner that is an interface here (the ONNX Runtime one is ort_backend.*; tests use a stub).
// Everything else is pure and deterministic: resampling, windowing, the fixed noise, the solve and the frame-rate
// conversion. The window layouts are what the feasibility gate found by running the networks (task 0071 "Stage a
// results"); they are INFERRED (RECALLED-VERIFY), not taken from NVIDIA's SDK:
//  - v3.0 (diffusion, 60 output frames per 16,000-sample window), V3Layout::Gate (the DEFAULT, the gate's reading, as
//    toddhow's brief says): consecutive 1 s windows (hop 16,000), frames 15..44 of every window kept as 30 frames per
//    second. V3Layout::Overlapped is an alternative this task's author suspects is the right one (a 60 fps reading:
//    hop 8,000, windows centred on the hop, frames 15..44 at 60 frames per second): on the gate's voice line the
//    mouth-to-audio lag was -0.25 s with Gate and -0.05 s with Overlapped, but the correlation metric is weak and Gate
//    reproduces the gate's recorded weights exactly (jawOpen max 0.547, mouthClose 0.861, mouthPucker 0.628; Overlapped
//    0.603, 0.956, 0.794). Neither is confirmed: task 0147 slice 2, decision 1.
//  - v2.3 (regression, 8,320-sample windows, 301 outputs): one window per 1/30 s centred on the frame time; the first
//    272 outputs are shape coefficients of a linear skin model.
// Standard library only.

#include "a2f_solve.hpp"

#include <array>
#include <cstdint>
#include <functional>
#include <string>
#include <string_view>
#include <vector>

namespace face_editor
{
    /// How the v3.0 windows are laid over the audio (see the comment at the top).
    enum class V3Layout
    {
        Gate,
        Overlapped
    };

    enum class A2FVariant
    {
        V30, ///< "v3.0", Mark identity
        V23 ///< "v2.3-mark"
    };

    namespace A2FDims
    {
        inline constexpr std::size_t sSampleRate = 16000;
        // v3.0
        inline constexpr std::size_t sV3Window = 16000;
        inline constexpr std::size_t sV3HopGate = 16000;
        inline constexpr std::size_t sV3HopOverlapped = 8000;
        inline constexpr std::size_t sV3Frames = 60;
        inline constexpr std::size_t sV3KeepFrom = 15;
        inline constexpr std::size_t sV3Keep = 30;
        inline constexpr std::size_t sV3Outputs = 88831;
        inline constexpr std::size_t sV3SkinFloats = 72006;
        inline constexpr std::size_t sV3NoiseFloats = 3 * 60 * 88831;
        inline constexpr std::size_t sV3LatentFloats = 2 * 2 * 1 * 256;
        inline constexpr std::size_t sV3EmotionFloats = 30 * 10;
        inline constexpr std::size_t sV3RateGate = 30;
        inline constexpr std::size_t sV3RateOverlapped = 60;
        // v2.3
        inline constexpr std::size_t sV23Window = 8320;
        inline constexpr std::size_t sV23Batch = 30;
        inline constexpr std::size_t sV23Outputs = 301;
        inline constexpr std::size_t sV23Coefficients = 272;
        inline constexpr std::size_t sV23EmotionFloats = 26;
        inline constexpr std::size_t sV23OutputRate = 30;
    }

    /// What a run needs from the models folder besides the network: the blendshape poses and, for v2.3, the skin model.
    struct GeneratorModel
    {
        A2FVariant mVariant = A2FVariant::V30;
        BlendshapeSet mPoses;
        std::array<float, 3> mIdentity{ 0.f, 0.f, 1.f }; ///< v3.0 one-hot over Claire, James, Mark
        std::vector<float> mV23MeanMinusNeutral; ///< masked, rowLength floats
        std::vector<float> mV23Basis; ///< 272 rows of rowLength floats, masked
        std::vector<float> mV23Emotion; ///< 26 floats: 10 explicit (zero) + 16 implicit (neutral row)
    };

    /// Reads the files of one variant folder (the names models_folder.cpp lists). The network itself is not read here.
    bool loadGeneratorModel(
        const std::filesystem::path& variantFolder, A2FVariant variant, GeneratorModel& out, std::string& error);

    /// The network. One implementation wraps ONNX Runtime; tests supply their own. Methods are called from one thread.
    class A2FBackend
    {
    public:
        virtual ~A2FBackend() = default;
        /// v3.0: one 1 s window. \a identity 3, \a emotion 300, \a latentsIn 1024, \a noise sV3NoiseFloats ->
        /// \a prediction sV3Frames * sV3Outputs and \a latentsOut 1024.
        virtual bool runV3(const float* window, const float* identity, const float* emotion, const float* latentsIn,
            const float* noise, float* prediction, float* latentsOut, std::string& error)
            = 0;
        /// v2.3: \a count windows of sV23Window samples and one emotion vector of 26 floats shared by all of them ->
        /// count * sV23Outputs.
        virtual bool runV23(const float* windows, std::size_t count, const float* emotion, float* results,
            std::string& error)
            = 0;
    };

    // ----- pure pieces -------------------------------------------------------------------------------------------

    /// Windowed-sinc resampling of mono audio (cutoff at the lower of the two Nyquist rates). Empty result with \a error
    /// set for a rate of 0 or beyond 384,000.
    std::vector<float> resampleMono(const std::vector<float>& in, std::uint32_t rateIn, std::uint32_t rateOut,
        std::string& error);

    /// Standard normal noise from a splitmix64 stream and the Box-Muller transform: the same seed gives the same bytes
    /// on every platform and library.
    void fillNormalNoise(float* dst, std::size_t count, std::uint64_t seed);

    /// Sample index of every v3.0 window's first sample (negative = starts in the zero padding): Gate ceil(n / 16000)
    /// windows at i * 16000; Overlapped ceil(n / 8000) windows at i * 8000 - 4000. None for no audio.
    std::vector<std::int64_t> planV3Windows(std::size_t sampleCount, V3Layout layout = V3Layout::Gate);
    /// Sample index of every v2.3 window's first sample: round(i * 16000 / 30) - 4160, one per 1/30 s (ceil), none for
    /// no audio.
    std::vector<std::int64_t> planV23Windows(std::size_t sampleCount);
    /// Copies audio[start, start + length) into \a dst, zeros where the range leaves the audio.
    void copyWindow(const std::vector<float>& audio, std::int64_t start, std::size_t length, float* dst);

    // ----- the run -----------------------------------------------------------------------------------------------

    struct GeneratedWeights
    {
        std::vector<std::string> mNames; ///< lower case ARKit names
        std::uint32_t mFrameRate = 0; ///< frames per second of mWeights
        std::vector<float> mWeights; ///< frameCount() rows of mNames.size(), 0..1
        // Diagnostics of the run (seconds): building the projection / noise, inside the network calls, in the solve.
        double mSetupSeconds = 0, mNetworkSeconds = 0, mSolveSeconds = 0;

        std::size_t frameCount() const { return mNames.empty() ? 0 : mWeights.size() / mNames.size(); }
        float at(std::size_t frame, std::size_t pose) const { return mWeights[frame * mNames.size() + pose]; }
    };

    struct GenerateOptions
    {
        V3Layout mV3Layout = V3Layout::Gate;
        /// Seeds the one noise tensor of a v3.0 run (generated once, reused by every window). The value is recorded in
        /// the curve file's generator.seed.
        std::uint64_t mNoiseSeed = 20261112;
        int mSolveIterations = 100;
    };

    /// \a progress gets a fraction in [0, 1] and returns false to cancel (the run then returns false with "cancelled").
    using A2FProgress = std::function<bool(double fraction, std::string_view message)>;

    /// Runs the whole line: windows through \a backend, the blendshape solve per frame. \a audio16k is mono at 16 kHz.
    bool generateWeights(const GeneratorModel& model, A2FBackend& backend, const std::vector<float>& audio16k,
        const GenerateOptions& options, const A2FProgress& progress, GeneratedWeights& out, std::string& error);

    /// The weight of \a pose at \a seconds, linear between the two nearest generated frames, held beyond the ends.
    float sampleWeight(const GeneratedWeights& weights, std::size_t pose, double seconds);
}
