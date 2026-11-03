// Task 0063 (AdvancedBodies B1), stage b: the command-line validator as a function (Body::runBodyValidateTool).
// Written from the task spec and bodytool.hpp only.

#include "bodyfixtures.hpp"

#include <components/body/bodyjson.hpp>
#include <components/body/bodypack.hpp>
#include <components/body/bodytool.hpp>
#include <components/body/bodyvalidate.hpp>

#include <gtest/gtest.h>

#include <filesystem>
#include <sstream>
#include <string>
#include <vector>

namespace
{
    using namespace BodyTest;

    struct ToolResult
    {
        int mExit = -1;
        std::string mOut;
        std::string mErr;
    };

    ToolResult runTool(const std::vector<std::string>& args)
    {
        std::ostringstream out;
        std::ostringstream err;
        ToolResult result;
        result.mExit = Body::runBodyValidateTool(args, out, err);
        result.mOut = out.str();
        result.mErr = err.str();
        return result;
    }

    std::string pathArg(const std::filesystem::path& path)
    {
        return path.string();
    }

    void writePackageFolder(const std::filesystem::path& folder, const Body::BodyPackage& package)
    {
        writeTextFile(folder / "body.json", Body::writeBodyPackage(package));
    }
}

TEST(BodyTool, CleanFolderExitsZeroAndPrintsTheTextReport)
{
    TempFolder folder;
    const Body::BodyPackage package = humanoid();
    writePackageFolder(folder.path(), package);
    const ToolResult result = runTool({ pathArg(folder.path()) });
    EXPECT_EQ(result.mExit, 0) << result.mErr << result.mOut;
    EXPECT_EQ(result.mOut, Body::formatReportText(Body::validateBodyPackage(package)));
    EXPECT_NE(result.mOut.find("0 error(s), 0 warning(s), humanoidGait=true"), std::string::npos);
}

TEST(BodyTool, BodyJsonFileIsAcceptedDirectly)
{
    TempFolder folder;
    writePackageFolder(folder.path(), tailedQuadruped());
    const ToolResult result = runTool({ pathArg(folder.path() / "body.json") });
    EXPECT_EQ(result.mExit, 0) << result.mErr << result.mOut;
    EXPECT_NE(result.mOut.find("humanoidGait=false"), std::string::npos);
}

TEST(BodyTool, ErrorsExitOne)
{
    TempFolder folder;
    Body::BodyPackage package = humanoid();
    setInfluences(*package.mMeshes[4].mSummary, 0, { { 0, 0.5 } });
    writePackageFolder(folder.path(), package);
    const ToolResult result = runTool({ pathArg(folder.path()) });
    EXPECT_EQ(result.mExit, 1) << result.mErr;
    EXPECT_NE(result.mOut.find("error BODY-SKIN-002 /meshes/4/summary/influenceOffsets/0"), std::string::npos)
        << result.mOut;
    EXPECT_EQ(result.mOut, Body::formatReportText(Body::validateBodyPackage(package)));
}

TEST(BodyTool, WarningsAloneExitZero)
{
    TempFolder folder;
    Body::BodyPackage package = humanoid();
    package.mBinding.mRestSkeleton.reset();
    writePackageFolder(folder.path(), package);
    const ToolResult result = runTool({ pathArg(folder.path()) });
    EXPECT_EQ(result.mExit, 0) << result.mErr;
    EXPECT_NE(result.mOut.find("warning BODY-BIND-014 /binding/restSkeleton"), std::string::npos) << result.mOut;
    EXPECT_NE(result.mOut.find("0 error(s), 1 warning(s)"), std::string::npos) << result.mOut;
}

TEST(BodyTool, JsonFlagPrintsTheCanonicalJsonReport)
{
    TempFolder folder;
    Body::BodyPackage package = humanoid();
    setInfluences(*package.mMeshes[4].mSummary, 0, { { 0, 0.5 } });
    writePackageFolder(folder.path(), package);
    const ToolResult result = runTool({ "--json", pathArg(folder.path()) });
    EXPECT_EQ(result.mExit, 1) << result.mErr;
    EXPECT_EQ(result.mOut, Body::formatReportJson(Body::validateBodyPackage(package)));

    const ToolResult clean = [&folder]() {
        writePackageFolder(folder.path(), humanoid());
        return runTool({ "--json", pathArg(folder.path()) });
    }();
    EXPECT_EQ(clean.mExit, 0);
    EXPECT_EQ(clean.mOut, Body::formatReportJson(Body::validateBodyPackage(humanoid())));
}

TEST(BodyTool, BodyJsonThatFailsToLoadExitsOneWithTheLocatedErrorOnTheErrorStream)
{
    TempFolder folder;
    const std::string text = Body::writeBodyPackage(humanoid());
    bool ok = true;
    const std::string broken = replaceFirst(text, "\"lod\": 0,", "\"lod\": \"zero\",", &ok);
    ASSERT_TRUE(ok);
    writeTextFile(folder.path() / "body.json", broken);
    const ToolResult result = runTool({ pathArg(folder.path()) });
    EXPECT_EQ(result.mExit, 1);
    EXPECT_FALSE(result.mErr.empty());
    EXPECT_NE(result.mErr.find("/meshes/0/lod"), std::string::npos) << result.mErr;
    EXPECT_NE(result.mErr.find(std::to_string(lineOf(broken, "\"lod\": \"zero\","))), std::string::npos) << result.mErr;
    EXPECT_EQ(result.mOut.find("BODY-"), std::string::npos) << "no report for a package that did not load";
}

