#ifndef OPENEDEN_COMPONENTS_TESTS_UI_UILUAFIXTURE_H
#define OPENEDEN_COMPONENTS_TESTS_UI_UILUAFIXTURE_H

#include <gtest/gtest.h>

#include <filesystem>
#include <memory>
#include <string>

#include <sol/sol.hpp>

#include <components/ui/headless.hpp>
#include <components/ui_lua/backendpackage.hpp>

namespace UiTest
{
    /// A bare Lua state that can `require('openeden.ui')` from the repository's files/data, over a headless backend.
    struct UiLuaFixture : testing::Test
    {
        sol::state lua;
        Ui::HeadlessBackend backend;

        UiLuaFixture()
        {
            lua.open_libraries(sol::lib::base, sol::lib::package, sol::lib::table, sol::lib::string, sol::lib::math);
            const std::filesystem::path data = std::filesystem::path{ OPENMW_PROJECT_SOURCE_DIR } / "files" / "data";
            lua["package"]["path"] = data.generic_string() + "/?.lua";
            lua["package"]["loaded"]["openeden.uibackend"] = UiLua::makeBackendPackage(lua, [this] {
                return std::shared_ptr<Ui::Backend>(&backend, [](Ui::Backend*) {});
            });
        }

        /// Runs a chunk; a Lua error is a test failure with its message.
        bool run(const std::string& code)
        {
            sol::protected_function_result result = lua.safe_script(code, sol::script_pass_on_error);
            if (!result.valid())
            {
                sol::error error = result;
                ADD_FAILURE() << error.what();
                return false;
            }
            return true;
        }

        template <class T>
        T get(const std::string& expression)
        {
            sol::protected_function_result result
                = lua.safe_script("return " + expression, sol::script_pass_on_error);
            if (!result.valid())
            {
                sol::error error = result;
                ADD_FAILURE() << error.what();
                return T{};
            }
            return result.get<T>();
        }

        /// The ids of the elements whose class list contains `cls` (the first word the library puts there is the
        /// component's own class).
        std::vector<Ui::ElementId> withClass(const std::string& cls) const
        {
            std::vector<Ui::ElementId> ids;
            for (const auto& [id, node] : backend.nodes())
            {
                std::string padded = " " + node.mClass + " ";
                if (padded.find(" " + cls + " ") != std::string::npos)
                    ids.push_back(id);
            }
            return ids;
        }
    };
}

#endif
