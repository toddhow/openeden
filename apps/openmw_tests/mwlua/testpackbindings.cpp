// Task 0097 stage A: script bindings for game packs (scalars, read only).
//
// In plain words: a game pack declares namespaces of functions through the BindingsService it gets from the host; the
// core turns each into a read-only script package "core.<game>.<area>". These tests use a stub game pack over the real
// host, lifecycle manager and script state: who gets the service, which declarations are refused, what a script sees,
// what a bad call says, and that nothing calls into a pack that has gone.
#include <components/extension/bindings_store.hpp>
#include <components/extension/host_impl.hpp>
#include <components/extension/lifecycle_manager.hpp>
#include <components/extension/manifest.hpp>
#include <components/lua/configuration.hpp>
#include <components/lua/luastate.hpp>
#include <components/testing/util.hpp>

#include <apps/openmw/mwlua/packbindings.hpp>

#include <openeden/extension.hpp>
#include <openeden/game/bindings.hpp>
#include <openeden/pack_kind.hpp>

#include <gtest/gtest.h>

#include <array>
#include <cmath>
#include <functional>
#include <memory>
#include <stdexcept>
#include <string>
#include <vector>

#include "apps/openmw_tests/support/allocationcounter.hpp"

namespace
{
    using namespace openeden::sdk;
    using openeden::extension::BindingsStore;
    using openeden::extension::HostImpl;
    using openeden::extension::LifecycleManager;

    class LambdaHandler final : public BindingHandler
    {
    public:
        using Fn = std::function<BindingResult(std::span<const BindingValue>)>;
        explicit LambdaHandler(Fn fn)
            : mFn(std::move(fn))
        {
        }
        BindingResult call(std::span<const BindingValue> args) override
        {
            ++mCalls;
            return mFn(args);
        }
        int mCalls = 0;

    private:
        Fn mFn;
    };

    BindingResult okInt(std::int32_t v)
    {
        BindingResult r;
        r.value.type = BindingType::Int32;
        r.value.integer = v;
        return r;
    }

    BindingResult okBool(bool v)
    {
        BindingResult r;
        r.value.type = BindingType::Bool;
        r.value.boolean = v;
        return r;
    }

    BindingResult okDouble(double v)
    {
        BindingResult r;
        r.value.type = BindingType::Double;
        r.value.number = v;
        return r;
    }

    BindingResult okText(std::string_view v)
    {
        BindingResult r;
        r.value.type = BindingType::String;
        r.value.text = v;
        return r;
    }

    BindingResult fail(BindingStatus status, std::string_view message)
    {
        BindingResult r;
        r.status = status;
        r.message = message;
        return r;
    }

    constexpr std::array<BindingType, 2> kIntInt = { BindingType::Int32, BindingType::Int32 };
    constexpr std::array<BindingType, 1> kInt = { BindingType::Int32 };
    constexpr std::array<BindingType, 1> kBool = { BindingType::Bool };
    constexpr std::array<BindingType, 1> kDouble = { BindingType::Double };
    constexpr std::array<BindingType, 1> kString = { BindingType::String };

    /// The stub game pack. `mDeclare` runs in start() with the service the host gave it (null if it got none).
    class StubPack final : public openeden::sdk::Extension
    {
    public:
        void registerServices(openeden::sdk::Registrar&) override {}
        bool resolve(const openeden::sdk::ServiceLocator&) override { return true; }
        void start(openeden::sdk::Host& host) override
        {
            mService = host.getBindingsService();
            if (mDeclare && mService != nullptr)
                mDeclare(*mService);
        }
        void stop() override {}

        std::function<void(BindingsService&)> mDeclare;
        BindingsService* mService = nullptr;
    };

    openeden::extension::ExtensionManifest manifest(const std::string& id, PackKind kind)
    {
        openeden::extension::ExtensionManifest result;
        result.version = kind == PackKind::Game ? 2 : 1;
        result.id = id;
        result.name = id;
        result.extensionVersion = "1.0.0";
        result.kind = std::string(toString(kind));
        result.toolchain = "t";
        return result;
    }

