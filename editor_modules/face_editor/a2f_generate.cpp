#include "a2f_generate.hpp"

#include "a2f_npz.hpp"

#include <algorithm>
#include <cctype>
#include <chrono>
#include <cmath>
#include <numbers>

namespace face_editor
{
    namespace fs = std::filesystem;

    namespace
    {
        constexpr std::uint32_t kMaxRate = 384000;
        constexpr double kSincZeroCrossings = 16.0;

        std::string lowerCase(std::string s)
        {
            std::transform(
                s.begin(), s.end(), s.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
            return s;
        }

        std::uint64_t splitmix64(std::uint64_t& state)
        {
            state += 0x9E3779B97F4A7C15ull;
            std::uint64_t z = state;
            z = (z ^ (z >> 30)) * 0xBF58476D1CE4E5B9ull;
            z = (z ^ (z >> 27)) * 0x94D049BB133111EBull;
            return z ^ (z >> 31);
        }

        using Clock = std::chrono::steady_clock;
        double secondsSince(Clock::time_point t)
        {
            return std::chrono::duration<double>(Clock::now() - t).count();
        }

        bool report(const A2FProgress& progress, double fraction, std::string_view message, std::string& error)
        {
            if (progress && !progress(std::clamp(fraction, 0.0, 1.0), message))
            {
                error = "cancelled";
                return false;
            }
            return true;
        }
    }

    // ----- model files -------------------------------------------------------------------------------------------------

    bool loadGeneratorModel(const fs::path& folder, A2FVariant variant, GeneratorModel& out, std::string& error)
    {
        GeneratorModel m;
        m.mVariant = variant;
        const bool v3 = variant == A2FVariant::V30;
        if (!loadBlendshapeSet(folder, v3 ? "bs_skin_Mark.npz" : "bs_skin.npz",
                v3 ? "bs_skin_config_Mark.json" : "bs_skin_config.json", m.mPoses, error))
            return false;
        for (std::string& name : m.mPoses.mNames)
            name = lowerCase(name);
        if (v3)
        {
            out = std::move(m);
            return true;
        }

        const std::size_t row = m.mPoses.rowLength();
        NpzFile data;
        if (!data.open(folder / "model_data.npz", error))
            return false;
        NpyInfo meanInfo, basisInfo;
        std::vector<float> mean;
        if (!data.readFloats("shapes_mean_skin", mean, meanInfo, error))
            return false;
        if (!data.info("shapes_matrix_skin", basisInfo, error))
            return false;
        const std::size_t V = m.mPoses.mVertexCount;
        if (meanInfo.mShape != std::vector<std::size_t>{ V, 3 })
        {
            error = "model_data.npz: shapes_mean_skin does not have the blendshape mesh's " + std::to_string(V)
                + " vertices";
            return false;
        }
        if (basisInfo.mDescr != "<f4" || basisInfo.mFortran
            || basisInfo.mShape != std::vector<std::size_t>{ A2FDims::sV23Coefficients, V, 3 })
        {
            error = "model_data.npz: shapes_matrix_skin is not " + std::to_string(A2FDims::sV23Coefficients) + " x "
                + std::to_string(V) + " x 3 float32 (the blendshape mesh's size)";
            return false;
        }
        m.mV23MeanMinusNeutral.resize(row);
        for (std::size_t k = 0; k < m.mPoses.mMask.size(); ++k)
            for (std::size_t c = 0; c < 3; ++c)
                m.mV23MeanMinusNeutral[k * 3 + c]
                    = mean[static_cast<std::size_t>(m.mPoses.mMask[k]) * 3 + c] - m.mPoses.mNeutralMasked[k * 3 + c];
        m.mV23Basis.resize(A2FDims::sV23Coefficients * row);
        std::vector<float> slab(V * 3);
        for (std::size_t k = 0; k < A2FDims::sV23Coefficients; ++k)
        {
            if (!data.readBytes(basisInfo, static_cast<std::uint64_t>(k) * V * 3 * 4, V * 3 * 4, slab.data(), error))
                return false;
            for (std::size_t j = 0; j < m.mPoses.mMask.size(); ++j)
                for (std::size_t c = 0; c < 3; ++c)
                    m.mV23Basis[k * row + j * 3 + c] = slab[static_cast<std::size_t>(m.mPoses.mMask[j]) * 3 + c];
        }

        NpzFile emo;
        if (!emo.open(folder / "implicit_emo_db.npz", error))
            return false;
        std::vector<float> db;
        std::vector<std::int32_t> starts;
        std::vector<std::string> names;
        NpyInfo dbInfo, startInfo;
        if (!emo.readFloats("emo_db", db, dbInfo, error) || !emo.readInt32s("emo_spec_start", starts, startInfo, error)
            || !emo.readStrings("emo_spec_names", names, error))
            return false;
        if (dbInfo.mShape.size() != 2 || dbInfo.mShape[1] != 16 || starts.empty() || names.size() != starts.size())
        {
            error = "implicit_emo_db.npz: unexpected emo_db / emo_spec_* shapes";
            return false;
        }
        std::size_t pick = 0;
        for (std::size_t i = 0; i < names.size(); ++i)
            if (lowerCase(names[i]).find("neutral") != std::string::npos)
            {
                pick = i;
                break;
            }
        if (starts[pick] < 0 || static_cast<std::size_t>(starts[pick]) >= dbInfo.mShape[0])
        {
            error = "implicit_emo_db.npz: emo_spec_start points outside emo_db";
            return false;
        }
        m.mV23Emotion.assign(A2FDims::sV23EmotionFloats, 0.f);
        for (std::size_t i = 0; i < 16; ++i)
            m.mV23Emotion[10 + i] = db[static_cast<std::size_t>(starts[pick]) * 16 + i];
        out = std::move(m);
        return true;
    }

