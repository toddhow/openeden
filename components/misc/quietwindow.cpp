#include "quietwindow.hpp"

#include <cstdlib>
#include <string_view>

bool Misc::isQuietWindowValue(const char* value)
{
    if (value == nullptr)
        return false;
    const std::string_view text(value);
    return !text.empty() && text != "0";
}

bool Misc::isQuietWindowRequested()
{
    return isQuietWindowValue(std::getenv(quietWindowVariable));
}
