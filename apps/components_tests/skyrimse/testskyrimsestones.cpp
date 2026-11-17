// Task 0237, milestone M-S6 (standing stones and shrines): the pure rules (skill experience from a stone's perk, what a
// spell does to the player, the timed blessings, the message box state and the save chunk) on a fake perk library, and
// the Papyrus natives the stones' scripts call (Actor.HasSpell and RemoveSpell, Spell.Cast, Message.Show with its
// answer and replay) on generated scripts. No game data.
#include <game_skyrimse/papyrus_host.hpp>
#include <game_skyrimse/quest.hpp>
#include <game_skyrimse/stones.hpp>
#include <game_skyrimse/vmad.hpp>

#include <gtest/gtest.h>

#include <array>
#include <cstring>
#include <iostream>
#include <map>
#include <memory>
#include <set>
#include <sstream>
#include <string>
#include <vector>

#include "../contentsource/pluginbuilder.hpp"
#include "../papyrus/papyrustestutil.hpp"

namespace
{
    using namespace ContentTests;
    using namespace game_skyrimse;
    using PexAsm::Bool;
    using PexAsm::Flt;
    using PexAsm::Id;
    using PexAsm::Int;
    using PexAsm::ScriptBuilder;

    constexpr FormId kWarriorSpell = 0x0E5F4C;
    constexpr FormId kWarriorEffect = 0x0E5F4B;
    constexpr FormId kWarriorPerk = 0x0E5F4A;
    constexpr FormId kLordSpell = 0x0E5F58;
    constexpr FormId kLordEffect = 0x0E5F55;
    constexpr FormId kMageSpell = 0x0E5F47;
    constexpr FormId kMageEffect = 0x0E5F48;
    constexpr FormId kMagePerk = 0x0E5F49;
    constexpr FormId kAltarKynareth = 0x0FB997;
    constexpr FormId kAltarTalos = 0x0FB99A;
    constexpr FormId kFortifyStamina = 0x0FB98F;
    constexpr FormId kCureDisease = 0x0FBFF5;
    constexpr FormId kInstantSpell = 0x000999;
    constexpr FormId kMessage = 0x016869;
    constexpr FormId kPlayer = 0x14;

    // ---- a fake library
    // -----------------------------------------------------------------------------------------------

    class FakeLibrary final : public PerkLibrary
    {
    public:
        const PerkRecord* perk(FormId id) const override
        {
            const auto it = mPerks.find(id);
            return it == mPerks.end() ? nullptr : &it->second;
        }
        const SpellRecord* spell(FormId id) const override
        {
            const auto it = mSpells.find(id);
            return it == mSpells.end() ? nullptr : &it->second;
        }
        const MagicEffectRecord* magicEffect(FormId id) const override
        {
            const auto it = mEffects.find(id);
            return it == mEffects.end() ? nullptr : &it->second;
        }

        void addSkillUsePerk(FormId perkId, float factor, std::vector<std::uint32_t> skillValues)
        {
            PerkRecord perk;
            perk.mCommon.mId = perkId;
            PerkEffect e;
            e.mKind = PerkEffectKind::EntryPoint;
            e.mEntryPoint = stones::kEntryModSkillUse;
            e.mFunction = PerkFunction::Multiply;
            e.mParamType = 1;
            e.mValue = factor;
            PerkTab tab;
            for (const std::uint32_t av : skillValues)
            {
                survey::Ctda c;
                c.mFunction = stones::kFunctionIsAdvanceSkill;
                c.mParam1 = av;
                tab.mConditions.push_back(c);
            }
            e.mTabs.push_back(tab);
            perk.mEffects.push_back(e);
            mPerks[perkId] = perk;
        }

        void addEffect(FormId id, std::uint32_t archetype, std::int32_t value, FormId perk = kNullFormId)
        {
            MagicEffectRecord m;
            m.mCommon.mId = id;
            m.mHasData = true;
            m.mArchetype = archetype;
            m.mPrimaryValue = value;
            m.mPerkToApply = perk;
            mEffects[id] = m;
        }

