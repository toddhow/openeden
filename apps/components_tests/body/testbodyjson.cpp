// Task 0063 (AdvancedBodies B1), stage a: the openeden.body v1 reader and canonical writer. Written from the task
// spec and the contract headers only.

#include "bodyfixtures.hpp"

#include <components/body/bodyjson.hpp>

#include <gtest/gtest.h>

#include <cmath>
#include <cstdint>
#include <limits>
#include <regex>
#include <string>
#include <vector>

namespace
{
    using namespace BodyTest;

    const std::string sMinFile = "humanoid_min.body.json";

    Body::BodyLoadResult load(const std::string& text)
    {
        return Body::readBodyPackage(text, "test.body.json");
    }

    std::string minText()
    {
        return readDataFile(sMinFile);
    }

    /// The error must carry the file name, a 1-based line (when line != 0) and a pointer that starts with `pointer`.
    void expectLocatedError(
        const Body::BodyLoadResult& result, const std::string& pointerPrefix, std::size_t line, const std::string& what)
    {
        EXPECT_FALSE(result.mValue.has_value()) << what;
        if (result.mValue.has_value())
        {
            return;
        }
        EXPECT_EQ(result.mError.file, "test.body.json") << what;
        EXPECT_FALSE(result.mError.message.empty()) << what;
        EXPECT_GE(result.mError.line, 1u) << what;
        EXPECT_EQ(result.mError.field.compare(0, pointerPrefix.size(), pointerPrefix), 0)
            << what << ": pointer was '" << result.mError.field << "', expected prefix '" << pointerPrefix << "'";
        if (line != 0)
        {
            EXPECT_EQ(result.mError.line, line) << what << ": " << result.mError.toString();
        }
    }

    void expectExactError(
        const Body::BodyLoadResult& result, const std::string& pointer, std::size_t line, const std::string& what)
    {
        expectLocatedError(result, pointer, line, what);
        if (!result.mValue.has_value())
        {
            EXPECT_EQ(result.mError.field, pointer) << what;
        }
    }

    /// Replaces `from` by `to` in the minimal file's text and loads it; fails the test when `from` is absent.
    Body::BodyLoadResult loadEdited(const std::string& from, const std::string& to, std::string* editedOut = nullptr)
    {
        bool ok = true;
        const std::string edited = replaceFirst(minText(), from, to, &ok);
        EXPECT_TRUE(ok) << "needle not found: " << from;
        if (editedOut != nullptr)
        {
            *editedOut = edited;
        }
        return load(edited);
    }
}

TEST(BodyJson, MinimalFileIsCanonical)
{
    const std::string text = minText();
    ASSERT_FALSE(text.empty());
    const Body::BodyLoadResult result = load(text);
    ASSERT_TRUE(result.mValue.has_value()) << result.mError.toString();
    EXPECT_EQ(Body::writeBodyPackage(*result.mValue), text);
}

TEST(BodyJson, MinimalFileContent)
{
    const Body::BodyLoadResult result = load(minText());
    ASSERT_TRUE(result.mValue.has_value()) << result.mError.toString();
    const Body::BodyPackage& package = *result.mValue;
    EXPECT_EQ(package.mIdentity.mId, "humanoid_min");
    EXPECT_EQ(package.mIdentity.mName, "Minimal humanoid");
    EXPECT_EQ(package.mIdentity.mVersion, "1.0.0");
    EXPECT_EQ(package.mIdentity.mContractVersion.mMajor, 1);
    EXPECT_EQ(package.mIdentity.mContractVersion.mMinor, 0);
    EXPECT_EQ(package.mIdentity.mAgeClass, Body::AgeClass::Unspecified);
    ASSERT_TRUE(package.mBinding.mSkeletonProfile.has_value());
    EXPECT_FALSE(package.mBinding.mSkeletonPath.has_value());
    EXPECT_EQ(package.mBinding.mSkeletonProfile->bones.size(), 2u);
    EXPECT_EQ(package.mBinding.mRootBone, "Bip Root");
    ASSERT_EQ(package.mBinding.mUnmappedBones.size(), 1u);
    ASSERT_TRUE(package.mBinding.mRestSkeleton.has_value());
    EXPECT_EQ(package.mBinding.mRestSkeleton->size(), 3u);
    ASSERT_EQ(package.mMeshes.size(), 1u);
    const Body::MeshEntry& mesh = package.mMeshes[0];
    EXPECT_EQ(mesh.mId, "body");
    EXPECT_EQ(mesh.mFormat, Body::MeshFormat::Gltf);
    EXPECT_EQ(mesh.mRole, Body::MeshRole::Skin);
    ASSERT_TRUE(mesh.mSummary.has_value());
    EXPECT_EQ(mesh.mSummary->mVertexCount, 3u);
    EXPECT_EQ(mesh.mSummary->mInfluences.size(), 4u);
    EXPECT_DOUBLE_EQ(mesh.mSummary->mInfluences[2].mWeight, 0.5);
    ASSERT_TRUE(package.mMorphSet.has_value());
    EXPECT_EQ(package.mMorphSet->mMorphs.size(), 1u);
    ASSERT_TRUE(package.mRegionMap.has_value());
    EXPECT_FALSE(package.mProxy.has_value());
}

