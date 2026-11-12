// Task 0147 slice 2: the whole generation run with the REAL networks and ONNX Runtime. Opt in: set
// OPENEDEN_A2F_MODELS to a folder holding v3.0/ and v2.3-mark/ (the downloads, or links to them); without it every test
// here skips. The audio is synthetic (a few vowel-like bursts), not game audio. What is checked is what can be checked
// without a reference: the run completes, is finite and in range, reacts to speech-like input differently from
// silence, is repeatable on one provider, CPU and DirectML agree, progress and cancel work, the hash gate refuses a
// damaged copy. Whether the curves look like the speech is a judgement made on frames, not here.
#include "editor_modules/face_editor/generate_run.hpp"

#include <gtest/gtest.h>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <numbers>

namespace
{
    using namespace face_editor;
    namespace fs = std::filesystem;

    bool modelsRoot(fs::path& root)
    {
        const char* env = std::getenv("OPENEDEN_A2F_MODELS");
        if (env == nullptr)
            return false;
        root = env;
        return true;
    }

    /// 1.5 s at 22.05 kHz: bursts of a 120 Hz voice with formants moving, silence between.
    std::vector<float> speechLike(std::uint32_t rate, double seconds = 1.5)
    {
        std::vector<float> a(static_cast<std::size_t>(rate * seconds), 0.f);
        for (std::size_t i = 0; i < a.size(); ++i)
        {
            const double t = static_cast<double>(i) / rate;
            const double burst = std::fmod(t, 0.5);
            if (burst > 0.35)
                continue;
            const double env = std::sin(std::numbers::pi * burst / 0.35);
            const double f1 = 400 + 300 * std::sin(2 * std::numbers::pi * 3 * t);
            double s = 0;
            for (int h = 1; h <= 20; ++h)
                s += std::sin(2 * std::numbers::pi * 120 * h * t) / h * std::exp(-std::pow((120 * h - f1) / 300.0, 2));
            a[i] = static_cast<float>(0.3 * env * s);
        }
        return a;
    }

    GenerateRequest request(const fs::path& root, const char* variant, bool directMl, bool speech = true)
    {
        GenerateRequest r;
        r.mModelsRoot = root;
        r.mVariantId = variant;
        r.mAllowDirectMl = directMl;
        r.mAudioRate = 22050;
        r.mAudio = speech ? speechLike(22050) : std::vector<float>(22050 * 3 / 2, 0.f);
        return r;
    }

    float peak(const GeneratedWeights& w, const char* name)
    {
        for (std::size_t p = 0; p < w.mNames.size(); ++p)
            if (w.mNames[p] == name)
            {
                float m = 0;
                for (std::size_t f = 0; f < w.frameCount(); ++f)
                    m = std::max(m, w.at(f, p));
                return m;
            }
        return -1;
    }

    TEST(A2FOrt, v23RunsFiniteInRangeAndSpeechDiffersFromSilence)
    {
        fs::path root;
        std::string why;
        if (!modelsRoot(root) || !generationAvailable(why))
            GTEST_SKIP() << "OPENEDEN_A2F_MODELS not set or no runtime: " << why;
        GenerateResult speech, silence;
        std::string error;
        ASSERT_TRUE(runGeneration(request(root, "v2.3-mark", false), {}, speech, error)) << error;
        ASSERT_TRUE(runGeneration(request(root, "v2.3-mark", false, false), {}, silence, error)) << error;
        EXPECT_EQ(speech.mProvider, "CPU");
        EXPECT_EQ(speech.mWeights.mFrameRate, 30u);
        EXPECT_EQ(speech.mWeights.mNames.size(), 52u);
        EXPECT_EQ(speech.mWeights.frameCount(), 45u); // 1.5 s * 30
        for (float w : speech.mWeights.mWeights)
        {
            ASSERT_TRUE(std::isfinite(w));
            ASSERT_GE(w, 0.f);
            ASSERT_LE(w, 1.f);
        }
        EXPECT_GT(peak(speech.mWeights, "jawopen"), peak(silence.mWeights, "jawopen") + 0.05f);
    }

    TEST(A2FOrt, v23TwoCpuRunsGiveIdenticalWeights)
    {
        fs::path root;
        std::string why;
        if (!modelsRoot(root) || !generationAvailable(why))
            GTEST_SKIP() << why;
        GenerateResult a, b;
        std::string error;
        ASSERT_TRUE(runGeneration(request(root, "v2.3-mark", false), {}, a, error)) << error;
        ASSERT_TRUE(runGeneration(request(root, "v2.3-mark", false), {}, b, error)) << error;
        EXPECT_EQ(a.mWeights.mWeights, b.mWeights.mWeights);
    }

