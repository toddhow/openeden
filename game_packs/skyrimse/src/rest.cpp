#include "game_skyrimse/rest.hpp"

#include <algorithm>
#include <cmath>
#include <cstring>

namespace game_skyrimse::rest
{
    std::string_view refusalName(Refusal refusal)
    {
        switch (refusal)
        {
            case Refusal::None:
                return "";
            case Refusal::NotReady:
                return "not_ready";
            case Refusal::InCombat:
                return "combat";
            case Refusal::EnemiesNear:
                return "enemies";
        }
        return "";
    }

    Refusal decideRest(bool ready, bool inCombat, bool enemiesNear)
    {
        if (!ready)
            return Refusal::NotReady;
        if (inCombat)
            return Refusal::InCombat;
        if (enemiesNear)
            return Refusal::EnemiesNear;
        return Refusal::None;
    }

    int clampHours(double hours)
    {
        if (!std::isfinite(hours))
            return kMinWaitHours;
        return static_cast<int>(std::clamp(std::floor(hours), static_cast<double>(kMinWaitHours),
            static_cast<double>(kMaxWaitHours)));
    }

    double regenSeconds(double hours, double timeScale)
    {
        if (!std::isfinite(hours) || hours <= 0.0)
            return 0.0;
        const double scale = (std::isfinite(timeScale) && timeScale > 0.0) ? timeScale : 1.0;
        return std::clamp(hours * 3600.0 / scale, 0.0, kMaxRegenSeconds);
    }

    std::string_view bedStateName(BedState state)
    {
        switch (state)
        {
            case BedState::NotABed:
                return "none";
            case BedState::Free:
                return "free";
            case BedState::Owned:
                return "owned";
        }
        return "none";
    }

    bool hasSleepMarker(const std::vector<FurnitureMarker>& markers)
    {
        return std::any_of(
            markers.begin(), markers.end(), [](const FurnitureMarker& m) { return m.mType == MarkerType::Sleep; });
    }

    BedState bedStateOf(bool isBed, bool owned)
    {
        if (!isBed)
            return BedState::NotABed;
        return owned ? BedState::Owned : BedState::Free;
    }

    // ---- Rested ----------------------------------------------------------------------------------------------------

    bool RestedState::grant(double now, int sleptHours)
    {
        if (!std::isfinite(now) || sleptHours < kRestedMinSleepHours)
            return false;
        mExpiresAt = now + kRestedSeconds;
        ++mRevision;
        return true;
    }

    bool RestedState::active(double now) const
    {
        return std::isfinite(now) && mExpiresAt >= 0.0 && now < mExpiresAt;
    }

    double RestedState::hoursLeft(double now) const
    {
        return active(now) ? (mExpiresAt - now) / 3600.0 : 0.0;
    }

    float RestedState::skillGain(double now) const
    {
        return active(now) ? kRestedSkillGain : 1.f;
    }

    // ---- the chunk -------------------------------------------------------------------------------------------------

    void RestChunk::write(openeden::sdk::ChunkWriter& writer)
    {
        writer.writeValue(mState.expiresAt());
    }

    bool RestChunk::read(openeden::sdk::ChunkReader& reader, std::uint32_t version)
    {
        std::string error;
        double expires = -1.0;
        if (version != kVersion)
            error = "the rest chunk has version " + std::to_string(version);
        else if (reader.remaining() != sizeof(double) || !reader.readValue(expires))
            error = "the rest chunk's size is not one number";
        else if (!std::isfinite(expires))
            error = "the rest chunk's expiry is not a finite number";
        if (!error.empty())
        {
            if (mSink)
                mSink(error);
            return false;
        }
        mState.restore(expires);
        if (mRestored)
            mRestored("restored: " + (expires >= 0.0 ? std::string("Rested") : std::string("not rested")));
        return true;
    }

    // ---- the service -----------------------------------------------------------------------------------------------

    namespace
    {
        using openeden::sdk::BindingResult;
        using openeden::sdk::BindingShape;
        using openeden::sdk::BindingType;
        using openeden::sdk::BindingValue;

        class Fn final : public openeden::sdk::BindingHandler
        {
        public:
            using Body = std::function<BindingResult(std::span<const BindingValue>)>;
            explicit Fn(Body body)
                : mBody(std::move(body))
            {
            }
            BindingResult call(std::span<const BindingValue> args) override { return mBody(args); }

        private:
            Body mBody;
        };

        BindingValue boolean(bool v)
        {
            BindingValue b;
            b.type = BindingType::Bool;
            b.boolean = v;
            return b;
        }
        BindingValue integer(std::int32_t v)
        {
            BindingValue b;
            b.type = BindingType::Int32;
            b.integer = v;
            return b;
        }
        BindingValue number(double v)
        {
            BindingValue b;
            b.type = BindingType::Double;
            b.number = v;
            return b;
        }
        BindingValue text(std::string_view v)
        {
            BindingValue b;
            b.type = BindingType::String;
            b.text = v;
            return b;
        }

        /// Lower case hex of at most 16 digits to a number; false for anything else (an empty text, a stray character).
        bool parseHex(std::string_view s, std::uint64_t& out)
        {
            if (s.empty() || s.size() > 16)
                return false;
            std::uint64_t v = 0;
            for (const char c : s)
            {
                int d = -1;
                if (c >= '0' && c <= '9')
                    d = c - '0';
                else if (c >= 'a' && c <= 'f')
                    d = c - 'a' + 10;
                else if (c >= 'A' && c <= 'F')
                    d = c - 'A' + 10;
                if (d < 0)
                    return false;
                v = (v << 4) | static_cast<std::uint64_t>(d);
            }
            out = v;
            return true;
        }
    }

