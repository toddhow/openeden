// Task 0097 stages B and C: handles, optionals, arrays, mutating calls, menu visibility and versions of the script
// bindings for game packs.
//
// In plain words: a script sees different tables depending on where it runs (global scripts get the functions that
// change the game, local scripts only the ones that read, the menu only namespaces the pack marked menuSafe); a call
// that changes the game is not run inside the script but queued and run later on the main thread in call order; a
// handle is an opaque thing a script can pass around and compare but not look into or save; every namespace says its
// version and the core can answer "is version M.m of this namespace there?". The stub game pack and host are the same
// kind as in testpackbindings.cpp (stage A).
#include <components/extension/bindings_store.hpp>
#include <components/extension/host_impl.hpp>
#include <components/extension/lifecycle_manager.hpp>
#include <components/extension/manifest.hpp>
#include <components/extension/mutate_queue.hpp>
#include <components/lua/configuration.hpp>
#include <components/lua/luastate.hpp>
#include <components/lua/serialization.hpp>
#include <components/testing/util.hpp>

#include <apps/openmw/mwlua/packbindings.hpp>

#include <openeden/extension.hpp>
#include <openeden/game/bindings.hpp>
#include <openeden/pack_kind.hpp>

#include <gtest/gtest.h>

#include <array>
#include <functional>
#include <memory>
#include <stdexcept>
#include <string>
#include <thread>
#include <vector>

#include "apps/openmw_tests/support/allocationcounter.hpp"

