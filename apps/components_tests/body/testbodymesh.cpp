// Task 0069 (AdvancedBodies, merged legacy body), stage c: the `bodymesh` format part of the package (meshFormat and
// weldGroups), the `bodymesh` payload, the cache key, the manifest and the publishing of the derived pack. Written from
// the spec and the headers (bodymesh.hpp, bodycache.hpp) before any implementation. Synthetic data only.

#include "bodyfixtures.hpp"

#include <components/body/bodycache.hpp>
#include <components/body/bodyjson.hpp>
#include <components/body/bodymerge.hpp>
#include <components/body/bodymesh.hpp>
#include <components/body/bodypack.hpp>
#include <components/body/bodyvalidate.hpp>

#include <gtest/gtest.h>

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <fstream>
#include <limits>
#include <string>
#include <vector>

namespace
{
    using namespace BodyTest;

    // ---- a synthetic merged body: two quads that share an edge, welded by a declared seam

    Body::PartMesh makeQuad(const std::string& name, int slot, float x0, const std::string& bone)
    {
        Body::PartMesh part;
        part.mName = name;
        part.mSlot = slot;
        part.mMaterial = "tex";
        part.mPositions = { { x0, 0.f, 0.f }, { x0 + 1.f, 0.f, 0.f }, { x0 + 1.f, 1.f, 0.f }, { x0, 1.f, 0.f } };
        part.mNormals = { { 0, 0, 1 }, { 0, 0, 1 }, { 0, 0, 1 }, { 0, 0, 1 } };
        part.mUvs = { { x0, 0.f }, { x0 + 1.f, 0.f }, { x0 + 1.f, 1.f }, { x0, 1.f } };
        part.mTriangles = { 0, 1, 2, 0, 2, 3 };
        part.mRigidBone = bone;
        return part;
    }

    Body::MergedBody makeBody()
    {
        const std::vector<Body::PartMesh> parts
            = { makeQuad("a", 0, 0.f, "bip01 spine"), makeQuad("b", 1, 1.f, "bip01 head") };
        Body::MergeOptions options;
        options.mBoneOrder = { "bip01 spine", "bip01 head" };
        const Body::MergeResult result = Body::mergeParts(parts, { { 0, 1, 0.01 } }, options);
        EXPECT_TRUE(result.ok());
        Body::MergedBody body = result.mBody;
        // The payload does not carry these; clear them so that a round trip compares equal.
        body.mVertexMap.clear();
        body.mPartOrder.clear();
        body.mReport = {};
        return body;
    }

    std::vector<std::uint8_t> payloadOf(const Body::MergedBody& body)
    {
        std::vector<std::uint8_t> bytes;
        std::string error;
        EXPECT_TRUE(Body::writeBodyMesh(body, bytes, error)) << error;
        return bytes;
    }

    // ---- format: meshFormat "bodymesh" and weldGroups

    Body::BodyPackage withWeldGroups()
    {
        Body::BodyPackage package = humanoid();
        package.mMeshes[0].mFormat = Body::MeshFormat::BodyMesh;
        Body::WeldGroups welds;
        welds.mMesh = package.mMeshes[0].mId;
        welds.mToleranceM = 0.5; // metres: a value whose shortest text is a plain decimal
        welds.mGroups = { { 0, 1 }, { 2, 3, 4 } };
        package.mWeldGroups = welds;
        return package;
    }

    TEST(BodyMeshFormat, bodymeshAndWeldGroupsRoundTripAndAreCanonical)
    {
        const Body::BodyPackage package = withWeldGroups();
        const std::string text = Body::writeBodyPackage(package);
        EXPECT_NE(text.find("\"meshFormat\": \"bodymesh\""), std::string::npos);
        EXPECT_NE(text.find("\"weldGroups\""), std::string::npos);
        const Body::BodyLoadResult loaded = Body::readBodyPackage(text, "test.body.json");
        ASSERT_TRUE(loaded.mValue.has_value()) << loaded.mError.message;
        EXPECT_EQ(*loaded.mValue, package);
        EXPECT_EQ(Body::writeBodyPackage(*loaded.mValue), text) << "the writer must be canonical";
    }

    TEST(BodyMeshFormat, theValidatorAcceptsWellFormedWeldGroups)
    {
        const Body::Report report = Body::validateBodyPackage(withWeldGroups());
        EXPECT_TRUE(report.ok()) << describe(report);
        EXPECT_EQ(findCode(report, "BODY-WELD-001"), nullptr);
    }

    struct WeldCase
    {
        const char* mName;
        const char* mCode;
        void (*mMutate)(Body::BodyPackage&);
        const char* mPathPrefix;
    };

