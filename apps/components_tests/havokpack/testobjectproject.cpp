#include <components/havokpack/objectproject.hpp>

#include <gtest/gtest.h>

#include <algorithm>
#include <string>
#include <vector>

// Task 0289 (dungeon mechanics): the animation project of a placed Skyrim object (a puzzle pillar, a gate, a lever) and
// the names the game's scripts call its clips by. The project texts below are the three that the installed game's
// meshes/animationdata/ruinspuzzlepillar01.txt, leveranimating.txt and puzzledoorkeyhole01.txt hold (checked by reading
// them; no game data is in the repository).
namespace
{
    using namespace HavokPack;

    const char* const kPillar = "1\n6\nBehaviors\\Behavior00.hkx\nCharacters\\Character00.hkx\n"
                                "CharacterAssets\\RuinsPillarSkeleton01.HKX\nAnimations\\ruinspillartrigger01.hkx\n"
                                "Animations\\ruinspillartrigger02.hkx\nAnimations\\ruinspillartrigger03.hkx\n0\n";
    const char* const kLever = "1\n4\nBehaviors\\LeverPushBehavior.hkx\nCharacters\\LeverAnimating_Character.hkx\n"
                               "CharacterAssets\\Lever_Character.hkx\nAnimations\\leverpush.hkx\n0\n";

    bool has(const std::vector<std::string>& names, const std::string& wanted)
    {
        return std::find(names.begin(), names.end(), wanted) != names.end();
    }

    TEST(HavokObjectProject, the_pillars_project_names_its_skeleton_and_three_clips)
    {
        ObjectProject project;
        ASSERT_TRUE(parseObjectProject(kPillar, project));
        EXPECT_EQ(project.mSkeleton, "characterassets/ruinspillarskeleton01.hkx");
        ASSERT_EQ(project.mClips.size(), 3u);
        EXPECT_EQ(project.mClips[0], "animations/ruinspillartrigger01.hkx");
        EXPECT_EQ(project.mClips[2], "animations/ruinspillartrigger03.hkx");
    }

    TEST(HavokObjectProject, carriage_returns_and_a_one_clip_project_parse)
    {
        std::string text = kLever;
        std::string crlf;
        for (char c : text)
        {
            if (c == '\n')
                crlf += '\r';
            crlf += c;
        }
        ObjectProject project;
        ASSERT_TRUE(parseObjectProject(crlf, project));
        ASSERT_EQ(project.mClips.size(), 1u);
        EXPECT_EQ(project.mClips[0], "animations/leverpush.hkx");
        EXPECT_EQ(project.mSkeleton, "characterassets/lever_character.hkx");
    }

    TEST(HavokObjectProject, hostile_texts_are_refused)
    {
        ObjectProject project;
        EXPECT_FALSE(parseObjectProject("", project));
        EXPECT_FALSE(parseObjectProject("1\n", project));
        EXPECT_FALSE(parseObjectProject("1\nmany\n", project));
        EXPECT_FALSE(parseObjectProject("1\n2\na.hkx\nb.hkx\n0\n", project)); // no room for behavior, character, skeleton
        EXPECT_FALSE(parseObjectProject("1\n5\na.hkx\nb.hkx\n0\n", project)); // fewer lines than it says
        EXPECT_FALSE(parseObjectProject("1\n9999\na.hkx\n", project)); // over the cap
        EXPECT_FALSE(parseObjectProject("1\n4\na\nb\nc\n..\\..\\evil.hkx\n0\n", project)); // leaves the folder
    }

    TEST(HavokObjectProject, the_project_path_and_folder_come_from_the_model_path)
    {
        EXPECT_EQ(objectProjectPath("meshes/clutter/ruins/pillar/RuinsPuzzlePillar01.nif"),
            "meshes/animationdata/ruinspuzzlepillar01.txt");
        EXPECT_EQ(objectProjectPath("Meshes\\Furniture\\Clutter\\LeverAnimating.nif"),
            "meshes/animationdata/leveranimating.txt");
        EXPECT_EQ(objectProjectPath("meshes/readme.txt"), "");
        EXPECT_EQ(objectFolder("Meshes\\Clutter\\Ruins\\Pillar\\RuinsPuzzlePillar01.nif"), "meshes/clutter/ruins/pillar/");
        EXPECT_EQ(objectFolder("a.nif"), "");
    }

    TEST(HavokObjectProject, a_clip_is_called_by_its_own_name_and_by_the_scripts_trigger_names)
    {
        const std::vector<std::string> names = eventNamesOfClip("ruinspillartrigger02");
        EXPECT_TRUE(has(names, "ruinspillartrigger02"));
        EXPECT_TRUE(has(names, "trigger02"));
        EXPECT_FALSE(has(names, "trigger01"));
        // the rings of the Hall of Stories door: largewheel01..03 are Trigger01..03
        EXPECT_TRUE(has(eventNamesOfClip("largewheel03"), "trigger03"));
        EXPECT_TRUE(has(eventNamesOfClip("keyhole01"), "trigger01"));
    }

    TEST(HavokObjectProject, open_and_close_keep_their_names_and_down_is_the_open_clip_of_a_ring)
    {
        EXPECT_TRUE(has(eventNamesOfClip("open"), "open"));
        EXPECT_TRUE(has(eventNamesOfClip("close"), "close"));
        EXPECT_TRUE(has(eventNamesOfClip("largewheeldown"), "open"));
        EXPECT_TRUE(has(eventNamesOfClip("keyholedown"), "open"));
        EXPECT_FALSE(has(eventNamesOfClip("largewheeldown"), "trigger01"));
        EXPECT_TRUE(eventNamesOfClip("leverpush").size() >= 1);
        EXPECT_FALSE(has(eventNamesOfClip("leverpush"), "trigger01"));
    }

    TEST(HavokObjectProject, a_start_state_event_names_the_clip_whose_first_pose_it_holds)
    {
        EXPECT_EQ(startStateClipEvent("StartState02"), "trigger02");
        EXPECT_EQ(startStateClipEvent("startstate03"), "trigger03");
        EXPECT_EQ(startStateClipEvent("StartState"), "");
        EXPECT_EQ(startStateClipEvent("Trigger01"), "");
        EXPECT_EQ(startStateClipEvent("StartState99999"), "");
    }

    TEST(HavokObjectProject, the_stem_of_a_clip_path_is_lower_case_without_folder_or_extension)
    {
        EXPECT_EQ(clipStem("animations/RuinsPillarTrigger01.hkx"), "ruinspillartrigger01");
        EXPECT_EQ(clipStem("KeyholeAnimations\\keyhole01.hkx"), "keyhole01");
        EXPECT_EQ(clipStem("noext"), "noext");
    }
}
