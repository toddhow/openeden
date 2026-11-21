// Task 0338 (docs/tasks/0338-skyrim-black-star.md): Azura speaks through a TALKING ACTIVATOR (TACT DA01VoiceofAzura,
// the altar). What a TACT needs of the pack, generated plugins, no game data. Written before the fields existed (red).
//  * SkyrimBlackStarTalking.records: the TACT's voice type (VNAM) is kept in its model record; an ACTI has none.
//  * SkyrimBlackStarTalking.sounds: a line meant for an NPC (the INFO's speaker) is spoken by a talking activator that
//  has
//    that NPC's voice type (Azura's altar says the lines of the NPC Azura), and by no other.
//  * SkyrimBlackStarTalking.voice: the condition world answers the voice type of a speaker that is no NPC_ from a
//  lookup
//    (the pack gives the TACT's), and nothing without it.
#include <game_skyrimse/actors.hpp>
#include <game_skyrimse/dialogue.hpp>
#include <game_skyrimse/records.hpp>

#include <gtest/gtest.h>

#include <memory>
#include <sstream>
#include <string>

#include "../contentsource/pluginbuilder.hpp"

namespace
{
    using namespace ContentTests;
    using namespace game_skyrimse;

    constexpr FormId kTact = 0x01000A10;
    constexpr FormId kVoice = 0x01000A11;

    LoadOrder order()
    {
        LoadOrder o;
        std::string err;
        EXPECT_TRUE(o.add("Skyrim.esm", false, err));
        EXPECT_TRUE(o.add("test.esp", false, err));
        return o;
    }

    bool parse(const char (&type)[5], const Bytes& subs, ParsedPlugin& plugin)
    {
        Bytes bytes = tes4Header(1.7f, 0, "A", "B", 1, { { "Skyrim.esm", 0 } }, 24, 44);
        bytes.append(group4(Content::fourCC(type), 0, record4(type, 0, kTact, subs)));
        Content::Error error;
        const bool ok = parseRecordsBytes(bytes.str(), "test.esp", order(), ParseLimits(), plugin, error);
        EXPECT_TRUE(ok) << error.describe();
        return ok;
    }

    TEST(SkyrimBlackStarTalking, aTalkingActivatorKeepsItsVoiceType)
    {
        Bytes subs;
        subs.append(sub4("EDID", Bytes().zstring("DA01VoiceofAzura")));
        subs.append(sub4("MODL", Bytes().zstring("Markers/XMarker.nif")));
        subs.append(sub4("VNAM", Bytes().u32(kVoice)));
        ParsedPlugin plugin;
        ASSERT_TRUE(parse("TACT", subs, plugin));
        ASSERT_EQ(plugin.mModels.size(), 1u);
        EXPECT_EQ(plugin.mModels[0].mVoiceType, kVoice);
    }

    TEST(SkyrimBlackStarTalking, anActivatorHasNoVoiceType)
    {
        Bytes subs;
        subs.append(sub4("EDID", Bytes().zstring("Lever")));
        subs.append(sub4("MODL", Bytes().zstring("Lever.nif")));
        subs.append(sub4("VNAM", Bytes().u32(kVoice))); // a stray subrecord of another type is not read as a voice
        ParsedPlugin plugin;
        ASSERT_TRUE(parse("ACTI", subs, plugin));
        ASSERT_EQ(plugin.mModels.size(), 1u);
        EXPECT_EQ(plugin.mModels[0].mVoiceType, 0u);
    }

    TEST(SkyrimBlackStarTalking, aSpeakerThatIsNoNpcTakesItsVoiceTypeFromTheLookup)
    {
        ActorIndex actors;
        DialogueIndex dialogue;
        SkyrimConditionWorld world(actors, dialogue);
        DialogueSpeaker speaker;
        speaker.mBase = kTact;
        world.setSpeaker(speaker);
        EXPECT_EQ(world.speakerVoiceType(), kNullFormId) << "nothing says what it sounds like";
        world.setActivatorVoiceLookup([](FormId base) { return base == kTact ? kVoice : kNullFormId; });
        world.setSpeaker(speaker);
        EXPECT_EQ(world.speakerVoiceType(), kVoice);
        speaker.mBase = 0x01000A99;
        world.setSpeaker(speaker);
        EXPECT_EQ(world.speakerVoiceType(), kNullFormId);
    }

    // an NPC_ record with a voice type (VTCK)
    Bytes npcBytes(FormId id, FormId voice)
    {
        Bytes npc;
        npc.append(sub4("EDID", Bytes().zstring("Azura")));
        Bytes acbs;
        acbs.u32(0).u16(0).u16(0).u16(5).u16(1).u16(50).u16(100).u16(0).u16(0).u16(0).u16(0);
        npc.append(sub4("ACBS", acbs));
        npc.append(sub4("RNAM", Bytes().u32(0x00013746)));
        npc.append(sub4("VTCK", Bytes().u32(voice)));
        Bytes bytes = tes4Header(1.7f, 0, "A", "B", 1, { { "Skyrim.esm", 0 } }, 24, 44);
        bytes.append(group4(Content::fourCC("NPC_"), 0, record4("NPC_", 0, id, npc)));
        return bytes;
    }

    TEST(SkyrimBlackStarTalking, aTalkingActivatorSpeaksTheLinesOfTheNpcThatHasItsVoiceType)
    {
        constexpr FormId kAzuraNpc = 0x01000A20;
        constexpr FormId kOtherNpc = 0x01000A21;
        ActorRecords records;
        Content::Error error;
        ASSERT_TRUE(parseActorRecordsBytes(
            npcBytes(kAzuraNpc, kVoice).str(), "test.esp", order(), ActorLimits(), records, error))
            << error.mMessage;
        ActorRecords others;
        ASSERT_TRUE(parseActorRecordsBytes(
            npcBytes(kOtherNpc, 0x01000A22).str(), "test.esp", order(), ActorLimits(), others, error))
            << error.mMessage;
        ActorIndex actors;
        actors.add(records);
        actors.add(others);
        DialogueIndex dialogue;
        SkyrimConditionWorld world(actors, dialogue);
        DialogueSpeaker speaker;
        speaker.mBase = kTact;
        world.setSpeaker(speaker);
        EXPECT_FALSE(world.speakerSoundsLike(kAzuraNpc)) << "no lookup: the old rule only";
        world.setActivatorVoiceLookup([](FormId base) { return base == kTact ? kVoice : kNullFormId; });
        world.setSpeaker(speaker);
        EXPECT_TRUE(world.speakerSoundsLike(kAzuraNpc));
        EXPECT_FALSE(world.speakerSoundsLike(kOtherNpc));
        EXPECT_FALSE(world.speakerSoundsLike(0x01000A99)) << "an unknown NPC";
    }
}