    // ----- audio and noise ---------------------------------------------------------------------------------------------

    std::vector<float> resampleMono(
        const std::vector<float>& in, std::uint32_t rateIn, std::uint32_t rateOut, std::string& error)
    {
        if (rateIn == 0 || rateOut == 0 || rateIn > kMaxRate || rateOut > kMaxRate)
        {
            error = "unsupported sample rate " + std::to_string(rateIn) + " -> " + std::to_string(rateOut);
            return {};
        }
        if (in.empty())
            return {};
        if (rateIn == rateOut)
            return in;
        const std::uint64_t outCount = (static_cast<std::uint64_t>(in.size()) * rateOut + rateIn / 2) / rateIn;
        const double ratio = static_cast<double>(rateOut) / rateIn;
        const double cutoff = std::min(1.0, ratio); // in cycles per input sample, relative to the input Nyquist rate
        const double halfWidth = kSincZeroCrossings / cutoff; // in input samples
        const double pi = std::numbers::pi;
        std::vector<float> out(static_cast<std::size_t>(outCount));
        const auto n = static_cast<std::int64_t>(in.size());
        for (std::size_t i = 0; i < out.size(); ++i)
        {
            const double centre = static_cast<double>(i) / ratio;
            const auto first = static_cast<std::int64_t>(std::ceil(centre - halfWidth));
            const auto last = static_cast<std::int64_t>(std::floor(centre + halfWidth));
            double sum = 0, weights = 0;
            for (std::int64_t k = first; k <= last; ++k)
            {
                const double d = centre - static_cast<double>(k);
                const double x = cutoff * d;
                const double sinc = std::fabs(x) < 1e-12 ? 1.0 : std::sin(pi * x) / (pi * x);
                const double t = d / halfWidth;
                const double window = 0.42 + 0.5 * std::cos(pi * t) + 0.08 * std::cos(2.0 * pi * t);
                const double h = cutoff * sinc * window;
                weights += h;
                if (k >= 0 && k < n)
                    sum += h * static_cast<double>(in[static_cast<std::size_t>(k)]);
            }
            out[i] = weights != 0.0 ? static_cast<float>(sum / weights) : 0.f;
        }
        return out;
    }

    void fillNormalNoise(float* dst, std::size_t count, std::uint64_t seed)
    {
        std::uint64_t state = seed;
        constexpr double kUnit = 1.0 / 9007199254740992.0; // 2^-53
        for (std::size_t i = 0; i < count; i += 2)
        {
            const double u1 = (static_cast<double>(splitmix64(state) >> 11) + 1.0) * kUnit; // (0, 1]
            const double u2 = static_cast<double>(splitmix64(state) >> 11) * kUnit; // [0, 1)
            const double r = std::sqrt(-2.0 * std::log(u1));
            const double a = 2.0 * std::numbers::pi * u2;
            dst[i] = static_cast<float>(r * std::cos(a));
            if (i + 1 < count)
                dst[i + 1] = static_cast<float>(r * std::sin(a));
        }
    }

    // ----- windows -------------------------------------------------------------------------------------------------

