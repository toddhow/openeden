// Task 0295 (more weapons and magic): enchanted weapons that apply their effects on a landed blow, staves that cast
// their enchantment, and bound weapons. The ENCH decoder and the WEAP EITM subrecord are tested on generated records;
// the combat rules through the combat contract with a fake data source and a recording writer (fixtures after
// testskyrimsepoison.cpp and testskyrimsecombatmagic.cpp). Numbers by hand: the stand-in player's skills are 15, so a
// sword of damage 7 hits for 7 x (1 + 15/200) = 7.525 and a bound sword of damage 20 for 21.5; the victim has 50 health
// and no armour; the enchantment of the test sword is an instant 8 points on health, so a hit that lands leaves
// 50 - 7.525 - 8 = 34.475.
#include <game_skyrimse/combat.hpp>
#include <game_skyrimse/combat_gear.hpp>
#include <game_skyrimse/scroll.hpp>

#include <gtest/gtest.h>

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <filesystem>
#include <iostream>
#include <map>
#include <sstream>
#include <string>
#include <vector>

#include "../contentsource/pluginbuilder.hpp"

namespace
{
    using namespace ContentTests;
    using namespace game_skyrimse;
    using namespace openeden::sdk::game;

    // ---- the decoders ----------------------------------------------------------------------------------------------

    LoadOrder oneMaster()
    {
        LoadOrder order;
        std::string err;
        EXPECT_TRUE(order.add("Skyrim.esm", false, err));
        EXPECT_TRUE(order.add("test.esp", false, err));
        return order;
    }

    struct DecodeFixture
    {
        LoadOrder mOrder = oneMaster();
        std::unique_ptr<Content::Plugin> mPlugin = [] {
            Content::Error error;
            return Content::Plugin::open(
                std::make_unique<std::istringstream>(
                    tes4Header(1.7f, 0, "A", "B", 1, { { "Skyrim.esm", 0 } }, 24, 44).str(), std::ios::binary),
                "test.esp", error);
        }();
        ExteriorLimits mLimits;
        DecodeNotes mNotes;
        FormIdResolver mResolver{ mPlugin->header(), "test.esp", mOrder };
        DecodeContext context() { return DecodeContext{ mResolver, mLimits, mNotes, false }; }
    };

    std::span<const std::byte> view(const Bytes& b)
    {
        return { reinterpret_cast<const std::byte*>(b.str().data()), b.str().size() };
    }

    Content::RawRecord raw(const char (&type)[5], std::uint32_t id)
    {
        Content::RawRecord r;
        r.mType = Content::fourCC(type);
        r.mFormId = id;
        r.mOffset = 0x1234;
        return r;
    }

    // An ENCH as the five masters have it: ENIT of 36 bytes, then the effects.
    Bytes enchBytes(std::uint32_t castType, std::uint32_t delivery, std::uint32_t type, std::size_t enitBytes = 36)
    {
        Bytes e;
        e.append(sub4("EDID", Bytes().zstring("EnchFireDamage")));
        Bytes enit;
        enit.u32(0).u32(0).u32(castType).u32(40).u32(delivery).u32(type).f32(0.f).u32(0).u32(0);
        std::string text = enit.str();
        text.resize(enitBytes);
        e.append(sub4("ENIT", Bytes().text(text)));
        e.append(sub4("EFID", Bytes().u32(0x00000555)));
        e.append(sub4("EFIT", Bytes().f32(8.f).u32(0).u32(3)));
        return e;
    }

    TEST(SkyrimEnchantDecode, anEnchantmentBecomesACastableSpellWithItsCastTypeDeliveryAndNoCost)
    {
        DecodeFixture f;
        std::string why;
        SpellRecord s;
        ASSERT_TRUE(decodeEnchantment(view(enchBytes(1, 1, 6)), raw("ENCH", 0x01000C00), f.context(), s, why)) << why;
        EXPECT_TRUE(s.mEnchantment);
        EXPECT_TRUE(s.mHasData);
        EXPECT_EQ(s.mCost, 0u);
        EXPECT_EQ(s.mCastType, MagicCast::FireAndForget);
        EXPECT_EQ(s.mDelivery, MagicDelivery::Touch);
        EXPECT_EQ(s.mEnchantType, 6u);
        EXPECT_EQ(s.mCommon.mEditorId, "EnchFireDamage");
        ASSERT_EQ(s.mEffects.size(), 1u);
        EXPECT_FLOAT_EQ(s.mEffects[0].mMagnitude, 8.f);
        EXPECT_EQ(s.mEffects[0].mDuration, 3u);
    }