    /// A host with one stub pack of the given kind and id; the pack is started by initialize().
    struct Session
    {
        HostImpl host;
        LifecycleManager manager{ host };
        StubPack* pack = nullptr;
        LambdaHandler getValue{ [](std::span<const BindingValue> a) { return okInt(a[0].integer + a[1].integer); } };
        LambdaHandler isBig{ [](std::span<const BindingValue> a) { return okBool(a[0].number > 100.0); } };
        LambdaHandler half{ [](std::span<const BindingValue> a) { return okDouble(a[0].number / 2.0); } };
        LambdaHandler shout{ [](std::span<const BindingValue> a) {
            static std::string buffer;
            buffer.assign(a[0].text);
            buffer += "!";
            return okText(buffer);
        } };

        explicit Session(PackKind kind = PackKind::Game, const std::string& id = "testgame")
        {
            auto stub = std::make_unique<StubPack>();
            pack = stub.get();
            manager.addExtension(manifest(id, kind), std::move(stub));
        }

        std::vector<FunctionDescriptor> functions()
        {
            return { FunctionDescriptor{ "add", kIntInt, true, BindingType::Int32, BindingAccess::Read, &getValue, 0 },
                FunctionDescriptor{ "is_big", kDouble, true, BindingType::Bool, BindingAccess::Read, &isBig, 0 },
                FunctionDescriptor{ "half", kDouble, true, BindingType::Double, BindingAccess::Read, &half, 0 },
                FunctionDescriptor{ "shout", kString, true, BindingType::String, BindingAccess::Read, &shout, 0 } };
        }

        void declareStandard()
        {
            pack->mDeclare = [this](BindingsService& service) {
                const std::vector<FunctionDescriptor> fns = functions();
                NamespaceDescriptor ns;
                ns.name = "testgame.actor";
                ns.version = { 1, 2 };
                ns.functions = fns;
                const DeclareResult result = service.declareNamespace(ns);
                mDeclareOk = result.ok;
                mDeclareMessage = result.message;
            };
        }

        bool start() { return manager.initialize(); }

        bool mDeclareOk = false;
        std::string mDeclareMessage;
    };

    DeclareResult declareOne(
        BindingsService& service, std::string_view name, Version version, std::span<const FunctionDescriptor> fns)
    {
        NamespaceDescriptor ns;
        ns.name = name;
        ns.version = version;
        ns.functions = fns;
        return service.declareNamespace(ns);
    }

    // ---- who gets the service ----

    TEST(PackBindingsHandOut, a_game_pack_gets_the_service)
    {
        Session session;
        ASSERT_TRUE(session.start());
        EXPECT_NE(session.pack->mService, nullptr);
    }

    TEST(PackBindingsHandOut, an_ordinary_extension_gets_nullptr_and_a_denial_is_recorded)
    {
        Session session(PackKind::Feature, "feature_x");
        ASSERT_TRUE(session.start());
        EXPECT_EQ(session.pack->mService, nullptr);
        bool found = false;
        for (const auto& denial : session.manager.getCapabilityDenials())
            found = found || denial.what == "script bindings service";
        EXPECT_TRUE(found);
    }

    TEST(PackBindingsHandOut, a_compatibility_pack_gets_nullptr)
    {
        Session session(PackKind::Compatibility, "compat_x");
        ASSERT_TRUE(session.start());
        EXPECT_EQ(session.pack->mService, nullptr);
    }

    // ---- declaration validation ----

    struct Validation : testing::Test
    {
        Session session;
        BindingsService* service = nullptr;
        std::vector<FunctionDescriptor> fns;

        void SetUp() override
        {
            ASSERT_TRUE(session.start());
            service = session.pack->mService;
            ASSERT_NE(service, nullptr);
            fns = session.functions();
        }
    };

