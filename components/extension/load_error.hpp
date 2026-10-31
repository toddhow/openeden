#pragma once

#include <cstddef>
#include <string>

namespace openeden::extension
{
    /**
     * @brief Where and why a manifest file was rejected.
     */
    struct LoadError
    {
        std::string file; ///< File path as supplied by caller
        std::size_t line = 0; ///< 1-based line number (0 if unknown)
        std::string field; ///< JSON pointer (e.g. "/id", "/provides/0/contract") or empty for document
        std::string message; ///< Descriptive reason for failure

        /**
         * @brief Formats error as "file:line: field: message".
         */
        [[nodiscard]] std::string toString() const;
    };
} // namespace openeden::extension