TEST(BodyJson, SameTextReadTwiceGivesEqualResults)
{
    const std::string text = minText();
    const Body::BodyLoadResult first = load(text);
    const Body::BodyLoadResult second = load(text);
    ASSERT_TRUE(first.mValue.has_value());
    ASSERT_TRUE(second.mValue.has_value());
    EXPECT_EQ(*first.mValue, *second.mValue);

    const Body::BodyLoadResult bad1 = load("{");
    const Body::BodyLoadResult bad2 = load("{");
    EXPECT_EQ(bad1.mError.toString(), bad2.mError.toString());
}

TEST(BodyJson, FixturesRoundTripByteForByte)
{
    const std::vector<std::pair<std::string, Body::BodyPackage>> fixtures = {
        { "humanoid", humanoid() },
        { "tailed_quadruped", tailedQuadruped() },
        { "kitchen_sink", kitchenSink() },
    };
    for (const auto& [name, package] : fixtures)
    {
        const std::string text = Body::writeBodyPackage(package);
        ASSERT_FALSE(text.empty()) << name;
        EXPECT_EQ(text.back(), '\n') << name;
        EXPECT_EQ(text.find('\r'), std::string::npos) << name;
        const Body::BodyLoadResult result = Body::readBodyPackage(text, name);
        ASSERT_TRUE(result.mValue.has_value()) << name << ": " << result.mError.toString();
        EXPECT_EQ(*result.mValue, package) << name;
        EXPECT_EQ(Body::writeBodyPackage(*result.mValue), text) << name;
    }
}

TEST(BodyJson, CanonicalLayoutRules)
{
    const std::string text = Body::writeBodyPackage(humanoid());
    // Two-space indent, never tabs; flat arrays of scalars on one line with ", " between elements.
    EXPECT_EQ(text.find('\t'), std::string::npos);
    EXPECT_NE(text.find("\n  \"identity\": {"), std::string::npos);
    EXPECT_NE(text.find("\"contractVersion\": {\"major\": 1, \"minor\": 0}"), std::string::npos);
    EXPECT_NE(text.find("\"range\": [-1, 1]"), std::string::npos);
    // Keys in the documented order.
    const std::size_t format = text.find("\"format\"");
    const std::size_t version = text.find("\"version\"");
    const std::size_t identity = text.find("\"identity\"");
    const std::size_t binding = text.find("\"binding\"");
    const std::size_t meshes = text.find("\"meshes\"");
    const std::size_t morphSet = text.find("\"morphSet\"");
    const std::size_t proxy = text.find("\"proxy\"");
    const std::size_t regionMap = text.find("\"regionMap\"");
    EXPECT_LT(format, version);
    EXPECT_LT(version, identity);
    EXPECT_LT(identity, binding);
    EXPECT_LT(binding, meshes);
    EXPECT_LT(meshes, morphSet);
    EXPECT_LT(morphSet, proxy);
    EXPECT_LT(proxy, regionMap);
    // Absent optional parts are omitted.
    EXPECT_EQ(text.find("\"physicsProfile\""), std::string::npos);
    EXPECT_EQ(text.find("\"softTissue\""), std::string::npos);
    EXPECT_EQ(text.find("\"provenance\""), std::string::npos);
    EXPECT_EQ(text.find("\n  \"targets\""), std::string::npos) << "top-level targets key";
}

TEST(BodyJson, DefaultsOnReadAreWrittenExplicitly)
{
    // ageClass and unmappedBones are optional on read and always written.
    std::string text = minText();
    bool ok = true;
    text = removeLineWith(text, "\"ageClass\"", &ok);
    ASSERT_TRUE(ok);
    // The line before ageClass ended with a comma that now dangles: fix it.
    text = replaceFirst(text, "\"contractVersion\": {\"major\": 1, \"minor\": 0},",
        "\"contractVersion\": {\"major\": 1, \"minor\": 0}", &ok);
    ASSERT_TRUE(ok);
    const Body::BodyLoadResult result = load(text);
    ASSERT_TRUE(result.mValue.has_value()) << result.mError.toString();
    EXPECT_EQ(result.mValue->mIdentity.mAgeClass, Body::AgeClass::Unspecified);
    EXPECT_EQ(Body::writeBodyPackage(*result.mValue), minText());
}

