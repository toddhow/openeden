// Task 0193 (milestone M-S5, stage S5-10 "impacts and sounds"): the IPCT, IPDS, MATT, SNDR records a hit reads, the
// weapon and race fields that choose them, the pure rules (material of a target, the impact for a pair, the file of a
// descriptor) and the SkyrimCombat glue: a swing event, a hit, a block, a bash, bare hands, a player victim, a clip's
// own `SoundPlay.<id>` event, the bow's two sounds and the setting that switches them off. Generated data only; the
// layouts are the ones the five masters were surveyed to have (see combat_impact.hpp).
#include <game_skyrimse/combat.hpp>
#include <game_skyrimse/combat_impact.hpp>
#include <game_skyrimse/combat_sound.hpp>
#include <game_skyrimse/hud.hpp>

#include <gtest/gtest.h>

#include <algorithm>
#include <map>
#include <sstream>
#include <string>
#include <vector>

#include "../contentsource/pluginbuilder.hpp"

namespace
{
    using namespace ContentTests;
    using namespace game_skyrimse;
    namespace sdk = openeden::sdk::game;

    // ---- the decoders ----------------------------------------------------------------------------------------------

    LoadOrder twoPlugins()
    {
        LoadOrder order;
        std::string err;
        EXPECT_TRUE(order.add("Skyrim.esm", false, err));
        EXPECT_TRUE(order.add("test.esp", false, err));
        return order;
    }

    struct Fixture
    {
        LoadOrder mOrder = twoPlugins();
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
        r.mOffset = 0x99;
        return r;
    }

    TEST(SkyrimCombatSoundRecords, anImpactHasItsTwoSounds)
    {
        Fixture f;
        Bytes subs;
        subs.append(sub4("EDID", Bytes().zstring("WPNBlade1HandVsFleshImpact")));
        subs.append(sub4("DATA", Bytes().u32(0).u32(0).u32(0).u32(0).u32(0).u32(0)));
        subs.append(sub4("SNAM", Bytes().u32(0x01000300)));
        subs.append(sub4("NAM1", Bytes().u32(0x00000301)));
        ImpactRecord r;
        std::string why;
        ASSERT_TRUE(decodeImpact(view(subs), raw("IPCT", 0x01000200), f.context(), r, why)) << why;
        EXPECT_EQ(r.mCommon.mEditorId, "WPNBlade1HandVsFleshImpact");
        EXPECT_EQ(r.mSound1, *f.mResolver.resolve(0x01000300));
        EXPECT_EQ(r.mSound2, *f.mResolver.resolve(0x00000301));
        EXPECT_NE(r.mSound1, r.mSound2);
    }

    TEST(SkyrimCombatSoundRecords, anImpactSoundOfAnotherSizeIsOddAndNotUsed)
    {
        Fixture f;
        Bytes subs;
        subs.append(sub4("SNAM", Bytes().u32(1).u32(2)));
        subs.append(sub4("NAM1", Bytes().u16(7)));
        ImpactRecord r;
        std::string why;
        ASSERT_TRUE(decodeImpact(view(subs), raw("IPCT", 0x01000200), f.context(), r, why)) << why;
        EXPECT_EQ(r.mSound1, kNullFormId);
        EXPECT_EQ(r.mSound2, kNullFormId);
        EXPECT_EQ(f.mNotes.mOddSubrecords, 2u);
    }

    TEST(SkyrimCombatSoundRecords, anImpactDataSetKeepsItsPairsInOrder)
    {
        Fixture f;
        Bytes subs;
        subs.append(sub4("EDID", Bytes().zstring("WPNzBlade1HandImpactSet")));
        subs.append(sub4("PNAM", Bytes().u32(0x00000100).u32(0x00000200)));
        subs.append(sub4("PNAM", Bytes().u32(0x00000101).u32(0x00000201)));
        subs.append(sub4("PNAM", Bytes().u32(5))); // odd
        ImpactSetRecord r;
        std::string why;
        ASSERT_TRUE(decodeImpactSet(view(subs), raw("IPDS", 0x01000400), f.context(), r, why)) << why;
        EXPECT_EQ(r.mCommon.mEditorId, "WPNzBlade1HandImpactSet");
        ASSERT_EQ(r.mPairs.size(), 2u);
        EXPECT_EQ(r.mPairs[0].mMaterial, *f.mResolver.resolve(0x00000100));
        EXPECT_EQ(r.mPairs[0].mImpact, *f.mResolver.resolve(0x00000200));
        EXPECT_EQ(r.mPairs[1].mMaterial, *f.mResolver.resolve(0x00000101));
        EXPECT_EQ(f.mNotes.mOddSubrecords, 1u);
    }

    TEST(SkyrimCombatSoundRecords, aHostileImpactDataSetIsCapped)
    {
        Fixture f;
        Bytes subs;
        for (std::size_t i = 0; i < kMaxImpactPairs + 40; ++i)
            subs.append(sub4("PNAM", Bytes().u32(0x100 + static_cast<std::uint32_t>(i)).u32(0x200)));
        ImpactSetRecord r;
        std::string why;
        ASSERT_TRUE(decodeImpactSet(view(subs), raw("IPDS", 0x01000400), f.context(), r, why)) << why;
        EXPECT_EQ(r.mPairs.size(), kMaxImpactPairs);
    }

    TEST(SkyrimCombatSoundRecords, aMaterialIsItsName)
    {
        Fixture f;
        Bytes subs;
        subs.append(sub4("EDID", Bytes().zstring("MaterialSkin")));
        subs.append(sub4("CNAM", Bytes().u32(0).u32(0).u32(0)));
        MaterialRecord r;
        std::string why;
        ASSERT_TRUE(decodeMaterial(view(subs), raw("MATT", 0x00000100), f.context(), r, why)) << why;
        EXPECT_EQ(r.mCommon.mEditorId, "MaterialSkin");
    }

