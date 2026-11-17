#include "game_skyrimse/diseases.hpp"

#include <algorithm>
#include <cmath>

namespace game_skyrimse::diseases
{
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

        bool startsWith(std::string_view s, std::string_view prefix)
        {
            return s.size() >= prefix.size() && s.substr(0, prefix.size()) == prefix;
        }

        std::string hexName(FormId id)
        {
            static const char digits[] = "0123456789abcdef";
            std::string out = "Disease ";
            for (int shift = 28; shift >= 0; shift -= 4)
                out.push_back(digits[(id >> shift) & 0xF]);
            return out;
        }

        /// The weapon skills whose damage a skill percent scales: One-handed, Two-handed, Archery (slots 0 to 2).
        constexpr std::int32_t kDamageSkillSlots = 3;

        /// Adds `amount` to what actor value `value` changes; false for a value this runtime does not change.
        bool addTo(PlayerValueModifiers& m, std::int32_t value, float amount, bool extended = false)
        {
            if (extended)
            {
                if (value == 24)
                {
                    m.mHealthMax += amount; // task 0248
                    return true;
                }
                if (value == 32)
                {
                    m.mCarry += amount;
                    return true;
                }
                const std::int32_t any = value - kFirstSkillModifier;
                if (any >= 0 && any < static_cast<std::int32_t>(m.mSkillPercent.size()))
                {
                    m.mSkillPercent[static_cast<std::size_t>(any)] += amount;
                    return true;
                }
            }
            switch (value)
            {
                case kValueMagicka:
                    m.mMagickaMax += amount;
                    return true;
                case kValueStamina:
                    m.mStaminaMax += amount;
                    return true;
                case kValueMagickaRate:
                    m.mMagickaRegen += amount;
                    return true;
                case kValueStaminaRate:
                    m.mStaminaRegen += amount;
                    return true;
                default:
                    break;
            }
            const std::int32_t slot = value - kFirstSkillModifier;
            if (slot >= 0 && slot < kDamageSkillSlots)
            {
                m.mSkillPercent[static_cast<std::size_t>(slot)] += amount;
                return true;
            }
            return false;
        }
    }

    struct DiseaseService::Impl
    {
        std::vector<std::unique_ptr<Fn>> mHandlers;
        std::vector<openeden::sdk::FunctionDescriptor> mFunctions;
        std::unique_ptr<openeden::sdk::SaveChunkHandler> mChunk;
        std::string mScratch; ///< what a binding returns lives here until the next call
    };

    std::string displayName(std::string_view editorId)
    {
        std::string_view rest = editorId;
        for (const std::string_view prefix :
            { std::string_view("TrapDisease"), std::string_view("DLC2Disease"), std::string_view("Disease") })
            if (startsWith(rest, prefix) && rest.size() > prefix.size())
            {
                rest.remove_prefix(prefix.size());
                break;
            }
        std::string out;
        for (std::size_t i = 0; i < rest.size() && out.size() < kMaxNameBytes; ++i)
        {
            const char c = rest[i];
            const bool upper = c >= 'A' && c <= 'Z';
            const bool prevLower
                = i > 0 && ((rest[i - 1] >= 'a' && rest[i - 1] <= 'z') || (rest[i - 1] >= '0' && rest[i - 1] <= '9'));
            if (upper && prevLower)
                out.push_back(' ');
            out.push_back(c);
        }
        return out;
    }

    bool isCureDiseaseEffect(const PerkLibrary& library, FormId effect)
    {
        const MagicEffectRecord* magic = library.magicEffect(effect);
        return magic != nullptr && magic->mHasData && magic->mArchetype == kArchetypeCureDisease;
    }

    bool hasCureDisease(const PerkLibrary& library, FormId spell)
    {
        const SpellRecord* record = library.spell(spell);
        if (record == nullptr)
            return false;
        return std::any_of(record->mEffects.begin(), record->mEffects.end(),
            [&](const SpellEffect& e) { return isCureDiseaseEffect(library, e.mEffect); });
    }

    namespace
    {
        PlayerValueModifiers modifiersImpl(const PerkLibrary& library, std::span<const FormId> diseaseSpells,
            std::vector<std::string>* ignored, bool extended)
        {
            PlayerValueModifiers out;
            for (const FormId id : diseaseSpells)
            {
                const SpellRecord* spell = library.spell(id);
                if (spell == nullptr || !spell->isDisease())
                    continue;
                for (const SpellEffect& effect : spell->mEffects)
                {
                    const MagicEffectRecord* magic = library.magicEffect(effect.mEffect);
                    const std::string where = displayName(spell->mCommon.mEditorId);
                    if (magic == nullptr || !magic->mHasData
                        || !(magic->mArchetype == MagicArchetype::ValueModifier
                            || magic->mArchetype == MagicArchetype::Dual
                            || magic->mArchetype == MagicArchetype::PeakValueModifier))
                    {
                        if (ignored != nullptr)
                            ignored->push_back(where + ": an effect of another archetype");
                        continue;
                    }
                    const float amount = signedMagnitude(effect.mMagnitude, magic->mFlags);
                    bool all = addTo(out, magic->mPrimaryValue, amount, extended);
                    if (magic->mArchetype == MagicArchetype::Dual && magic->mSecondaryValue >= 0)
                        all = addTo(out, magic->mSecondaryValue, amount * magic->mSecondaryWeight, extended) && all;
                    if (!all && ignored != nullptr)
                        ignored->push_back(where + ": actor value " + std::to_string(magic->mPrimaryValue)
                            + " is carried and changes nothing");
                }
            }
            return out;
        }
    }

    PlayerValueModifiers modifiersOf(
        const PerkLibrary& library, std::span<const FormId> diseaseSpells, std::vector<std::string>* ignored)
    {
        return modifiersImpl(library, diseaseSpells, ignored, false);
    }

    PlayerValueModifiers modifiersOfExtended(
        const PerkLibrary& library, std::span<const FormId> diseaseSpells, std::vector<std::string>* ignored)
    {
        return modifiersImpl(library, diseaseSpells, ignored, true);
    }

    float resistanceOf(const PerkLibrary& library, std::span<const FormId> spells)
    {
        float sum = 0.f;
        for (const FormId id : spells)
        {
            const SpellRecord* spell = library.spell(id);
            if (spell == nullptr || !spell->isAbility())
                continue;
            for (const SpellEffect& effect : spell->mEffects)
            {
                const MagicEffectRecord* magic = library.magicEffect(effect.mEffect);
                if (magic != nullptr && magic->modifiesAValue() && magic->mPrimaryValue == kValueResistDisease
                    && std::isfinite(effect.mMagnitude))
                    sum += effect.mMagnitude;
            }
        }
        return sum;
    }

    float infectionChance(float baseChance, float resistPercent)
    {
        if (!std::isfinite(baseChance))
            return 0.f;
        const float base = std::clamp(baseChance, 0.f, 1.f);
        const float resist = std::isfinite(resistPercent) ? std::clamp(resistPercent, 0.f, 100.f) : 0.f;
        return base * (1.f - resist / 100.f);
    }

    float rollUnit(std::uint64_t seed, std::uint64_t counter)
    {
        std::uint64_t z = seed + counter * 0x9E3779B97F4A7C15ull;
        z = (z ^ (z >> 30)) * 0xBF58476D1CE4E5B9ull;
        z = (z ^ (z >> 27)) * 0x94D049BB133111EBull;
        z ^= z >> 31;
        return static_cast<float>(z >> 40) / 16777216.f; // 24 bits: exactly representable, below 1
    }

    FormId diseaseOfAttacks(const PerkLibrary& library, std::span<const FormId> attackSpells)
    {
        for (const FormId id : attackSpells)
        {
            const SpellRecord* spell = library.spell(id);
            if (spell != nullptr && spell->isDisease())
                return id;
        }
        return kNullFormId;
    }

    // ---- the chunk
    // ---------------------------------------------------------------------------------------------------------

    namespace
    {
        class DiseaseChunk final : public openeden::sdk::SaveChunkHandler
        {
        public:
            explicit DiseaseChunk(DiseaseService& service)
                : mService(service)
            {
            }
            std::string_view chunkName() const override { return "diseases"; }
            std::uint32_t chunkVersion() const override { return 1; }
            void write(openeden::sdk::ChunkWriter& writer) override
            {
                const std::vector<FormId>& list = mService.infected();
                writer.writeValue(static_cast<std::uint32_t>(list.size()));
                for (const FormId id : list)
                    writer.writeValue(static_cast<std::uint32_t>(id));
                writer.writeValue(static_cast<std::uint64_t>(mService.rolls()));
            }
            bool read(openeden::sdk::ChunkReader& reader, std::uint32_t version) override
            {
                std::uint32_t count = 0;
                if (version != 1 || !reader.readValue(count) || count > kMaxDiseases
                    || reader.remaining()
                        != static_cast<std::size_t>(count) * sizeof(std::uint32_t) + sizeof(std::uint64_t))
                    return false;
                std::vector<FormId> list;
                for (std::uint32_t i = 0; i < count; ++i)
                {
                    std::uint32_t id = 0;
                    if (!reader.readValue(id) || id == 0 || std::find(list.begin(), list.end(), id) != list.end())
                        return false;
                    list.push_back(id);
                }
                std::uint64_t rolls = 0;
                if (!reader.readValue(rolls))
                    return false;
                mService.restore(std::move(list), rolls);
                return true;
            }

        private:
            DiseaseService& mService;
        };
    }

    // ---- the service
    // -------------------------------------------------------------------------------------------------------

    DiseaseService::DiseaseService(Hooks hooks, Settings settings)
        : mImpl(std::make_unique<Impl>())
        , mHooks(std::move(hooks))
        , mSettings(settings)
    {
        mImpl->mChunk = std::make_unique<DiseaseChunk>(*this);
        mNotifications.assign(kMaxNotifications, std::string());
    }

    DiseaseService::~DiseaseService() = default;

    openeden::sdk::SaveChunkHandler& DiseaseService::chunk()
    {
        return *mImpl->mChunk;
    }

    void DiseaseService::log(const std::string& line) const
    {
        if (mHooks.mLog)
            mHooks.mLog(line);
    }

    void DiseaseService::note(std::string text)
    {
        mNotifications[mNotificationCount % kMaxNotifications] = std::move(text);
        ++mNotificationCount;
    }

    std::string DiseaseService::notificationAt(std::uint64_t number) const
    {
        if (number < 1 || number > mNotificationCount || mNotificationCount - number >= kMaxNotifications)
            return {};
        return mNotifications[(number - 1) % kMaxNotifications];
    }

    bool DiseaseService::has(FormId disease) const
    {
        return std::find(mInfected.begin(), mInfected.end(), disease) != mInfected.end();
    }

    std::string DiseaseService::nameOf(FormId disease) const
    {
        const PerkLibrary* library = mHooks.mLibrary ? mHooks.mLibrary() : nullptr;
        const SpellRecord* spell = library != nullptr ? library->spell(disease) : nullptr;
        if (spell == nullptr || spell->mCommon.mEditorId.empty())
            return hexName(disease);
        return displayName(spell->mCommon.mEditorId);
    }

    bool DiseaseService::infect(FormId disease)
    {
        const PerkLibrary* library = mHooks.mLibrary ? mHooks.mLibrary() : nullptr;
        if (library == nullptr)
            return false;
        const SpellRecord* spell = library->spell(disease);
        if (spell == nullptr || !spell->isDisease() || has(disease) || mInfected.size() >= kMaxDiseases)
            return false;
        mInfected.push_back(disease);
        ++mRevision;
        const std::string name = nameOf(disease);
        note("You have contracted " + name);
        log("DISEASE contracted " + spell->mCommon.mEditorId + " (" + name + ")");
        return true;
    }

    std::size_t DiseaseService::cure()
    {
        const std::size_t count = mInfected.size();
        if (count == 0)
            return 0;
        for (const FormId id : mInfected)
        {
            const std::string name = nameOf(id);
            note("You have been cured of " + name);
            log("DISEASE cured " + name);
        }
        mInfected.clear();
        ++mRevision;
        return count;
    }

    bool DiseaseService::cureOne(FormId disease, bool announce)
    {
        const auto it = std::find(mInfected.begin(), mInfected.end(), disease);
        if (it == mInfected.end())
            return false;
        const std::string name = nameOf(disease);
        mInfected.erase(it);
        ++mRevision;
        if (announce)
            note("You have been cured of " + name);
        log("DISEASE cured " + name);
        return true;
    }

    DiseaseService::HitResult DiseaseService::onHit(std::span<const FormId> attackSpells, std::uint32_t attacker)
    {
        const PerkLibrary* library = mHooks.mLibrary ? mHooks.mLibrary() : nullptr;
        if (library == nullptr)
            return HitResult::NotReady;
        const FormId disease = diseaseOfAttacks(*library, attackSpells);
        if (disease == kNullFormId)
            return HitResult::NoDisease;
        if (has(disease))
            return HitResult::Already;
        const std::vector<FormId> spells = mHooks.mPlayerSpells ? mHooks.mPlayerSpells() : std::vector<FormId>();
        const float resist = resistanceOf(*library, spells);
        if (resist >= 100.f)
        {
            log("DISEASE blow of " + std::to_string(attacker) + " carrying " + nameOf(disease) + ": immune (resistance "
                + std::to_string(resist) + ")");
            return HitResult::Immune;
        }
        const float chance = infectionChance(mSettings.mChance, resist);
        const float roll = rollUnit(mSettings.mSeed, mRolls);
        ++mRolls;
        const bool hit = roll < chance;
        log("DISEASE blow of " + std::to_string(attacker) + " carrying " + nameOf(disease) + ": roll "
            + std::to_string(roll) + " chance " + std::to_string(chance) + (hit ? " infected" : " missed"));
        return hit && infect(disease) ? HitResult::Infected : HitResult::Missed;
    }

    void DiseaseService::poll()
    {
        if (mSentRevision == mRevision)
            return;
        const PerkLibrary* library = mHooks.mLibrary ? mHooks.mLibrary() : nullptr;
        if (library == nullptr || !mHooks.mApply)
            return;
        std::vector<std::string> ignored;
        const PlayerValueModifiers modifiers = modifiersOfExtended(*library, mInfected, &ignored); // task 0248
        mHooks.mApply(modifiers);
        mSentRevision = mRevision;
        for (const std::string& line : ignored)
            log("DISEASE carried, not applied: " + line);
        log("DISEASE modifiers: stamina max " + std::to_string(modifiers.mStaminaMax) + ", magicka max "
            + std::to_string(modifiers.mMagickaMax) + ", stamina regen " + std::to_string(modifiers.mStaminaRegen)
            + ", magicka regen " + std::to_string(modifiers.mMagickaRegen) + ", one-handed damage "
            + std::to_string(modifiers.mSkillPercent[0]) + " percent, two-handed "
            + std::to_string(modifiers.mSkillPercent[1]) + " percent");
    }

    void DiseaseService::restore(std::vector<FormId> infected, std::uint64_t rolls)
    {
        mInfected = std::move(infected);
        mRolls = rolls;
        ++mRevision;
    }

    openeden::sdk::DeclareResult DiseaseService::declare(openeden::sdk::BindingsService& service)
    {
        Impl& m = *mImpl;
        DiseaseService* self = this;
        static const BindingType oneInt[] = { BindingType::Int32 };
        const auto add
            = [&](std::string_view name, std::span<const BindingType> params, BindingType result, Fn::Body body) {
                  m.mHandlers.push_back(std::make_unique<Fn>(std::move(body)));
                  openeden::sdk::FunctionDescriptor f;
                  f.name = name;
                  f.params = params;
                  f.hasResult = true;
                  f.result = result;
                  f.access = openeden::sdk::BindingAccess::Read;
                  f.handler = m.mHandlers.back().get();
                  m.mFunctions.push_back(f);
              };
        add("count", {}, BindingType::Int32, [self](std::span<const BindingValue>) {
            BindingResult r;
            r.value = integer(static_cast<std::int32_t>(self->mInfected.size()));
            return r;
        });
        add("name", oneInt, BindingType::String, [self](std::span<const BindingValue> a) {
            BindingResult r;
            const std::int32_t n = a[0].integer;
            self->mImpl->mScratch = n >= 1 && static_cast<std::size_t>(n) <= self->mInfected.size()
                ? self->nameOf(self->mInfected[static_cast<std::size_t>(n - 1)])
                : std::string();
            r.value = text(self->mImpl->mScratch);
            return r;
        });
        add("revision", {}, BindingType::Int32, [self](std::span<const BindingValue>) {
            BindingResult r;
            r.value = integer(static_cast<std::int32_t>(self->mRevision & 0x7FFFFFFF));
            return r;
        });
        add("notification_count", {}, BindingType::Int32, [self](std::span<const BindingValue>) {
            BindingResult r;
            r.value = integer(static_cast<std::int32_t>(self->mNotificationCount & 0x7FFFFFFF));
            return r;
        });
        add("notification", oneInt, BindingType::String, [self](std::span<const BindingValue> a) {
            BindingResult r;
            self->mImpl->mScratch
                = a[0].integer >= 1 ? self->notificationAt(static_cast<std::uint64_t>(a[0].integer)) : std::string();
            r.value = text(self->mImpl->mScratch);
            return r;
        });
        openeden::sdk::NamespaceDescriptor ns;
        ns.name = "skyrimse.diseases";
        ns.version = { 1, 0 };
        ns.functions = m.mFunctions;
        return service.declareNamespace(ns);
    }
}