    TEST(SkyrimEnchantDecode, aStaffEnchantmentCarriesTypeTwelveAndAimedDelivery)
    {
        DecodeFixture f;
        std::string why;
        SpellRecord s;
        ASSERT_TRUE(decodeEnchantment(view(enchBytes(1, 2, 12)), raw("ENCH", 0x01000C01), f.context(), s, why)) << why;
        EXPECT_EQ(s.mEnchantType, 12u);
        EXPECT_EQ(s.mDelivery, MagicDelivery::Aimed);
    }

    TEST(SkyrimEnchantDecode, aShortEnitIsCountedAndTheRecordHasNoData)
    {
        DecodeFixture f;
        std::string why;
        SpellRecord s;
        ASSERT_TRUE(decodeEnchantment(view(enchBytes(1, 1, 6, 20)), raw("ENCH", 0x01000C02), f.context(), s, why)) << why;
        EXPECT_FALSE(s.mHasData);
        EXPECT_GE(f.mNotes.mOddSubrecords, 1u);
    }

    TEST(SkyrimEnchantDecode, aWeaponRecordKeepsItsEnchantment)
    {
        DecodeFixture f;
        Bytes weap;
        weap.append(sub4("EDID", Bytes().zstring("EnchantedSword")));
        weap.append(sub4("DATA", Bytes().u32(100).f32(9.f).u16(7)));
        weap.append(sub4("EITM", Bytes().u32(0x00000777)));
        std::string why;
        WeaponRecord w;
        ASSERT_TRUE(decodeWeapon(view(weap), raw("WEAP", 0x01000D00), f.context(), w, why)) << why;
        EXPECT_NE(w.mEnchantment, kNullFormId);
        Bytes plain;
        plain.append(sub4("EDID", Bytes().zstring("PlainSword")));
        WeaponRecord p;
        ASSERT_TRUE(decodeWeapon(view(plain), raw("WEAP", 0x01000D01), f.context(), p, why)) << why;
        EXPECT_EQ(p.mEnchantment, kNullFormId);
    }

    // ---- the five masters (gated: OPENEDEN_SKYRIMSE_DATA names the Data folder; prints SKYRIMSE-WEAPONMAGIC lines) -----

    TEST(SkyrimWeaponMagicReal, theEnchantmentsStavesAndBoundWeaponsOfTheFiveMasters)
    {
        namespace fs = std::filesystem;
        const char* env = std::getenv("OPENEDEN_SKYRIMSE_DATA");
        if (env == nullptr || *env == '\0' || !fs::is_directory(env))
            GTEST_SKIP() << "OPENEDEN_SKYRIMSE_DATA is not set";
        const fs::path data(env);
        const std::vector<std::string> names{ "Skyrim.esm", "Update.esm", "Dawnguard.esm", "HearthFires.esm",
            "Dragonborn.esm" };
        LoadOrder order;
        std::string err;
        for (const std::string& n : names)
            if (!fs::exists(data / n) || !order.add(n, false, err))
                GTEST_SKIP() << "the five official masters are not all in the Data folder";
        CombatIndex index;
        std::map<FormId, MagicEffectRecord> effects;
        std::uint64_t odd = 0;
        for (const std::string& n : names)
        {
            Content::Error error;
            const std::unique_ptr<Content::Plugin> file = Content::Plugin::openFile(data / n, error);
            ASSERT_NE(file, nullptr) << n;
            CombatRecords records;
            ASSERT_TRUE(parseCombatRecords(*file, order, ExteriorLimits(), records));
            odd += records.mNotes.mOddSubrecords;
            for (const MagicEffectRecord& m : records.mMagicEffects)
                effects[m.mCommon.mId] = m;
            index.add(records);
        }
        std::size_t weapons = 0, enchanted = 0, resolved = 0, touchMelee = 0, staves = 0, staffEnchanted = 0,
                    staffResolved = 0, staffAimed = 0, bound = 0, boundWeapons = 0;
        for (const FormId id : index.weaponIds())
        {
            const WeaponRecord* w = index.weapon(id);
            if (w == nullptr || !w->mHasData)
                continue;
            ++weapons;
            const SpellRecord* e = w->mEnchantment != kNullFormId ? index.spell(w->mEnchantment) : nullptr;
            if (w->mEnchantment != kNullFormId)
                ++enchanted;
            if (e != nullptr && e->mHasData && e->mEnchantment)
                ++resolved;
            if (w->isMelee() && e != nullptr && e->mCastType == MagicCast::FireAndForget
                && e->mDelivery == MagicDelivery::Touch)
                ++touchMelee;
            if (w->isStaff())
            {
                ++staves;
                if (w->mEnchantment != kNullFormId)
                    ++staffEnchanted;
                if (e != nullptr && e->mHasData && !e->mEffects.empty())
                    ++staffResolved;
                if (e != nullptr && e->mCastType == MagicCast::FireAndForget && e->mDelivery == MagicDelivery::Aimed)
                    ++staffAimed;
            }
        }
        for (const auto& entry : effects)
        {
            const MagicEffectRecord* m = &entry.second;
            if (!m->mHasData || m->mArchetype != MagicArchetype::BoundWeapon)
                continue;
            ++bound;
            if (index.weapon(m->mAssociatedItem) != nullptr)
                ++boundWeapons;
        }
        std::cout << "SKYRIMSE-WEAPONMAGIC weapons " << weapons << " enchanted " << enchanted << " resolving to an ENCH "
                  << resolved << " melee fire-and-forget touch " << touchMelee << " staves " << staves
                  << " enchanted " << staffEnchanted << " resolved " << staffResolved << " aimed fire-and-forget "
                  << staffAimed << " bound weapon effects " << bound << " naming a WEAP " << boundWeapons
                  << " odd subrecords " << odd << "\n";
        EXPECT_GT(touchMelee, 100u);
        EXPECT_EQ(resolved, enchanted) << "every EITM names a decoded ENCH";
        EXPECT_EQ(staffResolved, staffEnchanted);
        EXPECT_GT(staffAimed, 10u);
        EXPECT_GE(bound, 5u);
        EXPECT_EQ(boundWeapons, bound) << "every Bound Weapon effect names a WEAP";
    }