    const WeldCase weldCases[] = {
        { "mesh_missing", "BODY-WELD-001", [](Body::BodyPackage& p) { p.mWeldGroups->mMesh = "nope"; },
            "/weldGroups/mesh" },
        { "group_of_one", "BODY-WELD-002", [](Body::BodyPackage& p) { p.mWeldGroups->mGroups = { { 3 } }; },
            "/weldGroups/groups/0" },
        { "empty_group", "BODY-WELD-002", [](Body::BodyPackage& p) { p.mWeldGroups->mGroups = { {} }; },
            "/weldGroups/groups/0" },
        { "not_increasing", "BODY-WELD-003",
            [](Body::BodyPackage& p) {
                p.mWeldGroups->mGroups = { { 4, 2 } };
            },
            "/weldGroups/groups/0" },
        { "repeated_member", "BODY-WELD-003",
            [](Body::BodyPackage& p) {
                p.mWeldGroups->mGroups = { { 2, 2 } };
            },
            "/weldGroups/groups/0" },
        { "vertex_in_two_groups", "BODY-WELD-004",
            [](Body::BodyPackage& p) {
                p.mWeldGroups->mGroups = { { 0, 1 }, { 1, 2 } };
            },
            "/weldGroups/groups/1" },
        { "index_out_of_range", "BODY-WELD-005",
            [](Body::BodyPackage& p) {
                p.mWeldGroups->mGroups = { { 0, p.mMeshes[0].mSummary->mVertexCount } };
            },
            "/weldGroups/groups/0" },
        { "tolerance_nan", "BODY-WELD-006",
            [](Body::BodyPackage& p) { p.mWeldGroups->mToleranceM = std::numeric_limits<double>::quiet_NaN(); },
            "/weldGroups/tolerance" },
        { "tolerance_negative", "BODY-WELD-006", [](Body::BodyPackage& p) { p.mWeldGroups->mToleranceM = -0.001; },
            "/weldGroups/tolerance" },
        { "tolerance_over_a_metre", "BODY-WELD-006", [](Body::BodyPackage& p) { p.mWeldGroups->mToleranceM = 1.5; },
            "/weldGroups/tolerance" },
        { "derived_without_provenance", "BODY-PROV-001",
            [](Body::BodyPackage& p) { p.mWeldGroups->mOrigin = Body::Origin::Derived; }, "/weldGroups/origin" },
    };

    TEST(BodyMeshFormat, everyWeldRuleHasItsCodeAndPath)
    {
        for (const WeldCase& c : weldCases)
        {
            SCOPED_TRACE(c.mName);
            Body::BodyPackage package = withWeldGroups();
            c.mMutate(package);
            const Body::Report report = Body::validateBodyPackage(package);
            const Body::Finding* finding = findCode(report, c.mCode);
            ASSERT_NE(finding, nullptr) << describe(report);
            EXPECT_EQ(finding->mPath.rfind(c.mPathPrefix, 0), 0u) << finding->mPath;
            EXPECT_EQ(finding->mSeverity, Body::Severity::Error);
        }
    }

    TEST(BodyMeshFormat, theReaderRefusesHostileWeldGroups)
    {
        const std::string good = Body::writeBodyPackage(withWeldGroups());
        const auto refused = [&good](const std::string& from, const std::string& to) {
            bool ok = true;
            const std::string text = replaceFirst(good, from, to, &ok);
            EXPECT_TRUE(ok) << from;
            return !Body::readBodyPackage(text, "t.body.json").mValue.has_value();
        };
        EXPECT_TRUE(refused("\"groups\": [", "\"extra\": 1, \"groups\": ["));
        EXPECT_TRUE(refused("\"tolerance\": 0.5", "\"tolerance\": \"x\""));
        EXPECT_TRUE(refused("[0, 1]", "5"));
        EXPECT_TRUE(refused("[2, 3, 4]", "[2, -3]"));
        EXPECT_TRUE(refused("[2, 3, 4]", "[2, 3.5]"));

        // 65,537 groups of two are over the cap (Limits::sMaxWeldGroups).
        Body::BodyPackage many = withWeldGroups();
        many.mWeldGroups->mGroups.clear();
        for (std::uint32_t i = 0; i < 65537; ++i)
        {
            many.mWeldGroups->mGroups.push_back({ 2 * i, 2 * i + 1 });
        }
        EXPECT_FALSE(Body::readBodyPackage(Body::writeBodyPackage(many), "t.body.json").mValue.has_value());
    }

    // ---- payload

    TEST(BodyMeshPayload, aMergedBodyRoundTripsByteForByte)
    {
        const Body::MergedBody body = makeBody();
        const std::vector<std::uint8_t> bytes = payloadOf(body);
        ASSERT_FALSE(bytes.empty());
        EXPECT_EQ(std::memcmp(bytes.data(), Body::bodyMeshMagic, 8), 0);
        const Body::BodyMeshReadResult read = Body::readBodyMesh(bytes);
        ASSERT_TRUE(read.mBody.has_value()) << read.mError;
        EXPECT_EQ(*read.mBody, body);
        EXPECT_EQ(payloadOf(*read.mBody), bytes);
        EXPECT_EQ(payloadOf(body), bytes) << "writing twice gives the same bytes";
    }

    TEST(BodyMeshPayload, theWeldedVerticesAreOneAndTheBodyKeepsItsRegions)
    {
        const Body::MergedBody body = makeBody();
        EXPECT_EQ(body.mPositions.size(), 6u); // 8 input vertices, the shared edge welded
        EXPECT_EQ(body.mRegions.size(), 2u);
        const Body::BodyMeshReadResult read = Body::readBodyMesh(payloadOf(body));
        ASSERT_TRUE(read.mBody.has_value());
        EXPECT_EQ(read.mBody->mRegions, body.mRegions);
        EXPECT_EQ(read.mBody->mBones, body.mBones);
    }

    TEST(BodyMeshPayload, aBodyWithoutNormalsOrUvsRoundTrips)
    {
        Body::MergedBody body = makeBody();
        body.mNormals.clear();
        body.mUvs.clear();
        const std::vector<std::uint8_t> bytes = payloadOf(body);
        const Body::BodyMeshReadResult read = Body::readBodyMesh(bytes);
        ASSERT_TRUE(read.mBody.has_value()) << read.mError;
        EXPECT_EQ(*read.mBody, body);
    }

    TEST(BodyMeshPayload, everyTruncationIsRefused)
    {
        const std::vector<std::uint8_t> bytes = payloadOf(makeBody());
        for (std::size_t length = 0; length < bytes.size(); ++length)
        {
            const Body::BodyMeshReadResult read = Body::readBodyMesh(bytes.data(), length);
            EXPECT_FALSE(read.mBody.has_value()) << "length " << length;
            EXPECT_FALSE(read.mError.empty()) << "length " << length;
        }
    }

