#include "game_skyrimse/perks.hpp"

#include "exterior_common.hpp"

#include <algorithm>
#include <cmath>

namespace game_skyrimse
{
    using detail::Fields;
    using detail::readLe;
    using detail::SubReader;

    namespace
    {
        constexpr std::size_t kPerkDataBytes = 5;
        constexpr std::size_t kSpellSpitBytes = 36;
        constexpr std::size_t kSpellEfitBytes = 12;
        constexpr std::size_t kMagicEffectDataBytes = 152;

        // The condition functions whose first parameter is a form id of the PERK's own plugin (stored with that
        // plugin's master indices): HasPerk, HasKeyword, GetIsID, GetIsRace, GetInFaction, WornHasKeyword,
        // IsAttackType, HasMagicEffect, GetEquipped, GetGlobalValue, WornApparelHasKeywordCount,
        // EPMagic_SpellHasKeyword, SpellHasCastingPerk.
        bool firstParameterIsForm(std::uint16_t function)
        {
            switch (function)
            {
                case 448:
                case 560:
                case 72:
                case 69:
                case 71:
                case 682:
                case 697:
                case 214:
                case 182:
                case 74:
                case 722:
                case 693:
                case 713:
                    return true;
                default:
                    return false;
            }
        }

        // Task 0236: the condition functions of effects whose first parameter is a form id of the record's own plugin
        // (HasPerk, HasKeyword, GetIsID, GetIsRace, GetInFaction, WornHasKeyword, HasMagicEffect,
        // HasMagicEffectKeyword, GetGlobalValue, GetEquipped, WornApparelHasKeywordCount, GetShouldAttack,
        // IsHostileToActor, GetIsReference, GetInCurrentLocFormList, HasSpell, GetItemCount, GetStage, GetQuestRunning,
        // GetInWorldspace). Functions of the perk list above are included.
        bool effectParameterIsForm(std::uint16_t function)
        {
            switch (function)
            {
                case 699:
                case 66:
                case 719:
                case 136:
                case 444:
                case 264:
                case 47:
                case 58:
                case 56:
                case 310:
                    return true;
                default:
                    return firstParameterIsForm(function);
            }
        }

        /// Decodes one CTDA of a spell effect or a magic effect into `list` (parameter resolved to the load order);
        /// the caller counts it either way, so a damaged one leaves the list shorter than the count.
        void pushEffectCondition(
            const Fields& fields, std::uint32_t type, std::span<const std::byte> data, std::vector<survey::Ctda>& list)
        {
            survey::Ctda c;
            if (!survey::decodeCtda(data, c))
            {
                fields.odd(type);
                return;
            }
            if (effectParameterIsForm(c.mFunction) && c.mParam1 != 0)
                c.mParam1 = fields.resolved(c.mParam1);
            if (list.size() < kMaxPerkConditionsPerTab)
                list.push_back(c);
        }

        template <class T>
        bool contains(const std::vector<T>* list, T value)
        {
            return list != nullptr && std::find(list->begin(), list->end(), value) != list->end();
        }

        float valueOf(const PerkParticipant* p, std::uint32_t index)
        {
            if (p == nullptr || p->mValues == nullptr)
                return 0.f;
            for (const auto& [i, v] : *p->mValues)
                if (i == index)
                    return v;
            return 0.f;
        }

        const PerkParticipant* participantOf(const conditions::ConditionWorld& world, conditions::Subject subject)
        {
            const auto* perkWorld = dynamic_cast<const PerkConditionWorld*>(&world);
            return perkWorld != nullptr ? perkWorld->participant(subject) : nullptr;
        }

        // The five functions only the perk evaluator knows (the global table keeps failing closed on them).
        float isBlockingFn(const survey::Ctda&, conditions::Subject s, const conditions::ConditionWorld& w)
        {
            const PerkParticipant* p = participantOf(w, s);
            return p != nullptr && p->mBlocking ? 1.f : 0.f;
        }
        float isPowerAttackingFn(const survey::Ctda&, conditions::Subject s, const conditions::ConditionWorld& w)
        {
            const PerkParticipant* p = participantOf(w, s);
            return p != nullptr && p->mPowerAttacking ? 1.f : 0.f;
        }
        float isSprintingFn(const survey::Ctda&, conditions::Subject s, const conditions::ConditionWorld& w)
        {
            const PerkParticipant* p = participantOf(w, s);
            return p != nullptr && p->mSprinting ? 1.f : 0.f;
        }
        float isUndeadFn(const survey::Ctda&, conditions::Subject s, const conditions::ConditionWorld& w)
        {
            const PerkParticipant* p = participantOf(w, s);
            return p != nullptr && p->mUndead ? 1.f : 0.f;
        }
        float isAttackTypeFn(const survey::Ctda& c, conditions::Subject s, const conditions::ConditionWorld& w)
        {
            const PerkParticipant* p = participantOf(w, s);
            return p != nullptr && p->mAttackType != kNullFormId && p->mAttackType == c.mParam1 ? 1.f : 0.f;
        }

