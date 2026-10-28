#pragma once

// Internal: a rule broken by a rig or profile, located by the JSON pointer of
// the field as it appears in the file. The validate* functions turn it into
// "<pointer>: <message>"; the loaders look the pointer up in the parsed
// document to report the line.

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>

namespace opas::rig {

struct Rig;
struct Profile;

namespace detail {

struct Problem {
    std::string pointer;
    std::string message;
};

std::optional<Problem> findRigProblem(const Rig& rig);

// Rules a profile follows on its own (ranges, duplicate overrides).
std::optional<Problem> findProfileProblem(const Profile& profile);

// Rules that need the rig: the rig name and every overridden bone.
std::optional<Problem> findProfileRigProblem(const Profile& profile, const Rig& rig);

// Names (rigs, bones) must be well-formed UTF-8 without control characters
// (U+0000 to U+001F, U+007F). Otherwise a model could pass validation yet
// write a file nlohmann/json rejects (invalid UTF-8), or collide with the
// loader's internal non-finite sentinel (which starts with a NUL).
inline bool isValidName(std::string_view name)
{
    std::size_t i = 0;
    while (i < name.size()) {
        const auto lead = static_cast<unsigned char>(name[i]);
        if (lead < 0x20U || lead == 0x7FU) {
            return false;
        }
        if (lead < 0x80U) {
            ++i;
            continue;
        }
        std::size_t continuation = 0;
        std::uint32_t codePoint = 0;
        std::uint32_t smallest = 0; // rejects overlong encodings
        if ((lead & 0xE0U) == 0xC0U) {
            continuation = 1;
            codePoint = lead & 0x1FU;
            smallest = 0x80U;
        } else if ((lead & 0xF0U) == 0xE0U) {
            continuation = 2;
            codePoint = lead & 0x0FU;
            smallest = 0x800U;
        } else if ((lead & 0xF8U) == 0xF0U) {
            continuation = 3;
            codePoint = lead & 0x07U;
            smallest = 0x10000U;
        } else {
            return false;
        }
        if (name.size() - i <= continuation) {
            return false;
        }
        for (std::size_t k = 1; k <= continuation; ++k) {
            const auto byte = static_cast<unsigned char>(name[i + k]);
            if ((byte & 0xC0U) != 0x80U) {
                return false;
            }
            codePoint = (codePoint << 6U) | (byte & 0x3FU);
        }
        if (codePoint < smallest || codePoint > 0x10FFFFU ||
            (codePoint >= 0xD800U && codePoint <= 0xDFFFU)) {
            return false;
        }
        i += continuation + 1;
    }
    return true;
}

inline constexpr const char* kInvalidNameMessage = "must be valid UTF-8 without control characters";

inline std::string toString(const std::optional<Problem>& problem)
{
    return problem ? problem->pointer + ": " + problem->message : std::string();
}

} // namespace detail
} // namespace opas::rig
