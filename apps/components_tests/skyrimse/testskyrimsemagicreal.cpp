// Task 0186 (milestone M-S5, stage S5-8): the spell and magic effect records on the player's own Skyrim SE masters.
// Gated: skipped unless OPENEDEN_SKYRIMSE_DATA names the game's Data folder. Prints the numbers of the first-slice
// spells (lines SKYRIMSE-MAGIC, game numbers only) and checks the layouts the magic runtime reads.
#include <game_skyrimse/combat_stats.hpp>
#include <game_skyrimse/magic.hpp>
#include <game_skyrimse/pack_core.hpp>
#include <game_skyrimse/records.hpp>

#include <gtest/gtest.h>

#include <cmath>
#include <cstdlib>
#include <filesystem>
#include <iostream>
#include <memory>
#include <string>
#include <vector>

namespace
{
    namespace fs = std::filesystem;
    using namespace game_skyrimse;

    bool loadMagic(CombatIndex& index, std::string& skip)
    {
        const char* env = std::getenv("OPENEDEN_SKYRIMSE_DATA");
        if (env == nullptr || *env == '\0' || !fs::is_directory(env))
        {
            skip = "OPENEDEN_SKYRIMSE_DATA is not set";
            return false;
        }
        const fs::path data(env);
        const std::vector<std::string> names{ "Skyrim.esm", "Update.esm", "Dawnguard.esm", "HearthFires.esm",
            "Dragonborn.esm" };
        LoadOrder order;
        std::string err;
        for (const std::string& n : names)
            if (!fs::exists(data / n) || !order.add(n, false, err))
            {
                skip = "the five official masters are not all in the Data folder";
                return false;
            }
        for (const std::string& n : names)
        {
            Content::Error error;
            const std::unique_ptr<Content::Plugin> file = Content::Plugin::openFile(data / n, error);
            if (file == nullptr)
            {
                skip = "cannot open " + n;
                return false;
            }
            CombatRecords records;
            (void)parseCombatRecords(*file, order, ExteriorLimits(), records);
            index.add(records);
        }
        return true;
    }

    void print(const CombatIndex& index, const std::string& editorId)
    {
        const SpellRecord* spell = index.spellByEditorId(editorId);
        if (spell == nullptr)
        {
            std::cout << "SKYRIMSE-MAGIC " << editorId << " NOT FOUND\n";
            return;
        }
        std::cout << "SKYRIMSE-MAGIC spell " << spell->mCommon.mEditorId << " cost " << spell->mCost << " type "
                  << spell->mType << " cast " << spell->mCastType << " delivery " << spell->mDelivery << " duration "
                  << spell->mCastDuration << " range " << spell->mRange << " effects " << spell->mEffects.size()
                  << "\n";
        for (const SpellEffect& e : spell->mEffects)
        {
            const MagicEffectRecord* m = index.magicEffect(e.mEffect);
            std::cout << "SKYRIMSE-MAGIC   effect " << (m != nullptr ? m->mCommon.mEditorId : "?") << " mag "
                      << e.mMagnitude << " area " << e.mArea << " dur " << e.mDuration;
            if (m != nullptr)
            {
                std::cout << " | flags 0x" << std::hex << m->mFlags << std::dec << " arch " << m->mArchetype << " av "
                          << m->mPrimaryValue << " av2 " << m->mSecondaryValue << " w2 " << m->mSecondaryWeight
                          << " resist " << m->mResistValue << " cast " << m->mCastType << " deliv " << m->mDelivery
                          << " basecost " << m->mBaseCost << " script " << m->mHasScript;
                if (m->mProjectile != kNullFormId)
                {
                    const ProjectileRecord* p = index.projectile(m->mProjectile);
                    std::cout << " proj " << (p != nullptr ? p->mCommon.mEditorId : "?");
                    if (p != nullptr)
                        std::cout << " speed " << p->mSpeed << " grav " << p->mGravity << " range " << p->mRange
                                  << " type " << p->mType << " model " << p->mModel;
                }
            }
            std::cout << "\n";
        }
    }

