// Tests of Anim::AnimSet and the shared text key parser (task 0124, stage C1; components/animation/animset.* and
// textkeys.*). Written from the spec before the code existed (the first version of both was a stub that found nothing).
//
// In plain words: every animation source (a Morrowind .kf, a COLLADA model with its .txt, CreativeMotion's glTF clips)
// arrives as a ControllerMap with a Tags table of "group: key" at a time. AnimSet turns that table into ONE group
// table: which groups exist, where each starts and stops and where its loop section is. MWAnim::Play answers its
// group questions from it. The equivalence tests below drive a real Play and check that the table says what Play does.

#include <components/animation/animset.hpp>
#include <components/animation/bones.hpp>
#include <components/animation/controllermap.hpp>
#include <components/animation/tags.hpp>
#include <components/animation/textkeys.hpp>
#include <components/animation/transform.hpp>
#include <components/animation/transformcontroller.hpp>
#include <components/mwanimation/play.hpp>
#include <components/archivekinds/archivekinds.hpp>
#include <components/files/istreamptr.hpp>
#include <components/vfs/archive.hpp>
#include <components/vfs/archivefactory.hpp>
#include <components/vfs/file.hpp>
#include <components/vfs/pathutil.hpp>
#include <components/vsgadapters/nif/kf.hpp>
#include <components/vsgutil/name.hpp>

#include <vsg/io/Options.h>

#include <gtest/gtest.h>

#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <memory>
#include <string>
#include <utility>
#include <vector>

namespace
{
    using Keys = std::vector<std::pair<float, std::string>>;

    vsg::ref_ptr<Anim::ControllerMap> makeMap(const Keys& keys)
    {
        auto map = vsg::ref_ptr<Anim::ControllerMap>(new Anim::ControllerMap);
        map->tags = vsg::ref_ptr<Anim::Tags>(new Anim::Tags);
        for (const auto& [time, text] : keys)
            map->tags->emplace(time, std::string(text));
        return map;
    }

    Keys mainKeys()
    {
        return { { 0.f, "idle: start" }, { 2.f, "idle: stop" }, { 2.f, "walkforward: start" },
            { 2.5f, "walkforward: loop start" }, { 3.5f, "walkforward: loop stop" }, { 4.f, "walkforward: stop" },
            { 5.f, "attack: start" }, { 5.5f, "attack: hit" }, { 6.f, "attack: stop" } };
    }

    vsg::ref_ptr<Anim::AnimSet> makeSet(const Keys& keys)
    {
        return vsg::ref_ptr<Anim::AnimSet>(new Anim::AnimSet(makeMap(keys), "test"));
    }

    std::vector<std::string> keyTexts(const Anim::Tags& tags)
    {
        std::vector<std::string> out;
        for (const auto& [time, text] : tags)
            out.push_back(std::to_string(time) + " " + text);
        return out;
    }

    // ---- the group table ------------------------------------------------------------------------------------------

    TEST(AnimSet, a_source_without_tags_has_an_empty_table_and_every_query_is_absent)
    {
        vsg::ref_ptr<Anim::AnimSet> none(new Anim::AnimSet(vsg::ref_ptr<Anim::ControllerMap>(new Anim::ControllerMap)));
        EXPECT_TRUE(none->groups().empty());
        EXPECT_FALSE(none->hasGroup("idle"));
        EXPECT_EQ(none->startTime("idle"), Anim::AnimSet::sAbsent);
        EXPECT_EQ(none->textKeyTime("idle"), Anim::AnimSet::sAbsent);
        EXPECT_NE(none->tags, nullptr) << "the key table is never null";
        vsg::ref_ptr<Anim::AnimSet> null(new Anim::AnimSet(nullptr));
        EXPECT_TRUE(null->groups().empty());
        EXPECT_NE(null->tags, nullptr);
    }

