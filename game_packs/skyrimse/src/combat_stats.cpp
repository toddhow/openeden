#include "game_skyrimse/combat_stats.hpp"

#include "exterior_common.hpp"

#include "game_skyrimse/appearance.hpp"
#include "game_skyrimse/pack_core.hpp"

#include <algorithm>
#include <cmath>
#include <memory>

namespace game_skyrimse
{
    using detail::Fields;
    using detail::readLe;
    using detail::SubReader;

    namespace
    {
        namespace sdkgame = openeden::sdk::game;

        constexpr std::size_t kWeaponDataBytes = 10;
        constexpr std::size_t kWeaponDnamBytes = 100;
        constexpr std::size_t kWeaponCrdtBytes = 24;
        constexpr std::size_t kRaceDataBytes = 164;
        constexpr std::size_t kGameSettingBytes = 4;
        constexpr float kMaxSkill = 100.f;
        // A hostile float beyond this is not a game number (the data's largest damage is 10,000).
        constexpr float kSane = 1.0e9f;

        // Non-finite and negative numbers count as 0; the rest is capped so no product overflows to infinity.
        float clean(float v)
        {
            if (!std::isfinite(v) || v < 0.f)
                return 0.f;
            return std::min(v, kSane);
        }

        bool allFinite(std::initializer_list<float> values)
        {
            for (float v : values)
                if (!std::isfinite(v))
                    return false;
            return true;
        }

        // The names of the GMSTs of CombatSettings, with the member each fills.
        struct SettingName
        {
            const char* mName;
            float CombatSettings::*mMember;
        };
        constexpr SettingName kSettingNames[] = {
            { "fMaxArmorRating", &CombatSettings::mMaxArmorRating },
            { "fArmorScalingFactor", &CombatSettings::mArmorScalingFactor },
            { "fArmorBaseFactor", &CombatSettings::mArmorBaseFactor },
            { "fClothingArmorScale", &CombatSettings::mClothingArmorScale },
            { "fDamagePCSkillMax", &CombatSettings::mDamagePCSkillMax },
            { "fPowerAttackDefaultBonus", &CombatSettings::mPowerAttackDefaultBonus },
            { "fPowerAttackStaminaPenalty", &CombatSettings::mPowerAttackStaminaPenalty },
            { "fStaminaAttackWeaponBase", &CombatSettings::mStaminaAttackWeaponBase },
            { "fStaminaAttackWeaponMult", &CombatSettings::mStaminaAttackWeaponMult },
            { "fActionPointsAttackUnarmed", &CombatSettings::mActionPointsAttackUnarmed },
            { "fCombatDistance", &CombatSettings::mCombatDistance },
            { "fCombatHealthRegenRateMult", &CombatSettings::mCombatHealthRegenRateMult },
            { "fCombatMagickaRegenRateMult", &CombatSettings::mCombatMagickaRegenRateMult },
            { "fCombatStaminaRegenRateMult", &CombatSettings::mCombatStaminaRegenRateMult },
        };

        std::string lower(const std::string& s)
        {
            std::string out = s;
            for (char& c : out)
                c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
            return out;
        }
    }

    // ---- decoders --------------------------------------------------------------------------------------------------

