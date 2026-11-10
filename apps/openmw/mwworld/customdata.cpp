#include "customdata.hpp"

#include <sstream>
#include <stdexcept>

namespace MWWorld
{
    void CustomData::throwBadCast(const std::type_info& from, const std::type_info& to)
    {
        std::stringstream error;
        error << "bad cast " << from.name() << " to " << to.name();
        throw std::logic_error(error.str());
    }
}