    struct RestService::Impl
    {
        std::vector<std::unique_ptr<Fn>> mHandlers;
        std::vector<openeden::sdk::FunctionDescriptor> mFunctions;
        std::string mText; ///< the last string answer (a binding result points at it)
    };

    RestService::RestService(Hooks hooks)
        : mImpl(std::make_unique<Impl>())
        , mHooks(std::move(hooks))
    {
    }

    RestService::~RestService() = default;

    Refusal RestService::refusal() const
    {
        const bool ready = mHooks.mReady && mHooks.mReady();
        return decideRest(ready, ready && mHooks.mInCombat && mHooks.mInCombat(),
            ready && mHooks.mEnemiesNear && mHooks.mEnemiesNear());
    }

    void RestService::setClock(double gameSeconds)
    {
        if (std::isfinite(gameSeconds))
            mNow = gameSeconds;
    }

    bool RestService::perform(int hours, double regenSecondsAsked, bool sleeping)
    {
        const Refusal why = refusal();
        if (why != Refusal::None)
        {
            ++mRefused;
            if (mHooks.mLog)
                mHooks.mLog("REST refused: " + std::string(refusalName(why)));
            return false;
        }
        const int h = clampHours(static_cast<double>(hours));
        const double seconds
            = std::isfinite(regenSecondsAsked) ? std::clamp(regenSecondsAsked, 0.0, kMaxRegenSeconds) : 0.0;
        if (mHooks.mApply)
            mHooks.mApply(seconds, h, sleeping);
        bool granted = false;
        if (sleeping)
            granted = mRested.grant(mNow, h);
        ++mPerformed;
        if (mHooks.mLog)
            mHooks.mLog("REST " + std::string(sleeping ? "slept " : "waited ") + std::to_string(h)
                + " hours, regenerating " + std::to_string(static_cast<int>(seconds)) + " s"
                + (granted ? ", Rested" : ""));
        return true;
    }

    openeden::sdk::DeclareResult RestService::declare(openeden::sdk::BindingsService& service)
    {
        Impl& m = *mImpl;
        RestService* self = this;
        static const BindingType placementBase[] = { BindingType::String, BindingType::String };
        static const BindingType clockParam[] = { BindingType::Double };
        static const BindingType restParams[] = { BindingType::Int32, BindingType::Double, BindingType::Bool };

        const auto add = [&](std::string_view name, std::span<const BindingType> params, bool hasResult,
                             BindingType result, Fn::Body body,
                             openeden::sdk::BindingAccess access = openeden::sdk::BindingAccess::Read) {
            m.mHandlers.push_back(std::make_unique<Fn>(std::move(body)));
            openeden::sdk::FunctionDescriptor f;
            f.name = name;
            f.params = params;
            f.hasResult = hasResult;
            f.result = result;
            f.access = access;
            f.handler = m.mHandlers.back().get();
            m.mFunctions.push_back(f);
        };

        add("ready", {}, true, BindingType::Bool, [self](std::span<const BindingValue>) {
            BindingResult r;
            r.value = boolean(self->mHooks.mReady && self->mHooks.mReady());
            return r;
        });
        add("max_hours", {}, true, BindingType::Int32, [](std::span<const BindingValue>) {
            BindingResult r;
            r.value = integer(kMaxWaitHours);
            return r;
        });
        add("can_rest", {}, true, BindingType::String, [self, &m](std::span<const BindingValue>) {
            m.mText = std::string(refusalName(self->refusal()));
            BindingResult r;
            r.value = text(m.mText);
            return r;
        });
        add("bed_state", placementBase, true, BindingType::String, [self, &m](std::span<const BindingValue> a) {
            std::uint64_t placement = 0, base = 0;
            BedState state = BedState::NotABed;
            if (self->mHooks.mBed && parseHex(a[0].text, placement) && parseHex(a[1].text, base))
                state = self->mHooks.mBed(placement, base);
            m.mText = std::string(bedStateName(state));
            BindingResult r;
            r.value = text(m.mText);
            return r;
        });
        add("rested", {}, true, BindingType::Bool, [self](std::span<const BindingValue>) {
            BindingResult r;
            r.value = boolean(self->mRested.active(self->mNow));
            return r;
        });
        add("rested_hours_left", {}, true, BindingType::Double, [self](std::span<const BindingValue>) {
            BindingResult r;
            r.value = number(self->mRested.hoursLeft(self->mNow));
            return r;
        });
        add("set_clock", clockParam, false, BindingType::Bool,
            [self](std::span<const BindingValue> a) {
                self->setClock(a[0].number);
                return BindingResult();
            },
            openeden::sdk::BindingAccess::Mutate);
        add("rest", restParams, false, BindingType::Bool,
            [self](std::span<const BindingValue> a) {
                (void)self->perform(a[0].integer, a[1].number, a[2].boolean);
                return BindingResult();
            },
            openeden::sdk::BindingAccess::Mutate);

        openeden::sdk::NamespaceDescriptor ns;
        ns.name = "skyrimse.rest";
        ns.version = { 1, 0 };
        ns.functions = m.mFunctions;
        return service.declareNamespace(ns);
    }
}