    // ---- the pure gear rule ----------------------------------------------------------------------------------------

    WeaponRecord weaponOf(FormId id, std::uint8_t type, std::uint16_t damage, FormId enchantment = kNullFormId)
    {
        WeaponRecord w;
        w.mCommon.mId = id;
        w.mCommon.mEditorId = "W" + std::to_string(id);
        w.mHasData = true;
        w.mHasDnam = true;
        w.mAnimType = type;
        w.mDamage = damage;
        w.mWeight = 9.f;
        w.mReach = 1.f;
        w.mSkillValue = kFirstSkillActorValue;
        w.mEnchantment = enchantment;
        return w;
    }

    TEST(SkyrimStaffGear, theStaffChosenIsTheEnchantedOneWithTheLowestFormId)
    {
        const WeaponRecord a = weaponOf(30, WeaponAnimType::Staff, 8, 0x900);
        const WeaponRecord b = weaponOf(20, WeaponAnimType::Staff, 8, 0x901);
        const WeaponRecord plain = weaponOf(10, WeaponAnimType::Staff, 8); // no enchantment: nothing to cast
        const WeaponRecord sword = weaponOf(5, WeaponAnimType::OneHandSword, 7, 0x902);
        EXPECT_EQ(chooseStaff({ &a, &b, &plain, &sword }), &b);
        EXPECT_EQ(chooseStaff({ &b, &a }), &b); // the order of the list does not matter
        EXPECT_EQ(chooseStaff({ &plain, &sword }), nullptr);
        EXPECT_EQ(chooseStaff({}), nullptr);
        EXPECT_EQ(chooseStaff({ nullptr }), nullptr);
    }

    // ---- the combat rules ------------------------------------------------------------------------------------------

    constexpr WorldId kAttackerBase = 100;
    constexpr WorldId kVictimBase = 200;
    constexpr FormId kHurt = 0x2000, kBoundEffect = 0x2001, kFireEffect = 0x2002;
    constexpr FormId kTouchEnchant = 0x3000, kSelfEnchant = 0x3001, kStaffEnchant = 0x3002, kConstantEnchant = 0x3003;
    constexpr FormId kBoundSwordId = 0x4000, kBoundSpell = 0x4001, kProjectile = 0x5000;
    constexpr float kPi = 3.14159265358979f;