    TEST(BodyMeshPayload, aBitFlipIsRefusedOrCanonical)
    {
        const std::vector<std::uint8_t> bytes = payloadOf(makeBody());
        for (std::size_t i = 0; i < bytes.size(); ++i)
        {
            for (int bit : { 0, 3, 7 })
            {
                std::vector<std::uint8_t> flipped = bytes;
                flipped[i] = static_cast<std::uint8_t>(flipped[i] ^ (1u << bit));
                const Body::BodyMeshReadResult read = Body::readBodyMesh(flipped);
                if (read.mBody.has_value())
                {
                    // Accepted: it must be exactly what the writer would write (the format is canonical).
                    EXPECT_EQ(payloadOf(*read.mBody), flipped) << "byte " << i << " bit " << bit;
                }
            }
        }
    }

    TEST(BodyMeshPayload, hostileHeadersAreRefusedBeforeAllocating)
    {
        std::vector<std::uint8_t> bytes = payloadOf(makeBody());
        ASSERT_GE(bytes.size(), 48u);
        const auto setCount = [&bytes](std::size_t index, std::uint32_t value) {
            std::memcpy(bytes.data() + 16 + 4 * index, &value, 4);
        };
        const std::vector<std::uint8_t> good = bytes;
        for (std::size_t index = 0; index < 8; ++index)
        {
            bytes = good;
            setCount(index, 0xFFFFFFFFu);
            const Body::BodyMeshReadResult read = Body::readBodyMesh(bytes);
            EXPECT_FALSE(read.mBody.has_value()) << "count " << index;
            EXPECT_FALSE(read.mError.empty());
        }
        bytes = good;
        const std::uint32_t version = 7;
        std::memcpy(bytes.data() + 8, &version, 4);
        const Body::BodyMeshReadResult wrong = Body::readBodyMesh(bytes);
        EXPECT_FALSE(wrong.mBody.has_value());
        EXPECT_NE(wrong.mError.find("version 7"), std::string::npos) << wrong.mError;
        bytes = good;
        bytes[0] = 'X';
        EXPECT_FALSE(Body::readBodyMesh(bytes).mBody.has_value());
        bytes = good;
        bytes.push_back(0);
        EXPECT_FALSE(Body::readBodyMesh(bytes).mBody.has_value()) << "a trailing byte";
    }

    TEST(BodyMeshPayload, theWriterAndReaderRefuseABadBody)
    {
        const Body::MergedBody good = makeBody();
        const auto refusedWrite = [](Body::MergedBody body) {
            std::vector<std::uint8_t> out;
            std::string error;
            return !Body::writeBodyMesh(body, out, error) && !error.empty();
        };
        Body::MergedBody nan = good;
        nan.mPositions[2][1] = std::numeric_limits<float>::quiet_NaN();
        EXPECT_TRUE(refusedWrite(nan));
        Body::MergedBody inf = good;
        inf.mNormals[0][0] = std::numeric_limits<float>::infinity();
        EXPECT_TRUE(refusedWrite(inf));
        Body::MergedBody index = good;
        index.mTriangles[1] = 99;
        EXPECT_TRUE(refusedWrite(index));
        Body::MergedBody bone = good;
        bone.mBoneIndices[0][0] = 99;
        EXPECT_TRUE(refusedWrite(bone));
        Body::MergedBody weights = good;
        weights.mBoneWeights[0] = { 0.5f, 0.1f, 0.f, 0.f };
        EXPECT_TRUE(refusedWrite(weights));
        Body::MergedBody negative = good;
        negative.mBoneWeights[0] = { 1.5f, -0.5f, 0.f, 0.f };
        EXPECT_TRUE(refusedWrite(negative));
        Body::MergedBody region = good;
        region.mRegions[0].mTriangleCount = 99;
        EXPECT_TRUE(refusedWrite(region));
        Body::MergedBody order = good;
        std::swap(order.mRegions[0], order.mRegions[1]);
        EXPECT_TRUE(refusedWrite(order));
        Body::MergedBody submesh = good;
        submesh.mSubmeshes[0].mMaterial = 9;
        EXPECT_TRUE(refusedWrite(submesh));
        Body::MergedBody weld = good;
        weld.mWeldGroups = { { 1 } };
        EXPECT_TRUE(refusedWrite(weld));
        Body::MergedBody longName = good;
        longName.mBones[0] = std::string(200, 'a');
        EXPECT_TRUE(refusedWrite(longName));
        Body::MergedBody control = good;
        control.mBones[0] = "bip\x01";
        EXPECT_TRUE(refusedWrite(control));
        Body::MergedBody duplicate = good;
        duplicate.mBones[1] = duplicate.mBones[0];
        EXPECT_TRUE(refusedWrite(duplicate));
    }

    // ---- key, entry names, manifest

    Body::BodyKeyInput makeKeyInput()
    {
        Body::BodyKeyInput input;
        input.mRace = "Dark Elf";
        input.mSex = "female";
        input.mView = "third";
        input.mParts = { { 1, "b_n_dark elf_f_chest", 2, 1, "Dark Elf", "b/chest.nif", std::string(64, 'a') },
            { 4, "b_n_dark elf_f_hand", 7, 1, "Dark Elf", "b/hand.nif", std::string(64, 'b') } };
        input.mSkeletonPath = "meshes/base_animkna.nif";
        input.mSkeletonDigest = std::string(64, 'c');
        return input;
    }

