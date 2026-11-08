// Task 0085, move 2, stage 2b: a fixture that runs the crafting classes (alchemy, enchanting, repair, recharge) in a
// real MWWorld::ESMStore with the real NPC, potion, ingredient and apparatus classes, behind test doubles of the
// MWBase managers (world, window manager, mechanics manager, Lua manager).
//
// The doubles come from tools/mwbase_fakes/gen.py: every pure virtual function throws, and the doubles below override
// the few functions the crafting code uses (found by running it: a call that is not listed fails the test with the
// function's name). The doubles record what the code asked of them, so the tests can pin that too.
//
// Records and numbers here are TEST INPUTS. They are not claimed to be Morrowind's data: the tests pin what the
// current code computes from them, so that moving the code cannot change a result.
#ifndef OPENEDEN_TESTS_MWMECHANICS_CRAFTINGFIXTURE_H
#define OPENEDEN_TESTS_MWMECHANICS_CRAFTINGFIXTURE_H

#include <cstdint>
#include <memory>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

#include <gtest/gtest.h>

#include <components/esm3/loadappa.hpp>
#include <components/esm3/loadgmst.hpp>
#include <components/esm3/loadingr.hpp>
#include <components/esm3/loadmgef.hpp>
#include <components/esm3/loadnpc.hpp>
#include <components/esm3/loadrace.hpp>
#include <components/esm3/loadskil.hpp>
#include <components/esm3/readerscache.hpp>
#include <components/misc/rng.hpp>

#include <components/detournavigator/agentbounds.hpp>

#include "apps/openmw/mwbase/environment.hpp"
#include "apps/openmw/mwbase/luamanager.hpp"
#include "apps/openmw/mwbase/mechanicsmanager.hpp"
#include "apps/openmw/mwbase/windowmanager.hpp"
#include "apps/openmw/mwbase/world.hpp"
#include "apps/openmw/mwmechanics/npcstats.hpp"
#include "apps/openmw/mwworld/class.hpp"
#include "apps/openmw/mwworld/esmstore.hpp"
#include "apps/openmw/mwworld/livecellref.hpp"
#include "apps/openmw/mwworld/ptr.hpp"
#include "apps/openmw/mwworld/worldmodel.hpp"

#if defined(__GNUC__) || defined(__clang__)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wunused-parameter"
#endif
#ifdef _MSC_VER
#pragma warning(push)
#pragma warning(disable : 4100)
#endif

namespace CraftingTest
{
    // The generated doubles name MWBase's own types (RotationFlags, ...) without the namespace.
    using namespace MWBase;

    [[noreturn]] inline void unexpectedCall(const char* what)
    {
        throw std::logic_error(std::string("unexpected call of ") + what);
    }

#include "fakes/luamanager.inc"
#include "fakes/mechanicsmanager.inc"
#include "fakes/windowmanager.inc"
#include "fakes/world.inc"
}

namespace CraftingTest
{
    /// The GMSTs the crafting code reads, with the values of OpenMW's default GMST table as far as this author
    /// recalls them; they are test inputs (see the file comment). Some of the code caches its first read in a
    /// function-local static (fWortChanceValue, fMagicItemRechargePerSecond), so these values are the same for every
    /// test of the process and a test must never change them.
    struct GmstValue
    {
        const char* mName;
        float mFloat;
    };

    inline constexpr GmstValue kFloatGmsts[] = {
        { "fPotionStrengthMult", 0.5f },
        { "fPotionT1MagMult", 1.5f },
        { "fPotionT1DurMult", 0.5f },
        { "fWortChanceValue", 15.f },
        { "fRepairAmountMult", 3.f },
        { "fMagicItemRechargePerSecond", 0.05f },
        { "fEffectCostMult", 0.5f },
        { "fEnchantmentConstantDurationMult", 100.f },
        { "fEnchantmentValueMult", 1000.f },
        { "fEnchantmentMult", 0.1f },
        { "fEnchantmentChanceMult", 3.f },
        { "fEnchantmentConstantChanceMult", 0.5f },
        { "fFatigueBase", 1.25f },
        { "fFatigueMult", 0.5f },
        { "fPCbaseMagickaMult", 1.f },
        { "fNPCbaseMagickaMult", 3.f },
        { "fUnarmoredBase1", 0.1f },
        { "fUnarmoredBase2", 0.065f },
    };

    struct IntGmstValue
    {
        const char* mName;
        int mInt;
    };

    inline constexpr IntGmstValue kIntGmsts[] = {
        { "iAlchemyMod", 2 },
        { "iSoulAmountForConstantEffect", 400 },
        { "iAutoRepFacMod", 2 },
        { "iAutoRepLevMod", 0 },
    };

    struct Sound
    {
        std::string mId;
    };

    struct SkillUse
    {
        std::string mSkill;
        int mUseType;
        float mScale;
    };

    class FakeWorld : public GeneratedWorldBase
    {
    public:
        Misc::Rng::Generator mPrng{ 1u };
        MWWorld::Ptr mPlayer;
        MWWorld::ESMStore* mStore = nullptr;
        int mBreakInvisibilityCalls = 0;
        int mRemoveRefScriptCalls = 0;

        MWWorld::ESMStore& getStore() override { return *mStore; }
        const MWWorld::ESMStore& getStore() const override { return *mStore; }