    TEST(SkyrimCombatSoundRecords, aSoundMarkerIsAPointerToItsDescriptor)
    {
        Fixture f;
        Bytes subs;
        subs.append(sub4("EDID", Bytes().zstring("WPNBlockBladeVsOther")));
        subs.append(sub4("FNAM", Bytes().zstring("fx\\wpn\\block\\blade\\"))); // a folder, not a file
        subs.append(sub4("SDSC", Bytes().u32(0x0000030E)));
        SoundRedirectRecord r;
        std::string why;
        ASSERT_TRUE(decodeSoundRedirect(view(subs), raw("SOUN", 0x01000500), f.context(), r, why)) << why;
        EXPECT_EQ(r.mCommon.mEditorId, "WPNBlockBladeVsOther");
        EXPECT_EQ(r.mDescriptor, *f.mResolver.resolve(0x30E));
        Bytes odd;
        odd.append(sub4("SDSC", Bytes().u16(3)));
        ASSERT_TRUE(decodeSoundRedirect(view(odd), raw("SOUN", 0x01000500), f.context(), r, why)) << why;
        EXPECT_EQ(r.mDescriptor, kNullFormId);
    }

    TEST(SkyrimCombatSoundRecords, aWeaponCarriesItsImpactAndSoundLinks)
    {
        Fixture f;
        Bytes subs;
        subs.append(sub4("EDID", Bytes().zstring("IronSword")));
        subs.append(sub4("INAM", Bytes().u32(0x00000401)));
        subs.append(sub4("BIDS", Bytes().u32(0x00000402)));
        subs.append(sub4("BAMT", Bytes().u32(0x00000403)));
        subs.append(sub4("TNAM", Bytes().u32(0x00000404)));
        subs.append(sub4("NAM9", Bytes().u32(0x00000405)));
        subs.append(sub4("NAM8", Bytes().u32(0x00000406)));
        subs.append(sub4("SNAM", Bytes().u32(0x00000407)));
        WeaponRecord w;
        std::string why;
        ASSERT_TRUE(decodeWeapon(view(subs), raw("WEAP", 0x01000A00), f.context(), w, why)) << why;
        EXPECT_EQ(w.mImpactSet, *f.mResolver.resolve(0x401));
        EXPECT_EQ(w.mBashImpactSet, *f.mResolver.resolve(0x402));
        EXPECT_EQ(w.mBlockMaterial, *f.mResolver.resolve(0x403));
        EXPECT_EQ(w.mSwingSound, *f.mResolver.resolve(0x404));
        EXPECT_EQ(w.mDrawSound, *f.mResolver.resolve(0x405));
        EXPECT_EQ(w.mSheatheSound, *f.mResolver.resolve(0x406));
        EXPECT_EQ(w.mAttackSound, *f.mResolver.resolve(0x407));
    }

    TEST(SkyrimCombatSoundRecords, aRaceCarriesItsBodyMaterialAndBareHandsImpacts)
    {
        Fixture f;
        Bytes subs;
        subs.append(sub4("EDID", Bytes().zstring("NordRace")));
        subs.append(sub4("NAM4", Bytes().u32(0x00000100)));
        subs.append(sub4("NAM5", Bytes().u32(0x00000402)));
        RaceCombatRecord r;
        std::string why;
        ASSERT_TRUE(decodeRaceCombat(view(subs), raw("RACE", 0x01000B00), f.context(), r, why)) << why;
        EXPECT_EQ(r.mBodyMaterial, *f.mResolver.resolve(0x100));
        EXPECT_EQ(r.mUnarmedImpactSet, *f.mResolver.resolve(0x402));
    }

    // ---- the library and the pure rules ----------------------------------------------------------------------------

    constexpr FormId kSkin = 0x100, kHeavy = 0x101, kBlockBlade = 0x102, kStone = 0x103, kLight = 0x104;
    constexpr FormId kFleshImpact = 0x200, kMetalImpact = 0x201, kBlockImpact = 0x202, kBashImpact = 0x203,
                     kHandsImpact = 0x204, kArrowImpact = 0x205;
    constexpr FormId kFleshSnd = 0x300, kFleshSnd2 = 0x301, kMetalSnd = 0x302, kBlockSnd = 0x303, kBashSnd = 0x304,
                     kHandsSnd = 0x305, kSwingSnd = 0x306, kDrawSnd = 0x307, kArrowSnd = 0x308, kNockSnd = 0x309,
                     kFireSnd = 0x30A, kNamedSnd = 0x30B, kUnarmedSwingSnd = 0x30C, kBowAttackSnd = 0x30D,
                     kBlockSndr = 0x30E;
    constexpr FormId kSwordSet = 0x400, kBashSet = 0x401, kHandsSet = 0x402, kBowSet = 0x403;

    SoundDescriptorRecord snd(
        FormId id, const char* name, std::vector<std::string> files, std::uint16_t attenuation = 0)
    {
        SoundDescriptorRecord s;
        s.mCommon.mId = id;
        s.mCommon.mEditorId = name;
        s.mFiles = std::move(files);
        if (attenuation != 0)
            s.mAttenuation = attenuation;
        return s;
    }

