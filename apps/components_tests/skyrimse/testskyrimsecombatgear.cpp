// Task 0214 (real weapons and shields in combat): the pure selection rules, the clip table of every weapon class, the
// shield's block bonus, the gear lookup over real records and SkyrimCombat with `combat real gear` on and off, tested
// through the contract with a fake data source and a recording writer. Written from the spec
// (docs/tasks/0214-skyrim-real-weapons.md) before the code. Numbers are worked out by hand in comments with the default
// GMSTs: stamina = 20 + weight (x 2 for a power attack); damage = WEAP damage x (1 + skill / 200), after armour x
// (1 - mitigation / 100); reach = WEAP reach x 141.
#include <game_skyrimse/combat.hpp>
#include <game_skyrimse/combat_gear.hpp>

#include <gtest/gtest.h>

#include <algorithm>
#include <map>
#include <string>
#include <vector>

namespace
{
    using namespace game_skyrimse;
    using namespace openeden::sdk::game;

    WeaponRecord weapon(FormId id, std::uint8_t type, std::uint16_t damage, float weight = 10.f, float reach = 1.f)
    {
        WeaponRecord w;
        w.mCommon.mId = id;
        w.mCommon.mEditorId = "W" + std::to_string(id);
        w.mHasData = true;
        w.mHasDnam = true;
        w.mAnimType = type;
        w.mDamage = damage;
        w.mWeight = weight;
        w.mReach = reach;
        w.mSkillValue = kFirstSkillActorValue + (type >= 5 ? 1u : 0u); // One-handed or Two-handed
        return w;
    }

    ArmourPiece piece(FormId id, std::uint32_t slots, float rating, std::uint32_t type = 1)
    {
        ArmourPiece p;
        p.mForm = id;
        p.mSlotMask = slots;
        p.mRating = rating;
        p.mType = type;
        return p;
    }

    // ---- the pure rules ------------------------------------------------------------------------------------------

    TEST(SkyrimCombatGearRules, theClipClassFollowsTheAnimationType)
    {
        for (std::uint8_t type : { 1, 2, 3, 4 })
            EXPECT_EQ(meleeClipClass(type), kClipClassOneHanded) << int(type);
        EXPECT_EQ(meleeClipClass(5), kClipClassTwoHandedSword);
        EXPECT_EQ(meleeClipClass(6), kClipClassTwoHandedAxe);
        for (std::uint8_t type : { 0, 7, 8, 9, 200 })
            EXPECT_EQ(meleeClipClass(type), kClipClassOneHanded) << int(type);
        EXPECT_TRUE(isTwoHanded(5));
        EXPECT_TRUE(isTwoHanded(6));
        EXPECT_FALSE(isTwoHanded(1));
        EXPECT_FALSE(isTwoHanded(4));
    }

    TEST(SkyrimCombatGearRules, aFightingWeaponIsMeleeWithDataAndNotBareHands)
    {
        EXPECT_TRUE(isFightingWeapon(weapon(1, 1, 7)));
        EXPECT_TRUE(isFightingWeapon(weapon(1, 6, 7)));
        EXPECT_FALSE(isFightingWeapon(weapon(1, 0, 7))); // hand to hand
        EXPECT_FALSE(isFightingWeapon(weapon(1, 7, 7))); // bow
        EXPECT_FALSE(isFightingWeapon(weapon(1, 8, 7))); // staff
        EXPECT_FALSE(isFightingWeapon(weapon(1, 9, 7))); // crossbow
        WeaponRecord noData = weapon(1, 1, 7);
        noData.mHasData = false;
        EXPECT_FALSE(isFightingWeapon(noData));
        WeaponRecord noDnam = weapon(1, 1, 7);
        noDnam.mHasDnam = false;
        EXPECT_FALSE(isFightingWeapon(noDnam));
    }

    TEST(SkyrimCombatGearRules, theBestWeaponHasTheMostDamageThenTheLowestFormId)
    {
        const WeaponRecord a = weapon(30, 1, 8), b = weapon(20, 5, 20), c = weapon(10, 5, 20), d = weapon(5, 7, 99);
        EXPECT_EQ(chooseMeleeWeapon({ &a, &b, &c, &d }), &c); // the bow never qualifies; 20 and 20 tie: id 10
        EXPECT_EQ(chooseMeleeWeapon({ &d, &c, &b, &a }), &c); // the order does not matter
        EXPECT_EQ(chooseMeleeWeapon({ &a }), &a);
        EXPECT_EQ(chooseMeleeWeapon({ &d }), nullptr);
        EXPECT_EQ(chooseMeleeWeapon({}), nullptr);
        EXPECT_EQ(chooseMeleeWeapon({ nullptr, &a }), &a);
    }

