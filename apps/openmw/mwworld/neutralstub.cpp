#include "neutralstub.hpp"

#include <array>
#include <string_view>

#include <components/esm3/defaultgmsts.hpp>
#include <components/esm3/loadclas.hpp>
#include <components/esm3/loadglob.hpp>
#include <components/esm3/loadgmst.hpp>
#include <components/esm3/loadnpc.hpp>
#include <components/esm3/loadrace.hpp>

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
}