    TEST_F(Validation, a_good_declaration_is_stored_in_declaration_order)
    {
        const DeclareResult result = declareOne(*service, "testgame.actor", { 1, 0 }, fns);
        ASSERT_TRUE(result.ok) << result.message;
        const auto& namespaces = session.host.bindings().namespaces();
        ASSERT_EQ(namespaces.size(), 1u);
        ASSERT_EQ(namespaces[0]->functions.size(), 4u);
        EXPECT_EQ(namespaces[0]->functions[0].name, "add");
        EXPECT_EQ(namespaces[0]->functions[1].name, "is_big");
        EXPECT_EQ(namespaces[0]->functions[2].name, "half");
        EXPECT_EQ(namespaces[0]->functions[3].name, "shout");
    }

    TEST_F(Validation, a_name_not_starting_with_the_session_game_id_is_refused)
    {
        const DeclareResult result = declareOne(*service, "othergame.actor", { 1, 0 }, fns);
        EXPECT_FALSE(result.ok);
        EXPECT_NE(result.message.find("game id 'testgame'"), std::string::npos) << result.message;
        EXPECT_TRUE(session.host.bindings().namespaces().empty());
    }

    TEST_F(Validation, a_name_that_is_not_game_dot_area_is_refused)
    {
        for (const char* name : { "testgame", "testgame.", ".actor", "testgame.Actor", "testgame.9a", "testgame.a-b",
                 "testgame.a.b", "", "TestGame.actor" })
        {
            EXPECT_FALSE(declareOne(*service, name, { 1, 0 }, fns).ok) << name;
        }
    }

    TEST_F(Validation, a_duplicate_namespace_is_refused)
    {
        ASSERT_TRUE(declareOne(*service, "testgame.actor", { 1, 0 }, fns).ok);
        const DeclareResult second = declareOne(*service, "testgame.actor", { 1, 1 }, fns);
        EXPECT_FALSE(second.ok);
        EXPECT_NE(second.message.find("already declared"), std::string::npos);
    }

    TEST_F(Validation, major_version_zero_is_refused)
    {
        EXPECT_FALSE(declareOne(*service, "testgame.actor", { 0, 5 }, fns).ok);
    }

    TEST_F(Validation, a_duplicate_function_a_bad_function_name_or_no_handler_is_refused)
    {
        std::vector<FunctionDescriptor> twice = { fns[0], fns[0] };
        EXPECT_FALSE(declareOne(*service, "testgame.a", { 1, 0 }, twice).ok);

        std::vector<FunctionDescriptor> badName = { fns[0] };
        badName[0].name = "GetValue";
        EXPECT_FALSE(declareOne(*service, "testgame.b", { 1, 0 }, badName).ok);

        std::vector<FunctionDescriptor> noHandler = { fns[0] };
        noHandler[0].handler = nullptr;
        EXPECT_FALSE(declareOne(*service, "testgame.c", { 1, 0 }, noHandler).ok);
        EXPECT_TRUE(session.host.bindings().namespaces().empty());
    }

    TEST_F(Validation, more_than_eight_parameters_is_refused_and_eight_is_fine)
    {
        std::array<BindingType, 9> nine;
        nine.fill(BindingType::Int32);
        std::vector<FunctionDescriptor> many = { fns[0] };
        many[0].params = std::span<const BindingType>(nine.data(), 9);
        EXPECT_FALSE(declareOne(*service, "testgame.many", { 1, 0 }, many).ok);
        many[0].params = std::span<const BindingType>(nine.data(), 8);
        EXPECT_TRUE(declareOne(*service, "testgame.eight", { 1, 0 }, many).ok);
    }

    TEST_F(Validation, more_than_64_namespaces_or_256_functions_is_refused)
    {
        for (int i = 0; i < 64; ++i)
            ASSERT_TRUE(declareOne(*service, "testgame.n" + std::to_string(i), { 1, 0 }, fns).ok) << i;
        EXPECT_FALSE(declareOne(*service, "testgame.n64", { 1, 0 }, fns).ok);

        std::vector<std::string> names;
        for (int i = 0; i < 257; ++i)
            names.push_back("f" + std::to_string(i));
        std::vector<FunctionDescriptor> big;
        for (const std::string& name : names)
        {
            FunctionDescriptor fn = fns[0];
            fn.name = name;
            big.push_back(fn);
        }
        // Over the namespace cap now, so use a fresh session for the function cap
        Session other;
        ASSERT_TRUE(other.start());
        EXPECT_FALSE(declareOne(*other.pack->mService, "testgame.big", { 1, 0 }, big).ok);
        big.pop_back();
        EXPECT_TRUE(declareOne(*other.pack->mService, "testgame.big", { 1, 0 }, big).ok);
    }

