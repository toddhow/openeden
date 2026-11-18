// Task 0234: SCRL records as castable spells. See scroll.hpp.
#include "game_skyrimse/scroll.hpp"

#include "exterior_common.hpp"
#include "game_skyrimse/magic.hpp"
#include "game_skyrimse/summons.hpp"

namespace game_skyrimse
{
    using detail::Fields;
    using detail::readLe;
    using detail::SubReader;

    bool decodeScroll(std::span<const std::byte> bytes, const Content::RawRecord& record, const DecodeContext& context,
        SpellRecord& out, std::string& why)
    {
        // decodeSpell skips the subrecords a SCRL has and a SPEL has not (DATA, MODL, DESC, YNAM, ZNAM, KWDA ...),
        // and a
        // scroll's SPIT has the layout of a SPEL's.
        if (!decodeSpell(bytes, record, context, out, why))
            return false;
        out.mScroll = true;
        out.mCost = 0; // a scroll costs no magicka
        if (out.mCastType == kScrollCastType)
            out.mCastType = MagicCast::FireAndForget;
        return true;
    }

    bool decodeEnchantment(std::span<const std::byte> bytes, const Content::RawRecord& record,
        const DecodeContext& context, SpellRecord& out, std::string& why)
    {
        // decodeSpell reads the effects and the editor id and ignores ENIT (a SPEL has SPIT); ENIT is read here.
        if (!decodeSpell(bytes, record, context, out, why))
            return false;
        const Fields fields{ context, "ENCH", why };
        out.mEnchantment = true;
        out.mHasData = false;
        out.mCost = 0; // a weapon or staff spends charge, not magicka
        out.mType = 0;
        SubReader reader(bytes);
        std::uint32_t type = 0;
        std::span<const std::byte> data;
        int status = 0;
        while ((status = reader.next(type, data)) == 1)
        {
            if (type != Content::fourCC("ENIT"))
                continue;
            if (data.size() != kEnchantEnitBytes && data.size() != kEnchantEnitShortBytes)
            {
                fields.odd(type);
                continue;
            }
            out.mHasData = true;
            out.mCastType = readLe<std::uint32_t>(data.data() + 8);
            out.mDelivery = readLe<std::uint32_t>(data.data() + 16);
            out.mEnchantType = readLe<std::uint32_t>(data.data() + 20);
        }
        if (status < 0)
        {
            why = reader.message();
            return false;
        }
        return true;
    }

    bool scrollArchetypeApplied(std::uint32_t archetype)
    {
        switch (archetype)
        {
            case MagicArchetype::ValueModifier:
            case MagicArchetype::PeakValueModifier:
            case MagicArchetype::Dual:
            case MagicArchetype::Absorb:
            case MagicArchetype::Stagger:
            case MagicArchetype::Demoralize:
            case MagicArchetype::AccumulateMagnitude:
                return true;
            default:
                return false;
        }
    }
}

namespace game_skyrimse
{
    bool scrollArchetypeApplied(std::uint32_t archetype, bool summons)
    {
        if (summons && (archetype == SummonArchetype::SummonCreature || archetype == SummonArchetype::Reanimate))
            return true;
        return scrollArchetypeApplied(archetype);
    }

    ScrollSupport classifyScroll(
        const SpellRecord& scroll, const std::function<const MagicEffectRecord*(FormId)>& effect)
    {
        return classifyScroll(scroll, effect, false);
    }

    ScrollSupport classifyScroll(
        const SpellRecord& scroll, const std::function<const MagicEffectRecord*(FormId)>& effect, bool summons)
    {
        std::size_t applied = 0;
        for (const SpellEffect& e : scroll.mEffects)
        {
            const MagicEffectRecord* record = effect ? effect(e.mEffect) : nullptr;
            if (record != nullptr && record->mHasData && scrollArchetypeApplied(record->mArchetype, summons))
                ++applied;
        }
        if (applied == 0)
            return ScrollSupport::None;
        return applied == scroll.mEffects.size() ? ScrollSupport::All : ScrollSupport::Some;
    }
}
