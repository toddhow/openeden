// Task 0260: see game_skyrimse/vampirism.hpp.
#include "game_skyrimse/vampirism.hpp"

#include <algorithm>
#include <cmath>
#include <cstring>

namespace game_skyrimse::vampirism
{
    namespace
    {
        constexpr double kSecondsPerDay = 86400.0;
        constexpr std::size_t kChunkBytes = 1 + 2 * sizeof(double);
    }

    bool isSunDamageSpell(FormId spell)
    {
        return std::find(kSunDamageSpells.begin(), kSunDamageSpells.end(), spell) != kSunDamageSpells.end();
    }

    // ---- Machine
    // -----------------------------------------------------------------------------------------------------

    void Machine::observeInfection(bool infected, double days)
    {
        if (mVampire || !infected)
        {
            if (!mVampire)
                mInfectedAt = -1.0;
            return;
        }
        if (mInfectedAt < 0.0 && std::isfinite(days))
            mInfectedAt = std::max(days, 0.0);
    }

    Machine::Action Machine::step(double days)
    {
        if (!std::isfinite(days))
            return Action::None;
        if (!mVampire)
            return mInfectedAt >= 0.0 && days >= mInfectedAt + kChangeDays ? Action::Change : Action::None;
        if (mNextTick >= 0.0 && days >= mNextTick)
        {
            mNextTick = days + kTickDays;
            return Action::Tick;
        }
        return Action::None;
    }

    void Machine::becameVampire(double days)
    {
        mVampire = true;
        mInfectedAt = -1.0;
        mNextTick = days + kTickDays;
    }

    void Machine::cured()
    {
        mVampire = false;
        mInfectedAt = -1.0;
        mNextTick = -1.0;
    }

    void Machine::restartTicks(double days)
    {
        if (mVampire)
            mNextTick = days + kTickDays;
    }

    void Machine::restore(bool vampire, double infectedAt, double nextTick)
    {
        mVampire = vampire;
        mInfectedAt = std::isfinite(infectedAt) ? infectedAt : -1.0;
        mNextTick = std::isfinite(nextTick) ? nextTick : -1.0;
    }

    // ---- the sun
    // -----------------------------------------------------------------------------------------------------

    float hourOfClock(double gameSeconds)
    {
        if (!std::isfinite(gameSeconds))
            return 0.f;
        double seconds = std::fmod(gameSeconds, kSecondsPerDay);
        if (seconds < 0.0)
            seconds += kSecondsPerDay;
        return static_cast<float>(seconds / 3600.0);
    }

    bool sunlit(const PerkLibrary& library, FormId sunSpell, double gameSeconds, const effectconditions::Place& place,
        const effectconditions::Evaluator::GlobalLookup& otherGlobals)
    {
        const SpellRecord* spell = library.spell(sunSpell);
        if (spell == nullptr || spell->mEffects.empty())
            return false;
        effectconditions::Evaluator evaluator;
        evaluator.setExtended(true);
        evaluator.setPlace(place);
        const float hour = hourOfClock(gameSeconds);
        evaluator.setGlobalLookup([hour, &otherGlobals](FormId global, float& value) {
            if (global == kGameHourGlobal)
            {
                value = hour;
                return true;
            }
            return otherGlobals && otherGlobals(global, value);
        });
        effectconditions::Participant nobody;
        for (const SpellEffect& effect : spell->mEffects)
        {
            if (effect.mConditions == 0)
                continue;
            if (effect.mConditionList.size() != effect.mConditions)
                return false; // a list the pack could not decode fails closed
            if (evaluator.evaluate(effect.mConditionList, &nobody, &nobody, &nobody) != effectconditions::Verdict::Pass)
                return false;
        }
        return true;
    }

    std::vector<FormId> filterAbilities(std::span<const FormId> spells, bool vampire, bool sun)
    {
        std::vector<FormId> out;
        out.reserve(spells.size() + 1);
        for (const FormId id : spells)
            if (sun || !isSunDamageSpell(id))
                out.push_back(id);
        if (vampire)
            out.push_back(kVampirismAbility);
        std::sort(out.begin(), out.end());
        out.erase(std::unique(out.begin(), out.end()), out.end());
        return out;
    }

    // ---- the chunk
    // ---------------------------------------------------------------------------------------------------

    namespace
    {
        class VampirismChunk final : public openeden::sdk::SaveChunkHandler
        {
        public:
            explicit VampirismChunk(VampirismService& service)
                : mService(service)
            {
            }
            std::string_view chunkName() const override { return "vampirism"; }
            std::uint32_t chunkVersion() const override { return 1; }
            void write(openeden::sdk::ChunkWriter& writer) override
            {
                const Machine& m = mService.machine();
                const std::uint8_t vampire = m.vampire() ? 1 : 0;
                writer.writeValue(vampire);
                writer.writeValue(m.infectedAt());
                writer.writeValue(m.nextTick());
            }
            bool read(openeden::sdk::ChunkReader& reader, std::uint32_t version) override
            {
                std::uint8_t vampire = 0;
                double infectedAt = 0.0, nextTick = 0.0;
                if (version != 1 || reader.remaining() != kChunkBytes || !reader.readValue(vampire) || vampire > 1
                    || !reader.readValue(infectedAt) || !reader.readValue(nextTick) || !std::isfinite(infectedAt)
                    || !std::isfinite(nextTick))
                    return false;
                mService.restore(vampire == 1, infectedAt, nextTick);
                return true;
            }

        private:
            VampirismService& mService;
        };
    }