    SoundLibrary makeLibrary()
    {
        SoundRecords r;
        auto material = [&](FormId id, const char* name) {
            MaterialRecord m;
            m.mCommon.mId = id;
            m.mCommon.mEditorId = name;
            r.mMaterials.push_back(m);
        };
        material(kSkin, "MaterialSkin");
        material(kHeavy, "MaterialArmorHeavy");
        material(kLight, "MaterialArmorLight");
        material(kBlockBlade, "MaterialBlockBlade1Hand");
        material(kStone, "MaterialStone");
        auto impact = [&](FormId id, const char* name, FormId s1, FormId s2) {
            ImpactRecord i;
            i.mCommon.mId = id;
            i.mCommon.mEditorId = name;
            i.mSound1 = s1;
            i.mSound2 = s2;
            r.mImpacts.push_back(i);
        };
        impact(kFleshImpact, "FleshImpact", kFleshSnd, kFleshSnd2);
        impact(kMetalImpact, "MetalImpact", kMetalSnd, kNullFormId);
        impact(kBlockImpact, "BlockImpact", kBlockSnd, kNullFormId);
        impact(kBashImpact, "BashImpact", kBashSnd, kNullFormId);
        impact(kHandsImpact, "HandsImpact", kHandsSnd, kNullFormId);
        impact(kArrowImpact, "ArrowImpact", kArrowSnd, kNullFormId);
        auto set = [&](FormId id, const char* name, std::vector<ImpactPair> pairs) {
            ImpactSetRecord s;
            s.mCommon.mId = id;
            s.mCommon.mEditorId = name;
            s.mPairs = std::move(pairs);
            r.mImpactSets.push_back(s);
        };
        set(kSwordSet, "WPNzBlade1HandImpactSet",
            { { kSkin, kFleshImpact }, { kHeavy, kMetalImpact }, { kBlockBlade, kBlockImpact } });
        set(kBashSet, "WPNBashBladeImpactSet", { { kSkin, kBashImpact }, { kHeavy, kBashImpact } });
        set(kHandsSet, "WPNzUnarmedImpactSet", { { kSkin, kHandsImpact } });
        set(kBowSet, "WPNzArrowImpactSet", { { kSkin, kArrowImpact }, { kStone, kArrowImpact } });
        r.mSounds.push_back(snd(kFleshSnd, "WPNImpactBladeVsFleshSD",
            { "Data\\Sound\\FX\\WPN\\flesh_01.wav", "Data\\Sound\\FX\\WPN\\flesh_02.wav" }, 300));
        r.mSounds.push_back(snd(kFleshSnd2, "WPNImpactBladeVsFleshPLAYER", { "fx\\wpn\\flesh_player.wav" }));
        r.mSounds.push_back(snd(kMetalSnd, "WPNImpactBladeVsOtherSD", { "Data\\Sound\\FX\\WPN\\metal.wav" }));
        // The block impact names a SOUN (as 83 of the data's IPCT sounds do), which points at the SNDR that plays.
        SoundRedirectRecord soun;
        soun.mCommon.mId = kBlockSnd;
        soun.mCommon.mEditorId = "WPNBlockBladeVsOther";
        soun.mDescriptor = kBlockSndr;
        r.mRedirects.push_back(soun);
        r.mSounds.push_back(snd(kBlockSndr, "WPNBlockBladeVsOtherSD", { "Data\\Sound\\FX\\WPN\\block.wav" }));
        r.mSounds.push_back(snd(kBashSnd, "WPNBashBladeVsOther", { "Data\\Sound\\FX\\WPN\\bash.wav" }));
        r.mSounds.push_back(snd(kHandsSnd, "WPNImpactUnarmed", { "Data\\Sound\\FX\\WPN\\hands.wav" }));
        r.mSounds.push_back(snd(kSwingSnd, "WPNSwingBladeMediumSD", { "Data\\Sound\\FX\\WPN\\swing.wav" }));
        r.mSounds.push_back(snd(kDrawSnd, "WPNBlade1HandDrawSD", { "Data\\Sound\\FX\\WPN\\draw.wav" }));
        r.mSounds.push_back(snd(kArrowSnd, "WPNImpactArrowVsFleshSD", { "Data\\Sound\\FX\\WPN\\arrow.wav" }));
        r.mSounds.push_back(snd(kNockSnd, "WPNBowNockSD", { "Data\\Sound\\FX\\WPN\\nock.wav" }));
        r.mSounds.push_back(snd(kFireSnd, "WPNBowFireSD", { "Data\\Sound\\FX\\WPN\\fire.wav" }));
        r.mSounds.push_back(snd(kNamedSnd, "NPCHumanCombatShieldBash", { "Data\\Sound\\FX\\NPC\\shieldbash.wav" }));
        r.mSounds.push_back(snd(kUnarmedSwingSnd, "WPNSwingUnarmed", { "Data\\Sound\\FX\\WPN\\unarmed.wav" }));
        r.mSounds.push_back(snd(kBowAttackSnd, "WPNCrossbowFire", { "Data\\Sound\\FX\\WPN\\xbow.wav" }));
        SoundLibrary lib;
        lib.add(r);
        return lib;
    }

    TEST(SkyrimCombatSoundLibrary, namesAndPairsAreFound)
    {
        const SoundLibrary lib = makeLibrary();
        EXPECT_EQ(lib.materialByEditorId("materialskin"), kSkin); // case-insensitive
        EXPECT_EQ(lib.materialByEditorId("MaterialNone"), kNullFormId);
        EXPECT_EQ(lib.soundByEditorId("WPNBowFireSD"), kFireSnd);
        EXPECT_EQ(lib.impactSetByEditorId("wpnzunarmedimpactset"), kHandsSet);
        EXPECT_EQ(lib.impactFor(kSwordSet, kHeavy), kMetalImpact);
        EXPECT_EQ(lib.impactFor(kSwordSet, kStone), kNullFormId); // no pair
        EXPECT_EQ(lib.impactFor(0x999, kSkin), kNullFormId); // no set
        EXPECT_EQ(lib.impactFor(kSwordSet, kNullFormId), kNullFormId);
    }

