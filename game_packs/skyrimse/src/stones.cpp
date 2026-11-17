#include "game_skyrimse/stones.hpp"

#include "game_skyrimse/ability_numbers.hpp"
#include "game_skyrimse/diseases.hpp"

#include <algorithm>
#include <cmath>
#include <cstring>

namespace game_skyrimse::stones
{
    namespace
    {
        std::string lowerText(std::string s)
        {
            for (char& c : s)
                c = static_cast<char>(c >= 'A' && c <= 'Z' ? c + 32 : c);
            return s;
        }

        /// Does a ModSkillUse effect apply to the skill? Its tab conditions that test the skill being advanced
        /// (function 681) are OR-ed; with none the effect applies to every skill.
        bool effectCoversSkill(const PerkEffect& effect, std::size_t slot)
        {
            bool any = false;
            for (const PerkTab& tab : effect.mTabs)
                for (const survey::Ctda& c : tab.mConditions)
                    if (c.mFunction == kFunctionIsAdvanceSkill)
                    {
                        any = true;
                        if (c.mParam1 == kFirstSkillValue + slot)
                            return true;
                    }
            return !any;
        }

        const PerkRecord* perkOfEffect(const PerkLibrary& library, const SpellEffect& effect)
        {
            const MagicEffectRecord* magic = library.magicEffect(effect.mEffect);
            if (magic == nullptr || magic->mPerkToApply == kNullFormId)
                return nullptr;
            return library.perk(magic->mPerkToApply);
        }

        bool hasSkillUseEntry(const PerkRecord& perk)
        {
            return std::any_of(perk.mEffects.begin(), perk.mEffects.end(), [](const PerkEffect& e) {
                return e.mKind == PerkEffectKind::EntryPoint && e.mEntryPoint == kEntryModSkillUse;
            });
        }

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
        BindingValue integer(std::int32_t v)
        {
            BindingValue b;
            b.type = BindingType::Int32;
            b.integer = v;
            return b;
        }
        BindingValue text(std::string_view v)
        {
            BindingValue b;
            b.type = BindingType::String;
            b.text = v;
            return b;
        }

        /// Lower or upper case hex of at most 16 digits; false for anything else.
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

    float skillUseScale(const PerkLibrary& library, std::span<const FormId> spells, std::size_t slot)
    {
        std::vector<FormId> sorted(spells.begin(), spells.end());
        std::sort(sorted.begin(), sorted.end());
        sorted.erase(std::unique(sorted.begin(), sorted.end()), sorted.end());
        float scale = 1.f;
        for (const FormId id : sorted)
        {
            const SpellRecord* spell = library.spell(id);
            if (spell == nullptr)
                continue;
            for (const SpellEffect& effect : spell->mEffects)
            {
                const PerkRecord* perk = perkOfEffect(library, effect);
                if (perk == nullptr)
                    continue;
                for (const PerkEffect& e : perk->mEffects)
                {
                    if (e.mKind != PerkEffectKind::EntryPoint || e.mEntryPoint != kEntryModSkillUse
                        || !std::isfinite(e.mValue) || !effectCoversSkill(e, slot))
                        continue;
                    if (e.mFunction == PerkFunction::Multiply)
                        scale *= e.mValue;
                    else if (e.mFunction == PerkFunction::Add)
                        scale += e.mValue;
                    else if (e.mFunction == PerkFunction::Set)
                        scale = e.mValue;
                }
            }
        }
        return std::isfinite(scale) && scale > 0.f ? scale : 1.f;
    }

