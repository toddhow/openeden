#include "game_skyrimse/progression.hpp"

#include "exterior_common.hpp"

#include "game_skyrimse/combat_stats.hpp"

#include <algorithm>
#include <cmath>
#include <cstring>

// The player's skills, levelling and perks: see the header (task 0200).

namespace game_skyrimse::progression
{
    using detail::Fields;
    using detail::readLe;
    using detail::SubReader;

    namespace
    {
        constexpr const char* kEditorIds[kSkillCount] = { "AVOneHanded", "AVTwoHanded", "AVMarksman", "AVBlock",
            "AVSmithing", "AVHeavyArmor", "AVLightArmor", "AVPickpocket", "AVLockpicking", "AVSneak", "AVAlchemy",
            "AVSpeechcraft", "AVAlteration", "AVConjuration", "AVDestruction", "AVMysticism", "AVRestoration",
            "AVEnchanting" };
        constexpr const char* kNames[kSkillCount] = { "One-Handed", "Two-Handed", "Archery", "Block", "Smithing",
            "Heavy Armor", "Light Armor", "Pickpocket", "Lockpicking", "Sneak", "Alchemy", "Speech", "Alteration",
            "Conjuration", "Destruction", "Illusion", "Restoration", "Enchanting" };

        constexpr std::uint32_t kSaveMagic = 0x52475250; // "PRGR"
        constexpr std::uint32_t kSaveVersion = 1;
        constexpr std::size_t kMaxChildren = 16;
        constexpr float kMaxFloat = 1.0e9f;

        std::string lowerAscii(std::string_view text)
        {
            std::string out(text);
            for (char& c : out)
                if (c >= 'A' && c <= 'Z')
                    c = static_cast<char>(c + 32);
            return out;
        }

        template <class T>
        void put(std::vector<std::uint8_t>& out, T value)
        {
            std::uint8_t bytes[sizeof(T)];
            std::memcpy(bytes, &value, sizeof(T));
            out.insert(out.end(), bytes, bytes + sizeof(T));
        }

        struct Cursor
        {
            std::span<const std::uint8_t> mData;
            std::size_t mPos = 0;
            template <class T>
            bool get(T& out)
            {
                if (mData.size() - mPos < sizeof(T))
                    return false;
                std::memcpy(&out, mData.data() + mPos, sizeof(T));
                mPos += sizeof(T);
                return true;
            }
        };

        bool goodFloat(float v)
        {
            return std::isfinite(v) && v >= 0.f && v <= kMaxFloat;
        }
    }

    std::string_view skillEditorId(std::size_t slot)
    {
        return slot < kSkillCount ? std::string_view(kEditorIds[slot]) : std::string_view();
    }

    std::string_view skillName(std::size_t slot)
    {
        return slot < kSkillCount ? std::string_view(kNames[slot]) : std::string_view();
    }

    std::size_t slotOfSkillEditorId(std::string_view editorId)
    {
        const std::string wanted = lowerAscii(editorId);
        for (std::size_t i = 0; i < kSkillCount; ++i)
            if (lowerAscii(kEditorIds[i]) == wanted)
                return i;
        return kSkillCount;
    }

    std::size_t slotOfMagicSkillValue(std::int32_t actorValue)
    {
        if (actorValue < static_cast<std::int32_t>(kFirstSkillValue + Slot::Alteration)
            || actorValue > static_cast<std::int32_t>(kFirstSkillValue + Slot::Restoration))
            return kSkillCount;
        return static_cast<std::size_t>(actorValue) - kFirstSkillValue;
    }

    // ---- the AVIF decoder -----------------------------------------------------------------------------------------

