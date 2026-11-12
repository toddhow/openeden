// Task 0147 slice 2: the generator's windowing, the run and the model loading, with a STUB network. The stub writes
// meshes built from known blendshape weights (poses with disjoint support), so what comes out of generateWeights is
// known by construction: the frame bookkeeping (which output frames are kept, how many, at what rate), the fixed noise,
// the carried latents and the determinism are tested here; whether the real networks behave is tested by the editor
// check and the gated tests (OPENEDEN_A2F_MODELS).
#include "editor_modules/face_editor/a2f_generate.hpp"
#include "editor_modules/face_editor/a2f_npz.hpp"

#include <gtest/gtest.h>

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <fstream>

namespace
{
    using namespace face_editor;
    namespace fs = std::filesystem;

    // ---- planning --------------------------------------------------------------------------------------------------

    TEST(A2FPlan, v3WindowsHopHalfASecondAndAreCentredOnTheirHop)
    {
        EXPECT_TRUE(planV3Windows(0).empty());
        EXPECT_EQ(planV3Windows(1), (std::vector<std::int64_t>{ -4000 }));
        EXPECT_EQ(planV3Windows(8000), (std::vector<std::int64_t>{ -4000 }));
        EXPECT_EQ(planV3Windows(8001), (std::vector<std::int64_t>{ -4000, 4000 }));
        EXPECT_EQ(planV3Windows(20000), (std::vector<std::int64_t>{ -4000, 4000, 12000 }));
    }

    TEST(A2FPlan, v23WindowsAreOnePerThirtiethOfASecondCentredOnTheFrame)
    {
        EXPECT_TRUE(planV23Windows(0).empty());
        const auto w = planV23Windows(16000); // exactly one second -> 30 frames
        ASSERT_EQ(w.size(), 30u);
        EXPECT_EQ(w[0], -4160);
        EXPECT_EQ(w[1], 533 - 4160); // round(533.33)
        EXPECT_EQ(w[3], 1600 - 4160);
        EXPECT_EQ(w[29], 15467 - 4160); // round(29 * 533.33) = round(15466.67)
        EXPECT_EQ(planV23Windows(20000).size(), 38u); // ceil(20000 * 30 / 16000) = ceil(37.5)
        EXPECT_EQ(planV23Windows(1).size(), 1u);
    }

    TEST(A2FPlan, copyWindowZeroPadsOutsideTheAudio)
    {
        const std::vector<float> a{ 1, 2, 3, 4 };
        float dst[6];
        copyWindow(a, -2, 6, dst);
        EXPECT_EQ(std::vector<float>(dst, dst + 6), (std::vector<float>{ 0, 0, 1, 2, 3, 4 }));
        copyWindow(a, 2, 6, dst);
        EXPECT_EQ(std::vector<float>(dst, dst + 6), (std::vector<float>{ 3, 4, 0, 0, 0, 0 }));
        copyWindow(a, 100, 3, dst);
        EXPECT_EQ(std::vector<float>(dst, dst + 3), (std::vector<float>{ 0, 0, 0 }));
        copyWindow(a, -100, 3, dst);
        EXPECT_EQ(std::vector<float>(dst, dst + 3), (std::vector<float>{ 0, 0, 0 }));
    }

    // ---- a synthetic model and stub networks ---------------------------------------------------------------------

    /// Two poses with disjoint support over a mask of eight vertices of a 24002 vertex mesh: pose 0 moves mask[0..3] by
    /// z = 1, pose 1 moves mask[4..7] by z = 2.
    GeneratorModel syntheticV3()
    {
        GeneratorModel m;
        m.mVariant = A2FVariant::V30;
        BlendshapeSet& s = m.mPoses;
        s.mVertexCount = 24002;
        for (std::uint32_t v = 1; v <= 8; ++v)
            s.mMask.push_back(v * 10);
        s.mNames = { "jawopen", "mouthclose" };
        s.mNeutralMasked.assign(24, 0.f);
        s.mPosesMasked.assign(2 * 24, 0.f);
        for (std::size_t v = 0; v < 4; ++v)
            s.mPosesMasked[0 * 24 + v * 3 + 2] = 1.f;
        for (std::size_t v = 4; v < 8; ++v)
            s.mPosesMasked[1 * 24 + v * 3 + 2] = 2.f;
        s.mConfig.mL2 = 1e-4;
        s.mConfig.mActive = { true, true };
        s.mConfig.mSymmetryGroup = { -1, -1 };
        return m;
    }