    // ---- scripts ----

    // A protected call: a script error comes back as a result instead of leaving as an exception
    sol::protected_function_result callRun(sol::table& script)
    {
        sol::protected_function run = script["run"];
        return run();
    }

    constexpr VFS::Path::NormalizedView kScriptPath("test/pack.lua");

    struct ScriptSession
    {
        Session session;
        std::unique_ptr<TestingOpenMW::VFSTestFile> file;
        std::unique_ptr<VFS::Manager> vfs;
        LuaUtil::ScriptsConfiguration cfg;
        std::unique_ptr<LuaUtil::LuaState> lua;

        // `body` is the text of a function run(...) in a script that has the pack's first package as `p`
        explicit ScriptSession(const std::string& body, bool declare = true)
        {
            if (declare)
                session.declareStandard();
            if (!session.start())
                throw std::runtime_error("session did not start");
            file = std::make_unique<TestingOpenMW::VFSTestFile>(
                "local ok, p = pcall(require, 'core.testgame.actor')\n"
                "return { run = function(...) "
                + body + " end, hasPackage = ok, package = p }");
            vfs = TestingOpenMW::createTestVFS({ { kScriptPath, file.get() } });
            lua = std::make_unique<LuaUtil::LuaState>(vfs.get(), &cfg);
        }

        sol::table load(bool withPackage = true)
        {
            std::map<std::string, sol::main_object> packages;
            lua->protectedCall([&](LuaUtil::LuaView& view) {
                for (auto& [name, object] : MWLua::initPackPackages(view.sol(), session.host.bindings()))
                    if (withPackage)
                        packages.emplace(name, sol::main_object(object));
            });
            return lua->runInNewSandbox(VFS::Path::Normalized(kScriptPath), "test", packages);
        }
    };

    TEST(PackBindingsLua, a_scalar_call_returns_the_value_of_each_type)
    {
        ScriptSession s("return p.add(3, 4), p.is_big(250.5), p.half(5), p.shout('hey')");
        sol::table script = s.load();
        sol::protected_function_result result = callRun(script);
        ASSERT_TRUE(result.valid());
        EXPECT_EQ(result.get<int>(0), 7);
        EXPECT_TRUE(result.get<bool>(1));
        EXPECT_DOUBLE_EQ(result.get<double>(2), 2.5);
        EXPECT_EQ(result.get<std::string>(3), "hey!");
    }

    TEST(PackBindingsLua, the_package_is_read_only_and_strict)
    {
        ScriptSession s("p.add = nil");
        sol::table script = s.load();
        EXPECT_FALSE(callRun(script).valid());

        ScriptSession t("return p.nothing");
        sol::table script2 = t.load();
        sol::protected_function_result result = callRun(script2);
        ASSERT_FALSE(result.valid());
        EXPECT_NE(std::string(result.get<sol::error>().what()).find("Key not found"), std::string::npos);
    }

    TEST(PackBindingsLua, the_package_has_every_function_and_the_store_keeps_declaration_order)
    {
        ScriptSession s(
            "local names = {} for k in pairs(p) do names[#names + 1] = k end table.sort(names)\n"
            "return table.concat(names, ',')");
        sol::table script = s.load();
        sol::protected_function_result result = callRun(script);
        ASSERT_TRUE(result.valid());
        EXPECT_EQ(result.get<std::string>(), "add,half,is_big,shout");
        const auto& fns = s.session.host.bindings().namespaces()[0]->functions;
        EXPECT_EQ(fns[0].name, "add");
        EXPECT_EQ(fns[1].name, "is_big");
    }

