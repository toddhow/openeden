// Task 0146 (slice 2): the head morph writes base + weighted deltas into the face shape's vertex array and uploads only
// when the weights change. Plain VSG arrays, no device. Written before the implementation.
#include <apps/openmw/mwrender/skyrimlip.hpp>

#include <gtest/gtest.h>

#include <memory>

namespace
{
    std::shared_ptr<const TriMorph::File> twoVertexFile()
    {
        auto file = std::make_shared<TriMorph::File>();
        file->mVertices = 2;
        file->mMorphsInFile = 2;
        file->mMorphs.push_back({ "Aah", 0.5f, { 2, 0, 0, 0, -4, 0 } });
        file->mMorphs.push_back({ "BigAah", 0.25f, { 0, 8, 0, 0, 0, 12 } });
        return file;
    }

    vsg::ref_ptr<vsg::vec3Array> basePositions()
    {
        auto a = vsg::vec3Array::create(2);
        (*a)[0] = vsg::vec3(1.f, 1.f, 1.f);
        (*a)[1] = vsg::vec3(2.f, 2.f, 2.f);
        return a;
    }

    TEST(SkyrimLip, aMismatchedFileOrMissingArrayGivesNoMorph)
    {
        auto wrong = std::make_shared<TriMorph::File>(*twoVertexFile());
        wrong->mVertices = 3;
        EXPECT_FALSE(MWRender::SkyrimLip::HeadMorph::make(wrong, basePositions()));
        EXPECT_FALSE(MWRender::SkyrimLip::HeadMorph::make(twoVertexFile(), {}));
        EXPECT_FALSE(MWRender::SkyrimLip::HeadMorph::make({}, basePositions()));
        auto empty = std::make_shared<TriMorph::File>();
        empty->mVertices = 2;
        EXPECT_FALSE(MWRender::SkyrimLip::HeadMorph::make(empty, basePositions()));
    }

    TEST(SkyrimLip, theArrayBecomesDynamicData)
    {
        auto positions = basePositions();
        ASSERT_TRUE(MWRender::SkyrimLip::HeadMorph::make(twoVertexFile(), positions));
        EXPECT_EQ(positions->properties.dataVariance, vsg::DYNAMIC_DATA);
    }

    TEST(SkyrimLip, weightsMoveTheVerticesFromTheBase)
    {
        auto positions = basePositions();
        auto morph = MWRender::SkyrimLip::HeadMorph::make(twoVertexFile(), positions);
        ASSERT_TRUE(morph);
        EXPECT_TRUE(morph->setWeights({ 1.f, 0.5f }));
        // Aah x1: vertex 0 x += 0.5*2 = 1; vertex 1 y += 0.5*-4 = -2. BigAah x0.5: v0 y += 0.5*0.25*8 = 1; v1 z += 0.5*0.25*12 = 1.5
        EXPECT_FLOAT_EQ((*positions)[0].x, 2.f);
        EXPECT_FLOAT_EQ((*positions)[0].y, 2.f);
        EXPECT_FLOAT_EQ((*positions)[1].y, 0.f);
        EXPECT_FLOAT_EQ((*positions)[1].z, 3.5f);
    }

    TEST(SkyrimLip, aSecondCallWithTheSameWeightsUploadsNothing)
    {
        auto positions = basePositions();
        auto morph = MWRender::SkyrimLip::HeadMorph::make(twoVertexFile(), positions);
        ASSERT_TRUE(morph);
        EXPECT_FALSE(morph->setWeights({})); // closed already: the base is in the array
        EXPECT_EQ(morph->uploads(), 0u);
        EXPECT_TRUE(morph->setWeights({ 0.5f, 0.f }));
        EXPECT_FALSE(morph->setWeights({ 0.5f, 0.f }));
        EXPECT_EQ(morph->uploads(), 1u);
        EXPECT_TRUE(morph->setWeights({ 0.5f, 0.0625f }));
        EXPECT_EQ(morph->uploads(), 2u);
    }

    TEST(SkyrimLip, closingTheMouthRestoresTheBaseExactly)
    {
        auto positions = basePositions();
        auto morph = MWRender::SkyrimLip::HeadMorph::make(twoVertexFile(), positions);
        ASSERT_TRUE(morph);
        morph->setWeights({ 0.9f, 0.7f });
        EXPECT_TRUE(morph->setWeights({}));
        EXPECT_EQ((*positions)[0], vsg::vec3(1.f, 1.f, 1.f));
        EXPECT_EQ((*positions)[1], vsg::vec3(2.f, 2.f, 2.f));
        EXPECT_EQ(morph->weights(), TriMorph::MouthWeights{});
    }

    TEST(SkyrimLip, aFileWithOnlyOneOfTheMorphsStillMovesWithThat)
    {
        auto file = std::make_shared<TriMorph::File>(*twoVertexFile());
        file->mMorphs.erase(file->mMorphs.begin()); // no Aah
        auto positions = basePositions();
        auto morph = MWRender::SkyrimLip::HeadMorph::make(file, positions);
        ASSERT_TRUE(morph);
        morph->setWeights({ 1.f, 1.f });
        EXPECT_FLOAT_EQ((*positions)[0].x, 1.f); // the missing Aah did nothing
        EXPECT_FLOAT_EQ((*positions)[0].y, 1.f + 0.25f * 8.f);
    }

    TEST(SkyrimLip, theHeadFilesAreTheRaceHeads)
    {
        EXPECT_EQ(MWRender::SkyrimLip::sHeadTriFiles[0], "meshes/actors/character/character assets/femalehead.tri");
        for (std::string_view path : MWRender::SkyrimLip::sHeadTriFiles)
            EXPECT_EQ(path.substr(path.size() - 4), ".tri");
    }
}
