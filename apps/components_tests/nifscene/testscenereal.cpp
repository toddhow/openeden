#include <components/nifscene/build.hpp>

#include <gtest/gtest.h>

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <map>
#include <memory>
#include <sstream>
#include <string>
#include <vector>

// Task 0082: not run by default. With OPENEDEN_NIF_REPORT_DIRS set to folders (separated by ';') of the user's own
// NIF files and OPENEDEN_NIF_REPORT_OUT set to a file outside the repository, reads every .nif in them with the scene
// reader (every version) and writes AGGREGATES only: counts of files, shapes, vertices and so on. No path, name or
// vertex value is written (research note 0036's rule for real files). The files are only read.
namespace
{
    std::string lower(std::string text)
    {
        std::transform(
            text.begin(), text.end(), text.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
        return text;
    }

    // An error message without the file path it ends with.
    std::string withoutPath(const std::string& message)
    {
        const std::string marker = " when reading ";
        const std::size_t at = message.find(marker);
        return at == std::string::npos ? message : message.substr(0, at);
    }

    // A warning without the shape name it starts with: "shape 'x': text" becomes "shape: text".
    std::string withoutName(const std::string& warning)
    {
        const std::size_t open = warning.find('\'');
        const std::size_t close = warning.find('\'', open == std::string::npos ? 0 : open + 1);
        if (open == std::string::npos || close == std::string::npos)
            return warning;
        std::string text = warning.substr(0, open) + warning.substr(close + 1);
        // Numbers vary; keep the sentence.
        for (char& c : text)
        {
            if (c >= '0' && c <= '9')
                c = '#';
        }
        return text;
    }

    TEST(NifSceneReal, aggregates_of_the_users_files_when_asked)
    {
        const char* dirs = std::getenv("OPENEDEN_NIF_REPORT_DIRS");
        const char* out = std::getenv("OPENEDEN_NIF_REPORT_OUT");
        if (dirs == nullptr || out == nullptr)
            GTEST_SKIP() << "OPENEDEN_NIF_REPORT_DIRS and OPENEDEN_NIF_REPORT_OUT are not set";

        std::vector<std::filesystem::path> files;
        std::stringstream list(dirs);
        std::string dir;
        while (std::getline(list, dir, ';'))
        {
            if (dir.empty())
                continue;
            for (const auto& entry : std::filesystem::recursive_directory_iterator(dir))
            {
                if (entry.is_regular_file() && lower(entry.path().extension().string()) == ".nif")
                    files.push_back(entry.path());
            }
        }
        std::sort(files.begin(), files.end());

        NifScene::SceneOptions options;
        options.mRead.mAllowedVersions = Nif::VersionSet::Any;

        std::map<std::string, std::size_t> perStream; // "20.2.0.7 user 12 stream 100" to files
        std::map<std::string, std::size_t> errors;
        std::map<std::string, std::size_t> warnings;
        std::size_t read = 0, nodes = 0, shapes = 0, skinnedShapes = 0, zeroDataShapes = 0, filledFromPartition = 0,
                    emptyShapes = 0, vertices = 0, triangles = 0, bones = 0, influences = 0, partitions = 0,
                    nonIdentityPartitions = 0, outsideInfluences = 0, tangents = 0, uvs = 0, colors = 0,
                    filesWithZeroDataShapes = 0;
        float weightSumWorst = 0.f;
        for (const auto& file : files)
        {
            try
            {
                std::ifstream input(file, std::ios::binary);
                std::ostringstream bytes;
                bytes << input.rdbuf();
                auto stream = std::make_unique<std::istringstream>(bytes.str());
                stream->exceptions(std::ios::failbit | std::ios::badbit);
                const NifScene::Scene scene
                    = NifScene::readScene(std::move(stream), VFS::Path::Normalized("report.nif"), options);
                ++read;
                nodes += scene.mNodes.size();
                perStream["version " + std::to_string(scene.mVersion) + " user " + std::to_string(scene.mUserVersion)
                    + " stream " + std::to_string(scene.mBethVersion)]++;
                bool anyZeroData = false;
                for (const NifScene::Geometry& g : scene.mGeometries)
                {
                    ++shapes;
                    vertices += g.mPositions.size();
                    triangles += g.mTriangles.size() / 3;
                    influences += g.mInfluences.size();
                    tangents += g.mTangents.size();
                    uvs += g.mUV0.size();
                    colors += g.mColors.size();
                    if (g.mSkin != NifScene::sNone)
                        ++skinnedShapes;
                    if (g.mVerticesFromSkinPartition)
                    {
                        ++filledFromPartition;
                        ++zeroDataShapes;
                        anyZeroData = true;
                    }
                    if (g.mNoVertexData)
                        ++emptyShapes;
                    for (std::size_t v = 0; v + 1 < g.mInfluenceStart.size(); ++v)
                    {
                        float sum = 0.f;
                        for (std::uint32_t i = g.mInfluenceStart[v]; i < g.mInfluenceStart[v + 1]; ++i)
                            sum += g.mInfluences[i].mWeight;
                        if (g.mInfluenceStart[v] != g.mInfluenceStart[v + 1])
                            weightSumWorst = std::max(weightSumWorst, std::abs(sum - 1.f));
                    }
                }
                if (anyZeroData)
                    ++filesWithZeroDataShapes;
                for (const NifScene::Skin& skin : scene.mSkins)
                {
                    bones += skin.mBoneNames.size();
                    partitions += skin.mPartitions.size();
                    nonIdentityPartitions += skin.mNonIdentityPartitionBoneMaps;
                    outsideInfluences += skin.mInfluencesOutsidePartitionBones;
                }
                for (const std::string& warning : scene.mWarnings)
                    warnings[withoutName(warning)]++;
            }
            catch (const std::exception& e)
            {
                errors[withoutPath(e.what())]++;
            }
        }

        std::ofstream report(out);
        report << "files " << files.size() << " read " << read << " failed " << files.size() - read << "\n";
        for (const auto& [label, count] : perStream)
            report << "  " << label << ": " << count << " files\n";
        report << "nodes " << nodes << " shapes " << shapes << " skinned " << skinnedShapes << "\n";
        report << "shapes with Data Size 0 filled from the skin partition " << filledFromPartition << " in "
               << filesWithZeroDataShapes << " files; shapes with no vertex data at all " << emptyShapes << "\n";
        report << "vertices " << vertices << " triangles " << triangles << " tangents " << tangents << " uv0 " << uvs
               << " colors " << colors << "\n";
        report << "skin bones (summed) " << bones << " partitions " << partitions << " influences " << influences
               << "\n";
        report << "partitions with a bone list that is not the identity " << nonIdentityPartitions
               << "; influences on bones outside their partition's list " << outsideInfluences << "\n";
        report << "worst |sum of weights - 1| over vertices with influences " << weightSumWorst << "\n";
        for (const auto& [text, count] : errors)
            report << "error x" << count << ": " << text << "\n";
        for (const auto& [text, count] : warnings)
            report << "warning x" << count << ": " << text << "\n";
        EXPECT_TRUE(report.good());
    }
}