    TEST(BodyCacheKey, itIs64LowercaseHexAndStable)
    {
        const std::string key = Body::computeBodyKey(makeKeyInput());
        ASSERT_EQ(key.size(), 64u);
        EXPECT_TRUE(std::all_of(
            key.begin(), key.end(), [](char c) { return (c >= '0' && c <= '9') || (c >= 'a' && c <= 'f'); }));
        EXPECT_EQ(Body::computeBodyKey(makeKeyInput()), key);
    }

    TEST(BodyCacheKey, thePartOrderDoesNotMatter)
    {
        Body::BodyKeyInput swapped = makeKeyInput();
        std::swap(swapped.mParts[0], swapped.mParts[1]);
        EXPECT_EQ(Body::computeBodyKey(swapped), Body::computeBodyKey(makeKeyInput()));
    }

    TEST(BodyCacheKey, everyInputChangesTheKey)
    {
        const std::string base = Body::computeBodyKey(makeKeyInput());
        const auto changes = [&base](void (*mutate)(Body::BodyKeyInput&)) {
            Body::BodyKeyInput input = makeKeyInput();
            mutate(input);
            return Body::computeBodyKey(input) != base;
        };
        EXPECT_TRUE(changes([](Body::BodyKeyInput& i) { i.mParts[0].mModelDigest = std::string(64, 'd'); }));
        EXPECT_TRUE(changes([](Body::BodyKeyInput& i) { i.mParts[1].mModelPath = "b/hand2.nif"; }));
        EXPECT_TRUE(changes([](Body::BodyKeyInput& i) { i.mParts[0].mRecordId = "other"; }));
        EXPECT_TRUE(changes([](Body::BodyKeyInput& i) { i.mParts[0].mFlags = 0; }));
        EXPECT_TRUE(changes([](Body::BodyKeyInput& i) { i.mParts[0].mPartType = 3; }));
        EXPECT_TRUE(changes([](Body::BodyKeyInput& i) { i.mParts[0].mRace = "Nord"; }));
        EXPECT_TRUE(changes([](Body::BodyKeyInput& i) { i.mParts[0].mSlot = 2; }));
        EXPECT_TRUE(changes([](Body::BodyKeyInput& i) { i.mParts.pop_back(); }));
        EXPECT_TRUE(changes([](Body::BodyKeyInput& i) { i.mSkeletonDigest = std::string(64, 'e'); }));
        EXPECT_TRUE(changes([](Body::BodyKeyInput& i) { i.mSkeletonPath = "meshes/base_anim.nif"; }));
        EXPECT_TRUE(changes([](Body::BodyKeyInput& i) { i.mSeams = { { 3, 4, 0.02 } }; }));
        EXPECT_TRUE(changes([](Body::BodyKeyInput& i) { i.mMerge.mUvTolerance = 2e-5; }));
        EXPECT_TRUE(changes([](Body::BodyKeyInput& i) { i.mMerge.mNormalAngle = 0.1; }));
        EXPECT_TRUE(changes([](Body::BodyKeyInput& i) { i.mMergerVersion += 1; }));
        EXPECT_TRUE(changes([](Body::BodyKeyInput& i) { i.mLayoutVersion += 1; }));
        EXPECT_TRUE(changes([](Body::BodyKeyInput& i) { i.mRace = "Nord"; }));
        EXPECT_TRUE(changes([](Body::BodyKeyInput& i) { i.mSex = "male"; }));
        EXPECT_TRUE(changes([](Body::BodyKeyInput& i) { i.mView = "first"; }));
    }

    TEST(BodyCacheKey, aPartOfOneBodyDoesNotChangeTheKeyOfAnotherBody)
    {
        // The nord body shares no part with the dark elf body: changing the dark elf's hand cannot touch its key.
        Body::BodyKeyInput nord = makeKeyInput();
        nord.mRace = "Nord";
        for (Body::BodyPartKey& part : nord.mParts)
        {
            part.mRace = "Nord";
            part.mRecordId = "nord " + part.mRecordId;
        }
        const std::string before = Body::computeBodyKey(nord);
        Body::BodyKeyInput elf = makeKeyInput();
        elf.mParts[1].mModelDigest = std::string(64, '9');
        (void)Body::computeBodyKey(elf);
        EXPECT_EQ(Body::computeBodyKey(nord), before);
    }

    TEST(BodyCacheEntryName, itIsASafeLowercasePathAndDistinguishesRaces)
    {
        const std::string name = Body::bodyEntryName("Dark Elf", "female", "third");
        EXPECT_TRUE(Body::isSafePackagePath(name)) << name;
        EXPECT_EQ(name.rfind("bodies/", 0), 0u);
        EXPECT_EQ(name.substr(name.size() - 9), ".bodymesh");
        for (char c : name)
        {
            EXPECT_FALSE(c >= 'A' && c <= 'Z') << name;
            EXPECT_NE(c, ' ');
            EXPECT_NE(c, '\\');
        }
        EXPECT_NE(Body::bodyEntryName("dark_elf", "female", "third"), name);
        EXPECT_NE(
            Body::bodyEntryName("Dark-Elf", "female", "third"), Body::bodyEntryName("dark_elf", "female", "third"));
        EXPECT_NE(Body::bodyEntryName("Dark Elf", "male", "third"), name);
        EXPECT_EQ(Body::bodyEntryName("Dark Elf", "female", "third"), name);
    }

    Body::CacheManifest makeManifest()
    {
        Body::CacheManifest manifest;
        manifest.mEntries = { { "bodies/a.bodymesh", "Dark Elf", "female", "third", std::string(64, 'a') },
            { "bodies/b.bodymesh", "Nord", "male", "third", std::string(64, 'b') } };
        manifest.mFailures = { { "Argonian", "male", "third", "no skeleton: file missing" } };
        return manifest;
    }