    TEST(SkyrimCombatGearRules, theBestShieldHasTheMostRatingThenTheLowestFormId)
    {
        std::vector<ArmourCandidate> list(4);
        list[0].mPiece = piece(40, ArmourSlot::Shield, 20.f);
        list[1].mPiece = piece(30, ArmourSlot::Shield, 24.f);
        list[2].mPiece = piece(20, ArmourSlot::Shield, 24.f);
        list[3].mPiece = piece(10, ArmourSlot::Body, 50.f); // not a shield
        const ArmourCandidate* best = chooseShield(list);
        ASSERT_NE(best, nullptr);
        EXPECT_EQ(best->mPiece.mForm, 20u);
        std::reverse(list.begin(), list.end());
        EXPECT_EQ(chooseShield(list)->mPiece.mForm, 20u);
        EXPECT_EQ(chooseShield({}), nullptr);
        list.assign(1, ArmourCandidate{ piece(10, ArmourSlot::Body, 50.f), 0 });
        EXPECT_EQ(chooseShield(list), nullptr);
        EXPECT_TRUE(isShield(piece(1, ArmourSlot::Shield, 1.f)));
        EXPECT_FALSE(isShield(piece(1, ArmourSlot::Body, 1.f)));
    }

    TEST(SkyrimCombatGearRules, extraArmourFillsOnlyTheSlotsTheOutfitLeavesFree)
    {
        std::vector<ArmourCandidate> list(5);
        list[0].mPiece = piece(10, ArmourSlot::Body, 30.f);
        list[1].mPiece = piece(11, ArmourSlot::Body, 40.f); // beats 10 for the body
        list[2].mPiece = piece(12, ArmourSlot::Head, 10.f);
        list[3].mPiece = piece(13, ArmourSlot::Shield, 99.f); // a shield is not "extra armour"
        list[4].mPiece = piece(14, ArmourSlot::Feet, 8.f);
        const std::vector<ArmourPiece> worn = { piece(99, ArmourSlot::Head, 5.f) }; // the outfit has a helmet
        const std::vector<ArmourPiece> extra = chooseExtraArmour(list, worn);
        ASSERT_EQ(extra.size(), 2u);
        EXPECT_EQ(extra[0].mForm, 11u); // descending rating
        EXPECT_EQ(extra[1].mForm, 14u);
        EXPECT_TRUE(chooseExtraArmour({}, worn).empty());
    }

    TEST(SkyrimCombatGearRules, aShieldRaisesTheBlockedFractionBeforeTheCap)
    {
        const BlockSettings settings; // base 0.3, scaling 0.2, skill mult 2, max 0.7
        BlockInput in;
        in.mDamage = 10.f;
        in.mBlockerStamina = 100.f;
        const BlockResult plain = computeBlock(in, settings);
        EXPECT_NEAR(plain.mFraction, 0.3f, 1e-6f);
        in.mShieldBonus = 0.2f;
        EXPECT_NEAR(computeBlock(in, settings).mFraction, 0.5f, 1e-6f);
        in.mShieldBonus = 5.f; // capped at fBlockMax
        EXPECT_NEAR(computeBlock(in, settings).mFraction, 0.7f, 1e-6f);
        in.mShieldBonus = -1.f; // a negative bonus is no bonus
        EXPECT_NEAR(computeBlock(in, settings).mFraction, 0.3f, 1e-6f);
    }

    // ---- the clip table ------------------------------------------------------------------------------------------

