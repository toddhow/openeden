// Task 0243: who the victim of a crime is, for the bark of a witness: BarkSystem::speakerIsVictim while a line is chosen,
// and CrimeSystem::witnessVictim while the witness hook runs. Fakes only; no game data. Written against stubs first and
// run red (see the stage log of the task).
#include <game_skyrimse/barks.hpp>
#include <game_skyrimse/crime.hpp>

#include <gtest/gtest.h>

#include <cmath>
#include <map>
#include <memory>
#include <string>
#include <utility>
#include <vector>

namespace
{
    using namespace game_skyrimse;
    using openeden::sdk::game::ActorHandle;
    using openeden::sdk::game::ActorSpeed;
    using openeden::sdk::game::CombatRequestWriter;
    using openeden::sdk::game::CombatSubject;
    using openeden::sdk::game::RayHit;
    using openeden::sdk::game::RayRequest;
    using openeden::sdk::game::SubjectState;

    TEST(SkyrimBarkVictim, theFlagIsOnWhileTheLineOfAVictimIsChosenAndOffOtherwise)
    {
        std::vector<std::pair<std::uint64_t, bool>> seen;
        barks::Hooks hooks;
        std::unique_ptr<barks::BarkSystem> system;
        hooks.mChoose = [&](barks::Kind, std::uint64_t reference, std::uint32_t, barks::Line& out) {
            seen.push_back({ reference, system->speakerIsVictim() });
            out.mText = "Hey!";
            out.mVoice = { "voice/a.fuz" };
            return true;
        };
        hooks.mSpeak = [](std::uint64_t, const barks::Line&) { return true; };
        barks::Settings settings;
        settings.mIdleMin = settings.mIdleMax = 1.0e6f;
        system = std::make_unique<barks::BarkSystem>(settings, std::move(hooks));
        const std::vector<barks::Actor> actors{ { 5, 300.0, 0.0, 0.0, false }, { 6, 310.0, 0.0, 0.0, false } };
        system->update(0.1f, actors, true, 0.0, 0.0, 0.0);
        system->onEvent(barks::Kind::Pickpocket, 5, true);
        system->update(0.1f, actors, true, 0.0, 0.0, 0.0);
        system->update(5.0f, actors, true, 0.0, 0.0, 0.0); // the combat gap and the line pass
        system->onEvent(barks::Kind::Pickpocket, 6);
        system->update(0.1f, actors, true, 0.0, 0.0, 0.0);
        ASSERT_GE(seen.size(), 2u);
        for (const auto& [reference, victim] : seen)
        {
            EXPECT_EQ(victim, reference == 5) << reference;
        }
        EXPECT_FALSE(system->speakerIsVictim()) << "off again outside a choice";
    }

    class VictimWorld final : public crime::CrimeWorld
    {
    public:
        void actors(std::vector<crime::CrimeActor>& out) override
        {
            for (const auto& [ref, a] : mActors)
                out.push_back(a);
        }
        bool actor(ActorHandle handle, crime::CrimeActor& out) override
        {
            const auto it = mActors.find(handle.reference);
            if (it == mActors.end())
                return false;
            out = it->second;
            return true;
        }
        const CrimeData* crimeData(FormId) override { return &mData; }
        std::string factionName(FormId) override { return "Hold"; }
        std::int32_t itemValue(FormId) override { return 100; }
        std::int32_t playerGold() override { return 0; }
        bool takeGold(std::int32_t) override { return false; }
        std::int32_t takeItem(FormId, std::int32_t) override { return 0; }
        std::int32_t itemCount(FormId) override { return 0; }
        void guardGoTo(ActorHandle, double, double, double, ActorSpeed) override {}
        void guardHold(ActorHandle) override {}
        void guardRelease(ActorHandle) override {}
        void guardAttack(ActorHandle) override {}
        void log(const std::string&) override {}
        std::map<std::uint32_t, crime::CrimeActor> mActors;
        CrimeData mData;
    };

    class VictimWriter final : public CombatRequestWriter
    {
    public:
        bool subjectState(CombatSubject subject, SubjectState& out) override
        {
            out = {};
            if (subject.player)
                return true;
            const auto it = mStates.find(subject.actor.reference);
            if (it == mStates.end())
                return false;
            out = it->second;
            return true;
        }
        bool castRay(CombatSubject, const RayRequest&, RayHit& out) override
        {
            out = {};
            return true;
        }
        std::map<std::uint32_t, SubjectState> mStates;
    };

    TEST(SkyrimBarkVictim, aCaughtPickpocketNamesItsVictimToTheWitnessHook)
    {
        crime::Ledger ledger;
        VictimWorld world;
        world.mData.mPresent = true;
        world.mData.mPickpocket = 25;
        VictimWriter writer;
        for (const std::uint32_t reference : { 10u, 11u })
        {
            crime::CrimeActor a;
            a.mHandle = { reference, 1 };
            a.mBase = reference;
            a.mCrimeFaction = 0x01000100;
            a.mName = "npc" + std::to_string(reference);
            world.mActors[reference] = a;
            SubjectState s;
            s.x = 300.0 + 100.0 * static_cast<double>(reference - 10);
            writer.mStates[reference] = s;
        }
        crime::CrimeSystem system(ledger, world);
        std::vector<std::pair<std::uint32_t, std::uint32_t>> calls; // witness, victim as told during the hook
        system.setWitnessHook([&](crime::Kind, ActorHandle witness) {
            calls.push_back({ witness.reference, system.witnessVictim().reference });
        });
        system.onPickpocketCaught(11);
        for (int i = 0; i < 4; ++i)
            system.update(0.5f, writer);
        ASSERT_FALSE(calls.empty());
        for (const auto& [witness, victim] : calls)
        {
            EXPECT_EQ(victim, 11u) << witness;
        }
        EXPECT_EQ(system.witnessVictim().reference, 0u) << "outside a hook call there is none";
    }
}
