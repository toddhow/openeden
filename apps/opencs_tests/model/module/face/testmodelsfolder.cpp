// Task 0147: the Audio2Face models folder: README on first use, variant detection by file names and sizes, hash
// verification with progress and cancel. Uses generated stand-in files: no model is read from or copied into the
// repository. The recorded hashes themselves were computed from toddhow's downloads (task 0071 stage log).
#include "editor_modules/face_editor/models_folder.hpp"

#include <components/face/sha256.hpp>

#include <gtest/gtest.h>

#include <filesystem>
#include <fstream>

namespace
{
    using namespace face_editor;
    namespace fs = std::filesystem;

    fs::path scratch(const char* name)
    {
        fs::path p = fs::temp_directory_path() / ("oe_face_models_" + std::string(name));
        fs::remove_all(p);
        fs::create_directories(p);
        return p;
    }

    void writeFile(const fs::path& p, std::uint64_t size, char fill = 'x')
    {
        fs::create_directories(p.parent_path());
        std::ofstream out(p, std::ios::binary);
        std::string chunk(4096, fill);
        for (std::uint64_t left = size; left > 0;)
        {
            const std::uint64_t n = std::min<std::uint64_t>(left, chunk.size());
            out.write(chunk.data(), static_cast<std::streamsize>(n));
            left -= n;
        }
    }

    const VariantSpec* find(const std::string& id)
    {
        for (const auto& v : knownVariants())
            if (v.mId == id)
                return &v;
        return nullptr;
    }
}

TEST(FaceModels, theKnownVariantsAreV30V23MarkAndTheEmotionModel)
{
    ASSERT_EQ(knownVariants().size(), 3u);
    EXPECT_EQ(knownVariants()[0].mId, "v3.0");
    EXPECT_EQ(knownVariants()[1].mId, "v2.3-mark");
    EXPECT_EQ(knownVariants()[2].mId, "audio2emotion");
    EXPECT_TRUE(knownVariants()[2].mOptional);
    EXPECT_FALSE(knownVariants()[0].mOptional);
}

TEST(FaceModels, theRecordedNetworkHashesAndSizesAreTheOnesMeasured)
{
    const VariantSpec* v23 = find("v2.3-mark");
    const VariantSpec* v30 = find("v3.0");
    const VariantSpec* emo = find("audio2emotion");
    ASSERT_TRUE(v23 && v30 && emo);
    auto net = [](const VariantSpec& s) -> const ExpectedFile* {
        for (const auto& f : s.mFiles)
            if (f.mName == "network.onnx")
                return &f;
        return nullptr;
    };
    ASSERT_TRUE(net(*v23) && net(*v30) && net(*emo));
    EXPECT_EQ(net(*v23)->mSize, 75751144u);
    EXPECT_EQ(net(*v23)->mSha256, "dcd16d3b1affb090d4da1ba88791faa6bcd755f97c853b3b667abad9be15cb4b");
    EXPECT_EQ(net(*v30)->mSize, 724844664u);
    EXPECT_EQ(net(*v30)->mSha256, "db47c2701ca849de443c9e9f25657210f829a74fc458ee6fed603a8a501253a8");
    EXPECT_EQ(net(*emo)->mSize, 1271318264u);
    EXPECT_EQ(net(*emo)->mSha256, "92eab06c234b78ee3d5efe4762d0ac198cf39e085f942fe8e1065120e3d411b7");
}

TEST(FaceModels, everyRecordedHashIsLowerCaseHexOf64Characters)
{
    for (const auto& v : knownVariants())
        for (const auto& f : v.mFiles)
            if (!f.mSha256.empty())
            {
                EXPECT_EQ(f.mSha256.size(), 64u) << f.mName;
                for (char c : f.mSha256)
                    EXPECT_TRUE((c >= '0' && c <= '9') || (c >= 'a' && c <= 'f')) << f.mName;
            }
}

TEST(FaceModels, theRootIsUnderTheUserDataFolder)
{
    EXPECT_EQ(modelsRoot("U"), fs::path("U") / "models" / "audio2face");
}