    Support supportOf(const PerkLibrary& library, FormId spellId, std::string* detail)
    {
        const SpellRecord* spell = library.spell(spellId);
        if (spell == nullptr || spell->mEffects.empty())
            return Support::Nothing;
        std::size_t works = 0, partly = 0;
        std::string what;
        for (const SpellEffect& effect : spell->mEffects)
        {
            const MagicEffectRecord* magic = library.magicEffect(effect.mEffect);
            if (magic == nullptr || !magic->mHasData)
                continue;
            const PerkRecord* perk = perkOfEffect(library, effect);
            if (perk != nullptr && hasSkillUseEntry(*perk))
            {
                ++works;
                what += (what.empty() ? "" : ", ") + std::string("skill experience");
            }
            else if (magic->modifiesAValue() && magic->mPrimaryValue == kValueDamageResist)
            {
                ++works;
                what += (what.empty() ? "" : ", ") + std::string("damage resistance");
            }
            else if (magic->modifiesAValue())
            {
                ++partly;
                what += (what.empty() ? "" : ", ") + std::string("kept as status only");
            }
        }
        if (detail != nullptr)
            *detail = what;
        if (works > 0 && works == spell->mEffects.size())
            return Support::Works;
        return works + partly > 0 ? Support::Partly : Support::Nothing;
    }

    std::string_view supportName(Support support)
    {
        switch (support)
        {
            case Support::Works:
                return "works";
            case Support::Partly:
                return "partly";
            case Support::Nothing:
                return "nothing";
        }
        return "nothing";
    }

    double durationOf(const PerkLibrary& library, FormId spellId)
    {
        const SpellRecord* spell = library.spell(spellId);
        if (spell == nullptr)
            return 0.0;
        double longest = 0.0;
        for (const SpellEffect& effect : spell->mEffects)
            longest = std::max(longest, static_cast<double>(effect.mDuration));
        return std::min(longest, kMaxDurationSeconds);
    }

    // ---- the chunk -------------------------------------------------------------------------------------------------

    namespace
    {
        class StonesChunk final : public openeden::sdk::SaveChunkHandler
        {
        public:
            explicit StonesChunk(StonesService& service)
                : mService(service)
            {
            }
            std::string_view chunkName() const override { return "stones"; }
            std::uint32_t chunkVersion() const override { return 1; }
            void write(openeden::sdk::ChunkWriter& writer) override
            {
                const auto& timed = mService.timedSpells();
                writer.writeValue(static_cast<std::uint32_t>(timed.size()));
                for (const StonesService::Timed& t : timed)
                {
                    writer.writeValue(static_cast<std::uint32_t>(t.mSpell));
                    writer.writeValue(t.mEnds);
                }
            }
            bool read(openeden::sdk::ChunkReader& reader, std::uint32_t version) override
            {
                std::uint32_t count = 0;
                if (version != 1 || !reader.readValue(count) || count > kMaxTimedSpells
                    || reader.remaining() != static_cast<std::size_t>(count) * (sizeof(std::uint32_t) + sizeof(double)))
                    return false;
                std::vector<StonesService::Timed> spells;
                for (std::uint32_t i = 0; i < count; ++i)
                {
                    std::uint32_t id = 0;
                    double ends = 0.0;
                    if (!reader.readValue(id) || !reader.readValue(ends) || !std::isfinite(ends))
                        return false;
                    spells.push_back({ id, ends });
                }
                mService.restoreTimed(std::move(spells));
                return true;
            }

        private:
            StonesService& mService;
        };
    }

    struct StonesService::Impl
    {
        std::vector<std::unique_ptr<Fn>> mHandlers;
        std::vector<openeden::sdk::FunctionDescriptor> mFunctions;
        std::unique_ptr<StonesChunk> mChunk;
        std::string mText;
    };

    StonesService::StonesService(Hooks hooks)
        : mImpl(std::make_unique<Impl>())
        , mHooks(std::move(hooks))
    {
        mImpl->mChunk = std::make_unique<StonesChunk>(*this);
    }

    StonesService::~StonesService() = default;

    void StonesService::log(const std::string& line) const
    {
        if (mHooks.mLog)
            mHooks.mLog(line);
    }

    openeden::sdk::SaveChunkHandler& StonesService::chunk()
    {
        return *mImpl->mChunk;
    }