    TEST(SkyrimCombatGearSlots, everyWeaponClassHasItsOwnClips)
    {
        const SlotTable t = SlotTable::gear();
        EXPECT_GT(t.size(), 20u);
        struct Row
        {
            ClipSlot slot;
            std::uint8_t cls;
            const char* clip;
        };
        const Row rows[] = {
            { ClipSlot::Attack, 1, "1HM_AttackRight" },
            { ClipSlot::PowerAttack, 1, "1HM_AttackPower" },
            { ClipSlot::BlockHit, 1, "1HM_BlockHit" },
            { ClipSlot::Bash, 1, "1HM_BlockBash" },
            { ClipSlot::Recoil, 1, "1HM_RecoilRight" },
            { ClipSlot::Stagger, 1, "1HM_StaggerBackMedium" },
            { ClipSlot::BlockStart, 1, "1HM_BlockIdle" },
            { ClipSlot::AttackLeft, 1, "1HM_AttackLeft" },
            { ClipSlot::Attack, 5, "2HM_AttackRight" },
            { ClipSlot::AttackLeft, 5, "2HM_AttackLeft" },
            { ClipSlot::PowerAttack, 5, "2HM_AttackPower" },
            { ClipSlot::BlockHit, 5, "2HM_BlockHit" },
            { ClipSlot::Bash, 5, "2HM_BlockBash" },
            { ClipSlot::Recoil, 5, "2HM_RecoilRight" },
            { ClipSlot::Stagger, 5, "2HM_StaggerBackMedium" },
            { ClipSlot::BlockStart, 5, "2HM_BlockIdle" },
            { ClipSlot::Attack, 6, "2HW_AttackRight" },
            { ClipSlot::AttackLeft, 6, "2HW_AttackLeft" },
            { ClipSlot::PowerAttack, 6, "2HW_AttackPower" },
            { ClipSlot::BlockHit, 6, "2HW_BlockHit" },
            { ClipSlot::Bash, 6, "2HW_BlockBash" },
            { ClipSlot::Recoil, 6, "2HW_RecoilRight" },
            { ClipSlot::Stagger, 6, "2HW_StaggerBackMedium" },
            { ClipSlot::BlockStart, 6, "2HW_BlockIdle" },
            { ClipSlot::BlockHit, 10, "SHD_BlockHit" },
            { ClipSlot::Bash, 10, "SHD_BlockBash" },
            { ClipSlot::BlockStart, 10, "SHD_BlockIdle" },
        };
        for (const Row& r : rows)
            EXPECT_EQ(t.clipFor(r.slot, r.cls), r.clip) << r.clip;
        // The shield column has no attack of its own; unknown classes have nothing.
        EXPECT_TRUE(t.clipFor(ClipSlot::Attack, 10).empty());
        EXPECT_TRUE(t.clipFor(ClipSlot::Attack, 7).empty());
        // The one-handed column matches the old humanoid table.
        const SlotTable old = SlotTable::humanoid();
        for (ClipSlot s : { ClipSlot::Attack, ClipSlot::AttackLeft, ClipSlot::PowerAttack, ClipSlot::BlockHit,
                 ClipSlot::Bash, ClipSlot::Recoil, ClipSlot::Stagger })
            EXPECT_EQ(t.clipFor(s, 1), old.clipFor(s, 1));
        EXPECT_EQ(SlotTable::humanoid().size(), 7u); // untouched
    }

    // ---- the gear of an NPC from records -------------------------------------------------------------------------

    struct GearRecords : ::testing::Test
    {
        ActorIndex mActors;
        CombatIndex mCombat;
        static constexpr FormId kNpc = 0x100;

        void build(const std::vector<WeaponRecord>& weapons, const std::vector<ArmorExtra>& armours,
            const std::vector<InventoryEntry>& inventory, const std::vector<FormId>& outfitItems = {},
            const std::vector<LeveledList>& lists = {})
        {
            ActorRecords r;
            ActorNpc n;
            n.mCommon.mId = kNpc;
            n.mRace = 0x200;
            n.mInventory = inventory;
            n.mDefaultOutfit = outfitItems.empty() ? kNullFormId : 0x400;
            r.mNpcs.push_back(n);
            for (const ArmorExtra& a : armours)
                r.mArmors.push_back(a);
            if (!outfitItems.empty())
            {
                OutfitRecord o;
                o.mCommon.mId = 0x400;
                o.mItems = outfitItems;
                r.mOutfits.push_back(o);
            }
            mActors.add(r);
            if (!lists.empty())
                mActors.addLeveledLists(lists);
            CombatRecords c;
            c.mWeapons = weapons;
            mCombat.add(c);
        }
        static ArmorExtra armour(FormId id, std::uint32_t slots, std::uint32_t rating100, FormId bamt = 0)
        {
            ArmorExtra a;
            a.mCommon.mId = id;
            a.mSlotMask = slots;
            a.mRating = rating100;
            a.mArmorType = 1;
            a.mBlockMaterial = bamt;
            return a;
        }
    };

    TEST_F(GearRecords, theBestWeaponAndShieldOfTheInventoryAreChosen)
    {
        build({ weapon(0x10, 1, 8), weapon(0x11, 3, 9), weapon(0x12, 7, 50) },
            { armour(0x20, ArmourSlot::Shield, 2000, 0x77), armour(0x21, ArmourSlot::Shield, 3000, 0x78) },
            { { 0x10, 1 }, { 0x11, 1 }, { 0x12, 1 }, { 0x20, 1 }, { 0x21, 1 } });
        FighterGear gear;
        ASSERT_TRUE(resolveFighterGear(mActors, mCombat, kNpc, 0, 10, {}, gear));
        EXPECT_TRUE(gear.mKnown);
        ASSERT_NE(gear.mWeapon, nullptr);
        EXPECT_EQ(gear.mWeapon->mCommon.mId, 0x11u); // the axe: 9 beats 8, the bow is not melee
        EXPECT_TRUE(gear.mHasShield);
        EXPECT_EQ(gear.mShield.mForm, 0x21u);
        EXPECT_FLOAT_EQ(gear.mShield.mRating, 30.f);
        EXPECT_EQ(gear.mShieldBlockMaterial, 0x78u);
    }

