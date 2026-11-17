#include "game_skyrimse/services.hpp"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <span>

namespace game_skyrimse::services
{
    // ---- training --------------------------------------------------------------------------------------------------

    bool makeTrainer(bool hasData, std::uint8_t trainedSkill, std::uint8_t maxLevel, TrainerInfo& out)
    {
        if (!hasData || trainedSkill >= kSkillSlots || maxLevel == 0)
            return false;
        out.mSlot = trainedSkill;
        out.mMaxLevel = maxLevel;
        return true;
    }

    std::int32_t trainingCost(int skillLevel, float mult)
    {
        if (skillLevel < 0 || !std::isfinite(mult) || mult <= 0.f)
            return 0;
        const double cost = std::round(static_cast<double>(skillLevel) * static_cast<double>(mult));
        return cost >= 2147483647.0 ? 2147483647 : static_cast<std::int32_t>(cost);
    }

    std::string_view trainStatusName(TrainStatus status)
    {
        switch (status)
        {
            case TrainStatus::Ok:
                return "";
            case TrainStatus::NotReady:
                return "not_ready";
            case TrainStatus::SkillTooHigh:
                return "skill";
            case TrainStatus::LimitReached:
                return "limit";
            case TrainStatus::NotEnoughGold:
                return "gold";
        }
        return "";
    }

    TrainStatus decideTraining(
        int skillLevel, int maxLevel, int trainedThisLevel, int perLevel, std::int64_t gold, std::int32_t cost)
    {
        if (skillLevel < 0)
            return TrainStatus::NotReady;
        if (skillLevel >= maxLevel)
            return TrainStatus::SkillTooHigh;
        if (trainedThisLevel >= perLevel)
            return TrainStatus::LimitReached;
        if (gold < static_cast<std::int64_t>(cost))
            return TrainStatus::NotEnoughGold;
        return TrainStatus::Ok;
    }

    void TrainingLedger::record(std::uint16_t playerLevel)
    {
        if (playerLevel != mLevel)
        {
            mLevel = playerLevel;
            mCount = 0;
        }
        ++mCount;
    }

    void TrainingLedger::restore(std::uint16_t level, int count)
    {
        mLevel = level;
        mCount = std::max(0, count);
    }

    // ---- rooms -----------------------------------------------------------------------------------------------------

    std::string_view rentStatusName(RentStatus status)
    {
        switch (status)
        {
            case RentStatus::Ok:
                return "";
            case RentStatus::AlreadyRented:
                return "rented";
            case RentStatus::NotEnoughGold:
                return "gold";
        }
        return "";
    }

    RentStatus decideRent(bool alreadyRented, std::int64_t gold, std::int32_t cost)
    {
        if (alreadyRented)
            return RentStatus::AlreadyRented;
        if (gold < static_cast<std::int64_t>(cost))
            return RentStatus::NotEnoughGold;
        return RentStatus::Ok;
    }

    bool RentalBook::rent(FormId bed, double now, double hours)
    {
        if (bed == kNullFormId || !std::isfinite(now) || !std::isfinite(hours) || hours <= 0.0)
            return false;
        mRentals.erase(
            std::remove_if(mRentals.begin(), mRentals.end(), [now](const Rental& r) { return r.mExpires <= now; }),
            mRentals.end());
        const double expires = now + hours * 3600.0;
        const auto it = std::lower_bound(
            mRentals.begin(), mRentals.end(), bed, [](const Rental& r, FormId id) { return r.mBed < id; });
        if (it != mRentals.end() && it->mBed == bed)
            it->mExpires = expires;
        else
        {
            if (mRentals.size() >= kMaxRentals)
                return false;
            mRentals.insert(it, { bed, expires });
        }
        ++mRevision;
        return true;
    }

    bool RentalBook::active(FormId bed, double now) const
    {
        return hoursLeft(bed, now) > 0.0;
    }

    double RentalBook::hoursLeft(FormId bed, double now) const
    {
        const auto it = std::lower_bound(
            mRentals.begin(), mRentals.end(), bed, [](const Rental& r, FormId id) { return r.mBed < id; });
        if (it == mRentals.end() || it->mBed != bed || !(now < it->mExpires))
            return 0.0;
        return (it->mExpires - now) / 3600.0;
    }