    TEST(AnimSet, the_table_holds_each_group_with_its_first_key_and_its_markers)
    {
        const auto set = makeSet(mainKeys());
        EXPECT_EQ(set->groups().size(), 3u);
        const auto* idle = set->find("idle");
        ASSERT_NE(idle, nullptr);
        EXPECT_FLOAT_EQ(idle->firstKey, 0.f);
        EXPECT_FLOAT_EQ(idle->start, 0.f);
        EXPECT_FLOAT_EQ(idle->stop, 2.f);
        EXPECT_EQ(idle->loopStart, Anim::AnimSet::sAbsent);
        EXPECT_EQ(idle->loopStop, Anim::AnimSet::sAbsent);
        const auto* walk = set->find("walkforward");
        ASSERT_NE(walk, nullptr);
        EXPECT_FLOAT_EQ(walk->firstKey, 2.f);
        EXPECT_FLOAT_EQ(walk->start, 2.f);
        EXPECT_FLOAT_EQ(walk->loopStart, 2.5f);
        EXPECT_FLOAT_EQ(walk->loopStop, 3.5f);
        EXPECT_FLOAT_EQ(walk->stop, 4.f);
        const auto* attack = set->find("attack");
        ASSERT_NE(attack, nullptr);
        EXPECT_FLOAT_EQ(attack->firstKey, 5.f);
        EXPECT_FLOAT_EQ(attack->stop, 6.f);
        EXPECT_EQ(set->find("sneak"), nullptr);
        EXPECT_FALSE(set->hasGroup("walk")) << "a group is matched whole";
        EXPECT_TRUE(set->hasGroup("attack"));
    }

    TEST(AnimSet, start_time_is_the_first_key_of_the_group_whatever_it_is_and_absent_for_unknown)
    {
        const auto set = makeSet({ { 3.f, "odd: stop" }, { 1.f, "odd: hit" }, { 7.f, "odd: start" } });
        EXPECT_FLOAT_EQ(set->startTime("odd"), 1.f) << "the earliest key in time order, as Play::getStartTime";
        EXPECT_EQ(set->startTime("nothing"), Anim::AnimSet::sAbsent);
    }

    TEST(AnimSet, a_group_repeated_in_one_source_takes_its_last_markers)
    {
        const auto set
            = makeSet({ { 1.f, "wolf: start" }, { 2.f, "wolf: stop" }, { 5.f, "wolf: start" }, { 7.f, "wolf: stop" } });
        EXPECT_FLOAT_EQ(set->find("wolf")->start, 5.f);
        EXPECT_FLOAT_EQ(set->find("wolf")->stop, 7.f);
        EXPECT_FLOAT_EQ(set->find("wolf")->firstKey, 1.f);
    }

    TEST(AnimSet, a_stop_key_with_garbage_after_it_still_counts_as_the_stop)
    {
        const auto set = makeSet({ { 0.f, "scrib: start" }, { 1.f, "scrib: stop." } });
        EXPECT_FLOAT_EQ(set->find("scrib")->stop, 1.f);
    }

    TEST(AnimSet, keys_that_are_not_markers_do_not_move_the_markers)
    {
        const auto set = makeSet({ { 0.f, "g: start" }, { 0.5f, "g: hit" }, { 0.7f, "g: start2" },
            { 0.8f, "g: loop startle" }, { 1.f, "g: stop" } });
        EXPECT_FLOAT_EQ(set->find("g")->start, 0.f) << "'start2' is not 'start'";
        EXPECT_EQ(set->find("g")->loopStart, Anim::AnimSet::sAbsent) << "'loop startle' is not 'loop start'";
    }

    TEST(AnimSet, a_key_without_a_group_belongs_to_no_group)
    {
        // The example suite's BasicPlayer.txt has "hit1 stop 101.06" without the colon.
        const auto set = makeSet({ { 100.f, "hit1: start" }, { 101.f, "hit1 stop" }, { 102.f, "sound" } });
        ASSERT_NE(set->find("hit1"), nullptr);
        EXPECT_FLOAT_EQ(set->find("hit1")->start, 100.f);
        EXPECT_EQ(set->find("hit1")->stop, Anim::AnimSet::sAbsent);
        EXPECT_EQ(set->groups().size(), 1u);
        EXPECT_FLOAT_EQ(set->textKeyTime("hit1 stop"), 101.f) << "but it is still a key";
    }

    TEST(AnimSet, a_text_key_time_is_a_prefix_match_in_time_order)
    {
        const auto set = makeSet(mainKeys());
        EXPECT_FLOAT_EQ(set->textKeyTime("walkforward: loop"), 2.5f);
        EXPECT_FLOAT_EQ(set->textKeyTime("attack: h"), 5.5f);
        EXPECT_FLOAT_EQ(set->textKeyTime("idle: stop"), 2.f);
        EXPECT_EQ(set->textKeyTime("nothing"), Anim::AnimSet::sAbsent);
    }