    TEST_F(GearRecords, aTwoHandedWeaponLeavesNoShield)
    {
        build({ weapon(0x10, 1, 8), weapon(0x11, 5, 20) }, { armour(0x20, ArmourSlot::Shield, 2000) },
            { { 0x10, 1 }, { 0x11, 1 }, { 0x20, 1 } });
        FighterGear gear;
        ASSERT_TRUE(resolveFighterGear(mActors, mCombat, kNpc, 0, 10, {}, gear));
        ASSERT_NE(gear.mWeapon, nullptr);
        EXPECT_EQ(gear.mWeapon->mCommon.mId, 0x11u);
        EXPECT_FALSE(gear.mHasShield);
    }

    TEST_F(GearRecords, aShieldAloneIsKeptAndAnNpcWithNothingHasNoGear)
    {
        build({}, { armour(0x20, ArmourSlot::Shield, 2000) }, { { 0x20, 1 } });
        FighterGear gear;
        ASSERT_TRUE(resolveFighterGear(mActors, mCombat, kNpc, 0, 10, {}, gear));
        EXPECT_EQ(gear.mWeapon, nullptr);
        EXPECT_TRUE(gear.mHasShield);
        FighterGear none;
        EXPECT_FALSE(resolveFighterGear(mActors, mCombat, 0x999, 0, 10, {}, none)); // not an NPC
        EXPECT_FALSE(none.mKnown);
    }

    TEST_F(GearRecords, leveledListsAreSearchedAndNegativeCountsIgnored)
    {
        LeveledList list;
        list.mCommon.mId = 0x50;
        list.mEntries.push_back({ 1, 0x11, 1 });
        build({ weapon(0x11, 6, 30), weapon(0x10, 1, 8) }, {}, { { 0x50, 1 }, { 0x10, -3 } }, {}, { list });
        FighterGear gear;
        ASSERT_TRUE(resolveFighterGear(mActors, mCombat, kNpc, 0, 10, {}, gear));
        ASSERT_NE(gear.mWeapon, nullptr);
        EXPECT_EQ(gear.mWeapon->mCommon.mId, 0x11u); // through the list; the -3 count of 0x10 does not count
    }

    TEST_F(GearRecords, aListThatGivesAllItsEntriesGivesTheSwordAndTheShield)
    {
        LeveledList list;
        list.mCommon.mId = 0x50;
        list.mFlags = LeveledFlags::UseAll;
        list.mChanceNone = 100; // never drawn: a guard's gear is the same in every session
        list.mEntries.push_back({ 1, 0x10, 1 });
        list.mEntries.push_back({ 1, 0x20, 1 });
        list.mEntries.push_back({ 99, 0x11, 1 }); // above the level: not given
        build({ weapon(0x10, 1, 8), weapon(0x11, 5, 30) }, { armour(0x20, ArmourSlot::Shield, 2000) }, { { 0x50, 1 } },
            {}, { list });
        FighterGear gear;
        ASSERT_TRUE(resolveFighterGear(mActors, mCombat, kNpc, 0, 10, {}, gear));
        ASSERT_NE(gear.mWeapon, nullptr);
        EXPECT_EQ(gear.mWeapon->mCommon.mId, 0x10u);
        EXPECT_TRUE(gear.mHasShield);
    }

    TEST_F(GearRecords, inventoryArmourFillsOnlySlotsTheOutfitLeavesFree)
    {
        build({}, { armour(0x20, ArmourSlot::Body, 5000), armour(0x21, ArmourSlot::Head, 1000) },
            { { 0x20, 1 }, { 0x21, 1 } });
        FighterGear gear;
        const std::vector<ArmourPiece> worn = { piece(0x99, ArmourSlot::Body, 10.f) };
        ASSERT_TRUE(resolveFighterGear(mActors, mCombat, kNpc, 0, 10, worn, gear));
        ASSERT_EQ(gear.mExtraPieces.size(), 1u);
        EXPECT_EQ(gear.mExtraPieces[0].mForm, 0x21u); // the body is taken by the outfit
    }

    // ---- SkyrimCombat --------------------------------------------------------------------------------------------

    constexpr WorldId kAttackerBase = 100;
    constexpr WorldId kVictimBase = 200;