        constexpr std::array<conditions::ExtraFunction, 5> kExtraFunctions = { {
            { 568, isSprintingFn },
            { 569, isBlockingFn },
            { 673, isPowerAttackingFn },
            { 697, isAttackTypeFn },
            { 715, isUndeadFn },
        } };

        struct RoleRow
        {
            std::uint8_t mEntry;
            std::array<PerkRole, 3> mRoles;
        };
        constexpr PerkRole O = PerkRole::Owner, A = PerkRole::Attacker, W = PerkRole::Weapon, T = PerkRole::Target,
                           R = PerkRole::Armour, S = PerkRole::Spell, N = PerkRole::kCount;
        // The data corroborates the rows with conditions (which condition functions each tab holds, task 0179 log);
        // rows with none in the installed data are RECALLED-VERIFY.
        constexpr RoleRow kRoleRows[] = {
            { 0, { O, W, T } },
            { 1, { O, W, T } },
            { 2, { O, W, T } },
            { 8, { O, T, N } },
            { 14, { O, T, N } },
            { 18, { O, W, T } }, // task 0227: ModSneakAttackMult (RECALLED-VERIFY: not seen in a tab condition)
            { 20, { O, N, N } },
            { 21, { O, N, N } },
            { 26, { O, W, N } },
            { 27, { O, W, N } },
            { 28, { O, W, T } },
            { 29, { O, S, T } },
            { 30, { O, S, T } },
            { 32, { O, R, N } },
            { 33, { O, A, N } },
            { 34, { O, T, N } },
            { 35, { O, W, T } },
            { 36, { O, A, W } },
            { 37, { O, W, T } },
            { 38, { O, S, N } },
            { 39, { O, N, N } },
            { 40, { O, N, N } },
            { 41, { O, S, T } },
            { 42, { O, S, T } },
            { 50, { O, W, N } },
            { 51, { O, W, T } },
            { 52, { O, W, N } },
            { 53, { O, W, T } },
            { 56, { O, T, W } },
            { 57, { O, T, N } },
            { 59, { O, T, N } },
            { 60, { O, T, N } },
            { 66, { O, N, N } },
            { 67, { O, W, N } },
            { 74, { O, T, N } },
            { 76, { O, W, R } },
            { 77, { O, W, R } },
            { 85, { O, R, N } },
            { 88, { O, S, T } },
        };
    }

    // ---- decoders --------------------------------------------------------------------------------------------------