    bool decodeWeapon(std::span<const std::byte> bytes, const Content::RawRecord& record, const DecodeContext& context,
        WeaponRecord& out, std::string& why)
    {
        const Fields fields{ context, "WEAP", why };
        out = WeaponRecord();
        out.mCommon = fields.common(record);
        SubReader reader(bytes);
        std::uint32_t type = 0;
        std::span<const std::byte> data;
        int status = 0;
        while ((status = reader.next(type, data)) == 1)
        {
            switch (type)
            {
                case Content::fourCC("EDID"):
                    if (!fields.zstring(type, data, out.mCommon.mEditorId))
                        return false;
                    break;
                case Content::fourCC("DATA"):
                    if (data.size() != kWeaponDataBytes)
                    {
                        fields.odd(type);
                        break;
                    }
                    {
                        const float weight = readLe<float>(data.data() + 4);
                        if (!std::isfinite(weight))
                        {
                            fields.odd(type);
                            break;
                        }
                        out.mHasData = true;
                        out.mValue = readLe<std::uint32_t>(data.data());
                        out.mWeight = weight;
                        out.mDamage = readLe<std::uint16_t>(data.data() + 8);
                    }
                    break;
                case Content::fourCC("DNAM"):
                    if (data.size() != kWeaponDnamBytes)
                    {
                        fields.odd(type);
                        break;
                    }
                    {
                        const std::byte* p = data.data();
                        const float speed = readLe<float>(p + 4);
                        const float reach = readLe<float>(p + 8);
                        const float stagger = readLe<float>(p + 96);
                        if (!allFinite({ speed, reach, stagger }))
                        {
                            fields.odd(type);
                            break;
                        }
                        out.mHasDnam = true;
                        out.mAnimType = std::to_integer<std::uint8_t>(p[0]);
                        out.mSpeed = speed;
                        out.mReach = reach;
                        out.mFlags = readLe<std::uint16_t>(p + 12);
                        out.mSkillValue = readLe<std::uint32_t>(p + 76);
                        out.mStagger = stagger;
                    }
                    break;
                case Content::fourCC("CRDT"):
                    if (data.size() != kWeaponCrdtBytes)
                    {
                        fields.odd(type);
                        break;
                    }
                    {
                        const float multiplier = readLe<float>(data.data() + 4);
                        if (!std::isfinite(multiplier))
                        {
                            fields.odd(type);
                            break;
                        }
                        out.mHasCrit = true;
                        out.mCritDamage = readLe<std::uint16_t>(data.data());
                        out.mCritMultiplier = multiplier;
                    }
                    break;
                default:
                    break;
            }
        }
        if (status < 0)
        {
            why = reader.message();
            return false;
        }
        return true;
    }