    void writeMesh(float* skin, const BlendshapeSet& s, float w0, float w1)
    {
        for (std::size_t v = 0; v < 4; ++v)
            skin[s.mMask[v] * 3 + 2] = w0;
        for (std::size_t v = 4; v < 8; ++v)
            skin[s.mMask[v] * 3 + 2] = 2.f * w1;
    }

    class StubV3 final : public A2FBackend
    {
    public:
        explicit StubV3(const GeneratorModel& m)
            : mModel(m)
        {
        }

        bool runV3(const float* window, const float* identity, const float* emotion, const float* latentsIn,
            const float* noise, float* prediction, float* latentsOut, std::string& error) override
        {
            if (mFailAt == mCalls)
            {
                error = "boom";
                return false;
            }
            mWindows.emplace_back(window, window + A2FDims::sV3Window);
            mIdentities.emplace_back(identity, identity + 3);
            mNoisePointers.push_back(noise);
            mNoiseHeads.emplace_back(noise, noise + 4);
            mLatentsSeen.push_back(latentsIn[0]);
            for (std::size_t i = 0; i < A2FDims::sV3EmotionFloats; ++i)
                if (emotion[i] != 0.f)
                    mEmotionNonZero = true;
            std::memset(prediction, 0, sizeof(float) * A2FDims::sV3Frames * A2FDims::sV3Outputs);
            for (std::size_t f = 0; f < A2FDims::sV3Frames; ++f)
                writeMesh(prediction + f * A2FDims::sV3Outputs, mModel.mPoses,
                    static_cast<float>(f + 100 * mCalls) / 1000.f, 0.5f);
            std::memcpy(latentsOut, latentsIn, sizeof(float) * A2FDims::sV3LatentFloats);
            latentsOut[0] = latentsIn[0] + 1.f;
            ++mCalls;
            return true;
        }

        bool runV23(const float*, std::size_t, const float*, float*, std::string& error) override
        {
            error = "not a v2.3 stub";
            return false;
        }

        const GeneratorModel& mModel;
        int mCalls = 0;
        int mFailAt = -1;
        bool mEmotionNonZero = false;
        std::vector<std::vector<float>> mWindows, mIdentities, mNoiseHeads;
        std::vector<const float*> mNoisePointers;
        std::vector<float> mLatentsSeen;
    };

    std::vector<float> ramp(std::size_t n)
    {
        std::vector<float> a(n);
        for (std::size_t i = 0; i < n; ++i)
            a[i] = static_cast<float>(i % 1000) / 1000.f;
        return a;
    }

    TEST(A2FGenerate, v3KeepsFramesFifteenToFortyFourOfEveryWindowAtSixtyFramesPerSecond)
    {
        const GeneratorModel model = syntheticV3();
        StubV3 net(model);
        GeneratedWeights out;
        std::string error;
        ASSERT_TRUE(generateWeights(model, net, ramp(20000), {}, {}, out, error)) << error;
        EXPECT_EQ(out.mFrameRate, 60u);
        EXPECT_EQ(out.mNames, (std::vector<std::string>{ "jawopen", "mouthclose" }));
        EXPECT_EQ(out.frameCount(), 75u); // ceil(20000 * 60 / 16000), not the 90 the three windows hold
        EXPECT_EQ(net.mCalls, 3);
        for (std::size_t n = 0; n < out.frameCount(); ++n)
        {
            const std::size_t call = n / 30, j = n % 30;
            const float expected = static_cast<float>(15 + j + 100 * call) / 1000.f;
            EXPECT_NEAR(out.at(n, 0), expected, 0.01f) << n;
            EXPECT_NEAR(out.at(n, 1), 0.5f, 0.01f) << n;
        }
    }