TEST(BodyJson, OriginMissingIsWrittenWithEmptyArrays)
{
    Body::BodyPackage package = humanoid();
    package.mProxy->mOrigin = Body::Origin::Missing;
    package.mProxy->mCapsules.clear();
    const std::string text = Body::writeBodyPackage(package);
    EXPECT_NE(text.find("\"origin\": \"missing\""), std::string::npos);
    const Body::BodyLoadResult result = Body::readBodyPackage(text, "x");
    ASSERT_TRUE(result.mValue.has_value()) << result.mError.toString();
    EXPECT_EQ(*result.mValue, package);
}

TEST(BodyJson, NegativeZeroIsWrittenAsZero)
{
    Body::BodyPackage package = humanoid();
    package.mMorphSet->mMorphs[0].mDefault = -0.0;
    package.mMorphSet->mMorphs[0].mTargets[0].mDeltas[0] = { -0.0, 0.5, -0.0 };
    package.mBinding.mRestSkeleton->at(0).mTranslation = { -0.0, -0.0, -0.0 };
    const std::string text = Body::writeBodyPackage(package);
    const std::regex negativeZero("-0([^.0-9eE]|$)");
    EXPECT_FALSE(std::regex_search(text, negativeZero)) << "negative zero in canonical text";
    EXPECT_NE(text.find("\"default\": 0,"), std::string::npos);
}

TEST(BodyJson, NumbersAreShortestRoundTrip)
{
    Body::BodyPackage package = humanoid();
    Body::Morph& morph = package.mMorphSet->mMorphs[0];
    morph.mRangeMin = -2;
    morph.mRangeMax = 2.5;
    morph.mDefault = 0.1;
    std::string text = Body::writeBodyPackage(package);
    EXPECT_NE(text.find("\"range\": [-2, 2.5]"), std::string::npos);
    EXPECT_NE(text.find("\"default\": 0.1,"), std::string::npos);

    morph.mDefault = 0.1 + 0.2;
    text = Body::writeBodyPackage(package);
    EXPECT_NE(text.find("\"default\": 0.30000000000000004,"), std::string::npos);

    morph.mRangeMin = -1.0 / 3.0;
    morph.mDefault = 0;
    text = Body::writeBodyPackage(package);
    const Body::BodyLoadResult result = Body::readBodyPackage(text, "x");
    ASSERT_TRUE(result.mValue.has_value()) << result.mError.toString();
    EXPECT_EQ(result.mValue->mMorphSet->mMorphs[0].mRangeMin, -1.0 / 3.0);

    // Whole numbers are written without a fraction or exponent.
    morph.mRangeMax = 1000000;
    text = Body::writeBodyPackage(package);
    EXPECT_NE(text.find("1000000"), std::string::npos);
}

TEST(BodyJson, ExtremeDoublesRoundTrip)
{
    Body::BodyPackage package = humanoid();
    Body::Morph& morph = package.mMorphSet->mMorphs[0];
    morph.mRangeMin = -std::numeric_limits<double>::max();
    morph.mRangeMax = std::numeric_limits<double>::max();
    morph.mDefault = std::numeric_limits<double>::denorm_min();
    const std::string text = Body::writeBodyPackage(package);
    const Body::BodyLoadResult result = Body::readBodyPackage(text, "x");
    ASSERT_TRUE(result.mValue.has_value()) << result.mError.toString();
    EXPECT_EQ(*result.mValue, package);
}

TEST(BodyJson, StringsWithEscapesRoundTrip)
{
    Body::BodyPackage package = humanoid();
    package.mIdentity.mName = "Quote \" backslash \\ tab \t newline \n unicode \xC3\xA9 end";
    package.mIdentity.mAuthor = "slash / and \x01 control";
    const std::string text = Body::writeBodyPackage(package);
    const Body::BodyLoadResult result = Body::readBodyPackage(text, "x");
    ASSERT_TRUE(result.mValue.has_value()) << result.mError.toString();
    EXPECT_EQ(*result.mValue, package);
    EXPECT_EQ(Body::writeBodyPackage(*result.mValue), text);
}

