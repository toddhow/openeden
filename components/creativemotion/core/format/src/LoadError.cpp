#include "opas/format/LoadError.hpp"

namespace opas::format {

std::string LoadError::toString() const
{
    return file + ":" + std::to_string(line) + ": " + (field.empty() ? "(document)" : field) +
           ": " + message;
}

} // namespace opas::format