    TEST(A2FGenerate, v3GivesEveryWindowTheLatentsOfTheLastTheIdentityAndOneSharedNoiseTensor)
    {
        const GeneratorModel model = syntheticV3();
        StubV3 net(model);
        GenerateOptions options;
        options.mNoiseSeed = 99;
        GeneratedWeights out;
        std::string error;
        ASSERT_TRUE(generateWeights(model, net, ramp(20000), options, {}, out, error)) << error;
        ASSERT_EQ(net.mCalls, 3);
        EXPECT_EQ(net.mLatentsSeen, (std::vector<float>{ 0.f, 1.f, 2.f }));
        for (const auto& id : net.mIdentities)
            EXPECT_EQ(id, (std::vector<float>{ 0.f, 0.f, 1.f }));
        EXPECT_FALSE(net.mEmotionNonZero); // neutral emotion
        // The same tensor (the same buffer) for every window, and it is the seeded normal noise.
        EXPECT_EQ(net.mNoisePointers[0], net.mNoisePointers[1]);
        EXPECT_EQ(net.mNoisePointers[1], net.mNoisePointers[2]);
        std::vector<float> expect(4);
        fillNormalNoise(expect.data(), 4, 99);
        EXPECT_EQ(net.mNoiseHeads[0], expect);
    }

    TEST(A2FGenerate, v3WindowsHoldTheAudioAtTheirPlannedPlace)
    {
        const GeneratorModel model = syntheticV3();
        StubV3 net(model);
        const auto audio = ramp(20000);
        GeneratedWeights out;
        std::string error;
        ASSERT_TRUE(generateWeights(model, net, audio, {}, {}, out, error)) << error;
        const auto starts = planV3Windows(audio.size());
        for (std::size_t i = 0; i < starts.size(); ++i)
        {
            std::vector<float> expect(A2FDims::sV3Window);
            copyWindow(audio, starts[i], expect.size(), expect.data());
            EXPECT_EQ(net.mWindows[i], expect) << i;
        }
    }

    TEST(A2FGenerate, twoRunsGiveIdenticalWeights)
    {
        const GeneratorModel model = syntheticV3();
        GeneratedWeights a, b;
        std::string error;
        {
            StubV3 net(model);
            ASSERT_TRUE(generateWeights(model, net, ramp(18000), {}, {}, a, error)) << error;
        }
        {
            StubV3 net(model);
            ASSERT_TRUE(generateWeights(model, net, ramp(18000), {}, {}, b, error)) << error;
        }
        EXPECT_EQ(a.mWeights, b.mWeights);
    }

    TEST(A2FGenerate, weightsAreInsideZeroAndOneAndFinite)
    {
        const GeneratorModel model = syntheticV3();
        StubV3 net(model);
        GeneratedWeights out;
        std::string error;
        ASSERT_TRUE(generateWeights(model, net, ramp(20000), {}, {}, out, error)) << error;
        for (float w : out.mWeights)
        {
            EXPECT_TRUE(std::isfinite(w));
            EXPECT_GE(w, 0.f);
            EXPECT_LE(w, 1.f);
        }
    }

    TEST(A2FGenerate, aCancelStopsTheRunWithoutMoreNetworkCalls)
    {
        const GeneratorModel model = syntheticV3();
        StubV3 net(model);
        int asked = 0;
        GeneratedWeights out;
        std::string error;
        EXPECT_FALSE(generateWeights(model, net, ramp(40000), {}, [&](double, std::string_view) { return ++asked < 2; },
            out, error));
        EXPECT_NE(error.find("cancelled"), std::string::npos) << error;
        EXPECT_LE(net.mCalls, 2);
        EXPECT_GE(asked, 2);
    }