        void addSpell(
            FormId id, const std::string& editorId, std::vector<std::pair<FormId, std::uint32_t>> effects, bool ability)
        {
            SpellRecord s;
            s.mCommon.mId = id;
            s.mCommon.mEditorId = editorId;
            s.mHasData = true;
            s.mType = ability ? SpellRecord::kTypeAbility : 0;
            for (const auto& [effect, duration] : effects)
            {
                SpellEffect e;
                e.mEffect = effect;
                e.mDuration = duration;
                s.mEffects.push_back(e);
            }
            mSpells[id] = s;
        }

        std::map<FormId, PerkRecord> mPerks;
        std::map<FormId, SpellRecord> mSpells;
        std::map<FormId, MagicEffectRecord> mEffects;
    };

    // the Warrior stone's perk covers skills 6, 7, 9, 10 and 11 (VERIFIED on Skyrim.esm); the Mage stone's 18 to 23
    FakeLibrary makeLibrary()
    {
        FakeLibrary lib;
        lib.addSkillUsePerk(kWarriorPerk, 1.2f, { 6, 7, 9, 10, 11 });
        lib.addEffect(kWarriorEffect, 0, 24, kWarriorPerk);
        lib.addSpell(kWarriorSpell, "doomWarriorAbility", { { kWarriorEffect, 0 } }, true);
        lib.addSkillUsePerk(kMagePerk, 1.2f, { 18, 19, 20, 21, 22, 23 });
        lib.addEffect(kMageEffect, 1, -1, kMagePerk);
        lib.addSpell(kMageSpell, "doomMageAbility", { { kMageEffect, 0 } }, true);
        lib.addEffect(kLordEffect, 0, stones::kValueDamageResist);
        lib.addSpell(kLordSpell, "doomLordAbility", { { kLordEffect, 0 } }, true);
        lib.addEffect(kFortifyStamina, 34, 26);
        lib.addSpell(kAltarKynareth, "AltarKynarethSpell", { { kFortifyStamina, 28800 }, { kCureDisease, 0 } }, false);
        lib.addEffect(kCureDisease, 3, -1);
        lib.addSpell(kAltarTalos, "AltarTalosSpell", { { kFortifyStamina, 28800 } }, false);
        lib.addSpell(kInstantSpell, "InstantSpell", { { kCureDisease, 0 } }, false);
        return lib;
    }

    struct ServiceRig
    {
        ServiceRig()
            : lib(makeLibrary())
        {
            stones::StonesService::Hooks hooks;
            hooks.mLibrary = [this] { return &lib; };
            hooks.mPersistent = [this] { return persistent; };
            hooks.mNow = [this] { return now; };
            hooks.mApply
                = [this](std::span<const FormId> spells) { applied.emplace_back(spells.begin(), spells.end()); };
            hooks.mAnswer = [this](FormId message, std::int32_t button) {
                answers.emplace_back(message, button);
                return true;
            };
            service = std::make_unique<stones::StonesService>(std::move(hooks));
        }
        FakeLibrary lib;
        std::vector<FormId> persistent;
        double now = 0.0;
        std::vector<std::vector<FormId>> applied;
        std::vector<std::pair<FormId, std::int32_t>> answers;
        std::unique_ptr<stones::StonesService> service;
    };

    // ---- skill experience
    // --------------------------------------------------------------------------------------------

    TEST(SkyrimStonesScale, the_warrior_stone_multiplies_its_five_skills_and_no_other)
    {
        const FakeLibrary lib = makeLibrary();
        const std::vector<FormId> spells{ kWarriorSpell };
        EXPECT_FLOAT_EQ(stones::skillUseScale(lib, spells, 0), 1.2f); // One-handed (actor value 6)
        EXPECT_FLOAT_EQ(stones::skillUseScale(lib, spells, 1), 1.2f);
        EXPECT_FLOAT_EQ(stones::skillUseScale(lib, spells, 3), 1.2f); // Block (9)
        EXPECT_FLOAT_EQ(stones::skillUseScale(lib, spells, 5), 1.2f); // Heavy armour (11)
        EXPECT_FLOAT_EQ(stones::skillUseScale(lib, spells, 2), 1.f); // Archery (8) is the Thief's
        EXPECT_FLOAT_EQ(stones::skillUseScale(lib, spells, 12), 1.f);
    }