    TEST(BodyCacheManifest, itRoundTripsAndIsCanonical)
    {
        const Body::CacheManifest manifest = makeManifest();
        const std::string text = Body::writeManifest(manifest);
        ASSERT_FALSE(text.empty());
        EXPECT_EQ(text.back(), '\n');
        std::string error;
        const std::optional<Body::CacheManifest> read = Body::readManifest(text, error);
        ASSERT_TRUE(read.has_value()) << error;
        EXPECT_EQ(*read, manifest);
        EXPECT_EQ(Body::writeManifest(*read), text);
    }

    TEST(BodyCacheManifest, theStrictReaderRefusesGarbage)
    {
        const std::string good = Body::writeManifest(makeManifest());
        const auto refused = [](const std::string& text) {
            std::string error;
            return !Body::readManifest(text, error).has_value() && !error.empty();
        };
        EXPECT_TRUE(refused(""));
        EXPECT_TRUE(refused("garbage\n"));
        EXPECT_TRUE(refused(good + "unknown\tx\n"));
        EXPECT_TRUE(
            refused(good + "entry\tbodies/a.bodymesh\tDark Elf\tfemale\tthird\t" + std::string(64, 'a') + "\n"));
        EXPECT_TRUE(refused(good.substr(0, good.size() - 1))) << "no final newline";
        EXPECT_TRUE(refused(std::string(Body::CacheLimits::sMaxLineBytes + 10, 'x') + "\n"));
        EXPECT_TRUE(refused(good + "entry\tbodies/z.bodymesh\tR\tfemale\tthird\tnot-hex\n"));
        EXPECT_TRUE(refused(good + "entry\t../z\tR\tfemale\tthird\t" + std::string(64, 'a') + "\n"));
        std::string control = good;
        control[control.size() - 3] = '\x01';
        EXPECT_TRUE(refused(control));
        std::string version = good;
        version.replace(version.find("openeden.bodycache\t0"), 20, "openeden.bodycache\t9");
        EXPECT_TRUE(refused(version));
    }

    TEST(BodyCacheManifest, anEntryIsCurrentOnlyWithItsKey)
    {
        const Body::CacheManifest manifest = makeManifest();
        EXPECT_TRUE(Body::isEntryCurrent(manifest, "bodies/a.bodymesh", std::string(64, 'a')));
        EXPECT_FALSE(Body::isEntryCurrent(manifest, "bodies/a.bodymesh", std::string(64, 'b')));
        EXPECT_FALSE(Body::isEntryCurrent(manifest, "bodies/none.bodymesh", std::string(64, 'a')));
    }

    TEST(BodyCacheManifest, theStatusFileIsOneLineKeyedJson)
    {
        const std::string json = Body::writeStatusJson(makeManifest(), "complete");
        EXPECT_NE(json.find("\"state\": \"complete\""), std::string::npos) << json;
        EXPECT_NE(json.find("\"bodies\": 2"), std::string::npos) << json;
        EXPECT_NE(json.find("\"failed\""), std::string::npos) << json;
        EXPECT_NE(json.find("no skeleton: file missing"), std::string::npos) << json;
        EXPECT_EQ(json.back(), '\n');
    }

    // ---- publishing

    Body::PackageFiles makeFiles(std::uint8_t fill)
    {
        Body::PackageFiles files;
        const std::string manifest = Body::writeManifest(makeManifest());
        files.mFiles["manifest.txt"] = std::vector<std::uint8_t>(manifest.begin(), manifest.end());
        files.mFiles["bodies/a.bodymesh"] = std::vector<std::uint8_t>(5000, fill);
        files.mFiles["bodies/b.bodymesh"] = std::vector<std::uint8_t>(9000, static_cast<std::uint8_t>(fill + 1));
        return files;
    }

    TEST(BodyCachePublish, itPublishesLoadsAndRepublishesIdentically)
    {
        TempFolder folder;
        std::string name, error;
        ASSERT_TRUE(Body::publishCache(folder.path(), makeFiles(1), name, error)) << error;
        EXPECT_EQ(name.rfind("merged-", 0), 0u);
        EXPECT_EQ(name.substr(name.size() - 8), ".edenpak");
        EXPECT_TRUE(std::filesystem::exists(folder.path() / name));
        const std::string pointer = readTextFile(folder.path() / "current");
        EXPECT_EQ(pointer, name + "\n");
        const std::optional<Body::PackageFiles> loaded = Body::loadCurrentCache(folder.path(), error);
        ASSERT_TRUE(loaded.has_value()) << error;
        EXPECT_EQ(loaded->mFiles, makeFiles(1).mFiles);

        const std::string packBytes = readTextFile(folder.path() / name);
        std::string again;
        ASSERT_TRUE(Body::publishCache(folder.path(), makeFiles(1), again, error)) << error;
        EXPECT_EQ(again, name) << "the same files give the same pack name";
        EXPECT_EQ(readTextFile(folder.path() / again), packBytes) << "byte-identical re-publish";
    }

    TEST(BodyCachePublish, aSecondPublishMovesTheCurrentPointerAndKeepsTheOldPack)
    {
        TempFolder folder;
        std::string first, second, error;
        ASSERT_TRUE(Body::publishCache(folder.path(), makeFiles(1), first, error)) << error;
        ASSERT_TRUE(Body::publishCache(folder.path(), makeFiles(5), second, error)) << error;
        EXPECT_NE(first, second);
        EXPECT_TRUE(std::filesystem::exists(folder.path() / first));
        const std::optional<Body::PackageFiles> loaded = Body::loadCurrentCache(folder.path(), error);
        ASSERT_TRUE(loaded.has_value()) << error;
        EXPECT_EQ(loaded->mFiles, makeFiles(5).mFiles);
    }