    TEST(A2FGenerate, progressReachesOneAndNeverGoesBackwards)
    {
        const GeneratorModel model = syntheticV3();
        StubV3 net(model);
        std::vector<double> seen;
        GeneratedWeights out;
        std::string error;
        ASSERT_TRUE(generateWeights(model, net, ramp(40000), {}, [&](double f, std::string_view) {
            seen.push_back(f);
            return true;
        }, out, error)) << error;
        ASSERT_FALSE(seen.empty());
        for (std::size_t i = 1; i < seen.size(); ++i)
            EXPECT_GE(seen[i], seen[i - 1]);
        EXPECT_LE(seen.back(), 1.0);
        EXPECT_GT(seen.back(), 0.9);
    }

    TEST(A2FGenerate, aNetworkFailureIsReportedWithItsText)
    {
        const GeneratorModel model = syntheticV3();
        StubV3 net(model);
        net.mFailAt = 1;
        GeneratedWeights out;
        std::string error;
        EXPECT_FALSE(generateWeights(model, net, ramp(40000), {}, {}, out, error));
        EXPECT_NE(error.find("boom"), std::string::npos) << error;
    }

    TEST(A2FGenerate, noAudioAndAWrongSizedMeshAreRefused)
    {
        GeneratorModel model = syntheticV3();
        StubV3 net(model);
        GeneratedWeights out;
        std::string error;
        EXPECT_FALSE(generateWeights(model, net, {}, {}, {}, out, error));
        EXPECT_FALSE(error.empty());
        model.mPoses.mVertexCount = 1000; // the v3.0 network writes 24002 vertices
        EXPECT_FALSE(generateWeights(model, net, ramp(1000), {}, {}, out, error));
        EXPECT_NE(error.find("vertices"), std::string::npos) << error;
    }

    // ---- v2.3 -------------------------------------------------------------------------------------------------------

    GeneratorModel syntheticV23()
    {
        GeneratorModel m = syntheticV3();
        m.mVariant = A2FVariant::V23;
        m.mPoses.mVertexCount = 61520;
        const std::size_t row = m.mPoses.rowLength();
        m.mV23MeanMinusNeutral.assign(row, 0.f);
        m.mV23Basis.assign(A2FDims::sV23Coefficients * row, 0.f);
        for (std::size_t v = 0; v < 4; ++v)
            m.mV23Basis[0 * row + v * 3 + 2] = 1.f; // coefficient 0 = pose 0's mesh
        for (std::size_t v = 4; v < 8; ++v)
            m.mV23Basis[1 * row + v * 3 + 2] = 2.f; // coefficient 1 = pose 1's mesh
        m.mV23Emotion.assign(A2FDims::sV23EmotionFloats, 0.f);
        m.mV23Emotion[12] = 0.25f;
        return m;
    }

    class StubV23 final : public A2FBackend
    {
    public:
        bool runV3(const float*, const float*, const float*, const float*, const float*, float*, float*,
            std::string& error) override
        {
            error = "not a v3 stub";
            return false;
        }

        bool runV23(const float* windows, std::size_t count, const float* emotion, float* results,
            std::string& error) override
        {
            if (count == 0 || count > A2FDims::sV23Batch)
            {
                error = "bad batch";
                return false;
            }
            mBatchSizes.push_back(count);
            mEmotion.assign(emotion, emotion + A2FDims::sV23EmotionFloats);
            for (std::size_t n = 0; n < count; ++n)
            {
                mWindows.emplace_back(windows + n * A2FDims::sV23Window, windows + (n + 1) * A2FDims::sV23Window);
                float* r = results + n * A2FDims::sV23Outputs;
                std::memset(r, 0, sizeof(float) * A2FDims::sV23Outputs);
                r[0] = static_cast<float>(mWindows.size() - 1) / 100.f; // global frame index / 100
                r[1] = 0.4f;
                r[290] = 7.f; // jaw, eyes: ignored by the solve
            }
            return true;
        }