    bool StonesService::cast(FormId spell, FormId target)
    {
        const PerkLibrary* library = mHooks.mLibrary ? mHooks.mLibrary() : nullptr;
        if (library == nullptr || target != 0x14 || library->spell(spell) == nullptr)
            return false;
        if (mHooks.mCure && diseases::hasCureDisease(*library, spell))
            mHooks.mCure(); // task 0240: every temple blessing carries Cure Disease
        const double duration = durationOf(*library, spell);
        if (!(duration > 0.0))
        {
            log("STONES cast " + std::to_string(spell) + " is instant: nothing is kept");
            return true;
        }
        if (mTimed.size() >= kMaxTimedSpells
            && std::none_of(mTimed.begin(), mTimed.end(), [spell](const Timed& t) { return t.mSpell == spell; }))
            return false;
        const SpellRecord* record = library->spell(spell);
        const bool blessing = lowerText(record->mCommon.mEditorId).rfind(kBlessingPrefix, 0) == 0;
        if (blessing)
            mTimed.erase(std::remove_if(mTimed.begin(), mTimed.end(),
                             [&](const Timed& t) {
                                 const SpellRecord* other = library->spell(t.mSpell);
                                 return other != nullptr
                                     && lowerText(other->mCommon.mEditorId).rfind(kBlessingPrefix, 0) == 0;
                             }),
                mTimed.end());
        const double now = mHooks.mNow ? mHooks.mNow() : 0.0;
        const auto found
            = std::find_if(mTimed.begin(), mTimed.end(), [spell](const Timed& t) { return t.mSpell == spell; });
        if (found != mTimed.end())
            found->mEnds = now + duration;
        else
            mTimed.push_back({ spell, now + duration });
        std::string detail;
        const Support support = supportOf(*library, spell, &detail);
        const FormId one[] = { spell };
        log("STONES blessing " + record->mCommon.mEditorId + " for " + std::to_string(static_cast<long long>(duration))
            + " s: " + std::string(supportName(support)) + (detail.empty() ? "" : " (" + detail + ")")
            + (sumAbilityNumbers(*library, one).any() ? "; its numbers are applied (task 0241)" : ""));
        ++mRevision;
        return true;
    }

    void StonesService::restoreTimed(std::vector<Timed> spells)
    {
        mTimed = std::move(spells);
        ++mRevision;
    }

    std::vector<FormId> StonesService::timed() const
    {
        std::vector<FormId> out;
        for (const Timed& t : mTimed)
            out.push_back(t.mSpell);
        std::sort(out.begin(), out.end());
        return out;
    }

    std::vector<FormId> StonesService::active() const
    {
        std::vector<FormId> out = mHooks.mPersistent ? mHooks.mPersistent() : std::vector<FormId>();
        for (const Timed& t : mTimed)
            out.push_back(t.mSpell);
        std::sort(out.begin(), out.end());
        out.erase(std::unique(out.begin(), out.end()), out.end());
        return out;
    }

    void StonesService::poll()
    {
        const double now = mHooks.mNow ? mHooks.mNow() : 0.0;
        const std::size_t before = mTimed.size();
        mTimed.erase(std::remove_if(mTimed.begin(), mTimed.end(), [now](const Timed& t) { return t.mEnds <= now; }),
            mTimed.end());
        if (mTimed.size() != before)
        {
            log("STONES " + std::to_string(before - mTimed.size()) + " timed spell(s) ended");
            ++mRevision;
        }
        std::vector<FormId> now2 = active();
        if (!mApplied || now2 != mAppliedSpells)
        {
            mAppliedSpells = now2;
            mApplied = true;
            ++mRevision;
            std::string list;
            for (const FormId id : now2)
                list += (list.empty() ? "" : ",") + std::to_string(id);
            log("STONES active spells: [" + list + "]");
            const PerkLibrary* library = mHooks.mLibrary ? mHooks.mLibrary() : nullptr;
            if (library != nullptr)
            {
                std::string scales;
                for (const std::size_t slot : { std::size_t(0), std::size_t(2), std::size_t(12) })
                    scales += (scales.empty() ? "" : ", ") + std::to_string(slot) + " x"
                        + std::to_string(skillUseScale(*library, now2, slot));
                log("STONES skill experience scale by skill slot: " + scales);
            }
            if (mHooks.mApply)
                mHooks.mApply(now2);
        }
    }