    bool decodePerk(std::span<const std::byte> bytes, const Content::RawRecord& record, const DecodeContext& context,
        PerkRecord& out, std::string& why)
    {
        const Fields fields{ context, "PERK", why };
        out = PerkRecord();
        out.mCommon = fields.common(record);
        SubReader reader(bytes);
        std::uint32_t type = 0;
        std::span<const std::byte> data;
        int status = 0;
        bool inEffect = false;
        bool skipping = false; // an effect whose PRKE was odd: its subrecords are ignored up to its PRKF
        std::size_t tab = 0; // index into the current effect's mTabs; valid when haveTab
        bool haveTab = false;
        while ((status = reader.next(type, data)) == 1)
        {
            switch (type)
            {
                case Content::fourCC("EDID"):
                    if (!fields.zstring(type, data, out.mCommon.mEditorId))
                        return false;
                    break;
                case Content::fourCC("NNAM"):
                    if (data.size() != 4)
                        fields.odd(type);
                    else
                        out.mNext = fields.resolved(readLe<std::uint32_t>(data.data()));
                    break;
                case Content::fourCC("PRKE"):
                    if (data.size() != 3)
                    {
                        fields.odd(type);
                        inEffect = false;
                        skipping = true;
                        break;
                    }
                    if (out.mEffects.size() >= kMaxPerkEffects)
                    {
                        why = "PRKE: more than " + std::to_string(kMaxPerkEffects) + " effects";
                        return false;
                    }
                    out.mEffects.emplace_back();
                    out.mEffects.back().mKind = std::to_integer<std::uint8_t>(data[0]);
                    out.mEffects.back().mRank = std::to_integer<std::uint8_t>(data[1]);
                    out.mEffects.back().mPriority = std::to_integer<std::uint8_t>(data[2]);
                    inEffect = true;
                    skipping = false;
                    haveTab = false;
                    break;
                case Content::fourCC("PRKF"):
                    inEffect = false;
                    skipping = false;
                    haveTab = false;
                    break;
                case Content::fourCC("DATA"):
                    if (skipping)
                        break;
                    if (!inEffect)
                    {
                        if (data.size() != kPerkDataBytes)
                        {
                            fields.odd(type);
                            break;
                        }
                        out.mHasData = true;
                        out.mLevel = std::to_integer<std::uint8_t>(data[1]);
                        break;
                    }
                    {
                        PerkEffect& e = out.mEffects.back();
                        if (e.mKind == PerkEffectKind::EntryPoint && data.size() == 3)
                        {
                            e.mEntryPoint = std::to_integer<std::uint8_t>(data[0]);
                            e.mFunction = std::to_integer<std::uint8_t>(data[1]);
                            e.mTabCount = std::to_integer<std::uint8_t>(data[2]);
                        }
                        else if (e.mKind == PerkEffectKind::Ability && data.size() == 4)
                            e.mForm = fields.resolved(readLe<std::uint32_t>(data.data()));
                        else if (e.mKind == PerkEffectKind::Quest && data.size() == 8)
                        {
                            // quest and stage: not used in combat
                        }
                        else
                            fields.odd(type);
                    }
                    break;
                case Content::fourCC("PRKC"):
                    if (skipping || !inEffect)
                        break;
                    if (data.size() != 1)
                    {
                        fields.odd(type);
                        haveTab = false;
                        break;
                    }
                    {
                        PerkEffect& e = out.mEffects.back();
                        const std::uint8_t index = std::to_integer<std::uint8_t>(data[0]);
                        haveTab = false;
                        for (std::size_t i = 0; i < e.mTabs.size(); ++i)
                            if (e.mTabs[i].mIndex == index)
                            {
                                tab = i;
                                haveTab = true;
                            }
                        if (!haveTab)
                        {
                            if (e.mTabs.size() >= kMaxPerkTabs)
                            {
                                why = "PRKC: more than " + std::to_string(kMaxPerkTabs) + " tabs in one effect";
                                return false;
                            }
                            e.mTabs.emplace_back();
                            e.mTabs.back().mIndex = index;
                            tab = e.mTabs.size() - 1;
                            haveTab = true;
                        }
                    }
                    break;
                case Content::fourCC("CTDA"):
                {
                    if (skipping)
                        break;
                    survey::Ctda c;
                    if (!survey::decodeCtda(data, c))
                    {
                        fields.odd(type);
                        break;
                    }
                    if (firstParameterIsForm(c.mFunction) && c.mParam1 != 0)
                        c.mParam1 = fields.resolved(c.mParam1);
                    std::vector<survey::Ctda>* list = nullptr;
                    if (!inEffect)
                        list = &out.mPrerequisites;
                    else if (haveTab)
                        list = &out.mEffects.back().mTabs[tab].mConditions;
                    else
                    {
                        fields.odd(type); // a condition of an effect before its tab
                        break;
                    }
                    if (list->size() >= kMaxPerkConditionsPerTab)
                    {
                        why = "CTDA: more than " + std::to_string(kMaxPerkConditionsPerTab) + " conditions in one list";
                        return false;
                    }
                    list->push_back(c);
                    break;
                }
                case Content::fourCC("EPFT"):
                    if (skipping || !inEffect)
                        break;
                    if (data.size() != 1)
                        fields.odd(type);
                    else
                        out.mEffects.back().mParamType = std::to_integer<std::uint8_t>(data[0]);
                    break;
                case Content::fourCC("EPFD"):
                    if (skipping || !inEffect)
                        break;
                    {
                        PerkEffect& e = out.mEffects.back();
                        if (e.mParamType == 1)
                        {
                            const float v = data.size() == 4 ? readLe<float>(data.data()) : 0.f;
                            if (data.size() != 4 || !std::isfinite(v))
                            {
                                fields.odd(type);
                                break;
                            }
                            e.mValue = v;
                            e.mHasParam = true;
                        }
                        else if (e.mParamType == 2)
                        {
                            if (data.size() != 8)
                            {
                                fields.odd(type);
                                break;
                            }
                            const float a = readLe<float>(data.data());
                            const float b = readLe<float>(data.data() + 4);
                            if (!std::isfinite(a) || !std::isfinite(b))
                            {
                                fields.odd(type);
                                break;
                            }
                            e.mValue = a;
                            e.mValue2 = b;
                            e.mHasParam = true;
                        }
                        else if (e.mParamType >= 3 && e.mParamType <= 5)
                        {
                            if (data.size() != 4)
                            {
                                fields.odd(type);
                                break;
                            }
                            e.mForm = fields.resolved(readLe<std::uint32_t>(data.data()));
                            e.mHasParam = true;
                        }
                        // text parameters (6, 7) and an EPFD with no EPFT before it are not used
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

    bool decodeSpell(std::span<const std::byte> bytes, const Content::RawRecord& record, const DecodeContext& context,
        SpellRecord& out, std::string& why)
    {
        const Fields fields{ context, "SPEL", why };
        out = SpellRecord();
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
                case Content::fourCC("SPIT"):
                    if (data.size() != kSpellSpitBytes)
                    {
                        fields.odd(type);
                        break;
                    }
                    out.mHasData = true;
                    out.mType = readLe<std::uint32_t>(data.data() + 8);
                    out.mCost = readLe<std::uint32_t>(data.data());
                    out.mCastType = readLe<std::uint32_t>(data.data() + 16);
                    out.mDelivery = readLe<std::uint32_t>(data.data() + 20);
                    out.mCastDuration = readLe<float>(data.data() + 24);
                    out.mRange = readLe<float>(data.data() + 28);
                    if (!std::isfinite(out.mCastDuration))
                        out.mCastDuration = 0.f;
                    if (!std::isfinite(out.mRange))
                        out.mRange = 0.f;
                    break;
                case Content::fourCC("EFID"):
                    if (data.size() != 4)
                    {
                        fields.odd(type);
                        break;
                    }
                    if (out.mEffects.size() >= kMaxSpellEffects)
                    {
                        why = "EFID: more than " + std::to_string(kMaxSpellEffects) + " effects";
                        return false;
                    }
                    out.mEffects.emplace_back();
                    out.mEffects.back().mEffect = fields.resolved(readLe<std::uint32_t>(data.data()));
                    break;
                case Content::fourCC("CTDA"):
                    if (!out.mEffects.empty())
                    {
                        ++out.mEffects.back().mConditions;
                        pushEffectCondition(fields, type, data, out.mEffects.back().mConditionList); // task 0236
                    }
                    break;
                case Content::fourCC("EFIT"):
                    if (data.size() != kSpellEfitBytes || out.mEffects.empty())
                    {
                        fields.odd(type);
                        break;
                    }
                    {
                        const float magnitude = readLe<float>(data.data());
                        if (!std::isfinite(magnitude))
                        {
                            fields.odd(type);
                            break;
                        }
                        SpellEffect& e = out.mEffects.back();
                        e.mMagnitude = magnitude;
                        e.mArea = readLe<std::uint32_t>(data.data() + 4);
                        e.mDuration = readLe<std::uint32_t>(data.data() + 8);
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

    bool decodeMagicEffect(std::span<const std::byte> bytes, const Content::RawRecord& record,
        const DecodeContext& context, MagicEffectRecord& out, std::string& why)
    {
        const Fields fields{ context, "MGEF", why };
        out = MagicEffectRecord();
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
                case Content::fourCC("VMAD"):
                    out.mHasScript = true;
                    break;
                case Content::fourCC("CTDA"):
                    ++out.mConditions;
                    pushEffectCondition(fields, type, data, out.mConditionList); // task 0236
                    break;
                case Content::fourCC("KWDA"):
                    // Task 0236: the effect's keywords (HasMagicEffectKeyword asks them of a running effect)
                    if (data.size() % 4 != 0)
                    {
                        fields.odd(type);
                        break;
                    }
                    for (std::size_t i = 0; i + 4 <= data.size() && out.mKeywords.size() < 64; i += 4)
                        out.mKeywords.push_back(fields.resolved(readLe<std::uint32_t>(data.data() + i)));
                    break;
                case Content::fourCC("DATA"):
                    if (data.size() != kMagicEffectDataBytes)
                    {
                        fields.odd(type);
                        break;
                    }
                    out.mHasData = true;
                    out.mFlags = readLe<std::uint32_t>(data.data());
                    out.mArchetype = readLe<std::uint32_t>(data.data() + 64);
                    out.mPrimaryValue = readLe<std::int32_t>(data.data() + 68);
                    out.mBaseCost = readLe<float>(data.data() + 4);
                    out.mAssociatedSkill = readLe<std::int32_t>(data.data() + 12);
                    out.mAssociatedItem = readLe<std::uint32_t>(data.data() + 8) == 0
                        ? kNullFormId
                        : fields.resolved(readLe<std::uint32_t>(data.data() + 8)); // task 0250
                    out.mResistValue = readLe<std::int32_t>(data.data() + 16);
                    out.mSecondaryWeight = readLe<float>(data.data() + 60);
                    out.mProjectile = readLe<std::uint32_t>(data.data() + 72) == 0
                        ? kNullFormId
                        : fields.resolved(readLe<std::uint32_t>(data.data() + 72));
                    out.mCastingArt = readLe<std::uint32_t>(data.data() + 92) == 0
                        ? kNullFormId
                        : fields.resolved(readLe<std::uint32_t>(data.data() + 92));
                    out.mHitEffectArt = readLe<std::uint32_t>(data.data() + 96) == 0
                        ? kNullFormId
                        : fields.resolved(readLe<std::uint32_t>(data.data() + 96));
                    out.mPerkToApply = readLe<std::uint32_t>(data.data() + 136) == 0
                        ? kNullFormId
                        : fields.resolved(readLe<std::uint32_t>(data.data() + 136));
                    out.mCastType = readLe<std::uint32_t>(data.data() + 80);
                    out.mDelivery = readLe<std::uint32_t>(data.data() + 84);
                    out.mSecondaryValue = readLe<std::int32_t>(data.data() + 88);
                    if (!std::isfinite(out.mBaseCost))
                        out.mBaseCost = 0.f;
                    if (!std::isfinite(out.mSecondaryWeight))
                        out.mSecondaryWeight = 0.f;
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

    // ---- roles and the conditions world --------------------------------------------------------------------------

    PerkRole entryPointTabRole(std::uint8_t entryPoint, std::uint8_t tab, bool& known)
    {
        for (const RoleRow& row : kRoleRows)
            if (row.mEntry == entryPoint)
            {
                known = true;
                return tab < row.mRoles.size() ? row.mRoles[tab] : PerkRole::kCount;
            }
        known = false;
        return tab == 0 ? PerkRole::Owner : PerkRole::kCount;
    }

    const PerkParticipant* PerkConditionWorld::participant(conditions::Subject subject) const
    {
        if (mQuery == nullptr || subject == 0 || subject > mQuery->mRole.size())
            return nullptr;
        return mQuery->mRole[subject - 1];
    }

    conditions::Subject PerkConditionWorld::resolveRunOn(std::uint32_t runOn, std::uint32_t reference) const
    {
        // Task 0275: "reference" (2) 0x14 is the player, when a role was told to be his (the Thieves Guild's helper
        // perk asks HasPerk of the player)
        if (runOn == 2 && reference == kPlayerReference && mPlayerRole != PerkRole::kCount
            && participant(subjectOf(mPlayerRole)) != nullptr)
            return subjectOf(mPlayerRole);
        // Every other run-on of an effect tab but "subject" has nobody to name: the condition fails closed.
        if (runOn != 0 || mCurrent == PerkRole::kCount || participant(subjectOf(mCurrent)) == nullptr)
            return 0;
        return subjectOf(mCurrent);
    }

    bool PerkConditionWorld::hasBaseId(conditions::Subject subject, std::uint32_t formId) const
    {
        const PerkParticipant* p = participant(subject);
        return p != nullptr && formId != kNullFormId && p->mForm == formId;
    }

    bool PerkConditionWorld::inFaction(conditions::Subject subject, std::uint32_t faction) const
    {
        const PerkParticipant* p = participant(subject);
        return p != nullptr && contains(p->mFactions, static_cast<FormId>(faction));
    }

    bool PerkConditionWorld::hasPerk(conditions::Subject subject, std::uint32_t perk) const
    {
        const PerkParticipant* p = participant(subject);
        return p != nullptr && contains(p->mPerks, static_cast<FormId>(perk));
    }

    bool PerkConditionWorld::hasKeyword(conditions::Subject subject, std::uint32_t keyword) const
    {
        const PerkParticipant* p = participant(subject);
        return p != nullptr && contains(p->mKeywords, static_cast<FormId>(keyword));
    }

    float PerkConditionWorld::baseActorValue(conditions::Subject subject, std::uint32_t actorValueIndex) const
    {
        return valueOf(participant(subject), actorValueIndex);
    }

    float PerkConditionWorld::actorValue(conditions::Subject subject, std::uint32_t actorValueIndex) const
    {
        return valueOf(participant(subject), actorValueIndex); // no modifiers exist in combat yet: base is current
    }

    float PerkConditionWorld::levelOf(conditions::Subject subject) const
    {
        const PerkParticipant* p = participant(subject);
        return p != nullptr && p->mKind == PerkParticipant::Kind::Actor ? static_cast<float>(p->mLevel) : 0.f;
    }

    bool PerkConditionWorld::isRace(conditions::Subject subject, std::uint32_t race) const
    {
        const PerkParticipant* p = participant(subject);
        return p != nullptr && race != 0 && p->mRace == race;
    }

    int PerkConditionWorld::sexOf(conditions::Subject subject) const
    {
        const PerkParticipant* p = participant(subject);
        return p != nullptr && p->mKind == PerkParticipant::Kind::Actor ? (p->mFemale ? 1 : 0) : -1;
    }

    float PerkConditionWorld::equippedItemType(conditions::Subject subject, std::uint32_t hand) const
    {
        const PerkParticipant* p = participant(subject);
        return p != nullptr && p->mKind == PerkParticipant::Kind::Actor && hand == 1
            ? static_cast<float>(p->mRightHandType)
            : 0.f;
    }

    bool PerkConditionWorld::isSneaking(conditions::Subject subject) const
    {
        const PerkParticipant* p = participant(subject);
        return p != nullptr && p->mSneaking;
    }

    bool PerkConditionWorld::wornHasKeyword(conditions::Subject subject, std::uint32_t keyword) const
    {
        const PerkParticipant* p = participant(subject);
        return p != nullptr && contains(p->mWornKeywords, static_cast<FormId>(keyword));
    }

    // ---- the engine ------------------------------------------------------------------------------------------------

    PerkEngine::PerkEngine(const PerkLibrary& library)
        : mLibrary(library)
    {
        mEvaluator.setExtraFunctions(kExtraFunctions);
    }

    std::size_t PerkEngine::collect(std::uint8_t entryPoint, std::span<const FormId> perks,
        std::array<Pick, kMaxQueryEffects>& out, std::uint32_t& dropped) const
    {
        std::size_t n = 0;
        dropped = 0;
        for (const FormId id : perks)
        {
            const PerkRecord* perk = mLibrary.perk(id);
            if (perk == nullptr)
                continue;
            for (std::size_t i = 0; i < perk->mEffects.size(); ++i)
            {
                const PerkEffect& e = perk->mEffects[i];
                if (e.mKind != PerkEffectKind::EntryPoint || e.mEntryPoint != entryPoint)
                    continue;
                if (n == out.size())
                {
                    ++dropped;
                    continue;
                }
                out[n++] = { perk, &e, static_cast<std::uint32_t>(i) };
            }
        }
        // Higher priority first; ties by perk form id, then the effect's place: the same order whatever the list's.
        std::sort(out.begin(), out.begin() + static_cast<std::ptrdiff_t>(n), [](const Pick& a, const Pick& b) {
            if (a.mEffect->mPriority != b.mEffect->mPriority)
                return a.mEffect->mPriority > b.mEffect->mPriority;
            if (a.mPerk->mCommon.mId != b.mPerk->mCommon.mId)
                return a.mPerk->mCommon.mId < b.mPerk->mCommon.mId;
            return a.mIndex < b.mIndex;
        });
        return n;
    }

    bool PerkEngine::hasEntryPoint(std::span<const FormId> perks, std::uint8_t entryPoint) const
    {
        for (const FormId id : perks)
        {
            const PerkRecord* perk = mLibrary.perk(id);
            if (perk == nullptr)
                continue;
            for (const PerkEffect& e : perk->mEffects)
                if (e.mKind == PerkEffectKind::EntryPoint && e.mEntryPoint == entryPoint)
                    return true;
        }
        return false;
    }

    bool PerkEngine::tabsHold(const PerkEffect& effect, const PerkQuery& query)
    {
        mWorld.setQuery(&query);
        for (const PerkTab& tab : effect.mTabs)
        {
            if (tab.mConditions.empty())
                continue; // a tab with no conditions passes
            bool known = false;
            const PerkRole role = entryPointTabRole(effect.mEntryPoint, tab.mIndex, known);
            if (role == PerkRole::kCount || query.mRole[static_cast<std::size_t>(role)] == nullptr)
                return false; // nobody to ask: fails closed
            mWorld.setCurrentRole(role);
            if (!mEvaluator.evaluate(tab.mConditions, mWorld))
                return false;
        }
        return true;
    }

    EntryOutcome PerkEngine::apply(
        std::uint8_t entryPoint, std::span<const FormId> perks, const PerkQuery& query, float value)
    {
        EntryOutcome outcome;
        outcome.mValue = value;
        std::array<Pick, kMaxQueryEffects> picks;
        const std::size_t n = collect(entryPoint, perks, picks, outcome.mDropped);
        float v = value;
        for (std::size_t i = 0; i < n; ++i)
        {
            const PerkEffect& e = *picks[i].mEffect;
            ++outcome.mConsidered;
            const bool floatFunction = e.mFunction == PerkFunction::Set || e.mFunction == PerkFunction::Add
                || e.mFunction == PerkFunction::Multiply || e.mFunction == PerkFunction::MultiplyOnePlusActorValueMult;
            if (!floatFunction || !e.mHasParam)
            {
                ++outcome.mUnsupported;
                continue;
            }
            if (!tabsHold(e, query))
            {
                ++outcome.mFailedConditions;
                continue;
            }
            switch (e.mFunction)
            {
                case PerkFunction::Set:
                    v = e.mValue;
                    break;
                case PerkFunction::Add:
                    v += e.mValue;
                    break;
                case PerkFunction::Multiply:
                    v *= e.mValue;
                    break;
                default:
                {
                    const float av = valueOf(query.mRole[static_cast<std::size_t>(PerkRole::Owner)],
                        static_cast<std::uint32_t>(std::max(0.f, std::floor(e.mValue + 0.5f))));
                    v *= 1.f + av * e.mValue2;
                    break;
                }
            }
            ++outcome.mApplied;
        }
        outcome.mValue = v;
        return outcome;
    }

    std::size_t PerkEngine::selectSpells(
        std::uint8_t entryPoint, std::span<const FormId> perks, const PerkQuery& query, std::span<FormId> out)
    {
        std::array<Pick, kMaxQueryEffects> picks;
        std::uint32_t dropped = 0;
        const std::size_t n = collect(entryPoint, perks, picks, dropped);
        std::size_t written = 0;
        for (std::size_t i = 0; i < n && written < out.size(); ++i)
        {
            const PerkEffect& e = *picks[i].mEffect;
            if (e.mFunction != PerkFunction::SelectSpell || e.mForm == kNullFormId || !tabsHold(e, query))
                continue;
            out[written++] = e.mForm;
        }
        return written;
    }

    // ---- abilities -------------------------------------------------------------------------------------------------

    void collectPerkAbilities(const PerkLibrary& library, std::span<const FormId> perks, std::vector<FormId>& out)
    {
        for (const FormId id : perks)
        {
            const PerkRecord* perk = library.perk(id);
            if (perk == nullptr)
                continue;
            for (const PerkEffect& e : perk->mEffects)
                if (e.mKind == PerkEffectKind::Ability && e.mForm != kNullFormId)
                    out.push_back(e.mForm);
        }
    }

    float abilityModifier(
        const PerkLibrary& library, std::span<const FormId> spells, std::int32_t actorValue, bool& found)
    {
        found = false;
        float sum = 0.f;
        for (const FormId id : spells)
        {
            const SpellRecord* spell = library.spell(id);
            if (spell == nullptr || !spell->isAbility())
                continue;
            for (const SpellEffect& effect : spell->mEffects)
            {
                const MagicEffectRecord* magic = library.magicEffect(effect.mEffect);
                if (magic != nullptr && magic->modifiesAValue() && magic->mPrimaryValue == actorValue)
                {
                    sum += effect.mMagnitude;
                    found = true;
                }
            }
        }
        return sum;
    }
}
