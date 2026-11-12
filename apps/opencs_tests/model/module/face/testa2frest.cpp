// Task 0147 slice 3: the rest offset (toddhow 2026-11-12: "subtract"). The pure parts run with a STUB network that
// returns a constant mesh; the measured tables are checked for shape here and re-measured with the real models in the
// gated test of testa2fort.cpp.
#include "editor_modules/face_editor/a2f_generate.hpp"
#include "editor_modules/face_editor/a2f_rest.hpp"

#include <gtest/gtest.h>

#include <cmath>
#include <cstring>

namespace
{
    using namespace face_editor;

    // The synthetic model of testa2fgenerate.cpp: two poses with disjoint support over eight masked vertices.
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

    /// Every frame is the mesh of weights (w0, w1); remembers whether any window was not silent.
    class ConstantStub final : public A2FBackend
    {
    public:
        ConstantStub(const GeneratorModel& m, float w0, float w1)
            : mModel(m)
            , mW0(w0)
            , mW1(w1)
        {
        }
        bool runV3(const float* window, const float*, const float*, const float* latentsIn, const float*,
            float* prediction, float* latentsOut, std::string&) override
        {
            for (std::size_t i = 0; i < A2FDims::sV3Window; ++i)
                if (window[i] != 0.f)
                    mHeardSomething = true;
            ++mWindows;
            std::memset(prediction, 0, sizeof(float) * A2FDims::sV3Frames * A2FDims::sV3Outputs);
            const BlendshapeSet& s = mModel.mPoses;
            for (std::size_t f = 0; f < A2FDims::sV3Frames; ++f)
            {
                float* skin = prediction + f * A2FDims::sV3Outputs;
                for (std::size_t v = 0; v < 4; ++v)
                    skin[s.mMask[v] * 3 + 2] = mW0;
                for (std::size_t v = 4; v < 8; ++v)
                    skin[s.mMask[v] * 3 + 2] = 2.f * mW1;
            }
            std::memcpy(latentsOut, latentsIn, sizeof(float) * A2FDims::sV3LatentFloats);
            return true;
        }
        bool runV23(const float*, std::size_t, const float*, float*, std::string& error) override
        {
            error = "not used";
            return false;
        }
        GeneratorModel mModel;
        float mW0, mW1;
        bool mHeardSomething = false;
        int mWindows = 0;
    };

    GeneratedWeights weights3(std::size_t frames)
    {
        GeneratedWeights w;
        w.mNames = { "a", "b", "c" };
        w.mFrameRate = 30;
        w.mWeights.assign(frames * 3, 0.f);
        for (std::size_t f = 0; f < frames; ++f)
        {
            w.mWeights[f * 3 + 0] = 0.30f;
            w.mWeights[f * 3 + 1] = 0.05f;
            w.mWeights[f * 3 + 2] = 0.90f;
        }
        return w;
    }

    TEST(A2FRest, subtractRemovesTheOffsetAndClampsAtZero)
    {
        GeneratedWeights w = weights3(4);
        subtractRest(w, { 0.10f, 0.10f, 0.f });
        for (std::size_t f = 0; f < 4; ++f)
        {
            EXPECT_NEAR(w.at(f, 0), 0.20f, 1e-6);
            EXPECT_EQ(w.at(f, 1), 0.f);
            EXPECT_NEAR(w.at(f, 2), 0.90f, 1e-6);
        }
    }

    TEST(A2FRest, aNegativeOrZeroOffsetChangesNothingAndNeverRaisesAWeightPastItsOwn)
    {
        GeneratedWeights w = weights3(2);
        subtractRest(w, { 0.f, 0.f, 0.f });
        EXPECT_NEAR(w.at(0, 0), 0.30f, 1e-7);
        subtractRest(w, { -0.5f, 0.f, 0.f }); // a negative offset is not a boost
        EXPECT_NEAR(w.at(0, 0), 0.30f, 1e-7);
    }

    TEST(A2FRest, anOffsetListOfTheWrongSizeIsIgnored)
    {
        GeneratedWeights w = weights3(2);
        subtractRest(w, { 0.1f, 0.1f });
        EXPECT_NEAR(w.at(1, 1), 0.05f, 1e-7);
        subtractRest(w, {});
        EXPECT_NEAR(w.at(1, 2), 0.90f, 1e-7);
    }