    TEST(BodyCachePublish, aKilledBakeLeavesThePreviousCacheReadable)
    {
        TempFolder folder;
        std::string name, error;
        ASSERT_TRUE(Body::publishCache(folder.path(), makeFiles(1), name, error)) << error;
        // A half-written pack under a temporary name, and a half-written pointer, as a killed bake would leave them.
        writeTextFile(folder.path() / "merged-0000.edenpak.tmp", "half a pack");
        writeTextFile(folder.path() / "current.tmp", "merged-0000.edenpak");
        const std::optional<Body::PackageFiles> loaded = Body::loadCurrentCache(folder.path(), error);
        ASSERT_TRUE(loaded.has_value()) << error;
        EXPECT_EQ(loaded->mFiles, makeFiles(1).mFiles);
    }

    TEST(BodyCachePublish, aBadPointerOrAMissingPackIsRefused)
    {
        TempFolder folder;
        std::string error;
        EXPECT_FALSE(Body::loadCurrentCache(folder.path(), error).has_value()) << "no current file";
        EXPECT_FALSE(error.empty());
        writeTextFile(folder.path() / "current", "../escape.edenpak\n");
        EXPECT_FALSE(Body::loadCurrentCache(folder.path(), error).has_value());
        writeTextFile(folder.path() / "current", "missing.edenpak\n");
        EXPECT_FALSE(Body::loadCurrentCache(folder.path(), error).has_value());
        writeTextFile(folder.path() / "current", "a\\b.edenpak\n");
        EXPECT_FALSE(Body::loadCurrentCache(folder.path(), error).has_value());
        writeTextFile(folder.path() / "current", std::string(5000, 'x') + "\n");
        EXPECT_FALSE(Body::loadCurrentCache(folder.path(), error).has_value());
    }

    TEST(BodyCachePublish, everyTruncationAndBitFlipOfThePackIsRefusedOrHarmless)
    {
        TempFolder folder;
        std::string name, error;
        ASSERT_TRUE(Body::publishCache(folder.path(), makeFiles(1), name, error)) << error;
        const std::filesystem::path path = folder.path() / name;
        const std::string bytes = readTextFile(path);
        ASSERT_GT(bytes.size(), 4096u);
        for (std::size_t length = 0; length < bytes.size(); length += 4096)
        {
            writeTextFile(path, bytes.substr(0, length));
            EXPECT_FALSE(Body::loadCurrentCache(folder.path(), error).has_value()) << "length " << length;
        }
        writeTextFile(path, bytes.substr(0, bytes.size() - 1));
        EXPECT_FALSE(Body::loadCurrentCache(folder.path(), error).has_value());
        for (std::size_t i = 0; i < bytes.size(); i += 97)
        {
            std::string flipped = bytes;
            flipped[i] = static_cast<char>(flipped[i] ^ 0x10);
            writeTextFile(path, flipped);
            const std::optional<Body::PackageFiles> loaded = Body::loadCurrentCache(folder.path(), error);
            if (loaded.has_value())
            {
                // The pack reader verifies hashes: an accepted flip may only hit bytes that do not matter.
                EXPECT_EQ(loaded->mFiles, makeFiles(1).mFiles) << "byte " << i;
            }
        }
    }

    // ---- review fixes of stage c (task 0069): a complete key, collision-free names, escaped manifests, atomic
    // publishing, canonical payloads

    Body::BodyKeyInput fullKeyInput()
    {
        Body::BodyKeyInput input = makeKeyInput();
        input.mSeams = { { 3, 4, 0.01 }, { 5, 6, 0.01 } };
        input.mMerge.mBoneOrder = { "bip01 spine", "bip01 head" };
        return input;
    }

    TEST(BodyCacheKeyComplete, everyMergerInputChangesTheKey)
    {
        const std::string base = Body::computeBodyKey(fullKeyInput());
        const auto changes = [&base](void (*mutate)(Body::BodyKeyInput&)) {
            Body::BodyKeyInput input = fullKeyInput();
            mutate(input);
            return Body::computeBodyKey(input) != base;
        };
        EXPECT_TRUE(changes([](Body::BodyKeyInput& i) { i.mSeams[0].mTolerance = 0.02; }));
        EXPECT_TRUE(changes([](Body::BodyKeyInput& i) { i.mSeams[0].mSlotA = 9; }));
        EXPECT_TRUE(changes([](Body::BodyKeyInput& i) { i.mSeams[1].mSlotB = 9; }));
        EXPECT_TRUE(changes([](Body::BodyKeyInput& i) { i.mSeams.pop_back(); }));
        EXPECT_TRUE(changes([](Body::BodyKeyInput& i) { i.mMerge.mBoneOrder.push_back("bip01 tail"); }));
        EXPECT_TRUE(changes([](Body::BodyKeyInput& i) { std::swap(i.mMerge.mBoneOrder[0], i.mMerge.mBoneOrder[1]); }));
        EXPECT_TRUE(changes([](Body::BodyKeyInput& i) { i.mMerge.mUvTolerance = 2e-5; }));
        EXPECT_TRUE(changes([](Body::BodyKeyInput& i) { i.mMerge.mNormalAngle = 0.1; }));
        EXPECT_TRUE(changes([](Body::BodyKeyInput& i) { i.mReaderVersion += 1; }));
        EXPECT_TRUE(changes([](Body::BodyKeyInput& i) { i.mLimits.mMaxParts += 1; }));
        EXPECT_TRUE(changes([](Body::BodyKeyInput& i) { i.mLimits.mMaxVerticesPerPart += 1; }));
        EXPECT_TRUE(changes([](Body::BodyKeyInput& i) { i.mLimits.mMaxTrianglesPerPart += 1; }));
        EXPECT_TRUE(changes([](Body::BodyKeyInput& i) { i.mLimits.mMaxTotalVertices += 1; }));
        EXPECT_TRUE(changes([](Body::BodyKeyInput& i) { i.mLimits.mMaxTotalTriangles += 1; }));
        EXPECT_TRUE(changes([](Body::BodyKeyInput& i) { i.mLimits.mMaxBonesPerPart += 1; }));
        EXPECT_TRUE(changes([](Body::BodyKeyInput& i) { i.mLimits.mMaxSeams += 1; }));
        EXPECT_TRUE(changes([](Body::BodyKeyInput& i) { i.mLimits.mMaxPairsPerSeam += 1; }));
        EXPECT_TRUE(changes([](Body::BodyKeyInput& i) { i.mLimits.mMaxCandidateChecks += 1; }));
        EXPECT_TRUE(changes([](Body::BodyKeyInput& i) { i.mLimits.mMaxNameBytes += 1; }));
        EXPECT_TRUE(changes([](Body::BodyKeyInput& i) { i.mLimits.mMaxErrors += 1; }));
    }