TEST(BodyJson, EnumNamesRoundTrip)
{
    Body::BodyPackage package = humanoid();
    const Body::AgeClass ages[] = { Body::AgeClass::Unspecified, Body::AgeClass::Minor, Body::AgeClass::Adult };
    for (const Body::AgeClass age : ages)
    {
        package.mIdentity.mAgeClass = age;
        const Body::BodyLoadResult result = Body::readBodyPackage(Body::writeBodyPackage(package), "x");
        ASSERT_TRUE(result.mValue.has_value());
        EXPECT_EQ(result.mValue->mIdentity.mAgeClass, age);
    }
    const Body::MeshFormat formats[] = { Body::MeshFormat::Nif, Body::MeshFormat::Gltf, Body::MeshFormat::Summary };
    const Body::MeshRole roles[] = { Body::MeshRole::Skin, Body::MeshRole::Head, Body::MeshRole::Hair,
        Body::MeshRole::GarmentBase, Body::MeshRole::Other };
    for (const Body::MeshFormat format : formats)
    {
        for (const Body::MeshRole role : roles)
        {
            package.mMeshes[2].mFormat = format;
            package.mMeshes[2].mRole = role;
            const Body::BodyLoadResult result = Body::readBodyPackage(Body::writeBodyPackage(package), "x");
            ASSERT_TRUE(result.mValue.has_value());
            EXPECT_EQ(result.mValue->mMeshes[2].mFormat, format);
            EXPECT_EQ(result.mValue->mMeshes[2].mRole, role);
        }
    }
    EXPECT_EQ(Body::toString(Body::AgeClass::Adult), "adult");
    EXPECT_EQ(Body::toString(Body::MeshFormat::Gltf), "gltf");
    EXPECT_EQ(Body::toString(Body::MeshRole::GarmentBase), "garmentBase");
    EXPECT_EQ(Body::toString(Body::Origin::Derived), "derived");
    EXPECT_EQ(Body::toString(Body::ShapeKind::Capsule), "capsule");
}

TEST(BodyJson, FileNameAppearsInErrors)
{
    const Body::BodyLoadResult result = Body::readBodyPackage("[]", "some/where/body.json");
    ASSERT_FALSE(result.mValue.has_value());
    EXPECT_EQ(result.mError.file, "some/where/body.json");
    EXPECT_NE(result.mError.toString().find("some/where/body.json"), std::string::npos);
}

TEST(BodyJson, WrongTypeIsLocated)
{
    const std::string text = minText();
    {
        const auto result = loadEdited("\"lod\": 0,", "\"lod\": \"0\",");
        expectExactError(result, "/meshes/0/lod", lineOf(text, "\"lod\": 0,"), "lod as string");
    }
    {
        const auto result = loadEdited("\"lod\": 0,", "\"lod\": 0.5,");
        expectExactError(result, "/meshes/0/lod", lineOf(text, "\"lod\": 0,"), "lod fractional");
    }
    {
        const auto result = loadEdited("\"name\": \"Minimal humanoid\"", "\"name\": 5");
        expectExactError(result, "/identity/name", lineOf(text, "\"name\": \"Minimal humanoid\""), "name as number");
    }
    {
        const auto result = loadEdited("\"vertexCount\": 3,", "\"vertexCount\": -3,");
        expectExactError(result, "/meshes/0/summary/vertexCount", lineOf(text, "\"vertexCount\""), "negative count");
    }
    {
        const auto result = loadEdited("\"boundsMin\": [-0.25, -0.125, 0.5],", "\"boundsMin\": [-0.25, -0.125],");
        expectLocatedError(result, "/meshes/0/summary/boundsMin", lineOf(text, "\"boundsMin\""), "short vector");
    }
    {
        const auto result = loadEdited("\"unmappedBones\": [\"Bip Root\"],", "\"unmappedBones\": \"Bip Root\",");
        expectExactError(result, "/binding/unmappedBones", lineOf(text, "\"unmappedBones\""), "array as string");
    }
    {
        const auto result = loadEdited("\"identity\": {", "\"identity\": [");
        EXPECT_FALSE(result.mValue.has_value());
    }
    {
        const auto result = load("[]");
        expectLocatedError(result, "", 1, "document is an array");
    }
    {
        const auto result = load("5");
        expectLocatedError(result, "", 1, "document is a number");
    }
    {
        const auto result = load("null");
        expectLocatedError(result, "", 1, "document is null");
    }
}

TEST(BodyJson, MissingRequiredKeyIsLocated)
{
    struct Case
    {
        const char* mNeedle;
        const char* mPointerPrefix;
    };
    const Case cases[] = {
        { "\"license\"", "/identity" },
        { "\"author\"", "/identity" },
        { "\"id\": \"humanoid_min\"", "/identity" },
        { "\"rootBone\"", "/binding" },
        { "\"path\": \"mesh/body.glb\"", "/meshes/0" },
        { "\"role\": \"skin\"", "/meshes/0" },
        { "\"lod\": 0", "/meshes/0" },
        { "\"meshFormat\"", "/meshes/0" },
        { "\"origin\": \"authored\",\n    \"morphs\"", "/morphSet" },
    };
    for (const Case& testCase : cases)
    {
        std::string text = minText();
        bool ok = true;
        if (std::string_view(testCase.mNeedle).find('\n') != std::string_view::npos)
        {
            text = replaceFirst(text, testCase.mNeedle, "\"morphs\"", &ok);
        }
        else
        {
            text = removeLineWith(text, testCase.mNeedle, &ok);
        }
        ASSERT_TRUE(ok) << testCase.mNeedle;
        // Removing a last member leaves a dangling comma on the previous line; repair it for the cases where it does.
        const std::string repaired = std::regex_replace(text, std::regex(",(\\s*[}\\]])"), "$1");
        const Body::BodyLoadResult result = load(repaired);
        expectLocatedError(result, testCase.mPointerPrefix, 0, std::string("missing ") + testCase.mNeedle);
    }
}

