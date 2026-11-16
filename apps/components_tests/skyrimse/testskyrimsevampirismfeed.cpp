// Task 0260 (vampirism): what the cell source gained for the player's feeding on a sleeper and for the sunlight test: the feed
// hook is asked only for an actor that cannot talk (asleep or getting up), the feed turn is one closing line, and the place the
// core told is readable as the effect-condition place. No game data.
#include <game_skyrimse/diseases.hpp>
#include <game_skyrimse/neutral_world.hpp>
#include <game_skyrimse/world_view.hpp>

#include <gtest/gtest.h>

#include <map>
#include <memory>
#include <string>
#include <vector>

namespace
{
    using namespace game_skyrimse;
    using namespace openeden::sdk::game;

    TEST(SkyrimVampirismFeed, the_feed_hook_is_not_asked_for_an_actor_the_brain_does_not_know)
    {
        SkyrimWorldView view;
        SkyrimCellSource source(view);
        int asked = 0;
        source.setFeedHook([&asked](WorldId) {
            ++asked;
            return true;
        });
        auto* dialogue = dynamic_cast<DialogueSource*>(&source);
        ASSERT_NE(dialogue, nullptr);
        EXPECT_TRUE(dialogue->prepareConversation(10));
        EXPECT_EQ(asked, 0);
        EXPECT_TRUE(dialogue->readyForConversation(10));
    }

    TEST(SkyrimVampirismFeed, the_feed_turn_is_one_line_that_closes_the_box)
    {
        const DialogueTurn turn = SkyrimCellSource::feedTurn();
        EXPECT_FALSE(turn.text.empty());
        EXPECT_TRUE(turn.endsConversation);
        EXPECT_TRUE(turn.topics.empty());
        EXPECT_TRUE(turn.service.empty());
    }

    TEST(SkyrimVampirismFeed, with_no_feed_hook_a_conversation_is_prepared_as_before)
    {
        SkyrimWorldView view;
        SkyrimCellSource source(view);
        auto* dialogue = dynamic_cast<DialogueSource*>(&source);
        ASSERT_NE(dialogue, nullptr);
        EXPECT_TRUE(dialogue->prepareConversation(10));
    }

    TEST(SkyrimVampirismFeed, the_place_the_core_told_is_the_effect_condition_place_and_unknown_before)
    {
        SkyrimWorldView view;
        SkyrimCellSource source(view);
        effectconditions::Place place;
        EXPECT_FALSE(source.currentPlace(place));
        PlayerPlace outdoors;
        outdoors.interior = false;
        outdoors.world = 0x3C;
        source.onPlayerPlace(outdoors);
        ASSERT_TRUE(source.currentPlace(place));
        EXPECT_TRUE(place.mKnown);
        EXPECT_FALSE(place.mInterior);
        EXPECT_EQ(place.mWorld, 0x3Cu);
        PlayerPlace inside;
        inside.interior = true;
        inside.cell = 77;
        source.onPlayerPlace(inside);
        ASSERT_TRUE(source.currentPlace(place));
        EXPECT_TRUE(place.mInterior);
        EXPECT_EQ(place.mWorld, 0u);
    }

    // ---- Sanguinare Vampiris goes at the turn without a "cured" message -----------------------------------------------------

    class TinyDiseaseLibrary final : public PerkLibrary
    {
    public:
        TinyDiseaseLibrary()
        {
            add(0x0B8780, "DiseaseSanguinareVampiris");
            add(0x0B877E, "DiseaseBoneBreakFever");
        }
        const PerkRecord* perk(FormId) const override { return nullptr; }
        const SpellRecord* spell(FormId id) const override
        {
            const auto it = mSpells.find(id);
            return it == mSpells.end() ? nullptr : &it->second;
        }
        const MagicEffectRecord* magicEffect(FormId) const override { return nullptr; }

    private:
        void add(FormId id, const char* editorId)
        {
            SpellRecord s;
            s.mCommon.mId = id;
            s.mCommon.mEditorId = editorId;
            s.mHasData = true;
            s.mType = SpellRecord::kTypeDisease;
            mSpells[id] = s;
        }
        std::map<FormId, SpellRecord> mSpells;
    };

    struct CureRig
    {
        CureRig()
        {
            diseases::DiseaseService::Hooks hooks;
            hooks.mLibrary = [this]() -> const PerkLibrary* { return &library; };
            service = std::make_unique<diseases::DiseaseService>(std::move(hooks), diseases::Settings());
        }
        TinyDiseaseLibrary library;
        std::unique_ptr<diseases::DiseaseService> service;
    };

    TEST(SkyrimVampirismCure, cureOne_removes_that_disease_only_and_can_do_it_quietly)
    {
        CureRig rig;
        ASSERT_TRUE(rig.service->infect(0x0B8780));
        ASSERT_TRUE(rig.service->infect(0x0B877E));
        const std::uint64_t notes = rig.service->notificationCount();
        const std::uint64_t revision = rig.service->revision();
        EXPECT_TRUE(rig.service->cureOne(0x0B8780, false));
        EXPECT_FALSE(rig.service->has(0x0B8780));
        EXPECT_TRUE(rig.service->has(0x0B877E));
        EXPECT_EQ(rig.service->notificationCount(), notes); // quiet
        EXPECT_GT(rig.service->revision(), revision);
        EXPECT_FALSE(rig.service->cureOne(0x0B8780, false)); // already gone
        EXPECT_TRUE(rig.service->cureOne(0x0B877E, true));
        EXPECT_EQ(rig.service->notificationCount(), notes + 1);
        EXPECT_EQ(rig.service->notificationAt(notes + 1), "You have been cured of Bone Break Fever");
    }
}