    TEST(SkyrimMagicRealData, aggregatesOfEverySpellAndEffect)
    {
        CombatIndex index;
        std::string skip;
        if (!loadMagic(index, skip))
            GTEST_SKIP() << skip;
        std::uint64_t spells = 0, withData = 0, effects = 0, unresolved = 0, gated = 0;
        std::uint64_t cast[3] = {}, delivery[5] = {}, spellsFullySupported = 0;
        std::uint64_t byArchetype[64] = {};
        for (const char* probe : { "Healing", "Firebolt", "WardLesser" })
            EXPECT_NE(index.spellByEditorId(probe), nullptr) << probe;
        for (const FormId id : index.spellIds())
        {
            const SpellRecord* s = index.spell(id);
            ++spells;
            if (s->mHasData)
            {
                ++withData;
                if (s->mCastType < 3)
                    ++cast[s->mCastType];
                if (s->mDelivery < 5)
                    ++delivery[s->mDelivery];
            }
            bool allSupported = true;
            for (const SpellEffect& e : s->mEffects)
            {
                ++effects;
                const MagicEffectRecord* m = index.magicEffect(e.mEffect);
                if (m == nullptr)
                {
                    ++unresolved;
                    allSupported = false;
                    continue;
                }
                if (e.mConditions + m->mConditions > 0)
                    ++gated;
                if (m->mArchetype < 64)
                    ++byArchetype[m->mArchetype];
                const std::uint32_t a = m->mArchetype;
                if (!(a == MagicArchetype::ValueModifier || a == MagicArchetype::Absorb || a == MagicArchetype::Dual
                        || a == MagicArchetype::Demoralize || a == MagicArchetype::AccumulateMagnitude
                        || a == MagicArchetype::Stagger || a == MagicArchetype::PeakValueModifier))
                    allSupported = false;
            }
            if (allSupported && !s->mEffects.empty())
                ++spellsFullySupported;
        }
        std::cout << "SKYRIMSE-MAGIC spells " << spells << " with SPIT " << withData << " effects " << effects
                  << " no MGEF " << unresolved << " gated by conditions " << gated << " cast constant/ff/conc "
                  << cast[0] << "/" << cast[1] << "/" << cast[2] << " delivery self/touch/aimed/actor/location "
                  << delivery[0] << "/" << delivery[1] << "/" << delivery[2] << "/" << delivery[3] << "/"
                  << delivery[4] << " spells whose effects are all of the supported archetypes "
                  << spellsFullySupported << "\n";
        std::cout << "SKYRIMSE-MAGIC archetype uses";
        for (int i = 0; i < 64; ++i)
            if (byArchetype[i] != 0)
                std::cout << " " << i << ":" << byArchetype[i];
        std::cout << "\n";
        EXPECT_GT(spells, 1000u);
        EXPECT_EQ(withData, spells); // every SPEL has a 36 byte SPIT
        EXPECT_EQ(unresolved, 0u);
    }

    TEST(SkyrimMagicRealData, theFirstSliceSpellsDecode)
    {
        CombatIndex index;
        std::string skip;
        if (!loadMagic(index, skip))
            GTEST_SKIP() << skip;
        for (const char* name : { "Healing", "FastHealing", "Firebolt", "Flames", "WardLesser", "WardGreater",
                 "WardSteadfast", "Frostbite", "Sparks" })
            print(index, name);
        const MagicSettings settings = index.settings().magicSettings();
        std::cout << "SKYRIMSE-MAGIC settings ward base " << settings.mWardPowerMaxBase << " mult "
                  << settings.mWardPowerMaxMult << " regen delay " << settings.mDamagedMagickaRegenDelay << "\n";
        const SpellRecord* firebolt = index.spellByEditorId("Firebolt");
        ASSERT_NE(firebolt, nullptr);
        EXPECT_TRUE(firebolt->mHasData);
        EXPECT_EQ(firebolt->mCastType, 1u);
        EXPECT_EQ(firebolt->mDelivery, 2u);
    }
}