    float StonesService::skillScale(std::size_t slot) const
    {
        const PerkLibrary* library = mHooks.mLibrary ? mHooks.mLibrary() : nullptr;
        if (library == nullptr)
            return 1.f;
        const std::vector<FormId> spells = active();
        return skillUseScale(*library, spells, slot);
    }

    std::uint64_t StonesService::revision() const
    {
        return mRevision;
    }

    void StonesService::ask(FormId message, const PapyrusMessageInfo& info)
    {
        mMessage = message;
        mText = info.mText;
        mButtons = info.mButtons;
        ++mRevision;
        log("STONES message " + std::to_string(message) + " asks: " + info.mText);
    }

    bool StonesService::messageOpen() const
    {
        return mMessage != 0;
    }

    const std::string& StonesService::messageText() const
    {
        return mText;
    }

    const std::vector<std::string>& StonesService::messageButtons() const
    {
        return mButtons;
    }

    std::size_t StonesService::answered() const
    {
        return mAnswered;
    }

    bool StonesService::activate(FormId placement, FormId base)
    {
        const bool told = mHooks.mActivate && mHooks.mActivate(placement, base);
        if (told)
            log("STONES activated placement " + std::to_string(placement));
        return told;
    }

    bool StonesService::choose(std::int32_t button)
    {
        if (mMessage == 0 || button < 0 || static_cast<std::size_t>(button) >= mButtons.size())
            return false;
        const FormId message = mMessage;
        const std::string label = mButtons[static_cast<std::size_t>(button)];
        mMessage = 0;
        mText.clear();
        mButtons.clear();
        ++mRevision;
        ++mAnswered;
        log("STONES answer " + std::to_string(button) + " (" + label + ")");
        return !mHooks.mAnswer || mHooks.mAnswer(message, button);
    }

    openeden::sdk::DeclareResult StonesService::declare(openeden::sdk::BindingsService& service)
    {
        Impl& m = *mImpl;
        StonesService* self = this;
        static const BindingType oneInt[] = { BindingType::Int32 };
        static const BindingType twoText[] = { BindingType::String, BindingType::String };
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
        add("active", {}, true, BindingType::Bool, [self](std::span<const BindingValue>) {
            BindingResult r;
            r.value = boolean(self->messageOpen());
            return r;
        });
        add("text", {}, true, BindingType::String, [self](std::span<const BindingValue>) {
            BindingResult r;
            r.value = text(self->mText);
            return r;
        });
        add("button_count", {}, true, BindingType::Int32, [self](std::span<const BindingValue>) {
            BindingResult r;
            r.value = integer(static_cast<std::int32_t>(self->mButtons.size()));
            return r;
        });
        add("button", oneInt, true, BindingType::String, [self](std::span<const BindingValue> a) {
            BindingResult r;
            const std::int32_t n = a[0].integer;
            r.value = text(n >= 1 && static_cast<std::size_t>(n) <= self->mButtons.size()
                    ? std::string_view(self->mButtons[static_cast<std::size_t>(n - 1)])
                    : std::string_view());
            return r;
        });
        add("revision", {}, true, BindingType::Int32, [self](std::span<const BindingValue>) {
            BindingResult r;
            r.value = integer(static_cast<std::int32_t>(self->mRevision & 0x7FFFFFFF));
            return r;
        });
        add(
            "choose", oneInt, false, BindingType::Bool,
            [self](std::span<const BindingValue> a) {
                (void)self->choose(a[0].integer - 1);
                return BindingResult();
            },
            openeden::sdk::BindingAccess::Mutate);
        add(
            "activate", twoText, false, BindingType::Bool,
            [self](std::span<const BindingValue> a) {
                std::uint64_t placement = 0, base = 0;
                if (parseHex(a[0].text, placement) && parseHex(a[1].text, base))
                    (void)self->activate(static_cast<FormId>(placement), static_cast<FormId>(base));
                return BindingResult();
            },
            openeden::sdk::BindingAccess::Mutate);
        openeden::sdk::NamespaceDescriptor ns;
        ns.name = "skyrimse.messages";
        ns.version = { 1, 0 };
        ns.functions = m.mFunctions;
        return service.declareNamespace(ns);
    }
}