    struct VampirismService::Impl
    {
        std::unique_ptr<VampirismChunk> mChunk;
    };

    // ---- the service
    // -------------------------------------------------------------------------------------------------

    VampirismService::VampirismService(ScriptPort& port, Hooks hooks)
        : mImpl(std::make_unique<Impl>())
        , mPort(port)
        , mHooks(std::move(hooks))
    {
        mImpl->mChunk = std::make_unique<VampirismChunk>(*this);
    }

    VampirismService::~VampirismService() = default;

    void VampirismService::log(const std::string& line) const
    {
        if (mHooks.mLog)
            mHooks.mLog(line);
    }

    openeden::sdk::SaveChunkHandler& VampirismService::chunk()
    {
        return *mImpl->mChunk;
    }

    void VampirismService::restore(bool vampire, double infectedAt, double nextTick)
    {
        mMachine.restore(vampire, infectedAt, nextTick);
        mChanging = false;
        ++mRevision;
    }

    void VampirismService::startAs(int stage)
    {
        mStartStage = std::clamp(stage, 0, kMaxStage);
        mStartStep = 0;
    }

    bool VampirismService::feed()
    {
        if (!mMachine.vampire() || !mPort.ready())
            return false;
        const double days = mHooks.mDays ? mHooks.mDays() : 0.0;
        if (!mPort.feed())
            return false;
        mMachine.restartTicks(days);
        ++mRevision;
        log("VAMPIRE fed: the stage goes back to 1");
        return true;
    }

    void VampirismService::poll()
    {
        if (!mPort.ready())
            return;
        const double days = mHooks.mDays ? mHooks.mDays() : 0.0;
        mPort.setDays(days);

        // The script's own variable is the truth about the stage (VampireChange sets it after its waits).
        const int status = std::clamp(mPort.status(), 0, kMaxStage);
        if (status > 0 && !mMachine.vampire())
        {
            mMachine.becameVampire(days);
            mChanging = false;
            if (mHooks.mCureSanguinare)
                mHooks.mCureSanguinare();
            ++mRevision;
            log("VAMPIRE the player is a vampire (stage " + std::to_string(status) + ")");
        }
        else if (status == 0 && mStage > 0 && mMachine.vampire() && !mChanging)
        {
            mMachine.cured();
            ++mRevision;
            log("VAMPIRE cured");
        }
        if (status != mStage)
        {
            if (status > 0 && mStage > 0)
                log("VAMPIRE stage " + std::to_string(mStage) + " to " + std::to_string(status));
            mStage = status;
            ++mRevision;
        }

        // A test aid: VampireChange, then the stage's spells and variables once the turn has finished.
        if (mStartStage > 0)
        {
            if (mStartStep == 0 && !mChanging && !mMachine.vampire())
            {
                if (mPort.change())
                {
                    mChanging = true;
                    mStartStep = 1;
                }
            }
            else if (mStartStep == 1 && mMachine.vampire())
                mStartStep = 2;
            if (mStartStep == 2)
            {
                if (mStartStage > 1)
                {
                    (void)mPort.setStage(mStartStage, days - static_cast<double>(mStartStage - 1));
                    (void)mPort.progress(mStartStage);
                }
                log("VAMPIRE test start at stage " + std::to_string(mStartStage));
                mStartStage = 0;
            }
            return;
        }

        if (mCureAt >= 0.0 && mMachine.vampire() && days >= mCureAt)
        {
            mCureAt = -1.0;
            (void)cure();
        }

        const bool safe = !mHooks.mSafe || mHooks.mSafe();
        mMachine.observeInfection(mHooks.mSanguinare && mHooks.mSanguinare() && !mMachine.vampire(), days);
        if (!safe)
            return;
        switch (mMachine.step(days))
        {
            case Machine::Action::Change:
                if (!mChanging && mPort.change())
                {
                    mChanging = true;
                    log("VAMPIRE Sanguinare Vampiris has run its three days: the turn begins");
                }
                break;
            case Machine::Action::Tick:
                (void)mPort.tick();
                break;
            case Machine::Action::None:
                break;
        }
    }

    // ---- task 0279

    namespace
    {
        struct RacePair
        {
            FormId mHuman, mVampire;
        };
        // VERIFIED: the RACE editor ids of Skyrim.esm (ArgonianRace ... WoodElfRace and the *RaceVampire of the quest
        // script's own properties).
        constexpr RacePair kRaces[] = { { 0x13740, 0x8883A }, { 0x13741, 0x8883C }, { 0x13742, 0x8883D },
            { 0x13743, 0x88840 }, { 0x13744, 0x88844 }, { 0x13745, 0x88845 }, { 0x13746, 0x88794 },
            { 0x13747, 0xA82B9 }, { 0x13748, 0x88846 }, { 0x13749, 0x88884 } };
    }

    FormId vampireRaceOf(FormId race)
    {
        for (const RacePair& pair : kRaces)
            if (pair.mHuman == race)
                return pair.mVampire;
        return race;
    }

    FormId humanRaceOf(FormId race)
    {
        for (const RacePair& pair : kRaces)
            if (pair.mVampire == race)
                return pair.mHuman;
        return race;
    }

    bool VampirismService::cure()
    {
        if (!mMachine.vampire() || !mPort.ready() || !mPort.cure())
            return false;
        ++mRevision;
        log("VAMPIRE cure: VampireCure runs");
        return true;
    }

    void VampirismService::scheduleCure(double days)
    {
        mCureAt = std::isfinite(days) ? days : -1.0;
    }
}
