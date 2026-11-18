#include "game_skyrimse/papyrus_world.hpp"

#include <algorithm>

namespace game_skyrimse
{
    namespace
    {
        class NoteFn final : public openeden::sdk::BindingHandler
        {
        public:
            using Body = std::function<openeden::sdk::BindingResult(std::span<const openeden::sdk::BindingValue>)>;
            explicit NoteFn(Body body)
                : mBody(std::move(body))
            {
            }
            openeden::sdk::BindingResult call(std::span<const openeden::sdk::BindingValue> args) override
            {
                return mBody(args);
            }

        private:
            Body mBody;
        };
    }

    struct PapyrusNotificationBindings::Impl
    {
        explicit Impl(PapyrusHost& host)
            : mHost(host)
        {
        }
        PapyrusHost& mHost;
        std::vector<std::unique_ptr<NoteFn>> mHandlers;
        std::vector<openeden::sdk::FunctionDescriptor> mFunctions;
        std::string mScratch; ///< the answer's text points into it until the next call
    };

    PapyrusNotificationBindings::PapyrusNotificationBindings(PapyrusHost& host)
        : mImpl(std::make_unique<Impl>(host))
    {
    }

    PapyrusNotificationBindings::~PapyrusNotificationBindings() = default;

    openeden::sdk::DeclareResult PapyrusNotificationBindings::declareTo(openeden::sdk::BindingsService& service)
    {
        using openeden::sdk::BindingResult;
        using openeden::sdk::BindingType;
        using openeden::sdk::BindingValue;
        Impl& m = *mImpl;
        static const BindingType numberParam[] = { BindingType::Int32 };
        const auto add
            = [&m](std::string_view name, std::span<const BindingType> params, BindingType result, NoteFn::Body body) {
                  m.mHandlers.push_back(std::make_unique<NoteFn>(std::move(body)));
                  openeden::sdk::FunctionDescriptor f;
                  f.name = name;
                  f.params = params;
                  f.hasResult = true;
                  f.result = result;
                  f.access = openeden::sdk::BindingAccess::Read;
                  f.handler = m.mHandlers.back().get();
                  m.mFunctions.push_back(f);
              };
        add("notification_count", {}, BindingType::Int32, [&m](std::span<const BindingValue>) {
            BindingResult r;
            r.value.type = BindingType::Int32;
            r.value.integer = static_cast<std::int32_t>(m.mHost.notificationCount() & 0x7FFFFFFF);
            return r;
        });
        add("notification", numberParam, BindingType::String, [&m](std::span<const BindingValue> a) {
            m.mScratch
                = m.mHost.notificationAt(a.empty() || a[0].integer < 0 ? 0 : static_cast<std::uint64_t>(a[0].integer));
            BindingResult r;
            r.value.type = BindingType::String;
            r.value.text = m.mScratch;
            return r;
        });
        openeden::sdk::NamespaceDescriptor ns;
        ns.name = "skyrimse.papyrus";
        ns.version = { 1, 0 };
        ns.functions = m.mFunctions;
        return service.declareNamespace(ns);
    }
    using inventory::InventoryModel;
    using inventory::TransferStatus;

    bool PackWorldHooks::ready(FormId reference, InventoryModel*& model) const
    {
        model = this->model();
        if (model == nullptr || reference == 0)
            return false;
        const std::uint64_t owner = ownerOf(reference);
        if (model->hasOwner(owner))
            return true;
        return mEnv.ensureOwner && mEnv.ensureOwner(owner, reference) && model->hasOwner(owner);
    }

    FormId PackWorldHooks::resolveLeveledItem(FormId base)
    {
        return mEnv.resolveItem ? mEnv.resolveItem(base) : base;
    }

    bool PackWorldHooks::addItem(FormId owner, FormId item, std::int32_t count)
    {
        InventoryModel* m = nullptr;
        if (!ready(owner, m))
            return false;
        return m->add(ownerOf(owner), item, count) == TransferStatus::Ok;
    }

    std::int32_t PackWorldHooks::removeItem(FormId owner, FormId item, std::int32_t count)
    {
        InventoryModel* m = nullptr;
        if (!ready(owner, m))
            return -1;
        const std::int32_t have = m->count(ownerOf(owner), item);
        const std::int32_t take = std::min(have, count);
        if (take <= 0)
            return 0;
        return m->consume(ownerOf(owner), item, take) == TransferStatus::Ok ? take : -1;
    }

