#include "game_skyrimse/crime_service.hpp"

#include "game_skyrimse/inventory_service.hpp"

#include <algorithm>
#include <cctype>
#include <span>

// The crime service and its bindings: see the header (task 0204).

namespace game_skyrimse::crime
{
    using openeden::sdk::BindingAccess;
    using openeden::sdk::BindingResult;
    using openeden::sdk::BindingType;
    using openeden::sdk::BindingValue;

    namespace
    {
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

        BindingResult ok()
        {
            return BindingResult{};
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

        BindingValue boolean(bool v)
        {
            BindingValue b;
            b.type = BindingType::Bool;
            b.boolean = v;
            return b;
        }

        BindingValue text(std::string_view v)
        {
            BindingValue b;
            b.type = BindingType::String;
            b.text = v;
            return b;
        }
    }

    struct CrimeService::Impl
    {
        std::vector<std::unique_ptr<Fn>> mHandlers;
        std::vector<openeden::sdk::FunctionDescriptor> mFunctions;
        std::string mText; // result buffer: valid until the handler is called again
    };

    CrimeService::CrimeService(ServiceHooks hooks, Settings settings)
        : mImpl(std::make_unique<Impl>())
        , mHooks(std::move(hooks))
        , mChunk(mLedger)
        , mSystem(mLedger, *this, settings)
    {
        // Task 0258: the arrest conversation runs for a guard when the pack says it can
        mSystem.setDialogueProbe(
            [this](const CrimeActor& guard) { return mHooks.mDialogueAvailable && mHooks.mDialogueAvailable(guard); });
    }

    CrimeService::~CrimeService() = default;

    std::int32_t CrimeService::stolenHeld(FormId item) const
    {
        const std::int32_t stolen = mLedger.stolen(item);
        if (stolen <= 0 || !mHooks.mItemCount)
            return stolen;
        return std::min(stolen, std::max<std::int32_t>(0, mHooks.mItemCount(item)));
    }

    const JailDestination& CrimeService::jailOf(FormId faction)
    {
        const auto it = mJail.find(faction);
        if (it != mJail.end())
            return it->second;
        JailDestination dest;
        if (faction == kNullFormId || !mHooks.mJail || !mHooks.mJail(faction, dest))
            dest = {};
        if (mJail.size() < 256)
            return mJail.emplace(faction, std::move(dest)).first->second;
        mNoJail = std::move(dest);
        return mNoJail;
    }

    // ---- task 0258: the arrest conversation -----------------------------------------------------------------------

    namespace
    {
        constexpr std::size_t kMaxRoster = 4096;
    }

    FormId CrimeService::crimeFactionOfBase(FormId speakerBase)
    {
        if (speakerBase == kNullFormId)
            return kNullFormId;
        if (mSystem.state() == State::Offer && speakerBase == mSystem.guardBase())
            return mSystem.offerFaction();
        std::vector<CrimeActor> roster;
        actors(roster);
        for (std::size_t i = 0; i < roster.size() && i < kMaxRoster; ++i)
            if (roster[i].mBase == speakerBase && roster[i].mCrimeFaction != kNullFormId)
                return roster[i].mCrimeFaction;
        return kNullFormId;
    }

    FormId CrimeService::crimeFactionOfReference(FormId reference)
    {
        if (reference == kNullFormId)
            return kNullFormId;
        std::vector<CrimeActor> roster;
        actors(roster);
        for (std::size_t i = 0; i < roster.size() && i < kMaxRoster; ++i)
            if (roster[i].mHandle.reference == reference)
                return roster[i].mCrimeFaction;
        return kNullFormId;
    }

    CrimeConditionSource CrimeService::conditionSource()
    {
        CrimeConditionSource source;
        source.mCrimeGold = [this](FormId speakerBase, FormId faction) {
            const FormId f = faction != kNullFormId ? faction : crimeFactionOfBase(speakerBase);
            return f == kNullFormId ? 0.f : static_cast<float>(mLedger.bounty(f));
        };
        source.mCanPay = [this](FormId speakerBase, FormId faction) {
            const FormId f = faction != kNullFormId ? faction : crimeFactionOfBase(speakerBase);
            const std::int32_t bounty = f == kNullFormId ? 0 : mLedger.bounty(f);
            return bounty > 0 && playerGold() >= bounty;
        };
        source.mAlarmed = [this](FormId speakerBase) {
            return mSystem.state() == State::Offer && speakerBase != kNullFormId && speakerBase == mSystem.guardBase();
        };
        return source;
    }