TEST(FaceModels, firstUseWritesTheReadmeAndLeavesAnEditedOneAlone)
{
    fs::path root = scratch("readme") / "models" / "audio2face";
    std::string error;
    ASSERT_TRUE(ensureModelsFolder(root, error)) << error;
    ASSERT_TRUE(fs::exists(root / "README.txt"));
    std::ifstream in(root / "README.txt");
    std::string text((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    EXPECT_EQ(text, readmeText());
    EXPECT_NE(text.find("NVIDIA Open Model License"), std::string::npos);
    EXPECT_NE(text.find("Audio2Emotion"), std::string::npos);
    EXPECT_NE(text.find("dcd16d3b1affb090d4da1ba88791faa6bcd755f97c853b3b667abad9be15cb4b"), std::string::npos);
    EXPECT_NE(text.find("v2.3-mark"), std::string::npos);

    { std::ofstream(root / "README.txt") << "my notes"; }
    ASSERT_TRUE(ensureModelsFolder(root, error));
    std::ifstream again(root / "README.txt");
    std::string kept((std::istreambuf_iterator<char>(again)), std::istreambuf_iterator<char>());
    EXPECT_EQ(kept, "my notes");
}

TEST(FaceModels, aFolderThatCannotBeCreatedIsAnErrorNotACrash)
{
    fs::path base = scratch("blocked");
    { std::ofstream(base / "file") << "x"; }
    std::string error;
    EXPECT_FALSE(ensureModelsFolder(base / "file" / "sub", error));
    EXPECT_FALSE(error.empty());
}

TEST(FaceModels, scanReportsMissingIncompleteWrongSizeAndPresent)
{
    fs::path root = scratch("scan");
    auto statuses = scanModels(root);
    ASSERT_EQ(statuses.size(), 3u);
    for (const auto& s : statuses)
        EXPECT_EQ(s.mState, VariantState::Missing);

    // One required file of v2.3 only: incomplete, and the detail names a missing file.
    const VariantSpec* v23 = find("v2.3-mark");
    writeFile(root / "v2.3-mark" / v23->mFiles[1].mName, v23->mFiles[1].mSize);
    statuses = scanModels(root);
    EXPECT_EQ(statuses[1].mState, VariantState::Incomplete);
    EXPECT_FALSE(statuses[1].mDetail.empty());

    // Every small file at its size, the big ones too (sparse writes would be fine, but write real zeros small enough:
    // use the real sizes except network.onnx, which we set one byte short).
    for (const auto& f : v23->mFiles)
        if (f.mName != "network.onnx" && f.mSize < 10u * 1024 * 1024)
            writeFile(root / "v2.3-mark" / f.mName, f.mSize);
    // The two large files: resize without writing (sparse files via resize_file).
    for (const auto& f : v23->mFiles)
        if (f.mSize >= 10u * 1024 * 1024)
        {
            { std::ofstream(root / "v2.3-mark" / f.mName, std::ios::binary) << "x"; }
            fs::resize_file(root / "v2.3-mark" / f.mName, f.mName == "network.onnx" ? f.mSize - 1 : f.mSize);
        }
    statuses = scanModels(root);
    EXPECT_EQ(statuses[1].mState, VariantState::WrongSize);
    EXPECT_NE(statuses[1].mDetail.find("network.onnx"), std::string::npos);

    for (const auto& f : v23->mFiles)
        if (f.mName == "network.onnx")
            fs::resize_file(root / "v2.3-mark" / f.mName, f.mSize);
    statuses = scanModels(root);
    EXPECT_EQ(statuses[1].mState, VariantState::Present);
    EXPECT_EQ(statuses[0].mState, VariantState::Missing);
}

TEST(FaceModels, aDirectoryWhereAFileShouldBeIsNotPresent)
{
    fs::path root = scratch("dirfile");
    const VariantSpec* emo = find("audio2emotion");
    for (const auto& f : emo->mFiles)
        fs::create_directories(root / "audio2emotion" / f.mName);
    auto statuses = scanModels(root);
    EXPECT_NE(statuses[2].mState, VariantState::Present);
    EXPECT_NE(statuses[2].mState, VariantState::Verified);
}

TEST(FaceModels, verifyHashesAFileAgainstTheRecordedDigestWithProgressAndCancel)
{
    // A test variant with a generated file whose digest we compute with our own SHA-256.
    fs::path root = scratch("verify");
    VariantSpec spec;
    spec.mId = "t";
    spec.mLabel = "t";
    const std::string content(3 * 1024 * 1024 + 17, 'q');
    spec.mFiles.push_back({ "network.onnx", content.size(), Face::sha256Hex(content) });
    {
        fs::create_directories(root / "t");
        std::ofstream out(root / "t" / "network.onnx", std::ios::binary);
        out.write(content.data(), static_cast<std::streamsize>(content.size()));
    }
    std::vector<double> seen;
    VariantStatus ok = verifyVariant(root, spec, [&](double f) {
        seen.push_back(f);
        return true;
    });
    EXPECT_EQ(ok.mState, VariantState::Verified);
    ASSERT_FALSE(seen.empty());
    EXPECT_NEAR(seen.back(), 1.0, 1e-9);
    for (std::size_t i = 1; i < seen.size(); ++i)
        EXPECT_GE(seen[i], seen[i - 1]);

    spec.mFiles[0].mSha256 = std::string(64, '0');
    EXPECT_EQ(verifyVariant(root, spec, [](double) { return true; }).mState, VariantState::HashMismatch);

    spec.mFiles[0].mSha256 = Face::sha256Hex(content);
    EXPECT_EQ(verifyVariant(root, spec, [](double) { return false; }).mState, VariantState::Present);

    fs::remove(root / "t" / "network.onnx");
    EXPECT_EQ(verifyVariant(root, spec, [](double) { return true; }).mState, VariantState::Incomplete);
}