    class FakeData final : public CombatData
    {
    public:
        FakeData()
        {
            addEffect(kHurt, 0, 24, MagicFlag::Hostile | MagicFlag::Detrimental);
            addEffect(kBoundEffect, MagicArchetype::BoundWeapon, -1, 0);
            mEffects[kBoundEffect].mAssociatedItem = kBoundSwordId;
            addEffect(kFireEffect, 0, 24, MagicFlag::Hostile | MagicFlag::Detrimental);
            mEffects[kFireEffect].mProjectile = kProjectile;

            mProj.mCommon.mId = kProjectile;
            mProj.mHasData = true;
            mProj.mType = 1;
            mProj.mSpeed = 2500.f;
            mProj.mRange = 10000.f;
            mProj.mModel = "Magic\\FireBoltProjectile.nif";

            addSpell(kTouchEnchant, "EnchTouch", MagicCast::FireAndForget, MagicDelivery::Touch, 6, kHurt, 8.f, 0);
            addSpell(kSelfEnchant, "EnchSelf", MagicCast::FireAndForget, MagicDelivery::Self, 6, kHurt, 8.f, 0);
            addSpell(kConstantEnchant, "EnchConstant", MagicCast::Constant, MagicDelivery::Touch, 6, kHurt, 8.f, 0);
            addSpell(kStaffEnchant, "StaffFire", MagicCast::FireAndForget, MagicDelivery::Aimed, 12, kFireEffect, 20.f,
                0);
            addSpell(kBoundSpell, "BoundSwordSpell", MagicCast::FireAndForget, MagicDelivery::Self, 0, kBoundEffect,
                0.f, 60);
            mSpells[kBoundSpell].mCost = 10;
            mSpells[kBoundSpell].mEnchantment = false;
            mSpells[kBoundSpell].mEnchantType = 0;

            mSword = weaponOf(1, WeaponAnimType::OneHandSword, 7);
            mSword.mCommon.mEditorId = "IronSword";
            mBound = weaponOf(kBoundSwordId, WeaponAnimType::OneHandSword, 20);
            mBound.mCommon.mEditorId = "BoundSword";
            mStaff = weaponOf(3, WeaponAnimType::Staff, 8, kStaffEnchant);
            mPlainStaff = weaponOf(4, WeaponAnimType::Staff, 8);

            FighterProfile attacker;
            attacker.mHealthMax = 100.f;
            attacker.mStaminaMax = 100.f;
            attacker.mMagickaMax = 100.f;
            attacker.mSkills.fill(15);
            attacker.mUnarmedDamage = 4.f;
            attacker.mUnarmedReach = 96.f;
            mProfiles[kAttackerBase] = attacker;
            FighterProfile victim;
            victim.mHealthMax = 50.f;
            victim.mStaminaMax = 80.f;
            victim.mMagickaMax = 100.f;
            mProfiles[kVictimBase] = victim;
            mPlayer.mHealthMax = 100.f;
            mPlayer.mStaminaMax = 100.f;
            mPlayer.mMagickaMax = 100.f;
            mPlayer.mSkills.fill(15);
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
        const WeaponRecord* weaponByEditorId(std::string_view editorId) override
        {
            std::string lower(editorId);
            for (char& c : lower)
                c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
            return lower == "ironsword" ? &mSword : nullptr;
        }
        const WeaponRecord* weaponById(FormId id) override { return id == kBoundSwordId ? &mBound : nullptr; }
        CombatSettings settings() override { return CombatSettings(); }
        const SpellRecord* spellByEditorId(std::string_view editorId) override
        {
            for (const auto& entry : mSpells)
            {
                std::string lower = entry.second.mCommon.mEditorId;
                for (char& c : lower)
                    c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
                if (lower == editorId)
                    return &entry.second;
            }
            return nullptr;
        }
        const SpellRecord* spellById(FormId id) override
        {
            const auto it = mSpells.find(id);
            return it == mSpells.end() ? nullptr : &it->second;
        }
        const MagicEffectRecord* effectById(FormId id) override
        {
            const auto it = mEffects.find(id);
            return it == mEffects.end() ? nullptr : &it->second;
        }
        const ProjectileRecord* projectileById(FormId id) override { return id == kProjectile ? &mProj : nullptr; }
        bool fighterGear(WorldId base, const std::vector<ArmourPiece>&, FighterGear& out) override
        {
            out = {};
            if (base != kAttackerBase || !mAttackerHasStaff)
                return false;
            out.mKnown = true;
            out.mStaff = &mStaff;
            return true;
        }

        void addEffect(FormId id, std::uint32_t archetype, std::int32_t value, std::uint32_t flags)
        {
            MagicEffectRecord m;
            m.mCommon.mId = id;
            m.mCommon.mEditorId = "Effect" + std::to_string(id);
            m.mHasData = true;
            m.mArchetype = archetype;
            m.mPrimaryValue = value;
            m.mFlags = flags;
            mEffects[id] = m;
        }
        void addSpell(FormId id, const char* name, std::uint32_t cast, std::uint32_t delivery, std::uint32_t type,
            FormId effect, float magnitude, std::uint32_t duration)
        {
            SpellRecord s;
            s.mCommon.mId = id;
            s.mCommon.mEditorId = name;
            s.mHasData = true;
            s.mCost = 0;
            s.mCastType = cast;
            s.mDelivery = delivery;
            s.mEnchantment = true;
            s.mEnchantType = type;
            SpellEffect e;
            e.mEffect = effect;
            e.mMagnitude = magnitude;
            e.mDuration = duration;
            s.mEffects.push_back(e);
            mSpells[id] = s;
        }

        std::map<FormId, SpellRecord> mSpells;
        std::map<FormId, MagicEffectRecord> mEffects;
        ProjectileRecord mProj;
        WeaponRecord mSword, mBound, mStaff, mPlainStaff;
        std::map<WorldId, FighterProfile> mProfiles;
        FighterProfile mPlayer;
        bool mAttackerHasStaff = false;
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
        bool clipEventTime(std::string_view clip, bool, ClipEventKind kind, float& out) override
        {
            out = 0.f;
            if (kind != ClipEventKind::SpellFire)
                return false;
            if (clip == "MRh_Release")
                out = 0.167f;
            else if (clip == "MRh_SelfRelease")
                out = 0.367f;
            else
                return false;
            return true;
        }
        bool launchProjectile(CombatSubject owner, const ProjectileRequest& request) override
        {
            mLaunchOwners.push_back(owner);
            mLaunches.push_back(request);
            return true;
        }
        bool requestSweep(CombatSubject subject, const SweepRequest& request) override
        {
            mSweeps.push_back({ subject, request });
            return true;
        }
        bool reportDamage(const DamageReport& report) override
        {
            mDamage.push_back(report);
            if (report.victimIsPlayer)
                mPlayerState.health = report.healthAfter;
            return true;
        }
        bool reportDeath(ActorHandle) override { return true; }
        bool faceHeading(CombatSubject, float) override { return true; }
        bool clipTiming(std::string_view, bool, ClipTiming& out) override
        {
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
        std::vector<ProjectileRequest> mLaunches;
        std::vector<CombatSubject> mLaunchOwners;
        std::vector<std::pair<CombatSubject, SweepRequest>> mSweeps;
        std::vector<DamageReport> mDamage;
        std::map<WorldId, SubjectState> mStates;
        SubjectState mPlayerState;
    };

    ActorInfo info(WorldId reference, WorldId base, std::string_view editorId)
    {
        ActorInfo i;
        i.handle = { reference, 1 };
        i.base = base;
        i.editorId = editorId;
        return i;
    }

    ClipEvent clipEvent(CombatSubject subject, ClipSlot slot, ClipEventKind kind, std::uint32_t token)
    {
        ClipEvent e;
        e.subject = subject;
        e.slot = slot;
        e.token = token;
        e.kind = kind;
        return e;
    }

    struct WeaponMagic : ::testing::Test
    {
        FakeData mData;
        SkyrimCombat mCombat;
        FakeWriter mWriter;
        ActorHandle mA{ 10, 1 };
        ActorHandle mB{ 20, 1 };
        std::vector<std::string> mTrace;

        void SetUp() override
        {
            mCombat.setTrace([this](const std::string& line) { mTrace.push_back(line); });
            mWriter.mStates[10] = { 100.0, 200.0, 5.0, kPi / 2.f, 0.f, 0.f };
            mWriter.mStates[20] = { 100.0, 260.0, 5.0, 0.f, 0.f, 0.f };
            mWriter.mPlayerState = { 0.0, 0.0, 0.0, 1.f, 90.f, 100.f };
        }
        void TearDown() override
        {
            if (HasFailure())
                for (const std::string& line : mTrace)
                    std::cout << "  trace: " << line << "\n";
        }
        void start(bool magic, bool swordInHand = true)
        {
            mCombat.setData(&mData);
            mCombat.setWeaponMagic(magic);
            if (swordInHand)
                mCombat.setTestWeapon("IronSword");
            mCombat.onActorAdded(info(10, kAttackerBase, "Attacker"));
            mCombat.onActorAdded(info(20, kVictimBase, "Victim"));
        }
        void update(float seconds = 0.25f) { mCombat.combatUpdate(seconds, mWriter); }
        void updates(int n, float seconds = 0.25f)
        {
            for (int i = 0; i < n; ++i)
                update(seconds);
        }
        bool traceHas(const std::string& text) const
        {
            return std::any_of(mTrace.begin(), mTrace.end(),
                [&](const std::string& line) { return line.find(text) != std::string::npos; });
        }
        float healthOf(ActorHandle h)
        {
            SkyrimCombat::FighterView v;
            EXPECT_TRUE(mCombat.readView(h, v));
            return v.mHealth;
        }

        // The player swings (a normal attack) and the sweep finds the victim B.
        void playerHitsVictim()
        {
            mWriter.mSweeps.clear();
            mWriter.mDamage.clear();
            mCombat.onPlayerAttack(false);
            for (int i = 0; i < 6 && mWriter.mSweeps.empty(); ++i)
                update(0.2f);
            ASSERT_FALSE(mWriter.mSweeps.empty());
            SweepHit hit;
            hit.target = SweepTarget::Actor;
            hit.actor = mB;
            hit.distance = 20.f;
            mCombat.onSweepResult(CombatSubject::ofPlayer(), mWriter.mSweeps[0].second.token, &hit, 1);
            update(0.f);
            updates(10, 0.2f);
        }
        void npcHitsPlayer(std::uint32_t token)
        {
            mWriter.mDamage.clear();
            mCombat.onClipEvent(clipEvent(CombatSubject::of(mA), ClipSlot::Attack, ClipEventKind::Hit, token));
            update(0.016f);
            SweepHit hit;
            hit.target = SweepTarget::Player;
            hit.distance = 30.f;
            mCombat.onSweepResult(CombatSubject::of(mA), token, &hit, 1);
            update(0.016f);
        }
        // The player's cast: press; the player has no clip, so the spell goes after its timer.
        void playerCastFires()
        {
            mCombat.onPlayerAttackEdge(true);
            updates(3);
        }
    };

    // ---- enchanted weapons -----------------------------------------------------------------------------------------

    TEST_F(WeaponMagic, anEnchantedSwordAddsItsEffectsToABlowThatLands)
    {
        mData.mSword.mEnchantment = kTouchEnchant;
        start(true);
        playerHitsVictim();
        ASSERT_EQ(mWriter.mDamage.size(), 2u); // the blow, then the enchantment's 8
        EXPECT_NEAR(mWriter.mDamage[0].amount, 7.525f, 1e-3f);
        EXPECT_NEAR(mWriter.mDamage[1].amount, 8.f, 1e-3f);
        EXPECT_NEAR(healthOf(mB), 50.f - 7.525f - 8.f, 1e-3f);
    }

    TEST_F(WeaponMagic, withTheSettingOffTheEnchantmentDoesNothing)
    {
        mData.mSword.mEnchantment = kTouchEnchant;
        start(false);
        playerHitsVictim();
        ASSERT_EQ(mWriter.mDamage.size(), 1u);
        EXPECT_NEAR(healthOf(mB), 50.f - 7.525f, 1e-3f);
    }

    TEST_F(WeaponMagic, onlyAFireAndForgetTouchEnchantmentAppliesOnTheBlow)
    {
        for (const FormId id : { kSelfEnchant, kConstantEnchant })
        {
            mData.mSword.mEnchantment = id;
            mTrace.clear();
            SkyrimCombat combat;
            combat.setData(&mData);
            combat.setWeaponMagic(true);
            combat.setTestWeapon("IronSword");
            combat.onActorAdded(info(10, kAttackerBase, "Attacker"));
            combat.onActorAdded(info(20, kVictimBase, "Victim"));
            FakeWriter writer = mWriter;
            writer.mSweeps.clear();
            writer.mDamage.clear();
            combat.onPlayerAttack(false);
            for (int i = 0; i < 6 && writer.mSweeps.empty(); ++i)
                combat.combatUpdate(0.2f, writer);
            ASSERT_FALSE(writer.mSweeps.empty());
            SweepHit hit;
            hit.target = SweepTarget::Actor;
            hit.actor = mB;
            hit.distance = 20.f;
            combat.onSweepResult(CombatSubject::ofPlayer(), writer.mSweeps[0].second.token, &hit, 1);
            combat.combatUpdate(0.f, writer);
            EXPECT_EQ(writer.mDamage.size(), 1u) << "enchantment " << id;
        }
    }

    TEST_F(WeaponMagic, aSwingThatMissesAppliesNoEnchantment)
    {
        mData.mSword.mEnchantment = kTouchEnchant;
        start(true);
        mWriter.mSweeps.clear();
        mCombat.onPlayerAttack(false);
        for (int i = 0; i < 6 && mWriter.mSweeps.empty(); ++i)
            update(0.2f);
        ASSERT_FALSE(mWriter.mSweeps.empty());
        mCombat.onSweepResult(CombatSubject::ofPlayer(), mWriter.mSweeps[0].second.token, nullptr, 0);
        update(0.f);
        EXPECT_TRUE(mWriter.mDamage.empty());
        EXPECT_FLOAT_EQ(healthOf(mB), 50.f);
    }

    TEST_F(WeaponMagic, anNpcsEnchantedWeaponHurtsThePlayerBeyondTheBlow)
    {
        mData.mSword.mEnchantment = kTouchEnchant;
        start(true);
        npcHitsPlayer(5);
        ASSERT_EQ(mWriter.mDamage.size(), 2u);
        EXPECT_TRUE(mWriter.mDamage[1].victimIsPlayer);
        EXPECT_NEAR(mWriter.mDamage[1].amount, 8.f, 1e-3f);
    }

    // ---- staves ----------------------------------------------------------------------------------------------------

    TEST_F(WeaponMagic, theEquippedStaffGivesItsEnchantmentAsThePlayersSpell)
    {
        start(true, false);
        SkyrimCombat::PlayerEquipment eq;
        eq.mWeapon = &mData.mStaff;
        mCombat.setPlayerEquipment(eq);
        update();
        const SpellRecord* spell = mCombat.playerStaffSpell();
        ASSERT_NE(spell, nullptr);
        EXPECT_EQ(spell->mCommon.mId, kStaffEnchant);
    }

    TEST_F(WeaponMagic, noStaffSpellWithTheSettingOffOrWithoutAnEnchantmentOrWithASword)
    {
        start(false, false);
        SkyrimCombat::PlayerEquipment eq;
        eq.mWeapon = &mData.mStaff;
        mCombat.setPlayerEquipment(eq);
        update();
        EXPECT_EQ(mCombat.playerStaffSpell(), nullptr); // setting off
        mCombat.setWeaponMagic(true);
        eq.mWeapon = &mData.mPlainStaff;
        mCombat.setPlayerEquipment(eq);
        EXPECT_EQ(mCombat.playerStaffSpell(), nullptr); // no enchantment
        eq.mWeapon = &mData.mSword;
        mCombat.setPlayerEquipment(eq);
        EXPECT_EQ(mCombat.playerStaffSpell(), nullptr); // not a staff
    }

    TEST_F(WeaponMagic, theAttackControlWithAStaffCastsItsEnchantmentAtNoMagickaAndDoesNotSwing)
    {
        start(true, false);
        SkyrimCombat::PlayerEquipment eq;
        eq.mWeapon = &mData.mStaff;
        mCombat.setPlayerEquipment(eq);
        update();
        mCombat.onPlayerAttack(false);
        playerCastFires();
        updates(2);
        ASSERT_EQ(mWriter.mLaunches.size(), 1u);
        EXPECT_TRUE(mWriter.mLaunchOwners[0].player);
        EXPECT_FLOAT_EQ(mWriter.mLaunches[0].speed, 2500.f);
        EXPECT_TRUE(mWriter.mSweeps.empty()) << "a staff is not swung";
        SkyrimCombat::FighterView v;
        ASSERT_TRUE(mCombat.fighterView(CombatSubject::ofPlayer(), v));
        EXPECT_FLOAT_EQ(v.mMagicka, v.mMagickaMax); // the staff spends charge, not magicka
        // The bolt hits the victim for the enchantment's 20.
        ProjectileResult r;
        r.owner = CombatSubject::ofPlayer();
        r.token = mWriter.mLaunches[0].token;
        r.fate = ProjectileFate::HitActor;
        r.actor = mB;
        mCombat.onProjectileResult(r);
        update();
        EXPECT_NEAR(healthOf(mB), 30.f, 1e-3f);
    }

    TEST_F(WeaponMagic, aStaffNpcCastsItsEnchantmentAtARangeWithTheAiOn)
    {
        mData.mAttackerHasStaff = true;
        mData.mProfiles[kAttackerBase].mAi.mStyle.mMagicScore = 4.05f;
        mCombat.setData(&mData);
        mCombat.setWeaponMagic(true);
        mCombat.setRealGear(true);
        mCombat.setCombatAi(true);
        ASSERT_TRUE(mCombat.setTestHostile("attacker"));
        mCombat.onActorAdded(info(10, kAttackerBase, "Attacker"));
        mCombat.onActorAdded(info(20, kVictimBase, "Victim"));
        mWriter.mPlayerState.y = 900.0;
        mWriter.mPlayerState.x = 100.0;
        std::size_t casts = 0;
        for (int i = 0; i < 120 && casts == 0; ++i)
        {
            update(0.1f);
            casts = static_cast<std::size_t>(std::count_if(mWriter.mClips.begin(), mWriter.mClips.end(),
                [](const FakeWriter::Clip& c) { return c.mSlot == ClipSlot::Cast && !c.mSubject.player; }));
        }
        EXPECT_GE(casts, 1u);
        EXPECT_TRUE(traceHas("StaffFire")) << "the cast is of the staff's enchantment";
    }

    // ---- bound weapons ---------------------------------------------------------------------------------------------

    TEST_F(WeaponMagic, aBoundWeaponEffectPutsItsWeaponInTheCastersHandForItsDuration)
    {
        start(true);
        ASSERT_TRUE(mCombat.setTestPlayerSpell("BoundSwordSpell"));
        EXPECT_EQ(mCombat.boundWeaponOf(CombatSubject::ofPlayer()), nullptr);
        playerCastFires();
        const WeaponRecord* bound = mCombat.boundWeaponOf(CombatSubject::ofPlayer());
        ASSERT_NE(bound, nullptr);
        EXPECT_EQ(bound->mCommon.mId, kBoundSwordId);
        // it hits harder than the iron sword: 20 x 1.075 = 21.5 (the attack control swings again once no spell is set)
        ASSERT_TRUE(mCombat.setTestPlayerSpell(""));
        updates(8);
        playerHitsVictim();
        ASSERT_FALSE(mWriter.mDamage.empty());
        EXPECT_NEAR(mWriter.mDamage[0].amount, 21.5f, 1e-3f);
        // and it goes after 60 seconds
        updates(260, 0.25f);
        EXPECT_EQ(mCombat.boundWeaponOf(CombatSubject::ofPlayer()), nullptr);
    }

    TEST_F(WeaponMagic, aBoundWeaponDoesNothingWithTheSettingOff)
    {
        start(false);
        ASSERT_TRUE(mCombat.setTestPlayerSpell("BoundSwordSpell"));
        playerCastFires();
        EXPECT_EQ(mCombat.boundWeaponOf(CombatSubject::ofPlayer()), nullptr);
    }

    TEST_F(WeaponMagic, twoRunsOfTheEnchantedBlowGiveTheSameTrace)
    {
        std::vector<std::string> first;
        for (int run = 0; run < 2; ++run)
        {
            FakeData data;
            data.mSword.mEnchantment = kTouchEnchant;
            SkyrimCombat combat;
            std::vector<std::string> trace;
            combat.setTrace([&](const std::string& line) { trace.push_back(line); });
            combat.setData(&data);
            combat.setWeaponMagic(true);
            combat.setTestWeapon("IronSword");
            combat.onActorAdded(info(10, kAttackerBase, "Attacker"));
            combat.onActorAdded(info(20, kVictimBase, "Victim"));
            FakeWriter writer = mWriter;
            combat.onPlayerAttack(false);
            for (int i = 0; i < 6 && writer.mSweeps.empty(); ++i)
                combat.combatUpdate(0.2f, writer);
            ASSERT_FALSE(writer.mSweeps.empty());
            SweepHit hit;
            hit.target = SweepTarget::Actor;
            hit.actor = mB;
            hit.distance = 20.f;
            combat.onSweepResult(CombatSubject::ofPlayer(), writer.mSweeps[0].second.token, &hit, 1);
            combat.combatUpdate(0.f, writer);
            if (run == 0)
                first = trace;
            else
                EXPECT_EQ(first, trace);
        }
    }
}
