// Task 0063 (AdvancedBodies B1), decision D1: a body package as a folder or as one .edenpak, and the package digest.
// Written from the task spec and bodypack.hpp only.

#include "bodyfixtures.hpp"

#include <components/body/bodyjson.hpp>
#include <components/body/bodypack.hpp>

#include <edenarchive/archive.hpp>
#include <edenarchive/hash.hpp>

#include <gtest/gtest.h>

#include <cstdint>
#include <filesystem>
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace
{
    using namespace BodyTest;

    std::vector<std::uint8_t> bytesOf(const std::string& text)
    {
        return std::vector<std::uint8_t>(text.begin(), text.end());
    }

    /// A small package: body.json, a synthetic binary "mesh" in a sub folder and a text file in a deeper one.
    Body::PackageFiles samplePackage()
    {
        Body::PackageFiles files;
        files.mFiles["body.json"] = bytesOf(Body::writeBodyPackage(humanoid()));
        std::vector<std::uint8_t> mesh(5000);
        for (std::size_t i = 0; i < mesh.size(); ++i)
        {
            mesh[i] = static_cast<std::uint8_t>((i * 31 + 7) & 0xFF);
        }
        files.mFiles["mesh/body.glb"] = mesh;
        files.mFiles["profiles/extra/readme.txt"] = bytesOf("synthetic test data, no game asset\n");
        return files;
    }

    void writeFolder(const std::filesystem::path& folder, const Body::PackageFiles& files)
    {
        for (const auto& [path, content] : files.mFiles)
        {
            const std::filesystem::path target = folder / std::filesystem::path(path);
            std::filesystem::create_directories(target.parent_path());
            writeTextFile(target, std::string(content.begin(), content.end()));
        }
    }

    std::string manifestDigestOf(const std::filesystem::path& pack)
    {
        EdenArchive::OpenOptions options;
        EdenArchive::Error error;
        const std::unique_ptr<EdenArchive::Archive> archive = EdenArchive::Archive::open(pack, options, error);
        if (archive == nullptr)
        {
            ADD_FAILURE() << "cannot open " << pack.string() << ": " << error.mMessage;
            return {};
        }
        return EdenArchive::toHex(archive->info().mManifestDigest);
    }
}

TEST(BodyPack, FolderIsReadWithForwardSlashKeys)
{
    TempFolder folder;
    const Body::PackageFiles expected = samplePackage();
    writeFolder(folder.path(), expected);
    std::string error;
    const std::optional<Body::PackageFiles> read = Body::readPackageFolder(folder.path(), error);
    ASSERT_TRUE(read.has_value()) << error;
    EXPECT_EQ(read->mFiles, expected.mFiles);
}

TEST(BodyPack, FolderToPackToFilesRoundTrip)
{
    TempFolder folder;
    const Body::PackageFiles expected = samplePackage();
    std::string error;
    const std::filesystem::path pack = folder.path() / "sample.edenpak";
    ASSERT_TRUE(Body::writePackageFile(expected, pack, error)) << error;
    EXPECT_TRUE(std::filesystem::exists(pack));
    const std::optional<Body::PackageFiles> read = Body::readPackageFile(pack, error);
    ASSERT_TRUE(read.has_value()) << error;
    EXPECT_EQ(read->mFiles, expected.mFiles);
}

TEST(BodyPack, FolderPackFolderKeepsTheBodyJsonReadable)
{
    TempFolder source;
    TempFolder output;
    const Body::BodyPackage package = humanoid();
    Body::PackageFiles files;
    files.mFiles["body.json"] = bytesOf(Body::writeBodyPackage(package));
    writeFolder(source.path(), files);

    std::string error;
    const std::optional<Body::PackageFiles> fromFolder = Body::readPackageFolder(source.path(), error);
    ASSERT_TRUE(fromFolder.has_value()) << error;
    const std::filesystem::path pack = output.path() / "humanoid.edenpak";
    ASSERT_TRUE(Body::writePackageFile(*fromFolder, pack, error)) << error;
    const std::optional<Body::PackageFiles> fromPack = Body::readPackageFile(pack, error);
    ASSERT_TRUE(fromPack.has_value()) << error;
    ASSERT_EQ(fromPack->mFiles.count("body.json"), 1u);
    const std::vector<std::uint8_t>& bytes = fromPack->mFiles.at("body.json");
    const Body::BodyLoadResult loaded = Body::readBodyPackage(std::string(bytes.begin(), bytes.end()), "body.json");
    ASSERT_TRUE(loaded.mValue.has_value()) << loaded.mError.toString();
    EXPECT_EQ(*loaded.mValue, package);
}