TEST(BodyTool, GarbageBodyJsonExitsOne)
{
    TempFolder folder;
    writeTextFile(folder.path() / "body.json", "not json");
    const ToolResult result = runTool({ pathArg(folder.path()) });
    EXPECT_EQ(result.mExit, 1);
    EXPECT_FALSE(result.mErr.empty());
}

TEST(BodyTool, MissingInputExitsTwo)
{
    TempFolder folder;
    const ToolResult result = runTool({ pathArg(folder.path() / "nothing_here") });
    EXPECT_EQ(result.mExit, 2);
    EXPECT_FALSE(result.mErr.empty());
    EXPECT_TRUE(result.mOut.empty()) << result.mOut;
}

TEST(BodyTool, BadArgumentsExitTwo)
{
    const std::vector<std::vector<std::string>> bad = { {}, { "--json" }, { "--bogus", "x" }, { "--help-me-not" } };
    for (const std::vector<std::string>& args : bad)
    {
        const ToolResult result = runTool(args);
        EXPECT_EQ(result.mExit, 2) << (args.empty() ? "no arguments" : args[0]);
        EXPECT_FALSE(result.mErr.empty()) << (args.empty() ? "no arguments" : args[0]);
    }
}

TEST(BodyTool, FolderWithoutBodyJsonIsNotAPackage)
{
    TempFolder folder;
    writeTextFile(folder.path() / "readme.txt", "hello");
    const ToolResult result = runTool({ pathArg(folder.path()) });
    EXPECT_NE(result.mExit, 0);
    EXPECT_FALSE(result.mErr.empty());
}

TEST(BodyTool, PackIsValidatedLikeItsFolder)
{
    TempFolder folder;
    TempFolder output;
    Body::BodyPackage package = humanoid();
    setInfluences(*package.mMeshes[4].mSummary, 0, { { 0, 0.5 } });
    writePackageFolder(folder.path(), package);
    std::string error;
    const std::optional<Body::PackageFiles> files = Body::readPackageFolder(folder.path(), error);
    ASSERT_TRUE(files.has_value()) << error;
    const std::filesystem::path pack = output.path() / "bad.edenpak";
    ASSERT_TRUE(Body::writePackageFile(*files, pack, error)) << error;

    const ToolResult fromFolder = runTool({ pathArg(folder.path()) });
    const ToolResult fromPack = runTool({ pathArg(pack) });
    EXPECT_EQ(fromPack.mExit, 1) << fromPack.mErr;
    EXPECT_EQ(fromFolder.mExit, fromPack.mExit);
    EXPECT_EQ(fromFolder.mOut, fromPack.mOut);

    TempFolder cleanFolder;
    writePackageFolder(cleanFolder.path(), humanoid());
    const std::optional<Body::PackageFiles> cleanFiles = Body::readPackageFolder(cleanFolder.path(), error);
    ASSERT_TRUE(cleanFiles.has_value()) << error;
    const std::filesystem::path cleanPack = output.path() / "clean.edenpak";
    ASSERT_TRUE(Body::writePackageFile(*cleanFiles, cleanPack, error)) << error;
    EXPECT_EQ(runTool({ pathArg(cleanPack) }).mExit, 0);
}

TEST(BodyTool, PackWithoutBodyJsonIsNotAPackage)
{
    TempFolder folder;
    Body::PackageFiles files;
    files.mFiles["readme.txt"] = { 'h', 'i' };
    std::string error;
    const std::filesystem::path pack = folder.path() / "empty.edenpak";
    ASSERT_TRUE(Body::writePackageFile(files, pack, error)) << error;
    const ToolResult result = runTool({ pathArg(pack) });
    EXPECT_NE(result.mExit, 0);
    EXPECT_FALSE(result.mErr.empty());
}

TEST(BodyTool, FilesThePackageNamesAreReadRelativeToTheFolder)
{
    Body::BodyPackage package = humanoid();
    const opas::anim::SkeletonProfile profile = *package.mBinding.mSkeletonProfile;
    package.mBinding.mSkeletonProfile.reset();
    package.mBinding.mSkeletonPath = "profiles/h.skeletonprofile.json";

    TempFolder withFile;
    writePackageFolder(withFile.path(), package);
    writeTextFile(withFile.path() / "profiles" / "h.skeletonprofile.json", opas::anim::writeSkeletonProfile(profile));
    const ToolResult found = runTool({ pathArg(withFile.path()) });
    EXPECT_EQ(found.mExit, 0) << found.mErr << found.mOut;
    EXPECT_EQ(found.mOut.find("BODY-BIND-005"), std::string::npos) << found.mOut;
    EXPECT_EQ(found.mOut.find("BODY-BIND-006"), std::string::npos) << found.mOut;

    TempFolder withoutFile;
    writePackageFolder(withoutFile.path(), package);
    const ToolResult missing = runTool({ pathArg(withoutFile.path()) });
    EXPECT_EQ(missing.mExit, 1) << missing.mOut;
    EXPECT_NE(missing.mOut.find("BODY-BIND-006"), std::string::npos) << missing.mOut;
}

TEST(BodyTool, SameInputGivesSameOutput)
{
    TempFolder folder;
    Body::BodyPackage package = humanoid();
    package.mIdentity.mVersion = "1.0";
    package.mTargets.push_back(Body::Target{});
    writePackageFolder(folder.path(), package);
    const ToolResult first = runTool({ pathArg(folder.path()) });
    const ToolResult second = runTool({ pathArg(folder.path()) });
    EXPECT_EQ(first.mExit, second.mExit);
    EXPECT_EQ(first.mOut, second.mOut);
    EXPECT_EQ(first.mErr, second.mErr);
}