    class FakeData final : public CombatData
    {
    public:
        FakeData()
        {
            mSword = weapon(1, WeaponAnimType::OneHandSword, 7, 9.f, 1.f);
            mSword.mCommon.mEditorId = "IronSword";
            mGreatsword = weapon(2, WeaponAnimType::TwoHandSword, 20, 20.f, 1.3f);
            mBattleaxe = weapon(3, WeaponAnimType::TwoHandAxe, 22, 24.f, 1.4f);
            mDagger = weapon(4, WeaponAnimType::OneHandDagger, 4, 2.f, 0.7f);
            for (WorldId base : { kAttackerBase, kVictimBase })
            {
                FighterProfile p;
                p.mHealthMax = base == kAttackerBase ? 100.f : 50.f;
                p.mStaminaMax = 100.f;
                mProfiles[base] = p; // every skill 0: the multiplier is 1
            }
            mPlayer.mHealthMax = 100.f;
            mPlayer.mStaminaMax = 100.f;
            mPlayer.mSkills.fill(0);
        }
        bool fighter(WorldId base, FighterProfile& out) override
        {
            const auto it = mProfiles.find(base);
            if (it == mProfiles.end())
                return false;
            out = it->second;
            return true;
        }
        FighterProfile playerFighter() override { return mPlayer; }
        const WeaponRecord* weaponByEditorId(std::string_view id) override
        {
            return id == "ironsword" ? &mSword : nullptr;
        }
        CombatSettings settings() override { return mSettings; }
        bool fighterGear(WorldId base, const std::vector<ArmourPiece>&, FighterGear& out) override
        {
            ++mGearAsks;
            const auto it = mGear.find(base);
            if (it == mGear.end())
                return false;
            out = it->second;
            out.mKnown = true;
            return true;
        }

        WeaponRecord mSword, mGreatsword, mBattleaxe, mDagger;
        std::map<WorldId, FighterProfile> mProfiles;
        std::map<WorldId, FighterGear> mGear;
        FighterProfile mPlayer;
        CombatSettings mSettings;
        int mGearAsks = 0;
    };

    class FakeWriter final : public CombatRequestWriter
    {
    public:
        struct Clip
        {
            CombatSubject mSubject;
            ClipSlot mSlot;
            std::string mName;
            std::uint32_t mToken;
        };
        bool playClip(CombatSubject subject, const ClipRequest& request) override
        {
            mClips.push_back({ subject, request.slot, std::string(request.clip), request.token });
            return true;
        }
        bool stopClip(CombatSubject) override { return true; }
        bool requestSweep(CombatSubject subject, const SweepRequest& request) override
        {
            mSweeps.push_back({ subject, request });
            return true;
        }
        bool reportDamage(const DamageReport& report) override
        {
            mDamage.push_back(report);
            return true;
        }
        bool reportDeath(ActorHandle) override { return true; }
        bool faceHeading(CombatSubject, float) override { return true; }
        bool clipTiming(std::string_view clip, bool, ClipTiming& out) override
        {
            mTimingClips.emplace_back(clip);
            out = { true, 0.367f, 1.287f };
            return true;
        }
        bool subjectState(CombatSubject subject, SubjectState& out) override
        {
            out = {};
            if (subject.player)
            {
                out = mPlayerState;
                return true;
            }
            const auto it = mStates.find(subject.actor.reference);
            if (it == mStates.end())
                return false;
            out = it->second;
            return true;
        }
        std::vector<Clip> mClips;
        std::vector<std::pair<CombatSubject, SweepRequest>> mSweeps;
        std::vector<DamageReport> mDamage;
        std::map<WorldId, SubjectState> mStates;
        SubjectState mPlayerState;
        std::vector<std::string> mTimingClips;
    };

    ActorInfo info(WorldId reference, WorldId base, std::string_view editorId)
    {
        ActorInfo i;
        i.handle = { reference, 1 };
        i.base = base;
        i.editorId = editorId;
        return i;
    }

    ClipEvent hitEvent(CombatSubject subject, std::uint32_t token, ClipSlot slot = ClipSlot::Attack)
    {
        ClipEvent e;
        e.subject = subject;
        e.slot = slot;
        e.token = token;
        e.kind = ClipEventKind::Hit;
        return e;
    }

    struct CombatGear : ::testing::Test
    {
        FakeData mData;
        SkyrimCombat mCombat;
        FakeWriter mWriter;
        ActorHandle mA{ 10, 1 };
        ActorHandle mB{ 20, 1 };
        std::vector<std::string> mTrace;