    bool RentalBook::restore(std::vector<Rental> rentals)
    {
        if (rentals.size() > kMaxRentals)
            return false;
        std::sort(rentals.begin(), rentals.end(), [](const Rental& a, const Rental& b) { return a.mBed < b.mBed; });
        for (std::size_t i = 0; i < rentals.size(); ++i)
        {
            if (rentals[i].mBed == kNullFormId || !std::isfinite(rentals[i].mExpires)
                || (i > 0 && rentals[i - 1].mBed == rentals[i].mBed))
                return false;
        }
        mRentals = std::move(rentals);
        ++mRevision;
        return true;
    }

    // ---- the chunk -------------------------------------------------------------------------------------------------

    void ServicesChunk::write(openeden::sdk::ChunkWriter& writer)
    {
        writer.writeValue(static_cast<std::uint32_t>(mLedger.level()));
        writer.writeValue(static_cast<std::uint32_t>(mLedger.count()));
        const auto& entries = mBook.entries();
        writer.writeValue(static_cast<std::uint32_t>(entries.size()));
        for (const Rental& r : entries)
        {
            writer.writeValue(static_cast<std::uint32_t>(r.mBed));
            writer.writeValue(r.mExpires);
        }
    }

    bool ServicesChunk::read(openeden::sdk::ChunkReader& reader, std::uint32_t version)
    {
        std::string error;
        std::uint32_t level = 0, count = 0, rentals = 0;
        std::vector<Rental> list;
        if (version != kVersion)
            error = "the services chunk has version " + std::to_string(version);
        else if (reader.remaining() < 12 || !reader.readValue(level) || !reader.readValue(count)
            || !reader.readValue(rentals))
            error = "the services chunk is too short";
        else if (level > 0xFFFF || count > 1000000)
            error = "the services chunk's training ledger is out of range";
        else if (rentals > kMaxRentals)
            error = "the services chunk holds more rentals than the limit";
        else if (reader.remaining() != static_cast<std::size_t>(rentals) * 12)
            error = "the services chunk's size does not match its rental count";
        else
        {
            for (std::uint32_t i = 0; i < rentals && error.empty(); ++i)
            {
                std::uint32_t bed = 0;
                double expires = 0.0;
                if (!reader.readValue(bed) || !reader.readValue(expires))
                    error = "the services chunk ends inside a rental";
                else
                    list.push_back({ static_cast<FormId>(bed), expires });
            }
            if (error.empty())
            {
                RentalBook probe;
                if (!probe.restore(list))
                    error = "the services chunk holds a bad rental";
            }
        }
        if (!error.empty())
        {
            if (mSink)
                mSink(error);
            return false;
        }
        mLedger.restore(static_cast<std::uint16_t>(level), static_cast<int>(count));
        (void)mBook.restore(std::move(list));
        if (mRestored)
            mRestored("restored: ledger level " + std::to_string(level) + " count " + std::to_string(count) + ", "
                + std::to_string(rentals) + " rentals");
        return true;
    }

    // ---- the service -----------------------------------------------------------------------------------------------

    namespace
    {
        using openeden::sdk::BindingResult;
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
        BindingValue integer(std::int64_t v)
        {
            BindingValue b;
            b.type = BindingType::Int32;
            b.integer = static_cast<std::int32_t>(std::clamp<std::int64_t>(v, INT32_MIN, INT32_MAX));
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

        std::string hex8(std::uint64_t v)
        {
            char buffer[24];
            std::snprintf(buffer, sizeof(buffer), "%08llx", static_cast<unsigned long long>(v));
            return buffer;
        }
    }

    struct ServicesService::Impl
    {
        std::vector<std::unique_ptr<Fn>> mHandlers;
        std::vector<openeden::sdk::FunctionDescriptor> mFunctions;
        std::string mText; ///< the last string answer (a binding result points at it)
    };

    ServicesService::ServicesService(Hooks hooks, ServicesSettings settings)
        : mImpl(std::make_unique<Impl>())
        , mHooks(std::move(hooks))
        , mSettings(settings)
    {
    }

    ServicesService::~ServicesService() = default;

    void ServicesService::log(const std::string& line) const
    {
        if (mHooks.mLog)
            mHooks.mLog(line);
    }

    void ServicesService::result(std::string text)
    {
        mLastResult = std::move(text);
        ++mResults;
        ++mRevision;
        log("SERVICES " + mLastResult);
    }