    TEST(SkyrimStonesScale, nothing_applies_without_a_stone_and_a_stone_without_a_perk_does_nothing)
    {
        const FakeLibrary lib = makeLibrary();
        EXPECT_FLOAT_EQ(stones::skillUseScale(lib, {}, 0), 1.f);
        const std::vector<FormId> lord{ kLordSpell, 0x777777 };
        EXPECT_FLOAT_EQ(stones::skillUseScale(lib, lord, 0), 1.f);
    }

    TEST(SkyrimStonesScale, two_spells_multiply_and_the_order_does_not_matter)
    {
        const FakeLibrary lib = makeLibrary();
        const std::vector<FormId> a{ kWarriorSpell, kMageSpell };
        const std::vector<FormId> b{ kMageSpell, kWarriorSpell, kWarriorSpell };
        EXPECT_FLOAT_EQ(stones::skillUseScale(lib, a, 0), 1.2f);
        EXPECT_FLOAT_EQ(stones::skillUseScale(lib, a, 12), 1.2f); // Destruction (20)
        EXPECT_FLOAT_EQ(stones::skillUseScale(lib, b, 0), stones::skillUseScale(lib, a, 0));
    }

    TEST(SkyrimStonesScale, a_perk_with_no_skill_condition_covers_every_skill)
    {
        FakeLibrary lib = makeLibrary();
        lib.addSkillUsePerk(kMagePerk, 1.15f, {}); // the Lover stone's perk names no skill
        const std::vector<FormId> spells{ kMageSpell };
        for (std::size_t slot = 0; slot < 18; ++slot)
            EXPECT_FLOAT_EQ(stones::skillUseScale(lib, spells, slot), 1.15f);
    }

    // ---- what the runtime does with a spell ------------------------------------------------------------------------

    TEST(SkyrimStonesSupport, an_experience_stone_and_a_damage_resist_stone_work_a_blessing_is_a_status)
    {
        FakeLibrary lib = makeLibrary();
        EXPECT_EQ(stones::supportOf(lib, kWarriorSpell), stones::Support::Works);
        EXPECT_EQ(stones::supportOf(lib, kLordSpell), stones::Support::Works);
        // the real Lord stone has a second effect (magic resist) that is only kept as a status
        lib.addEffect(0x0E5F56, 0, stones::kValueMagicResist);
        lib.addSpell(0x0E5F59, "doomLordAbilityReal", { { kLordEffect, 0 }, { 0x0E5F56, 0 } }, true);
        EXPECT_EQ(stones::supportOf(lib, 0x0E5F59), stones::Support::Partly);
        EXPECT_EQ(stones::supportOf(lib, kAltarTalos), stones::Support::Partly);
        EXPECT_EQ(stones::supportOf(lib, kAltarKynareth), stones::Support::Partly); // Fortify Stamina and a cure
        EXPECT_EQ(stones::supportOf(lib, kInstantSpell), stones::Support::Nothing);
        EXPECT_EQ(stones::supportOf(lib, 0x424242), stones::Support::Nothing);
        EXPECT_EQ(stones::supportName(stones::Support::Works), "works");
    }

    TEST(SkyrimStonesSupport, the_duration_is_the_longest_effect)
    {
        const FakeLibrary lib = makeLibrary();
        EXPECT_DOUBLE_EQ(stones::durationOf(lib, kAltarKynareth), 28800.0);
        EXPECT_DOUBLE_EQ(stones::durationOf(lib, kWarriorSpell), 0.0);
        EXPECT_DOUBLE_EQ(stones::durationOf(lib, 0x1), 0.0);
    }

    // ---- the service
    // ---------------------------------------------------------------------------------------------------