    TEST(AnimSet, the_set_keeps_its_controllers_and_the_same_tag_table)
    {
        auto map = makeMap(mainKeys());
        vsg::ref_ptr<Anim::AnimSet> set(new Anim::AnimSet(map, "source.kf"));
        EXPECT_EQ(set->controllers.get(), map.get());
        EXPECT_EQ(set->tags.get(), map->tags.get());
        EXPECT_EQ(set->name, "source.kf");
    }

    // ---- equivalence with what Play does -------------------------------------------------------------------------

    struct PlayRig
    {
        vsg::ref_ptr<Anim::Transform> mScene = Anim::Transform::create();
        std::unique_ptr<MWAnim::Play> mPlay;

        PlayRig()
        {
            auto bip01 = Anim::Transform::create();
            vsgUtil::setName(*mScene, "scene");
            vsgUtil::setName(*bip01, "bip01");
            mScene->addChild(bip01);
            Anim::Bones bones([](const std::string& name) { return name; }, *mScene);
            mPlay = std::make_unique<MWAnim::Play>(bones, 1);
        }
    };

    // For every group of the table: Play knows it, its start time query is the table's first key, playing it starts
    // at the table's start and a long step ends it at the table's stop (or refuses when there is no start or stop).
    void expectPlayAgrees(const Keys& keys)
    {
        PlayRig rig;
        auto map = makeMap(keys);
        rig.mPlay->addSingleAnimSource(map, "equivalence");
        vsg::ref_ptr<Anim::AnimSet> set(new Anim::AnimSet(map, "equivalence"));

        std::vector<std::string> playGroups;
        rig.mPlay->listAnimationGroups([&](const std::string& name) { playGroups.push_back(name); });
        std::vector<std::string> tableGroups;
        for (const auto& [name, group] : set->groups())
            tableGroups.push_back(name);
        EXPECT_EQ(playGroups, tableGroups);
        EXPECT_EQ(std::vector<std::string>(map->tags->getGroups().begin(), map->tags->getGroups().end()), tableGroups)
            << "the group names Tags::emplace collected before AnimSet existed";

        for (const auto& [name, group] : set->groups())
        {
            SCOPED_TRACE(name);
            EXPECT_TRUE(rig.mPlay->hasAnimation(name));
            EXPECT_FLOAT_EQ(rig.mPlay->getStartTime(name), group.firstKey);
            // The derivation Play used before AnimSet existed, kept here as the independent reference.
            ASSERT_TRUE(map->tags->hasGroup(name));
            ASSERT_NE(map->tags->findGroup(name), map->tags->end());
            EXPECT_FLOAT_EQ(map->tags->findGroup(name)->first, group.firstKey);
            rig.mPlay->endedAnimations.clear();
            rig.mPlay->play(name, MWAnim::Play::Priority(1), 1, true, 1.f, "start", "stop", 0.f, 0, false);
            const bool playable
                = group.start != Anim::AnimSet::sAbsent && group.stop != Anim::AnimSet::sAbsent && group.start <= group.stop;
            EXPECT_EQ(rig.mPlay->getInfo(name), playable);
            if (!playable)
                continue;
            EXPECT_FLOAT_EQ(rig.mPlay->getCurrentTime(name), group.start);
            rig.mPlay->runAnimation(1000.f);
            ASSERT_EQ(rig.mPlay->endedAnimations.size(), 1u);
            EXPECT_FLOAT_EQ(rig.mPlay->endedAnimations[0].time, group.stop);
        }
        EXPECT_FALSE(rig.mPlay->hasAnimation("not a group"));
    }

    TEST(AnimSetPlayEquivalence, the_main_source)
    {
        expectPlayAgrees(mainKeys());
    }