    TEST(A2FRest, generateWeightsSubtractsTheOptionsOffsets)
    {
        const GeneratorModel model = syntheticV3();
        std::vector<float> audio(16000 * 2, 0.1f);
        GenerateOptions plain;
        ConstantStub a(model, 0.20f, 0.50f);
        GeneratedWeights raw;
        std::string error;
        ASSERT_TRUE(generateWeights(model, a, audio, plain, {}, raw, error)) << error;
        EXPECT_NEAR(raw.at(10, 0), 0.20f, 0.01);
        EXPECT_NEAR(raw.at(10, 1), 0.50f, 0.01);

        GenerateOptions withRest;
        withRest.mRestOffsets = { 0.10f, 0.60f };
        ConstantStub b(model, 0.20f, 0.50f);
        GeneratedWeights cut;
        ASSERT_TRUE(generateWeights(model, b, audio, withRest, {}, cut, error)) << error;
        ASSERT_EQ(cut.frameCount(), raw.frameCount());
        for (std::size_t f = 0; f < cut.frameCount(); ++f)
        {
            EXPECT_NEAR(cut.at(f, 0), std::max(0.f, raw.at(f, 0) - 0.10f), 1e-6);
            EXPECT_EQ(cut.at(f, 1), 0.f);
        }
    }

    TEST(A2FRest, measureRestFeedsSilenceAndReturnsTheMeanPerPose)
    {
        const GeneratorModel model = syntheticV3();
        ConstantStub stub(model, 0.20f, 0.50f);
        RestTable table;
        std::string error;
        ASSERT_TRUE(measureRest(model, stub, 3.0, GenerateOptions{}, table, error)) << error;
        EXPECT_FALSE(stub.mHeardSomething);
        EXPECT_EQ(stub.mWindows, 3);
        ASSERT_EQ(table.size(), 2u);
        EXPECT_EQ(table[0].first, "jawopen");
        EXPECT_EQ(table[1].first, "mouthclose");
        EXPECT_NEAR(table[0].second, 0.20f, 0.01);
        EXPECT_NEAR(table[1].second, 0.50f, 0.01);
    }

    TEST(A2FRest, measureRestIgnoresTheOptionsOwnOffsetsAndIsRepeatable)
    {
        const GeneratorModel model = syntheticV3();
        GenerateOptions options;
        options.mRestOffsets = { 0.15f, 0.15f };
        ConstantStub s1(model, 0.20f, 0.50f), s2(model, 0.20f, 0.50f);
        RestTable a, b;
        std::string error;
        ASSERT_TRUE(measureRest(model, s1, 2.0, options, a, error)) << error;
        ASSERT_TRUE(measureRest(model, s2, 2.0, options, b, error)) << error;
        ASSERT_EQ(a.size(), 2u);
        EXPECT_NEAR(a[0].second, 0.20f, 0.01) << "not reduced by the options' own offsets";
        EXPECT_EQ(a, b);
    }

    TEST(A2FRest, measureRestRefusesBadLengthsAndFailingNetworks)
    {
        const GeneratorModel model = syntheticV3();
        ConstantStub stub(model, 0.2f, 0.5f);
        RestTable t;
        std::string error;
        EXPECT_FALSE(measureRest(model, stub, 0.0, GenerateOptions{}, t, error));
        EXPECT_FALSE(measureRest(model, stub, -1.0, GenerateOptions{}, t, error));
        EXPECT_FALSE(measureRest(model, stub, std::nan(""), GenerateOptions{}, t, error));
        EXPECT_FALSE(measureRest(model, stub, 1e9, GenerateOptions{}, t, error));
        EXPECT_FALSE(error.empty());
    }

    TEST(A2FRest, offsetsAreLookedUpByNameInTheCallersOrder)
    {
        const RestTable& t = restTable(A2FVariant::V23);
        ASSERT_FALSE(t.empty());
        const std::vector<float> o = restOffsetsFor(A2FVariant::V23, { "nosuchpose", t[1].first, t[0].first });
        ASSERT_EQ(o.size(), 3u);
        EXPECT_EQ(o[0], 0.f);
        EXPECT_EQ(o[1], t[1].second);
        EXPECT_EQ(o[2], t[0].second);
    }

    TEST(A2FRest, theMeasuredTablesHaveTheShapeTheNetworksAnd52PosesImply)
    {
        for (A2FVariant v : { A2FVariant::V30, A2FVariant::V23 })
        {
            const RestTable& t = restTable(v);
            EXPECT_EQ(t.size(), 52u);
            bool jaw = false;
            for (std::size_t i = 0; i < t.size(); ++i)
            {
                EXPECT_GE(t[i].second, 0.f) << t[i].first;
                EXPECT_LE(t[i].second, 0.5f) << t[i].first; // a rest offset, not a pose
                for (std::size_t j = 0; j < i; ++j)
                    EXPECT_NE(t[i].first, t[j].first);
                if (t[i].first == "jawopen")
                {
                    jaw = true;
                    // task 0071 measured about 0.09 to 0.1 on silence in both networks.
                    EXPECT_GT(t[i].second, 0.03f);
                    EXPECT_LT(t[i].second, 0.2f);
                }
            }
            EXPECT_TRUE(jaw);
        }
    }

    TEST(A2FRest, theSilenceLengthIsRecorded)
    {
        EXPECT_EQ(kRestSilenceSeconds, 10.0);
    }
}