        void SetUp() override
        {
            mCombat.setData(&mData);
            mCombat.setTrace([this](const std::string& line) { mTrace.push_back(line); });
            mCombat.onActorAdded(info(10, kAttackerBase, "Attacker"));
            mCombat.onActorAdded(info(20, kVictimBase, "Victim"));
            mWriter.mStates[10] = { 100.0, 200.0, 5.0, 0.25f, 0.f, 0.f };
            mWriter.mStates[20] = { 100.0, 260.0, 5.0, 0.f, 0.f, 0.f };
            mWriter.mPlayerState = { 0.0, 0.0, 0.0, 1.f, 90.f, 100.f };
        }
        FighterGear gearOf(const WeaponRecord* w, bool shield = false, float rating = 0.f, FormId material = 0)
        {
            FighterGear g;
            g.mWeapon = w;
            g.mHasShield = shield;
            if (shield)
            {
                g.mShield = piece(0x50, ArmourSlot::Shield, rating);
                g.mShieldBlockMaterial = material;
            }
            return g;
        }
        void swing(CombatSubject attacker, std::uint32_t token, ClipSlot slot = ClipSlot::Attack)
        {
            mCombat.onClipEvent(hitEvent(attacker, token, slot));
            mCombat.combatUpdate(0.016f, mWriter);
            SweepHit hit;
            hit.target = SweepTarget::Actor;
            hit.actor = mB;
            hit.distance = 20.f;
            mCombat.onSweepResult(attacker, token, &hit, 1);
            mCombat.combatUpdate(0.016f, mWriter);
        }
        SkyrimCombat::FighterView view(ActorHandle who)
        {
            SkyrimCombat::FighterView v;
            EXPECT_TRUE(mCombat.fighterView(CombatSubject::of(who), v));
            return v;
        }
    };

    TEST_F(CombatGear, withTheSettingOffEveryoneKeepsTheTestWeaponAndTheOneHandedClips)
    {
        mData.mGear[kAttackerBase] = gearOf(&mData.mGreatsword);
        mData.mGear[kVictimBase] = gearOf(&mData.mBattleaxe, true, 20.f);
        mCombat.setTestWeapon("IronSword");
        swing(CombatSubject::of(mA), 5);
        ASSERT_EQ(mWriter.mDamage.size(), 1u);
        EXPECT_NEAR(mWriter.mDamage[0].amount, 7.f, 1e-4f); // the sword, not the greatsword's 20
        ASSERT_EQ(mWriter.mClips.size(), 1u);
        EXPECT_EQ(mWriter.mClips[0].mName, "1HM_RecoilRight");
        EXPECT_EQ(mData.mGearAsks, 0); // the gear is never even looked up
        EXPECT_FALSE(view(mB).mHasShield);
        EXPECT_EQ(view(mA).mWeaponClass, 0);
    }

    TEST_F(CombatGear, aGreatswordHitsWithItsOwnDamageReachAndStamina)
    {
        mCombat.setRealGear(true);
        mData.mGear[kAttackerBase] = gearOf(&mData.mGreatsword);
        mCombat.onClipEvent(hitEvent(CombatSubject::of(mA), 5));
        mCombat.combatUpdate(0.016f, mWriter);
        ASSERT_EQ(mWriter.mSweeps.size(), 1u);
        EXPECT_NEAR(mWriter.mSweeps[0].second.reach, 1.3f * 141.f, 1e-3f);
        EXPECT_NEAR(view(mA).mStamina, 100.f - (20.f + 20.f), 1e-3f); // 20 + weight 20
        EXPECT_EQ(view(mA).mWeaponClass, kClipClassTwoHandedSword);
        SweepHit hit;
        hit.target = SweepTarget::Actor;
        hit.actor = mB;
        mCombat.onSweepResult(CombatSubject::of(mA), 5, &hit, 1);
        mCombat.combatUpdate(0.016f, mWriter);
        ASSERT_EQ(mWriter.mDamage.size(), 1u);
        EXPECT_NEAR(mWriter.mDamage[0].amount, 20.f, 1e-4f); // skill 0: x 1.0, no armour
    }

    TEST_F(CombatGear, theTestWeaponOverridesAnNpcsOwnWeapon)
    {
        mCombat.setRealGear(true);
        mCombat.setTestWeapon("IronSword");
        mData.mGear[kAttackerBase] = gearOf(&mData.mGreatsword);
        swing(CombatSubject::of(mA), 5);
        ASSERT_EQ(mWriter.mDamage.size(), 1u);
        EXPECT_NEAR(mWriter.mDamage[0].amount, 7.f, 1e-4f);
        EXPECT_EQ(mWriter.mClips.back().mName, "1HM_RecoilRight");
    }

    TEST_F(CombatGear, theVictimReactsInTheClipsOfItsOwnWeaponClass)
    {
        mCombat.setRealGear(true);
        mData.mGear[kAttackerBase] = gearOf(&mData.mSword);
        mData.mGear[kVictimBase] = gearOf(&mData.mBattleaxe);
        swing(CombatSubject::of(mA), 5);
        ASSERT_EQ(mWriter.mClips.size(), 1u);
        EXPECT_EQ(mWriter.mClips[0].mName, "2HW_RecoilRight");
    }

