#include "defaultglobals.hpp"

#include <components/esm/refid.hpp>
#include <components/esm3/loadglob.hpp>

#include "esmstore.hpp"
#include "globals.hpp"

namespace MWWorld
{
    std::vector<std::pair<GlobalVariableName, ESM::Variant>> generateDefaultGlobals()
    {
        return {
            // vanilla Morrowind does not define dayspassed.
            { Globals::sDaysPassed, ESM::Variant(1) }, // but the addons start counting at 1 :(
            { Globals::sWerewolfClawMult, ESM::Variant(25.f) },
            { Globals::sPCKnownWerewolf, ESM::Variant(0) },
            // following should exist in all versions of MW, but not necessarily in TCs
            // gamehour is a FLOAT global (Morrowind.esm defines it so): DateTimeManager::advanceTime writes the hour
            // with setFloat, which on an integer variant truncates, and a save then held the whole hour (task 0213).
            { Globals::sGameHour, ESM::Variant(0.f) },
            { Globals::sTimeScale, ESM::Variant(30.f) },
            { Globals::sDay, ESM::Variant(1) },
            { Globals::sYear, ESM::Variant(1) },
            { Globals::sPCRace, ESM::Variant(0) },
            { Globals::sPCHasCrimeGold, ESM::Variant(0) },
            { Globals::sCrimeGoldDiscount, ESM::Variant(0) },
            { Globals::sCrimeGoldTurnIn, ESM::Variant(0) },
            { Globals::sPCHasTurnIn, ESM::Variant(0) },
        };
    }

    std::size_t insertMissingDefaultGlobals(ESMStore& store)
    {
        std::size_t added = 0;
        for (const auto& [name, value] : generateDefaultGlobals())
        {
            if (store.get<ESM::Global>().search(ESM::RefId::stringRefId(name.getValue())) == nullptr)
            {
                ESM::Global record;
                record.mId = ESM::RefId::stringRefId(name.getValue());
                record.mValue = value;
                record.mRecordFlags = 0;
                store.insertStatic(record);
                ++added;
            }
        }
        return added;
    }
}