    TEST(SkyrimStonesService, a_blessing_is_timed_and_ends_on_the_game_clock)
    {
        ServiceRig rig;
        rig.now = 100.0;
        ASSERT_TRUE(rig.service->cast(kAltarKynareth, kPlayer));
        rig.service->poll();
        EXPECT_EQ(rig.service->active(), std::vector<FormId>{ kAltarKynareth });
        ASSERT_FALSE(rig.applied.empty());
        EXPECT_EQ(rig.applied.back(), std::vector<FormId>{ kAltarKynareth });
        rig.now = 100.0 + 28799.0;
        rig.service->poll();
        EXPECT_EQ(rig.service->timed().size(), 1u);
        rig.now = 100.0 + 28800.0;
        rig.service->poll();
        EXPECT_TRUE(rig.service->timed().empty());
        EXPECT_TRUE(rig.applied.back().empty());
    }

    TEST(SkyrimStonesService, a_new_shrine_replaces_the_blessing_of_the_last_one)
    {
        ServiceRig rig;
        ASSERT_TRUE(rig.service->cast(kAltarKynareth, kPlayer));
        ASSERT_TRUE(rig.service->cast(kAltarTalos, kPlayer));
        EXPECT_EQ(rig.service->timed(), std::vector<FormId>{ kAltarTalos });
        // the same shrine again refreshes, it does not stack
        ASSERT_TRUE(rig.service->cast(kAltarTalos, kPlayer));
        EXPECT_EQ(rig.service->timed().size(), 1u);
    }

    TEST(SkyrimStonesService, only_the_player_is_blessed_and_an_instant_spell_keeps_nothing)
    {
        ServiceRig rig;
        EXPECT_FALSE(rig.service->cast(kAltarKynareth, 0x01000A00));
        EXPECT_FALSE(rig.service->cast(0x424242, kPlayer));
        EXPECT_TRUE(rig.service->cast(kInstantSpell, kPlayer));
        EXPECT_TRUE(rig.service->timed().empty());
    }

    TEST(SkyrimStonesService, persistent_and_timed_spells_make_the_active_list_and_it_is_applied_once_per_change)
    {
        ServiceRig rig;
        rig.persistent = { kWarriorSpell };
        rig.service->poll();
        rig.service->poll();
        EXPECT_EQ(rig.applied.size(), 1u);
        rig.persistent = { kMageSpell };
        rig.service->poll();
        ASSERT_EQ(rig.applied.size(), 2u);
        EXPECT_EQ(rig.applied.back(), std::vector<FormId>{ kMageSpell });
    }

    TEST(SkyrimStonesService, the_skill_scale_follows_the_active_spells)
    {
        ServiceRig rig;
        EXPECT_FLOAT_EQ(rig.service->skillScale(0), 1.f);
        rig.persistent = { kWarriorSpell };
        EXPECT_FLOAT_EQ(rig.service->skillScale(0), 1.2f);
        rig.persistent = { kMageSpell };
        EXPECT_FLOAT_EQ(rig.service->skillScale(0), 1.f);
    }

    TEST(SkyrimStonesService, a_message_waits_for_a_button_and_tells_the_host)
    {
        ServiceRig rig;
        EXPECT_FALSE(rig.service->messageOpen());
        PapyrusMessageInfo info;
        info.mText = "Warrior";
        info.mButtons = { "Yes", "No" };
        info.mBox = true;
        rig.service->ask(kMessage, info);
        ASSERT_TRUE(rig.service->messageOpen());
        EXPECT_EQ(rig.service->messageText(), "Warrior");
        EXPECT_FALSE(rig.service->choose(2));
        EXPECT_FALSE(rig.service->choose(-1));
        EXPECT_TRUE(rig.service->choose(0));
        EXPECT_FALSE(rig.service->messageOpen());
        ASSERT_EQ(rig.answers.size(), 1u);
        EXPECT_EQ(rig.answers[0], (std::pair<FormId, std::int32_t>{ kMessage, 0 }));
        EXPECT_FALSE(rig.service->choose(0)); // nothing open any more
    }

    // ---- the chunk
    // ---------------------------------------------------------------------------------------------------------

