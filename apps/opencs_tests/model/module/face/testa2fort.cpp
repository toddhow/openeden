// Task 0147 slice 2: the whole generation run with the REAL networks and ONNX Runtime. Opt in: set
// OPENEDEN_A2F_MODELS to a folder holding v3.0/ and v2.3-mark/ (the downloads, or links to them); without it every test
// here skips. The audio is synthetic (a few vowel-like bursts), not game audio. What is checked is what can be checked
// without a reference: the run completes, is finite and in range, reacts to speech-like input differently from
// silence, is repeatable on one provider, CPU and DirectML agree, progress and cancel work, the hash gate refuses a
// damaged copy. Whether the curves look like the speech is a judgement made on frames, not here.
#include "editor_modules/face_editor/a2f_rest.hpp"
#include "editor_modules/face_editor/audio_pcm.hpp"
#include "editor_modules/face_editor/generate_run.hpp"
#ifdef OPENEDEN_FACE_GENERATOR
#include "editor_modules/face_editor/ort_backend.hpp"
#endif

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
        // Slice 3: DirectML is optional; the tests that want it name an installed (or hand-placed) DirectML.dll.
        if (directMl)
            if (const char* dll = std::getenv("OPENEDEN_DML_DLL"))
                r.mDirectMlDll = dll;
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
        std::printf(
            "DirectML run used provider %s, fallback \"%s\"; compute %.3f s CPU, %.3f s %s for %.2f s of audio\n",
            gpu1.mProvider.c_str(), gpu1.mFallbackReason.c_str(), cpu.mComputeSeconds, gpu1.mComputeSeconds,
            gpu1.mProvider.c_str(), gpu1.mAudioSeconds);
        if (gpu1.mProvider != "DirectML")
            GTEST_SKIP() << "DirectML was not used (OPENEDEN_DML_DLL names a DirectML.dll to use): "
                         << gpu1.mFallbackReason;
        EXPECT_EQ(gpu1.mWeights.mWeights, gpu2.mWeights.mWeights); // same provider: identical
        ASSERT_EQ(cpu.mWeights.mWeights.size(), gpu1.mWeights.mWeights.size());
        double worst = 0;
        for (std::size_t i = 0; i < cpu.mWeights.mWeights.size(); ++i)
            worst
                = std::max(worst, static_cast<double>(std::fabs(cpu.mWeights.mWeights[i] - gpu1.mWeights.mWeights[i])));
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
        r.mAudio.resize(22050); // 1 s: one window, 30 frames
        ASSERT_TRUE(runGeneration(r, {}, a, error)) << error;
        ASSERT_TRUE(runGeneration(r, {}, b, error)) << error;
        EXPECT_EQ(a.mWeights.mFrameRate, 30u);
        EXPECT_EQ(a.mWeights.frameCount(), 30u);
        EXPECT_EQ(a.mWeights.mWeights, b.mWeights.mWeights);
        for (float w : a.mWeights.mWeights)
            ASSERT_TRUE(std::isfinite(w));
    }

    TEST(A2FOrt, v30GateLayoutOnTheGatesVoiceLineReproducesItsRecordedPeaks)
    {
        fs::path root;
        std::string why;
        const char* wav = std::getenv("OPENEDEN_A2F_VOICE_WAV");
        if (!modelsRoot(root) || wav == nullptr || !generationAvailable(why))
            GTEST_SKIP() << "needs OPENEDEN_A2F_MODELS, OPENEDEN_A2F_VOICE_WAV (the gate's bIdl_AM001 as 16 kHz WAV) "
                            "and the runtime";
        Pcm pcm;
        std::string error;
        ASSERT_TRUE(loadVoiceFile(wav, pcm, error)) << error;
        GenerateRequest r;
        r.mModelsRoot = root;
        r.mVariantId = "v3.0";
        r.mAllowDirectMl = false;
        r.mSubtractRest = false; // the recorded peaks are the raw solve
        r.mAudio = pcm.mSamples;
        r.mAudioRate = pcm.mRate;
        GenerateResult gate, overlapped;
        ASSERT_TRUE(runGeneration(r, {}, gate, error)) << error;
        r.mOptions.mV3Layout = V3Layout::Overlapped;
        ASSERT_TRUE(runGeneration(r, {}, overlapped, error)) << error;
        std::printf(
            "v3.0 on the gate's line: Gate jaw %.3f close %.3f pucker %.3f (%zu frames at %u); Overlapped jaw %.3f "
            "close %.3f "
            "pucker %.3f (%zu frames at %u); the gate recorded 0.547 / 0.861 / 0.628\n",
            peak(gate.mWeights, "jawopen"), peak(gate.mWeights, "mouthclose"), peak(gate.mWeights, "mouthpucker"),
            gate.mWeights.frameCount(), gate.mWeights.mFrameRate, peak(overlapped.mWeights, "jawopen"),
            peak(overlapped.mWeights, "mouthclose"), peak(overlapped.mWeights, "mouthpucker"),
            overlapped.mWeights.frameCount(), overlapped.mWeights.mFrameRate);
        // The gate's noise was per window and numpy's; ours is one seeded tensor, so near, not equal.
        EXPECT_NEAR(peak(gate.mWeights, "jawopen"), 0.547f, 0.06f);
        EXPECT_NEAR(peak(gate.mWeights, "mouthclose"), 0.861f, 0.08f);
        EXPECT_NEAR(peak(gate.mWeights, "mouthpucker"), 0.628f, 0.12f);
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
        ASSERT_TRUE(runGeneration(
            request(root, "v2.3-mark", false),
            [&](double f, std::string_view) {
                monotonic = monotonic && f >= last;
                last = f;
                return true;
            },
            r, error))
            << error;
        EXPECT_TRUE(monotonic);
        EXPECT_NEAR(last, 1.0, 1e-9);
        int calls = 0;
        GenerateResult c;
        EXPECT_FALSE(runGeneration(
            request(root, "v2.3-mark", false), [&](double, std::string_view) { return ++calls < 6; }, c, error));
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
            std::fstream f(
                dir / "models" / "v2.3-mark" / "network.onnx", std::ios::in | std::ios::out | std::ios::binary);
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

#ifdef OPENEDEN_FACE_GENERATOR
    // ---- slice 3: DirectML is optional, the rest offset -----------------------------------------------------------

    TEST(A2FOrt, withoutAnInstalledDirectMlTheCpuRunsAndSaysWhy)
    {
        fs::path root;
        std::string why;
        if (!modelsRoot(root) || !generationAvailable(why))
            GTEST_SKIP() << why;
        GenerateRequest r = request(root, "v2.3-mark", true);
        r.mDirectMlDll.clear();
        GenerateResult result;
        std::string error;
        ASSERT_TRUE(runGeneration(r, {}, result, error)) << error;
        EXPECT_EQ(result.mProvider, "CPU");
        EXPECT_NE(result.mFallbackReason.find("not installed"), std::string::npos) << result.mFallbackReason;
        // Not asking for the GPU says nothing.
        r.mAllowDirectMl = false;
        ASSERT_TRUE(runGeneration(r, {}, result, error)) << error;
        EXPECT_TRUE(result.mFallbackReason.empty());
    }

    TEST(A2FOrt, aMissingOrWrongDirectMlPathFallsBackToTheCpuWithAReason)
    {
        fs::path root;
        std::string why;
        if (!modelsRoot(root) || !generationAvailable(why))
            GTEST_SKIP() << why;
        GenerateRequest r = request(root, "v2.3-mark", true);
        r.mDirectMlDll = fs::temp_directory_path() / "oe_no_such_dir" / "DirectML.dll";
        GenerateResult result;
        std::string error;
        ASSERT_TRUE(runGeneration(r, {}, result, error)) << error;
        EXPECT_EQ(result.mProvider, "CPU");
        EXPECT_NE(result.mFallbackReason.find("DirectML"), std::string::npos);
        // A file that is not a DLL.
        const fs::path junk = fs::temp_directory_path() / "oe_fake_dml" / "DirectML.dll";
        fs::create_directories(junk.parent_path());
        std::ofstream(junk, std::ios::binary) << "this is not a dll";
        r.mDirectMlDll = junk;
        ASSERT_TRUE(runGeneration(r, {}, result, error)) << error;
        EXPECT_EQ(result.mProvider, "CPU");
        EXPECT_NE(result.mFallbackReason.find("could not"), std::string::npos) << result.mFallbackReason;
    }

    TEST(A2FOrt, theInstalledDirectMlDllIsTheOneLoadedByFullPath)
    {
        const char* dll = std::getenv("OPENEDEN_DML_DLL");
        std::string why;
        if (dll == nullptr || !generationAvailable(why))
            GTEST_SKIP() << "OPENEDEN_DML_DLL (a DirectML.dll outside System32) is not set: " << why;
        ASSERT_TRUE(ortLoadDirectMl(dll, why)) << why;
        EXPECT_TRUE(ortLoadDirectMl(dll, why)) << "loading twice is fine: " << why;
    }

    /// Measures the rest table of a variant with the real network on the CPU: the options and silence the table's
    /// header in a2f_rest_table.inc describes.
    bool measureVariant(const fs::path& root, const char* id, A2FVariant variant, RestTable& table, std::string& error)
    {
        GeneratorModel model;
        const fs::path folder = root / id;
        if (!loadGeneratorModel(folder, variant, model, error))
            return false;
        std::string fallback;
        auto backend = OrtBackend::create(folder / "network.onnx", variant, fs::path(), fallback, error);
        if (!backend)
            return false;
        return measureRest(model, *backend, kRestSilenceSeconds, GenerateOptions{}, table, error);
    }

    std::string tableText(const char* symbol, const RestTable& table)
    {
        std::string text = std::string("static const RestEntry ") + symbol + "[] = {\n";
        char line[96];
        for (const auto& [name, value] : table)
        {
            std::snprintf(line, sizeof(line), "    { \"%s\", %.6ff },\n", name.c_str(), value);
            text += line;
        }
        return text + "};\n";
    }

    // Prints the two tables for a2f_rest_table.inc (to the file OPENEDEN_A2F_REST_OUT when set, else stdout).
    TEST(A2FOrt, printRestTable)
    {
        fs::path root;
        std::string why;
        if (!modelsRoot(root) || !generationAvailable(why) || std::getenv("OPENEDEN_A2F_PRINT_REST") == nullptr)
            GTEST_SKIP() << "set OPENEDEN_A2F_MODELS and OPENEDEN_A2F_PRINT_REST=1 to measure";
        RestTable v30, v23;
        std::string error;
        ASSERT_TRUE(measureVariant(root, "v3.0", A2FVariant::V30, v30, error)) << error;
        ASSERT_TRUE(measureVariant(root, "v2.3-mark", A2FVariant::V23, v23, error)) << error;
        const std::string text = tableText("sRestV30", v30) + tableText("sRestV23", v23);
        if (const char* out = std::getenv("OPENEDEN_A2F_REST_OUT"))
            std::ofstream(out, std::ios::binary) << text;
        else
            std::printf("%s", text.c_str());
    }

    // The constants in the source are what the networks give today on silence (the CPU provider is deterministic).
    TEST(A2FOrt, theStoredRestTablesEqualAFreshMeasurement)
    {
        fs::path root;
        std::string why;
        if (!modelsRoot(root) || !generationAvailable(why))
            GTEST_SKIP() << why;
        for (auto [id, variant] : { std::pair{ "v2.3-mark", A2FVariant::V23 }, std::pair{ "v3.0", A2FVariant::V30 } })
        {
            RestTable fresh;
            std::string error;
            ASSERT_TRUE(measureVariant(root, id, variant, fresh, error)) << error;
            const RestTable& stored = restTable(variant);
            ASSERT_EQ(fresh.size(), stored.size()) << id;
            for (std::size_t i = 0; i < fresh.size(); ++i)
            {
                EXPECT_EQ(fresh[i].first, stored[i].first) << id;
                EXPECT_NEAR(fresh[i].second, stored[i].second, 1e-4) << id << " " << fresh[i].first;
            }
        }
    }

    TEST(A2FOrt, silenceGivesNearlyNothingOnceTheRestIsSubtracted)
    {
        fs::path root;
        std::string why;
        if (!modelsRoot(root) || !generationAvailable(why))
            GTEST_SKIP() << why;
        for (const char* id : { "v2.3-mark", "v3.0" })
        {
            GenerateRequest r = request(root, id, false, false);
            r.mAudio.assign(22050 * 4, 0.f);
            GenerateResult raw, cut;
            std::string error;
            r.mSubtractRest = false;
            ASSERT_TRUE(runGeneration(r, {}, raw, error)) << error;
            r.mSubtractRest = true;
            ASSERT_TRUE(runGeneration(r, {}, cut, error)) << error;
            EXPECT_TRUE(cut.mRestSubtracted);
            EXPECT_FALSE(raw.mRestSubtracted);
            EXPECT_GT(peak(raw.mWeights, "jawopen"), 0.03f) << id;
            // The mean of the cut jaw on silence is near zero (the table was measured with ten seconds of it).
            double sum = 0;
            std::size_t n = 0;
            for (std::size_t p = 0; p < cut.mWeights.mNames.size(); ++p)
                if (cut.mWeights.mNames[p] == "jawopen")
                    for (std::size_t f = 0; f < cut.mWeights.frameCount(); ++f, ++n)
                        sum += cut.mWeights.at(f, p);
            ASSERT_GT(n, 0u);
            EXPECT_LT(sum / n, 0.04) << id;
            std::printf("%s: jawopen on silence raw peak %.3f, after the subtraction mean %.4f peak %.3f\n", id,
                peak(raw.mWeights, "jawopen"), sum / n, peak(cut.mWeights, "jawopen"));
        }
    }
#endif
}
