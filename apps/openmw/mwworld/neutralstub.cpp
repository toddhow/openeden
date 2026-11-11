#include "neutralstub.hpp"

#include <components/esm3/loadclas.hpp>
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
}
