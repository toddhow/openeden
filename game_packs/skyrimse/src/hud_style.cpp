#include <game_skyrimse/hud_style.hpp>

#include <algorithm>
#include <cmath>
#include <utility>

namespace game_skyrimse::hudstyle
{
    using openeden::sdk::game::ActorHandle;
    using openeden::sdk::game::CombatSubject;

    namespace
    {
        float clean(float value)
        {
            return std::isfinite(value) && value > 0.f ? value : 0.f;
        }

        bool finiteSample(const ActorSample& a)
        {
            return std::isfinite(a.mX) && std::isfinite(a.mY) && std::isfinite(a.mZ);
        }
    }

    void selectBars(double playerX, double playerY, double playerZ, std::span<const ActorSample> actors,
        const SelectOptions& options, std::vector<ActorSample>& out)
    {
        out.clear();
        if (options.mMaxBars == 0 || !(options.mRange > 0.f))
            return;
        const double limit = static_cast<double>(options.mRange) * static_cast<double>(options.mRange);
        struct Ranked
        {
            double mDistance;
            std::size_t mIndex;
        };
        std::vector<Ranked> ranked;
        ranked.reserve(actors.size());
        for (std::size_t i = 0; i < actors.size(); ++i)
        {
            const ActorSample& a = actors[i];
            if (!a.mEngaged || a.mDead || !finiteSample(a))
                continue;
            const double dx = a.mX - playerX;
            const double dy = a.mY - playerY;
            const double dz = a.mZ - playerZ;
            const double d2 = dx * dx + dy * dy + dz * dz;
            if (!(d2 <= limit))
                continue;
            ranked.push_back({ d2, i });
        }
        std::sort(ranked.begin(), ranked.end(), [&](const Ranked& l, const Ranked& r) {
            if (l.mDistance != r.mDistance)
                return l.mDistance < r.mDistance;
            return actors[l.mIndex].mHandle.reference < actors[r.mIndex].mHandle.reference;
        });
        const std::size_t count = std::min(ranked.size(), options.mMaxBars);
        for (std::size_t i = 0; i < count; ++i)
            out.push_back(actors[ranked[i].mIndex]);
    }

    std::size_t chooseBoss(std::span<const ActorSample> actors)
    {
        std::size_t best = actors.size();
        for (std::size_t i = 0; i < actors.size(); ++i)
        {
            const ActorSample& a = actors[i];
            if (!a.mEngaged || a.mDead || !a.mBoss)
                continue;
            if (best == actors.size() || a.mHealthMax > actors[best].mHealthMax
                || (a.mHealthMax == actors[best].mHealthMax && a.mHandle.reference < actors[best].mHandle.reference))
                best = i;
        }
        return best;
    }

    StyleHud::StyleHud(const CombatHud& hud, Source source)
        : mHud(hud)
        , mSource(std::move(source))
    {
        mMemos.reserve(kMaxMemos);
        mCandidates.reserve(kMaxMemos);
        mBars.reserve(kMaxActorBars);
        mInRange.reserve(kMaxMemos);
        mNames.resize(kMaxActorBars);
        for (std::string& name : mNames)
            name.reserve(kMaxHudNameBytes);
        mBossName.reserve(kMaxHudNameBytes);
        mVisitor = [this](const CombatSubject& subject, const SkyrimCombat::FighterView& view) { visit(subject, view); };
    }

    StyleHud::Memo& StyleHud::memoOf(ActorHandle handle)
    {
        for (Memo& memo : mMemos)
            if (memo.mHandle == handle)
                return memo;
        if (mMemos.size() >= kMaxMemos)
        {
            // forget the one engaged longest ago
            std::size_t oldest = 0;
            for (std::size_t i = 1; i < mMemos.size(); ++i)
                if (mMemos[i].mEngagedAgo > mMemos[oldest].mEngagedAgo)
                    oldest = i;
            mMemos[oldest] = Memo{};
            mMemos[oldest].mHandle = handle;
            return mMemos[oldest];
        }
        mMemos.push_back(Memo{});
        mMemos.back().mHandle = handle;
        return mMemos.back();
    }