        std::vector<std::size_t> mBatchSizes;
        std::vector<float> mEmotion;
        std::vector<std::vector<float>> mWindows;
    };

    TEST(A2FGenerate, v23RunsOneWindowPerFrameInBatchesOfThirtyAtThirtyFramesPerSecond)
    {
        const GeneratorModel model = syntheticV23();
        StubV23 net;
        const auto audio = ramp(20000);
        GeneratedWeights out;
        std::string error;
        ASSERT_TRUE(generateWeights(model, net, audio, {}, {}, out, error)) << error;
        EXPECT_EQ(out.mFrameRate, 30u);
        EXPECT_EQ(out.frameCount(), 38u);
        EXPECT_EQ(net.mBatchSizes, (std::vector<std::size_t>{ 30, 8 }));
        EXPECT_EQ(net.mEmotion, model.mV23Emotion);
        for (std::size_t n = 0; n < out.frameCount(); ++n)
        {
            EXPECT_NEAR(out.at(n, 0), static_cast<float>(n) / 100.f, 0.01f) << n;
            EXPECT_NEAR(out.at(n, 1), 0.4f, 0.01f) << n;
        }
        const auto starts = planV23Windows(audio.size());
        for (std::size_t i = 0; i < starts.size(); ++i)
        {
            std::vector<float> expect(A2FDims::sV23Window);
            copyWindow(audio, starts[i], expect.size(), expect.data());
            ASSERT_EQ(net.mWindows[i], expect) << i;
        }
    }

    TEST(A2FGenerate, v23NeedsItsSkinModel)
    {
        GeneratorModel model = syntheticV23();
        model.mV23Basis.clear();
        StubV23 net;
        GeneratedWeights out;
        std::string error;
        EXPECT_FALSE(generateWeights(model, net, ramp(1600), {}, {}, out, error));
        EXPECT_FALSE(error.empty());
    }

    TEST(A2FGenerate, v23TwoRunsGiveIdenticalWeights)
    {
        const GeneratorModel model = syntheticV23();
        GeneratedWeights a, b;
        std::string error;
        StubV23 n1, n2;
        ASSERT_TRUE(generateWeights(model, n1, ramp(9000), {}, {}, a, error)) << error;
        ASSERT_TRUE(generateWeights(model, n2, ramp(9000), {}, {}, b, error)) << error;
        EXPECT_EQ(a.mWeights, b.mWeights);
    }

    // ---- sampling ----------------------------------------------------------------------------------------------------

    TEST(A2FGenerate, sampleWeightIsLinearBetweenFramesAndHeldAtTheEnds)
    {
        GeneratedWeights w;
        w.mNames = { "a" };
        w.mFrameRate = 2;
        w.mWeights = { 0.f, 1.f, 0.5f };
        EXPECT_FLOAT_EQ(sampleWeight(w, 0, 0.0), 0.f);
        EXPECT_FLOAT_EQ(sampleWeight(w, 0, 0.25), 0.5f);
        EXPECT_FLOAT_EQ(sampleWeight(w, 0, 0.5), 1.f);
        EXPECT_FLOAT_EQ(sampleWeight(w, 0, 0.75), 0.75f);
        EXPECT_FLOAT_EQ(sampleWeight(w, 0, 5.0), 0.5f);
        EXPECT_FLOAT_EQ(sampleWeight(w, 0, -1.0), 0.f);
        EXPECT_FLOAT_EQ(sampleWeight(GeneratedWeights{}, 0, 1.0), 0.f);
    }

    // ---- loading the model files ---------------------------------------------------------------------------------------

    void writeFile(const fs::path& p, const std::string& bytes)
    {
        std::ofstream(p, std::ios::binary).write(bytes.data(), static_cast<std::streamsize>(bytes.size()));
    }