namespace
{
    using namespace openeden::sdk;
    using MWLua::PackContext;
    using openeden::extension::BindingsStore;
    using openeden::extension::BoundFunction;
    using openeden::extension::HostImpl;
    using openeden::extension::LifecycleManager;
    using openeden::extension::MutateQueue;

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
            mLastThread = std::this_thread::get_id();
            return mFn(args);
        }
        void setFn(Fn fn) { mFn = std::move(fn); }
        int mCalls = 0;
        std::thread::id mLastThread;

    private:
        Fn mFn;
    };

    BindingResult okValue(BindingType type)
    {
        BindingResult r;
        r.value.type = type;
        return r;
    }

    BindingResult okHandle(std::uint64_t v)
    {
        BindingResult r = okValue(BindingType::Handle);
        r.value.handle = v;
        return r;
    }

    BindingResult okText(std::string_view v)
    {
        BindingResult r = okValue(BindingType::String);
        r.value.text = v;
        return r;
    }

    BindingResult nothing()
    {
        return BindingResult{};
    }

    BindingResult fail(BindingStatus status, std::string_view message)
    {
        BindingResult r;
        r.status = status;
        r.message = message;
        return r;
    }

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

    openeden::extension::ExtensionManifest gameManifest()
    {
        openeden::extension::ExtensionManifest result;
        result.version = 2;
        result.id = "testgame";
        result.name = "testgame";
        result.extensionVersion = "1.0.0";
        result.kind = std::string(toString(PackKind::Game));
        result.toolchain = "t";
        return result;
    }

    constexpr std::array<BindingType, 1> kInt = { BindingType::Int32 };
    constexpr std::array<BindingType, 1> kHandle = { BindingType::Handle };
    constexpr std::array<BindingType, 1> kString = { BindingType::String };
    constexpr std::array<BindingType, 2> kIntInt = { BindingType::Int32, BindingType::Int32 };
    constexpr std::array<BindingType, 2> kHandleInt = { BindingType::Handle, BindingType::Int32 };
    constexpr std::array<BindingShape, 1> kOptional1 = { BindingShape::Optional };
    constexpr std::array<BindingShape, 1> kArray1 = { BindingShape::Array };
    constexpr std::array<BindingShape, 2> kSingleOptional = { BindingShape::Single, BindingShape::Optional };
    constexpr std::array<BindingShape, 2> kOptionalSingle = { BindingShape::Optional, BindingShape::Single };

    /// A host with a stub game pack "testgame" declaring "testgame.actor" (the set of functions the tests below use).
    struct Session
    {
        HostImpl host;
        LifecycleManager manager{ host };
        StubPack* pack = nullptr;

        // What the Mutate handlers saw, in order
        std::vector<std::string> log;
        // Backing store for the array results
        std::vector<BindingValue> resultElements;
        std::string scratch;

        LambdaHandler mint{ [](std::span<const BindingValue> a) {
            return okHandle(static_cast<std::uint64_t>(a[0].integer));
        } };
        LambdaHandler describe{ [this](std::span<const BindingValue> a) {
            scratch = "h=" + std::to_string(a[0].handle);
            return okText(scratch);
        } };
        LambdaHandler describeWith{ [this](std::span<const BindingValue> a) {
            scratch = "h=" + std::to_string(a[0].handle) + " n=" + std::to_string(a[1].integer);
            return okText(scratch);
        } };
        LambdaHandler stale{ [](std::span<const BindingValue>) {
            return fail(BindingStatus::NotFound, "no such actor");
        } };
        LambdaHandler setValue{ [this](std::span<const BindingValue> a) {
            log.push_back("set " + std::to_string(a[0].integer));
            return nothing();
        } };
        LambdaHandler setOther{ [this](std::span<const BindingValue> a) {
            log.push_back("other " + std::to_string(a[0].integer));
            return nothing();
        } };
        LambdaHandler note{ [this](std::span<const BindingValue> a) {
            log.push_back("note " + std::string(a[0].text));
            return nothing();
        } };
        LambdaHandler badMutate{ [](std::span<const BindingValue>) { return fail(BindingStatus::NotFound, "gone"); } };
        LambdaHandler throwingMutate{ [](std::span<const BindingValue>) -> BindingResult {
            throw std::runtime_error("boom");
        } };
        LambdaHandler optionals{ [this](std::span<const BindingValue> a) {
            scratch = std::string("present=") + (a[0].present ? "yes" : "no");
            if (a[0].present)
                scratch += " value=" + std::to_string(a[0].integer);
            return okText(scratch);
        } };
        LambdaHandler optionalAfter{ [this](std::span<const BindingValue> a) {
            scratch = "first=" + std::to_string(a[0].integer)
                + " second=" + (a[1].present ? std::to_string(a[1].integer) : std::string("none"));
            return okText(scratch);
        } };
        LambdaHandler optionalResult{ [](std::span<const BindingValue> a) {
            BindingResult r = okValue(BindingType::Int32);
            r.value.present = a[0].integer != 0;
            r.value.integer = a[0].integer;
            return r;
        } };
        LambdaHandler sumArray{ [this](std::span<const BindingValue> a) {
            std::int64_t sum = 0;
            for (std::size_t i = 0; i < a[0].count; ++i)
                sum += a[0].elements[i].integer;
            scratch = "count=" + std::to_string(a[0].count) + " sum=" + std::to_string(sum);
            return okText(scratch);
        } };
        LambdaHandler joinArray{ [this](std::span<const BindingValue> a) {
            scratch.clear();
            for (std::size_t i = 0; i < a[0].count; ++i)
                scratch += std::string(a[0].elements[i].text) + "|";
            return okText(scratch);
        } };
        LambdaHandler handleArray{ [this](std::span<const BindingValue> a) {
            scratch.clear();
            for (std::size_t i = 0; i < a[0].count; ++i)
                scratch += std::to_string(a[0].elements[i].handle) + ",";
            return okText(scratch);
        } };
        LambdaHandler rangeResult{ [this](std::span<const BindingValue> a) {
            resultElements.assign(static_cast<std::size_t>(a[0].integer), BindingValue{});
            for (std::size_t i = 0; i < resultElements.size(); ++i)
            {
                resultElements[i].type = BindingType::Int32;
                resultElements[i].integer = static_cast<std::int32_t>(i + 1);
            }
            BindingResult r = okValue(BindingType::Int32);
            r.value.elements = resultElements.data();
            r.value.count = resultElements.size();
            return r;
        } };
        LambdaHandler wrongElementResult{ [this](std::span<const BindingValue>) {
            resultElements.assign(2, BindingValue{});
            resultElements[0].type = BindingType::Int32;
            resultElements[1].type = BindingType::Bool;
            BindingResult r = okValue(BindingType::Int32);
            r.value.elements = resultElements.data();
            r.value.count = 2;
            return r;
        } };
        LambdaHandler handleResultArray{ [this](std::span<const BindingValue> a) {
            resultElements.assign(2, BindingValue{});
            for (std::size_t i = 0; i < 2; ++i)
            {
                resultElements[i].type = BindingType::Handle;
                resultElements[i].handle = static_cast<std::uint64_t>(a[0].integer) + i;
            }
            BindingResult r = okValue(BindingType::Handle);
            r.value.elements = resultElements.data();
            r.value.count = 2;
            return r;
        } };

        bool declareOk = false;
        std::string declareMessage;
        Version version{ 1, 2 };
        bool menuSafe = false;

        static FunctionDescriptor fn(std::string_view name, std::span<const BindingType> params, bool hasResult,
            BindingType result, BindingAccess access, BindingHandler* handler)
        {
            FunctionDescriptor d;
            d.name = name;
            d.params = params;
            d.hasResult = hasResult;
            d.result = result;
            d.access = access;
            d.handler = handler;
            return d;
        }

        std::vector<FunctionDescriptor> functions()
        {
            std::vector<FunctionDescriptor> f;
            f.push_back(fn("mint", kInt, true, BindingType::Handle, BindingAccess::Read, &mint));
            f.push_back(fn("describe", kHandle, true, BindingType::String, BindingAccess::Read, &describe));
            f.push_back(fn("describe_with", kHandleInt, true, BindingType::String, BindingAccess::Read, &describeWith));
            f.push_back(fn("stale", kHandle, true, BindingType::String, BindingAccess::Read, &stale));
            f.push_back(fn("set", kInt, false, BindingType::Bool, BindingAccess::Mutate, &setValue));
            f.push_back(fn("other", kInt, false, BindingType::Bool, BindingAccess::Mutate, &setOther));
            f.push_back(fn("note", kString, false, BindingType::Bool, BindingAccess::Mutate, &note));
            f.push_back(fn("bad_mutate", kInt, false, BindingType::Bool, BindingAccess::Mutate, &badMutate));
            f.push_back(fn("throwing_mutate", kInt, false, BindingType::Bool, BindingAccess::Mutate, &throwingMutate));
            FunctionDescriptor o = fn("opt", kInt, true, BindingType::String, BindingAccess::Read, &optionals);
            o.paramShapes = kOptional1;
            f.push_back(o);
            FunctionDescriptor oa
                = fn("opt_after", kIntInt, true, BindingType::String, BindingAccess::Read, &optionalAfter);
            oa.paramShapes = kSingleOptional;
            f.push_back(oa);
            FunctionDescriptor ob
                = fn("opt_before", kIntInt, true, BindingType::String, BindingAccess::Read, &optionalAfter);
            ob.paramShapes = kOptionalSingle;
            f.push_back(ob);
            FunctionDescriptor orr
                = fn("opt_result", kInt, true, BindingType::Int32, BindingAccess::Read, &optionalResult);
            orr.resultShape = BindingShape::Optional;
            f.push_back(orr);
            FunctionDescriptor sa = fn("sum", kInt, true, BindingType::String, BindingAccess::Read, &sumArray);
            sa.paramShapes = kArray1;
            f.push_back(sa);
            FunctionDescriptor ja = fn("join", kString, true, BindingType::String, BindingAccess::Read, &joinArray);
            ja.paramShapes = kArray1;
            f.push_back(ja);
            FunctionDescriptor ha
                = fn("handles", kHandle, true, BindingType::String, BindingAccess::Read, &handleArray);
            ha.paramShapes = kArray1;
            f.push_back(ha);
            FunctionDescriptor rr = fn("range", kInt, true, BindingType::Int32, BindingAccess::Read, &rangeResult);
            rr.resultShape = BindingShape::Array;
            f.push_back(rr);
            FunctionDescriptor we
                = fn("wrong_elements", kInt, true, BindingType::Int32, BindingAccess::Read, &wrongElementResult);
            we.resultShape = BindingShape::Array;
            f.push_back(we);
            FunctionDescriptor hr
                = fn("two_handles", kInt, true, BindingType::Handle, BindingAccess::Read, &handleResultArray);
            hr.resultShape = BindingShape::Array;
            f.push_back(hr);
            return f;
        }

        Session()
        {
            auto stub = std::make_unique<StubPack>();
            pack = stub.get();
            manager.addExtension(gameManifest(), std::move(stub));
            pack->mDeclare = [this](BindingsService& service) {
                const std::vector<FunctionDescriptor> fns = functions();
                NamespaceDescriptor ns;
                ns.name = "testgame.actor";
                ns.version = version;
                ns.functions = fns;
                ns.menuSafe = menuSafe;
                const DeclareResult result = service.declareNamespace(ns);
                declareOk = result.ok;
                declareMessage = result.message;
            };
        }

        bool start() { return manager.initialize(); }
    };

    // ---- running scripts ----

    sol::protected_function_result callRun(sol::table& script)
    {
        sol::protected_function run = script["run"];
        return run();
    }

    constexpr VFS::Path::NormalizedView kScriptPath("test/packb.lua");

    struct ScriptSession
    {
        Session session;
        std::unique_ptr<TestingOpenMW::VFSTestFile> file;
        std::unique_ptr<VFS::Manager> vfs;
        LuaUtil::ScriptsConfiguration cfg;
        std::unique_ptr<LuaUtil::LuaState> lua;
        std::map<std::string, sol::main_object> packages;

        explicit ScriptSession(const std::string& body, bool menuSafe = false, Version version = { 1, 2 })
        {
            session.menuSafe = menuSafe;
            session.version = version;
            if (!session.start())
                throw std::runtime_error("session did not start");
            file = std::make_unique<TestingOpenMW::VFSTestFile>(
                "local ok, p = pcall(require, 'core.testgame.actor')\n"
                "return { run = function(...) "
                + body + " end, hasPackage = ok, package = p }");
            vfs = TestingOpenMW::createTestVFS({ { kScriptPath, file.get() } });
            lua = std::make_unique<LuaUtil::LuaState>(vfs.get(), &cfg);
            // The packages for each context are built once here, from the one set of declarations
            lua->protectedCall([&](LuaUtil::LuaView& view) {
                for (auto& [name, object] :
                    MWLua::initPackPackages(view.sol(), session.host.bindings(), PackContext::Global))
                    packages.emplace("g:" + name, sol::main_object(object));
                for (auto& [name, object] :
                    MWLua::initPackPackages(view.sol(), session.host.bindings(), PackContext::Local))
                    packages.emplace("l:" + name, sol::main_object(object));
                for (auto& [name, object] :
                    MWLua::initPackPackages(view.sol(), session.host.bindings(), PackContext::Menu))
                    packages.emplace("m:" + name, sol::main_object(object));
            });
        }

        sol::table load(PackContext context, const char* name = "test")
        {
            const char* prefix = context == PackContext::Global ? "g:" : context == PackContext::Local ? "l:" : "m:";
            std::map<std::string, sol::main_object> mine;
            for (auto& [key, object] : packages)
            {
                if (key.rfind(prefix, 0) == 0)
                    mine.emplace(key.substr(2), object);
            }
            return lua->runInNewSandbox(VFS::Path::Normalized(kScriptPath), name, mine);
        }

        BindingsStore& store() { return session.host.bindings(); }
    };

    std::string errorOf(sol::table& script)
    {
        sol::protected_function_result result = callRun(script);
        EXPECT_FALSE(result.valid());
        return result.valid() ? std::string() : std::string(result.get<sol::error>().what());
    }

    bool contains(const std::string& text, const char* part)
    {
        return text.find(part) != std::string::npos;
    }

    std::string stringResult(sol::table& script)
    {
        sol::protected_function_result result = callRun(script);
        EXPECT_TRUE(result.valid());
        return result.valid() ? result.get<std::string>() : std::string("<error>");
    }

    // ---- the contract header ----

    TEST(PackBindingsContract, the_header_version_is_two_zero_after_the_appended_members)
    {
        EXPECT_EQ(kBindingsVersion.major, 2u);
        EXPECT_EQ(kBindingsVersion.minor, 0u);
    }

    // ---- declaration of the new things ----

    TEST(PackBindingsDeclare, the_standard_set_with_mutate_optional_array_and_handle_is_accepted)
    {
        Session session;
        ASSERT_TRUE(session.start());
        EXPECT_TRUE(session.declareOk) << session.declareMessage;
    }

    struct Declare : testing::Test
    {
        Session session;
        BindingsService* service = nullptr;
        LambdaHandler handler{ [](std::span<const BindingValue>) { return nothing(); } };

        void SetUp() override
        {
            session.pack->mDeclare = nullptr;
            ASSERT_TRUE(session.start());
            service = session.pack->mService;
            ASSERT_NE(service, nullptr);
        }

        DeclareResult declare(const FunctionDescriptor& f, std::string_view name = "testgame.actor")
        {
            const std::array<FunctionDescriptor, 1> fns = { f };
            NamespaceDescriptor ns;
            ns.name = name;
            ns.functions = fns;
            return service->declareNamespace(ns);
        }

        FunctionDescriptor base()
        {
            FunctionDescriptor d;
            d.name = "f";
            d.params = kInt;
            d.handler = &handler;
            return d;
        }
    };

    TEST_F(Declare, a_mutate_function_that_declares_a_result_is_refused)
    {
        FunctionDescriptor d = base();
        d.access = BindingAccess::Mutate;
        d.hasResult = true;
        d.result = BindingType::Int32;
        const DeclareResult result = declare(d);
        EXPECT_FALSE(result.ok);
        EXPECT_NE(result.message.find("Mutate"), std::string::npos);
    }

    TEST_F(Declare, a_mutate_function_without_a_result_is_accepted)
    {
        FunctionDescriptor d = base();
        d.access = BindingAccess::Mutate;
        EXPECT_TRUE(declare(d).ok);
    }

    TEST_F(Declare, parameter_shapes_must_match_the_parameter_count_or_be_empty)
    {
        FunctionDescriptor d = base();
        constexpr std::array<BindingShape, 2> two = { BindingShape::Single, BindingShape::Single };
        d.paramShapes = two;
        EXPECT_FALSE(declare(d).ok);
        d.paramShapes = kOptional1;
        EXPECT_TRUE(declare(d).ok);
    }

    TEST_F(Declare, an_unknown_shape_is_refused)
    {
        FunctionDescriptor d = base();
        constexpr std::array<BindingShape, 1> bad = { static_cast<BindingShape>(9) };
        d.paramShapes = bad;
        EXPECT_FALSE(declare(d).ok);
        FunctionDescriptor e = base();
        e.hasResult = true;
        e.resultShape = static_cast<BindingShape>(9);
        EXPECT_FALSE(declare(e, "testgame.other").ok);
    }

    TEST_F(Declare, an_unknown_access_is_refused)
    {
        FunctionDescriptor d = base();
        d.access = static_cast<BindingAccess>(7);
        EXPECT_FALSE(declare(d).ok);
    }

    // ---- who sees what ----

    TEST(PackBindingsContext, a_global_script_sees_read_and_mutate_functions)
    {
        ScriptSession s("return type(p.mint), type(p.set), type(p.range)");
        sol::table script = s.load(PackContext::Global);
        sol::protected_function_result result = callRun(script);
        ASSERT_TRUE(result.valid());
        EXPECT_EQ(result.get<std::string>(0), "function");
        EXPECT_EQ(result.get<std::string>(1), "function");
        EXPECT_EQ(result.get<std::string>(2), "function");
    }

    TEST(PackBindingsContext, a_local_script_sees_read_functions_and_a_mutate_key_is_absent)
    {
        ScriptSession s("return type(p.mint)");
        sol::table script = s.load(PackContext::Local);
        sol::protected_function_result result = callRun(script);
        ASSERT_TRUE(result.valid());
        EXPECT_EQ(result.get<std::string>(), "function");

        ScriptSession t("return p.set");
        sol::table other = t.load(PackContext::Local);
        EXPECT_TRUE(contains(errorOf(other), "Key not found"));

        ScriptSession u("p.set(1)");
        sol::table third = u.load(PackContext::Local);
        EXPECT_TRUE(contains(errorOf(third), "Key not found"));
        EXPECT_EQ(u.session.setValue.mCalls, 0);
        EXPECT_EQ(u.store().mutateQueue().pending(), 0u);
    }

    TEST(PackBindingsContext, the_menu_gets_nothing_unless_the_namespace_is_menu_safe)
    {
        ScriptSession s("return 1", /*menuSafe*/ false);
        sol::table script = s.load(PackContext::Menu);
        EXPECT_FALSE(script["hasPackage"].get<bool>());
    }

    TEST(PackBindingsContext, a_menu_safe_namespace_gives_the_menu_its_read_functions_only)
    {
        ScriptSession s("return type(p.mint)", /*menuSafe*/ true);
        sol::table script = s.load(PackContext::Menu);
        ASSERT_TRUE(script["hasPackage"].get<bool>());
        sol::protected_function_result result = callRun(script);
        ASSERT_TRUE(result.valid());
        EXPECT_EQ(result.get<std::string>(), "function");

        ScriptSession t("return p.set", /*menuSafe*/ true);
        sol::table other = t.load(PackContext::Menu);
        EXPECT_TRUE(contains(errorOf(other), "Key not found"));
    }

    // ---- mutating calls ----

    TEST(PackBindingsMutate, a_mutate_call_returns_nothing_and_does_not_run_until_the_queue_is_drained)
    {
        ScriptSession s("return select('#', p.set(5))");
        sol::table script = s.load(PackContext::Global);
        sol::protected_function_result result = callRun(script);
        ASSERT_TRUE(result.valid());
        EXPECT_EQ(result.get<int>(), 0);
        EXPECT_EQ(s.session.setValue.mCalls, 0);
        EXPECT_EQ(s.store().mutateQueue().pending(), 1u);

        const MutateQueue::DrainResult drained = s.store().mutateQueue().drain();
        EXPECT_EQ(drained.ran, 1u);
        EXPECT_EQ(s.session.log, std::vector<std::string>{ "set 5" });
        EXPECT_EQ(s.store().mutateQueue().pending(), 0u);

        // Nothing is run twice
        EXPECT_EQ(s.store().mutateQueue().drain().ran, 0u);
        EXPECT_EQ(s.session.setValue.mCalls, 1);
    }

    TEST(PackBindingsMutate, calls_run_in_the_order_they_were_made_across_functions)
    {
        ScriptSession s("p.set(1) p.other(2) p.set(3) p.note('x') p.other(4)");
        sol::table script = s.load(PackContext::Global);
        ASSERT_TRUE(callRun(script).valid());
        s.store().mutateQueue().drain();
        EXPECT_EQ(s.session.log, (std::vector<std::string>{ "set 1", "other 2", "set 3", "note x", "other 4" }));
    }

    TEST(PackBindingsMutate, calls_from_two_scripts_keep_the_order_they_were_made_in)
    {
        ScriptSession a("p.set(10) p.set(30)");
        sol::table first = a.load(PackContext::Global, "one");
        sol::table second = a.load(PackContext::Global, "two");

        ASSERT_TRUE(callRun(first).valid());
        ASSERT_TRUE(callRun(second).valid());
        ASSERT_TRUE(callRun(first).valid());
        a.store().mutateQueue().drain();
        EXPECT_EQ(
            a.session.log, (std::vector<std::string>{ "set 10", "set 30", "set 10", "set 30", "set 10", "set 30" }));
    }

    TEST(PackBindingsMutate, arguments_are_checked_before_queueing)
    {
        ScriptSession s("p.set('five')");
        sol::table script = s.load(PackContext::Global);
        EXPECT_TRUE(contains(errorOf(script), "argument #1"));
        EXPECT_EQ(s.store().mutateQueue().pending(), 0u);

        ScriptSession t("p.set(1, 2)");
        sol::table other = t.load(PackContext::Global);
        EXPECT_TRUE(contains(errorOf(other), "expected 1 arguments"));
        EXPECT_EQ(t.store().mutateQueue().pending(), 0u);
    }

    TEST(PackBindingsMutate, a_string_argument_is_copied_so_the_script_cannot_change_it_afterwards)
    {
        ScriptSession s(
            "local text = string.rep('abc', 100) .. 'end' p.note(text) text = nil "
            "local junk = {} for i = 1, 2000 do junk[i] = string.rep('z', 50) .. i end");
        sol::table script = s.load(PackContext::Global);
        ASSERT_TRUE(callRun(script).valid());
        s.store().mutateQueue().drain();
        ASSERT_EQ(s.session.log.size(), 1u);
        EXPECT_EQ(s.session.log[0].substr(0, 9), "note abca");
        EXPECT_EQ(s.session.log[0].size(), 5u + 303u);
        EXPECT_EQ(s.session.log[0].substr(5 + 300), "end");
    }

    TEST(PackBindingsMutate, a_full_queue_drops_further_calls_counts_them_and_raises_no_script_error)
    {
        ScriptSession s("for i = 1, 4100 do p.set(i) end");
        sol::table script = s.load(PackContext::Global);
        ASSERT_TRUE(callRun(script).valid());
        EXPECT_EQ(s.store().mutateQueue().pending(), kMaxBindingQueuedCalls);
        EXPECT_EQ(s.store().mutateQueue().droppedTotal(), 4100u - kMaxBindingQueuedCalls);

        const MutateQueue::DrainResult drained = s.store().mutateQueue().drain();
        EXPECT_EQ(drained.ran, kMaxBindingQueuedCalls);
        EXPECT_EQ(drained.functionsWithDrops, 1u);
        ASSERT_EQ(s.session.log.size(), kMaxBindingQueuedCalls);
        EXPECT_EQ(s.session.log.front(), "set 1");
        EXPECT_EQ(s.session.log.back(), "set 4096");

        // The drop counters start again after a drain
        EXPECT_EQ(s.store().mutateQueue().drain().functionsWithDrops, 0u);
        ASSERT_TRUE(callRun(script).valid());
        EXPECT_EQ(s.store().mutateQueue().pending(), kMaxBindingQueuedCalls);
    }

    BoundFunction makeMutateFunction(BindingHandler* handler, BindingType param, BindingShape shape)
    {
        BoundFunction fn;
        fn.name = "f";
        fn.qualifiedName = "testgame.x.f";
        fn.params = { param };
        fn.shapes = { shape };
        fn.access = BindingAccess::Mutate;
        fn.handler = handler;
        return fn;
    }

    TEST(PackBindingsMutate, a_call_whose_text_does_not_fit_the_arena_is_dropped_and_counted)
    {
        LambdaHandler h{ [](std::span<const BindingValue>) { return nothing(); } };
        BoundFunction fn = makeMutateFunction(&h, BindingType::String, BindingShape::Single);

        MutateQueue queue(10, /*arenaBytes*/ 100, /*arenaValues*/ 4);
        queue.reserve();
        const std::string sixty(60, 'x');
        BindingValue arg;
        arg.type = BindingType::String;
        arg.text = sixty;
        EXPECT_EQ(queue.push(fn, std::span<const BindingValue>(&arg, 1)), MutateQueue::PushResult::Queued);
        EXPECT_EQ(queue.push(fn, std::span<const BindingValue>(&arg, 1)), MutateQueue::PushResult::Dropped);
        EXPECT_EQ(queue.droppedTotal(), 1u);
        EXPECT_EQ(fn.droppedTotal, 1u);
        const MutateQueue::DrainResult drained = queue.drain();
        EXPECT_EQ(drained.ran, 1u);
        EXPECT_EQ(drained.functionsWithDrops, 1u);
        // The arena is free again after the drain
        EXPECT_EQ(queue.push(fn, std::span<const BindingValue>(&arg, 1)), MutateQueue::PushResult::Queued);
    }

    TEST(PackBindingsMutate, text_and_array_elements_are_copied_into_the_queue_not_borrowed)
    {
        std::vector<std::string> seen;
        LambdaHandler h{ [&seen](std::span<const BindingValue> a) {
            seen.push_back(std::string(a[0].text) + "/" + std::to_string(a[1].count) + ":"
                + std::string(a[1].elements[0].text) + std::string(a[1].elements[1].text));
            return nothing();
        } };
        BoundFunction fn;
        fn.name = "f";
        fn.qualifiedName = "testgame.x.f";
        fn.params = { BindingType::String, BindingType::String };
        fn.shapes = { BindingShape::Single, BindingShape::Array };
        fn.access = BindingAccess::Mutate;
        fn.handler = &h;

        MutateQueue queue(10, 1024, 16);
        queue.reserve();
        std::string text = "hello";
        std::string first = "ab";
        std::string second = "cd";
        std::array<BindingValue, 2> elements{};
        elements[0].type = BindingType::String;
        elements[0].text = first;
        elements[1].type = BindingType::String;
        elements[1].text = second;
        std::array<BindingValue, 2> args{};
        args[0].type = BindingType::String;
        args[0].text = text;
        args[1].type = BindingType::String;
        args[1].elements = elements.data();
        args[1].count = 2;
        ASSERT_EQ(queue.push(fn, args), MutateQueue::PushResult::Queued);

        // The caller's memory goes away or changes before the main thread runs the call
        text.assign(5, 'Z');
        first.assign(2, 'Y');
        second.assign(2, 'X');
        elements[0].text = {};
        elements[1].text = {};
        queue.drain();
        ASSERT_EQ(seen.size(), 1u);
        EXPECT_EQ(seen[0], "hello/2:abcd");
    }

    TEST(PackBindingsMutate, array_elements_that_do_not_fit_the_value_arena_are_dropped)
    {
        LambdaHandler h{ [](std::span<const BindingValue>) { return nothing(); } };
        BoundFunction fn = makeMutateFunction(&h, BindingType::Int32, BindingShape::Array);

        MutateQueue queue(10, 1024, /*arenaValues*/ 4);
        queue.reserve();
        std::array<BindingValue, 5> elements{};
        for (BindingValue& e : elements)
            e.type = BindingType::Int32;
        BindingValue arg;
        arg.type = BindingType::Int32;
        arg.elements = elements.data();
        arg.count = 5;
        EXPECT_EQ(queue.push(fn, std::span<const BindingValue>(&arg, 1)), MutateQueue::PushResult::Dropped);
        arg.count = 4;
        EXPECT_EQ(queue.push(fn, std::span<const BindingValue>(&arg, 1)), MutateQueue::PushResult::Queued);
    }

    TEST(PackBindingsMutate, a_queue_that_was_never_reserved_drops_everything)
    {
        LambdaHandler h{ [](std::span<const BindingValue>) { return nothing(); } };
        BoundFunction fn;
        fn.name = "f";
        fn.handler = &h;
        MutateQueue queue;
        EXPECT_EQ(queue.push(fn, {}), MutateQueue::PushResult::Dropped);
        EXPECT_EQ(queue.drain().ran, 0u);
    }

    TEST(PackBindingsMutate, the_store_reserves_the_queue_when_the_first_mutate_function_is_declared)
    {
        Session session;
        ASSERT_TRUE(session.start());
        BoundFunction& fn = session.host.bindings().namespaces()[0]->functions[4];
        ASSERT_EQ(fn.name, "set");
        BindingValue arg;
        arg.type = BindingType::Int32;
        arg.integer = 1;
        EXPECT_EQ(session.host.bindings().mutateQueue().push(fn, std::span<const BindingValue>(&arg, 1)),
            MutateQueue::PushResult::Queued);
    }

    TEST(PackBindingsMutate, a_pack_with_only_read_functions_does_not_reserve_the_queue)
    {
        Session session;
        session.pack->mDeclare = [&](BindingsService& service) {
            std::array<FunctionDescriptor, 1> fns;
            fns[0] = Session::fn("mint", kInt, true, BindingType::Handle, BindingAccess::Read, &session.mint);
            NamespaceDescriptor ns;
            ns.name = "testgame.actor";
            ns.functions = fns;
            ASSERT_TRUE(service.declareNamespace(ns).ok);
        };
        ASSERT_TRUE(session.start());
        BoundFunction& fn = session.host.bindings().namespaces()[0]->functions[0];
        BindingValue arg;
        arg.type = BindingType::Int32;
        EXPECT_EQ(session.host.bindings().mutateQueue().push(fn, std::span<const BindingValue>(&arg, 1)),
            MutateQueue::PushResult::Dropped);
    }

    TEST(PackBindingsMutate, a_call_queued_for_a_pack_that_is_gone_is_dropped_at_the_drain)
    {
        ScriptSession s("p.set(1) p.set(2)");
        sol::table script = s.load(PackContext::Global);
        ASSERT_TRUE(callRun(script).valid());
        s.store().retireAll();
        const MutateQueue::DrainResult drained = s.store().mutateQueue().drain();
        EXPECT_EQ(drained.retired, 2u);
        EXPECT_EQ(drained.ran, 0u);
        EXPECT_EQ(s.session.setValue.mCalls, 0);
    }

    TEST(PackBindingsMutate, a_handler_that_answers_with_an_error_or_throws_is_counted_and_the_rest_still_run)
    {
        ScriptSession s("p.bad_mutate(1) p.throwing_mutate(2) p.set(3)");
        sol::table script = s.load(PackContext::Global);
        ASSERT_TRUE(callRun(script).valid());
        MutateQueue::DrainResult drained;
        EXPECT_NO_THROW(drained = s.store().mutateQueue().drain());
        EXPECT_EQ(drained.ran, 3u);
        EXPECT_EQ(drained.failed, 2u);
        EXPECT_EQ(s.session.log, std::vector<std::string>{ "set 3" });
    }

    TEST(PackBindingsMutate, the_handler_runs_on_the_thread_that_drains_not_the_one_that_called)
    {
        ScriptSession s("p.set(1)");
        sol::table script = s.load(PackContext::Global);
        ASSERT_TRUE(callRun(script).valid());
        std::thread other([&] { s.store().mutateQueue().drain(); });
        const std::thread::id otherId = other.get_id();
        other.join();
        EXPECT_EQ(s.session.setValue.mLastThread, otherId);
        EXPECT_NE(s.session.setValue.mLastThread, std::this_thread::get_id());
    }

    TEST(PackBindingsMutate, pushing_from_other_threads_is_safe_and_keeps_each_threads_order)
    {
        ScriptSession s("return 1");
        BoundFunction& fn = s.store().namespaces()[0]->functions[4];
        ASSERT_EQ(fn.name, "set");
        auto producer = [&](int base) {
            for (int i = 0; i < 500; ++i)
            {
                BindingValue arg;
                arg.type = BindingType::Int32;
                arg.integer = base + i;
                (void)s.store().mutateQueue().push(fn, std::span<const BindingValue>(&arg, 1));
            }
        };
        std::thread a(producer, 0);
        std::thread b(producer, 10000);
        a.join();
        b.join();
        s.store().mutateQueue().drain();
        int lastA = -1;
        int lastB = 9999;
        for (const std::string& entry : s.session.log)
        {
            const int value = std::stoi(entry.substr(4));
            if (value < 10000)
            {
                EXPECT_GT(value, lastA);
                lastA = value;
            }
            else
            {
                EXPECT_GT(value, lastB);
                lastB = value;
            }
        }
        EXPECT_EQ(s.session.log.size(), 1000u);
    }

    TEST(PackBindingsMutate, queueing_and_draining_scalar_calls_allocates_nothing_after_warm_up)
    {
        ScriptSession s("local set = p.set for i = 1, 1000 do set(i) end");
        sol::table script = s.load(PackContext::Global);
        sol::protected_function run = script["run"];
        ASSERT_TRUE(sol::protected_function_result(run()).valid());
        s.store().mutateQueue().drain();
        // The handler's own log would allocate strings; measure with a handler that does not
        s.session.setValue.setFn([](std::span<const BindingValue>) { return nothing(); });

        std::size_t allocations = 0;
        sol::protected_function_result result;
        {
            TestSupport::AllocationCounter counter;
            result = run();
            s.store().mutateQueue().drain();
            allocations = counter.count();
        }
        ASSERT_TRUE(result.valid());
        EXPECT_EQ(allocations, 0u);
    }

    // ---- handles ----

    TEST(PackBindingsHandle, a_handle_is_an_opaque_userdata_that_compares_equal_by_value)
    {
        ScriptSession s(
            "local a, b, c = p.mint(7), p.mint(7), p.mint(8) return type(a), a == b, a ~= c, rawequal(a, b)");
        sol::table script = s.load(PackContext::Global);
        sol::protected_function_result result = callRun(script);
        ASSERT_TRUE(result.valid());
        EXPECT_EQ(result.get<std::string>(0), "userdata");
        EXPECT_TRUE(result.get<bool>(1));
        EXPECT_TRUE(result.get<bool>(2));
        EXPECT_FALSE(result.get<bool>(3)); // two userdata objects, equal only through __eq
    }

    TEST(PackBindingsHandle, a_handle_cannot_be_indexed_assigned_or_inspected)
    {
        ScriptSession s("local h = p.mint(1) return h.id");
        sol::table script = s.load(PackContext::Global);
        EXPECT_TRUE(contains(errorOf(script), "opaque"));

        ScriptSession t("local h = p.mint(1) h.id = 5");
        sol::table other = t.load(PackContext::Global);
        EXPECT_TRUE(contains(errorOf(other), "opaque"));

        // The sandbox's getmetatable works on tables only, so a script cannot reach the handle's metatable
        ScriptSession u("local h = p.mint(1) return getmetatable(h)");
        sol::table third = u.load(PackContext::Global);
        EXPECT_TRUE(contains(errorOf(third), "only for tables"));
    }

    TEST(PackBindingsHandle, a_handle_goes_back_to_the_pack_unchanged)
    {
        ScriptSession s("return p.describe(p.mint(123456789))");
        sol::table script = s.load(PackContext::Global);
        EXPECT_EQ(stringResult(script), "h=123456789");
    }

    TEST(PackBindingsHandle, a_64_bit_value_survives_a_round_trip)
    {
        Session session;
        LambdaHandler big{ [](std::span<const BindingValue>) { return okHandle(0xFFFFFFFFFFFFFFFFull); } };
        LambdaHandler echo{ [&session](std::span<const BindingValue> a) {
            session.scratch = std::to_string(a[0].handle);
            return okText(session.scratch);
        } };
        session.pack->mDeclare = [&](BindingsService& service) {
            std::array<FunctionDescriptor, 2> fns;
            fns[0].name = "big";
            fns[0].hasResult = true;
            fns[0].result = BindingType::Handle;
            fns[0].handler = &big;
            fns[1].name = "echo";
            fns[1].params = kHandle;
            fns[1].hasResult = true;
            fns[1].result = BindingType::String;
            fns[1].handler = &echo;
            NamespaceDescriptor ns;
            ns.name = "testgame.big";
            ns.functions = fns;
            ASSERT_TRUE(service.declareNamespace(ns).ok);
        };
        ASSERT_TRUE(session.start());
        auto file = std::make_unique<TestingOpenMW::VFSTestFile>(
            "local p = require('core.testgame.big') return { run = function() return p.echo(p.big()) end }");
        auto vfs = TestingOpenMW::createTestVFS({ { kScriptPath, file.get() } });
        LuaUtil::ScriptsConfiguration cfg;
        LuaUtil::LuaState lua(vfs.get(), &cfg);
        std::map<std::string, sol::main_object> packages;
        lua.protectedCall([&](LuaUtil::LuaView& view) {
            for (auto& [name, object] : MWLua::initPackPackages(view.sol(), session.host.bindings()))
                packages.emplace(name, sol::main_object(object));
        });
        sol::table script = lua.runInNewSandbox(VFS::Path::Normalized(kScriptPath), "test", packages);
        EXPECT_EQ(stringResult(script), "18446744073709551615");
    }

    TEST(PackBindingsHandle, a_number_where_a_handle_is_expected_and_a_handle_where_a_number_is_expected_are_errors)
    {
        ScriptSession s("return p.describe(5)");
        sol::table script = s.load(PackContext::Global);
        const std::string message = errorOf(script);
        EXPECT_TRUE(contains(message, "argument #1"));
        EXPECT_TRUE(contains(message, "handle"));
        EXPECT_EQ(s.session.describe.mCalls, 0);

        ScriptSession t("return p.mint(p.mint(1))");
        sol::table other = t.load(PackContext::Global);
        EXPECT_TRUE(contains(errorOf(other), "argument #1"));
        EXPECT_EQ(t.session.mint.mCalls, 1); // the inner call only

        // Any other userdata (here: the read-only package itself) is not a handle either
        ScriptSession u("return p.describe(p)");
        sol::table third = u.load(PackContext::Global);
        EXPECT_TRUE(contains(errorOf(third), "argument #1"));
        EXPECT_EQ(u.session.describe.mCalls, 0);
    }

    TEST(PackBindingsHandle, a_stale_handle_is_not_found_and_is_a_located_script_error)
    {
        ScriptSession s("return p.stale(p.mint(3))");
        sol::table script = s.load(PackContext::Global);
        const std::string message = errorOf(script);
        EXPECT_TRUE(contains(message, "core.testgame.actor.stale"));
        EXPECT_TRUE(contains(message, "NotFound"));
        EXPECT_TRUE(contains(message, "no such actor"));
    }

    TEST(PackBindingsHandle, a_script_that_keeps_a_handle_in_its_saved_data_cannot_serialize_it)
    {
        ScriptSession s("return p.mint(1)");
        sol::table script = s.load(PackContext::Global);
        sol::protected_function_result result = callRun(script);
        ASSERT_TRUE(result.valid());
        sol::object handle = result.get<sol::object>();
        bool threw = false;
        std::string message;
        s.lua->protectedCall([&](LuaUtil::LuaView& view) {
            sol::table saved(view.sol(), sol::create);
            saved["keep"] = handle;
            try
            {
                (void)LuaUtil::serialize(saved, nullptr);
            }
            catch (const std::exception& e)
            {
                threw = true;
                message = e.what();
            }
        });
        // components/lua/scriptscontainer.cpp (ScriptsContainer::save) catches this per script, logs it and writes the
        // script with no data, so the save itself still works.
        EXPECT_TRUE(threw);
        EXPECT_NE(message.find("not serializable"), std::string::npos);
    }

    // ---- optionals ----

    TEST(PackBindingsOptional, an_optional_parameter_may_be_nil_omitted_or_given)
    {
        ScriptSession s("return p.opt(nil), p.opt(), p.opt(9)");
        sol::table script = s.load(PackContext::Global);
        sol::protected_function_result result = callRun(script);
        ASSERT_TRUE(result.valid());
        EXPECT_EQ(result.get<std::string>(0), "present=no");
        EXPECT_EQ(result.get<std::string>(1), "present=no");
        EXPECT_EQ(result.get<std::string>(2), "present=yes value=9");
    }

    TEST(PackBindingsOptional, a_given_optional_is_still_type_checked)
    {
        ScriptSession s("return p.opt('x')");
        sol::table script = s.load(PackContext::Global);
        EXPECT_TRUE(contains(errorOf(script), "argument #1"));
        EXPECT_EQ(s.session.optionals.mCalls, 0);
    }

    TEST(PackBindingsOptional, only_trailing_optional_parameters_may_be_left_out)
    {
        ScriptSession s("return p.opt_after(4), p.opt_after(4, nil), p.opt_after(4, 5)");
        sol::table script = s.load(PackContext::Global);
        sol::protected_function_result result = callRun(script);
        ASSERT_TRUE(result.valid());
        EXPECT_EQ(result.get<std::string>(0), "first=4 second=none");
        EXPECT_EQ(result.get<std::string>(1), "first=4 second=none");
        EXPECT_EQ(result.get<std::string>(2), "first=4 second=5");

        ScriptSession t("return p.opt_before(4)");
        sol::table other = t.load(PackContext::Global);
        EXPECT_TRUE(contains(errorOf(other), "expected 2 arguments"));
        ScriptSession u("return p.opt_after(1, 2, 3)");
        sol::table third = u.load(PackContext::Global);
        EXPECT_TRUE(contains(errorOf(third), "expected"));
    }

    TEST(PackBindingsOptional, an_absent_optional_result_is_nil)
    {
        ScriptSession s("return p.opt_result(0), p.opt_result(6)");
        sol::table script = s.load(PackContext::Global);
        sol::protected_function_result result = callRun(script);
        ASSERT_TRUE(result.valid());
        EXPECT_EQ(result.get_type(0), sol::type::lua_nil);
        EXPECT_EQ(result.get<int>(1), 6);
    }

    // ---- arrays ----

    TEST(PackBindingsArray, an_array_argument_reaches_the_pack_flat)
    {
        ScriptSession s("return p.sum({1, 2, 3, 4}), p.sum({})");
        sol::table script = s.load(PackContext::Global);
        sol::protected_function_result result = callRun(script);
        ASSERT_TRUE(result.valid());
        EXPECT_EQ(result.get<std::string>(0), "count=4 sum=10");
        EXPECT_EQ(result.get<std::string>(1), "count=0 sum=0");
    }

    TEST(PackBindingsArray, an_array_of_exactly_256_elements_is_fine_and_257_is_an_error)
    {
        ScriptSession s("local t = {} for i = 1, 256 do t[i] = 1 end return p.sum(t)");
        sol::table script = s.load(PackContext::Global);
        EXPECT_EQ(stringResult(script), "count=256 sum=256");

        ScriptSession t("local x = {} for i = 1, 257 do x[i] = 1 end return p.sum(x)");
        sol::table other = t.load(PackContext::Global);
        const std::string message = errorOf(other);
        EXPECT_TRUE(contains(message, "argument #1"));
        EXPECT_TRUE(contains(message, "256"));
        EXPECT_EQ(t.session.sumArray.mCalls, 0);
    }

    TEST(PackBindingsArray, a_bad_element_names_its_position_and_a_hole_or_a_non_table_is_refused)
    {
        ScriptSession s("return p.sum({1, 2, 'three'})");
        sol::table script = s.load(PackContext::Global);
        EXPECT_TRUE(contains(errorOf(script), "element 3"));

        ScriptSession t("return p.sum({1, 2.5})");
        sol::table other = t.load(PackContext::Global);
        EXPECT_TRUE(contains(errorOf(other), "element 2"));

        ScriptSession u("return p.sum({1, nil, 3})");
        sol::table third = u.load(PackContext::Global);
        EXPECT_TRUE(contains(errorOf(third), "argument #1"));

        ScriptSession v("return p.sum(5)");
        sol::table fourth = v.load(PackContext::Global);
        EXPECT_TRUE(contains(errorOf(fourth), "expected an array"));
        EXPECT_EQ(v.session.sumArray.mCalls + u.session.sumArray.mCalls, 0);
    }

    TEST(PackBindingsArray, a_table_with_a_metatable_is_read_raw_and_its_index_function_is_never_run)
    {
        ScriptSession s(
            "local ran = false local t = setmetatable({1, 2}, { __index = function() ran = true return 7 "
            "end, __len = function() ran = true return 99 end }) local r = p.sum(t) return r, ran");
        sol::table script = s.load(PackContext::Global);
        sol::protected_function_result result = callRun(script);
        ASSERT_TRUE(result.valid());
        EXPECT_EQ(result.get<std::string>(0), "count=2 sum=3");
        EXPECT_FALSE(result.get<bool>(1));
    }

    TEST(PackBindingsArray, string_and_handle_arrays_work)
    {
        ScriptSession s("return p.join({'a', 'bb', ''}), p.handles({p.mint(5), p.mint(6)})");
        sol::table script = s.load(PackContext::Global);
        sol::protected_function_result result = callRun(script);
        ASSERT_TRUE(result.valid());
        EXPECT_EQ(result.get<std::string>(0), "a|bb||");
        EXPECT_EQ(result.get<std::string>(1), "5,6,");

        ScriptSession t("return p.handles({p.mint(5), 6})");
        sol::table other = t.load(PackContext::Global);
        EXPECT_TRUE(contains(errorOf(other), "element 2"));
    }

    TEST(PackBindingsArray, an_array_result_becomes_a_sequence_and_an_empty_one_an_empty_table)
    {
        ScriptSession s("local t = p.range(3) return #t, t[1], t[3], #p.range(0)");
        sol::table script = s.load(PackContext::Global);
        sol::protected_function_result result = callRun(script);
        ASSERT_TRUE(result.valid());
        EXPECT_EQ(result.get<int>(0), 3);
        EXPECT_EQ(result.get<int>(1), 1);
        EXPECT_EQ(result.get<int>(2), 3);
        EXPECT_EQ(result.get<int>(3), 0);
    }

    TEST(PackBindingsArray, a_result_of_257_elements_or_with_a_wrong_element_type_is_internal)
    {
        ScriptSession s("return p.range(257)");
        sol::table script = s.load(PackContext::Global);
        EXPECT_TRUE(contains(errorOf(script), "Internal"));

        ScriptSession t("return p.range(256)");
        sol::table other = t.load(PackContext::Global);
        EXPECT_TRUE(callRun(other).valid());

        ScriptSession u("return p.wrong_elements(0)");
        sol::table third = u.load(PackContext::Global);
        EXPECT_TRUE(contains(errorOf(third), "Internal"));
    }

    TEST(PackBindingsArray, an_array_of_handles_comes_back_as_handles_that_compare_by_value)
    {
        ScriptSession s(
            "local t = p.two_handles(10) return type(t[1]), t[1] == p.mint(10), t[2] == p.mint(11), t[1] == t[2]");
        sol::table script = s.load(PackContext::Global);
        sol::protected_function_result result = callRun(script);
        ASSERT_TRUE(result.valid());
        EXPECT_EQ(result.get<std::string>(0), "userdata");
        EXPECT_TRUE(result.get<bool>(1));
        EXPECT_TRUE(result.get<bool>(2));
        EXPECT_FALSE(result.get<bool>(3));
    }

    // ---- versions ----

    TEST(PackBindingsVersion, every_package_has_a_read_only_API_VERSION_table_with_the_declared_version)
    {
        ScriptSession s("return p.API_VERSION.major, p.API_VERSION.minor", false, Version{ 3, 7 });
        sol::table script = s.load(PackContext::Global);
        sol::protected_function_result result = callRun(script);
        ASSERT_TRUE(result.valid());
        EXPECT_EQ(result.get<int>(0), 3);
        EXPECT_EQ(result.get<int>(1), 7);

        ScriptSession t("p.API_VERSION.major = 9");
        sol::table other = t.load(PackContext::Global);
        EXPECT_FALSE(callRun(other).valid());
        ScriptSession u("p.API_VERSION = nil");
        sol::table third = u.load(PackContext::Global);
        EXPECT_FALSE(callRun(third).valid());
    }

    TEST(PackBindingsVersion, the_local_and_menu_tables_carry_the_version_too)
    {
        ScriptSession s("return p.API_VERSION.minor", true);
        sol::table local = s.load(PackContext::Local);
        sol::protected_function_result result = callRun(local);
        ASSERT_TRUE(result.valid());
        EXPECT_EQ(result.get<int>(), 2);
        sol::table menu = s.load(PackContext::Menu);
        sol::protected_function_result menuResult = callRun(menu);
        ASSERT_TRUE(menuResult.valid());
        EXPECT_EQ(menuResult.get<int>(), 2);
    }

    struct RequirementCase
    {
        const char* package;
        std::uint32_t major;
        std::uint32_t minor;
        bool met;
        const char* mustMention; // part of the reason when not met
    };

    TEST(PackBindingsVersion, the_requirement_rule_same_major_and_minor_not_lower)
    {
        Session session; // declares testgame.actor 1.2
        ASSERT_TRUE(session.start());
        const RequirementCase cases[] = {
            { "core.testgame.actor", 1, 0, true, "" },
            { "core.testgame.actor", 1, 1, true, "" },
            { "core.testgame.actor", 1, 2, true, "" },
            { "core.testgame.actor", 1, 3, false, "1.2" },
            { "core.testgame.actor", 2, 0, false, "1.2" },
            { "core.testgame.actor", 0, 0, false, "1.2" },
            { "core.testgame.world", 1, 0, false, "does not provide" },
            { "core.othergame.actor", 1, 0, false, "does not provide" },
            { "testgame.actor", 1, 0, false, "core." },
            { "openmw.core", 1, 0, false, "core." },
            { "", 1, 0, false, "" },
        };
        for (const RequirementCase& c : cases)
        {
            const std::string why = session.host.bindings().unmetRequirement(c.package, c.major, c.minor);
            EXPECT_EQ(why.empty(), c.met) << c.package << ">=" << c.major << "." << c.minor << ": " << why;
            if (!c.met)
            {
                EXPECT_NE(why.find(c.mustMention), std::string::npos) << why;
            }
        }
    }

    TEST(PackBindingsVersion, with_no_namespace_declared_everything_is_unmet_and_says_so)
    {
        BindingsStore store;
        const std::string why = store.unmetRequirement("core.testgame.actor", 1, 0);
        EXPECT_FALSE(why.empty());
        EXPECT_NE(why.find("no game pack"), std::string::npos);
    }

    TEST(PackBindingsVersion, the_requirement_check_for_the_lua_manager_uses_the_store_or_refuses)
    {
        Session session;
        ASSERT_TRUE(session.start());
        const LuaUtil::ScriptsConfiguration::RequirementCheck withPack
            = MWLua::makePackRequirementCheck(&session.host.bindings());
        ESM::LuaScriptCfg::Requirement need;
        need.mPackage = "core.testgame.actor";
        need.mMajor = 1;
        need.mMinor = 2;
        EXPECT_TRUE(withPack(need).empty());
        need.mMinor = 3;
        EXPECT_FALSE(withPack(need).empty());

        const LuaUtil::ScriptsConfiguration::RequirementCheck without = MWLua::makePackRequirementCheck(nullptr);
        need.mMinor = 0;
        const std::string why = without(need);
        EXPECT_FALSE(why.empty());
        EXPECT_NE(why.find("no game pack"), std::string::npos);
    }
}