    TEST_F(CombatGear, aHostileActorSwingsTheAttackClipOfItsWeapon)
    {
        mCombat.setRealGear(true);
        ASSERT_TRUE(mCombat.setTestHostile("attacker"));
        mData.mGear[kAttackerBase] = gearOf(&mData.mGreatsword);
        mCombat.onActorAdded(info(10, kAttackerBase, "Attacker"));
        mWriter.mStates[10] = { 100.0, 100.0, 5.0, 0.f, 0.f, 0.f };
        mCombat.combatUpdate(0.1f, mWriter);
        ASSERT_EQ(mWriter.mClips.size(), 1u);
        EXPECT_EQ(mWriter.mClips[0].mSlot, ClipSlot::Attack);
        EXPECT_EQ(mWriter.mClips[0].mName, "2HM_AttackRight");
    }

    TEST_F(CombatGear, anAxePlaysTheAxeClipsAndADaggerTheOneHandedOnesWithItsShortReach)
    {
        mCombat.setRealGear(true);
        ASSERT_TRUE(mCombat.setTestHostile("attacker"));
        mData.mGear[kAttackerBase] = gearOf(&mData.mBattleaxe);
        mCombat.onActorAdded(info(10, kAttackerBase, "Attacker"));
        mWriter.mStates[10] = { 100.0, 100.0, 5.0, 0.f, 0.f, 0.f };
        mCombat.combatUpdate(0.1f, mWriter);
        ASSERT_EQ(mWriter.mClips.size(), 1u);
        EXPECT_EQ(mWriter.mClips[0].mName, "2HW_AttackRight");

        // A dagger (animation type 2): the one-handed clips, reach 0.7 x 141 = 98.7 (+ a body radius less 4).
        FakeData data2;
        SkyrimCombat combat2;
        FakeWriter writer2;
        combat2.setData(&data2);
        combat2.setRealGear(true);
        combat2.setTestHostile("attacker");
        data2.mGear[kAttackerBase] = gearOf(&data2.mDagger);
        combat2.onActorAdded(info(10, kAttackerBase, "Attacker"));
        writer2.mPlayerState = { 0.0, 0.0, 0.0, 1.f, 90.f, 100.f };
        writer2.mStates[10] = { 100.0, 100.0, 5.0, 0.f, 0.f, 0.f }; // 141 away: out of the dagger's reach (116)
        combat2.combatUpdate(0.1f, writer2);
        EXPECT_TRUE(writer2.mClips.empty());
        writer2.mStates[10] = { 60.0, 60.0, 5.0, 0.f, 0.f, 0.f }; // 85 away: in reach
        combat2.combatUpdate(0.1f, writer2);
        ASSERT_EQ(writer2.mClips.size(), 1u);
        EXPECT_EQ(writer2.mClips[0].mName, "1HM_AttackRight");
    }

    TEST_F(CombatGear, aShieldBlocksWithItsOwnClipAndABonus)
    {
        mCombat.setRealGear(true);
        ASSERT_TRUE(mCombat.setTestBlock("victim"));
        mData.mGear[kAttackerBase] = gearOf(&mData.mSword);
        mData.mGear[kVictimBase] = gearOf(&mData.mSword, true, 10.f, 0x77);
        mCombat.onActorAdded(info(20, kVictimBase, "Victim"));
        swing(CombatSubject::of(mA), 5);
        ASSERT_EQ(mWriter.mDamage.size(), 1u);
        EXPECT_TRUE(view(mB).mHasShield);
        // The sword hits for 7; the shield's rating 10 takes mitigation percent off; the block takes
        // 0.3 + 10 x 0.01 = 0.4 of what is left (skill 0).
        const std::array<std::uint8_t, kActorSkillCount> noSkills{};
        const float mitigation
            = armourOf({ piece(0x50, ArmourSlot::Shield, 10.f) }, noSkills, CombatSettings{}).mMitigationPercent;
        const float afterArmour = 7.f * (1.f - mitigation / 100.f);
        EXPECT_NEAR(mWriter.mDamage[0].amount, afterArmour * (1.f - 0.4f), 1e-3f);
        ASSERT_EQ(mWriter.mClips.size(), 1u);
        EXPECT_EQ(mWriter.mClips[0].mSlot, ClipSlot::BlockHit);
        EXPECT_EQ(mWriter.mClips[0].mName, "SHD_BlockHit");
    }

    TEST_F(CombatGear, withoutAShieldABlockerKeepsTheWeaponsBlockClip)
    {
        mCombat.setRealGear(true);
        ASSERT_TRUE(mCombat.setTestBlock("victim"));
        mData.mGear[kAttackerBase] = gearOf(&mData.mSword);
        mData.mGear[kVictimBase] = gearOf(&mData.mGreatsword);
        mCombat.onActorAdded(info(20, kVictimBase, "Victim"));
        swing(CombatSubject::of(mA), 5);
        ASSERT_EQ(mWriter.mClips.size(), 1u);
        EXPECT_EQ(mWriter.mClips[0].mName, "2HM_BlockHit");
        EXPECT_NEAR(mWriter.mDamage[0].amount, 7.f * (1.f - 0.3f), 1e-4f); // no bonus
    }