    std::vector<std::int64_t> planV3Windows(std::size_t sampleCount)
    {
        std::vector<std::int64_t> starts;
        const std::size_t windows = (sampleCount + A2FDims::sV3Hop - 1) / A2FDims::sV3Hop;
        for (std::size_t i = 0; i < windows; ++i)
            starts.push_back(static_cast<std::int64_t>(i * A2FDims::sV3Hop) - static_cast<std::int64_t>(A2FDims::sV3Hop / 2));
        return starts;
    }

    std::vector<std::int64_t> planV23Windows(std::size_t sampleCount)
    {
        std::vector<std::int64_t> starts;
        const std::size_t frames = (sampleCount * 30 + A2FDims::sSampleRate - 1) / A2FDims::sSampleRate;
        for (std::size_t i = 0; i < frames; ++i)
            starts.push_back(static_cast<std::int64_t>(std::llround(static_cast<double>(i) * 16000.0 / 30.0))
                - static_cast<std::int64_t>(A2FDims::sV23Window / 2));
        return starts;
    }

    void copyWindow(const std::vector<float>& audio, std::int64_t start, std::size_t length, float* dst)
    {
        const auto n = static_cast<std::int64_t>(audio.size());
        for (std::size_t i = 0; i < length; ++i)
        {
            const std::int64_t k = start + static_cast<std::int64_t>(i);
            dst[i] = k >= 0 && k < n ? audio[static_cast<std::size_t>(k)] : 0.f;
        }
    }

    // ----- the run -------------------------------------------------------------------------------------------------

    namespace
    {
        void appendWeights(GeneratedWeights& out, const double* w, std::size_t poses)
        {
            for (std::size_t i = 0; i < poses; ++i)
                out.mWeights.push_back(static_cast<float>(std::clamp(w[i], 0.0, 1.0)));
        }

        bool runV3(const GeneratorModel& model, A2FBackend& backend, const std::vector<float>& audio,
            const GenerateOptions& options, const A2FProgress& progress, GeneratedWeights& out, std::string& error)
        {
            const BlendshapeSet& set = model.mPoses;
            if (set.mVertexCount * 3 != A2FDims::sV3SkinFloats)
            {
                error = "the v3.0 network writes " + std::to_string(A2FDims::sV3SkinFloats / 3)
                    + " vertices but the blendshape mesh has " + std::to_string(set.mVertexCount) + " vertices";
                return false;
            }
            const auto setupStart = Clock::now();
            const BlendshapeSolver solver(set);
            const std::size_t P = solver.poseCount();
            const auto starts = planV3Windows(audio.size());
            const std::size_t totalFrames = (audio.size() * A2FDims::sV3OutputRate + A2FDims::sSampleRate - 1) / A2FDims::sSampleRate;

            std::vector<float> noise(A2FDims::sV3NoiseFloats);
            fillNormalNoise(noise.data(), noise.size(), options.mNoiseSeed);
            std::vector<float> latents(A2FDims::sV3LatentFloats, 0.f), nextLatents(A2FDims::sV3LatentFloats, 0.f);
            const std::vector<float> emotion(A2FDims::sV3EmotionFloats, 0.f);
            std::vector<float> window(A2FDims::sV3Window);
            std::vector<float> prediction(A2FDims::sV3Frames * A2FDims::sV3Outputs);
            std::vector<float> target(set.rowLength());
            std::vector<double> b(P), w(P, 0.0);
            out.mSetupSeconds = secondsSince(setupStart);

            for (std::size_t i = 0; i < starts.size(); ++i)
            {
                if (!report(progress, static_cast<double>(i) / static_cast<double>(starts.size()),
                        "window " + std::to_string(i + 1) + " of " + std::to_string(starts.size()), error))
                    return false;
                copyWindow(audio, starts[i], window.size(), window.data());
                const auto netStart = Clock::now();
                if (!backend.runV3(window.data(), model.mIdentity.data(), emotion.data(), latents.data(), noise.data(),
                        prediction.data(), nextLatents.data(), error))
                    return false;
                out.mNetworkSeconds += secondsSince(netStart);
                const auto solveStart = Clock::now();
                latents.swap(nextLatents);
                for (std::size_t j = 0; j < A2FDims::sV3Keep && out.frameCount() < totalFrames; ++j)
                {
                    const float* skin = prediction.data() + (A2FDims::sV3KeepFrom + j) * A2FDims::sV3Outputs;
                    for (std::size_t m = 0; m < set.mMask.size(); ++m)
                        for (std::size_t c = 0; c < 3; ++c)
                            target[m * 3 + c] = skin[static_cast<std::size_t>(set.mMask[m]) * 3 + c];
                    solver.project(target.data(), b.data());
                    solver.solve(b.data(), w.data(), w.data(), options.mSolveIterations);
                    appendWeights(out, w.data(), P);
                }
                out.mSolveSeconds += secondsSince(solveStart);
            }
            out.mFrameRate = static_cast<std::uint32_t>(A2FDims::sV3OutputRate);
            return report(progress, 1.0, "done", error);
        }