    void ServicesService::refuse(std::string reason)
    {
        mState = State::Refused;
        mReason = std::move(reason);
        ++mRevision;
        log("SERVICES refused " + mWanted + " with " + mName + ": " + mReason);
    }

    void ServicesService::open(std::string_view service, std::uint64_t placement, FormId npcBase, std::string name)
    {
        mWanted = std::string(service);
        mName = std::move(name);
        mPlacement = placement;
        mNpcBase = npcBase;
        mReason.clear();
        mLastResult.clear();
        mPolls = 0;
        mTrainer = TrainerInfo();
        mOffer = RentOffer();
        ++mRevision;
        if (mWanted != "training" && mWanted != "rent" && mWanted != "gift")
        {
            refuse("unknown service '" + mWanted + "'");
            return;
        }
        mState = State::Pending;
        (void)tryStart();
    }

    bool ServicesService::tryStart()
    {
        if (mWanted == "training")
        {
            const Lookup found = mHooks.mTrainerOf ? mHooks.mTrainerOf(mNpcBase, mTrainer) : Lookup::None;
            if (found == Lookup::Pending)
                return false;
            if (found == Lookup::None)
            {
                refuse("this is not a trainer");
                return true;
            }
            mState = State::Training;
        }
        else if (mWanted == "rent")
        {
            const Lookup found = mHooks.mRentOf ? mHooks.mRentOf(mNpcBase, mOffer) : Lookup::None;
            if (found == Lookup::Pending)
                return false;
            if (found == Lookup::None || mOffer.mBed == kNullFormId)
            {
                refuse("this person has no room to rent");
                return true;
            }
            mState = State::Rent;
        }
        else
        {
            if (!mHooks.mOpenOwner || !mHooks.mOpenOwner(mPlacement, mNpcBase))
            {
                refuse("the inventory of this person could not be opened");
                return true;
            }
            mState = State::Gift;
        }
        ++mRevision;
        log("SERVICES opened " + mWanted + " with " + mName);
        return true;
    }

    void ServicesService::poll()
    {
        if (mState != State::Pending)
            return;
        if (tryStart())
            return;
        if (++mPolls > kMaxPendingPolls)
            refuse("the records were not ready");
    }

    void ServicesService::close()
    {
        if (mState == State::None)
            return;
        mState = State::None;
        mWanted.clear();
        mReason.clear();
        ++mRevision;
        log("SERVICES closed");
    }

    void ServicesService::setClock(double gameSeconds)
    {
        if (std::isfinite(gameSeconds))
            mNow = gameSeconds;
    }

    int ServicesService::skillLevel() const
    {
        return mState == State::Training && mHooks.mSkillLevel ? mHooks.mSkillLevel(mTrainer.mSlot) : -1;
    }

    std::int32_t ServicesService::trainingCostNow() const
    {
        return trainingCost(skillLevel(), mSettings.mTrainingCostMult);
    }

    int ServicesService::trainsLeft() const
    {
        const std::uint16_t level = mHooks.mPlayerLevel ? mHooks.mPlayerLevel() : 0;
        return std::max(0, mSettings.mTrainsPerLevel - mLedger.countAt(level));
    }

    TrainStatus ServicesService::trainStatus() const
    {
        const int skill = skillLevel();
        const std::uint16_t level = mHooks.mPlayerLevel ? mHooks.mPlayerLevel() : 0;
        if (level == 0)
            return TrainStatus::NotReady;
        return decideTraining(skill, mTrainer.mMaxLevel, mLedger.countAt(level), mSettings.mTrainsPerLevel,
            mHooks.mGold ? mHooks.mGold() : 0, trainingCostNow());
    }

    bool ServicesService::train()
    {
        if (mState != State::Training)
            return false;
        const TrainStatus status = trainStatus();
        if (status != TrainStatus::Ok)
        {
            result("training refused: " + std::string(trainStatusName(status)));
            return false;
        }
        const std::int32_t cost = trainingCostNow();
        const std::uint16_t level = mHooks.mPlayerLevel();
        const int before = skillLevel();
        // The skill goes up first: if the progression refuses (a skill at the top), nothing is paid.
        if (!mHooks.mRaise || !mHooks.mRaise(mTrainer.mSlot))
        {
            result("training refused: the skill could not be raised");
            return false;
        }
        if (cost > 0 && (!mHooks.mPay || !mHooks.mPay(cost)))
        {
            result("training: the payment failed after the skill rose");
            return false;
        }
        mLedger.record(level);
        const std::string skill = mHooks.mSkillName ? mHooks.mSkillName(mTrainer.mSlot) : std::string("skill");
        result("trained " + skill + " " + std::to_string(before) + " -> " + std::to_string(skillLevel()) + " for "
            + std::to_string(cost) + " gold (" + std::to_string(trainsLeft()) + " left at level "
            + std::to_string(level) + ")");
        return true;
    }

