#pragma once

// Where and why a data file was rejected. Every loader (rig, profile, marker,
// body-map, pose-clip and preset files) reports errors this way.

#include <cstddef>
#include <string>

namespace opas::format {

struct LoadError {
    std::string file;     // as passed by the caller
    std::size_t line = 0; // 1-based
    std::string field;    // JSON pointer, e.g. "/markers/2/toleranceM"; "" for the whole document
    std::string message;

    // "file:line: field: message"
    [[nodiscard]] std::string toString() const;
};

} // namespace opas::format
