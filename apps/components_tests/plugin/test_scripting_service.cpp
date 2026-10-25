#include <components/plugin/host_impl.hpp>
#include <components/plugin/scripting_service.hpp>
#include <openeden/host.hpp>
#include <openeden/scripting.hpp>

#include <gtest/gtest.h>

#include <functional>
#include <map>
#include <memory>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

namespace
{
    using openeden::plugin::HostImpl;
    using openeden::plugin::ScriptingServiceImpl;
    using openeden::sdk::LuaInitCallback;
    using openeden::sdk::LuaModuleRegistrar;
    using openeden::sdk::LuaTable;
    using openeden::sdk::ScriptingService;

    // Mock Lua table capturing definitions for verification
    class MockLuaTable : public LuaTable
    {
    public:
        void setString(std::string_view key, std::string_view value) override
        {
            mStrings[std::string(key)] = std::string(value);
        }

        void setNumber(std::string_view key, double value) override { mNumbers[std::string(key)] = value; }

        void setBool(std::string_view key, bool value) override { mBools[std::string(key)] = value; }

        void setFunction(std::string_view key, std::function<int(void*)> fn) override
        {
            mFunctions[std::string(key)] = std::move(fn);
        }

        [[nodiscard]] std::string getString(std::string_view key) const
        {
            auto it = mStrings.find(std::string(key));
            return it != mStrings.end() ? it->second : "";
        }

        [[nodiscard]] double getNumber(std::string_view key) const
        {
            auto it = mNumbers.find(std::string(key));
            return it != mNumbers.end() ? it->second : 0.0;
        }

        [[nodiscard]] bool getBool(std::string_view key) const
        {
            auto it = mBools.find(std::string(key));
            return it != mBools.end() ? it->second : false;
        }

        [[nodiscard]] bool hasFunction(std::string_view key) const
        {
            return mFunctions.find(std::string(key)) != mFunctions.end();
        }

    private:
        std::map<std::string, std::string> mStrings;
        std::map<std::string, double> mNumbers;
        std::map<std::string, bool> mBools;
        std::map<std::string, std::function<int(void*)>> mFunctions;
    };

    TEST(ScriptingServiceTest, RegisterModuleCallback)
    {
        ScriptingServiceImpl service;

        EXPECT_FALSE(service.hasModule("creativemotion"));

        service.registerModule("creativemotion", [](LuaModuleRegistrar& /*registrar*/) {});

        EXPECT_TRUE(service.hasModule("creativemotion"));
        EXPECT_FALSE(service.hasModule("other_plugin"));
    }

    TEST(ScriptingServiceTest, CanonicalNamespaceFormatting)
    {
        ScriptingServiceImpl service;

        EXPECT_EQ(service.getCanonicalNamespace("creativemotion"), "openeden.creativemotion");
        EXPECT_EQ(service.getCanonicalNamespace("plugin_physics"), "openeden.plugin_physics");
        EXPECT_EQ(service.getCanonicalNamespace("gameplay_ai"), "openeden.gameplay_ai");
    }

    TEST(ScriptingServiceTest, ExecutionOfRegistrationCallback)
    {
        ScriptingServiceImpl service;

        std::string capturedNamespace;
        std::string capturedPluginId;

        service.registerModule("creativemotion", [&](LuaModuleRegistrar& registrar) {
            capturedNamespace = registrar.getCanonicalNamespace();
            capturedPluginId = registrar.getPluginId();

            LuaTable& table = registrar.getTable();
            table.setString("version", "1.4.0");
            table.setNumber("sample_rate", 60.0);
            table.setBool("ragdoll_enabled", true);
            table.setFunction("play_animation", [](void*) -> int { return 0; });
        });

        // Initialize modules in simulated / mock Lua state
        service.initializeModules();

        EXPECT_EQ(capturedPluginId, "creativemotion");
        EXPECT_EQ(capturedNamespace, "openeden.creativemotion");

        // Verify registered module's table content
        const LuaTable* table = service.getModuleTable("creativemotion");
        ASSERT_NE(table, nullptr);
        EXPECT_EQ(table->getString("version"), "1.4.0");
        EXPECT_DOUBLE_EQ(table->getNumber("sample_rate"), 60.0);
        EXPECT_TRUE(table->getBool("ragdoll_enabled"));
        EXPECT_TRUE(table->hasFunction("play_animation"));
    }

    TEST(ScriptingServiceTest, BindingRegistrationInLoadOrder)
    {
        ScriptingServiceImpl service;

        std::vector<std::string> executionOrder;

        service.registerModule(
            "plugin_gameplay", [&](LuaModuleRegistrar& /*reg*/) { executionOrder.push_back("plugin_gameplay"); });

        service.registerModule(
            "plugin_core", [&](LuaModuleRegistrar& /*reg*/) { executionOrder.push_back("plugin_core"); });

        service.registerModule(
            "plugin_physics", [&](LuaModuleRegistrar& /*reg*/) { executionOrder.push_back("plugin_physics"); });

        // Provide explicit deterministic plugin load order
        service.setPluginLoadOrder({ "plugin_core", "plugin_physics", "plugin_gameplay" });

        service.initializeModules();

        ASSERT_EQ(executionOrder.size(), 3u);
        EXPECT_EQ(executionOrder[0], "plugin_core");
        EXPECT_EQ(executionOrder[1], "plugin_physics");
        EXPECT_EQ(executionOrder[2], "plugin_gameplay");
    }

    TEST(ScriptingServiceTest, UnregisterModule)
    {
        ScriptingServiceImpl service;

        bool executed = false;
        service.registerModule("temp_plugin", [&](LuaModuleRegistrar& /*reg*/) { executed = true; });

        EXPECT_TRUE(service.hasModule("temp_plugin"));

        service.unregisterModule("temp_plugin");
        EXPECT_FALSE(service.hasModule("temp_plugin"));

        service.initializeModules();
        EXPECT_FALSE(executed);
    }

    TEST(ScriptingServiceTest, ExceptionContainmentIfCallbackThrows)
    {
        ScriptingServiceImpl service;

        bool secondPluginExecuted = false;

        service.registerModule("failing_plugin", [](LuaModuleRegistrar& /*reg*/) {
            throw std::runtime_error("Simulated Lua module syntax or runtime error");
        });

        service.registerModule("healthy_plugin", [&](LuaModuleRegistrar& /*reg*/) { secondPluginExecuted = true; });

        service.setPluginLoadOrder({ "failing_plugin", "healthy_plugin" });

        // Initializing modules must never throw out of the core service
        EXPECT_NO_THROW(service.initializeModules());

        // Healthy plugin must execute despite previous failure
        EXPECT_TRUE(secondPluginExecuted);
    }

    TEST(ScriptingServiceTest, HostImplWiresScriptingService)
    {
        HostImpl host("TestHost");
        ScriptingService* scripting = host.getScriptingService();

        ASSERT_NE(scripting, nullptr);

        scripting->registerModule("host_script_plugin", [](LuaModuleRegistrar& /*reg*/) {});
        EXPECT_TRUE(scripting->hasModule("host_script_plugin"));
    }
}
