#include <components/nifscene/build.hpp>

#include <gtest/gtest.h>

#include <algorithm>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <map>
#include <memory>
#include <sstream>
#include <string>
#include <vector>

// Task 0082, item 4: not run by default. With OPENEDEN_NIF_REPORT_DIRS set to folders (separated by ';') of the user's
// own NIF files and OPENEDEN_NIF_REPORT_OUT_MATERIALS set to a file outside the repository, reads every .nif in them
// with the scene reader (every version) and writes AGGREGATES only: shader kinds and types, texture set slot usage,
// alpha property modes, and how many shapes have complete material data. No path, name or texture name is written.
namespace
{
    std::string lower(std::string text)
    {
        std::transform(
            text.begin(), text.end(), text.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
        return text;
    }

    bool endsWith(const std::string& text, const std::string& suffix)
    {
        const std::string t = lower(text);
        return t.size() >= suffix.size() && t.compare(t.size() - suffix.size(), suffix.size(), suffix) == 0;
    }

    // A warning without the shape name and with numbers hidden: "shape 'x': ... record 12 is a Y" becomes
    // "shape : ... record # is a Y".
    std::string withoutName(const std::string& warning)
    {
        const std::size_t open = warning.find('\'');
        const std::size_t close = warning.find('\'', open == std::string::npos ? 0 : open + 1);
        std::string text = (open == std::string::npos || close == std::string::npos)
            ? warning
            : warning.substr(0, open) + warning.substr(close + 1);
        for (char& c : text)
        {
            if (c >= '0' && c <= '9')
                c = '#';
        }
        return text;
    }

    TEST(NifSceneRealMaterials, aggregates_of_the_users_files_when_asked)
    {
        const char* dirs = std::getenv("OPENEDEN_NIF_REPORT_DIRS");
        const char* out = std::getenv("OPENEDEN_NIF_REPORT_OUT_MATERIALS");
        if (dirs == nullptr || out == nullptr)
            GTEST_SKIP() << "OPENEDEN_NIF_REPORT_DIRS and OPENEDEN_NIF_REPORT_OUT_MATERIALS are not set";

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

        std::map<std::string, std::size_t> errors;
        std::map<std::string, std::size_t> warnings;
        std::map<std::string, std::size_t> perStream; // stream version to shapes
        std::map<std::string, std::size_t> kinds; // "stream N lighting/effect"
        std::map<std::string, std::size_t> lightingTypes; // "stream N type T"
        std::map<std::string, std::size_t> setSizes; // "stream N slots S"
        std::map<std::string, std::size_t> slotUsed; // "stream N slot S" to texture sets with a non-empty path
        std::map<std::string, std::size_t> alphaModes; // "blend s/d test mode"
        std::map<std::string, std::size_t> clampModes;
        std::map<std::string, std::size_t> flag2DoubleSided;
        std::size_t filesRead = 0, shapes = 0, shapesWithMaterial = 0, shapesWithAlpha = 0, shapesWithoutShader = 0,
                    complete = 0, incomplete = 0, materialFileNames = 0, effectWithSource = 0, effects = 0,
                    setsReferenced = 0, setsWithBase = 0, setsWithNormal = 0;
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
                ++filesRead;
                const std::string streamName = "stream " + std::to_string(scene.mBethVersion);
                for (const std::string& warning : scene.mWarnings)
                    ++warnings[withoutName(warning)];
                for (const NifScene::Geometry& g : scene.mGeometries)
                {
                    ++shapes;
                    ++perStream[streamName];
                    if (g.mAlpha != NifScene::sNone)
                    {
                        ++shapesWithAlpha;
                        const NifScene::AlphaProperty& a = scene.mAlphaProperties[g.mAlpha];
                        ++alphaModes[std::string("blend ") + (a.mBlend ? "on " : "off ")
                            + std::to_string(a.mSourceBlend) + "/" + std::to_string(a.mDestinationBlend) + " test "
                            + (a.mTest ? "on " : "off ") + std::to_string(a.mTestMode)];
                    }
                    if (g.mMaterial == NifScene::sNone)
                    {
                        ++shapesWithoutShader;
                        continue;
                    }
                    ++shapesWithMaterial;
                    const NifScene::ShaderMaterial& m = scene.mMaterials[g.mMaterial];
                    ++kinds[streamName + (m.mKind == NifScene::ShaderKind::Lighting ? " lighting" : " effect")];
                    ++clampModes["clamp " + std::to_string(m.mClampMode)];
                    ++flag2DoubleSided[(m.mFlags2 & 0x10) != 0 ? "double sided" : "single sided"];
                    bool isComplete = false;
                    if (m.mKind == NifScene::ShaderKind::Lighting)
                    {
                        ++lightingTypes[streamName + " type " + std::to_string(m.mShaderType)];
                        const bool named = endsWith(m.mName, ".bgsm");
                        if (named)
                            ++materialFileNames;
                        if (m.mTextureSet != NifScene::sNone)
                        {
                            const NifScene::TextureSet& set = scene.mTextureSets[m.mTextureSet];
                            ++setsReferenced;
                            ++setSizes[streamName + " slots " + std::to_string(set.mTextures.size())];
                            for (std::size_t slot = 0; slot < set.mTextures.size(); ++slot)
                            {
                                if (!set.mTextures[slot].empty())
                                    ++slotUsed[streamName + " slot " + std::to_string(slot)];
                            }
                            if (!set.mTextures.empty() && !set.mTextures[0].empty())
                                ++setsWithBase;
                            if (set.mTextures.size() > 1 && !set.mTextures[1].empty())
                                ++setsWithNormal;
                            isComplete = !set.mTextures.empty() && !set.mTextures[0].empty();
                        }
                        // Fallout 4: the textures live in the material file the property names.
                        if (named)
                            isComplete = true;
                    }
                    else
                    {
                        ++effects;
                        if (endsWith(m.mName, ".bgem"))
                            ++materialFileNames;
                        if (!m.mSourceTexture.empty())
                            ++effectWithSource;
                        isComplete = !m.mSourceTexture.empty() || endsWith(m.mName, ".bgem");
                    }
                    if (isComplete)
                        ++complete;
                    else
                        ++incomplete;
                }
            }
            catch (const std::exception& e)
            {
                std::string message = e.what();
                const std::size_t at = message.find(" when reading ");
                if (at != std::string::npos)
                    message = message.substr(0, at);
                for (char& c : message)
                {
                    if (c >= '0' && c <= '9')
                        c = '#';
                }
                ++errors[message];
            }
        }