TEST(BodyJson, MissingTopLevelKeysAreRefused)
{
    for (const char* text : { "{\"format\": \"openeden.body\", \"version\": 1}", "{\"version\": 1}",
             "{\"format\": \"openeden.body\"}", "{\"format\": \"openeden.body\", \"version\": 1, \"x\": 1}" })
    {
        const Body::BodyLoadResult result = load(text);
        EXPECT_FALSE(result.mValue.has_value()) << text;
        EXPECT_FALSE(result.mError.message.empty()) << text;
    }
    expectLocatedError(load("{}"), "", 1, "empty object");
}

TEST(BodyJson, UnknownKeysAreRefusedAtEveryLevel)
{
    const std::string text = minText();
    struct Case
    {
        const char* mAfter;
        const char* mPointerPrefix;
    };
    const Case cases[] = {
        { "\"format\": \"openeden.body\",", "" },
        { "\"id\": \"humanoid_min\",", "/identity" },
        { "\"major\": 1", "/identity/contractVersion" },
        { "\"rootBone\": \"Bip Root\",", "/binding" },
        { "\"id\": \"body\",", "/meshes/0" },
        { "\"vertexCount\": 3,", "/meshes/0/summary" },
        { "\"id\": \"height\",", "/morphSet/morphs/0" },
        { "\"id\": \"torso\",", "/regionMap/regions/0" },
    };
    for (const Case& testCase : cases)
    {
        std::string edited;
        bool ok = true;
        if (std::string_view(testCase.mAfter) == "\"major\": 1")
        {
            edited
                = replaceFirst(text, "{\"major\": 1, \"minor\": 0}", "{\"major\": 1, \"minor\": 0, \"extra\": 1}", &ok);
        }
        else
        {
            edited = insertLineAfter(text, testCase.mAfter, "\"extra\": 1,", &ok);
        }
        ASSERT_TRUE(ok) << testCase.mAfter;
        const Body::BodyLoadResult result = load(edited);
        expectLocatedError(result, testCase.mPointerPrefix, 0, std::string("unknown key after ") + testCase.mAfter);
        if (!result.mValue.has_value())
        {
            EXPECT_NE(result.mError.message.find("extra"), std::string::npos)
                << "the message should name the key: " << result.mError.message;
        }
    }
}

TEST(BodyJson, NonFiniteNumbersAreRefused)
{
    const std::string text = minText();
    const std::size_t line = lineOf(text, "\"default\": 0,");
    for (const char* bad : { "NaN", "nan", "Infinity", "-Infinity", "inf", "+1", ".5", "1.", "01", "0x10" })
    {
        const auto result = loadEdited("\"default\": 0,", std::string("\"default\": ") + bad + ",");
        expectLocatedError(result, "", line, std::string("number text ") + bad);
    }
    for (const char* overflow : { "1e999", "-1e999", "1e400", "123456789e999" })
    {
        const auto result = loadEdited("\"default\": 0,", std::string("\"default\": ") + overflow + ",");
        expectLocatedError(result, "", line, std::string("overflowing number ") + overflow);
    }
    // Weights in the summary too.
    const auto weight = loadEdited("[1, 1],", "[1, 1e999],");
    expectLocatedError(weight, "", 0, "overflowing weight");
}

TEST(BodyJson, WrongFormatAndVersionAreRefused)
{
    const std::string text = minText();
    {
        const auto result = loadEdited("\"openeden.body\"", "\"openeden.mesh\"");
        expectExactError(result, "/format", lineOf(text, "\"openeden.body\""), "wrong format");
    }
    {
        const auto result = loadEdited("\"openeden.body\"", "\"\"");
        expectExactError(result, "/format", lineOf(text, "\"openeden.body\""), "empty format");
    }
    for (const char* version : { "2", "0", "-1", "1.5", "\"1\"", "null", "true" })
    {
        const auto result = loadEdited("\"version\": 1,", std::string("\"version\": ") + version + ",");
        expectExactError(result, "/version", lineOf(text, "\"version\": 1,"), std::string("version ") + version);
    }
}