    TEST(PackBindingsLua, a_sandbox_that_is_not_given_the_package_cannot_require_it)
    {
        ScriptSession s("return 1");
        sol::table script = s.load(false);
        EXPECT_FALSE(script["hasPackage"].get<bool>());
    }

    TEST(PackBindingsLua, the_package_is_named_core_game_area)
    {
        ScriptSession s("return 1");
        sol::table script = s.load();
        EXPECT_TRUE(script["hasPackage"].get<bool>());
    }

    std::string errorOf(ScriptSession& s, sol::table& script)
    {
        sol::protected_function_result result = callRun(script);
        EXPECT_FALSE(result.valid());
        return result.valid() ? std::string() : std::string(result.get<sol::error>().what());
    }

    TEST(PackBindingsLua, a_wrong_argument_count_names_the_function)
    {
        ScriptSession s("return p.add(1)");
        sol::table script = s.load();
        const std::string error = errorOf(s, script);
        EXPECT_NE(error.find("core.testgame.actor.add"), std::string::npos) << error;
        EXPECT_NE(error.find("expected 2 arguments, got 1"), std::string::npos) << error;
        EXPECT_EQ(s.session.getValue.mCalls, 0);
    }

    TEST(PackBindingsLua, a_wrong_type_names_function_and_argument_and_never_reaches_the_pack)
    {
        ScriptSession s("return p.add(1, 'x')");
        sol::table script = s.load();
        const std::string error = errorOf(s, script);
        EXPECT_NE(error.find("core.testgame.actor.add"), std::string::npos) << error;
        EXPECT_NE(error.find("argument #2"), std::string::npos) << error;
        EXPECT_EQ(s.session.getValue.mCalls, 0);
    }

    TEST(PackBindingsLua, an_integer_out_of_int32_a_fraction_or_nan_is_an_error_not_a_clamp)
    {
        for (const char* code : { "return p.add(2147483648, 0)", "return p.add(-2147483649, 0)", "return p.add(1.5, 0)",
                 "return p.add(0/0, 0)", "return p.add(math.huge, 0)" })
        {
            ScriptSession s(code);
            sol::table script = s.load();
            const std::string error = errorOf(s, script);
            EXPECT_NE(error.find("argument #1"), std::string::npos) << code << ": " << error;
            EXPECT_EQ(s.session.getValue.mCalls, 0) << code;
        }
        ScriptSession edge("return p.add(2147483647, -2147483648)");
        sol::table script = edge.load();
        sol::protected_function_result result = callRun(script);
        ASSERT_TRUE(result.valid());
        EXPECT_EQ(result.get<int>(), -1);
    }

    TEST(PackBindingsLua, a_string_argument_over_64_KiB_is_refused_and_a_64_KiB_result_is_fine)
    {
        ScriptSession tooLong("return p.shout(string.rep('a', 65537))");
        sol::table script = tooLong.load();
        EXPECT_NE(errorOf(tooLong, script).find("64 KiB"), std::string::npos);
        EXPECT_EQ(tooLong.session.shout.mCalls, 0);

        ScriptSession exact("return #p.shout(string.rep('a', 65535))");
        sol::table script2 = exact.load();
        sol::protected_function_result result = callRun(script2);
        ASSERT_TRUE(result.valid());
        EXPECT_EQ(result.get<int>(), 65536);
    }

    TEST(PackBindingsLua, a_status_other_than_ok_is_a_located_error_with_the_pack_text)
    {
        ScriptSession s("return p.add(1, 2)");
        s.session.getValue = LambdaHandler(
            [](std::span<const BindingValue>) { return fail(BindingStatus::NotFound, "no such actor value"); });
        // The handler object is replaced in place; the declaration points at it
        sol::table script = s.load();
        const std::string error = errorOf(s, script);
        EXPECT_NE(error.find("core.testgame.actor.add: NotFound: no such actor value"), std::string::npos) << error;
    }

