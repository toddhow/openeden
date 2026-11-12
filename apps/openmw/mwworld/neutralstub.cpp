#include "neutralstub.hpp"

#include <algorithm>
#include <array>
#include <cstdint>
#include <string_view>

#include <components/esm3/defaultgmsts.hpp>
#include <components/esm3/loadclas.hpp>
#include <components/esm3/loadglob.hpp>
#include <components/esm3/loadgmst.hpp>
#include <components/esm3/loadmgef.hpp>
#include <components/esm3/loadnpc.hpp>
#include <components/esm3/loadrace.hpp>
#include <components/esm3/loadskil.hpp>

#include "esmstore.hpp"

namespace MWWorld
{
    namespace
    {
        // The dummy records are named so that a log line or a debugger shows what they are. The ids carry a ':' like
        // the neutral base ids do, which no Morrowind record id has in practice.
        constexpr std::string_view sStubRace = "neutral:stub race";
        constexpr std::string_view sStubClass = "neutral:stub class";
        constexpr std::string_view sStubPlayerName = "Player";
        constexpr std::int16_t sStubLevel = 1;
        constexpr unsigned char sStubAttribute = 50;
        constexpr unsigned char sStubSkill = 5;
        constexpr std::uint16_t sStubHealth = 100;
        constexpr std::uint16_t sStubMana = 100;
        constexpr std::uint16_t sStubFatigue = 200;
    }

    ESM::RefId neutralStubRaceId()
    {
        return ESM::RefId::stringRefId(sStubRace);
    }

    ESM::RefId neutralStubClassId()
    {
        return ESM::RefId::stringRefId(sStubClass);
    }

    bool insertNeutralStubPlayer(ESMStore& store)
    {
        const ESM::RefId playerId = ESM::RefId::stringRefId("Player");
        if (store.get<ESM::NPC>().search(playerId) != nullptr)
            return false;

        if (store.get<ESM::Race>().search(neutralStubRaceId()) == nullptr)
        {
            ESM::Race race;
            race.blank();
            race.mId = neutralStubRaceId();
            race.mName = std::string(sStubRace);
            store.insertStatic(race);
        }
        if (store.get<ESM::Class>().search(neutralStubClassId()) == nullptr)
        {
            ESM::Class cls;
            cls.blank();
            cls.mId = neutralStubClassId();
            cls.mName = std::string(sStubClass);
            store.insertStatic(cls);
        }

        ESM::NPC npc;
        npc.blank();
        npc.mId = playerId;
        npc.mName = std::string(sStubPlayerName);
        // A blank record has no health, so the player would be dead at once (and the dead player's "hit" line would be
        // looked up in a dialogue store that has none). Plain mid values, named in one place.
        npc.mNpdt.mLevel = sStubLevel;
        npc.mNpdt.mAttributes.fill(sStubAttribute);
        npc.mNpdt.mSkills.fill(sStubSkill);
        npc.mNpdt.mHealth = sStubHealth;
        npc.mNpdt.mMana = sStubMana;
        npc.mNpdt.mFatigue = sStubFatigue;
        npc.mRace = neutralStubRaceId();
        npc.mClass = neutralStubClassId();
        store.insertStatic(npc);

        store.markNeutralStubs();
        return true;
    }

    std::size_t insertNeutralStubGlobals(ESMStore& store)
    {
        // The World's date setup reads these by name; a value of 0 is the start of a count.
        constexpr std::array<std::string_view, 1> sNames{ "month" };
        std::size_t added = 0;
        for (const std::string_view name : sNames)
        {
            const ESM::RefId id = ESM::RefId::stringRefId(name);
            if (store.get<ESM::Global>().search(id) != nullptr)
                continue;
            ESM::Global record;
            record.mId = id;
            record.mValue = ESM::Variant(0);
            record.mRecordFlags = 0;
            store.insertStatic(record);
            ++added;
        }
        return added;
    }

