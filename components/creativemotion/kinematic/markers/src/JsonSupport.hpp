#pragma once

// The JSON helpers moved to core/format (M2) so the rig loader can share
// them. This alias keeps the markers sources' detail:: names working.

#include "opas/format/JsonSupport.hpp"

namespace opas::markers {
namespace detail = ::opas::format::detail;
} // namespace opas::markers