    TEST(SkyrimCombatSoundLibrary, aSoundMarkerResolvesToTheDescriptorItNames)
    {
        const SoundLibrary lib = makeLibrary();
        ASSERT_NE(lib.sound(kBlockSnd), nullptr);
        EXPECT_EQ(lib.sound(kBlockSnd)->mCommon.mEditorId, "WPNBlockBladeVsOtherSD");
        EXPECT_EQ(lib.soundByEditorId("WPNBlockBladeVsOther"), kBlockSnd); // by the marker's own name too
        ResolvedSound out;
        ASSERT_TRUE(resolveSound(lib, kBlockSnd, 0, {}, out));
        EXPECT_EQ(out.mFile, "sound/fx/wpn/block.wav");
        EXPECT_EQ(out.mName, "WPNBlockBladeVsOtherSD");
    }

    TEST(SkyrimCombatSoundLibrary, aLaterRecordReplacesAndADeletedOneGoes)
    {
        SoundLibrary lib = makeLibrary();
        SoundRecords more;
        more.mSounds.push_back(snd(kMetalSnd, "WPNImpactBladeVsOtherSD", { "Data\\Sound\\FX\\WPN\\metal2.wav" }));
        SoundDescriptorRecord gone = snd(kBashSnd, "WPNBashBladeVsOther", {});
        gone.mCommon.mFlags = RecordFlags::Deleted;
        more.mSounds.push_back(gone);
        lib.add(more);
        ResolvedSound out;
        ASSERT_TRUE(resolveSound(lib, kMetalSnd, 0, {}, out));
        EXPECT_EQ(out.mFile, "sound/fx/wpn/metal2.wav");
        EXPECT_FALSE(resolveSound(lib, kBashSnd, 0, {}, out));
    }

    TEST(SkyrimCombatSoundRules, aFileIsNormalisedTakenInTurnAndScaledByItsAttenuation)
    {
        const SoundLibrary lib = makeLibrary();
        ResolvedSound a, b, c;
        ASSERT_TRUE(resolveSound(lib, kFleshSnd, 0, {}, a));
        ASSERT_TRUE(resolveSound(lib, kFleshSnd, 1, {}, b));
        ASSERT_TRUE(resolveSound(lib, kFleshSnd, 2, {}, c));
        EXPECT_EQ(a.mFile, "sound/fx/wpn/flesh_01.wav");
        EXPECT_EQ(b.mFile, "sound/fx/wpn/flesh_02.wav");
        EXPECT_EQ(c.mFile, a.mFile); // the files take turns
        EXPECT_EQ(a.mName, "WPNImpactBladeVsFleshSD");
        EXPECT_NEAR(a.mVolume, 0.70794578f, 1e-5f); // 300 hundredths of a decibel: 10 ^ (-3 / 20)
    }

    TEST(SkyrimCombatSoundRules, aFileTheProbeDoesNotFindIsLeftOut)
    {
        const SoundLibrary lib = makeLibrary();
        const SoundFileProbe probe = [](const std::string& path) { return path == "sound/fx/wpn/flesh_02.wav"; };
        ResolvedSound a, b;
        ASSERT_TRUE(resolveSound(lib, kFleshSnd, 0, probe, a));
        ASSERT_TRUE(resolveSound(lib, kFleshSnd, 1, probe, b));
        EXPECT_EQ(a.mFile, "sound/fx/wpn/flesh_02.wav");
        EXPECT_EQ(b.mFile, "sound/fx/wpn/flesh_02.wav");
        const SoundFileProbe none = [](const std::string&) { return false; };
        EXPECT_FALSE(resolveSound(lib, kFleshSnd, 0, none, a));
        EXPECT_FALSE(resolveSound(lib, kNullFormId, 0, {}, a));
        EXPECT_FALSE(resolveSound(lib, 0x999, 0, {}, a));
    }

    TEST(SkyrimCombatSoundRules, anArmouredBodyCountsAsItsArmourMaterial)
    {
        const SoundLibrary lib = makeLibrary();
        EXPECT_EQ(targetMaterial(lib, kSkin, -1, true), kSkin);
        EXPECT_EQ(targetMaterial(lib, kSkin, 1, true), kHeavy);
        EXPECT_EQ(targetMaterial(lib, kSkin, 0, true), kLight);
        EXPECT_EQ(targetMaterial(lib, kSkin, 2, true), kSkin); // clothing: the body
        EXPECT_EQ(targetMaterial(lib, kSkin, 1, false), kSkin); // armour materials switched off
    }

    TEST(SkyrimCombatSoundRules, anImpactGivesItsFirstSoundAndTheSecondOnlyWhenAsked)
    {
        const SoundLibrary lib = makeLibrary();
        const ImpactSounds one = impactSoundFor(lib, kSwordSet, kSkin, false);
        EXPECT_EQ(one.mImpact, kFleshImpact);
        EXPECT_EQ(one.mFirst, kFleshSnd);
        EXPECT_EQ(one.mSecond, kNullFormId);
        EXPECT_EQ(impactSoundFor(lib, kSwordSet, kSkin, true).mSecond, kFleshSnd2);
        EXPECT_EQ(impactSoundFor(lib, kSwordSet, kStone, true).mImpact, kNullFormId);
    }

    // ---- SkyrimCombat -----------------------------------------------------------------------------------------------

    constexpr sdk::WorldId kAttackerBase = 100;
    constexpr sdk::WorldId kVictimBase = 200;