    std::size_t insertNeutralStubNewGameGlobals(ESMStore& store)
    {
        constexpr std::array<std::string_view, 1> sNames{ "chargenstate" };
        std::size_t added = 0;
        for (const std::string_view name : sNames)
        {
            const ESM::RefId id = ESM::RefId::stringRefId(name);
            if (store.get<ESM::Global>().search(id) != nullptr)
                continue;
            ESM::Global record;
            record.mId = id;
            record.mValue = ESM::Variant(0);
            record.mRecordFlags = 0;
            store.insertStatic(record);
            ++added;
        }
        return added;
    }

    std::size_t insertNeutralStubGameSettings(ESMStore& store)
    {
        std::size_t added = 0;
        const auto add = [&](std::string_view name, const ESM::Variant& value) {
            const ESM::RefId id = ESM::RefId::stringRefId(name);
            if (store.get<ESM::GameSetting>().search(id) != nullptr)
                return;
            ESM::GameSetting record;
            record.mId = id;
            record.mValue = value;
            record.mRecordFlags = 0;
            store.insertStatic(record);
            ++added;
        };
        // The one table of Morrowind's default numeric game settings is OpenMW-CS's, in components/esm3.
        for (std::size_t i = 0; i < ESM::DefaultGmsts::FloatCount; ++i)
            add(ESM::DefaultGmsts::Floats[i], ESM::Variant(ESM::DefaultGmsts::FloatsDefaultValues[i]));
        for (std::size_t i = 0; i < ESM::DefaultGmsts::IntCount; ++i)
            add(ESM::DefaultGmsts::Ints[i], ESM::Variant(ESM::DefaultGmsts::IntsDefaultValues[i]));
        return added;
    }

    std::string neutralStubGmstText(std::string_view name)
    {
        // Task skyext: the messages the game formats with arguments (the l10n "gmst" lookup of Mechanics: skill changes
        // and the days in prison) need real wording with printf-style tokens; the rest keep the name-derived text.
        // The wording is our own.
        if (name == "sNotifyMessage39")
            return "Your %s skill increased to %d.";
        if (name == "sNotifyMessage44")
            return "Your %s skill decreased to %d.";
        if (name == "sNotifyMessage42")
            return "%d day has passed in prison.";
        if (name == "sNotifyMessage43")
            return "%d days have passed in prison.";
        if (!name.empty() && name.front() == 's')
            name.remove_prefix(1);
        std::string text(name);
        std::replace(text.begin(), text.end(), '_', ' ');
        return text;
    }

    std::size_t insertNeutralStubGameSettingStrings(ESMStore& store)
    {
        std::size_t added = 0;
        for (std::size_t i = 0; i < ESM::DefaultGmsts::StringCount; ++i)
        {
            const ESM::RefId id = ESM::RefId::stringRefId(ESM::DefaultGmsts::Strings[i]);
            if (store.get<ESM::GameSetting>().search(id) != nullptr)
                continue;
            ESM::GameSetting record;
            record.mId = id;
            record.mValue = ESM::Variant(neutralStubGmstText(ESM::DefaultGmsts::Strings[i]));
            record.mRecordFlags = 0;
            store.insertStatic(record);
            ++added;
        }
        return added;
    }

    std::size_t insertNeutralStubSkills(ESMStore& store)
    {
        std::size_t added = 0;
        for (int index = 0; index < ESM::Skill::Length; ++index)
        {
            const ESM::RefId id = ESM::Skill::indexToRefId(index);
            if (store.get<ESM::Skill>().search(id) != nullptr)
                continue;
            ESM::Skill skill;
            skill.blank();
            skill.mId = *id.getIf<ESM::SkillId>();
            store.insertStatic(skill);
            ++added;
        }
        return added;
    }

    std::size_t insertNeutralStubMagicEffects(ESMStore& store)
    {
        std::size_t added = 0;
        for (int index = 0; index < ESM::MagicEffect::Length; ++index)
        {
            const ESM::RefId id = ESM::MagicEffect::indexToRefId(index);
            if (store.get<ESM::MagicEffect>().search(id) != nullptr)
                continue;
            ESM::MagicEffect effect;
            effect.blank();
            effect.mId = id;
            store.insertStatic(effect);
            ++added;
        }
        return added;
    }
}