    void StyleHud::visit(const CombatSubject& subject, const SkyrimCombat::FighterView& view)
    {
        if (subject.player || !subject.actor.valid() || view.mDead)
            return;
        Memo& memo = memoOf(subject.actor);
        memo.mTouched = true;
        if (view.mEngaged)
            memo.mEngagedAgo = 0.f;
        else
            memo.mEngagedAgo += mSeconds;
        const float magicka = clean(view.mMagicka);
        const float stamina = clean(view.mStamina);
        if (memo.mSeen)
        {
            if (std::fabs(magicka - memo.mMagicka) > kSecondaryChange
                || std::fabs(stamina - memo.mStamina) > kSecondaryChange)
                memo.mSecondaryLeft = kSecondarySeconds;
            else
                memo.mSecondaryLeft = std::max(0.f, memo.mSecondaryLeft - mSeconds);
        }
        memo.mSeen = true;
        const bool showMagicka = memo.mSecondaryLeft > 0.f && clean(view.mMagickaMax) > 0.f;
        const bool showStamina = memo.mSecondaryLeft > 0.f && clean(view.mStaminaMax) > 0.f;
        memo.mMagicka = magicka;
        memo.mStamina = stamina;
        if (!(memo.mEngagedAgo <= kBarLingerSeconds))
            return;
        if (mCandidates.size() >= mCandidates.capacity())
            return;
        CombatSubject s = subject;
        openeden::sdk::game::SubjectState state;
        if (mWriter == nullptr || !mWriter->subjectState(s, state))
            return;
        ActorSample sample;
        sample.mHandle = subject.actor;
        sample.mLevel = view.mLevel;
        sample.mHealth = clean(view.mHealth);
        sample.mHealthMax = clean(view.mHealthMax);
        sample.mMagicka = magicka;
        sample.mMagickaMax = clean(view.mMagickaMax);
        sample.mStamina = stamina;
        sample.mStaminaMax = clean(view.mStaminaMax);
        sample.mEngaged = true;
        sample.mDead = false;
        sample.mAlly = mSource.mIsAlly && mSource.mIsAlly(subject.actor.reference);
        sample.mBoss = mHud.isBoss(subject.actor);
        sample.mShowMagicka = showMagicka;
        sample.mShowStamina = showStamina;
        sample.mX = state.x;
        sample.mY = state.y;
        sample.mZ = state.z + static_cast<double>(kHeadOffset);
        mCandidates.push_back(sample);
    }

    void StyleHud::update(float seconds, openeden::sdk::game::CombatRequestWriter& writer)
    {
        ++mUpdates;
        if (!std::isfinite(seconds) || seconds < 0.f)
            seconds = 0.f;
        mSeconds = seconds;
        mWriter = &writer;
        mCandidates.clear();
        for (Memo& memo : mMemos)
            memo.mTouched = false;

        openeden::sdk::game::SubjectState player;
        mPlayerX = mPlayerY = mPlayerZ = 0.0;
        bool havePlayer = writer.subjectState(CombatSubject::ofPlayer(), player);
        if (havePlayer)
        {
            mPlayerX = player.x;
            mPlayerY = player.y;
            mPlayerZ = player.z;
        }
        SkyrimCombat::FighterView playerView;
        if (mSource.mLookup && mSource.mLookup(CombatSubject::ofPlayer(), playerView))
            mPlayerLevel = playerView.mLevel;

        if (mSource.mFighters)
            mSource.mFighters(mVisitor);
        // Memos of actors that are gone (or dead) are forgotten.
        std::erase_if(mMemos, [](const Memo& memo) { return !memo.mTouched; });

        // Everyone in range, nearest first; the first kMaxActorBars get a bar, the boss may be any of them.
        SelectOptions options;
        options.mMaxBars = kMaxMemos;
        if (havePlayer)
            selectBars(mPlayerX, mPlayerY, mPlayerZ, mCandidates, options, mInRange);
        else
            mInRange.clear();
        const std::size_t shown = std::min(mInRange.size(), kMaxActorBars);
        mBars.assign(mInRange.begin(), mInRange.begin() + static_cast<std::ptrdiff_t>(shown));
        for (std::size_t i = 0; i < mBars.size(); ++i)
        {
            const std::string_view name = mHud.nameOf(mBars[i].mHandle);
            mNames[i].assign(name.data(), name.size());
        }

        // The top bar: a boss in range, else the enemy the combat HUD shows.
        mBoss = Boss{};
        mBossName.clear();
        const std::size_t bossIndex = chooseBoss(mInRange);
        if (bossIndex < mInRange.size())
        {
            const ActorSample& boss = mInRange[bossIndex];
            mBoss.mShown = true;
            mBoss.mIsBoss = true;
            mBoss.mHealth = boss.mHealth;
            mBoss.mHealthMax = boss.mHealthMax;
            mBoss.mLevel = boss.mLevel;
            const std::string_view name = mHud.nameOf(boss.mHandle);
            mBossName.assign(name.data(), name.size());
        }
        else
        {
            const HudSnapshot& snap = mHud.snapshot();
            if (snap.mEnemyShown)
            {
                mBoss.mShown = true;
                mBoss.mDead = snap.mEnemyDead;
                mBoss.mHealth = snap.mEnemyHealth;
                mBoss.mHealthMax = snap.mEnemyHealthMax;
                mBossName.assign(mHud.enemyName());
                SkyrimCombat::FighterView view;
                const ActorHandle target = mHud.target();
                if (target.valid() && mSource.mLookup && mSource.mLookup(CombatSubject::of(target), view))
                    mBoss.mLevel = view.mLevel;
            }
        }
        mWriter = nullptr;
    }