    bool CrimeService::payByScript(FormId faction, bool removeStolenItems)
    {
        if (mSystem.state() != State::Offer || faction == kNullFormId || faction != mSystem.offerFaction())
            return false;
        // The pack's pay always takes the stolen goods too (RECALLED-VERIFY: the game's flag); the flag is only logged.
        log(std::string("CRIME script pays the bounty (remove stolen items: ") + (removeStolenItems ? "yes" : "no")
            + ")");
        return choose("pay") == ChoiceStatus::Ok;
    }

    bool CrimeService::jailByScript(FormId faction)
    {
        if (mSystem.state() != State::Offer || faction == kNullFormId || faction != mSystem.offerFaction())
            return false;
        log("CRIME script sends the player to jail");
        return choose("jail") == ChoiceStatus::Ok;
    }

    bool CrimeService::resistByScript()
    {
        if (mSystem.state() != State::Offer)
            return false;
        log("CRIME script: the player resists arrest");
        return choose("resist") == ChoiceStatus::Ok;
    }

    std::int32_t CrimeService::crimeGoldOf(FormId faction) const
    {
        return mLedger.bounty(faction);
    }

    ChoiceStatus CrimeService::choose(std::string_view choice)
    {
        const FormId faction = mSystem.offerFaction();
        const ChoiceStatus status = mSystem.choose(choice);
        if (status == ChoiceStatus::Ok && mSystem.state() == State::Jailed && !jailOf(faction).mValid)
        {
            log("CRIME no jail destination for the faction: the bounty is cleared without a sentence");
            mSystem.setMessage("There is no jail here: your bounty is cleared.");
            mSystem.released();
        }
        return status;
    }

    void CrimeService::actors(std::vector<CrimeActor>& out)
    {
        if (mHooks.mActors)
            mHooks.mActors(out);
    }

    bool CrimeService::actor(openeden::sdk::game::ActorHandle handle, CrimeActor& out)
    {
        return mHooks.mActor && mHooks.mActor(handle, out);
    }

    const CrimeData* CrimeService::crimeData(FormId faction)
    {
        return mHooks.mCrimeData ? mHooks.mCrimeData(faction) : nullptr;
    }

    std::string CrimeService::factionName(FormId faction)
    {
        return mHooks.mFactionName ? mHooks.mFactionName(faction) : std::string();
    }

    std::int32_t CrimeService::itemValue(FormId item)
    {
        return mHooks.mItemValue ? mHooks.mItemValue(item) : 0;
    }

    std::int32_t CrimeService::playerGold()
    {
        return mHooks.mGold ? mHooks.mGold() : 0;
    }

    bool CrimeService::takeGold(std::int32_t amount)
    {
        return mHooks.mTakeGold && mHooks.mTakeGold(amount);
    }

    std::int32_t CrimeService::takeItem(FormId item, std::int32_t count)
    {
        return mHooks.mTakeItem ? mHooks.mTakeItem(item, count) : 0;
    }

    std::int32_t CrimeService::itemCount(FormId item)
    {
        return mHooks.mItemCount ? mHooks.mItemCount(item) : 0;
    }

    void CrimeService::guardGoTo(
        openeden::sdk::game::ActorHandle guard, double x, double y, double z, openeden::sdk::game::ActorSpeed speed)
    {
        if (mHooks.mGoTo)
            mHooks.mGoTo(guard, x, y, z, speed);
    }

    void CrimeService::guardHold(openeden::sdk::game::ActorHandle guard)
    {
        if (mHooks.mHold)
            mHooks.mHold(guard);
    }

    void CrimeService::guardRelease(openeden::sdk::game::ActorHandle guard)
    {
        if (mHooks.mRelease)
            mHooks.mRelease(guard);
    }

    void CrimeService::guardAttack(openeden::sdk::game::ActorHandle guard)
    {
        if (mHooks.mAttack)
            mHooks.mAttack(guard);
    }