    class MemoryWriter final : public openeden::sdk::ChunkWriter
    {
    public:
        void write(const void* data, std::size_t size) override
        {
            const auto* p = static_cast<const std::uint8_t*>(data);
            mBytes.insert(mBytes.end(), p, p + size);
        }
        std::vector<std::uint8_t> mBytes;
    };

    class MemoryReader final : public openeden::sdk::ChunkReader
    {
    public:
        explicit MemoryReader(std::vector<std::uint8_t> bytes)
            : mBytes(std::move(bytes))
        {
        }
        bool read(void* data, std::size_t size) override
        {
            if (size > remaining())
                return false;
            std::memcpy(data, mBytes.data() + mAt, size);
            mAt += size;
            return true;
        }
        std::size_t remaining() const override { return mBytes.size() - mAt; }

    private:
        std::vector<std::uint8_t> mBytes;
        std::size_t mAt = 0;
    };

    TEST(SkyrimStonesChunk, the_timed_spells_round_trip_and_a_damaged_chunk_changes_nothing)
    {
        ServiceRig a;
        a.now = 50.0;
        ASSERT_TRUE(a.service->cast(kAltarKynareth, kPlayer));
        MemoryWriter writer;
        a.service->chunk().write(writer);
        EXPECT_EQ(a.service->chunk().chunkName(), "stones");

        ServiceRig b;
        MemoryReader good(writer.mBytes);
        ASSERT_TRUE(b.service->chunk().read(good, 1));
        ASSERT_EQ(b.service->timedSpells().size(), 1u);
        EXPECT_EQ(b.service->timedSpells()[0].mSpell, kAltarKynareth);
        EXPECT_DOUBLE_EQ(b.service->timedSpells()[0].mEnds, 50.0 + 28800.0);

        ServiceRig c;
        MemoryReader wrongVersion(writer.mBytes);
        EXPECT_FALSE(c.service->chunk().read(wrongVersion, 2));
        std::vector<std::uint8_t> cut = writer.mBytes;
        cut.pop_back();
        MemoryReader truncated(cut);
        EXPECT_FALSE(c.service->chunk().read(truncated, 1));
        std::vector<std::uint8_t> huge = writer.mBytes;
        huge[0] = 0xFF;
        MemoryReader tooMany(huge);
        EXPECT_FALSE(c.service->chunk().read(tooMany, 1));
        EXPECT_TRUE(c.service->timedSpells().empty());
    }

    // ---- the Papyrus natives
    // -----------------------------------------------------------------------------------------------

    constexpr FormId kQuestA = 0x01000800;
    constexpr FormId kStoneRef = 0x01000A00;
    constexpr FormId kBaseA = 0x01000B00;
    constexpr FormId kSpellA = 0x01000D01;
    constexpr FormId kBlessing = 0x01000D02;
    constexpr FormId kMessageA = 0x01000E01;
    constexpr FormId kNoteMessage = 0x01000E02;

    LoadOrder order()
    {
        LoadOrder o;
        std::string err;
        EXPECT_TRUE(o.add("Skyrim.esm", false, err));
        EXPECT_TRUE(o.add("test.esp", false, err));
        return o;
    }

    Bytes questRecord()
    {
        Bytes b;
        b.append(sub4("EDID", Bytes().zstring("QStone")));
        b.append(sub4("DNAM", Bytes().u16(QuestFlags::StartGameEnabled).u8(30).u8(0).u32(0).u32(6)));
        b.append(sub4("NEXT", Bytes()));
        b.append(sub4("INDX", Bytes().u16(10).u8(0).u8(0)));
        b.append(sub4("QSDT", Bytes().u8(0)));
        return record4("QUST", 0u, kQuestA, b);
    }