    TEST(PackBindingsLua, every_failure_status_is_named)
    {
        for (const auto& [status, name] : { std::pair{ BindingStatus::BadArgument, "BadArgument" },
                 std::pair{ BindingStatus::Refused, "Refused" }, std::pair{ BindingStatus::Internal, "Internal" } })
        {
            ScriptSession s("return p.add(1, 2)");
            const BindingStatus st = status;
            s.session.getValue = LambdaHandler([st](std::span<const BindingValue>) { return fail(st, "why"); });
            sol::table script = s.load();
            EXPECT_NE(errorOf(s, script).find(std::string(name) + ": why"), std::string::npos) << name;
        }
    }

    TEST(PackBindingsLua, an_escaped_exception_becomes_a_lua_error_and_is_counted_once_per_function)
    {
        ScriptSession s("return p.add(1, 2)");
        s.session.getValue
            = LambdaHandler([](std::span<const BindingValue>) -> BindingResult { throw std::runtime_error("boom"); });
        sol::table script = s.load();
        EXPECT_NE(errorOf(s, script).find("Internal: an exception escaped"), std::string::npos);
        EXPECT_NE(errorOf(s, script).find("Internal"), std::string::npos);
        EXPECT_EQ(s.session.host.bindings().namespaces()[0]->functions[0].escapedExceptions, 2u);
    }

    TEST(PackBindingsLua, a_result_of_the_wrong_type_is_internal_and_not_passed_on)
    {
        ScriptSession s("return p.add(1, 2)");
        s.session.getValue = LambdaHandler([](std::span<const BindingValue>) { return okText("seven"); });
        sol::table script = s.load();
        EXPECT_NE(errorOf(s, script).find("wrong type"), std::string::npos);
    }

    TEST(PackBindingsLua, a_nan_result_is_returned_as_is)
    {
        ScriptSession s("return p.half(1)");
        s.session.half = LambdaHandler([](std::span<const BindingValue>) { return okDouble(std::nan("")); });
        sol::table script = s.load();
        sol::protected_function_result result = callRun(script);
        ASSERT_TRUE(result.valid());
        const double value = result.get<double>();
        EXPECT_NE(value, value);
    }

    TEST(PackBindingsLua, after_the_pack_is_released_a_call_is_an_error_and_never_reaches_the_pack)
    {
        ScriptSession s("return p.add(1, 2)");
        sol::table script = s.load();
        ASSERT_TRUE(callRun(script).valid());
        ASSERT_EQ(s.session.getValue.mCalls, 1);

        s.session.host.releaseExtensionObjects();
        const std::string error = errorOf(s, script);
        EXPECT_NE(error.find("no longer loaded"), std::string::npos) << error;
        EXPECT_EQ(s.session.getValue.mCalls, 1);
    }

    TEST(PackBindingsLua, disabling_the_pack_retires_its_handlers_too)
    {
        ScriptSession s("return p.add(1, 2)");
        sol::table script = s.load();
        s.session.host.disableExtension("testgame");
        EXPECT_NE(errorOf(s, script).find("no longer loaded"), std::string::npos);
        EXPECT_EQ(s.session.getValue.mCalls, 0);
    }

    TEST(PackBindingsLua, a_pack_that_declares_nothing_adds_no_package)
    {
        ScriptSession s("return 1", false);
        std::map<std::string, sol::object> packages;
        s.lua->protectedCall(
            [&](LuaUtil::LuaView& view) { packages = MWLua::initPackPackages(view.sol(), s.session.host.bindings()); });
        EXPECT_TRUE(packages.empty());
    }

    TEST(PackBindingsLua, a_scalar_call_allocates_nothing_after_warm_up)
    {
        ScriptSession s("local add = p.add local x = 0 for i = 1, 1000 do x = x + add(i, 1) end return x");
        sol::table script = s.load();
        sol::protected_function run = script["run"];
        ASSERT_TRUE(sol::protected_function_result(run()).valid());

        // The counter counts operator new on this thread; the call path uses the Lua C API and a fixed argument array.
        sol::protected_function_result result;
        std::size_t allocations = 0;
        {
            TestSupport::AllocationCounter counter;
            result = run();
            allocations = counter.count();
        }
        ASSERT_TRUE(result.valid());
        EXPECT_EQ(allocations, 0u);
    }
}