TEST(BodyJson, UnknownEnumNamesAreRefused)
{
    const std::string text = minText();
    {
        const auto result = loadEdited("\"meshFormat\": \"gltf\"", "\"meshFormat\": \"fbx\"");
        expectExactError(result, "/meshes/0/meshFormat", lineOf(text, "\"meshFormat\""), "meshFormat");
    }
    {
        const auto result = loadEdited("\"role\": \"skin\"", "\"role\": \"boss\"");
        expectExactError(result, "/meshes/0/role", lineOf(text, "\"role\""), "role");
    }
    {
        const auto result = loadEdited("\"role\": \"skin\"", "\"role\": \"Skin\"");
        expectExactError(result, "/meshes/0/role", lineOf(text, "\"role\""), "role is case sensitive");
    }
    {
        const auto result = loadEdited("\"ageClass\": \"unspecified\"", "\"ageClass\": \"teen\"");
        expectExactError(result, "/identity/ageClass", lineOf(text, "\"ageClass\""), "ageClass");
    }
    {
        const auto result = loadEdited("\"origin\": \"authored\"", "\"origin\": \"maybe\"");
        expectExactError(result, "/morphSet/origin", lineOf(text, "\"origin\""), "origin");
    }
}

TEST(BodyJson, UnsafeMeshPathsAreRefused)
{
    const std::string text = minText();
    const std::size_t line = lineOf(text, "\"path\": \"mesh/body.glb\"");
    const std::vector<std::string> bad = { "../x.glb", "mesh/../../x.glb", "/abs/x.glb", "a\\\\b.glb", "mesh//x.glb",
        "C:/x.glb", "", "./x.glb", "mesh/./x.glb", "mesh/x\\u0001.glb", std::string(513, 'a') };
    for (const std::string& path : bad)
    {
        const auto result = loadEdited("\"path\": \"mesh/body.glb\"", "\"path\": \"" + path + "\"");
        expectExactError(result, "/meshes/0/path", line, "path '" + path.substr(0, 20) + "'");
    }
    const auto good = loadEdited("\"path\": \"mesh/body.glb\"", "\"path\": \"a/b-c_d.e/f.glb\"");
    EXPECT_TRUE(good.mValue.has_value());
}

TEST(BodyJson, InvalidIdsAreRefused)
{
    const std::string text = minText();
    const std::vector<std::string> bad
        = { "Humanoid", "has space", "", "\xC3\xBCmlaut", "a/b", "a:b", std::string(65, 'a') };
    for (const std::string& id : bad)
    {
        const auto result = loadEdited("\"id\": \"humanoid_min\"", "\"id\": \"" + id + "\"");
        expectExactError(
            result, "/identity/id", lineOf(text, "\"id\": \"humanoid_min\""), "id '" + id.substr(0, 10) + "'");
        const auto mesh = loadEdited("\"id\": \"body\",", "\"id\": \"" + id + "\",");
        expectExactError(mesh, "/meshes/0/id", lineOf(text, "\"id\": \"body\","), "mesh id '" + id.substr(0, 10) + "'");
    }
    const auto good = loadEdited("\"id\": \"humanoid_min\"", "\"id\": \"a.b-c_9\"");
    EXPECT_TRUE(good.mValue.has_value());
    const auto longest = loadEdited("\"id\": \"humanoid_min\"", "\"id\": \"" + std::string(64, 'a') + "\"");
    EXPECT_TRUE(longest.mValue.has_value());
}

TEST(BodyJson, OverLongStringsAreRefused)
{
    const std::string text = minText();
    const std::size_t line = lineOf(text, "\"name\": \"Minimal humanoid\"");
    const auto atLimit = loadEdited(
        "\"name\": \"Minimal humanoid\"", "\"name\": \"" + std::string(Body::Limits::sMaxStringBytes, 'x') + "\"");
    EXPECT_TRUE(atLimit.mValue.has_value());
    const auto over = loadEdited(
        "\"name\": \"Minimal humanoid\"", "\"name\": \"" + std::string(Body::Limits::sMaxStringBytes + 1, 'x') + "\"");
    expectExactError(over, "/identity/name", line, "name over the limit");
    const auto huge = loadEdited("\"name\": \"Minimal humanoid\"", "\"name\": \"" + std::string(200000, 'x') + "\"");
    expectExactError(huge, "/identity/name", line, "name far over the limit");
}

TEST(BodyJson, ListSizeLimitsAreEnforced)
{
    {
        Body::BodyPackage package = humanoid();
        package.mMeshes.clear();
        for (std::size_t i = 0; i < Body::Limits::sMaxMeshes; ++i)
        {
            Body::MeshEntry mesh;
            mesh.mId = "m" + std::to_string(i);
            mesh.mPath = "mesh/m" + std::to_string(i) + ".glb";
            mesh.mLod = static_cast<int>(i);
            package.mMeshes.push_back(mesh);
        }
        package.mMorphSet.reset();
        package.mRegionMap.reset();
        const Body::BodyLoadResult atLimit = Body::readBodyPackage(Body::writeBodyPackage(package), "x");
        EXPECT_TRUE(atLimit.mValue.has_value()) << atLimit.mError.toString();

        Body::MeshEntry extra;
        extra.mId = "extra";
        extra.mPath = "mesh/extra.glb";
        package.mMeshes.push_back(extra);
        const Body::BodyLoadResult over = Body::readBodyPackage(Body::writeBodyPackage(package), "test.body.json");
        expectLocatedError(over, "/meshes", 0, "too many meshes");
    }
    {
        Body::BodyPackage package = humanoid();
        for (std::size_t i = 0; i <= Body::Limits::sMaxBones; ++i)
        {
            package.mBinding.mUnmappedBones.push_back("extra" + std::to_string(i));
        }
        const Body::BodyLoadResult over = Body::readBodyPackage(Body::writeBodyPackage(package), "test.body.json");
        expectLocatedError(over, "/binding", 0, "too many unmapped bones");
    }
}