    void CrimeService::log(const std::string& line)
    {
        if (mHooks.mLog)
            mHooks.mLog(line);
    }

    void CrimeService::enteredCell(std::string_view editorId)
    {
        std::string name(editorId);
        std::transform(name.begin(), name.end(), name.begin(),
            [](unsigned char ch) { return static_cast<char>(std::tolower(ch)); });
        if (name.empty() || name == mLastCell)
            return;
        const bool first = mLastCell.empty();
        mLastCell = name;
        mSystem.onLeftPlace(); // the earlier visit is over
        if (first)
            return; // a start or a load: the player did not walk in
        if (mSystem.state() == State::Jailed)
        {
            mAfterJail = true; // the cell of the jail, and the way back from it, are not entered of the player's will
            return;
        }
        if (mAfterJail)
        {
            mAfterJail = false;
            return;
        }
        CellAccess access;
        if (!mHooks.mCellAccess || !mHooks.mCellAccess(name, access) || !cellEntryIsTrespass(access))
            return;
        log("CRIME trespass: entered " + name + " (owner " + inventory::formatId(access.mOwner) + ")");
        // One visit is judged at a time (a new entry ends the last), so the owner, never null here, names it.
        mSystem.onTrespass(access.mOwner, Place::Cell);
    }

    void CrimeService::trespassBed(FormId placement)
    {
        if (placement == kNullFormId || !mHooks.mPlacementOwner)
            return;
        const FormId owner = mHooks.mPlacementOwner(placement);
        if (owner == kNullFormId || owner == kUnresolvedFormId || isPlayersOwner(owner))
            return;
        log("CRIME trespass: bed " + inventory::formatId(placement) + " (owner " + inventory::formatId(owner) + ")");
        mSystem.onTrespass(placement, Place::Bed);
    }

