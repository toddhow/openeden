#include "game_skyrimse/combat_style.hpp"

#include "exterior_common.hpp"

#include <cmath>

namespace game_skyrimse
{
    using detail::Fields;
    using detail::readLe;
    using detail::SubReader;

    namespace
    {
        // Reads `count` floats from `data` into `dest[0..count)`; false (and nothing written) when one is not finite.
        bool readFloats(std::span<const std::byte> data, std::size_t count, float* const* dest)
        {
            for (std::size_t i = 0; i < count; ++i)
                if (!std::isfinite(readLe<float>(data.data() + 4 * i)))
                    return false;
            for (std::size_t i = 0; i < count; ++i)
                *dest[i] = readLe<float>(data.data() + 4 * i);
            return true;
        }
    }

    bool decodeCombatStyle(std::span<const std::byte> bytes, const Content::RawRecord& record,
        const DecodeContext& context, CombatStyleRecord& out, std::string& why)
    {
        const Fields fields{ context, "CSTY", why };
        out = CombatStyleRecord();
        out.mCommon = fields.common(record);
        SubReader reader(bytes);
        std::uint32_t type = 0;
        std::span<const std::byte> data;
        int status = 0;
        while ((status = reader.next(type, data)) == 1)
        {
            if (type == Content::fourCC("EDID"))
            {
                if (!fields.zstring(type, data, out.mCommon.mEditorId))
                    return false;
            }
            else if (type == Content::fourCC("DATA"))
            {
                if (data.size() != 4)
                    fields.odd(type);
                else
                    out.mFlags = readLe<std::uint32_t>(data.data());
            }
            else if (type == Content::fourCC("CSGD"))
            {
                // 40 bytes; the 32 and 8 byte variants stop earlier and leave the rest at the defaults.
                float* const dest[10] = { &out.mOffensive, &out.mDefensive, &out.mGroupOffensive, &out.mMeleeScore,
                    &out.mMagicScore, &out.mRangedScore, &out.mShoutScore, &out.mUnarmedScore, &out.mStaffScore,
                    &out.mAvoidThreatChance };
                if (data.size() < 8 || data.size() > 40 || data.size() % 4 != 0
                    || !readFloats(data, data.size() / 4, dest))
                    fields.odd(type);
                else
                    out.mHasData = true;
            }
            else if (type == Content::fourCC("CSME"))
            {
                float* const dest[8] = { &out.mAttackStaggered, &out.mPowerAttackStaggered, &out.mPowerAttackBlocking,
                    &out.mBash, &out.mBashRecoil, &out.mBashAttack, &out.mBashPowerAttack, &out.mSpecialAttack };
                if ((data.size() != 28 && data.size() != 32) || !readFloats(data, data.size() / 4, dest))
                    fields.odd(type);
                else
                    out.mHasData = true;
            }
            else if (type == Content::fourCC("CSCR"))
            {
                float* const dest[4] = { &out.mCircle, &out.mFallback, &out.mFlankDistance, &out.mStalkTime };
                if ((data.size() != 8 && data.size() != 16) || !readFloats(data, data.size() / 4, dest))
                    fields.odd(type);
                else
                    out.mHasData = true;
            }
        }
        if (status < 0)
        {
            why = reader.message();
            return false;
        }
        return true;
    }
}