TEST(BodyJson, DuplicateIdsAreTheValidatorsConcern)
{
    // bodyjson.hpp: "nothing else: every semantic rule is the validator's". Duplicate ids load.
    Body::BodyPackage package = humanoid();
    package.mMeshes[1].mId = package.mMeshes[0].mId;
    const Body::BodyLoadResult result = Body::readBodyPackage(Body::writeBodyPackage(package), "x");
    EXPECT_TRUE(result.mValue.has_value());
}

TEST(BodyJson, DeepNestingIsRefusedWithoutCrashing)
{
    for (const std::size_t depth :
        { Body::Limits::sMaxNesting + 1, Body::Limits::sMaxNesting * 4, std::size_t{ 100000 } })
    {
        const std::string arrays = std::string(depth, '[') + std::string(depth, ']');
        expectLocatedError(load(arrays), "", 0, "nested arrays " + std::to_string(depth));

        std::string objects;
        for (std::size_t i = 0; i < depth; ++i)
        {
            objects += "{\"a\":";
        }
        objects += "1" + std::string(depth, '}');
        expectLocatedError(load(objects), "", 0, "nested objects " + std::to_string(depth));
    }
    // Deep nesting inside an otherwise valid file.
    std::string nested = minText();
    bool ok = true;
    nested = insertLineAfter(nested, "\"format\": \"openeden.body\",",
        "\"extra\": " + std::string(Body::Limits::sMaxNesting + 10, '[')
            + std::string(Body::Limits::sMaxNesting + 10, ']') + ",",
        &ok);
    ASSERT_TRUE(ok);
    EXPECT_FALSE(load(nested).mValue.has_value());
}

TEST(BodyJson, EmptyAndTruncatedInputIsRefused)
{
    expectLocatedError(load(""), "", 0, "empty");
    expectLocatedError(load("   \n\n  "), "", 0, "whitespace only");
    expectLocatedError(load("{"), "", 0, "open brace");
    const std::string text = minText();
    // Every proper prefix that leaves out at least the closing brace is invalid JSON.
    const std::size_t closing = text.rfind('}');
    ASSERT_NE(closing, std::string::npos);
    for (std::size_t length = 0; length < closing; ++length)
    {
        const Body::BodyLoadResult result = load(text.substr(0, length));
        ASSERT_FALSE(result.mValue.has_value()) << "prefix of " << length << " bytes loaded";
        ASSERT_FALSE(result.mError.message.empty()) << length;
    }
}

TEST(BodyJson, NonJsonInputIsRefused)
{
    for (const char* text : { "hello", "{'format': 'openeden.body'}", "<xml/>", "{\"format\": }", "{\"a\": 1,}",
             "{\"a\": 1} trailing", "{\"a\": 1}{\"b\": 2}", "\"just a string\"", "true", "{\"format\" \"x\"}",
             "{\"a\": \"unterminated}", "{\"a\": \"bad escape \\q\"}", "[1, 2,", "// comment\n{}" })
    {
        const Body::BodyLoadResult result = load(text);
        EXPECT_FALSE(result.mValue.has_value()) << text;
        EXPECT_FALSE(result.mError.message.empty()) << text;
    }
}

TEST(BodyJson, Utf8BomNeverCrashes)
{
    const std::string text = minText();
    const Body::BodyLoadResult withBom = load(std::string("\xEF\xBB\xBF") + text);
    if (withBom.mValue.has_value())
    {
        EXPECT_EQ(*withBom.mValue, *load(text).mValue);
    }
    else
    {
        EXPECT_FALSE(withBom.mError.message.empty());
    }
    const Body::BodyLoadResult truncatedBom = load("\xEF\xBB");
    EXPECT_FALSE(truncatedBom.mValue.has_value());
}

TEST(BodyJson, InvalidUtf8InStringsNeverCrashes)
{
    for (const char* bad : { "\xFF", "\xC3", "\xC0\xAF", "\xED\xA0\x80", "\xF8\x88\x80\x80\x80" })
    {
        const Body::BodyLoadResult result = loadEdited("Minimal humanoid", std::string("x") + bad + "y");
        if (result.mValue.has_value())
        {
            const Body::BodyLoadResult again = Body::readBodyPackage(Body::writeBodyPackage(*result.mValue), "x");
            EXPECT_TRUE(again.mValue.has_value());
        }
        else
        {
            EXPECT_FALSE(result.mError.message.empty());
        }
    }
}