    TEST(AnimSetPlayEquivalence, repeated_sections_garbage_stops_and_groupless_keys)
    {
        expectPlayAgrees({ { 1.f, "wolf: start" }, { 2.f, "wolf: stop" }, { 5.f, "wolf: start" }, { 7.f, "wolf: stop" },
            { 8.f, "scrib: start" }, { 9.f, "scrib: stop." }, { 10.f, "hit1: start" }, { 11.f, "hit1 stop" },
            { 12.f, "late: start" }, { 12.f, "late: stop" }, { 13.f, "sound: foot" }, { 14.f, "only: stop" },
            { 20.f, "back: stop" }, { 21.f, "back: start" } });
    }

    TEST(AnimSetPlayEquivalence, generated_sources_with_random_keys)
    {
        // A fixed linear congruential sequence (no <random> distribution, whose output differs between libraries).
        unsigned state = 12345u;
        auto next = [&](unsigned n) {
            state = state * 1664525u + 1013904223u;
            return (state >> 8) % n;
        };
        const char* groups[] = { "a", "b", "c", "d" };
        const char* markers[] = { "start", "stop", "loop start", "loop stop", "hit", "stop.", "start2" };
        for (int round = 0; round < 60; ++round)
        {
            Keys keys;
            const unsigned count = 2 + next(14);
            for (unsigned i = 0; i < count; ++i)
                keys.emplace_back(static_cast<float>(next(40)) * 0.5f,
                    std::string(groups[next(4)]) + ": " + markers[next(7)]);
            SCOPED_TRACE("round " + std::to_string(round));
            expectPlayAgrees(keys);
        }
    }

    // Every .kf in the archives of the folders named by OPENEDEN_ANIMSET_CHECK_DIRS (';' separated; read only; skipped
    // when unset). Only counts are printed: no file names, no content.
    TEST(AnimSetPlayEquivalence, installed_kf_files_give_the_table_play_follows)
    {
        const char* dirs = std::getenv("OPENEDEN_ANIMSET_CHECK_DIRS");
        if (dirs == nullptr)
            GTEST_SKIP() << "OPENEDEN_ANIMSET_CHECK_DIRS is not set";
        VFS::registerBethesdaArchiveKinds();

        const std::string all(dirs);
        std::size_t start = 0;
        unsigned files = 0, unreadable = 0, groups = 0, filesWithoutGroups = 0;
        while (start <= all.size())
        {
            const std::size_t end = all.find(';', start);
            const std::filesystem::path dir(all.substr(start, end == std::string::npos ? end : end - start));
            start = end == std::string::npos ? all.size() + 1 : end + 1;
            if (dir.empty() || !std::filesystem::is_directory(dir))
                continue;
            for (const auto& entry : std::filesystem::directory_iterator(dir))
            {
                if (entry.path().extension() != ".bsa")
                    continue;
                const std::unique_ptr<VFS::Archive> archive = VFS::makeArchive(entry.path(), nullptr);
                ASSERT_NE(archive, nullptr);
                VFS::FileMap map;
                archive->listResources(map);
                for (const auto& [path, file] : map)
                {
                    if (!path.value().ends_with(".kf"))
                        continue;
                    const Files::IStreamPtr stream = file->open();
                    auto options = vsg::Options::create();
                    options->extensionHint = ".kf";
                    options->setValue("filename", std::string(path.value()));
                    vsg::ref_ptr<vsgAdapters::kf> reader{ new vsgAdapters::kf };
                    auto object = reader->read(*stream, options);
                    vsg::ref_ptr<Anim::ControllerMap> source(dynamic_cast<Anim::ControllerMap*>(object.get()));
                    if (source == nullptr || source->tags == nullptr)
                    {
                        ++unreadable;
                        continue;
                    }
                    ++files;
                    Keys keys;
                    for (const auto& [time, text] : *source->tags)
                        keys.emplace_back(time, text);
                    groups += static_cast<unsigned>(source->tags->getGroups().size());
                    filesWithoutGroups += source->tags->getGroups().empty() ? 1 : 0;
                    expectPlayAgrees(keys);
                }
            }
        }
        std::cout << "[animset] " << files << " .kf files compared, " << groups << " groups, " << filesWithoutGroups
                  << " without groups, " << unreadable << " not read\n";
        EXPECT_GT(files, 0u);
    }

