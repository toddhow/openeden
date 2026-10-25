#include "load_error.hpp"

namespace openeden::plugin
{
    std::string LoadError::toString() const
    {
        return file + ":" + std::to_string(line) + ": " + (field.empty() ? "(document)" : field) + ": " + message;
    }
} // namespace openeden::plugin