    class FakeData final : public CombatData
    {
    public:
        FakeData() : mLibrary(makeLibrary())
        {
            mSword.mCommon.mEditorId = "IronSword";
            mSword.mHasData = true;
            mSword.mDamage = 7;
            mSword.mWeight = 9.f;
            mSword.mHasDnam = true;
            mSword.mAnimType = WeaponAnimType::OneHandSword;
            mSword.mReach = 1.f;
            mSword.mSkillValue = kFirstSkillActorValue;
            mSword.mImpactSet = kSwordSet;
            mSword.mBashImpactSet = kBashSet;
            mSword.mBlockMaterial = kBlockBlade;
            mSword.mSwingSound = kSwingSnd;
            mSword.mDrawSound = kDrawSnd;

            mBow = mSword;
            mBow.mCommon.mEditorId = "HuntingBow";
            mBow.mAnimType = WeaponAnimType::Bow;
            mBow.mImpactSet = kBowSet;

            FighterProfile attacker;
            attacker.mHealthMax = 100.f;
            attacker.mStaminaMax = 100.f;
            attacker.mSkills[0] = 50;
            attacker.mUnarmedDamage = 4.f;
            attacker.mUnarmedReach = 96.f;
            attacker.mBodyMaterial = kSkin;
            attacker.mUnarmedImpactSet = kHandsSet;
            mProfiles[kAttackerBase] = attacker;

            FighterProfile victim;
            victim.mHealthMax = 500.f;
            victim.mStaminaMax = 80.f;
            victim.mBodyMaterial = kSkin;
            mProfiles[kVictimBase] = victim;

            mPlayer.mHealthMax = 100.f;
            mPlayer.mStaminaMax = 100.f;
            mPlayer.mSkills.fill(15);
            mPlayer.mBodyMaterial = kSkin;
            mPlayer.mUnarmedImpactSet = kHandsSet;
        }
        bool fighter(sdk::WorldId base, FighterProfile& out) override
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
            if (editorId == "ironsword")
                return &mSword;
            return editorId == "huntingbow" ? &mBow : nullptr;
        }
        CombatSettings settings() override { return CombatSettings(); }
        const SoundLibrary* soundLibrary() override { return mHaveLibrary ? &mLibrary : nullptr; }
        bool soundFileExists(const std::string& path) override
        {
            ++mProbes;
            return mAllFiles || path != "sound/fx/wpn/flesh_01.wav";
        }