    std::string_view StyleHud::barName(std::size_t index) const
    {
        return index < mBars.size() && index < mNames.size() ? std::string_view(mNames[index]) : std::string_view();
    }

    void StyleHud::playerValues(std::array<double, kPlayerValueCount>& out) const
    {
        const HudSnapshot& s = mHud.snapshot();
        out = { s.mPlayerHealth, s.mPlayerHealthMax, s.mPlayerMagicka, s.mPlayerMagickaMax, s.mPlayerStamina,
            s.mPlayerStaminaMax, static_cast<double>(mPlayerLevel) };
    }

    void StyleHud::bossValues(std::array<double, kBossValueCount>& out) const
    {
        out = { mBoss.mShown ? 1.0 : 0.0, mBoss.mHealth, mBoss.mHealthMax, static_cast<double>(mBoss.mLevel),
            mBoss.mIsBoss ? 1.0 : 0.0, mBoss.mDead ? 1.0 : 0.0 };
    }

    void StyleHud::barValues(std::vector<double>& out) const
    {
        out.clear();
        for (const ActorSample& a : mBars)
        {
            const double flags = (a.mShowMagicka ? 1.0 : 0.0) + (a.mShowStamina ? 2.0 : 0.0);
            out.insert(out.end(),
                { static_cast<double>(a.mHandle.reference), a.mX, a.mY, a.mZ, a.mHealth, a.mHealthMax, a.mMagicka,
                    a.mMagickaMax, a.mStamina, a.mStaminaMax, static_cast<double>(a.mLevel), a.mAlly ? 1.0 : 0.0,
                    flags });
        }
    }

    // ---- the script package ----

    struct StyleBindings::Impl
    {
        using BindingResult = openeden::sdk::BindingResult;
        using BindingValue = openeden::sdk::BindingValue;
        using BindingType = openeden::sdk::BindingType;

        struct ActiveHandler final : openeden::sdk::BindingHandler
        {
            const StyleHud& mHud;
            explicit ActiveHandler(const StyleHud& hud)
                : mHud(hud)
            {
            }
            BindingResult call(std::span<const BindingValue>) override
            {
                BindingResult result;
                result.value.type = BindingType::Bool;
                result.value.boolean = mHud.active();
                return result;
            }
        };

        template <std::size_t N>
        struct NumbersHandler final : openeden::sdk::BindingHandler
        {
            std::function<void(std::array<double, N>&)> mFill;
            std::array<BindingValue, N> mElements;
            BindingResult call(std::span<const BindingValue>) override
            {
                std::array<double, N> numbers{};
                mFill(numbers);
                for (std::size_t i = 0; i < N; ++i)
                {
                    mElements[i] = BindingValue{};
                    mElements[i].type = BindingType::Double;
                    mElements[i].number = numbers[i];
                }
                BindingResult result;
                result.value.type = BindingType::Double;
                result.value.elements = mElements.data();
                result.value.count = N;
                return result;
            }
        };

        struct BossNameHandler final : openeden::sdk::BindingHandler
        {
            const StyleHud& mHud;
            explicit BossNameHandler(const StyleHud& hud)
                : mHud(hud)
            {
            }
            BindingResult call(std::span<const BindingValue>) override
            {
                BindingResult result;
                result.value.type = BindingType::String;
                result.value.text = mHud.bossName();
                return result;
            }
        };