    TEST(BodyCacheEntryName, theDigestIsLongEnoughThatACollisionIsNotAFeasibleAttack)
    {
        const std::string name = Body::bodyEntryName("Dark Elf", "female", "third");
        const std::size_t dash = name.rfind('-');
        ASSERT_NE(dash, std::string::npos);
        const std::string digest = name.substr(dash + 1, name.size() - dash - 1 - 9);
        EXPECT_GE(digest.size(), 32u) << name;
        // The exact (race, sex, view) tuple decides it, including bytes after the sanitised prefix is cut.
        const std::string common(60, 'a');
        EXPECT_NE(
            Body::bodyEntryName(common + "1", "male", "third"), Body::bodyEntryName(common + "2", "male", "third"));
        EXPECT_TRUE(Body::isSafePackagePath(name));
    }

    TEST(BodyCacheManifest, aRaceWithATabOrNewlineRoundTripsAndPercentIsEscaped)
    {
        Body::CacheManifest manifest;
        manifest.mEntries = { { "bodies/x-0.bodymesh", "Odd\tRace\nname%", "female", "third", std::string(64, 'a') } };
        manifest.mFailures = { { "Odd\tRace\nname%", "male", "third", "one line" } };
        const std::string text = Body::writeManifest(manifest);
        // Exactly the lines of the grammar: no raw newline inside a field.
        EXPECT_EQ(std::count(text.begin(), text.end(), '\n'), 5);
        std::string error;
        const std::optional<Body::CacheManifest> read = Body::readManifest(text, error);
        ASSERT_TRUE(read.has_value()) << error;
        EXPECT_EQ(*read, manifest);
        EXPECT_EQ(Body::writeManifest(*read), text) << "canonical";
        // A non-canonical escape (an escaped letter) is refused, so each manifest has one text.
        std::string bad = text;
        const std::size_t at = bad.find("female");
        ASSERT_NE(at, std::string::npos);
        bad.replace(at, 1, "%66");
        EXPECT_FALSE(Body::readManifest(bad, error).has_value());
    }

    TEST(BodyCacheManifest, theStatusFileIsValidUtf8EvenForAHostileRaceName)
    {
        Body::CacheManifest manifest;
        manifest.mFailures = { { "bad\xff\xfe race", "male", "third", "reason \xc3(" } };
        const std::string json = Body::writeStatusJson(manifest, "complete");
        for (const char c : json)
            EXPECT_NE(static_cast<unsigned char>(c), 0xffu);
        EXPECT_NE(json.find("\\ufffd"), std::string::npos);
        // Valid UTF-8 passes through untouched.
        manifest.mFailures = { { "Ch\xc3\xa2teau", "male", "third", "ok" } };
        EXPECT_NE(Body::writeStatusJson(manifest, "complete").find("Ch\xc3\xa2teau"), std::string::npos);
    }

    TEST(BodyCachePublish, republishingIdenticalFilesDoesNotRewriteTheLivePack)
    {
        TempFolder folder;
        std::string name, error;
        ASSERT_TRUE(Body::publishCache(folder.path(), makeFiles(1), name, error)) << error;
        const std::filesystem::path pack = folder.path() / name;
        const auto past = std::filesystem::file_time_type::clock::now() - std::chrono::hours(48);
        std::filesystem::last_write_time(pack, past);
        const auto before = std::filesystem::last_write_time(pack);
        std::string again;
        ASSERT_TRUE(Body::publishCache(folder.path(), makeFiles(1), again, error)) << error;
        EXPECT_EQ(again, name);
        EXPECT_EQ(std::filesystem::last_write_time(pack), before) << "the live pack was rewritten in place";
    }

    TEST(BodyCachePublish, aDamagedLivePackIsReplacedNotTrustedAndLeavesNoTemporaryFile)
    {
        TempFolder folder;
        std::string name, error;
        ASSERT_TRUE(Body::publishCache(folder.path(), makeFiles(1), name, error)) << error;
        writeTextFile(folder.path() / name, "damaged");
        std::string again;
        ASSERT_TRUE(Body::publishCache(folder.path(), makeFiles(1), again, error)) << error;
        EXPECT_EQ(again, name);
        const std::optional<Body::PackageFiles> loaded = Body::loadCurrentCache(folder.path(), error);
        ASSERT_TRUE(loaded.has_value()) << error;
        EXPECT_EQ(loaded->mFiles, makeFiles(1).mFiles);
        for (const auto& entry : std::filesystem::directory_iterator(folder.path()))
            EXPECT_NE(entry.path().extension(), ".tmp") << entry.path();
    }