    TEST_F(CombatGear, aShieldBashUsesTheShieldClip)
    {
        mCombat.setRealGear(true);
        ASSERT_TRUE(mCombat.setTestHostile("attacker"));
        mCombat.setTestBashEvery(1);
        mData.mGear[kAttackerBase] = gearOf(&mData.mSword, true, 10.f);
        mCombat.onActorAdded(info(10, kAttackerBase, "Attacker"));
        mWriter.mStates[10] = { 100.0, 100.0, 5.0, 0.f, 0.f, 0.f };
        mCombat.combatUpdate(0.1f, mWriter);
        ASSERT_EQ(mWriter.mClips.size(), 1u);
        EXPECT_EQ(mWriter.mClips[0].mSlot, ClipSlot::Bash);
        EXPECT_EQ(mWriter.mClips[0].mName, "SHD_BlockBash");
    }

    TEST_F(CombatGear, inventoryArmourAddsToTheNpcsMitigation)
    {
        mCombat.setRealGear(true);
        mData.mGear[kAttackerBase] = gearOf(&mData.mSword);
        FighterGear victim = gearOf(&mData.mSword);
        victim.mExtraPieces.push_back(piece(0x60, ArmourSlot::Body, 40.f));
        mData.mGear[kVictimBase] = victim;
        swing(CombatSubject::of(mA), 5);
        ASSERT_EQ(mWriter.mDamage.size(), 1u);
        const std::array<std::uint8_t, kActorSkillCount> noSkills{};
        const float mitigation
            = armourOf({ piece(0x60, ArmourSlot::Body, 40.f) }, noSkills, CombatSettings{}).mMitigationPercent;
        EXPECT_GT(mitigation, 0.f);
        EXPECT_NEAR(mWriter.mDamage[0].amount, 7.f * (1.f - mitigation / 100.f), 1e-3f);
    }

    TEST_F(CombatGear, thePlayersEquippedTwoHandedWeaponAsksForItsOwnSwingTiming)
    {
        mCombat.setRealGear(true);
        SkyrimCombat::PlayerEquipment eq;
        eq.mWeapon = &mData.mGreatsword;
        mCombat.setPlayerEquipment(eq);
        mCombat.onPlayerAttack(false);
        mCombat.combatUpdate(0.1f, mWriter);
        ASSERT_FALSE(mWriter.mTimingClips.empty());
        EXPECT_EQ(mWriter.mTimingClips.front(), "2HM_AttackRight");
        EXPECT_EQ(
            std::count(mWriter.mTimingClips.begin(), mWriter.mTimingClips.end(), std::string("1HM_AttackRight")), 0);
    }

    TEST_F(CombatGear, thePlayerKeepsTheOneHandedTimingWhileTheSettingIsOff)
    {
        SkyrimCombat::PlayerEquipment eq;
        eq.mWeapon = &mData.mGreatsword;
        mCombat.setPlayerEquipment(eq);
        mCombat.onPlayerAttack(false);
        mCombat.combatUpdate(0.1f, mWriter);
        ASSERT_FALSE(mWriter.mTimingClips.empty());
        for (const std::string& clip : mWriter.mTimingClips)
            EXPECT_EQ(clip.substr(0, 4), "1HM_") << clip;
    }

    TEST_F(CombatGear, thePlayersEquippedShieldCountsInItsArmourAndIsDroppedWithATwoHandedWeapon)
    {
        mCombat.setRealGear(true);
        SkyrimCombat::PlayerEquipment eq;
        eq.mWeapon = &mData.mSword;
        eq.mPieces = { piece(0x50, ArmourSlot::Shield, 20.f) };
        mCombat.setPlayerEquipment(eq);
        mCombat.onPlayerAttack(false);
        mCombat.combatUpdate(0.1f, mWriter);
        EXPECT_GT(mCombat.playerMitigationPercent(), 0.f);
        SkyrimCombat::FighterView v;
        ASSERT_TRUE(mCombat.fighterView(CombatSubject::ofPlayer(), v));
        EXPECT_TRUE(v.mHasShield);
        EXPECT_EQ(v.mWeaponClass, kClipClassOneHanded);
        eq.mWeapon = &mData.mGreatsword;
        mCombat.setPlayerEquipment(eq);
        mCombat.combatUpdate(0.1f, mWriter);
        ASSERT_TRUE(mCombat.fighterView(CombatSubject::ofPlayer(), v));
        EXPECT_FALSE(v.mHasShield);
        EXPECT_EQ(v.mWeaponClass, kClipClassTwoHandedSword);
    }
}