TEST(BodyJson, RandomBytesNeverLoad)
{
    std::uint32_t state = 12345u;
    const auto next = [&state]() {
        state = state * 1664525u + 1013904223u;
        return state >> 16;
    };
    for (int round = 0; round < 500; ++round)
    {
        std::string bytes(1 + next() % 300, '\0');
        for (char& byte : bytes)
        {
            byte = static_cast<char>(next() & 0xFF);
        }
        const Body::BodyLoadResult result = load(bytes);
        ASSERT_FALSE(result.mValue.has_value()) << "round " << round;
        ASSERT_FALSE(result.mError.message.empty()) << "round " << round;
    }
}

TEST(BodyJson, MutatingEveryByteNeverCrashesAndLoadedResultsRoundTrip)
{
    const std::string original = minText();
    ASSERT_FALSE(original.empty());
    int loaded = 0;
    int refused = 0;
    for (std::size_t i = 0; i < original.size(); ++i)
    {
        for (const unsigned char mask : { 0x01, 0x20, 0x80 })
        {
            std::string mutated = original;
            mutated[i] = static_cast<char>(static_cast<unsigned char>(mutated[i]) ^ mask);
            const Body::BodyLoadResult result = load(mutated);
            if (result.mValue.has_value())
            {
                ++loaded;
                const std::string written = Body::writeBodyPackage(*result.mValue);
                const Body::BodyLoadResult again = Body::readBodyPackage(written, "again");
                ASSERT_TRUE(again.mValue.has_value())
                    << "byte " << i << " mask " << int(mask) << ": " << again.mError.toString();
                EXPECT_EQ(*again.mValue, *result.mValue) << "byte " << i;
                EXPECT_EQ(Body::writeBodyPackage(*again.mValue), written) << "byte " << i;
            }
            else
            {
                ++refused;
                EXPECT_FALSE(result.mError.message.empty()) << "byte " << i;
                EXPECT_EQ(result.mError.file, "test.body.json") << "byte " << i;
            }
        }
    }
    EXPECT_GT(refused, 0);
    EXPECT_GT(loaded + refused, 0);
}

TEST(BodyJson, MutatingTokensNeverCrashes)
{
    // Replace whole values by other types, in turn, at every quoted or numeric token of the minimal file.
    const std::string original = minText();
    const std::vector<std::string> replacements = { "null", "true", "[]", "{}", "\"x\"", "-1", "1e999", "[[]]" };
    for (std::size_t i = 0; i < original.size(); ++i)
    {
        const char c = original[i];
        if (c != ':')
        {
            continue;
        }
        // The value starts after ": " and ends at the next ',', '\n' or closing bracket on the same line.
        std::size_t begin = i + 1;
        while (begin < original.size() && original[begin] == ' ')
        {
            ++begin;
        }
        std::size_t end = begin;
        int depth = 0;
        while (end < original.size())
        {
            const char e = original[end];
            if (e == '[' || e == '{')
            {
                ++depth;
            }
            else if (e == ']' || e == '}')
            {
                if (depth == 0)
                {
                    break;
                }
                --depth;
            }
            else if ((e == ',' || e == '\n') && depth == 0)
            {
                break;
            }
            ++end;
        }
        if (end <= begin)
        {
            continue;
        }
        for (const std::string& replacement : replacements)
        {
            const std::string mutated = original.substr(0, begin) + replacement + original.substr(end);
            const Body::BodyLoadResult result = load(mutated);
            if (result.mValue.has_value())
            {
                const Body::BodyLoadResult again = Body::readBodyPackage(Body::writeBodyPackage(*result.mValue), "x");
                ASSERT_TRUE(again.mValue.has_value());
                EXPECT_EQ(*again.mValue, *result.mValue);
            }
            else
            {
                ASSERT_FALSE(result.mError.message.empty());
            }
        }
    }
}

TEST(BodyJson, ReadFileReportsMissingFiles)
{
    TempFolder folder;
    const Body::BodyLoadResult missing = Body::readBodyPackageFile(folder.path() / "nope.body.json");
    EXPECT_FALSE(missing.mValue.has_value());
    EXPECT_FALSE(missing.mError.message.empty());
    EXPECT_EQ(missing.mError.line, 0u);
}

TEST(BodyJson, ReadFileReadsCanonicalText)
{
    TempFolder folder;
    const Body::BodyPackage package = humanoid();
    writeTextFile(folder.path() / "body.json", Body::writeBodyPackage(package));
    const Body::BodyLoadResult result = Body::readBodyPackageFile(folder.path() / "body.json");
    ASSERT_TRUE(result.mValue.has_value()) << result.mError.toString();
    EXPECT_EQ(*result.mValue, package);
}