    TEST(BodyFileAtomic, itWritesWholeReplacesAndLeavesNothingBehind)
    {
        TempFolder folder;
        std::string error;
        ASSERT_TRUE(Body::writeFileAtomic(folder.path() / "status.json", "first\n", error)) << error;
        ASSERT_TRUE(Body::writeFileAtomic(folder.path() / "status.json", "second\n", error)) << error;
        EXPECT_EQ(readTextFile(folder.path() / "status.json"), "second\n");
        std::size_t files = 0;
        for (const auto& entry : std::filesystem::directory_iterator(folder.path()))
        {
            ++files;
            EXPECT_NE(entry.path().extension(), ".tmp");
        }
        EXPECT_EQ(files, 1u);
        EXPECT_FALSE(Body::writeFileAtomic(folder.path() / "missing" / "status.json", "x", error));
        EXPECT_FALSE(error.empty());
    }

    TEST(BodyCachePrune, itDeletesOldPacksButKeepsTheNamedOnes)
    {
        TempFolder folder;
        std::string error;
        std::vector<std::string> names;
        for (const std::uint8_t fill : std::array<std::uint8_t, 4>{ 1, 2, 3, 4 })
        {
            std::string name;
            ASSERT_TRUE(Body::publishCache(folder.path(), makeFiles(fill), name, error)) << error;
            names.push_back(name);
        }
        writeTextFile(folder.path() / "notes.txt", "keep me");
        EXPECT_EQ(Body::pruneOldPacks(folder.path(), { names[2], names[3] }), 2u);
        EXPECT_FALSE(std::filesystem::exists(folder.path() / names[0]));
        EXPECT_FALSE(std::filesystem::exists(folder.path() / names[1]));
        EXPECT_TRUE(std::filesystem::exists(folder.path() / names[2]));
        EXPECT_TRUE(std::filesystem::exists(folder.path() / names[3]));
        EXPECT_TRUE(std::filesystem::exists(folder.path() / "notes.txt"));
        EXPECT_TRUE(std::filesystem::exists(folder.path() / "current"));
        EXPECT_TRUE(Body::loadCurrentCache(folder.path(), error).has_value()) << error;
    }

    // ---- the payload reader accepts canonical bodies only

    bool refusedAtWrite(const Body::MergedBody& body)
    {
        std::vector<std::uint8_t> out;
        std::string error;
        return !Body::writeBodyMesh(body, out, error) && !error.empty();
    }

    TEST(BodyMeshPayloadCanonical, weightsLargestFirstTiesByIndexZeroSlotsAtTheEnd)
    {
        const Body::MergedBody good = makeBody();
        Body::MergedBody ok = good;
        ok.mBoneIndices[0] = { 0, 1, 0, 0 };
        ok.mBoneWeights[0] = { 0.75f, 0.25f, 0.f, 0.f };
        std::vector<std::uint8_t> bytes;
        std::string error;
        ASSERT_TRUE(Body::writeBodyMesh(ok, bytes, error)) << error;
        EXPECT_TRUE(Body::readBodyMesh(bytes).mBody.has_value());
        Body::MergedBody tie = good;
        tie.mBoneIndices[0] = { 0, 1, 0, 0 };
        tie.mBoneWeights[0] = { 0.5f, 0.5f, 0.f, 0.f };
        EXPECT_TRUE(Body::writeBodyMesh(tie, bytes, error)) << error;

        Body::MergedBody unsorted = good;
        unsorted.mBoneIndices[0] = { 0, 1, 0, 0 };
        unsorted.mBoneWeights[0] = { 0.25f, 0.75f, 0.f, 0.f };
        EXPECT_TRUE(refusedAtWrite(unsorted)) << "smaller weight first";
        Body::MergedBody tieDescending = good;
        tieDescending.mBoneIndices[0] = { 1, 0, 0, 0 };
        tieDescending.mBoneWeights[0] = { 0.5f, 0.5f, 0.f, 0.f };
        EXPECT_TRUE(refusedAtWrite(tieDescending)) << "a tie must list the lower bone index first";
        Body::MergedBody zeroSlot = good;
        zeroSlot.mBoneIndices[0] = { 0, 1, 0, 0 };
        zeroSlot.mBoneWeights[0] = { 1.f, 0.f, 0.f, 0.f };
        EXPECT_TRUE(refusedAtWrite(zeroSlot)) << "a zero-weight slot must name bone 0";
        Body::MergedBody duplicate = good;
        duplicate.mBoneIndices[0] = { 0, 0, 0, 0 };
        duplicate.mBoneWeights[0] = { 0.5f, 0.5f, 0.f, 0.f };
        EXPECT_TRUE(refusedAtWrite(duplicate)) << "a bone listed twice for one vertex";
    }

    TEST(BodyMeshPayloadCanonical, onlyUsedBonesAreListedAndTheSumIsTight)
    {
        const Body::MergedBody good = makeBody();
        Body::MergedBody unused = good;
        unused.mBones.push_back("bip01 unused");
        EXPECT_TRUE(refusedAtWrite(unused)) << "a bone no vertex uses";
        Body::MergedBody off = good;
        off.mBoneWeights[0] = { 0.9999f, 0.f, 0.f, 0.f }; // 1e-4 off: accepted by the old 1e-3 tolerance
        EXPECT_TRUE(refusedAtWrite(off));
        Body::MergedBody close = good;
        close.mBoneWeights[0] = { 1.f - 1e-7f, 0.f, 0.f, 0.f };
        std::vector<std::uint8_t> bytes;
        std::string error;
        EXPECT_TRUE(Body::writeBodyMesh(close, bytes, error)) << error;
    }
}