    openeden::sdk::DeclareResult CrimeService::declare(openeden::sdk::BindingsService& service)
    {
        Impl& m = *mImpl;
        CrimeService* self = this;
        static const BindingType s1[] = { BindingType::String };
        constexpr BindingType kStr = BindingType::String;

        const auto add = [&](std::string_view name, std::span<const BindingType> params, bool hasResult,
                             BindingType result, BindingAccess access, Fn::Body body) {
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
        const auto stringFn = [&](std::string_view name, std::function<std::string(CrimeService&)> get) {
            add(name, {}, true, kStr, BindingAccess::Read,
                [self, &m, get = std::move(get)](std::span<const BindingValue>) {
                    m.mText = get(*self);
                    BindingResult r = ok();
                    r.value = text(m.mText);
                    return r;
                });
        };
        const auto intFn = [&](std::string_view name, std::function<std::int64_t(CrimeService&)> get) {
            add(name, {}, true, BindingType::Int32, BindingAccess::Read,
                [self, get = std::move(get)](std::span<const BindingValue>) {
                    BindingResult r = ok();
                    r.value = integer(static_cast<std::int32_t>(std::clamp<std::int64_t>(get(*self), 0, 0x7FFFFFFF)));
                    return r;
                });
        };
        const auto doubleFn = [&](std::string_view name, std::function<double(CrimeService&)> get) {
            add(name, {}, true, BindingType::Double, BindingAccess::Read,
                [self, get = std::move(get)](std::span<const BindingValue>) {
                    BindingResult r = ok();
                    r.value = number(get(*self));
                    return r;
                });
        };
        // The faction whose jail is meant: the offer's, or the one the player was sent to.
        const auto jailFaction = [](CrimeService& s) {
            return s.mSystem.jailFaction() != kNullFormId ? s.mSystem.jailFaction() : s.mSystem.offerFaction();
        };

        stringFn("state", [](CrimeService& s) { return std::string(stateName(s.mSystem.state())); });
        // Task 0258 (1.1): how the offer is made, the guard's base (decimal, "" without an offer) and whether the
        // arrest conversation has begun; `dialogue_failed` tells the pack the conversation did not begin in time.
        stringFn("offer_mode", [](CrimeService& s) { return std::string(offerModeName(s.mSystem.offerMode())); });
        stringFn("guard_base", [](CrimeService& s) {
            return s.mSystem.state() == State::Offer && s.mSystem.guardBase() != kNullFormId
                ? std::to_string(s.mSystem.guardBase())
                : std::string();
        });
        add("dialogue_open", {}, true, BindingType::Bool, BindingAccess::Read, [self](std::span<const BindingValue>) {
            BindingResult r = ok();
            r.value = boolean(self->mSystem.dialogueOpen());
            return r;
        });
        add("dialogue_failed", {}, false, BindingType::Bool, BindingAccess::Mutate,
            [self](std::span<const BindingValue>) {
                self->mSystem.dialogueFailed(self->mSystem.guardReference());
                return ok();
            });
        stringFn("guard_name", [](CrimeService& s) { return s.mSystem.guardName(); });
        stringFn("message", [](CrimeService& s) { return s.mSystem.message(); });
        intFn("bounty", [](CrimeService& s) { return s.mLedger.totalBounty(); });
        intFn("offer_bounty", [](CrimeService& s) { return s.mSystem.offerBounty(); });
        intFn("stolen_count", [](CrimeService& s) { return s.mLedger.stolenTotal(); });
        intFn("revision", [](CrimeService& s) {
            return static_cast<std::int64_t>(s.mSystem.revision() + s.mLedger.revision() * 7u);
        });
        add("can_pay", {}, true, BindingType::Bool, BindingAccess::Read, [self](std::span<const BindingValue>) {
            BindingResult r = ok();
            r.value = boolean(self->mSystem.canPay());
            return r;
        });
        add("jail_available", {}, true, BindingType::Bool, BindingAccess::Read,
            [self, jailFaction](std::span<const BindingValue>) {
                BindingResult r = ok();
                r.value = boolean(self->jailOf(jailFaction(*self)).mValid);
                return r;
            });
        stringFn("jail_cell", [jailFaction](CrimeService& s) { return s.jailOf(jailFaction(s)).mCell; });
        doubleFn("jail_x", [jailFaction](CrimeService& s) { return s.jailOf(jailFaction(s)).mX; });
        doubleFn("jail_y", [jailFaction](CrimeService& s) { return s.jailOf(jailFaction(s)).mY; });
        doubleFn("jail_z", [jailFaction](CrimeService& s) { return s.jailOf(jailFaction(s)).mZ; });
        doubleFn("jail_heading", [jailFaction](CrimeService& s) { return s.jailOf(jailFaction(s)).mHeading; });
        doubleFn(
            "jail_seconds", [](CrimeService& s) { return static_cast<double>(s.mSystem.settings().mJailSeconds); });
        add("choose", s1, false, BindingType::Bool, BindingAccess::Mutate, [self](std::span<const BindingValue> a) {
            const ChoiceStatus status = self->choose(a[0].text);
            if (status != ChoiceStatus::Ok)
                self->log("CRIME choice " + std::string(a[0].text) + " refused ("
                    + (status == ChoiceStatus::NoOffer              ? "no offer"
                            : status == ChoiceStatus::NotEnoughGold ? "not enough gold"
                                                                    : "unknown choice")
                    + ")");
            return ok();
        });
        add("entered_cell", s1, false, BindingType::Bool, BindingAccess::Mutate,
            [self](std::span<const BindingValue> a) {
                self->enteredCell(a[0].text);
                return ok();
            });
        add("trespass_bed", s1, false, BindingType::Bool, BindingAccess::Mutate,
            [self](std::span<const BindingValue> a) {
                std::uint64_t id = 0;
                if (!inventory::parseId(a[0].text, id) || id > 0xFFFFFFFFull)
                {
                    self->log("CRIME trespass_bed refused: bad id");
                    return ok();
                }
                self->trespassBed(static_cast<FormId>(id));
                return ok();
            });
        add("released", {}, false, BindingType::Bool, BindingAccess::Mutate, [self](std::span<const BindingValue>) {
            self->mSystem.released();
            return ok();
        });

        openeden::sdk::NamespaceDescriptor ns;
        ns.name = "skyrimse.crime";
        ns.version = { 1, 1 };
        ns.functions = m.mFunctions;
        return service.declareNamespace(ns);
    }
}
