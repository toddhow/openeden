// Task 0143, milestone M-S4: the voice of a Skyrim SE conversation on the player's own masters. Prints counts only
// (lines starting SKYRIMSE-VOICE), never a line of text or a file name. Skipped when OPENEDEN_SKYRIMSE_DATA is not set.
//
//   OPENEDEN_SKYRIMSE_DATA   the game's Data folder (required); "Skyrim - Voices_en0.bsa" is read from it
//
// 1. Every file of the voice archive splits as a .fuz (container layout VERIFIED).
// 2. The file name rule: for every response of every INFO of the five masters, is there a voice file under the
//    plugin folder of the record that wins the load order (with the voice type left open)?
// 3. The Bannered Mare: for every actor, does the greeting INFO have voice files for its voice type?
#include <components/bsa/compressedbsafile.hpp>
#include <components/fuz/fuz.hpp>

#include <game_skyrimse/dialogue.hpp>
#include <game_skyrimse/neutral_world.hpp>
#include <game_skyrimse/quest.hpp>
#include <game_skyrimse/voice.hpp>
#include <game_skyrimse/world_view.hpp>

#include <gtest/gtest.h>

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <iostream>
#include <map>
#include <set>
#include <string>
#include <vector>

namespace
{
    namespace fs = std::filesystem;
    namespace sdkgame = openeden::sdk::game;
    using namespace game_skyrimse;

    std::string unify(std::string_view name)
    {
        std::string s(name);
        for (char& c : s)
        {
            if (c == '\\')
                c = '/';
            else if (c >= 'A' && c <= 'Z')
                c = static_cast<char>(c - 'A' + 'a');
        }
        return s;
    }

    /// Existence of voice files from the archive's listing; everything else is read from the Data folder.
    class ListedVoiceFiles final : public FileSource
    {
    public:
        ListedVoiceFiles(const fs::path& data, std::set<std::string> voices)
            : mLoose(data)
            , mVoices(std::move(voices))
        {
        }
        bool read(std::string_view path, std::string& bytes) const override { return mLoose.read(path, bytes); }
        bool exists(std::string_view path) const override
        {
            const std::string key = unify(path);
            if (key.rfind("sound/voice/", 0) == 0)
                return mVoices.count(key) != 0;
            return mLoose.exists(path);
        }

    private:
        DataFolderFiles mLoose;
        std::set<std::string> mVoices;
    };

    const char* const kMasters[] = { "Skyrim.esm", "Update.esm", "Dawnguard.esm", "HearthFires.esm", "Dragonborn.esm" };