    class FakeData final : public PapyrusGameData
    {
    public:
        bool readPex(const std::string& name, std::vector<std::uint8_t>& bytes) override
        {
            const auto it = mScripts.find(name);
            if (it == mScripts.end())
                return false;
            bytes = it->second;
            return true;
        }
        std::string formClass(FormId form) const override
        {
            const auto it = mClasses.find(form);
            return it != mClasses.end() ? it->second : std::string();
        }
        bool messageInfo(FormId message, PapyrusMessageInfo& out) override
        {
            const auto it = mMessages.find(message);
            if (it == mMessages.end())
                return false;
            out = it->second;
            return true;
        }
        void add(const ScriptBuilder& script)
        {
            std::string lower = script.name();
            for (char& c : lower)
                c = static_cast<char>(c >= 'A' && c <= 'Z' ? c + 32 : c);
            mScripts[lower] = script.build(PapyrusTest::optionsFor(PexAsm::Target::SkyrimSE)).mBytes;
        }
        std::map<std::string, std::vector<std::uint8_t>> mScripts;
        std::map<FormId, std::string> mClasses;
        std::map<FormId, PapyrusMessageInfo> mMessages;
    };

    void addClasses(FakeData& data)
    {
        ScriptBuilder form("Form");
        form.function("GetFormID", "Int").native();
        data.add(form);
        ScriptBuilder spell("Spell");
        spell.parent("Form");
        spell.function("Cast").native().param("akSource", "ObjectReference").param("akTarget", "ObjectReference");
        data.add(spell);
        ScriptBuilder message("Message");
        message.parent("Form");
        auto& show = message.function("Show", "Int").native();
        for (int i = 0; i < 9; ++i)
            show.param("a" + std::to_string(i), "Float");
        data.add(message);
        ScriptBuilder ref("ObjectReference");
        ref.parent("Form");
        data.add(ref);
        ScriptBuilder actor("Actor");
        actor.parent("ObjectReference");
        actor.function("AddSpell", "Bool").native().param("akSpell", "Spell").param("abVerbose", "Bool");
        actor.function("RemoveSpell", "Bool").native().param("akSpell", "Spell");
        actor.function("HasSpell", "Bool").native().param("akForm", "Form");
        data.add(actor);
        ScriptBuilder game("Game");
        game.function("GetPlayer", "Actor").global().native();
        game.function("AddAchievement").global().native().param("aiAchievementID", "Int");
        data.add(game);
        ScriptBuilder utility("Utility");
        utility.function("Wait").global().native().param("afWaitTimeSeconds", "Float");
        data.add(utility);
    }

    ScriptBuilder stoneScript()
    {
        ScriptBuilder s("TestStone");
        s.parent("ObjectReference");
        s.var("hasAfterAdd", "Bool");
        s.var("hasAfterRemove", "Bool");
        s.var("removedTwice", "Bool");
        s.var("choice", "Int", Int(-7));
        s.var("runs", "Int");
        s.var("noteChoice", "Int", Int(-7));
        s.autoProperty("TheSpell", "Spell");
        s.autoProperty("Blessing", "Spell");
        s.autoProperty("Box", "Message");
        s.autoProperty("Note", "Message");
        s.function("Spells")
            .local("p", "Actor")
            .callStatic("Game", "GetPlayer", Id("p"))
            .local("b", "Bool")
            .callMethod("AddSpell", Id("p"), Id("b"), { Id("::TheSpell_var"), Bool(true) })
            .callMethod("HasSpell", Id("p"), Id("b"), { Id("::TheSpell_var") })
            .assign(Id("hasAfterAdd"), Id("b"))
            .callMethod("RemoveSpell", Id("p"), Id("b"), { Id("::TheSpell_var") })
            .callMethod("HasSpell", Id("p"), Id("b"), { Id("::TheSpell_var") })
            .assign(Id("hasAfterRemove"), Id("b"))
            .callMethod("RemoveSpell", Id("p"), Id("b"), { Id("::TheSpell_var") })
            .assign(Id("removedTwice"), Id("b"));
        s.function("CastOnPlayer")
            .local("p", "Actor")
            .callStatic("Game", "GetPlayer", Id("p"))
            .local("n", "None")
            .callMethod("Cast", Id("::Blessing_var"), Id("n"), { Id("p"), Id("p") });
        s.function("OnActivate")
            .param("akActionRef", "ObjectReference")
            .iadd(Id("runs"), Id("runs"), Int(1))
            .local("r", "Int")
            .callMethod("Show", Id("::Box_var"), Id("r"),
                { Flt(0), Flt(0), Flt(0), Flt(0), Flt(0), Flt(0), Flt(0), Flt(0), Flt(0) })
            .assign(Id("choice"), Id("r"))
            .callMethod("Show", Id("::Note_var"), Id("r"),
                { Flt(0), Flt(0), Flt(0), Flt(0), Flt(0), Flt(0), Flt(0), Flt(0), Flt(0) })
            .assign(Id("noteChoice"), Id("r"));
        return s;
    }

