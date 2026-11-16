// Task 0234: SCRL records as castable spells. See scroll.hpp.
#include "game_skyrimse/scroll.hpp"

#include "game_skyrimse/magic.hpp"

namespace game_skyrimse
{
    bool decodeScroll(std::span<const std::byte> bytes, const Content::RawRecord& record,
        const DecodeContext& context, SpellRecord& out, std::string& why)
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
    ScrollSupport classifyScroll(
        const SpellRecord& scroll, const std::function<const MagicEffectRecord*(FormId)>& effect)
    {
        std::size_t applied = 0;
        for (const SpellEffect& e : scroll.mEffects)
        {
            const MagicEffectRecord* record = effect ? effect(e.mEffect) : nullptr;
            if (record != nullptr && record->mHasData && scrollArchetypeApplied(record->mArchetype))
                ++applied;
        }
        if (applied == 0)
            return ScrollSupport::None;
        return applied == scroll.mEffects.size() ? ScrollSupport::All : ScrollSupport::Some;
    }
}