    std::int32_t PackWorldHooks::itemCount(FormId owner, FormId item)
    {
        InventoryModel* m = this->model();
        if (m == nullptr || owner == 0)
            return -1;
        if (!m->hasOwner(ownerOf(owner)) && !ready(owner, m))
            return -1;
        return m->count(ownerOf(owner), item);
    }

    std::int32_t PackWorldHooks::goldValue(FormId item)
    {
        return mEnv.goldValue ? mEnv.goldValue(item) : -1;
    }

    bool PackWorldHooks::transferItem(FormId from, FormId to, FormId item, std::int32_t count)
    {
        InventoryModel* m = nullptr;
        if (!ready(from, m) || to == 0)
            return false;
        InventoryModel* other = nullptr;
        if (!ready(to, other))
            return false;
        const std::int32_t take = std::min(m->count(ownerOf(from), item), count);
        return take > 0 && m->transfer(ownerOf(from), ownerOf(to), item, take) == TransferStatus::Ok;
    }

    std::int64_t PackWorldHooks::removeAllItems(FormId owner, FormId target)
    {
        InventoryModel* m = nullptr;
        if (!ready(owner, m))
            return -1;
        std::int64_t moved = 0;
        if (target != 0)
        {
            InventoryModel* other = nullptr;
            if (!ready(target, other) || ownerOf(target) == ownerOf(owner))
                return -1;
            (void)m->transferAll(ownerOf(owner), ownerOf(target), moved);
            return moved;
        }
        for (const inventory::Stack& s : m->stacks(ownerOf(owner)))
            if (m->consume(ownerOf(owner), s.mItem, s.mCount) == TransferStatus::Ok)
                moved += s.mCount;
        return moved;
    }

    bool PackWorldHooks::actorValue(FormId actor, std::string_view lowerName, double& current, double& maximum)
    {
        return mEnv.actorValue && mEnv.actorValue(actor, lowerName, current, maximum);
    }

    bool PackWorldHooks::setHealth(FormId actor, double health)
    {
        return mEnv.setHealth && mEnv.setHealth(actor, health);
    }

    bool PackWorldHooks::isDead(FormId actor)
    {
        return mEnv.isDead && mEnv.isDead(actor);
    }

    bool PackWorldHooks::killActor(FormId actor)
    {
        return mEnv.queueKill && mEnv.queueKill(actor);
    }

    bool PackWorldHooks::isItem(FormId form)
    {
        return mEnv.isItem && mEnv.isItem(form);
    }

    void PackWorldHooks::notification(const std::string& text)
    {
        if (mEnv.notification)
            mEnv.notification(text);
    }

    // ---- ScriptCombatBridge
    // ------------------------------------------------------------------------------------------

    bool ScriptCombatBridge::queueKill(FormId actor)
    {
        if (actor == 0 || mKills.size() >= kMaxKills)
            return false;
        if (std::find(mKills.begin(), mKills.end(), actor) == mKills.end())
            mKills.push_back(actor);
        return true;
    }

    void ScriptCombatBridge::noteDamage(const openeden::sdk::game::DamageReport& report)
    {
        if (!mHit && !mDeath)
            return;
        const FormId victim = report.victimIsPlayer ? kPapyrusPlayerReference : report.victim.reference;
        const FormId attacker = report.attackerIsPlayer ? kPapyrusPlayerReference : report.attacker.reference;
        if (victim == 0)
            return;
        if (mLastAttacker.size() >= kMaxRememberedAttackers && mLastAttacker.count(victim) == 0)
            mLastAttacker.clear(); // a bound, not a rule: the next death then names no killer
        mLastAttacker[victim] = attacker;
        if (mHit && report.amount >= 0.f)
            mHit(victim, attacker, report.blocked, false);
    }

    void ScriptCombatBridge::noteDeath(openeden::sdk::game::ActorHandle actor)
    {
        if (!mDeath || actor.reference == 0)
            return;
        FormId killer = 0;
        const auto it = mLastAttacker.find(actor.reference);
        if (it != mLastAttacker.end())
        {
            killer = it->second;
            mLastAttacker.erase(it);
        }
        mDeath(actor.reference, killer);
    }

    void ScriptCombatBridge::drain(openeden::sdk::game::CombatRequestWriter& writer)
    {
        if (!mShots.empty())
        {
            std::vector<QueuedShot> shots;
            shots.swap(mShots);
            for (const QueuedShot& shot : shots)
                if (mShot)
                    mShot(shot, writer);
        }
        if (mKills.empty())
            return;
        std::vector<FormId> kills;
        kills.swap(mKills);
        for (const FormId actor : kills)
            if (mKill)
                mKill(actor, writer);
    }
}
