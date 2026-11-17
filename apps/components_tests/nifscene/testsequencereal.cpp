// Task 0232: not run by default. With OPENEDEN_SKYRIMSE_DATA naming Skyrim SE's Data folder it reads every .nif of
// the five masters' BSAs and prints AGGREGATE numbers about controller sequences: how many models have one, their
// names, the interpolator block types they use, and which folders hold the models with an Open sequence. No file
// content is written. The files are only read.

#include <components/bsa/compressedbsafile.hpp>
#include <components/nif/controller.hpp>
#include <components/nif/niffile.hpp>
#include <components/nifscene/build.hpp>
#include <components/nifscene/sequence.hpp>
#include <components/vfs/pathutil.hpp>

#include <gtest/gtest.h>

#include <algorithm>
#include <cstdlib>
#include <filesystem>
#include <iostream>
#include <iterator>
#include <map>
#include <sstream>
#include <string>
#include <vector>

namespace
{
    namespace fs = std::filesystem;

    std::string lower(std::string s)
    {
        std::transform(
            s.begin(), s.end(), s.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
        return s;
    }

    bool endsWith(const std::string& text, const std::string& suffix)
    {
        return text.size() >= suffix.size() && text.compare(text.size() - suffix.size(), suffix.size(), suffix) == 0;
    }

    void line(const std::string& text)
    {
        std::cout << "SEQ-SURVEY " << text << std::endl;
    }

    // The first three path components, "meshes/dungeons/misc".
    std::string folderOf(const std::string& path)
    {
        std::size_t at = 0;
        for (int i = 0; i < 3; ++i)
        {
            const std::size_t next = path.find_first_of("/\\", at);
            if (next == std::string::npos)
                return path.substr(0, path.find_last_of("/\\"));
            at = next + 1;
        }
        return path.substr(0, at - 1);
    }

    TEST(SkyrimSequencesReal, aggregates_of_the_users_meshes_when_asked)
    {
        const char* data = std::getenv("OPENEDEN_SKYRIMSE_DATA");
        if (data == nullptr || !fs::is_directory(data))
            GTEST_SKIP() << "OPENEDEN_SKYRIMSE_DATA does not name a Data folder";

        std::vector<fs::path> archives;
        std::error_code ec;
        for (fs::directory_iterator it(data, ec), end; !ec && it != end; it.increment(ec))
        {
            if (!it->is_regular_file(ec) || lower(it->path().extension().string()) != ".bsa")
                continue;
            const std::string stem = lower(it->path().filename().string());
            for (const char* prefix : { "skyrim - ", "update", "dawnguard", "hearthfires", "dragonborn" })
                if (stem.rfind(prefix, 0) == 0)
                {
                    archives.push_back(it->path());
                    break;
                }
        }
        std::sort(archives.begin(), archives.end());

        std::uint64_t files = 0, failed = 0, withSequences = 0, withOpen = 0, withOpenAndClose = 0;
        std::uint64_t tracks = 0, unsupportedBlocks = 0, openUnsupported = 0, openFullySupported = 0;
        std::uint64_t openWithTranslation = 0, openWithRotation = 0, openWithScale = 0;
        std::map<std::string, std::uint64_t> sequenceNames, interpolatorTypes, folders, interpolation;
        std::map<std::string, std::uint64_t> cycle;
        std::uint64_t maxTracks = 0, eulerAxis[3] = {}, quatTracks = 0, doorNamed = 0, containerNamed = 0,
                      otherNamed = 0;
        std::map<std::string, std::uint64_t>
            motion; // 'door'/'container'/'other' + a bucket of the largest turn or slide
        std::map<std::string, std::vector<std::string>> bigContainers;
        double longest = 0.0, shortest = 1e9;
        // Models with Open that have a track whose node is not found in the scene.
        std::uint64_t openWithMissingNode = 0;

        const NifScene::SceneOptions options;
        for (const fs::path& archivePath : archives)
        {
            try
            {
                Bsa::CompressedBSAFile archive;
                archive.open(archivePath);
                for (const auto& entry : archive.getList())
                {
                    const std::string name = lower(std::string(entry.name()));
                    if (!endsWith(name, ".nif"))
                        continue;
                    ++files;
                    try
                    {
                        auto stream = archive.getFile(&entry);
                        std::string bytes((std::istreambuf_iterator<char>(*stream)), std::istreambuf_iterator<char>());
                        // The raw records, for the interpolator block types.
                        {
                            Files::IStreamPtr input(new std::istringstream(bytes, std::ios::binary));
                            Nif::NIFFile file(VFS::Path::Normalized("survey.nif"));
                            Nif::Reader reader(file, nullptr);
                            Nif::ReadOptions readOptions;
                            readOptions.mAllowedVersions = Nif::VersionSet::Any;
                            reader.parse(std::move(input), readOptions);
                            for (const auto& record : file.mRecords)
                            {
                                const auto* sequence = dynamic_cast<const Nif::NiControllerSequence*>(record.get());
                                if (sequence == nullptr)
                                    continue;
                                for (const Nif::ControlledBlock& block : sequence->mControlledBlocks)
                                    ++interpolatorTypes[block.mInterpolator.empty()
                                            ? std::string("(none)")
                                            : std::string(block.mInterpolator.getPtr()->mRecordName)];
                            }
                        }
                        Files::IStreamPtr input(new std::istringstream(std::move(bytes), std::ios::binary));
                        const NifScene::Scene scene
                            = NifScene::readScene(std::move(input), VFS::Path::Normalized("survey.nif"), options);
                        if (scene.mSequences.empty())
                            continue;
                        ++withSequences;
                        bool hasOpen = false, hasClose = false;
                        for (const NifScene::Sequence& sequence : scene.mSequences)
                        {
                            const std::string sname = lower(sequence.mName);
                            ++sequenceNames[sname];
                            tracks += sequence.mTracks.size();
                            unsupportedBlocks += sequence.mUnsupportedBlocks;
                            maxTracks = std::max<std::uint64_t>(maxTracks, sequence.mTracks.size());
                            ++cycle[std::to_string(sequence.mCycleType)];
                            const double d = NifScene::Sequences::duration(sequence);
                            if (sname == "open")
                            {
                                hasOpen = true;
                                longest = std::max(longest, d);
                                shortest = std::min(shortest, d);
                                if (sequence.mUnsupportedBlocks > 0)
                                    ++openUnsupported;
                                else
                                    ++openFullySupported;
                                bool t = false, r = false, s = false;
                                for (const NifScene::Track& track : sequence.mTracks)
                                {
                                    t = t || !track.mTranslations.empty();
                                    r = r || !track.mRotations.empty() || !track.mEuler[0].empty()
                                        || !track.mEuler[1].empty() || !track.mEuler[2].empty();
                                    for (int axis = 0; axis < 3; ++axis)
                                        eulerAxis[axis] += track.mEuler[axis].empty() ? 0u : 1u;
                                    quatTracks += track.mRotations.empty() ? 0u : 1u;
                                    s = s || !track.mScales.empty();
                                    ++interpolation[std::to_string(track.mTranslationInterpolation)];
                                }
                                openWithTranslation += t;
                                openWithRotation += r;
                                openWithScale += s;
                                if (NifScene::Sequences::animatedNodes(scene, sequence).size()
                                    != sequence.mTracks.size())
                                    ++openWithMissingNode;
                            }
                            else if (sname == "close")
                                hasClose = true;
                        }
                        if (hasOpen)
                        {
                            // By the words in the path (a survey guess: the record kind is the pack's, not the file's).
                            const auto has = [&name](const char* word) { return name.find(word) != std::string::npos; };
                            if (has("door") || has("gate"))
                                ++doorNamed;
                            else if (has("chest") || has("barrel") || has("crate") || has("urn") || has("coffin")
                                || has("sarc") || has("casket") || has("trunk") || has("box") || has("drawer")
                                || has("dresser") || has("cupboard") || has("wardrobe") || has("desk") || has("safe")
                                || has("lid") || has("hatch") || has("cabinet") || has("shelf"))
                                ++containerNamed;
                            else
                                ++otherNamed;
                            // How far the sequence moves its parts at its end: the largest turn (degrees) and slide.
                            double maxTurn = 0.0, maxSlide = 0.0;
                            for (const NifScene::Sequence& sequence : scene.mSequences)
                            {
                                if (lower(sequence.mName) != "open")
                                    continue;
                                for (std::uint32_t node : NifScene::Sequences::animatedNodes(scene, sequence))
                                {
                                    const auto d = NifScene::Sequences::delta(scene, sequence, node, sequence.mStop);
                                    const double s
                                        = std::sqrt(double(d[0]) * d[0] + double(d[1]) * d[1] + double(d[2]) * d[2]);
                                    const double cosine = s > 1e-9
                                        ? std::clamp((double(d[0]) + d[5] + d[10]) / s - 1.0, -2.0, 2.0) / 2.0
                                        : 1.0;
                                    maxTurn = std::max(
                                        maxTurn, std::acos(std::clamp(cosine, -1.0, 1.0)) * 180.0 / 3.14159265358979);
                                    maxSlide = std::max(maxSlide,
                                        std::sqrt(
                                            double(d[12]) * d[12] + double(d[13]) * d[13] + double(d[14]) * d[14]));
                                }
                            }
                            const char* kind = (has("door") || has("gate")) ? "door" : "other";
                            const std::string bucket = maxTurn < 10 ? "turn under 10"
                                : maxTurn < 45                      ? "turn 10 to 45"
                                : maxTurn < 120                     ? "turn 45 to 120"
                                                                    : "turn over 120";
                            ++motion[std::string(kind) + " " + bucket];
                            if (std::string(kind) != "door" && maxTurn >= 45)
                                bigContainers[bucket].push_back(name);
                            ++motion[std::string(kind)
                                + (maxSlide >= 5.0 ? " slides 5 or more units" : " slides under 5 units")];
                            ++withOpen;
                            ++folders[folderOf(name)];
                        }
                        if (hasOpen && hasClose)
                            ++withOpenAndClose;
                    }
                    catch (const std::exception&)
                    {
                        ++failed;
                    }
                }
            }
            catch (const std::exception& e)
            {
                line(std::string("archive failed: ") + e.what());
            }
        }

        line("files " + std::to_string(files) + " failed " + std::to_string(failed) + " with sequences "
            + std::to_string(withSequences) + " with Open " + std::to_string(withOpen) + " with Open and Close "
            + std::to_string(withOpenAndClose));
        line("Open models by path words: door or gate " + std::to_string(doorNamed) + ", container-like "
            + std::to_string(containerNamed) + ", other " + std::to_string(otherNamed));
        for (const auto& [name, count] : motion)
            line("Open motion, " + name + ": " + std::to_string(count));
        for (const auto& [bucket, names] : bigContainers)
            for (std::size_t i = 0; i < names.size() && i < 12; ++i)
                line("non-door with " + bucket + ": " + names[i]);
        line("tracks " + std::to_string(tracks) + " unsupported blocks " + std::to_string(unsupportedBlocks)
            + " max tracks in one sequence " + std::to_string(maxTracks));
        line("Open sequences fully supported " + std::to_string(openFullySupported) + " with an unsupported block "
            + std::to_string(openUnsupported) + " with a track whose node is missing "
            + std::to_string(openWithMissingNode));
        line("Open channels: translation " + std::to_string(openWithTranslation) + " rotation "
            + std::to_string(openWithRotation) + " scale " + std::to_string(openWithScale) + " duration "
            + std::to_string(shortest) + ".." + std::to_string(longest));
        line("Open tracks with quaternion keys " + std::to_string(quatTracks) + ", with Euler keys on x "
            + std::to_string(eulerAxis[0]) + " y " + std::to_string(eulerAxis[1]) + " z "
            + std::to_string(eulerAxis[2]));
        for (const auto& [name, count] : sequenceNames)
            if (count >= 20)
                line("sequence name '" + name + "' " + std::to_string(count));
        for (const auto& [name, count] : interpolatorTypes)
            line("interpolator block " + name + " " + std::to_string(count));
        for (const auto& [name, count] : interpolation)
            line("Open translation interpolation code " + name + " " + std::to_string(count));
        for (const auto& [name, count] : cycle)
            line("cycle type " + name + " " + std::to_string(count));
        std::vector<std::pair<std::uint64_t, std::string>> ranked;
        for (const auto& [name, count] : folders)
            ranked.emplace_back(count, name);
        std::sort(ranked.rbegin(), ranked.rend());
        for (std::size_t i = 0; i < ranked.size() && i < 25; ++i)
            line("Open models in " + ranked[i].second + " " + std::to_string(ranked[i].first));
        SUCCEED();
    }

    // OPENEDEN_SEQ_DUMP_MODEL=<model path inside the BSAs, with slashes>: prints the Open sequence of one model, the
    // tracks (node, key counts) and the delta matrix of each animated node at the end of the sequence.
    TEST(SkyrimSequencesReal, one_model_when_asked)
    {
        const char* data = std::getenv("OPENEDEN_SKYRIMSE_DATA");
        const char* wanted = std::getenv("OPENEDEN_SEQ_DUMP_MODEL");
        if (data == nullptr || wanted == nullptr || !fs::is_directory(data))
            GTEST_SKIP() << "OPENEDEN_SKYRIMSE_DATA and OPENEDEN_SEQ_DUMP_MODEL are not set";
        const std::string target = lower(wanted);
        for (const auto& item : fs::directory_iterator(data))
        {
            if (lower(item.path().extension().string()) != ".bsa")
                continue;
            try
            {
                Bsa::CompressedBSAFile archive;
                archive.open(item.path());
                for (const auto& entry : archive.getList())
                {
                    std::string name = lower(std::string(entry.name()));
                    for (char& c : name)
                        if (c == '\\')
                            c = '/';
                    if (name != target)
                        continue;
                    auto stream = archive.getFile(&entry);
                    std::string bytes((std::istreambuf_iterator<char>(*stream)), std::istreambuf_iterator<char>());
                    Files::IStreamPtr input(new std::istringstream(std::move(bytes), std::ios::binary));
                    const NifScene::Scene scene = NifScene::readScene(
                        std::move(input), VFS::Path::Normalized("dump.nif"), NifScene::SceneOptions{});
                    const std::size_t open = NifScene::Sequences::find(scene, "Open");
                    line("model " + name + " nodes " + std::to_string(scene.mNodes.size()) + " sequences "
                        + std::to_string(scene.mSequences.size()) + " open index " + std::to_string(open));
                    for (std::uint32_t n = 0; n < scene.mNodes.size(); ++n)
                        line("node " + std::to_string(n) + " '" + scene.mNodes[n].mName + "' parent "
                            + std::to_string(static_cast<int>(scene.mNodes[n].mParent)) + " geometry "
                            + std::to_string(static_cast<int>(scene.mNodes[n].mGeometry)));
                    if (open == NifScene::Sequences::sNoSequence)
                        return;
                    const NifScene::Sequence& sequence = scene.mSequences[open];
                    line("Open start " + std::to_string(sequence.mStart) + " stop " + std::to_string(sequence.mStop)
                        + " freq " + std::to_string(sequence.mFrequency));
                    for (const NifScene::Track& track : sequence.mTracks)
                    {
                        std::string text = "track '" + track.mNodeName + "' t"
                            + std::to_string(track.mTranslations.size()) + " q"
                            + std::to_string(track.mRotations.size()) + " ex" + std::to_string(track.mEuler[0].size())
                            + " ey" + std::to_string(track.mEuler[1].size()) + " ez"
                            + std::to_string(track.mEuler[2].size()) + " s" + std::to_string(track.mScales.size());
                        for (int axis = 0; axis < 3; ++axis)
                        {
                            text += " e" + std::string(1, "xyz"[axis]) + "[interp "
                                + std::to_string(track.mEulerInterpolation[axis]) + ":";
                            for (const NifScene::KeyFloat& key : track.mEuler[axis])
                                text += " " + std::to_string(key.mTime) + "=" + std::to_string(key.mValue) + "(in "
                                    + std::to_string(key.mInTan) + " out " + std::to_string(key.mOutTan) + ")";
                            text += "]";
                        }
                        line(text);
                    }
                    for (std::uint32_t node : NifScene::Sequences::animatedNodes(scene, sequence))
                    {
                        const auto d = NifScene::Sequences::delta(scene, sequence, node, sequence.mStop);
                        std::ostringstream out;
                        out << "delta node " << node << ":";
                        for (float v : d)
                            out << ' ' << v;
                        line(out.str());
                    }
                    return;
                }
            }
            catch (const std::exception&)
            {
            }
        }
        line("model not found");
    }
}
