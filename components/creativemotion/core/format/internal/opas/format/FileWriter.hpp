#pragma once

// Internal: canonical-text helpers shared by the format writers.

#include <cstddef>
#include <string>
#include <vector>

namespace opas::format::detail {

// The value of a top-level array: "[]" when empty, else one entry per element
// (each produced by `write`, already indented), comma-separated, one per line.
template <typename Item, typename Write>
std::string arrayBlock(const std::vector<Item>& items, Write write)
{
    if (items.empty()) {
        return "[]";
    }
    std::string out = "[\n";
    for (std::size_t i = 0; i < items.size(); ++i) {
        out += write(items[i]);
        out += i + 1 < items.size() ? ",\n" : "\n";
    }
    out += "  ]";
    return out;
}

} // namespace opas::format::detail