    vmad::Property objProp(const std::string& name, FormId form)
    {
        vmad::Property p;
        p.mName = name;
        p.mType = 1;
        p.mValue.mKind = vmad::ValueKind::Object;
        p.mValue.mForm = form;
        p.mValue.mAlias = -1;
        return p;
    }

    struct HostRig
    {
        HostRig()
        {
            addClasses(mData);
            mData.add(stoneScript());
            mData.mClasses[kStoneRef] = "ObjectReference";
            mData.mClasses[kSpellA] = "Spell";
            mData.mClasses[kBlessing] = "Spell";
            mData.mClasses[kMessageA] = "Message";
            mData.mClasses[kNoteMessage] = "Message";
            PapyrusMessageInfo box;
            box.mText = "Choose the Warrior?";
            box.mButtons = { "Yes", "No" };
            box.mBox = true;
            mData.mMessages[kMessageA] = box;
            PapyrusMessageInfo note;
            note.mText = "Blessing removed";
            mData.mMessages[kNoteMessage] = note;
            mHost = std::make_unique<PapyrusHost>(mData);
            mHost->setLog([this](bool error, const std::string& line) { (error ? mErrors : mLogs).push_back(line); });
            Bytes bytes = tes4Header(1.7f, 0, "A", "B", 1, { { "Skyrim.esm", 0 } }, 24, 44);
            bytes.append(group4(Content::fourCC("QUST"), 0, questRecord()));
            QuestRecords parsed;
            Content::Error error;
            EXPECT_TRUE(parseQuestRecordsBytes(bytes.str(), "test.esp", order(), QuestLimits(), parsed, error));
            std::unique_ptr<Content::Plugin> plugin = Content::Plugin::open(
                std::make_unique<std::istringstream>(bytes.str(), std::ios::binary), "test.esp", error);
            mIndex.add(std::move(parsed), FormIdResolver(plugin->header(), "test.esp", order()));
            mState = QuestState::initial(mIndex);
            mHost->setQuests(&mIndex, &mState, [](FormId) -> const vmad::Vmad* { return nullptr; });
            vmad::Script s;
            s.mName = "TestStone";
            s.mProperties = { objProp("TheSpell", kSpellA), objProp("Blessing", kBlessing), objProp("Box", kMessageA),
                objProp("Note", kNoteMessage) };
            EXPECT_EQ(mHost->attachReference(kStoneRef, kBaseA, false, { s }), 1u);
            mHost->setMessageSink(
                [this](FormId message, const PapyrusMessageInfo& info) { asked.emplace_back(message, info.mText); });
            tick();
        }
        ~HostRig()
        {
            if (::testing::Test::HasFailure())
                for (const std::string& line : mErrors)
                    std::cerr << "  error: " << line << std::endl;
        }
        void tick(int n = 3, float dt = 0.1f)
        {
            for (int i = 0; i < n; ++i)
                mHost->tick(dt);
        }
        double number(const char* name) { return mHost->numberVariable(kStoneRef, "TestStone", name).value_or(-99); }

        FakeData mData;
        std::unique_ptr<PapyrusHost> mHost;
        QuestIndex mIndex;
        QuestState mState;
        std::vector<std::string> mLogs;
        std::vector<std::string> mErrors;
        std::vector<std::pair<FormId, std::string>> asked;
    };