        bool runV23(const GeneratorModel& model, A2FBackend& backend, const std::vector<float>& audio,
            const GenerateOptions& options, const A2FProgress& progress, GeneratedWeights& out, std::string& error)
        {
            const BlendshapeSet& set = model.mPoses;
            const std::size_t row = set.rowLength();
            if (model.mV23Basis.size() != A2FDims::sV23Coefficients * row || model.mV23MeanMinusNeutral.size() != row
                || model.mV23Emotion.size() != A2FDims::sV23EmotionFloats)
            {
                error = "the v2.3 skin model is missing or does not match the blendshape mesh";
                return false;
            }
            const auto setupStart = Clock::now();
            const BlendshapeSolver solver(set);
            const std::size_t P = solver.poseCount();
            const LinearProjection linear(solver, model.mV23MeanMinusNeutral, model.mV23Basis, A2FDims::sV23Coefficients);
            const auto starts = planV23Windows(audio.size());

            std::vector<float> windows(A2FDims::sV23Batch * A2FDims::sV23Window);
            std::vector<float> results(A2FDims::sV23Batch * A2FDims::sV23Outputs);
            std::vector<double> b(P), w(P, 0.0);
            out.mSetupSeconds = secondsSince(setupStart);
            for (std::size_t first = 0; first < starts.size(); first += A2FDims::sV23Batch)
            {
                const std::size_t count = std::min(A2FDims::sV23Batch, starts.size() - first);
                if (!report(progress, static_cast<double>(first) / static_cast<double>(starts.size()),
                        "frames " + std::to_string(first + 1) + " of " + std::to_string(starts.size()), error))
                    return false;
                for (std::size_t n = 0; n < count; ++n)
                    copyWindow(audio, starts[first + n], A2FDims::sV23Window, windows.data() + n * A2FDims::sV23Window);
                const auto netStart = Clock::now();
                if (!backend.runV23(windows.data(), count, model.mV23Emotion.data(), results.data(), error))
                    return false;
                out.mNetworkSeconds += secondsSince(netStart);
                const auto solveStart = Clock::now();
                for (std::size_t n = 0; n < count; ++n)
                {
                    linear.project(results.data() + n * A2FDims::sV23Outputs, b.data());
                    solver.solve(b.data(), w.data(), w.data(), options.mSolveIterations);
                    appendWeights(out, w.data(), P);
                }
                out.mSolveSeconds += secondsSince(solveStart);
            }
            out.mFrameRate = static_cast<std::uint32_t>(A2FDims::sV23OutputRate);
            return report(progress, 1.0, "done", error);
        }
    }

    bool generateWeights(const GeneratorModel& model, A2FBackend& backend, const std::vector<float>& audio16k,
        const GenerateOptions& options, const A2FProgress& progress, GeneratedWeights& out, std::string& error)
    {
        out = GeneratedWeights{};
        if (audio16k.empty())
        {
            error = "there is no audio to generate curves for";
            return false;
        }
        if (model.mPoses.mNames.empty() || model.mPoses.mMask.empty())
        {
            error = "the blendshape poses are not loaded";
            return false;
        }
        out.mNames = model.mPoses.mNames;
        const bool ok = model.mVariant == A2FVariant::V30
            ? runV3(model, backend, audio16k, options, progress, out, error)
            : runV23(model, backend, audio16k, options, progress, out, error);
        if (!ok)
            out = GeneratedWeights{};
        return ok;
    }

    float sampleWeight(const GeneratedWeights& weights, std::size_t pose, double seconds)
    {
        const std::size_t frames = weights.frameCount();
        if (frames == 0 || weights.mFrameRate == 0 || pose >= weights.mNames.size())
            return 0.f;
        const double p = seconds * weights.mFrameRate;
        if (!(p > 0.0))
            return weights.at(0, pose);
        const double lastIndex = static_cast<double>(frames - 1);
        if (p >= lastIndex)
            return weights.at(frames - 1, pose);
        const auto i0 = static_cast<std::size_t>(std::floor(p));
        const double f = p - static_cast<double>(i0);
        return static_cast<float>(
            weights.at(i0, pose) * (1.0 - f) + weights.at(i0 + 1, pose) * f);
    }
}