    // The text key files of the OpenMW example suite (the animated models of the Lua integration tests), read through
    // the shared line parser: OPENEDEN_ANIMSET_SUITE_DIR is the suite's folder. Skipped when unset.
    TEST(AnimSetPlayEquivalence, the_example_suite_text_key_files_give_the_table_play_follows)
    {
        const char* dir = std::getenv("OPENEDEN_ANIMSET_SUITE_DIR");
        if (dir == nullptr)
            GTEST_SKIP() << "OPENEDEN_ANIMSET_SUITE_DIR is not set";
        unsigned read = 0;
        for (const char* relative :
            { "example_animated_creature/data/meshes/land_racer.txt", "game_template/data/meshes/BasicPlayer.txt" })
        {
            std::ifstream file(std::filesystem::path(dir) / relative);
            ASSERT_TRUE(file.good()) << relative;
            Anim::Tags tags;
            std::string line;
            while (std::getline(file, line))
                Anim::addTextKeyLine(tags, line);
            Keys keys;
            for (const auto& [time, text] : tags)
                keys.emplace_back(time, text);
            ASSERT_FALSE(keys.empty());
            ++read;
            SCOPED_TRACE(relative);
            expectPlayAgrees(keys);
        }
        EXPECT_EQ(read, 2u);
    }

    // ---- the shared text key parser -------------------------------------------------------------------------------

    TEST(TextKeys, a_record_gives_lower_case_trimmed_keys_split_on_line_breaks)
    {
        Anim::Tags tags;
        Anim::addTextKeys(tags, 1.5f, "Idle: Start\r\n  Idle: Loop Start \nSound: Foot\r\n\r\n");
        EXPECT_EQ(keyTexts(tags), (std::vector<std::string>{ "1.500000 idle: start", "1.500000 idle: loop start",
                                      "1.500000 sound: foot" }));
        EXPECT_TRUE(tags.hasGroup("idle"));
        EXPECT_TRUE(tags.hasGroup("sound"));
    }

    TEST(TextKeys, a_record_of_only_blanks_adds_nothing)
    {
        Anim::Tags tags;
        Anim::addTextKeys(tags, 1.f, " \r\n \t ");
        Anim::addTextKeys(tags, 1.f, "");
        EXPECT_TRUE(tags.empty());
    }

    TEST(TextKeys, a_line_is_a_key_then_a_time_after_the_last_space)
    {
        Anim::Tags tags;
        EXPECT_TRUE(Anim::addTextKeyLine(tags, "Walkforward: Loop Start 2.5"));
        EXPECT_TRUE(Anim::addTextKeyLine(tags, "  idle: stop   2.033333  "));
        EXPECT_TRUE(Anim::addTextKeyLine(tags, "hit1 stop 101.5"));
        EXPECT_EQ(keyTexts(tags), (std::vector<std::string>{ "2.033333 idle: stop", "2.500000 walkforward: loop start",
                                      "101.500000 hit1 stop" }));
        EXPECT_FALSE(tags.hasGroup("hit1")) << "no colon, no group";
    }

    TEST(TextKeys, a_line_that_is_not_a_key_adds_nothing)
    {
        Anim::Tags tags;
        EXPECT_FALSE(Anim::addTextKeyLine(tags, "nospace"));
        EXPECT_FALSE(Anim::addTextKeyLine(tags, ""));
        EXPECT_FALSE(Anim::addTextKeyLine(tags, "   "));
        EXPECT_FALSE(Anim::addTextKeyLine(tags, " 3.0")) << "an empty key";
        EXPECT_TRUE(tags.empty());
    }

    TEST(TextKeys, a_time_that_is_not_a_number_is_zero)
    {
        Anim::Tags tags;
        EXPECT_TRUE(Anim::addTextKeyLine(tags, "idle: start soon"));
        ASSERT_FALSE(tags.empty());
        EXPECT_FLOAT_EQ(tags.begin()->first, 0.f);
        EXPECT_EQ(tags.begin()->second, "idle: start") << "the word after the last space was taken as the time";
        EXPECT_EQ(tags.begin()->second.find("soon"), std::string::npos);
    }

    TEST(TextKeys, a_line_with_a_carriage_return_is_trimmed)
    {
        Anim::Tags tags;
        EXPECT_TRUE(Anim::addTextKeyLine(tags, "idle: stop 2\r"));
        EXPECT_FLOAT_EQ(tags.begin()->first, 2.f);
        EXPECT_EQ(tags.begin()->second, "idle: stop");
    }
}