    TEST(A2FOrt, v23DirectMlRunsAndAgreesWithTheCpuWithinRounding)
    {
        fs::path root;
        std::string why;
        if (!modelsRoot(root) || !generationAvailable(why))
            GTEST_SKIP() << why;
        GenerateResult cpu, gpu1, gpu2;
        std::string error;
        ASSERT_TRUE(runGeneration(request(root, "v2.3-mark", false), {}, cpu, error)) << error;
        ASSERT_TRUE(runGeneration(request(root, "v2.3-mark", true), {}, gpu1, error)) << error;
        ASSERT_TRUE(runGeneration(request(root, "v2.3-mark", true), {}, gpu2, error)) << error;
        std::printf("DirectML run used provider %s, fallback \"%s\"; compute %.3f s CPU, %.3f s %s for %.2f s of audio\n",
            gpu1.mProvider.c_str(), gpu1.mFallbackReason.c_str(), cpu.mComputeSeconds, gpu1.mComputeSeconds,
            gpu1.mProvider.c_str(), gpu1.mAudioSeconds);
        if (gpu1.mProvider != "DirectML")
            GTEST_SKIP() << "DirectML was not used: " << gpu1.mFallbackReason;
        EXPECT_EQ(gpu1.mWeights.mWeights, gpu2.mWeights.mWeights); // same provider: identical
        ASSERT_EQ(cpu.mWeights.mWeights.size(), gpu1.mWeights.mWeights.size());
        double worst = 0;
        for (std::size_t i = 0; i < cpu.mWeights.mWeights.size(); ++i)
            worst = std::max(worst, static_cast<double>(std::fabs(cpu.mWeights.mWeights[i] - gpu1.mWeights.mWeights[i])));
        EXPECT_LT(worst, 0.02);
    }

    TEST(A2FOrt, v30RunsOnTheCpuAndKeepsItsFixedNoiseRepeatable)
    {
        fs::path root;
        std::string why;
        if (!modelsRoot(root) || !generationAvailable(why))
            GTEST_SKIP() << why;
        GenerateResult a, b;
        std::string error;
        auto r = request(root, "v3.0", false);
        r.mAudio.resize(22050); // one second: two windows
        ASSERT_TRUE(runGeneration(r, {}, a, error)) << error;
        ASSERT_TRUE(runGeneration(r, {}, b, error)) << error;
        EXPECT_EQ(a.mWeights.mFrameRate, 60u);
        EXPECT_EQ(a.mWeights.frameCount(), 60u);
        EXPECT_EQ(a.mWeights.mWeights, b.mWeights.mWeights);
        for (float w : a.mWeights.mWeights)
            ASSERT_TRUE(std::isfinite(w));
    }

    TEST(A2FOrt, progressNeverGoesBackAndACancelStopsTheRun)
    {
        fs::path root;
        std::string why;
        if (!modelsRoot(root) || !generationAvailable(why))
            GTEST_SKIP() << why;
        GenerateResult r;
        std::string error;
        double last = -1;
        bool monotonic = true;
        ASSERT_TRUE(runGeneration(request(root, "v2.3-mark", false), [&](double f, std::string_view) {
            monotonic = monotonic && f >= last;
            last = f;
            return true;
        }, r, error)) << error;
        EXPECT_TRUE(monotonic);
        EXPECT_NEAR(last, 1.0, 1e-9);
        int calls = 0;
        GenerateResult c;
        EXPECT_FALSE(runGeneration(request(root, "v2.3-mark", false), [&](double, std::string_view) { return ++calls < 6; }, c, error));
        EXPECT_EQ(error, "cancelled");
    }

    TEST(A2FOrt, aDamagedNetworkIsRefusedByTheHashGateBeforeItIsLoaded)
    {
        fs::path root;
        std::string why;
        if (!modelsRoot(root) || !generationAvailable(why))
            GTEST_SKIP() << why;
        // A copy of the v2.3 folder whose network.onnx has a different size is caught by the size check; one with the
        // right size and one flipped byte only by the hash. Build the second (75 MB copy).
        const fs::path dir = fs::temp_directory_path() / "oe_a2f_ort_damaged";
        fs::remove_all(dir);
        fs::create_directories(dir / "models" / "v2.3-mark");
        std::error_code ec;
        for (const auto& entry : fs::directory_iterator(root / "v2.3-mark"))
            fs::copy_file(entry.path(), dir / "models" / "v2.3-mark" / entry.path().filename(), ec);
        {
            std::fstream f(dir / "models" / "v2.3-mark" / "network.onnx", std::ios::in | std::ios::out | std::ios::binary);
            ASSERT_TRUE(f.good());
            f.seekp(1000000);
            char c = 0;
            f.seekg(1000000);
            f.read(&c, 1);
            c = static_cast<char>(c ^ 0x55);
            f.seekp(1000000);
            f.write(&c, 1);
        }
        GenerateResult r;
        std::string error;
        EXPECT_FALSE(runGeneration(request(dir / "models", "v2.3-mark", false), {}, r, error));
        EXPECT_NE(error.find("SHA-256"), std::string::npos) << error;
        fs::remove_all(dir);
    }
}
