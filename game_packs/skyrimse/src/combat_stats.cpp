#include "game_skyrimse/combat_stats.hpp"

#include "exterior_common.hpp"

#include "game_skyrimse/appearance.hpp"
#include "game_skyrimse/pack_core.hpp"
#include "game_skyrimse/scroll.hpp"

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
        constexpr std::size_t kProjectileDataBytes = 92;
        constexpr std::size_t kAmmoDataBytes = 20; // VERIFIED on Skyrim.esm: every AMMO DATA is 20 bytes
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
                        const float minRange = readLe<float>(p + 28);
                        const float maxRange = readLe<float>(p + 32);
                        if (!allFinite({ speed, reach, stagger, minRange, maxRange }))
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
                        out.mMinRange = minRange;
                        out.mMaxRange = maxRange;
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
                case Content::fourCC("INAM"): // task 0193: the sounds and impact sets (4 byte form ids)
                case Content::fourCC("BIDS"):
                case Content::fourCC("BAMT"):
                case Content::fourCC("TNAM"):
                case Content::fourCC("NAM9"):
                case Content::fourCC("NAM8"):
                case Content::fourCC("SNAM"):
                {
                    if (data.size() != 4)
                    {
                        fields.odd(type);
                        break;
                    }
                    const FormId id = fields.resolved(readLe<std::uint32_t>(data.data()));
                    FormId& slot = type == Content::fourCC("INAM") ? out.mImpactSet
                        : type == Content::fourCC("BIDS")          ? out.mBashImpactSet
                        : type == Content::fourCC("BAMT")          ? out.mBlockMaterial
                        : type == Content::fourCC("TNAM")          ? out.mSwingSound
                        : type == Content::fourCC("NAM9")          ? out.mDrawSound
                        : type == Content::fourCC("NAM8")          ? out.mSheatheSound
                                                                   : out.mAttackSound;
                    slot = id;
                    break;
                }
                case Content::fourCC("KWDA"): // task 0179: the keywords the perk conditions ask about
                    if (data.size() % 4 != 0 || data.size() / 4 > 64)
                    {
                        fields.odd(type);
                        break;
                    }
                    for (std::size_t i = 0; i < data.size() / 4; ++i)
                        out.mKeywords.push_back(fields.resolved(readLe<std::uint32_t>(data.data() + i * 4)));
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
            else if (type == Content::fourCC("NAM4") || type == Content::fourCC("NAM5"))
            {
                // Task 0193: the body material and the bare hands' impact data set (4 byte form ids, VERIFIED).
                if (data.size() != 4)
                {
                    fields.odd(type);
                    continue;
                }
                const FormId id = fields.resolved(readLe<std::uint32_t>(data.data()));
                (type == Content::fourCC("NAM4") ? out.mBodyMaterial : out.mUnarmedImpactSet) = id;
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

    // ---- task 0191: ARTO -----------------------------------------------------------------------------------------

    bool decodeArt(std::span<const std::byte> bytes, const Content::RawRecord& record, const DecodeContext& context,
        ArtRecord& out, std::string& why)
    {
        const Fields fields{ context, "ARTO", why };
        out = ArtRecord();
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
            else if (type == Content::fourCC("MODL"))
            {
                if (!fields.zstring(type, data, out.mModel))
                    return false;
            }
        }
        if (status < 0)
        {
            why = reader.message();
            return false;
        }
        return true;
    }

    const ArtRecord* CombatIndex::art(FormId id) const
    {
        const auto it = mArts.find(id);
        return it == mArts.end() ? nullptr : &it->second;
    }

    // ---- task 0178 (stage S5-6): PROJ, AMMO, the bow hit and the arrow's numbers -----------------------------------

    bool decodeProjectile(std::span<const std::byte> bytes, const Content::RawRecord& record,
        const DecodeContext& context, ProjectileRecord& out, std::string& why)
    {
        const Fields fields{ context, "PROJ", why };
        out = ProjectileRecord();
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
            else if (type == Content::fourCC("MODL"))
            {
                if (!fields.zstring(type, data, out.mModel))
                    return false;
            }
            else if (type == Content::fourCC("DATA"))
            {
                // 92 bytes for 230 of the 236 in the five masters, 84 or 88 for the other six.
                if (data.size() != 84 && data.size() != 88 && data.size() != kProjectileDataBytes)
                {
                    fields.odd(type);
                    continue;
                }
                const std::byte* p = data.data();
                const float gravity = readLe<float>(p + 4);
                const float speed = readLe<float>(p + 8);
                const float range = readLe<float>(p + 12);
                if (!allFinite({ gravity, speed, range }))
                {
                    fields.odd(type);
                    continue;
                }
                out.mHasData = true;
                out.mFlags = readLe<std::uint16_t>(p);
                out.mType = readLe<std::uint16_t>(p + 2);
                out.mGravity = gravity;
                out.mSpeed = speed;
                out.mRange = range;
            }
        }
        if (status < 0)
        {
            why = reader.message();
            return false;
        }
        return true;
    }

    bool decodeAmmo(std::span<const std::byte> bytes, const Content::RawRecord& record, const DecodeContext& context,
        AmmoRecord& out, std::string& why)
    {
        const Fields fields{ context, "AMMO", why };
        out = AmmoRecord();
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
                if (data.size() != kAmmoDataBytes)
                {
                    fields.odd(type);
                    continue;
                }
                const float damage = readLe<float>(data.data() + 8);
                if (!std::isfinite(damage))
                {
                    fields.odd(type);
                    continue;
                }
                const std::uint32_t stored = readLe<std::uint32_t>(data.data());
                out.mHasData = true;
                out.mProjectile = 0;
                if (stored != 0)
                {
                    const FormId id = fields.resolved(stored);
                    out.mProjectile = id == kUnresolvedFormId ? 0 : id;
                }
                out.mFlags = readLe<std::uint32_t>(data.data() + 4);
                out.mDamage = damage;
                out.mValue = readLe<std::uint32_t>(data.data() + 12);
            }
        }
        if (status < 0)
        {
            why = reader.message();
            return false;
        }
        return true;
    }

    BowHit computeBowHit(const BowInput& input, const CombatSettings& settings)
    {
        BowHit hit;
        if (input.mBow == nullptr || input.mAmmo == nullptr)
            return hit;
        const float skill = std::min(clean(input.mArcherySkill), kMaxSkill);
        const float skillMult = std::min(clean(settings.mDamagePCSkillMax), 1.f + skill / 200.f);
        const float draw = std::isfinite(input.mDrawFraction) ? std::clamp(input.mDrawFraction, 0.f, 1.f) : 0.f;
        hit.mDamageBefore
            = clean((static_cast<float>(input.mBow->mDamage) + clean(input.mAmmo->mDamage)) * skillMult * draw);
        const float mitigation = std::min(clean(input.mTargetMitigationPercent), clean(settings.mMaxArmorRating));
        hit.mDamageAfter = std::clamp(hit.mDamageBefore * (1.f - mitigation / 100.f), 0.f, hit.mDamageBefore);
        hit.mStagger = clean(input.mBow->mStagger);
        return hit;
    }

    float bowDrawSpeed(float weaponSpeed, float bowSpeedBonus)
    {
        if (!std::isfinite(weaponSpeed) || !std::isfinite(bowSpeedBonus))
            return 1.f;
        return std::clamp(weaponSpeed * (1.f + bowSpeedBonus), 0.1f, 10.f);
    }

    LaunchSolution solveLaunchPitch(float horizontal, float rise, float speed, float gravity)
    {
        constexpr float kLimit = 1.5f; // never straight up or down
        constexpr float kFallback = 0.78539816f; // 45 degrees
        LaunchSolution out;
        out.mPitch = kFallback;
        if (!std::isfinite(horizontal) || !std::isfinite(rise) || !std::isfinite(speed) || !std::isfinite(gravity)
            || horizontal < 0.f || !(speed > 0.f) || gravity < 0.f)
            return out;
        if (gravity < 1e-6f)
        {
            out.mReachable = true;
            out.mPitch = std::clamp(std::atan2(rise, horizontal), -kLimit, kLimit);
            return out;
        }
        if (horizontal < 1e-3f)
        {
            out.mReachable = true;
            out.mPitch = rise >= 0.f ? kLimit : -kLimit;
            return out;
        }
        const double v = static_cast<double>(speed);
        const double g = static_cast<double>(gravity);
        const double d = static_cast<double>(horizontal);
        const double h = static_cast<double>(rise);
        const double v2 = v * v;
        const double disc = v2 * v2 - g * (g * d * d + 2.0 * h * v2);
        if (disc < 0.0)
            return out;
        const double tangent = (v2 - std::sqrt(disc)) / (g * d);
        out.mReachable = true;
        out.mPitch = std::clamp(static_cast<float>(std::atan(tangent)), -kLimit, kLimit);
        return out;
    }

    float rollPercent(std::uint64_t seed)
    {
        // splitmix64 (public domain): a mix of one number, the same on every platform.
        std::uint64_t z = seed + 0x9E3779B97F4A7C15ull;
        z = (z ^ (z >> 30)) * 0xBF58476D1CE4E5B9ull;
        z = (z ^ (z >> 27)) * 0x94D049BB133111EBull;
        z ^= z >> 31;
        const double unit = static_cast<double>(z >> 11) * (1.0 / 9007199254740992.0); // [0, 1)
        return std::min(static_cast<float>(unit * 100.0), 99.99999f);
    }

    std::string projectileModelPath(const std::string& stored)
    {
        if (stored.empty() || stored.size() > 256 || stored.find("..") != std::string::npos
            || stored.find(':') != std::string::npos)
            return {};
        std::string path = stored;
        for (char& c : path)
            if (c == '\\')
                c = '/';
        if (path.front() == '/')
            return {};
        const std::string prefix = "meshes/";
        bool hasPrefix = path.size() >= prefix.size();
        for (std::size_t i = 0; hasPrefix && i < prefix.size(); ++i)
            hasPrefix = std::tolower(static_cast<unsigned char>(path[i])) == prefix[i];
        return hasPrefix ? path : prefix + path;
    }

    ProjectileSettings SettingsTable::projectileSettings() const
    {
        ProjectileSettings settings;
        settings.mArrowInventoryChance = get("iArrowInventoryChance", settings.mArrowInventoryChance);
        settings.mMissileStickDepth = get("fCombatMissileStickDepth", settings.mMissileStickDepth);
        settings.mAttachedArrowAgeMax = get("fAttachedArrowAgeMax", settings.mAttachedArrowAgeMax);
        return settings;
    }

    std::vector<FormId> CombatIndex::spellIds() const
    {
        std::vector<FormId> ids;
        ids.reserve(mSpells.size());
        for (const auto& entry : mSpells)
            ids.push_back(entry.first);
        std::sort(ids.begin(), ids.end());
        return ids;
    }

    MagicSettings SettingsTable::magicSettings() const
    {
        MagicSettings settings;
        settings.mWardPowerMaxBase = get("fMagicWardPowerMaxBase", settings.mWardPowerMaxBase);
        settings.mWardPowerMaxMult = get("fMagicWardPowerMaxMult", settings.mWardPowerMaxMult);
        settings.mDamagedMagickaRegenDelay = get("fDamagedMagickaRegenDelay", settings.mDamagedMagickaRegenDelay);
        return settings;
    }

    const SpellRecord* CombatIndex::spellByEditorId(const std::string& editorId) const
    {
        const std::string wanted = lower(editorId);
        const SpellRecord* found = nullptr;
        for (const auto& entry : mSpells)
        {
            // The highest form id when two share a name, so that the unordered iteration cannot change the answer.
            if (lower(entry.second.mCommon.mEditorId) == wanted
                && (found == nullptr || entry.first > found->mCommon.mId))
                found = &entry.second;
        }
        return found;
    }

    const ProjectileRecord* CombatIndex::projectile(FormId id) const
    {
        const auto it = mProjectiles.find(id);
        return it == mProjectiles.end() ? nullptr : &it->second;
    }

    const AmmoRecord* CombatIndex::ammo(FormId id) const
    {
        const auto it = mAmmo.find(id);
        return it == mAmmo.end() ? nullptr : &it->second;
    }

    const CombatStyleRecord* CombatIndex::style(FormId id) const
    {
        const auto it = mStyles.find(id);
        return it == mStyles.end() ? nullptr : &it->second;
    }

    std::vector<FormId> CombatIndex::ammoIds() const
    {
        std::vector<FormId> ids;
        ids.reserve(mAmmo.size());
        for (const auto& entry : mAmmo)
            ids.push_back(entry.first);
        std::sort(ids.begin(), ids.end());
        return ids;
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
                if (t != Content::fourCC("WEAP") && t != Content::fourCC("RACE") && t != Content::fourCC("GMST")
                    && t != Content::fourCC("PROJ") && t != Content::fourCC("AMMO") && t != Content::fourCC("PERK")
                    && t != Content::fourCC("SPEL") && t != Content::fourCC("MGEF") && t != Content::fourCC("IPCT")
                    && t != Content::fourCC("IPDS") && t != Content::fourCC("MATT") && t != Content::fourCC("SNDR")
                    && t != Content::fourCC("SOUN") && t != Content::fourCC("ARTO")
                    && t != Content::fourCC("CSTY") && t != Content::fourCC("AVIF") && t != Content::fourCC("SCRL"))
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
                else if (t == Content::fourCC("AVIF"))
                {
                    progression::SkillRecord s;
                    ok = progression::decodeSkill(bytes, record, mContext, s, why);
                    if (ok && s.mHasSkillData)
                        mOut.mSkills.push_back(std::move(s));
                }
                else if (t == Content::fourCC("PROJ"))
                {
                    ProjectileRecord p;
                    ok = decodeProjectile(bytes, record, mContext, p, why);
                    if (ok)
                        mOut.mProjectiles.push_back(std::move(p));
                }
                else if (t == Content::fourCC("ARTO"))
                {
                    ArtRecord a;
                    ok = decodeArt(bytes, record, mContext, a, why);
                    if (ok)
                        mOut.mArts.push_back(std::move(a));
                }
                else if (t == Content::fourCC("CSTY"))
                {
                    CombatStyleRecord st;
                    ok = decodeCombatStyle(bytes, record, mContext, st, why);
                    if (ok)
                        mOut.mStyles.push_back(std::move(st));
                }
                else if (t == Content::fourCC("AMMO"))
                {
                    AmmoRecord a;
                    ok = decodeAmmo(bytes, record, mContext, a, why);
                    if (ok)
                        mOut.mAmmo.push_back(std::move(a));
                }
                else if (t == Content::fourCC("PERK"))
                {
                    PerkRecord p;
                    ok = decodePerk(bytes, record, mContext, p, why);
                    if (ok)
                        mOut.mPerks.push_back(std::move(p));
                }
                else if (t == Content::fourCC("SPEL"))
                {
                    SpellRecord sp;
                    ok = decodeSpell(bytes, record, mContext, sp, why);
                    if (ok)
                        mOut.mSpells.push_back(std::move(sp));
                }
                else if (t == Content::fourCC("SCRL"))
                {
                    SpellRecord sp; // task 0234: a scroll is cast as a spell
                    ok = decodeScroll(bytes, record, mContext, sp, why);
                    if (ok)
                        mOut.mScrolls.push_back(std::move(sp));
                }
                else if (t == Content::fourCC("MGEF"))
                {
                    MagicEffectRecord m;
                    ok = decodeMagicEffect(bytes, record, mContext, m, why);
                    if (ok)
                        mOut.mMagicEffects.push_back(std::move(m));
                }
                else if (t == Content::fourCC("IPCT"))
                {
                    ImpactRecord r;
                    ok = decodeImpact(bytes, record, mContext, r, why);
                    if (ok)
                        mOut.mSounds.mImpacts.push_back(std::move(r));
                }
                else if (t == Content::fourCC("IPDS"))
                {
                    ImpactSetRecord r;
                    ok = decodeImpactSet(bytes, record, mContext, r, why);
                    if (ok)
                        mOut.mSounds.mImpactSets.push_back(std::move(r));
                }
                else if (t == Content::fourCC("MATT"))
                {
                    MaterialRecord r;
                    ok = decodeMaterial(bytes, record, mContext, r, why);
                    if (ok)
                        mOut.mSounds.mMaterials.push_back(std::move(r));
                }
                else if (t == Content::fourCC("SOUN"))
                {
                    SoundRedirectRecord r;
                    ok = decodeSoundRedirect(bytes, record, mContext, r, why);
                    if (ok)
                        mOut.mSounds.mRedirects.push_back(std::move(r));
                }
                else if (t == Content::fourCC("SNDR"))
                {
                    SoundDescriptorRecord r;
                    ok = decodeSoundDescriptor(bytes, record, mContext, WeatherLimits(), r, why);
                    if (ok)
                        mOut.mSounds.mSounds.push_back(std::move(r));
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

    BlockSettings SettingsTable::blockSettings() const
    {
        struct BlockName
        {
            const char* mName;
            float BlockSettings::*mMember;
        };
        static constexpr BlockName kNames[] = {
            { "fBlockMax", &BlockSettings::mBlockMax },
            { "fBlockWeaponBase", &BlockSettings::mBlockWeaponBase },
            { "fBlockWeaponScaling", &BlockSettings::mBlockWeaponScaling },
            { "fBlockSkillMult", &BlockSettings::mBlockSkillMult },
            { "fBlockPowerAttackMult", &BlockSettings::mBlockPowerAttackMult },
            { "fStaminaBlockDmgMult", &BlockSettings::mStaminaBlockDmgMult },
            { "fStaminaBlockBase", &BlockSettings::mStaminaBlockBase },
            { "fStaminaBashBase", &BlockSettings::mStaminaBashBase },
        };
        BlockSettings settings;
        for (const BlockName& entry : kNames)
        {
            const auto it = mValues.find(lower(entry.mName));
            if (it != mValues.end())
                settings.*entry.mMember = it->second;
        }
        return settings;
    }

    void CombatIndex::add(const CombatRecords& records)
    {
        mSoundLibrary.add(records.mSounds);
        for (const WeaponRecord& w : records.mWeapons)
        {
            if (w.mCommon.deleted())
                mWeapons.erase(w.mCommon.mId);
            else
                mWeapons[w.mCommon.mId] = w;
        }
        for (const ProjectileRecord& p : records.mProjectiles)
        {
            if (p.mCommon.deleted())
                mProjectiles.erase(p.mCommon.mId);
            else
                mProjectiles[p.mCommon.mId] = p;
        }
        for (const ArtRecord& a : records.mArts)
        {
            if (a.mCommon.deleted())
                mArts.erase(a.mCommon.mId);
            else
                mArts[a.mCommon.mId] = a;
        }
        for (const AmmoRecord& a : records.mAmmo)
        {
            if (a.mCommon.deleted())
                mAmmo.erase(a.mCommon.mId);
            else
                mAmmo[a.mCommon.mId] = a;
        }
        for (const progression::SkillRecord& sk : records.mSkills)
            mSkillTable.add(sk);
        for (const CombatStyleRecord& st : records.mStyles)
        {
            if (st.mCommon.deleted())
                mStyles.erase(st.mCommon.mId);
            else
                mStyles[st.mCommon.mId] = st;
        }
        for (const RaceCombatRecord& r : records.mRaces)
        {
            if (r.mCommon.deleted())
                mRaces.erase(r.mCommon.mId);
            else
                mRaces[r.mCommon.mId] = r;
        }
        for (const PerkRecord& p : records.mPerks)
        {
            if (p.mCommon.deleted())
                mPerks.erase(p.mCommon.mId);
            else
                mPerks[p.mCommon.mId] = p;
        }
        for (const SpellRecord& sp : records.mSpells)
        {
            if (sp.mCommon.deleted())
                mSpells.erase(sp.mCommon.mId);
            else
                mSpells[sp.mCommon.mId] = sp;
        }
        for (const SpellRecord& sc : records.mScrolls)
        {
            if (sc.mCommon.deleted())
                mScrolls.erase(sc.mCommon.mId);
            else
                mScrolls[sc.mCommon.mId] = sc;
        }
        for (const MagicEffectRecord& m : records.mMagicEffects)
        {
            if (m.mCommon.deleted())
                mMagicEffects.erase(m.mCommon.mId);
            else
                mMagicEffects[m.mCommon.mId] = m;
        }
        mSettings.add(records);
    }

    const PerkRecord* CombatIndex::perk(FormId id) const
    {
        const auto it = mPerks.find(id);
        return it == mPerks.end() ? nullptr : &it->second;
    }

    const SpellRecord* CombatIndex::spell(FormId id) const
    {
        const auto it = mSpells.find(id);
        if (it != mSpells.end())
            return &it->second;
        const auto scroll = mScrolls.find(id); // task 0234: a scroll is found by its form id too (never by editor id)
        return scroll == mScrolls.end() ? nullptr : &scroll->second;
    }

    const MagicEffectRecord* CombatIndex::magicEffect(FormId id) const
    {
        const auto it = mMagicEffects.find(id);
        return it == mMagicEffects.end() ? nullptr : &it->second;
    }

    std::vector<FormId> CombatIndex::perkIds() const
    {
        std::vector<FormId> ids;
        ids.reserve(mPerks.size());
        for (const auto& entry : mPerks)
            ids.push_back(entry.first);
        std::sort(ids.begin(), ids.end());
        return ids;
    }

    FormId CombatIndex::perkByEditorId(const std::string& editorId) const
    {
        const std::string wanted = lower(editorId);
        FormId best = kNullFormId;
        for (const auto& [id, perk] : mPerks)
            if (lower(perk.mCommon.mEditorId) == wanted && (best == kNullFormId || id < best))
                best = id;
        return best;
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
            out.push_back({ static_cast<float>(armor->mRating) / 100.f, armor->mSlotMask, armor->mArmorType,
                worn.mArmor, armor->mKeywords });
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

    BlockResult computeBlock(const BlockInput& input, const BlockSettings& settings)
    {
        BlockResult result;
        const float damage = clean(input.mDamage);
        const float skill = std::min(clean(input.mBlockerSkill), kMaxSkill);
        float fraction = clean(settings.mBlockWeaponBase)
            + clean(settings.mBlockWeaponScaling) * skill * clean(settings.mBlockSkillMult) / 100.f
            + std::max(0.f, clean(input.mShieldBonus)); // task 0214: 0 unless a shield is held
        fraction = std::min(fraction, clean(settings.mBlockMax));
        if (input.mPowerAttack)
            fraction *= clean(settings.mBlockPowerAttackMult);
        fraction = std::clamp(fraction * clean(input.mFractionScale), 0.f, 1.f);
        const float blocked = damage * fraction;
        const float drain = clean(blocked * clean(settings.mStaminaBlockDmgMult) + clean(settings.mStaminaBlockBase));
        const float stamina = clean(input.mBlockerStamina);
        // A tiny tolerance: 10 x 0.3 x 0.25 is 0.75000006 in float, and a blocker with exactly 0.75 can pay it.
        constexpr float kStaminaTolerance = 1.0e-4f;
        if (stamina + kStaminaTolerance >= drain)
        {
            result.mBlocked = true;
            result.mFraction = fraction;
            result.mDamageTaken = clean(damage - blocked);
            result.mStaminaDrain = std::min(drain, stamina);
        }
        else
        {
            result.mBroken = true;
            result.mDamageTaken = damage;
            result.mStaminaDrain = stamina;
        }
        return result;
    }

    float staggerMagnitude(float weaponStagger, bool powerAttack, const BlockSettings& settings)
    {
        return clean(clean(weaponStagger) + (powerAttack ? clean(settings.mPowerAttackStaggerBonus) : 0.f));
    }

    bool causesStagger(float weaponStagger, bool powerAttack, const BlockSettings& settings)
    {
        return staggerMagnitude(weaponStagger, powerAttack, settings) >= clean(settings.mStaggerThreshold);
    }

    float bashStaminaCost(const BlockSettings& settings)
    {
        return clean(settings.mStaminaBashBase);
    }
}
