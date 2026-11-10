#include "doordestination.hpp"

#include "../mwbase/environment.hpp"
#include "../mwbase/world.hpp"

#include "worldmodel.hpp"

namespace MWWorld
{
    std::string getDoorDestination(const LiveCellRef<ESM::Door>& door)
    {
        std::string_view dest = MWBase::Environment::get().getWorld()->getCellName(
            &MWBase::Environment::get().getWorldModel()->getCell(door.mRef.getDestCell()));

        return "#{sCell=" + std::string{ dest } + "}";
    }
}