    TEST(SkyrimStonesNatives, HasSpell_and_RemoveSpell_follow_AddSpell_and_bump_the_revision)
    {
        HostRig rig;
        const std::uint64_t before = rig.mHost->spellRevision();
        ASSERT_TRUE(rig.mHost->callFunction(kStoneRef, "TestStone", "Spells"));
        EXPECT_EQ(rig.number("hasAfterAdd"), 1.0);
        EXPECT_EQ(rig.number("hasAfterRemove"), 0.0);
        EXPECT_EQ(rig.number("removedTwice"), 0.0);
        EXPECT_GE(rig.mHost->spellRevision(), before + 2);
        EXPECT_TRUE(rig.mHost->spellsOf(kPlayer).empty());
        EXPECT_EQ(rig.mHost->counters().mNatives.at("actor.hasspell"), 2u);
    }

    TEST(SkyrimStonesNatives, Spell_Cast_is_queued_for_the_pack_once_and_emptied_by_takeCasts)
    {
        HostRig rig;
        ASSERT_TRUE(rig.mHost->callFunction(kStoneRef, "TestStone", "CastOnPlayer"));
        const std::vector<PapyrusHost::SpellCast> casts = rig.mHost->takeCasts();
        ASSERT_EQ(casts.size(), 1u);
        EXPECT_EQ(casts[0].mSpell, kBlessing);
        EXPECT_EQ(casts[0].mTarget, kPlayer);
        EXPECT_EQ(casts[0].mSource, kPlayer);
        EXPECT_TRUE(rig.mHost->takeCasts().empty());
    }

    TEST(SkyrimStonesNatives, a_message_box_asks_the_player_and_answers_the_last_button_meanwhile)
    {
        HostRig rig;
        rig.mHost->onActivate(kStoneRef, kPlayer);
        rig.tick();
        ASSERT_EQ(rig.asked.size(), 1u);
        EXPECT_EQ(rig.asked[0].first, kMessageA);
        EXPECT_EQ(rig.asked[0].second, "Choose the Warrior?");
        EXPECT_EQ(rig.number("choice"), 1.0); // "No" while the player has not answered
        EXPECT_EQ(rig.number("noteChoice"), 0.0); // a corner text answers 0 at once
        EXPECT_EQ(rig.mHost->messagesAsked(), 1u);
        EXPECT_EQ(rig.mHost->notificationCount(), 1u);
        EXPECT_EQ(rig.mHost->notificationAt(1), "Blessing removed");
    }

    TEST(SkyrimStonesNatives, a_yes_runs_the_activation_again_with_that_answer_and_a_no_does_not)
    {
        HostRig rig;
        rig.mHost->onActivate(kStoneRef, kPlayer);
        rig.tick();
        ASSERT_TRUE(rig.mHost->answerMessage(kMessageA, 0));
        EXPECT_FALSE(rig.mHost->answerMessage(kMessageA, 0)); // asked once, answered once
        rig.tick();
        EXPECT_EQ(rig.number("runs"), 2.0);
        EXPECT_EQ(rig.number("choice"), 0.0);
        EXPECT_EQ(rig.asked.size(), 1u); // the replay did not ask again

        HostRig no;
        no.mHost->onActivate(kStoneRef, kPlayer);
        no.tick();
        ASSERT_TRUE(no.mHost->answerMessage(kMessageA, 1));
        no.tick();
        EXPECT_EQ(no.number("runs"), 1.0);
        EXPECT_EQ(no.number("choice"), 1.0);
    }

    TEST(SkyrimStonesNatives, an_answer_for_a_message_nobody_asked_is_refused)
    {
        HostRig rig;
        EXPECT_FALSE(rig.mHost->answerMessage(kMessageA, 0));
        EXPECT_FALSE(rig.mHost->answerMessage(0x424242, 0));
    }

    TEST(SkyrimStonesNatives, gameSeconds_is_script_time_at_the_time_scale)
    {
        HostRig rig;
        const double before = rig.mHost->gameSeconds();
        rig.tick(10, 0.5f);
        EXPECT_NEAR(rig.mHost->gameSeconds() - before, 5.0 * PapyrusHostOptions().mTimeScale, 1e-6);
    }
}
