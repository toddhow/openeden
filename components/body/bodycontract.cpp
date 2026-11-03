#include "bodycontract.hpp"

namespace Body
{
    bool restrictedTierAllowed(AgeClass package, AgeClass actor) noexcept
    {
        // Unspecified is never adult: both sides must say Adult explicitly (design doc section 14).
        return package == AgeClass::Adult && actor == AgeClass::Adult;
    }
}