        struct BarsHandler final : openeden::sdk::BindingHandler
        {
            const StyleHud& mHud;
            std::vector<double> mNumbers;
            std::vector<BindingValue> mElements;
            explicit BarsHandler(const StyleHud& hud)
                : mHud(hud)
            {
                mNumbers.reserve(kMaxActorBars * kBarStride);
                mElements.reserve(kMaxActorBars * kBarStride);
            }
            BindingResult call(std::span<const BindingValue>) override
            {
                mHud.barValues(mNumbers);
                mElements.clear();
                for (const double number : mNumbers)
                {
                    BindingValue v;
                    v.type = BindingType::Double;
                    v.number = number;
                    mElements.push_back(v);
                }
                BindingResult result;
                result.value.type = BindingType::Double;
                result.value.elements = mElements.empty() ? nullptr : mElements.data();
                result.value.count = mElements.size();
                return result;
            }
        };

        struct BarNamesHandler final : openeden::sdk::BindingHandler
        {
            const StyleHud& mHud;
            std::array<BindingValue, kMaxActorBars> mElements;
            explicit BarNamesHandler(const StyleHud& hud)
                : mHud(hud)
            {
            }
            BindingResult call(std::span<const BindingValue>) override
            {
                const std::size_t count = std::min(mHud.bars().size(), kMaxActorBars);
                for (std::size_t i = 0; i < count; ++i)
                {
                    mElements[i] = BindingValue{};
                    mElements[i].type = BindingType::String;
                    mElements[i].text = mHud.barName(i);
                }
                BindingResult result;
                result.value.type = BindingType::String;
                result.value.elements = count == 0 ? nullptr : mElements.data();
                result.value.count = count;
                return result;
            }
        };

        explicit Impl(const StyleHud& hud)
            : mActive(hud)
            , mBossName(hud)
            , mBars(hud)
            , mBarNames(hud)
        {
            mPlayer.mFill = [&hud](std::array<double, kPlayerValueCount>& out) { hud.playerValues(out); };
            mBoss.mFill = [&hud](std::array<double, kBossValueCount>& out) { hud.bossValues(out); };
        }

        ActiveHandler mActive;
        NumbersHandler<kPlayerValueCount> mPlayer;
        NumbersHandler<kBossValueCount> mBoss;
        BossNameHandler mBossName;
        BarsHandler mBars;
        BarNamesHandler mBarNames;
        std::array<openeden::sdk::FunctionDescriptor, 6> mFunctions;
    };

    StyleBindings::StyleBindings(const StyleHud& hud)
        : mImpl(std::make_unique<Impl>(hud))
    {
    }

    StyleBindings::~StyleBindings() = default;

    openeden::sdk::DeclareResult StyleBindings::declareTo(openeden::sdk::BindingsService& service)
    {
        using namespace openeden::sdk;
        mImpl->mFunctions[0] = FunctionDescriptor{ "active", {}, true, BindingType::Bool, BindingAccess::Read,
            &mImpl->mActive, 0 };
        mImpl->mFunctions[1] = FunctionDescriptor{ "player", {}, true, BindingType::Double, BindingAccess::Read,
            &mImpl->mPlayer, 0, {}, BindingShape::Array };
        mImpl->mFunctions[2] = FunctionDescriptor{ "boss", {}, true, BindingType::Double, BindingAccess::Read,
            &mImpl->mBoss, 0, {}, BindingShape::Array };
        mImpl->mFunctions[3] = FunctionDescriptor{ "boss_name", {}, true, BindingType::String, BindingAccess::Read,
            &mImpl->mBossName, 0 };
        mImpl->mFunctions[4] = FunctionDescriptor{ "bars", {}, true, BindingType::Double, BindingAccess::Read,
            &mImpl->mBars, 0, {}, BindingShape::Array };
        mImpl->mFunctions[5] = FunctionDescriptor{ "bar_names", {}, true, BindingType::String, BindingAccess::Read,
            &mImpl->mBarNames, 0, {}, BindingShape::Array };
        NamespaceDescriptor ns;
        ns.name = kNamespace;
        ns.version = Version{ 1, 0 };
        ns.functions = mImpl->mFunctions;
        ns.menuSafe = false;
        return service.declareNamespace(ns);
    }
}