        SoundLibrary mLibrary;
        bool mHaveLibrary = true;
        bool mAllFiles = true;
        int mProbes = 0;
        WeaponRecord mSword, mBow;
        std::map<sdk::WorldId, FighterProfile> mProfiles;
        FighterProfile mPlayer;
    };

    class FakeWriter final : public sdk::CombatRequestWriter
    {
    public:
        struct Sound
        {
            sdk::CombatSubject mSubject;
            std::string mFile, mName;
            float mVolume;
        };
        bool playClip(sdk::CombatSubject, const sdk::ClipRequest&) override { return true; }
        bool stopClip(sdk::CombatSubject) override { return true; }
        bool requestSweep(sdk::CombatSubject, const sdk::SweepRequest&) override { return true; }
        bool reportDamage(const sdk::DamageReport& report) override
        {
            mDamage.push_back(report);
            return true;
        }
        bool reportDeath(sdk::ActorHandle) override { return true; }
        bool faceHeading(sdk::CombatSubject, float) override { return true; }
        bool clipTiming(std::string_view, bool, sdk::ClipTiming& out) override
        {
            out = { true, 0.367f, 1.287f };
            return true;
        }
        bool subjectState(sdk::CombatSubject subject, sdk::SubjectState& out) override
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
        bool playSound(const sdk::SoundRequest& request) override
        {
            if (mRefuseSounds)
                return false;
            mSounds.push_back(
                { request.subject, std::string(request.file), std::string(request.name), request.volume });
            return true;
        }
        std::vector<Sound> mSounds;
        std::vector<sdk::DamageReport> mDamage;
        std::map<sdk::WorldId, sdk::SubjectState> mStates;
        sdk::SubjectState mPlayerState;
        bool mRefuseSounds = false;
    };

    sdk::ActorInfo info(sdk::WorldId reference, sdk::WorldId base, std::string_view editorId)
    {
        sdk::ActorInfo i;
        i.handle = { reference, 1 };
        i.base = base;
        i.editorId = editorId;
        return i;
    }

    sdk::ClipEvent event(sdk::CombatSubject subject, std::uint32_t token, sdk::ClipEventKind kind,
        sdk::ClipSlot slot = sdk::ClipSlot::Attack, std::string_view name = {})
    {
        sdk::ClipEvent e;
        e.subject = subject;
        e.slot = slot;
        e.token = token;
        e.kind = kind;
        e.name = name;
        return e;
    }

    struct CombatSound : ::testing::Test
    {
        FakeData mData;
        SkyrimCombat mCombat;
        FakeWriter mWriter;
        sdk::ActorHandle mA{ 10, 1 };
        sdk::ActorHandle mB{ 20, 1 };
        std::vector<std::string> mTrace;

        void SetUp() override
        {
            mCombat.setData(&mData);
            mCombat.setTestWeapon("IronSword");
            mCombat.setSoundTrace([this](const std::string& line) { mTrace.push_back(line); });
            mCombat.onActorAdded(info(10, kAttackerBase, "Attacker"));
            mCombat.onActorAdded(info(20, kVictimBase, "Victim"));
            mWriter.mStates[10] = { 100.0, 200.0, 5.0, 0.25f, 0.f, 0.f };
            mWriter.mStates[20] = { 100.0, 260.0, 5.0, 0.f, 0.f, 0.f };
            mWriter.mPlayerState = { 0.0, 0.0, 0.0, 1.f, 90.f, 100.f };
            update(); // the first update reads the data and binds the sound records: events before it are not played
        }

        void update() { mCombat.combatUpdate(0.016f, mWriter); }

        // A hit event, a frame to ask for the sweep, the result, a frame to resolve it.
        void blow(sdk::CombatSubject attacker, std::uint32_t token, sdk::SweepHit hit,
            sdk::ClipSlot slot = sdk::ClipSlot::Attack)
        {
            mCombat.onClipEvent(event(attacker, token, sdk::ClipEventKind::Hit, slot));
            update();
            mCombat.onSweepResult(attacker, token, &hit, 1);
            update();
        }
        static sdk::SweepHit onActor(sdk::ActorHandle h)
        {
            sdk::SweepHit hit;
            hit.target = sdk::SweepTarget::Actor;
            hit.actor = h;
            hit.distance = 20.f;
            return hit;
        }
        static sdk::SweepHit onPlayer()
        {
            sdk::SweepHit hit;
            hit.target = sdk::SweepTarget::Player;
            hit.distance = 20.f;
            return hit;
        }
        std::vector<std::string> files() const
        {
            std::vector<std::string> out;
            for (const FakeWriter::Sound& s : mWriter.mSounds)
                out.push_back(s.mFile);
            return out;
        }
    };

    TEST_F(CombatSound, aSwingEventPlaysTheWeaponsSwingSoundAtTheSwingerInTheNextUpdate)
    {
        mCombat.onClipEvent(event(sdk::CombatSubject::of(mA), 5, sdk::ClipEventKind::WeaponSwing));
        EXPECT_TRUE(mWriter.mSounds.empty()); // nothing is requested from inside an event
        update();
        ASSERT_EQ(mWriter.mSounds.size(), 1u);
        EXPECT_EQ(mWriter.mSounds[0].mFile, "sound/fx/wpn/swing.wav");
        EXPECT_EQ(mWriter.mSounds[0].mName, "WPNSwingBladeMediumSD");
        EXPECT_TRUE(mWriter.mSounds[0].mSubject == sdk::CombatSubject::of(mA));
        EXPECT_FLOAT_EQ(mWriter.mSounds[0].mVolume, 1.f);
        ASSERT_FALSE(mTrace.empty());
        EXPECT_NE(mTrace[0].find("CSOUND swing WPNSwingBladeMediumSD sound/fx/wpn/swing.wav"), std::string::npos);
    }

    TEST_F(CombatSound, aHitPlaysTheImpactOfTheWeaponsSetAgainstTheVictimsMaterialAtTheVictim)
    {
        mData.mAllFiles = false; // the probe leaves flesh_01 out
        mCombat.setData(&mData);
        update();
        blow(sdk::CombatSubject::of(mA), 5, onActor(mB));
        ASSERT_EQ(mWriter.mDamage.size(), 1u);
        ASSERT_EQ(mWriter.mSounds.size(), 1u);
        // Skin against the sword set is the flesh impact; the probe leaves flesh_01 out of the files (see FakeData).
        EXPECT_EQ(mWriter.mSounds[0].mFile, "sound/fx/wpn/flesh_02.wav");
        EXPECT_TRUE(mWriter.mSounds[0].mSubject == sdk::CombatSubject::of(mB));
        // 300 hundredths of a decibel: 10 ^ (-3 / 20).
        EXPECT_NEAR(mWriter.mSounds[0].mVolume, 0.70794578f, 1e-5f);
    }

    TEST_F(CombatSound, theFilesOfADescriptorTakeTurnsOneHitAfterTheOther)
    {
        mData.mAllFiles = true;
        mCombat.setData(&mData); // read the probe again
        update();
        blow(sdk::CombatSubject::of(mA), 5, onActor(mB));
        blow(sdk::CombatSubject::of(mA), 6, onActor(mB));
        blow(sdk::CombatSubject::of(mA), 7, onActor(mB));
        ASSERT_EQ(mWriter.mSounds.size(), 3u);
        EXPECT_EQ(mWriter.mSounds[0].mFile, "sound/fx/wpn/flesh_01.wav");
        EXPECT_EQ(mWriter.mSounds[1].mFile, "sound/fx/wpn/flesh_02.wav");
        EXPECT_EQ(mWriter.mSounds[2].mFile, "sound/fx/wpn/flesh_01.wav");
    }

    TEST_F(CombatSound, aHeavyCuirassChangesTheMaterialAndSoTheSound)
    {
        mData.mProfiles[kVictimBase].mPieces.push_back({ 50.f, ArmourSlot::Body, 1, 0, {} });
        mCombat.setData(&mData);
        update();
        blow(sdk::CombatSubject::of(mA), 5, onActor(mB));
        ASSERT_EQ(mWriter.mSounds.size(), 1u);
        EXPECT_EQ(mWriter.mSounds[0].mFile, "sound/fx/wpn/metal.wav");
    }

    TEST_F(CombatSound, aHitOnThePlayerAlsoPlaysTheSecondSoundOfTheImpact)
    {
        mData.mAllFiles = true;
        mCombat.setData(&mData);
        update();
        blow(sdk::CombatSubject::of(mA), 5, onPlayer());
        ASSERT_EQ(mWriter.mSounds.size(), 2u);
        EXPECT_EQ(mWriter.mSounds[0].mFile, "sound/fx/wpn/flesh_01.wav");
        EXPECT_EQ(mWriter.mSounds[1].mFile, "sound/fx/wpn/flesh_player.wav");
        EXPECT_TRUE(mWriter.mSounds[0].mSubject.player);
        EXPECT_TRUE(mWriter.mSounds[1].mSubject.player);
    }

    TEST_F(CombatSound, aBlowOnABlockerPlaysTheBlockSoundOfTheBlockersWeaponMaterial)
    {
        SkyrimCombat blocking; // the blockers are named before the actors appear
        blocking.setData(&mData);
        blocking.setTestWeapon("IronSword");
        ASSERT_TRUE(blocking.setTestBlock("Victim"));
        blocking.setSoundTrace([this](const std::string& line) { mTrace.push_back(line); });
        blocking.onActorAdded(info(10, kAttackerBase, "Attacker"));
        blocking.onActorAdded(info(20, kVictimBase, "Victim"));
        blocking.combatUpdate(0.016f, mWriter);
        blocking.onClipEvent(event(sdk::CombatSubject::of(mA), 5, sdk::ClipEventKind::Hit));
        blocking.combatUpdate(0.016f, mWriter);
        const sdk::SweepHit hit = onActor(mB);
        blocking.onSweepResult(sdk::CombatSubject::of(mA), 5, &hit, 1);
        blocking.combatUpdate(0.016f, mWriter);
        ASSERT_EQ(mWriter.mDamage.size(), 1u);
        ASSERT_EQ(mWriter.mSounds.size(), 1u);
        EXPECT_EQ(mWriter.mSounds[0].mFile, "sound/fx/wpn/block.wav");
        EXPECT_NE(mTrace.back().find("CSOUND block "), std::string::npos);
    }

    TEST_F(CombatSound, aBashUsesTheWeaponsBashSet)
    {
        blow(sdk::CombatSubject::of(mA), 5, onActor(mB), sdk::ClipSlot::Bash);
        ASSERT_EQ(mWriter.mSounds.size(), 1u);
        EXPECT_EQ(mWriter.mSounds[0].mFile, "sound/fx/wpn/bash.wav");
        EXPECT_NE(mTrace.back().find("CSOUND bash "), std::string::npos);
    }

    TEST_F(CombatSound, bareHandsUseTheRacesUnarmedSetAndTheUnarmedSwing)
    {
        SkyrimCombat bare;
        bare.setData(&mData);
        bare.setSoundTrace([this](const std::string& line) { mTrace.push_back(line); });
        bare.onActorAdded(info(10, kAttackerBase, "Attacker"));
        bare.onActorAdded(info(20, kVictimBase, "Victim"));
        bare.combatUpdate(0.016f, mWriter);
        bare.onClipEvent(event(sdk::CombatSubject::of(mA), 1, sdk::ClipEventKind::WeaponSwing));
        bare.combatUpdate(0.016f, mWriter);
        ASSERT_EQ(mWriter.mSounds.size(), 1u);
        EXPECT_EQ(mWriter.mSounds[0].mFile, "sound/fx/wpn/unarmed.wav");
        bare.onClipEvent(event(sdk::CombatSubject::of(mA), 2, sdk::ClipEventKind::Hit));
        bare.combatUpdate(0.016f, mWriter);
        const sdk::SweepHit hit = onActor(mB);
        bare.onSweepResult(sdk::CombatSubject::of(mA), 2, &hit, 1);
        bare.combatUpdate(0.016f, mWriter);
        ASSERT_EQ(mWriter.mSounds.size(), 2u);
        EXPECT_EQ(mWriter.mSounds[1].mFile, "sound/fx/wpn/hands.wav");
    }

    TEST_F(CombatSound, thePlayersSwingDrawsTheWeaponOnceAndSwingsAtTheSwingStart)
    {
        mCombat.onPlayerAttack(false);
        update(); // the swing starts: the draw now, the swing sound next frame
        update();
        ASSERT_GE(mWriter.mSounds.size(), 2u);
        EXPECT_EQ(mWriter.mSounds[0].mFile, "sound/fx/wpn/draw.wav");
        EXPECT_TRUE(mWriter.mSounds[0].mSubject.player);
        EXPECT_EQ(mWriter.mSounds[1].mFile, "sound/fx/wpn/swing.wav");
        for (int i = 0; i < 120; ++i)
            update(); // the first swing is over
        const std::size_t before = mWriter.mSounds.size();
        mCombat.onPlayerAttack(false);
        update();
        update();
        const std::vector<std::string> now = files();
        EXPECT_EQ(std::count(now.begin(), now.end(), "sound/fx/wpn/draw.wav"), 1); // once per fighter
        EXPECT_GT(mWriter.mSounds.size(), before); // a second swing sound
    }

    TEST_F(CombatSound, aClipsOwnSoundEventNamesTheDescriptor)
    {
        mCombat.onClipEvent(event(sdk::CombatSubject::of(mA), 1, sdk::ClipEventKind::Other, sdk::ClipSlot::Bash,
            "SoundPlay.NPCHumanCombatShieldBash"));
        mCombat.onClipEvent(event(sdk::CombatSubject::of(mA), 1, sdk::ClipEventKind::Other, sdk::ClipSlot::Bash,
            "NPCSoundPlay.NPCHumanCombatShieldBash"));
        mCombat.onClipEvent(event(
            sdk::CombatSubject::of(mA), 1, sdk::ClipEventKind::Other, sdk::ClipSlot::Bash, "SoundPlay")); // no payload
        mCombat.onClipEvent(event(sdk::CombatSubject::of(mA), 1, sdk::ClipEventKind::Other, sdk::ClipSlot::Bash,
            "SoundPlay.NoSuchDescriptor"));
        update();
        ASSERT_EQ(mWriter.mSounds.size(), 2u);
        EXPECT_EQ(mWriter.mSounds[0].mFile, "sound/fx/npc/shieldbash.wav");
        EXPECT_EQ(mWriter.mSounds[1].mFile, "sound/fx/npc/shieldbash.wav");
        EXPECT_EQ(mCombat.sounds().unresolved(), 1u);
    }

    TEST_F(CombatSound, theSettingSwitchesEverySoundOff)
    {
        mCombat.setUseSounds(false);
        mCombat.onClipEvent(event(sdk::CombatSubject::of(mA), 5, sdk::ClipEventKind::WeaponSwing));
        update();
        blow(sdk::CombatSubject::of(mA), 6, onActor(mB));
        EXPECT_TRUE(mWriter.mSounds.empty());
        EXPECT_EQ(mWriter.mDamage.size(), 1u); // the fight itself is untouched
    }

    TEST_F(CombatSound, noSoundRecordsMeansNoSoundsAndTheFightGoesOn)
    {
        mData.mHaveLibrary = false;
        mCombat.setData(&mData);
        mCombat.onClipEvent(event(sdk::CombatSubject::of(mA), 5, sdk::ClipEventKind::WeaponSwing));
        update();
        blow(sdk::CombatSubject::of(mA), 6, onActor(mB));
        EXPECT_TRUE(mWriter.mSounds.empty());
        EXPECT_EQ(mWriter.mDamage.size(), 1u);
    }

    TEST_F(CombatSound, aWriterThatRefusesCountsTheRefusalAndNothingBreaks)
    {
        mWriter.mRefuseSounds = true;
        mCombat.onClipEvent(event(sdk::CombatSubject::of(mA), 5, sdk::ClipEventKind::WeaponSwing));
        update();
        blow(sdk::CombatSubject::of(mA), 6, onActor(mB));
        EXPECT_TRUE(mWriter.mSounds.empty());
        EXPECT_GE(mCombat.sounds().refused(), 2u);
        EXPECT_EQ(mCombat.sounds().played(), 0u);
        EXPECT_EQ(mWriter.mDamage.size(), 1u);
    }

    TEST_F(CombatSound, theQueueIsBoundedAndTheDropsAreCounted)
    {
        for (std::size_t i = 0; i < CombatSounds::kQueue + 10; ++i)
            mCombat.onClipEvent(event(sdk::CombatSubject::of(mA), 5, sdk::ClipEventKind::WeaponSwing));
        update();
        EXPECT_EQ(mWriter.mSounds.size(), CombatSounds::kQueue);
        EXPECT_EQ(mCombat.sounds().dropped(), 10u);
    }

    TEST_F(CombatSound, theSoundTraceIsNotACombatLine)
    {
        // Existing traces filter on COMBAT: the sound lines must not start with it (they would change those traces).
        std::vector<std::string> combat;
        mCombat.setTrace([&](const std::string& line) { combat.push_back(line); });
        blow(sdk::CombatSubject::of(mA), 5, onActor(mB));
        ASSERT_FALSE(mTrace.empty());
        for (const std::string& line : mTrace)
            EXPECT_EQ(line.rfind("CSOUND ", 0), 0u);
        for (const std::string& line : combat)
            EXPECT_EQ(line.find("CSOUND"), std::string::npos);
    }

    TEST(SkyrimCombatSoundHud, theHudWriterPassesASoundOnToTheRealWriter)
    {
        // The pack's writer is wrapped by the HUD's (hud.hpp); a request it does not forward never reaches the engine.
        FakeWriter inner;
        CombatHud hud;
        HudWriter wrapped(inner, hud);
        sdk::SoundRequest request;
        request.subject = sdk::CombatSubject::ofPlayer();
        request.file = "sound/fx/wpn/swing.wav";
        request.name = "WPNSwingBladeMediumSD";
        request.volume = 0.25f;
        EXPECT_TRUE(wrapped.playSound(request));
        ASSERT_EQ(inner.mSounds.size(), 1u);
        EXPECT_EQ(inner.mSounds[0].mFile, "sound/fx/wpn/swing.wav");
        EXPECT_FLOAT_EQ(inner.mSounds[0].mVolume, 0.25f);
        inner.mRefuseSounds = true;
        EXPECT_FALSE(wrapped.playSound(request));
    }

    // ---- the bow ---------------------------------------------------------------------------------------------------

    struct SoundsOnly : ::testing::Test
    {
        SoundLibrary mLib = makeLibrary();
        CombatSounds mSounds;
        FakeWriter mWriter;
        void SetUp() override { mSounds.bind(&mLib, {}); }
    };

    TEST_F(SoundsOnly, aBowNocksAndReleasesWithTheStandInSounds)
    {
        const sdk::CombatSubject who = sdk::CombatSubject::ofPlayer();
        mSounds.bowNock(mWriter, who);
        mSounds.bowRelease(mWriter, who, nullptr);
        ASSERT_EQ(mWriter.mSounds.size(), 2u);
        EXPECT_EQ(mWriter.mSounds[0].mFile, "sound/fx/wpn/nock.wav");
        EXPECT_EQ(mWriter.mSounds[1].mFile, "sound/fx/wpn/fire.wav");
    }

    TEST_F(SoundsOnly, aWeaponWithItsOwnAttackSoundReleasesWithThat)
    {
        WeaponRecord crossbow;
        crossbow.mAttackSound = kBowAttackSnd;
        mSounds.bowRelease(mWriter, sdk::CombatSubject::ofPlayer(), &crossbow);
        ASSERT_EQ(mWriter.mSounds.size(), 1u);
        EXPECT_EQ(mWriter.mSounds[0].mFile, "sound/fx/wpn/xbow.wav");
    }

    TEST_F(SoundsOnly, anArrowHitUsesTheBowsImpactSetAgainstFlesh)
    {
        WeaponRecord bow;
        bow.mImpactSet = kBowSet;
        HitSoundInput in;
        in.mWeapon = &bow;
        in.mVictimBodyMaterial = kSkin;
        in.mVictim = sdk::CombatSubject::of({ 20, 1 });
        mSounds.hit(mWriter, in);
        ASSERT_EQ(mWriter.mSounds.size(), 1u);
        EXPECT_EQ(mWriter.mSounds[0].mFile, "sound/fx/wpn/arrow.wav");
    }

    TEST_F(SoundsOnly, anUnboundLibraryIsSilent)
    {
        CombatSounds silent;
        silent.bowNock(mWriter, sdk::CombatSubject::ofPlayer());
        silent.queueSwing(sdk::CombatSubject::ofPlayer(), nullptr);
        silent.flush(mWriter);
        EXPECT_TRUE(mWriter.mSounds.empty());
        EXPECT_FALSE(silent.enabled());
    }
}
