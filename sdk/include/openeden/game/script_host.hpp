#pragma once

#include <string_view>

namespace openeden::sdk
{
    /**
     * @brief The game pack's side of the script runtimes (decision 0014: the runtimes live in the common pack behind
     * the openeden.script.* contracts). In this SDK version a pack only declares which language it runs; the function
     * tables and the state records come through those contracts. Game packs only. Main thread only.
     */
    class ScriptHostService
    {
    public:
        virtual ~ScriptHostService() = default;

        /// False for an empty name or a language already declared.
        [[nodiscard]] virtual bool declareLanguage(std::string_view name) = 0;
        [[nodiscard]] virtual bool isLanguageDeclared(std::string_view name) const = 0;
    };
} // namespace openeden::sdk