    const char* kBsConfig = "{\"blendshape_params\": {\"strengthL2regularization\": 0.5, "
                            "\"strengthTemporalSmoothing\": 0.3, \"strengthL1regularization\": 0.5, "
                            "\"strengthSymmetry\": 100.0, \"numPoses\": 2, \"bsSolveActivePoses\": [1, 1], "
                            "\"bsSolveSymmetryPoses\": [-1, -1]}}";

    std::string bsNpz(std::size_t vertices)
    {
        std::vector<float> a(vertices * 3, 0.f), b(vertices * 3, 0.f);
        a[2] = 1.f;
        b[3] = 1.f;
        return buildNpz({ { "neutral", { vertices, 3 }, std::vector<float>(vertices * 3, 0.5f) },
                            { "jawOpen", { vertices, 3 }, a }, { "mouthClose", { vertices, 3 }, b } },
            { { "frontalMask", { 2 }, { 0, 1 } } }, { { "poseNames", { "neutral", "jawOpen", "mouthClose" } } });
    }

    TEST(A2FLoadModel, v30ReadsTheMarkFilesAndLowerCasesThePoseNames)
    {
        const fs::path dir = fs::temp_directory_path() / "oe_a2f_gen_v30";
        fs::remove_all(dir);
        fs::create_directories(dir);
        writeFile(dir / "bs_skin_Mark.npz", bsNpz(6));
        writeFile(dir / "bs_skin_config_Mark.json", kBsConfig);
        GeneratorModel m;
        std::string error;
        ASSERT_TRUE(loadGeneratorModel(dir, A2FVariant::V30, m, error)) << error;
        EXPECT_EQ(m.mVariant, A2FVariant::V30);
        EXPECT_EQ(m.mPoses.mNames, (std::vector<std::string>{ "jawopen", "mouthclose" }));
        EXPECT_EQ(m.mIdentity, (std::array<float, 3>{ 0.f, 0.f, 1.f }));
        EXPECT_EQ(m.mPoses.mMask, (std::vector<std::uint32_t>{ 0, 1 }));
    }

    TEST(A2FLoadModel, v23ReadsTheSkinModelAndTheNeutralImplicitEmotionRow)
    {
        const fs::path dir = fs::temp_directory_path() / "oe_a2f_gen_v23";
        fs::remove_all(dir);
        fs::create_directories(dir);
        const std::size_t V = 6;
        writeFile(dir / "bs_skin.npz", bsNpz(V));
        writeFile(dir / "bs_skin_config.json", kBsConfig);
        std::vector<float> mean(V * 3, 0.75f), basis(A2FDims::sV23Coefficients * V * 3, 0.f);
        basis[(5 * V + 1) * 3 + 2] = 3.f; // coefficient 5, vertex 1, z
        writeFile(dir / "model_data.npz",
            buildNpz({ { "shapes_mean_skin", { V, 3 }, mean },
                         { "shapes_matrix_skin", { A2FDims::sV23Coefficients, V, 3 }, basis } },
                {}, {}));
        std::vector<float> db(8 * 16);
        for (std::size_t i = 0; i < db.size(); ++i)
            db[i] = static_cast<float>(i);
        writeFile(dir / "implicit_emo_db.npz",
            buildNpz({ { "emo_db", { 8, 16 }, db } }, { { "emo_spec_start", { 3 }, { 0, 2, 5 } }, { "emo_spec_size", { 3 }, { 2, 3, 3 } } },
                { { "emo_spec_names", { "a_happy", "b_neutral", "c_neutral" } } }));
        GeneratorModel m;
        std::string error;
        ASSERT_TRUE(loadGeneratorModel(dir, A2FVariant::V23, m, error)) << error;
        ASSERT_EQ(m.mV23Emotion.size(), 26u);
        for (std::size_t i = 0; i < 10; ++i)
            EXPECT_EQ(m.mV23Emotion[i], 0.f);
        for (std::size_t i = 0; i < 16; ++i)
            EXPECT_EQ(m.mV23Emotion[10 + i], static_cast<float>(2 * 16 + i)); // row emo_spec_start[1] = 2
        // masked mean - neutral: vertices 0 and 1, 0.75 - 0.5.
        ASSERT_EQ(m.mV23MeanMinusNeutral.size(), 6u);
        for (float v : m.mV23MeanMinusNeutral)
            EXPECT_FLOAT_EQ(v, 0.25f);
        ASSERT_EQ(m.mV23Basis.size(), A2FDims::sV23Coefficients * 6);
        EXPECT_EQ(m.mV23Basis[5 * 6 + 1 * 3 + 2], 3.f); // coefficient 5, masked vertex 1 (= mesh vertex 1), z
        EXPECT_EQ(m.mV23Basis[0], 0.f);
    }