    std::string_view ServicesService::rentState() const
    {
        if (mState != State::Rent)
            return "none";
        if (mRentals.active(mOffer.mBed, mNow))
            return "active";
        const std::int64_t gold = mHooks.mGold ? mHooks.mGold() : 0;
        return decideRent(false, gold, mSettings.mRoomCost) == RentStatus::Ok ? "available" : "no_gold";
    }

    double ServicesService::rentHoursLeft() const
    {
        return mState == State::Rent ? mRentals.hoursLeft(mOffer.mBed, mNow) : 0.0;
    }

    bool ServicesService::rent()
    {
        if (mState != State::Rent)
            return false;
        const std::int64_t gold = mHooks.mGold ? mHooks.mGold() : 0;
        const RentStatus status = decideRent(mRentals.active(mOffer.mBed, mNow), gold, mSettings.mRoomCost);
        if (status != RentStatus::Ok)
        {
            result("rent refused: " + std::string(rentStatusName(status)));
            return false;
        }
        if (mSettings.mRoomCost > 0 && (!mHooks.mPay || !mHooks.mPay(mSettings.mRoomCost)))
        {
            result("rent refused: the payment failed");
            return false;
        }
        if (!mRentals.rent(mOffer.mBed, mNow, mSettings.mRentHours))
        {
            result("rent: the rental book is full");
            return false;
        }
        result("rented bed " + hex8(mOffer.mBed) + " for " + std::to_string(static_cast<int>(mSettings.mRentHours))
            + " hours at " + std::to_string(mSettings.mRoomCost) + " gold");
        return true;
    }