TEST(BodyPack, PackageDigestEqualsTheManifestDigestOfThePack)
{
    TempFolder folder;
    const Body::PackageFiles files = samplePackage();
    std::string error;
    const std::string digest = Body::packageDigest(files, error);
    ASSERT_FALSE(digest.empty()) << error;
    EXPECT_EQ(digest.size() % 2, 0u);
    for (const char c : digest)
    {
        EXPECT_TRUE((c >= '0' && c <= '9') || (c >= 'a' && c <= 'f')) << "not lowercase hex: " << digest;
    }

    const std::filesystem::path pack = folder.path() / "sample.edenpak";
    ASSERT_TRUE(Body::writePackageFile(files, pack, error)) << error;
    EXPECT_EQ(manifestDigestOf(pack), digest);
}

TEST(BodyPack, FolderAndPackGiveTheSameDigest)
{
    TempFolder folder;
    TempFolder output;
    writeFolder(folder.path(), samplePackage());
    std::string error;
    const std::optional<Body::PackageFiles> fromFolder = Body::readPackageFolder(folder.path(), error);
    ASSERT_TRUE(fromFolder.has_value()) << error;
    const std::string folderDigest = Body::packageDigest(*fromFolder, error);
    ASSERT_FALSE(folderDigest.empty()) << error;

    const std::filesystem::path pack = output.path() / "sample.edenpak";
    ASSERT_TRUE(Body::writePackageFile(*fromFolder, pack, error)) << error;
    const std::optional<Body::PackageFiles> fromPack = Body::readPackageFile(pack, error);
    ASSERT_TRUE(fromPack.has_value()) << error;
    EXPECT_EQ(Body::packageDigest(*fromPack, error), folderDigest);
    EXPECT_EQ(manifestDigestOf(pack), folderDigest);
}

TEST(BodyPack, DigestIsStableAndChangesWithContentAndPaths)
{
    const Body::PackageFiles files = samplePackage();
    std::string error;
    const std::string digest = Body::packageDigest(files, error);
    ASSERT_FALSE(digest.empty()) << error;
    EXPECT_EQ(Body::packageDigest(files, error), digest);
    EXPECT_EQ(Body::packageDigest(samplePackage(), error), digest);

    {
        Body::PackageFiles changed = files;
        changed.mFiles["mesh/body.glb"][2500] ^= 0x01;
        EXPECT_NE(Body::packageDigest(changed, error), digest) << "one flipped bit";
    }
    {
        Body::PackageFiles changed = files;
        changed.mFiles["mesh/body.glb"].push_back(0);
        EXPECT_NE(Body::packageDigest(changed, error), digest) << "one more byte";
    }
    {
        Body::PackageFiles changed = files;
        changed.mFiles["mesh/body2.glb"] = changed.mFiles["mesh/body.glb"];
        changed.mFiles.erase("mesh/body.glb");
        EXPECT_NE(Body::packageDigest(changed, error), digest) << "same bytes under another path";
    }
    {
        Body::PackageFiles changed = files;
        changed.mFiles["extra.txt"] = bytesOf("x");
        EXPECT_NE(Body::packageDigest(changed, error), digest) << "an added file";
    }
    {
        Body::PackageFiles changed = files;
        changed.mFiles.erase("profiles/extra/readme.txt");
        EXPECT_NE(Body::packageDigest(changed, error), digest) << "a removed file";
    }
    {
        Body::PackageFiles changed = files;
        changed.mFiles["body.json"][0] = static_cast<std::uint8_t>(changed.mFiles["body.json"][0] ^ 0x20);
        EXPECT_NE(Body::packageDigest(changed, error), digest) << "first byte of body.json";
    }
}