    bool decodeSkill(std::span<const std::byte> bytes, const Content::RawRecord& record, const DecodeContext& context,
        SkillRecord& out, std::string& why)
    {
        const Fields fields{ context, "AVIF", why };
        out = SkillRecord();
        out.mCommon = fields.common(record);
        SubReader reader(bytes);
        std::uint32_t type = 0;
        std::span<const std::byte> data;
        int status = 0;
        bool inTree = false;
        while ((status = reader.next(type, data)) == 1)
        {
            const auto single = [&](std::uint32_t& value) {
                if (data.size() != 4)
                {
                    fields.odd(type);
                    return false;
                }
                value = readLe<std::uint32_t>(data.data());
                return true;
            };
            const auto floating = [&](float& value) {
                if (data.size() != 4)
                {
                    fields.odd(type);
                    return false;
                }
                value = readLe<float>(data.data());
                if (!std::isfinite(value))
                {
                    why = detail::tag(type) + ": a float that is not finite";
                    return false;
                }
                return true;
            };
            switch (type)
            {
                case Content::fourCC("EDID"):
                    if (!fields.zstring(type, data, out.mCommon.mEditorId))
                        return false;
                    break;
                case Content::fourCC("AVSK"):
                    if (data.size() != 16)
                    {
                        fields.odd(type);
                        break;
                    }
                    out.mUseMult = readLe<float>(data.data());
                    out.mUseOffset = readLe<float>(data.data() + 4);
                    out.mImproveMult = readLe<float>(data.data() + 8);
                    out.mImproveOffset = readLe<float>(data.data() + 12);
                    if (!std::isfinite(out.mUseMult) || !std::isfinite(out.mUseOffset)
                        || !std::isfinite(out.mImproveMult) || !std::isfinite(out.mImproveOffset))
                    {
                        why = "AVSK: a float that is not finite";
                        return false;
                    }
                    out.mHasSkillData = true;
                    break;
                case Content::fourCC("PNAM"):
                {
                    std::uint32_t stored = 0;
                    if (!single(stored))
                        break;
                    if (out.mNodes.size() >= kMaxNodesPerSkill)
                    {
                        why = "AVIF: more than " + std::to_string(kMaxNodesPerSkill) + " perk tree nodes";
                        return false;
                    }
                    inTree = true;
                    PerkNode node;
                    node.mPerk = stored == 0 ? kNullFormId : fields.resolved(stored);
                    out.mNodes.push_back(std::move(node));
                    break;
                }
                case Content::fourCC("CNAM"):
                {
                    std::uint32_t value = 0;
                    if (!single(value))
                        break;
                    if (!inTree)
                        out.mCategory = value;
                    else if (out.mNodes.back().mChildren.size() < kMaxChildren)
                        out.mNodes.back().mChildren.push_back(value);
                    break;
                }
                case Content::fourCC("XNAM"):
                    if (inTree)
                        (void)single(out.mNodes.back().mColumn);
                    break;
                case Content::fourCC("YNAM"):
                    if (inTree)
                        (void)single(out.mNodes.back().mRow);
                    break;
                case Content::fourCC("INAM"):
                    if (inTree)
                        (void)single(out.mNodes.back().mIndex);
                    break;
                case Content::fourCC("HNAM"):
                    if (inTree && !floating(out.mNodes.back().mHorizontal))
                    {
                        if (!why.empty())
                            return false;
                    }
                    break;
                case Content::fourCC("VNAM"):
                    if (inTree && !floating(out.mNodes.back().mVertical))
                    {
                        if (!why.empty())
                            return false;
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

    Settings settingsFrom(const SettingsTable& table)
    {
        Settings s;
        s.mSkillUseCurve = table.get("fSkillUseCurve", s.mSkillUseCurve);
        s.mXpBase = table.get("fXPLevelUpBase", s.mXpBase);
        s.mXpMult = table.get("fXPLevelUpMult", s.mXpMult);
        s.mCarryWeightMod = table.get("fLevelUpCarryWeightMod", s.mCarryWeightMod);
        return s;
    }

    // ---- the table ------------------------------------------------------------------------------------------------

    void SkillTable::add(const SkillRecord& record)
    {
        const std::size_t slot = slotOfSkillEditorId(record.mCommon.mEditorId);
        if (slot >= kSkillCount || record.mCommon.deleted())
            return;
        mSkills[slot] = record;
        mHave[slot] = true;
    }

    const SkillRecord* SkillTable::skill(std::size_t slot) const
    {
        return slot < kSkillCount && mHave[slot] ? &mSkills[slot] : nullptr;
    }

    bool SkillTable::complete() const
    {
        for (std::size_t i = 0; i < kSkillCount; ++i)
            if (!mHave[i] || !mSkills[i].mHasSkillData)
                return false;
        return true;
    }

    const PerkNode* SkillTable::nodeOfPerk(FormId perk, std::size_t* slot) const
    {
        if (perk == kNullFormId)
            return nullptr;
        for (std::size_t s = 0; s < kSkillCount; ++s)
        {
            if (!mHave[s])
                continue;
            for (const PerkNode& node : mSkills[s].mNodes)
                if (node.mPerk == perk)
                {
                    if (slot != nullptr)
                        *slot = s;
                    return &node;
                }
        }
        return nullptr;
    }

    std::size_t SkillTable::perkNodeCount() const
    {
        std::size_t n = 0;
        for (std::size_t s = 0; s < kSkillCount; ++s)
            if (mHave[s])
                for (const PerkNode& node : mSkills[s].mNodes)
                    if (node.mPerk != kNullFormId)
                        ++n;
        return n;
    }

    // ---- the player -----------------------------------------------------------------------------------------------

    PlayerProgression::PlayerProgression(const SkillTable& skills, const PerkLibrary* perks, Settings settings)
        : mSkillTable(skills)
        , mPerkLibrary(perks)
        , mSettings(settings)
    {
        mSkill.fill(15);
    }

    void PlayerProgression::setStart(std::uint16_t level, std::uint8_t skill)
    {
        mLevel = std::max<std::uint16_t>(level, 1);
        mSkill.fill(std::min(skill, kMaxSkill));
        mSkillXp.fill(0.f);
        ++mRevision;
    }

    void PlayerProgression::reset(std::uint16_t level, std::uint8_t skill)
    {
        setStart(level, skill);
        mLevelXp = 0.f;
        mPerkPoints = 0;
        mVitalCount = {};
        mOwned.clear();
        mLevelUps.clear();
    }

    std::uint8_t PlayerProgression::skill(std::size_t slot) const
    {
        return slot < kSkillCount ? mSkill[slot] : std::uint8_t(0);
    }

    float PlayerProgression::skillXp(std::size_t slot) const
    {
        return slot < kSkillCount ? mSkillXp[slot] : 0.f;
    }

    float PlayerProgression::skillNeed(std::size_t slot) const
    {
        const SkillRecord* record = mSkillTable.skill(slot);
        if (record == nullptr)
            return 0.f;
        const float need = record->mImproveMult * std::pow(static_cast<float>(mSkill[slot]), mSettings.mSkillUseCurve)
            + record->mImproveOffset;
        return std::isfinite(need) && need >= 1.f ? need : 1.f;
    }

    float PlayerProgression::levelNeed() const
    {
        return mSettings.mXpBase + mSettings.mXpMult * static_cast<float>(mLevel);
    }

    bool PlayerProgression::levelUpReady() const
    {
        return mLevelXp >= levelNeed();
    }

    float PlayerProgression::vitalBonus(Vital vital) const
    {
        return static_cast<float>(mVitalCount[static_cast<std::size_t>(vital)]) * mSettings.mVitalPerLevel;
    }

    float PlayerProgression::carryWeightBonus() const
    {
        return static_cast<float>(mVitalCount[static_cast<std::size_t>(Vital::Stamina)]) * mSettings.mCarryWeightMod;
    }

    std::uint32_t PlayerProgression::useSkill(std::size_t slot, float amount)
    {
        const SkillRecord* record = mSkillTable.skill(slot);
        if (record == nullptr || !std::isfinite(amount) || amount <= 0.f || mSkill[slot] >= kMaxSkill)
            return 0;
        const float gain = record->mUseMult * amount + record->mUseOffset;
        if (!std::isfinite(gain) || gain <= 0.f)
            return 0;
        mSkillXp[slot] += gain;
        std::uint32_t increases = 0;
        while (mSkill[slot] < kMaxSkill && mSkillXp[slot] >= skillNeed(slot))
        {
            mSkillXp[slot] -= skillNeed(slot);
            ++mSkill[slot];
            ++increases;
            mLevelXp += static_cast<float>(mSkill[slot]) * mSettings.mCharacterXpPerSkillLevel;
        }
        if (mSkill[slot] >= kMaxSkill)
            mSkillXp[slot] = 0.f;
        ++mRevision;
        return increases;
    }

    bool PlayerProgression::chooseLevelUp(Vital vital)
    {
        if (!levelUpReady())
            return false;
        mLevelXp -= levelNeed();
        ++mLevel;
        ++mVitalCount[static_cast<std::size_t>(vital)];
        ++mPerkPoints;
        mLevelUps.push_back(mLevel);
        ++mRevision;
        return true;
    }

    std::vector<std::uint16_t> PlayerProgression::takeLevelUps()
    {
        std::vector<std::uint16_t> out;
        out.swap(mLevelUps);
        return out;
    }

    // ---- perks ----------------------------------------------------------------------------------------------------

    std::vector<FormId> PlayerProgression::ranksOf(FormId firstRank) const
    {
        std::vector<FormId> ranks;
        if (mPerkLibrary == nullptr)
            return ranks;
        FormId current = firstRank;
        while (current != kNullFormId && ranks.size() < kMaxPerkRanks)
        {
            const PerkRecord* record = mPerkLibrary->perk(current);
            if (record == nullptr || std::find(ranks.begin(), ranks.end(), current) != ranks.end())
                break;
            ranks.push_back(current);
            current = record->mNext;
        }
        return ranks;
    }

    std::uint8_t PlayerProgression::perkRank(FormId firstRank) const
    {
        for (const auto& [form, rank] : mOwned)
            if (form == firstRank)
                return rank;
        return 0;
    }

    std::vector<FormId> PlayerProgression::ownedPerks() const
    {
        std::vector<FormId> out;
        for (const auto& [form, rank] : mOwned)
        {
            const std::vector<FormId> ranks = ranksOf(form);
            if (ranks.empty())
                out.push_back(form);
            else
                out.push_back(ranks[std::min<std::size_t>(rank, ranks.size()) - 1]);
        }
        std::sort(out.begin(), out.end());
        out.erase(std::unique(out.begin(), out.end()), out.end());
        return out;
    }

    std::vector<FormId> PlayerProgression::hasPerkSet() const
    {
        std::vector<FormId> out;
        for (const auto& [form, rank] : mOwned)
        {
            const std::vector<FormId> ranks = ranksOf(form);
            if (ranks.empty())
                out.push_back(form);
            for (std::size_t i = 0; i < ranks.size() && i < rank; ++i)
                out.push_back(ranks[i]);
        }
        std::sort(out.begin(), out.end());
        out.erase(std::unique(out.begin(), out.end()), out.end());
        return out;
    }

    std::vector<std::pair<std::uint32_t, float>> PlayerProgression::actorValues() const
    {
        std::vector<std::pair<std::uint32_t, float>> out;
        out.reserve(kSkillCount);
        for (std::size_t s = 0; s < kSkillCount; ++s)
            out.emplace_back(kFirstSkillValue + static_cast<std::uint32_t>(s), static_cast<float>(mSkill[s]));
        return out;
    }

    bool PlayerProgression::canBuyPerk(FormId firstRank, std::string& why) const
    {
        if (mSkillTable.nodeOfPerk(firstRank) == nullptr)
        {
            why = "not a perk of any skill tree";
            return false;
        }
        if (mPerkLibrary == nullptr)
        {
            why = "no perk records are loaded";
            return false;
        }
        const std::vector<FormId> ranks = ranksOf(firstRank);
        if (ranks.empty())
        {
            why = "the perk record is missing";
            return false;
        }
        const std::uint8_t owned = perkRank(firstRank);
        if (owned >= ranks.size())
        {
            why = "no further rank";
            return false;
        }
        if (mPerkPoints == 0)
        {
            why = "no perk point";
            return false;
        }
        const PerkRecord* target = mPerkLibrary->perk(ranks[owned]);
        if (target == nullptr)
        {
            why = "the perk record is missing";
            return false;
        }
        const std::vector<FormId> has = hasPerkSet();
        const std::vector<std::pair<std::uint32_t, float>> values = actorValues();
        PerkParticipant participant;
        participant.mKind = PerkParticipant::Kind::Actor;
        participant.mLevel = mLevel;
        participant.mPerks = &has;
        participant.mValues = &values;
        PerkQuery query;
        query.set(PerkRole::Owner, &participant);
        PerkConditionWorld world;
        world.setQuery(&query);
        world.setCurrentRole(PerkRole::Owner);
        conditions::Evaluator evaluator;
        if (!evaluator.evaluate(target->mPrerequisites, world))
        {
            why = "requires a skill level or a perk you do not have";
            return false;
        }
        return true;
    }

    bool PlayerProgression::buyPerk(FormId firstRank, std::string& why)
    {
        if (!canBuyPerk(firstRank, why))
            return false;
        if (mOwned.size() >= kMaxOwnedPerks && perkRank(firstRank) == 0)
        {
            why = "too many perks";
            return false;
        }
        const auto it = std::lower_bound(mOwned.begin(), mOwned.end(), firstRank,
            [](const std::pair<FormId, std::uint8_t>& a, FormId b) { return a.first < b; });
        if (it != mOwned.end() && it->first == firstRank)
            ++it->second;
        else
            mOwned.insert(it, { firstRank, std::uint8_t(1) });
        --mPerkPoints;
        ++mRevision;
        return true;
    }

    std::vector<NodeView> PlayerProgression::nodes(std::size_t slot) const
    {
        std::vector<NodeView> out;
        const SkillRecord* record = mSkillTable.skill(slot);
        if (record == nullptr)
            return out;
        for (const PerkNode& node : record->mNodes)
        {
            if (node.mPerk == kNullFormId)
                continue;
            NodeView view;
            view.mSkill = slot;
            view.mIndex = node.mIndex;
            view.mPerk = node.mPerk;
            view.mRank = perkRank(node.mPerk);
            view.mRanks = static_cast<std::uint8_t>(ranksOf(node.mPerk).size());
            std::string why;
            view.mCanBuy = canBuyPerk(node.mPerk, why);
            out.push_back(view);
        }
        return out;
    }

    // ---- saving ---------------------------------------------------------------------------------------------------

    std::vector<std::uint8_t> PlayerProgression::serialise() const
    {
        std::vector<std::uint8_t> out;
        put<std::uint32_t>(out, kSaveMagic);
        put<std::uint32_t>(out, kSaveVersion);
        put<std::uint16_t>(out, mLevel);
        put<std::uint32_t>(out, mPerkPoints);
        put<float>(out, mLevelXp);
        for (const std::uint16_t count : mVitalCount)
            put<std::uint16_t>(out, count);
        for (std::size_t s = 0; s < kSkillCount; ++s)
        {
            put<std::uint8_t>(out, mSkill[s]);
            put<float>(out, mSkillXp[s]);
        }
        put<std::uint32_t>(out, static_cast<std::uint32_t>(mOwned.size()));
        for (const auto& [form, rank] : mOwned)
        {
            put<std::uint32_t>(out, form);
            put<std::uint8_t>(out, rank);
        }
        return out;
    }

    bool PlayerProgression::restore(std::span<const std::uint8_t> bytes, std::string& error)
    {
        Cursor c{ bytes };
        std::uint32_t magic = 0, version = 0, points = 0, owned = 0;
        std::uint16_t level = 0;
        float levelXp = 0.f;
        std::array<std::uint16_t, 3> vitals{};
        std::array<std::uint8_t, kSkillCount> skills{};
        std::array<float, kSkillCount> xp{};
        if (!c.get(magic) || magic != kSaveMagic)
        {
            error = "the progression save is not a progression save";
            return false;
        }
        if (!c.get(version) || version != kSaveVersion)
        {
            error = "the progression save has version " + std::to_string(version) + ", not "
                + std::to_string(kSaveVersion);
            return false;
        }
        bool ok = c.get(level) && c.get(points) && c.get(levelXp);
        for (std::uint16_t& v : vitals)
            ok = ok && c.get(v);
        for (std::size_t s = 0; ok && s < kSkillCount; ++s)
            ok = c.get(skills[s]) && c.get(xp[s]) && skills[s] <= kMaxSkill && goodFloat(xp[s]);
        ok = ok && c.get(owned) && owned <= kMaxOwnedPerks && level >= 1 && goodFloat(levelXp);
        std::vector<std::pair<FormId, std::uint8_t>> list;
        for (std::uint32_t i = 0; ok && i < owned; ++i)
        {
            std::uint32_t form = 0;
            std::uint8_t rank = 0;
            ok = c.get(form) && c.get(rank) && rank >= 1 && rank <= kMaxPerkRanks && form != 0
                && (list.empty() || form > list.back().first);
            if (ok)
                list.emplace_back(form, rank);
        }
        if (!ok || c.mPos != bytes.size())
        {
            error = "the progression save is damaged";
            return false;
        }
        mLevel = level;
        mPerkPoints = points;
        mLevelXp = levelXp;
        mVitalCount = vitals;
        mSkill = skills;
        mSkillXp = xp;
        mOwned = std::move(list);
        mLevelUps.clear();
        ++mRevision;
        return true;
    }
}
