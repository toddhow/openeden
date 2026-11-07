#pragma once

#include <string_view>

namespace openeden::sdk
{
    /**
     * @brief A game pack's windows. The toolkit stays in the core; this SDK version only records the declaration
     * (the window description and callbacks come with task 0085). Game packs only. Main thread only.
     */
    class UiService
    {
    public:
        virtual ~UiService() = default;

        /// False for an empty id or an id already declared.
        [[nodiscard]] virtual bool declareWindow(std::string_view id, std::string_view title) = 0;
        [[nodiscard]] virtual bool isWindowDeclared(std::string_view id) const = 0;
    };
} // namespace openeden::sdk