    TEST(SkyrimVoiceRealData, theVoiceArchiveFilesSplitAndTheNameRuleFindThemFromTheRecords)
    {
        const char* env = std::getenv("OPENEDEN_SKYRIMSE_DATA");
        if (env == nullptr || !fs::is_directory(env))
            GTEST_SKIP() << "OPENEDEN_SKYRIMSE_DATA is not set";
        const fs::path data = env;
        const fs::path archivePath = data / "Skyrim - Voices_en0.bsa";
        if (!fs::exists(archivePath))
            GTEST_SKIP() << "no Skyrim - Voices_en0.bsa";

        // ---- 1. the container, over every file
        // ------------------------------------------------------------------------
        Bsa::CompressedBSAFile archive;
        archive.open(archivePath);
        std::set<std::string> voices; // "sound/voice/<plugin>/<voice type>/<file>"
        std::uint64_t files = 0, fuzOk = 0, xwma = 0, trailing = 0, failed = 0, maxLip = 0, maxAudio = 0;
        std::string firstError;
        for (const Bsa::BSAFile::FileStruct& file : archive.getList())
        {
            const std::string name = unify(file.name());
            voices.insert(name);
            ++files;
            const Files::IStreamPtr stream = archive.getFile(&file);
            std::string bytes(std::istreambuf_iterator<char>(*stream), {});
            const Fuz::Result r = Fuz::split(bytes, "");
            if (!r.mOk)
            {
                ++failed;
                if (firstError.empty())
                    firstError = r.mError;
                continue;
            }
            ++fuzOk;
            xwma += r.mParts.mAudioIsXwma;
            trailing += r.mParts.mTrailingBytes != 0;
            maxLip = std::max<std::uint64_t>(maxLip, r.mParts.mLip.size());
            maxAudio = std::max<std::uint64_t>(maxAudio, r.mParts.mAudio.size());
        }
        std::cout << "SKYRIMSE-VOICE container: files " << files << " split " << fuzOk << " xwma " << xwma
                  << " with trailing bytes " << trailing << " failed " << failed << " max lip " << maxLip
                  << " max audio " << maxAudio << "\n";
        if (failed != 0)
            std::cout << "SKYRIMSE-VOICE first container error: " << firstError << "\n";
        EXPECT_EQ(failed, 0u);
        EXPECT_EQ(fuzOk, xwma);
        ASSERT_GT(files, 0u);

        // ---- 2. the file name rule over every INFO response of the five masters
        // --------------------------------------------
        LoadOrder order;
        std::vector<fs::path> paths;
        for (const char* name : kMasters)
        {
            if (!fs::exists(data / name))
                continue;
            std::string err;
            ASSERT_TRUE(order.add(name, false, err)) << err;
            paths.push_back(data / name);
        }
        ASSERT_FALSE(paths.empty());

        // "sound/voice/<plugin>/<tail>" without the voice type folder, to ask about a line before its speaker is known.
        std::set<std::string> byPlugin;
        for (const std::string& v : voices)
        {
            const std::size_t p0 = std::string("sound/voice/").size();
            const std::size_t p1 = v.find('/', p0);
            const std::size_t p2 = p1 == std::string::npos ? p1 : v.find('/', p1 + 1);
            if (p2 != std::string::npos)
                byPlugin.insert(v.substr(p0, p1 - p0) + "/" + v.substr(p2 + 1));
        }

        DialogueIndex dialogue;
        QuestIndex quests;
        for (const fs::path& path : paths)
        {
            Content::Error error;
            const std::unique_ptr<Content::Plugin> file = Content::Plugin::openFile(path, error);
            ASSERT_NE(file, nullptr) << error.mMessage;
            const std::string name = path.filename().string();
            DialogueRecords d;
            (void)parseDialogueRecords(*file, order, DialogueLimits(), d);
            dialogue.add(std::move(d), FormIdResolver(file->header(), name, order));
            QuestRecords q;
            (void)parseQuestRecords(*file, order, QuestLimits(), q);
            quests.add(std::move(q), FormIdResolver(file->header(), name, order));
        }

        std::uint64_t responses = 0, found = 0, missing = 0, foundByOverride = 0, missingOverrideOfBase = 0,
                      foundUnderOrigin = 0;
        for (FormId topicId : dialogue.topicIds())
        {
            const DialogTopic* topic = dialogue.topic(topicId);
            const QuestRecord* quest = quests.quest(topic->mQuest);
            const std::string stem
                = voiceStem(quest != nullptr ? quest->mCommon.mEditorId : std::string(), topic->mCommon.mEditorId);
            for (const DialogInfo* info : dialogue.infosOf(topicId))
            {
                for (const DialogResponse& response : info->mResponses)
                {
                    ++responses;
                    char tail[48];
                    std::snprintf(tail, sizeof(tail), "_%08x_%u.fuz", static_cast<unsigned>(info->mCommon.mStoredId),
                        static_cast<unsigned>(response.mNumber));
                    const std::string key = unify(dialogue.pluginName(info->mPlugin)) + "/" + unify(stem) + tail;
                    if (byPlugin.count(key) != 0)
                    {
                        ++found;
                        foundByOverride += info->mPlugin != 0 && (info->mCommon.mId >> 24) == 0;
                    }
                    else
                    {
                        ++missing;
                        const bool overrideOfBase = info->mPlugin != 0 && (info->mCommon.mId >> 24) == 0;
                        missingOverrideOfBase += overrideOfBase;
                        if (overrideOfBase)
                        {
                            // The other candidate: the folder of the plugin that made the record (Skyrim.esm, whose own
                            // ids are the load order ids), the id as that plugin stores it.
                            char originTail[48];
                            std::snprintf(originTail, sizeof(originTail), "_%08x_%u.fuz",
                                static_cast<unsigned>(info->mCommon.mId), static_cast<unsigned>(response.mNumber));
                            foundUnderOrigin += byPlugin.count("skyrim.esm/" + unify(stem) + originTail) != 0;
                        }
                    }
                }
            }
        }
        std::cout << "SKYRIMSE-VOICE names: info responses " << responses << " with a voice file " << found
                  << " without " << missing
                  << " (of those found, from a later plugin's override of a Skyrim.esm record: " << foundByOverride
                  << "; missing for such overrides: " << missingOverrideOfBase
                  << ", of which a file exists under skyrim.esm with the load order id: " << foundUnderOrigin << ")\n";
        EXPECT_GT(found, 0u);

        // ---- 3. the Bannered Mare
        // ----------------------------------------------------------------------------------------------
        ListedVoiceFiles listed(data, voices);
        SkyrimWorldView view({}, &listed);
        std::vector<std::string> problems;
        ASSERT_GT(view.load(paths, problems), 0u);
        SkyrimCellSource source(view);
        const sdkgame::WorldId cellId = source.findCellByEditorId("WhiterunBanneredMare");
        ASSERT_NE(cellId, sdkgame::kNoWorldId);
        sdkgame::WorldCell cell;
        std::string error;
        ASSERT_TRUE(source.buildCell(cellId, cell, error)) << error;
        std::uint64_t actors = 0, greeted = 0, withVoice = 0, voiceLines = 0;
        for (const sdkgame::WorldObject& o : cell.objects)
        {
            if (o.kind != sdkgame::ObjectKind::Actor)
                continue;
            ++actors;
            sdkgame::DialogueTurn turn;
            if (!source.beginConversation(o.base, o.id, cellId, turn, error))
                continue;
            ++greeted;
            withVoice += !turn.voice.empty();
            voiceLines += turn.voice.size();
            for (const std::string& path : turn.voice)
                EXPECT_TRUE(voices.count("sound/" + path) != 0) << "the pack named a file that is not in the archive";
            source.endConversation();
        }
        std::cout << "SKYRIMSE-VOICE bannered mare: actors " << actors << " greeted " << greeted
                  << " greeting with voice " << withVoice << " voice lines " << voiceLines << "\n";
        EXPECT_GT(actors, 0u);
    }
}