    TEST(A2FLoadModel, v23RefusesASkinModelOfAnotherSize)
    {
        const fs::path dir = fs::temp_directory_path() / "oe_a2f_gen_v23bad";
        fs::remove_all(dir);
        fs::create_directories(dir);
        writeFile(dir / "bs_skin.npz", bsNpz(6));
        writeFile(dir / "bs_skin_config.json", kBsConfig);
        writeFile(dir / "model_data.npz",
            buildNpz({ { "shapes_mean_skin", { 7, 3 }, std::vector<float>(21, 0.f) },
                         { "shapes_matrix_skin", { 10, 7, 3 }, std::vector<float>(210, 0.f) } },
                {}, {}));
        writeFile(dir / "implicit_emo_db.npz",
            buildNpz({ { "emo_db", { 1, 16 }, std::vector<float>(16, 0.f) } }, { { "emo_spec_start", { 1 }, { 0 } } },
                { { "emo_spec_names", { "neutral" } } }));
        GeneratorModel m;
        std::string error;
        EXPECT_FALSE(loadGeneratorModel(dir, A2FVariant::V23, m, error));
        EXPECT_FALSE(error.empty());
    }

    TEST(A2FLoadModel, aMissingFolderIsAnErrorNotACrash)
    {
        GeneratorModel m;
        std::string error;
        EXPECT_FALSE(loadGeneratorModel(fs::temp_directory_path() / "oe_a2f_gen_none", A2FVariant::V30, m, error));
        EXPECT_FALSE(error.empty());
    }

    // ---- the real files (opt in) -----------------------------------------------------------------------------------------

    TEST(A2FLoadModel, realDownloadsLoadWhenOpenedenA2fModelsPointsAtThem)
    {
        const char* root = std::getenv("OPENEDEN_A2F_MODELS");
        if (root == nullptr)
            GTEST_SKIP() << "set OPENEDEN_A2F_MODELS to a folder holding v3.0/ and v2.3-mark/";
        GeneratorModel v3, v23;
        std::string error;
        ASSERT_TRUE(loadGeneratorModel(fs::path(root) / "v3.0", A2FVariant::V30, v3, error)) << error;
        EXPECT_EQ(v3.mPoses.mNames.size(), 52u);
        EXPECT_EQ(v3.mPoses.mVertexCount, 24002u);
        EXPECT_EQ(v3.mPoses.mMask.size(), 4262u);
        EXPECT_NE(std::find(v3.mPoses.mNames.begin(), v3.mPoses.mNames.end(), "jawopen"), v3.mPoses.mNames.end());
        ASSERT_TRUE(loadGeneratorModel(fs::path(root) / "v2.3-mark", A2FVariant::V23, v23, error)) << error;
        EXPECT_EQ(v23.mPoses.mNames.size(), 52u);
        EXPECT_EQ(v23.mPoses.mVertexCount, 61520u);
        EXPECT_EQ(v23.mPoses.mMask.size(), 10085u);
        EXPECT_EQ(v23.mV23Basis.size(), 272u * 10085u * 3u);
        EXPECT_EQ(v23.mV23Emotion.size(), 26u);
    }
}