    openeden::sdk::DeclareResult ServicesService::declare(openeden::sdk::BindingsService& service)
    {
        Impl& m = *mImpl;
        ServicesService* self = this;
        static const BindingType hexOne[] = { BindingType::String };
        static const BindingType clockParam[] = { BindingType::Double };
        static const BindingType openParams[]
            = { BindingType::String, BindingType::String, BindingType::String, BindingType::String };

        const auto add
            = [&](std::string_view name, std::span<const BindingType> params, bool hasResult, BindingType result,
                  Fn::Body body, openeden::sdk::BindingAccess access = openeden::sdk::BindingAccess::Read) {
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
        const auto str = [&m](std::string value) {
            m.mText = std::move(value);
            BindingResult r;
            r.value = text(m.mText);
            return r;
        };
        const auto num = [](std::int64_t value) {
            BindingResult r;
            r.value = integer(value);
            return r;
        };

        add("state", {}, true, BindingType::String, [self, str](std::span<const BindingValue>) {
            switch (self->mState)
            {
                case State::None:
                    return str("none");
                case State::Pending:
                    return str("pending");
                case State::Training:
                    return str("training");
                case State::Rent:
                    return str("rent");
                case State::Gift:
                    return str("gift");
                case State::Refused:
                    return str("refused");
            }
            return str("none");
        });
        add("reason", {}, true, BindingType::String,
            [self, str](std::span<const BindingValue>) { return str(self->mReason); });
        add("name", {}, true, BindingType::String,
            [self, str](std::span<const BindingValue>) { return str(self->mName); });
        add("revision", {}, true, BindingType::Int32,
            [self, num](std::span<const BindingValue>) { return num(self->revision()); });
        add("gold", {}, true, BindingType::Int32,
            [self, num](std::span<const BindingValue>) { return num(self->mHooks.mGold ? self->mHooks.mGold() : 0); });
        add("skill_name", {}, true, BindingType::String, [self, str](std::span<const BindingValue>) {
            return str(self->mState == State::Training && self->mHooks.mSkillName
                    ? self->mHooks.mSkillName(self->mTrainer.mSlot)
                    : std::string());
        });
        add("skill_level", {}, true, BindingType::Int32,
            [self, num](std::span<const BindingValue>) { return num(self->skillLevel()); });
        add("max_level", {}, true, BindingType::Int32, [self, num](std::span<const BindingValue>) {
            return num(self->mState == State::Training ? self->mTrainer.mMaxLevel : 0);
        });
        add("cost", {}, true, BindingType::Int32,
            [self, num](std::span<const BindingValue>) { return num(self->trainingCostNow()); });
        add("trains_left", {}, true, BindingType::Int32,
            [self, num](std::span<const BindingValue>) { return num(self->trainsLeft()); });
        add("can_train", {}, true, BindingType::String, [self, str](std::span<const BindingValue>) {
            return str(self->mState == State::Training ? std::string(trainStatusName(self->trainStatus()))
                                                       : std::string("not_ready"));
        });
        add("rent_cost", {}, true, BindingType::Int32,
            [self, num](std::span<const BindingValue>) { return num(self->rentCost()); });
        add("rent_hours", {}, true, BindingType::Double, [self](std::span<const BindingValue>) {
            BindingResult r;
            r.value = number(self->rentHours());
            return r;
        });
        add("rent_state", {}, true, BindingType::String,
            [self, str](std::span<const BindingValue>) { return str(std::string(self->rentState())); });
        add("rent_hours_left", {}, true, BindingType::Double, [self](std::span<const BindingValue>) {
            BindingResult r;
            r.value = number(self->rentHoursLeft());
            return r;
        });
        add("owner", {}, true, BindingType::String, [self, str](std::span<const BindingValue>) {
            return str(self->mState == State::Gift ? hex8(self->mPlacement) : std::string());
        });
        add("last_result", {}, true, BindingType::String,
            [self, str](std::span<const BindingValue>) { return str(self->mLastResult); });
        add("results", {}, true, BindingType::Int32,
            [self, num](std::span<const BindingValue>) { return num(self->mResults); });
        add("rented", hexOne, true, BindingType::Bool, [self](std::span<const BindingValue> a) {
            std::uint64_t bed = 0;
            BindingResult r;
            r.value = boolean(parseHex(a[0].text, bed) && bed <= 0xFFFFFFFFull
                && self->mRentals.active(static_cast<FormId>(bed), self->mNow));
            return r;
        });
        add(
            "open", openParams, false, BindingType::Bool,
            [self](std::span<const BindingValue> a) {
                std::uint64_t placement = 0, base = 0;
                if (!parseHex(a[1].text, placement) || !parseHex(a[2].text, base) || base > 0xFFFFFFFFull)
                {
                    self->log("SERVICES open refused: bad id");
                    return BindingResult();
                }
                self->open(a[0].text, placement, static_cast<FormId>(base), std::string(a[3].text));
                return BindingResult();
            },
            openeden::sdk::BindingAccess::Mutate);
        add(
            "train", {}, false, BindingType::Bool,
            [self](std::span<const BindingValue>) {
                (void)self->train();
                return BindingResult();
            },
            openeden::sdk::BindingAccess::Mutate);
        add(
            "rent", {}, false, BindingType::Bool,
            [self](std::span<const BindingValue>) {
                (void)self->rent();
                return BindingResult();
            },
            openeden::sdk::BindingAccess::Mutate);
        add(
            "close", {}, false, BindingType::Bool,
            [self](std::span<const BindingValue>) {
                self->close();
                return BindingResult();
            },
            openeden::sdk::BindingAccess::Mutate);
        add(
            "set_clock", clockParam, false, BindingType::Bool,
            [self](std::span<const BindingValue> a) {
                self->setClock(a[0].number);
                return BindingResult();
            },
            openeden::sdk::BindingAccess::Mutate);

        openeden::sdk::NamespaceDescriptor ns;
        ns.name = "skyrimse.services";
        ns.version = { 1, 0 };
        ns.functions = m.mFunctions;
        return service.declareNamespace(ns);
    }

    std::string classifyCarry(const std::vector<std::string>& referencedNames)
    {
        for (const std::string& name : referencedNames)
        {
            if (name == "openinventory")
                return "carry";
        }
        return std::string();
    }

    std::string_view openedServiceName(std::string_view classified)
    {
        if (classified == "training")
            return "training";
        if (classified == "rent a room")
            return "rent";
        if (classified == "gift")
            return "gift";
        if (classified == "carry")
            return "carry";
        return {};
    }

    std::string openedServiceTag(std::string_view classified)
    {
        const std::string_view name = openedServiceName(classified);
        return name.empty() ? std::string() : "open:" + std::string(name);
    }
}