    bool decodeRaceCombat(std::span<const std::byte> bytes, const Content::RawRecord& record,
        const DecodeContext& context, RaceCombatRecord& out, std::string& why)
    {
        const Fields fields{ context, "RACE", why };
        out = RaceCombatRecord();
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
                if (data.size() != kRaceDataBytes)
                {
                    fields.odd(type);
                    continue;
                }
                const std::byte* p = data.data();
                const float h = readLe<float>(p + 84), m = readLe<float>(p + 88), s = readLe<float>(p + 92),
                            d = readLe<float>(p + 96), r = readLe<float>(p + 100);
                if (!allFinite({ h, m, s, d, r }))
                {
                    fields.odd(type);
                    continue;
                }
                out.mHasData = true;
                out.mHealthRegen = h;
                out.mMagickaRegen = m;
                out.mStaminaRegen = s;
                out.mUnarmedDamage = d;
                out.mUnarmedReach = r;
            }
        }
        if (status < 0)
        {
            why = reader.message();
            return false;
        }
        return true;
    }

    bool decodeGameSetting(std::span<const std::byte> bytes, const Content::RawRecord& record,
        const DecodeContext& context, GameSetting& out, std::string& why)
    {
        const Fields fields{ context, "GMST", why };
        out = GameSetting();
        out.mCommon = fields.common(record);
        SubReader reader(bytes);
        std::uint32_t type = 0;
        std::span<const std::byte> data;
        int status = 0;
        std::span<const std::byte> value;
        bool hasValue = false;
        while ((status = reader.next(type, data)) == 1)
        {
            if (type == Content::fourCC("EDID"))
            {
                if (!fields.zstring(type, data, out.mCommon.mEditorId))
                    return false;
            }
            else if (type == Content::fourCC("DATA"))
            {
                value = data;
                hasValue = true;
            }
        }
        if (status < 0)
        {
            why = reader.message();
            return false;
        }
        if (!hasValue || out.mCommon.mEditorId.empty())
            return true;
        const char kind = out.mCommon.mEditorId[0];
        if (kind != 'f' && kind != 'i' && kind != 'b')
            return true; // a string or a leftover: not numeric
        if (value.size() != kGameSettingBytes)
        {
            fields.odd(Content::fourCC("DATA"));
            return true;
        }
        if (kind == 'f')
        {
            const float v = readLe<float>(value.data());
            if (!std::isfinite(v))
            {
                fields.odd(Content::fourCC("DATA"));
                return true;
            }
            out.mValue = v;
        }
        else if (kind == 'i')
            out.mValue = static_cast<float>(readLe<std::int32_t>(value.data()));
        else
            out.mValue = readLe<std::uint32_t>(value.data()) != 0 ? 1.f : 0.f;
        out.mNumeric = true;
        return true;
    }

    // ---- the walk --------------------------------------------------------------------------------------------------

    namespace
    {
        class Walker final : public Content::Visitor
        {
        public:
            Walker(const Content::Plugin& plugin, const FormIdResolver& resolver, const ExteriorLimits& limits,
                CombatRecords& out)
                : mPlugin(plugin)
                , mLimits(limits)
                , mOut(out)
                , mContext{ resolver, limits, out.mNotes, plugin.header().isLocalized() }
            {
            }

            bool onRecord(const Content::RawRecord& record) override
            {
                const std::uint32_t t = record.mType;
                if (t != Content::fourCC("WEAP") && t != Content::fourCC("RACE") && t != Content::fourCC("GMST"))
                    return true;
                std::span<const std::byte> bytes = record.mData;
                if (record.isCompressed())
                {
                    if (record.mUncompressedSize > mLimits.mMaxRecordBytes)
                    {
                        fail(record,
                            "the record inflates to " + std::to_string(record.mUncompressedSize)
                                + " bytes, over the limit of " + std::to_string(mLimits.mMaxRecordBytes));
                        return true;
                    }
                    Content::Error error;
                    if (!inflateRecord(record, mPlugin.name(), mInflated, error))
                    {
                        addError(std::move(error));
                        return true;
                    }
                    bytes = mInflated;
                }
                else if (record.mDataSize > mLimits.mMaxRecordBytes)
                {
                    fail(record,
                        "the record is " + std::to_string(record.mDataSize) + " bytes, over the limit of "
                            + std::to_string(mLimits.mMaxRecordBytes));
                    return true;
                }
                std::string why;
                bool ok = true;
                if (t == Content::fourCC("WEAP"))
                {
                    WeaponRecord w;
                    ok = decodeWeapon(bytes, record, mContext, w, why);
                    if (ok)
                        mOut.mWeapons.push_back(std::move(w));
                }
                else if (t == Content::fourCC("RACE"))
                {
                    RaceCombatRecord r;
                    ok = decodeRaceCombat(bytes, record, mContext, r, why);
                    if (ok)
                        mOut.mRaces.push_back(std::move(r));
                }
                else
                {
                    GameSetting g;
                    ok = decodeGameSetting(bytes, record, mContext, g, why);
                    if (ok && g.mNumeric)
                        mOut.mSettings.push_back(std::move(g));
                }
                if (!ok)
                    fail(record, why);
                return true;
            }

        private:
            void addError(Content::Error error)
            {
                if (mOut.mErrors.size() < mLimits.mMaxErrors)
                    mOut.mErrors.push_back(std::move(error));
                else
                    ++mOut.mErrorsDropped;
            }

            void fail(const Content::RawRecord& record, std::string message)
            {
                Content::Error error;
                error.mFile = mPlugin.name();
                error.mOffset = record.mOffset;
                error.mRecordType = record.mType;
                error.mMessage = std::move(message);
                addError(std::move(error));
            }

            const Content::Plugin& mPlugin;
            const ExteriorLimits& mLimits;
            CombatRecords& mOut;
            DecodeContext mContext;
            std::vector<std::byte> mInflated;
        };
    }

    bool parseCombatRecords(
        const Content::Plugin& plugin, const LoadOrder& order, const ExteriorLimits& limits, CombatRecords& out)
    {
        out = CombatRecords();
        out.mName = plugin.name();
        const FormIdResolver resolver(plugin.header(), plugin.name(), order);
        Walker walker(plugin, resolver, limits, out);
        const std::optional<Content::Error> error = plugin.walk(walker);
        if (error)
        {
            out.mWalkError = *error;
            return false;
        }
        return true;
    }

    // ---- tables ----------------------------------------------------------------------------------------------------

    void SettingsTable::add(const CombatRecords& records)
    {
        for (const GameSetting& g : records.mSettings)
            if (g.mNumeric && !g.mCommon.deleted())
                mValues[lower(g.mCommon.mEditorId)] = g.mValue;
    }

    void SettingsTable::set(const std::string& name, float value)
    {
        mValues[lower(name)] = value;
    }

    bool SettingsTable::has(const std::string& name) const
    {
        return mValues.find(lower(name)) != mValues.end();
    }

    float SettingsTable::get(const std::string& name, float fallback) const
    {
        const auto it = mValues.find(lower(name));
        return it == mValues.end() ? fallback : it->second;
    }

    CombatSettings SettingsTable::combatSettings(std::vector<std::string>* missing) const
    {
        CombatSettings settings;
        for (const SettingName& entry : kSettingNames)
        {
            const auto it = mValues.find(lower(entry.mName));
            if (it != mValues.end())
                settings.*entry.mMember = it->second;
            else if (missing != nullptr)
                missing->push_back(entry.mName);
        }
        return settings;
    }

    void CombatIndex::add(const CombatRecords& records)
    {
        for (const WeaponRecord& w : records.mWeapons)
        {
            if (w.mCommon.deleted())
                mWeapons.erase(w.mCommon.mId);
            else
                mWeapons[w.mCommon.mId] = w;
        }
        for (const RaceCombatRecord& r : records.mRaces)
        {
            if (r.mCommon.deleted())
                mRaces.erase(r.mCommon.mId);
            else
                mRaces[r.mCommon.mId] = r;
        }
        mSettings.add(records);
    }

    const WeaponRecord* CombatIndex::weapon(FormId id) const
    {
        const auto it = mWeapons.find(id);
        return it == mWeapons.end() ? nullptr : &it->second;
    }

    const RaceCombatRecord* CombatIndex::race(FormId id) const
    {
        const auto it = mRaces.find(id);
        return it == mRaces.end() ? nullptr : &it->second;
    }

    std::vector<FormId> CombatIndex::weaponIds() const
    {
        std::vector<FormId> ids;
        ids.reserve(mWeapons.size());
        for (const auto& entry : mWeapons)
            ids.push_back(entry.first);
        std::sort(ids.begin(), ids.end());
        return ids;
    }

    // ---- armour ----------------------------------------------------------------------------------------------------

    ArmourResult armourOf(const std::vector<ArmourPiece>& pieces,
        const std::array<std::uint8_t, sdkgame::kActorSkillCount>& skills, const CombatSettings& settings)
    {
        constexpr std::size_t kHeavySlot = 5, kLightSlot = 6; // ActorStats::skills order
        ArmourResult result;
        double rating = 0.0;
        std::uint32_t seen = 0; // main slots already counted
        for (const ArmourPiece& piece : pieces)
        {
            const float base = clean(piece.mRating);
            float scaled = base;
            if (piece.mType == 0 || piece.mType == 1)
            {
                const float skill
                    = std::min(static_cast<float>(skills[piece.mType == 0 ? kLightSlot : kHeavySlot]), kMaxSkill);
                scaled = base * (1.f + skill / 200.f); // RECALLED-VERIFY
            }
            else
                scaled = base * clean(settings.mClothingArmorScale);
            rating += scaled;
            if (base > 0.f)
                seen |= piece.mSlotMask & ArmourSlot::MainPieces;
        }
        for (std::uint32_t bit : { ArmourSlot::Head, ArmourSlot::Body, ArmourSlot::Hands, ArmourSlot::Feet })
            if ((seen & bit) != 0)
                ++result.mMainPieces;
        result.mRating = static_cast<float>(std::min(rating, static_cast<double>(kSane)));
        const double percent = static_cast<double>(result.mRating) * clean(settings.mArmorScalingFactor)
            + 100.0 * clean(settings.mArmorBaseFactor) * result.mMainPieces;
        result.mMitigationPercent
            = static_cast<float>(std::min(percent, static_cast<double>(clean(settings.mMaxArmorRating))));
        return result;
    }

    bool wornArmour(const ActorIndex& index, FormId npc, std::uint32_t variation, std::uint16_t playerLevel,
        std::vector<ArmourPiece>& out, std::string& error)
    {
        out.clear();
        sdkgame::ActorStats stats;
        if (!computeActorStats(index, npc, variation, playerLevel, stats, error))
            return false;
        ActorIndex::Resolved resolved;
        if (index.resolve(npc, kAppearanceLevel, variation, resolved) != ActorIndex::ResolveStatus::Ok)
        {
            error = "the NPC cannot be resolved";
            return false;
        }
        const Sex sex = stats.female ? Sex::Female : Sex::Male;
        std::vector<FormId> seen;
        for (const ActorIndex::WornModel& worn :
            index.wornModels(resolved.mDefaultOutfit, resolved.mRace, sex, kAppearanceLevel, variation))
        {
            if (std::find(seen.begin(), seen.end(), worn.mArmor) != seen.end())
                continue;
            seen.push_back(worn.mArmor);
            const ArmorExtra* armor = index.armor(worn.mArmor);
            if (armor == nullptr)
                continue;
            out.push_back({ static_cast<float>(armor->mRating) / 100.f, armor->mSlotMask, armor->mArmorType });
        }
        error.clear();
        return true;
    }

    bool computeActorArmour(const ActorIndex& index, const CombatSettings& settings, FormId npc,
        std::uint32_t variation, std::uint16_t playerLevel, ArmourResult& out, std::string& error)
    {
        out = {};
        std::vector<ArmourPiece> pieces;
        if (!wornArmour(index, npc, variation, playerLevel, pieces, error))
            return false;
        sdkgame::ActorStats stats;
        if (!computeActorStats(index, npc, variation, playerLevel, stats, error))
            return false;
        out = armourOf(pieces, stats.skills, settings);
        return true;
    }

    // ---- a melee hit -----------------------------------------------------------------------------------------------

    MeleeHit computeMeleeHit(const MeleeInput& input, const CombatSettings& settings)
    {
        MeleeHit hit;
        const float powerFactor = input.mPowerAttack ? 1.f + clean(settings.mPowerAttackDefaultBonus) : 1.f;
        float base = 0.f;
        if (input.mWeapon != nullptr)
        {
            const float skill = std::min(clean(input.mAttackerSkill), kMaxSkill);
            const float skillMult = std::min(clean(settings.mDamagePCSkillMax), 1.f + skill / 200.f);
            base = static_cast<float>(input.mWeapon->mDamage) * skillMult;
            hit.mStaminaCost = clean(settings.mStaminaAttackWeaponBase)
                + clean(input.mWeapon->mWeight) * clean(settings.mStaminaAttackWeaponMult);
            hit.mStagger = clean(input.mWeapon->mStagger);
            if (input.mWeapon->mHasCrit)
                hit.mCriticalDamage
                    = static_cast<float>(input.mWeapon->mCritDamage) * clean(input.mWeapon->mCritMultiplier);
        }
        else
        {
            base = clean(input.mUnarmedDamage);
            hit.mStaminaCost = clean(settings.mActionPointsAttackUnarmed);
        }
        if (input.mPowerAttack)
            hit.mStaminaCost *= clean(settings.mPowerAttackStaminaPenalty);
        hit.mDamageBefore = clean(base * powerFactor);
        const float mitigation = std::min(clean(input.mTargetMitigationPercent), clean(settings.mMaxArmorRating));
        // Never negative and never more than the blow itself, whatever the mitigation says (a negative one is data).
        hit.mDamageAfter = std::clamp(hit.mDamageBefore * (1.f - mitigation / 100.f), 0.f, hit.mDamageBefore);
        hit.mStaminaCost = clean(hit.mStaminaCost);
        return hit;
    }

    float meleeReachUnits(const WeaponRecord* weapon, float unarmedReach, const CombatSettings& settings)
    {
        if (weapon == nullptr)
            return clean(unarmedReach);
        return clean(weapon->mReach) * clean(settings.mCombatDistance);
    }

    float regenAmount(float maximum, float ratePercentPerSecond, float combatMultiplier, bool inCombat, float seconds)
    {
        const float multiplier = inCombat ? clean(combatMultiplier) : 1.f;
        return clean(maximum) * clean(ratePercentPerSecond) / 100.f * clean(seconds) * multiplier;
    }
}