        std::ofstream report(out);
        auto printMap = [&report](const char* title, const std::map<std::string, std::size_t>& values) {
            report << title << "\n";
            for (const auto& [key, count] : values)
                report << "  " << key << ": " << count << "\n";
        };
        report << "files " << files.size() << " read " << filesRead << "\n";
        printMap("errors", errors);
        printMap("warnings", warnings);
        printMap("shapes by stream version", perStream);
        report << "shapes " << shapes << " with a material " << shapesWithMaterial
               << " without a material (no shader, or a "
               << "kind the scene does not read) " << shapesWithoutShader << " with an alpha property "
               << shapesWithAlpha << "\n";
        printMap("shader kinds", kinds);
        printMap("lighting shader types", lightingTypes);
        printMap("texture set sizes", setSizes);
        printMap("texture set slots with a path", slotUsed);
        printMap("alpha property modes", alphaModes);
        printMap("clamp modes", clampModes);
        printMap("double sided flag", flag2DoubleSided);
        report << "lighting shader texture sets referenced " << setsReferenced << " with a base texture "
               << setsWithBase << " with a normal map " << setsWithNormal << "\n";
        report << "effect shaders " << effects << " with a source texture " << effectWithSource << "\n";
        report << "material file names (.bgsm, .bgem) " << materialFileNames << "\n";
        report << "shapes with complete material data " << complete << " incomplete " << incomplete << "\n";
        EXPECT_TRUE(report.good());
    }
}