        Misc::Rng::Generator& getPrng() override { return mPrng; }
        void breakInvisibility(const MWWorld::Ptr& actor) override { ++mBreakInvisibilityCalls; }
        void removeRefScript(const MWWorld::CellRef* ref) override { ++mRemoveRefScriptCalls; }
        MWWorld::Ptr getPlayerPtr() override { return mPlayer; }
    };

    class FakeWindowManager : public GeneratedWindowManagerBase
    {
    public:
        std::vector<std::string> mSounds;
        std::vector<std::string> mMessages;
        int mInventoryUpdates = 0;

        void playSound(const ESM::RefId& soundId, float volume, float pitch) override
        {
            mSounds.push_back(soundId.getRefIdString());
        }
        void messageBox(std::string_view message, enum MWGui::ShowInDialogueMode showInDialogueMode) override
        {
            mMessages.emplace_back(message);
        }
        void inventoryUpdated(const MWWorld::Ptr& ptr) const override { const_cast<int&>(mInventoryUpdates)++; }
        /// The words alchemy puts in front of an attribute or skill name ("Fortify Strength").
        std::string_view getGameSettingString(std::string_view id, std::string_view defaultValue) override
        {
            if (id == "sAbsorb")
                return "Absorb";
            if (id == "sDamage")
                return "Damage";
            if (id == "sDrain")
                return "Drain";
            if (id == "sFortify")
                return "Fortify";
            if (id == "sRestore")
                return "Restore";
            return defaultValue;
        }
    };

    class FakeMechanicsManager : public GeneratedMechanicsManagerBase
    {
    public:
        struct BarterCall
        {
            int mBasePrice;
            bool mBuying;
        };
        std::vector<BarterCall> mBarterCalls;
        /// The price returned is the base price times this factor, truncated.
        float mBarterFactor = 1.f;

        int getBarterOffer(const MWWorld::Ptr& ptr, int basePrice, bool buying) override
        {
            mBarterCalls.push_back({ basePrice, buying });
            return static_cast<int>(basePrice * mBarterFactor);
        }
    };

    class FakeLuaManager : public GeneratedLuaManagerBase
    {
    public:
        std::vector<SkillUse> mSkillUses;

        void skillUse(const MWWorld::Ptr& actor, ESM::RefId skillId, int useType, float scale) override
        {
            mSkillUses.push_back({ skillId.getRefIdString(), useType, scale });
        }
    };
}

#ifdef _MSC_VER
#pragma warning(pop)
#endif
#if defined(__GNUC__) || defined(__clang__)
#pragma GCC diagnostic pop
#endif

namespace CraftingTest
{
    /// One effect of an ingredient: an effect id and, for skill or attribute effects, which one.
    struct IngredientEffect
    {
        ESM::RefId mEffect;
        ESM::RefId mSkill;
        ESM::RefId mAttribute;
    };

    /// Real store, real NPC class, test doubles of the managers. Not thread safe (the game's main thread only).
    class CraftingRulesTest : public ::testing::Test
    {
    protected:
        void SetUp() override;
        void TearDown() override;

        void addFloatGmst(const std::string& name, float value);
        void addIntGmst(const std::string& name, int value);
        void addStringGmst(const std::string& name, const std::string& value);

        const ESM::MagicEffect* addEffect(const ESM::RefId& id, float baseCost, int flags);
        ESM::RefId addIngredient(
            const std::string& id, const std::vector<IngredientEffect>& effects, float weight, int value);
        ESM::RefId addApparatus(const std::string& id, int type, float quality);

        /// Adds  count of the item to the player's inventory and returns the first stack with that id.
        MWWorld::Ptr give(const ESM::RefId& id, int count);

        MWMechanics::NpcStats& stats();

        ESM::RefId rid(const std::string& name) const { return ESM::RefId::stringRefId(name); }

        /// The first seed (from 1 up) for which the predicate, run on a fresh generator, is true. The mapping from
        /// a generator's raw output to a roll differs between standard libraries (std::uniform_int_distribution),
        /// so tests choose a seed by the roll they need instead of pinning a seed's outcome.
        template <class Predicate>
        static unsigned findSeed(Predicate predicate)
        {
            for (unsigned seed = 1; seed < 4000000; ++seed)
            {
                Misc::Rng::Generator generator(seed);
                if (predicate(generator))
                    return seed;
            }
            throw std::logic_error("no seed found");
        }

        /// A seed whose first roll0to99 is \a roll.
        static unsigned seedWithRoll(int roll)
        {
            return findSeed([roll](Misc::Rng::Generator& g) { return Misc::Rng::roll0to99(g) == roll; });
        }

        void seedWorld(unsigned seed) { mWorld.mPrng.seed(seed); }

        MWWorld::ESMStore mStore;
        ESM::ReadersCache mReaders;
        std::unique_ptr<MWWorld::WorldModel> mWorldModel;
        FakeWorld mWorld;
        FakeWindowManager mWindow;
        FakeMechanicsManager mMechanics;
        FakeLuaManager mLua;
        std::unique_ptr<MWBase::Environment> mEnvironment;

        ESM::NPC mNpc;
        std::unique_ptr<MWWorld::LiveCellRef<ESM::NPC>> mPlayerRef;
        MWWorld::Ptr mPlayer;
    };
}

#endif