TEST(BodyPack, PackingIsDeterministic)
{
    TempFolder folder;
    const Body::PackageFiles files = samplePackage();
    std::string error;
    ASSERT_TRUE(Body::writePackageFile(files, folder.path() / "a.edenpak", error)) << error;
    ASSERT_TRUE(Body::writePackageFile(files, folder.path() / "b.edenpak", error)) << error;
    EXPECT_EQ(readTextFile(folder.path() / "a.edenpak"), readTextFile(folder.path() / "b.edenpak"));
}

TEST(BodyPack, LargerFilesSurviveTheRoundTrip)
{
    TempFolder folder;
    Body::PackageFiles files;
    std::vector<std::uint8_t> big(3u * 1024 * 1024 + 17);
    std::uint32_t state = 99;
    for (std::uint8_t& byte : big)
    {
        state = state * 1664525u + 1013904223u;
        byte = static_cast<std::uint8_t>(state >> 24);
    }
    files.mFiles["mesh/big.bin"] = big;
    files.mFiles["body.json"] = bytesOf(Body::writeBodyPackage(humanoid()));
    std::string error;
    const std::filesystem::path pack = folder.path() / "big.edenpak";
    ASSERT_TRUE(Body::writePackageFile(files, pack, error)) << error;
    const std::optional<Body::PackageFiles> read = Body::readPackageFile(pack, error);
    ASSERT_TRUE(read.has_value()) << error;
    EXPECT_EQ(read->mFiles, files.mFiles);
    EXPECT_EQ(manifestDigestOf(pack), Body::packageDigest(files, error));
}

TEST(BodyPack, MissingFolderIsAnError)
{
    TempFolder folder;
    std::string error;
    const std::optional<Body::PackageFiles> read = Body::readPackageFolder(folder.path() / "does_not_exist", error);
    EXPECT_FALSE(read.has_value());
    EXPECT_FALSE(error.empty());
    EXPECT_EQ(error.find('\n'), std::string::npos) << "one line";
}

TEST(BodyPack, MissingOrDamagedPackIsAnError)
{
    TempFolder folder;
    std::string error;
    EXPECT_FALSE(Body::readPackageFile(folder.path() / "nope.edenpak", error).has_value());
    EXPECT_FALSE(error.empty());

    writeTextFile(folder.path() / "garbage.edenpak", "this is not an eden archive at all");
    error.clear();
    EXPECT_FALSE(Body::readPackageFile(folder.path() / "garbage.edenpak", error).has_value());
    EXPECT_FALSE(error.empty());

    // A real pack with its middle damaged must not read as the original.
    const Body::PackageFiles files = samplePackage();
    ASSERT_TRUE(Body::writePackageFile(files, folder.path() / "good.edenpak", error)) << error;
    std::string text = readTextFile(folder.path() / "good.edenpak");
    ASSERT_GT(text.size(), 100u);
    for (std::size_t position = 0; position < text.size(); position += text.size() / 40 + 1)
    {
        std::string damaged = text;
        damaged[position] = static_cast<char>(damaged[position] ^ 0x55);
        writeTextFile(folder.path() / "damaged.edenpak", damaged);
        error.clear();
        const std::optional<Body::PackageFiles> read = Body::readPackageFile(folder.path() / "damaged.edenpak", error);
        if (read.has_value())
        {
            // Padding bytes may be unchecked; whatever is returned must then be the original content.
            EXPECT_EQ(read->mFiles, files.mFiles) << "damage at " << position;
        }
        else
        {
            EXPECT_FALSE(error.empty()) << "damage at " << position;
        }
    }
}

#ifndef _WIN32
TEST(BodyPack, UnsafeFileNamesInAFolderAreRejected)
{
    TempFolder folder;
    writeTextFile(folder.path() / "body.json", "{}");
    writeTextFile(folder.path() / "bad\\name.txt", "x");
    std::string error;
    EXPECT_FALSE(Body::readPackageFolder(folder.path(), error).has_value());
    EXPECT_FALSE(error.empty());
}
#endif

TEST(BodyPack, LimitsAreSet)
{
    EXPECT_GT(Body::PackLimits::sMaxFiles, 0u);
    EXPECT_GT(Body::PackLimits::sMaxTotalBytes, 0u);
}
