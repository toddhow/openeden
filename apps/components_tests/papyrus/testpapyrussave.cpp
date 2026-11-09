// Task 0093 stage e: script state. serialise() writes every instance, queued event and parked stack as a versioned,
// canonical blob; restore() reads one into a fresh runtime that has the same scripts loaded, validating before it
// trusts and refusing a bad blob without changing anything. Written from the task's spec before the code and run red
// against the stub (serialise returned nothing, restore refused every blob) first. No game asset: every script is
// generated.

#include <gtest/gtest.h>

#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <functional>
#include <limits>
#include <memory>
#include <random>
#include <string>
#include <vector>

#include <components/script_papyrus/papyrusruntime.hpp>
#include <components/script_papyrus/papyrussave.hpp>

#include "papyrustestutil.hpp"

namespace
{
    using namespace PapyrusTest;
    using namespace PexAsm;
    using namespace openeden::script;
    namespace Save = ScriptPapyrus::Save;

    PapyrusValue F(float v)
    {
        return PapyrusValue::makeFloat(v);
    }
    PapyrusValue I(std::int32_t v)
    {
        return PapyrusValue::makeInt(v);
    }

    // ---- the scripts -----------------------------------------------------------------------------------------------

    ScriptBuilder gameScript()
    {
        ScriptBuilder g("Game");
        g.function("Wait").global().native().param("seconds", "Float");
        g.function("Park").global().native();
        g.function("Mark").global().native().param("what", "Int");
        return g;
    }

    /// variant 1 is "the script changed since the save": one more variable and a different body.
    ScriptBuilder saverScript(int variant = 0)
    {
        ScriptBuilder s("Saver");
        s.var("iv", "Int");
        s.var("fv", "Float");
        s.var("bv", "Bool");
        s.var("sv", "String");
        s.var("arr", "Int[]");
        s.var("alias", "Int[]");
        s.var("strs", "String[]");
        s.var("who", "Actor");
        s.var("peer", "Saver");
        if (variant == 1)
        {
            s.var("extra", "Int");
        }
        s.function("Setup")
            .assign(Id("iv"), Int(7))
            .assign(Id("fv"), Flt(2.5f))
            .assign(Id("bv"), Bool(true))
            .op(Op::StrCat, { Id("sv"), Str("Hello-"), Str("World") })
            .op(Op::ArrayCreate, { Id("arr"), Int(4) })
            .op(Op::ArraySetElement, { Id("arr"), Int(0), Int(10) })
            .op(Op::ArraySetElement, { Id("arr"), Int(1), Int(20) })
            .op(Op::ArraySetElement, { Id("arr"), Int(3), Int(40) })
            .assign(Id("alias"), Id("arr"))
            .op(Op::ArrayCreate, { Id("strs"), Int(2) })
            .op(Op::ArraySetElement, { Id("strs"), Int(1), Str("elem-one") })
            .ret(Nil());
        s.function("Sum", "Int")
            .local("a", "Int")
            .local("b", "Int")
            .local("c", "Int")
            .op(Op::ArrayGetElement, { Id("a"), Id("arr"), Int(0) })
            .op(Op::ArrayGetElement, { Id("b"), Id("alias"), Int(1) })
            .op(Op::ArrayGetElement, { Id("c"), Id("arr"), Int(3) })
            .iadd(Id("a"), Id("a"), Id("b"))
            .iadd(Id("a"), Id("a"), Id("c"))
            .iadd(Id("a"), Id("a"), Id("iv"))
            .ret(Id("a"));
        s.function("Poke").op(Op::ArraySetElement, { Id("alias"), Int(1), Int(99) }).ret(Nil());
        s.function("Elem", "String")
            .local("v", "String")
            .op(Op::ArrayGetElement, { Id("v"), Id("strs"), Int(1) })
            .ret(Id("v"));
        s.function("GotoState").param("newState", "String").assign(Id("::State"), Id("newState")).ret(Nil());
        s.function("GetState", "String").ret(Id("::State"));
        s.function("Describe", "Int").ret(Int(variant == 1 ? 5 : 1));
        s.function("OnLongWork")
            .param("w", "Float")
            .param("base", "Int")
            .local("n", "None")
            .local("l1", "Int")
            .local("ls", "String")
            .local("la", "Int[]")
            .local("e", "Int")
            .op(Op::IMul, { Id("l1"), Id("base"), Int(2) })
            .op(Op::StrCat, { Id("ls"), Str("loc-"), Str("str") })
            .op(Op::ArrayCreate, { Id("la"), Int(3) })
            .op(Op::ArraySetElement, { Id("la"), Int(2), Id("base") })
            .callStatic("Game", "Mark", Id("n"), { Id("l1") })
            .callStatic("Game", "Wait", Id("n"), { Id("w") })
            .iadd(Id("iv"), Id("iv"), Id("l1"))
            .op(Op::StrCat, { Id("sv"), Id("ls"), Str("!") })
            .op(Op::ArrayGetElement, { Id("e"), Id("la"), Int(2) })
            .iadd(Id("e"), Id("e"), Id("l1"))
            .callStatic("Game", "Mark", Id("n"), { Id("e") })
            .ret(Nil());
        s.function("OnChain")
            .param("w", "Float")
            .param("base", "Int")
            .local("n", "None")
            .local("t", "Int")
            .callStatic("Game", "Mark", Id("n"), { Id("base") })
            .callStatic("Game", "Wait", Id("n"), { Id("w") })
            .iadd(Id("t"), Id("base"), Int(1))
            .callStatic("Game", "Mark", Id("n"), { Id("t") })
            .callStatic("Game", "Wait", Id("n"), { Id("w") })
            .iadd(Id("t"), Id("base"), Int(2))
            .callStatic("Game", "Mark", Id("n"), { Id("t") })
            .ret(Nil());
        s.function("Helper").local("n", "None").callStatic("Game", "Wait", Id("n"), { Flt(3.0f) }).ret(Nil());
        s.function("OnNested")
            .local("n", "None")
            .assign(Id("iv"), Int(1))
            .callMethod("Helper", Id("self"), Id("n"))
            .assign(Id("iv"), Int(2))
            .callStatic("Game", "Mark", Id("n"), { Id("iv") })
            .ret(Nil());
        s.function("OnPark")
            .local("n", "None")
            .callStatic("Game", "Park", Id("n"))
            .iadd(Id("iv"), Id("iv"), Int(1))
            .callStatic("Game", "Mark", Id("n"), { Id("iv") })
            .ret(Nil());
        s.function("OnMark")
            .param("v", "Int")
            .local("n", "None")
            .callStatic("Game", "Mark", Id("n"), { Id("v") })
            .ret(Nil());
        s.function("OnChurn")
            .local("i", "Int")
            .local("s", "String")
            .local("c", "Bool")
            .assign(Id("i"), Int(0))
            .op(Op::StrCat, { Id("s"), Str("churn-"), Str("churn-churn-churn-churn-churn-churn") })
            .iadd(Id("i"), Id("i"), Int(1))
            .op(Op::CompareLt, { Id("c"), Id("i"), Int(300) })
            .jmpt(Id("c"), -3)
            .ret(Nil());
        s.state("Busy").function("Describe", "Int").ret(Int(2));
        return s;
    }

    ScriptBuilder shapesScript()
    {
        ScriptBuilder s("Shapes");
        s.structDef("Point").member("x", "Float", Flt(2.0f)).member("y", "Float").member("tag", "String", Str("t"));
        s.var("keep", "Shapes#Point");
        s.var("list", "Shapes#Point[]");
        s.function("Setup")
            .local("p", "Shapes#Point")
            .op(Op::StructCreate, { Id("p") })
            .op(Op::StructSet, { Id("p"), Str("y"), Flt(7.5f) })
            .assign(Id("keep"), Id("p"))
            .op(Op::ArrayCreate, { Id("list"), Int(3) })
            .op(Op::ArraySetElement, { Id("list"), Int(0), Id("p") })
            .op(Op::ArraySetElement, { Id("list"), Int(2), Id("p") })
            .ret(Nil());
        s.function("Y", "Float").local("v", "Float").op(Op::StructGet, { Id("v"), Id("keep"), Str("y") }).ret(Id("v"));
        s.function("YOfListTwo", "Float")
            .local("p", "Shapes#Point")
            .local("v", "Float")
            .op(Op::ArrayGetElement, { Id("p"), Id("list"), Int(2) })
            .op(Op::StructGet, { Id("v"), Id("p"), Str("y") })
            .ret(Id("v"));
        s.function("SetY", "None").param("v", "Float").op(Op::StructSet, { Id("keep"), Str("y"), Id("v") }).ret(Nil());
        return s;
    }

    // ---- a runtime with the same scripts and natives every time ----------------------------------------------------

    struct Host
    {
        std::vector<std::int32_t> mMarks;
        std::vector<std::uint64_t> mTokens;
        std::function<void()> mInMark;
    };

    class SaveWorld final : public PapyrusWorld
    {
    public:
        bool formClass(FormId form, std::string& className) const override
        {
            if (form == 0x14)
            {
                className = "Actor";
                return true;
            }
            return false;
        }
        void report(std::string_view) override {}
        void log(Severity, std::string_view message) override
        {
            mLogs.emplace_back(message);
            if (mOnLog)
            {
                std::function<void()> once = std::move(mOnLog);
                mOnLog = nullptr;
                once();
            }
        }
        bool attachKeyExists(const AttachKey& key) const override { return !mKeyCheck || mKeyCheck(key); }
        bool logged(std::string_view part) const
        {
            for (const std::string& line : mLogs)
            {
                if (line.find(part) != std::string::npos)
                {
                    return true;
                }
            }
            return false;
        }

        std::vector<std::string> mLogs;
        std::function<void()> mOnLog;
        std::function<bool(const AttachKey&)> mKeyCheck;
    };

    ScriptPapyrus::RuntimeOptions latentOptions()
    {
        ScriptPapyrus::RuntimeOptions options;
        options.runLatentNatives = true;
        return options;
    }

    struct Node
    {
        explicit Node(Target target = Target::SkyrimSE, const ScriptPapyrus::RuntimeOptions& options = latentOptions(),
            int saverVariant = 0, bool withSaver = true, UnknownFunctionPolicy policy = UnknownFunctionPolicy::Tolerant)
            : mRuntime(ScriptPapyrus::createRuntime(mWorld, policy, options))
            , mTarget(target)
        {
            add(gameScript());
            if (withSaver)
            {
                add(saverScript(saverVariant));
            }
            if (target == Target::Fallout4)
            {
                add(shapesScript());
            }
            bind();
        }

        void add(const ScriptBuilder& script)
        {
            const LoadResult result = mRuntime->loadScript(script.build(optionsFor(mTarget)).mBytes, script.name());
            ASSERT_TRUE(result.ok) << script.name();
        }

        void bind()
        {
            ErrorReport error;
            NativeDescriptor wait;
            wait.className = "Game";
            wait.functionName = "Wait";
            wait.global = true;
            wait.latent = true;
            ASSERT_TRUE(mRuntime->registerNative(
                wait,
                [](const NativeCall& call) {
                    NativeResult result;
                    result.waitSeconds = call.arguments[0].number;
                    return result;
                },
                error))
                << error.format();
            NativeDescriptor park;
            park.className = "Game";
            park.functionName = "Park";
            park.global = true;
            park.latent = true;
            ASSERT_TRUE(mRuntime->registerNative(
                park,
                [this](const NativeCall&) {
                    mHost.mTokens.push_back(mRuntime->latentToken());
                    NativeResult result;
                    result.waitSeconds = -1.0;
                    return result;
                },
                error))
                << error.format();
            NativeDescriptor mark;
            mark.className = "Game";
            mark.functionName = "Mark";
            mark.global = true;
            ASSERT_TRUE(mRuntime->registerNative(
                mark,
                [this](const NativeCall& call) {
                    mHost.mMarks.push_back(call.arguments[0].integer);
                    if (mHost.mInMark)
                    {
                        mHost.mInMark();
                    }
                    return NativeResult{};
                },
                error))
                << error.format();
        }

        AttachKey keyFor(std::uint32_t n) const
        {
            AttachKey key;
            key.kind = AttachKind::Form;
            key.formId = 0x10000u + n;
            return key;
        }

        /// A new instance on form 0x10000 + n, with OnInit run.
        InstanceId make(std::uint32_t n, const char* script = "Saver")
        {
            const InstanceId id = mRuntime->attach(keyFor(n), script);
            EXPECT_NE(id, 0u);
            mRuntime->tick(mTime);
            return id;
        }

        CallResult send(InstanceId id, const char* event, std::vector<PapyrusValue> args = {})
        {
            return mRuntime->send(id, event, args);
        }

        TickResult tick(double time)
        {
            mTime = time;
            return mRuntime->tick(time);
        }

        CallResult call(InstanceId id, const char* function, std::vector<PapyrusValue> args = {})
        {
            return mRuntime->call(id, function, args);
        }

        std::int32_t callInt(InstanceId id, const char* function)
        {
            const CallResult result = call(id, function);
            EXPECT_TRUE(result.ok) << function << ": " << result.error.format();
            return result.returned.integer;
        }

        std::int32_t varInt(InstanceId id, const char* name) const
        {
            PapyrusValue value;
            EXPECT_TRUE(mRuntime->getVariable(id, name, value)) << name;
            return value.integer;
        }

        std::string varText(InstanceId id, const char* name) const
        {
            PapyrusValue value;
            EXPECT_TRUE(mRuntime->getVariable(id, name, value)) << name;
            return std::string(value.text);
        }

        /// Everything observable about an instance without changing it, as one text.
        std::string dump(InstanceId id) const
        {
            std::string out;
            PapyrusValue v;
            for (const char* name : { "iv", "fv", "bv", "sv", "who", "peer" })
            {
                if (!mRuntime->getVariable(id, name, v))
                {
                    out += std::string(name) + "=?;";
                    continue;
                }
                out += std::string(name) + "=" + std::to_string(static_cast<int>(v.kind)) + ":"
                    + std::to_string(v.integer) + ":";
                std::uint32_t bits = 0;
                std::memcpy(&bits, &v.number, sizeof bits);
                std::string text(v.text);
                if (v.kind == PapyrusKind::Object)
                {
                    for (char& c : text)
                    {
                        c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
                    }
                }
                out += std::to_string(bits) + ":" + std::to_string(v.handle) + (v.attached ? "a" : "f") + ":" + text
                    + ";";
            }
            out += "state=" + mRuntime->stateOf(id);
            return out;
        }

        SaveWorld mWorld;
        Host mHost;
        std::unique_ptr<ScriptPapyrus::PapyrusRuntime> mRuntime;
        Target mTarget;
        double mTime = 0.0;
    };

    /// The same population on a node, as a test of everything the blob must carry. Returns the instance ids.
    struct Population
    {
        InstanceId mFilled = 0; ///< Setup run, state Busy, a form and a peer in its variables.
        InstanceId mLong = 0; ///< Parked in OnLongWork until 6.0.
        InstanceId mNested = 0; ///< Parked inside Helper until 3.0.
        InstanceId mHost = 0; ///< Parked for the host.
        InstanceId mDone = 0; ///< Completed by the host, not woken yet.
        InstanceId mQueued = 0; ///< Has events waiting that have not started.
    };

    Population populate(Node& node)
    {
        Population p;
        p.mFilled = node.make(1);
        node.call(p.mFilled, "Setup");
        node.call(p.mFilled, "GotoState", { PapyrusValue::makeString("Busy") });
        p.mLong = node.make(2);
        p.mNested = node.make(3);
        p.mHost = node.make(4);
        p.mDone = node.make(5);
        p.mQueued = node.make(6);
        EXPECT_TRUE(node.mRuntime->setVariable(p.mFilled, "who", PapyrusValue::makeForm("Actor", 0x14)));
        EXPECT_TRUE(node.mRuntime->setVariable(p.mFilled, "peer", PapyrusValue::makeInstance(p.mLong)));
        EXPECT_TRUE(node.mRuntime->setVariable(p.mFilled, "fv", F(-0.0f)));
        node.send(p.mLong, "OnLongWork", { F(5.0f), I(7) });
        node.send(p.mNested, "OnNested");
        node.send(p.mHost, "OnPark");
        node.send(p.mDone, "OnPark");
        node.tick(1.0);
        EXPECT_EQ(node.mHost.mTokens.size(), 2u);
        EXPECT_TRUE(node.mRuntime->completeLatent(node.mHost.mTokens[1]));
        node.send(p.mQueued, "OnChain", { F(1.0f), I(50) });
        node.send(p.mQueued, "OnMark", { I(77) });
        return p;
    }

    Bytes fixHeader(Bytes bytes)
    {
        if (bytes.size() < Save::kHeaderBytes)
        {
            return bytes;
        }
        const std::uint64_t body = bytes.size() - Save::kHeaderBytes;
        std::memcpy(bytes.data() + 8, &body, 8);
        const std::uint64_t sum = Save::fnv1a(bytes.data() + Save::kHeaderBytes, body);
        std::memcpy(bytes.data() + 16, &sum, 8);
        return bytes;
    }

    SaveBlob blobOf(Bytes bytes)
    {
        SaveBlob blob;
        blob.formatVersion = Save::kFormatVersion;
        blob.bytes = std::move(bytes);
        return blob;
    }

    // ---- the format
    // --------------------------------------------------------------------------------------------------

    TEST(PapyrusSave, an_empty_runtime_writes_a_header_and_restores)
    {
        Node a;
        const SaveBlob blob = a.mRuntime->serialise();
        EXPECT_EQ(blob.formatVersion, Save::kFormatVersion);
        ASSERT_GE(blob.bytes.size(), Save::kHeaderBytes);
        std::uint32_t magic = 0;
        std::memcpy(&magic, blob.bytes.data(), 4);
        EXPECT_EQ(magic, Save::kMagic);
        std::uint32_t version = 0;
        std::memcpy(&version, blob.bytes.data() + 4, 4);
        EXPECT_EQ(version, Save::kFormatVersion);
        std::uint64_t body = 0;
        std::memcpy(&body, blob.bytes.data() + 8, 8);
        EXPECT_EQ(body, blob.bytes.size() - Save::kHeaderBytes);
        std::uint64_t sum = 0;
        std::memcpy(&sum, blob.bytes.data() + 16, 8);
        EXPECT_EQ(sum, Save::fnv1a(blob.bytes.data() + Save::kHeaderBytes, body));
        Node b;
        const RestoreResult result = b.mRuntime->restore(blob);
        EXPECT_TRUE(result.ok) << result.error.format();
    }

    TEST(PapyrusSave, serialising_twice_gives_the_same_bytes)
    {
        Node a;
        populate(a);
        const SaveBlob first = a.mRuntime->serialise();
        const SaveBlob second = a.mRuntime->serialise();
        EXPECT_FALSE(first.bytes.empty());
        EXPECT_EQ(first.bytes, second.bytes);
    }

    TEST(PapyrusSave, two_runtimes_built_the_same_way_write_the_same_bytes)
    {
        Node a;
        Node b;
        populate(a);
        populate(b);
        EXPECT_EQ(a.mRuntime->serialise().bytes, b.mRuntime->serialise().bytes);
    }

    TEST(PapyrusSave, a_restored_runtime_writes_the_blob_it_was_given_byte_for_byte)
    {
        for (const Target target : { Target::SkyrimSE, Target::Fallout4 })
        {
            Node a(target);
            populate(a);
            const SaveBlob blob = a.mRuntime->serialise();
            ASSERT_FALSE(blob.bytes.empty());
            Node b(target);
            const RestoreResult result = b.mRuntime->restore(blob);
            ASSERT_TRUE(result.ok) << result.error.format();
            EXPECT_EQ(b.mRuntime->serialise().bytes, blob.bytes);
        }
    }

    TEST(PapyrusSave, the_blob_holds_no_pointer_the_same_bytes_come_from_runs_in_other_memory_layouts)
    {
        // Two runtimes with different histories of allocation (b made and dropped garbage first) must write the same
        // bytes: nothing in the blob is an address or an id that belongs to one run.
        Node a;
        Node b;
        const InstanceId churn = b.make(99);
        b.send(churn, "OnChurn");
        b.tick(0.5);
        Population pa = populate(a);
        (void)pa;
        // b cannot be compared with a directly (it has the extra instance); restore a's blob into b and write again.
        const SaveBlob blob = a.mRuntime->serialise();
        ASSERT_TRUE(b.mRuntime->restore(blob).ok);
        EXPECT_EQ(b.mRuntime->serialise().bytes, blob.bytes);
    }

    // ---- what is carried -------------------------------------------------------------------------------------------

    TEST(PapyrusSave, instances_variables_and_states_come_back_with_their_ids)
    {
        Node a;
        const Population p = populate(a);
        Node b;
        const SaveBlob blob = a.mRuntime->serialise();
        const RestoreResult result = b.mRuntime->restore(blob);
        ASSERT_TRUE(result.ok) << result.error.format();
        for (const InstanceId id : { p.mFilled, p.mLong, p.mNested, p.mHost, p.mDone, p.mQueued })
        {
            EXPECT_EQ(a.dump(id), b.dump(id)) << "instance " << id;
        }
        EXPECT_EQ(b.mRuntime->stateOf(p.mFilled), "Busy");
        EXPECT_EQ(b.varText(p.mFilled, "sv"), "Hello-World");
        EXPECT_EQ(b.callInt(p.mFilled, "Describe"), 2) << "the state's own function runs";
        EXPECT_EQ(b.callInt(p.mFilled, "Sum"), 10 + 20 + 40 + 7);
        PapyrusValue who;
        ASSERT_TRUE(b.mRuntime->getVariable(p.mFilled, "who", who));
        EXPECT_EQ(who.kind, PapyrusKind::Object);
        EXPECT_FALSE(who.attached);
        EXPECT_EQ(who.handle, 0x14u);
        PapyrusValue peer;
        ASSERT_TRUE(b.mRuntime->getVariable(p.mFilled, "peer", peer));
        EXPECT_TRUE(peer.attached);
        EXPECT_EQ(peer.handle, p.mLong);
        PapyrusValue fv;
        ASSERT_TRUE(b.mRuntime->getVariable(p.mFilled, "fv", fv));
        EXPECT_TRUE(std::signbit(fv.number)) << "negative zero keeps its sign";
    }

    TEST(PapyrusSave, a_state_name_keeps_the_case_the_script_gave_it)
    {
        Node a;
        const InstanceId id = a.make(1);
        a.call(id, "GotoState", { PapyrusValue::makeString("bUsY") });
        Node b;
        ASSERT_TRUE(b.mRuntime->restore(a.mRuntime->serialise()).ok);
        EXPECT_EQ(b.mRuntime->stateOf(id), "bUsY");
        EXPECT_EQ(b.callInt(id, "Describe"), 2) << "the state is still found without regard to case";
    }

    TEST(PapyrusSave, a_native_form_keeps_its_class_for_is_and_casts)
    {
        Node a;
        const InstanceId id = a.make(1);
        ASSERT_TRUE(a.mRuntime->setVariable(id, "who", PapyrusValue::makeForm("Actor", 0x14)));
        Node b;
        ASSERT_TRUE(b.mRuntime->restore(a.mRuntime->serialise()).ok);
        PapyrusValue who;
        ASSERT_TRUE(b.mRuntime->getVariable(id, "who", who));
        std::string cls(who.text);
        for (char& c : cls)
        {
            c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
        }
        EXPECT_EQ(cls, "actor");
    }

    TEST(PapyrusSave, two_variables_that_hold_one_array_still_hold_one_array)
    {
        Node a;
        const InstanceId id = a.make(1);
        a.call(id, "Setup");
        Node b;
        ASSERT_TRUE(b.mRuntime->restore(a.mRuntime->serialise()).ok);
        EXPECT_EQ(b.callInt(id, "Sum"), 10 + 20 + 40 + 7);
        b.call(id, "Poke");
        EXPECT_EQ(b.callInt(id, "Sum"), 10 + 99 + 40 + 7) << "a write through the second name is seen by the first";
        EXPECT_EQ(std::string(b.call(id, "Elem").returned.text), "elem-one");
    }

    TEST(PapyrusSave, structs_and_arrays_of_structs_are_shared_and_typed_after_restore)
    {
        Node a(Target::Fallout4);
        const InstanceId id = a.make(1, "Shapes");
        ASSERT_TRUE(a.call(id, "Setup").ok);
        Node b(Target::Fallout4);
        const RestoreResult result = b.mRuntime->restore(a.mRuntime->serialise());
        ASSERT_TRUE(result.ok) << result.error.format();
        EXPECT_FLOAT_EQ(b.call(id, "Y").returned.number, 7.5f);
        EXPECT_FLOAT_EQ(b.call(id, "YOfListTwo").returned.number, 7.5f);
        b.call(id, "SetY", { F(1.5f) });
        EXPECT_FLOAT_EQ(b.call(id, "YOfListTwo").returned.number, 1.5f) << "the struct in the array is the same one";
    }

    TEST(PapyrusSave, keys_are_kept_and_a_second_attach_of_a_key_is_refused)
    {
        Node a;
        const Population p = populate(a);
        Node b;
        ASSERT_TRUE(b.mRuntime->restore(a.mRuntime->serialise()).ok);
        EXPECT_EQ(b.mRuntime->attach(b.keyFor(2), "Saver"), 0u) << "that form already has its script";
        const InstanceId fresh = b.mRuntime->attach(b.keyFor(500), "Saver");
        EXPECT_EQ(fresh, p.mQueued + 1) << "ids go on from the last one";
    }

    // ---- stacks ----------------------------------------------------------------------------------------------------

    TEST(PapyrusSave, a_restore_in_the_middle_of_a_wait_resumes_at_the_right_time_with_the_right_locals)
    {
        Node a;
        const InstanceId id = a.make(1);
        a.send(id, "OnLongWork", { F(5.0f), I(7) });
        a.tick(1.0);
        ASSERT_EQ(a.mHost.mMarks, (std::vector<std::int32_t>{ 14 }));
        Node b;
        const RestoreResult result = b.mRuntime->restore(a.mRuntime->serialise());
        ASSERT_TRUE(result.ok) << result.error.format();
        EXPECT_EQ(b.mRuntime->suspendedStacks(), 1u);
        b.tick(5.9);
        EXPECT_TRUE(b.mHost.mMarks.empty()) << "parked at 1.0 for 5.0: due at 6.0";
        b.tick(6.0);
        a.tick(5.9);
        a.tick(6.0);
        ASSERT_EQ(b.mHost.mMarks, (std::vector<std::int32_t>{ 21 })) << "base 7: l1 14 + the array element 7";
        EXPECT_EQ(a.mHost.mMarks, (std::vector<std::int32_t>{ 14, 21 }));
        EXPECT_EQ(b.varText(id, "sv"), "loc-str!") << "the string and the array made before the wait";
        EXPECT_EQ(b.varInt(id, "iv"), 14);
        EXPECT_EQ(b.mRuntime->suspendedStacks(), 0u);
    }

    TEST(PapyrusSave, a_wait_inside_a_called_function_resumes_the_whole_chain)
    {
        Node a;
        const InstanceId id = a.make(1);
        a.send(id, "OnNested");
        a.tick(0.0);
        Node b;
        ASSERT_TRUE(b.mRuntime->restore(a.mRuntime->serialise()).ok);
        EXPECT_EQ(b.varInt(id, "iv"), 1);
        b.tick(2.9);
        EXPECT_EQ(b.varInt(id, "iv"), 1);
        b.tick(3.0);
        EXPECT_EQ(b.varInt(id, "iv"), 2);
        EXPECT_EQ(b.mHost.mMarks, (std::vector<std::int32_t>{ 2 }));
    }

    TEST(PapyrusSave, events_that_have_not_started_run_after_restore_in_the_order_they_were_queued)
    {
        Node a;
        const InstanceId x = a.make(1);
        const InstanceId y = a.make(2);
        a.send(x, "OnMark", { I(1) });
        a.send(y, "OnMark", { I(2) });
        a.send(x, "OnMark", { I(3) });
        Node b;
        ASSERT_TRUE(b.mRuntime->restore(a.mRuntime->serialise()).ok);
        b.tick(0.0);
        a.tick(0.0);
        EXPECT_EQ(b.mHost.mMarks, (std::vector<std::int32_t>{ 1, 2, 3 }));
        EXPECT_EQ(a.mHost.mMarks, b.mHost.mMarks);
    }

    TEST(PapyrusSave, a_host_wait_keeps_its_token_and_the_host_can_still_complete_it)
    {
        Node a;
        const InstanceId id = a.make(1);
        a.send(id, "OnPark");
        a.tick(0.0);
        ASSERT_EQ(a.mHost.mTokens.size(), 1u);
        const std::uint64_t token = a.mHost.mTokens[0];
        Node b;
        ASSERT_TRUE(b.mRuntime->restore(a.mRuntime->serialise()).ok);
        b.tick(1.0e6);
        EXPECT_EQ(b.mRuntime->suspendedStacks(), 1u) << "still waiting for the host";
        EXPECT_FALSE(b.mRuntime->completeLatent(token + 1));
        EXPECT_TRUE(b.mRuntime->completeLatent(token)) << "the token the native saw before the save";
        b.tick(1.0e6);
        EXPECT_EQ(b.varInt(id, "iv"), 1);
        EXPECT_EQ(b.mHost.mMarks, (std::vector<std::int32_t>{ 1 }));
    }

    TEST(PapyrusSave, a_stack_in_the_highest_allowed_pool_slot_is_restored_with_its_token_in_well_under_a_second)
    {
        Node a;
        const InstanceId id = a.make(1);
        a.send(id, "OnPark");
        a.tick(0.0);
        ASSERT_EQ(a.mHost.mTokens.size(), 1u);
        Bytes bytes = a.mRuntime->serialise().bytes;
        const std::uint64_t token = a.mHost.mTokens[0];
        // The last place the token's eight bytes appear is the stack's own (the scalars before the texts may look the
        // same by chance).
        std::size_t at = bytes.size() - 12;
        for (; at > Save::kHeaderBytes; --at)
        {
            std::uint64_t seen = 0;
            std::memcpy(&seen, bytes.data() + at, 8);
            if (seen == token)
            {
                break;
            }
        }
        ASSERT_GT(at, Save::kHeaderBytes + 100);
        const std::uint32_t slot = static_cast<std::uint32_t>(Save::kMaxPoolSlots - 1);
        const std::uint64_t moved = ((static_cast<std::uint64_t>(slot) + 1) << 32) | (token & 0xFFFFFFFFu);
        std::memcpy(bytes.data() + at, &moved, 8);
        std::memcpy(bytes.data() + at + 8, &slot, 4);
        Node b;
        const auto start = std::chrono::steady_clock::now();
        const RestoreResult result = b.mRuntime->restore(blobOf(fixHeader(bytes)));
        const double seconds = std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count();
        ASSERT_TRUE(result.ok) << result.error.format();
        EXPECT_LT(seconds, 1.0) << "the pool grows to the slot: about 2^18 stacks at the most";
        EXPECT_TRUE(b.mRuntime->completeLatent(moved));
        b.tick(1.0);
        EXPECT_EQ(b.varInt(id, "iv"), 1);
    }

    TEST(PapyrusSave, a_completed_wait_that_had_not_woken_yet_wakes_first_after_restore)
    {
        Node a;
        const InstanceId slow = a.make(1);
        const InstanceId host = a.make(2);
        a.send(slow, "OnChain", { F(1.0f), I(100) });
        a.send(host, "OnPark");
        a.tick(0.0);
        ASSERT_TRUE(a.mRuntime->completeLatent(a.mHost.mTokens[0]));
        Node b;
        ASSERT_TRUE(b.mRuntime->restore(a.mRuntime->serialise()).ok);
        b.tick(1.0);
        a.tick(1.0);
        EXPECT_EQ(b.mHost.mMarks, (std::vector<std::int32_t>{ 1, 101 })) << "the completed one before the timed one";
        EXPECT_EQ(a.mHost.mMarks, (std::vector<std::int32_t>{ 100, 1, 101 }));
    }

    TEST(PapyrusSave, equal_wake_times_keep_their_creation_order)
    {
        Node a;
        std::vector<InstanceId> ids;
        for (std::uint32_t i = 0; i < 8; ++i)
        {
            ids.push_back(a.make(i + 1));
        }
        for (std::size_t i = 0; i < ids.size(); ++i)
        {
            a.send(ids[i], "OnChain", { F(2.0f), I(static_cast<std::int32_t>(100 * (i + 1))) });
        }
        a.tick(0.0);
        Node b;
        ASSERT_TRUE(b.mRuntime->restore(a.mRuntime->serialise()).ok);
        b.tick(2.0);
        a.tick(2.0);
        const std::vector<std::int32_t> tailA(a.mHost.mMarks.end() - 8, a.mHost.mMarks.end());
        EXPECT_EQ(b.mHost.mMarks, tailA);
        EXPECT_EQ(b.mHost.mMarks.front(), 101);
        EXPECT_EQ(b.mHost.mMarks.back(), 801);
    }

    TEST(PapyrusSave, the_time_of_the_last_tick_is_part_of_the_state)
    {
        // A wait started by call() is due at the time of the last tick plus the wait.
        Node a;
        const InstanceId id = a.make(1);
        a.tick(100.0);
        Node b;
        ASSERT_TRUE(b.mRuntime->restore(a.mRuntime->serialise()).ok);
        ASSERT_TRUE(a.call(id, "Helper").suspended);
        ASSERT_TRUE(b.call(id, "Helper").suspended);
        a.tick(50.0);
        b.tick(50.0);
        EXPECT_EQ(a.mRuntime->suspendedStacks(), 1u);
        EXPECT_EQ(b.mRuntime->suspendedStacks(), 1u) << "due at 103, not at 3";
        a.tick(103.0);
        b.tick(103.0);
        EXPECT_EQ(a.mRuntime->suspendedStacks(), 0u);
        EXPECT_EQ(b.mRuntime->suspendedStacks(), 0u);
    }

    TEST(PapyrusSave, a_collection_after_restore_keeps_what_was_restored)
    {
        Node a;
        const Population p = populate(a);
        Node b;
        ASSERT_TRUE(b.mRuntime->restore(a.mRuntime->serialise()).ok);
        const InstanceId churner = b.make(900);
        for (int i = 0; i < 20; ++i)
        {
            b.send(churner, "OnChurn");
        }
        b.tick(2.0);
        EXPECT_GT(b.mRuntime->stats().collections, 0u) << "the churn collected";
        EXPECT_EQ(b.varText(p.mFilled, "sv"), "Hello-World");
        EXPECT_EQ(b.callInt(p.mFilled, "Sum"), 10 + 20 + 40 + 7);
        b.tick(6.0);
        EXPECT_EQ(b.varText(p.mLong, "sv"), "loc-str!");
    }

    // ---- replace, not merge
    // ------------------------------------------------------------------------------------------

    TEST(PapyrusSave, restore_replaces_what_the_runtime_held)
    {
        Node a;
        a.make(1);
        Node b;
        const InstanceId old1 = b.make(10);
        const InstanceId old2 = b.make(11);
        b.send(old1, "OnLongWork", { F(50.0f), I(1) });
        b.send(old2, "OnMark", { I(5) });
        b.tick(0.0);
        ASSERT_EQ(b.mRuntime->suspendedStacks(), 1u);
        b.send(old2, "OnMark", { I(6) });
        const std::size_t marksBefore = b.mHost.mMarks.size();
        ASSERT_TRUE(b.mRuntime->restore(a.mRuntime->serialise()).ok);
        EXPECT_EQ(b.mRuntime->suspendedStacks(), 0u) << "the old parked stack is gone";
        b.tick(100.0);
        EXPECT_EQ(b.mHost.mMarks.size(), marksBefore) << "and the old queued event never runs";
        EXPECT_EQ(b.mRuntime->attach(b.keyFor(10), "Saver"), 2u) << "the old keys are free again";
    }

    TEST(PapyrusSave, a_second_restore_of_the_same_blob_is_the_same_state)
    {
        Node a;
        populate(a);
        const SaveBlob blob = a.mRuntime->serialise();
        Node b;
        ASSERT_TRUE(b.mRuntime->restore(blob).ok);
        ASSERT_TRUE(b.mRuntime->restore(blob).ok);
        EXPECT_EQ(b.mRuntime->serialise().bytes, blob.bytes);
    }

    // ---- the round trip as a property ------------------------------------------------------------------------------

    TEST(PapyrusSave, random_runs_saved_and_restored_in_the_middle_behave_like_the_uninterrupted_run)
    {
        for (std::uint32_t seed = 1; seed <= 40; ++seed)
        {
            std::mt19937 rng(seed);
            auto pick = [&](std::uint32_t n) { return static_cast<std::uint32_t>(rng() % n); };
            Node a;
            std::unique_ptr<Node> b;
            std::vector<InstanceId> ids;
            const std::uint32_t count = 3 + pick(4);
            for (std::uint32_t i = 0; i < count; ++i)
            {
                ids.push_back(a.make(i + 1));
                a.call(ids.back(), "Setup");
            }
            const std::uint32_t steps = 12 + pick(10);
            const std::uint32_t saveAt = 2 + pick(steps - 3);
            double time = 0.0;
            std::size_t marksAtSave = 0;
            auto both = [&](auto&& op) {
                op(a);
                if (b)
                {
                    op(*b);
                }
            };
            for (std::uint32_t step = 0; step < steps; ++step)
            {
                if (step == saveAt)
                {
                    const SaveBlob blob = a.mRuntime->serialise();
                    ASSERT_FALSE(blob.bytes.empty()) << "seed " << seed;
                    b = std::make_unique<Node>();
                    const RestoreResult result = b->mRuntime->restore(blob);
                    ASSERT_TRUE(result.ok) << "seed " << seed << ": " << result.error.format();
                    EXPECT_EQ(b->mRuntime->serialise().bytes, blob.bytes) << "seed " << seed;
                    b->mHost.mTokens = a.mHost.mTokens; // the tokens a host kept before the save still work
                    b->mTime = a.mTime;
                    marksAtSave = a.mHost.mMarks.size();
                }
                const InstanceId target = ids[pick(count)];
                const float wait = 0.25f * static_cast<float>(pick(12));
                const std::int32_t base = static_cast<std::int32_t>(10 * pick(20));
                switch (pick(6))
                {
                    case 0:
                        both([&](Node& n) { n.send(target, "OnLongWork", { F(wait), I(base) }); });
                        break;
                    case 1:
                        both([&](Node& n) { n.send(target, "OnChain", { F(wait), I(base) }); });
                        break;
                    case 2:
                        both([&](Node& n) { n.send(target, "OnNested"); });
                        break;
                    case 3:
                        both([&](Node& n) { n.send(target, "OnPark"); });
                        break;
                    case 4:
                    {
                        if (!a.mHost.mTokens.empty())
                        {
                            const std::size_t which = pick(static_cast<std::uint32_t>(a.mHost.mTokens.size()));
                            both([&](Node& n) {
                                if (which < n.mHost.mTokens.size())
                                {
                                    n.mRuntime->completeLatent(n.mHost.mTokens[which]);
                                }
                            });
                        }
                        break;
                    }
                    default:
                        both([&](Node& n) { n.send(target, "OnMark", { I(base) }); });
                        break;
                }
                time += 0.25f * static_cast<float>(pick(8));
                both([&](Node& n) { n.tick(time); });
                if (b)
                {
                    ASSERT_EQ(b->mRuntime->suspendedStacks(), a.mRuntime->suspendedStacks())
                        << "seed " << seed << " step " << step;
                    const std::vector<std::int32_t> tailA(
                        a.mHost.mMarks.begin() + static_cast<std::ptrdiff_t>(marksAtSave), a.mHost.mMarks.end());
                    ASSERT_EQ(b->mHost.mMarks, tailA) << "seed " << seed << " step " << step;
                }
            }
            ASSERT_TRUE(b != nullptr);
            for (const InstanceId id : ids)
            {
                EXPECT_EQ(a.dump(id), b->dump(id)) << "seed " << seed << " instance " << id;
            }
        }
    }

    // ---- refusals
    // ----------------------------------------------------------------------------------------------------

    /// A runtime with state, to see that a refused blob leaves it as it was.
    struct Witness
    {
        Witness()
        {
            mPop = populate(mNode);
            mBefore = mNode.mRuntime->serialise().bytes;
        }
        bool unchanged() { return mNode.mRuntime->serialise().bytes == mBefore; }
        Node mNode;
        Population mPop;
        Bytes mBefore;
    };

    TEST(PapyrusSave, a_version_other_than_one_is_refused)
    {
        Node a;
        populate(a);
        SaveBlob blob = a.mRuntime->serialise();
        Witness w;
        blob.formatVersion = 2;
        RestoreResult result = w.mNode.mRuntime->restore(blob);
        EXPECT_FALSE(result.ok);
        EXPECT_NE(result.error.message.find("version"), std::string::npos) << result.error.message;
        blob.formatVersion = 0;
        EXPECT_FALSE(w.mNode.mRuntime->restore(blob).ok);
        // The version inside the header must agree with the field.
        blob = a.mRuntime->serialise();
        std::uint32_t inner = 2;
        std::memcpy(blob.bytes.data() + 4, &inner, 4);
        result = w.mNode.mRuntime->restore(blob);
        EXPECT_FALSE(result.ok);
        EXPECT_TRUE(w.unchanged());
    }

    TEST(PapyrusSave, an_empty_blob_and_a_foreign_blob_are_refused)
    {
        Witness w;
        EXPECT_FALSE(w.mNode.mRuntime->restore(SaveBlob{}).ok);
        EXPECT_FALSE(w.mNode.mRuntime->restore(blobOf(Bytes(100, 0xAB))).ok);
        Bytes bytes = w.mBefore;
        bytes[0] ^= 0xFF;
        const RestoreResult result = w.mNode.mRuntime->restore(blobOf(bytes));
        EXPECT_FALSE(result.ok);
        EXPECT_NE(result.error.message.find("magic"), std::string::npos) << result.error.message;
        EXPECT_TRUE(w.unchanged());
    }

    TEST(PapyrusSave, a_blob_with_a_wrong_checksum_or_length_is_refused)
    {
        Node a;
        populate(a);
        Witness w;
        Bytes bytes = a.mRuntime->serialise().bytes;
        Bytes flipped = bytes;
        flipped[flipped.size() / 2] ^= 0x01;
        RestoreResult result = w.mNode.mRuntime->restore(blobOf(flipped));
        EXPECT_FALSE(result.ok);
        EXPECT_NE(result.error.message.find("checksum"), std::string::npos) << result.error.message;
        Bytes longer = bytes;
        longer.push_back(0);
        result = w.mNode.mRuntime->restore(blobOf(longer));
        EXPECT_FALSE(result.ok);
        Bytes shorter = bytes;
        shorter.pop_back();
        EXPECT_FALSE(w.mNode.mRuntime->restore(blobOf(shorter)).ok);
        EXPECT_TRUE(w.unchanged());
    }

    TEST(PapyrusSave, every_truncation_is_refused_and_changes_nothing)
    {
        Node a;
        populate(a);
        const Bytes full = a.mRuntime->serialise().bytes;
        Witness w;
        ASSERT_TRUE(w.mNode.mRuntime->restore(blobOf(full)).ok);
        w.mBefore = w.mNode.mRuntime->serialise().bytes;
        for (std::size_t length = 0; length < full.size(); ++length)
        {
            Bytes cut(full.begin(), full.begin() + static_cast<std::ptrdiff_t>(length));
            // Both raw (the checksum fails) and with a header that was made to fit (the structure fails).
            ASSERT_FALSE(w.mNode.mRuntime->restore(blobOf(cut)).ok) << length;
            ASSERT_FALSE(w.mNode.mRuntime->restore(blobOf(fixHeader(cut))).ok) << length;
        }
        EXPECT_TRUE(w.unchanged());
    }

    TEST(PapyrusSave, errors_name_the_place_in_the_blob)
    {
        Node a;
        populate(a);
        Bytes bytes = a.mRuntime->serialise().bytes;
        bytes.resize(bytes.size() - 5);
        Witness w;
        const RestoreResult result = w.mNode.mRuntime->restore(blobOf(fixHeader(bytes)));
        ASSERT_FALSE(result.ok);
        EXPECT_NE(result.error.message.find("offset"), std::string::npos) << result.error.message;
    }

    TEST(PapyrusSave, huge_counts_are_refused_before_anything_is_allocated_for_them)
    {
        Node a;
        populate(a);
        const Bytes good = a.mRuntime->serialise().bytes;
        Witness w;
        // The first u32 after the three scalars is the text count; the one after the texts is the script count.
        const std::size_t textCountAt = Save::kHeaderBytes + 8 + 8 + 4 + 4;
        for (const std::uint32_t huge : { 0xFFFFFFFFu, 0x7FFFFFFFu, 0x10000000u, 0x01000000u })
        {
            Bytes bytes = good;
            std::memcpy(bytes.data() + textCountAt, &huge, 4);
            const auto start = std::chrono::steady_clock::now();
            const RestoreResult result = w.mNode.mRuntime->restore(blobOf(fixHeader(bytes)));
            const auto elapsed = std::chrono::steady_clock::now() - start;
            EXPECT_FALSE(result.ok) << std::hex << huge;
            EXPECT_LT(std::chrono::duration<double>(elapsed).count(), 1.0) << "no giant allocation";
        }
        EXPECT_TRUE(w.unchanged());
    }

    TEST(PapyrusSave, a_script_that_is_not_loaded_refuses_the_blob_and_names_it)
    {
        Node a;
        populate(a);
        Node b(Target::SkyrimSE, latentOptions(), 0, false);
        b.make(1000, "Game");
        const Bytes before = b.mRuntime->serialise().bytes;
        const RestoreResult result = b.mRuntime->restore(a.mRuntime->serialise());
        ASSERT_FALSE(result.ok);
        EXPECT_NE(result.error.message.find("saver"), std::string::npos)
            << "the blob holds names in lower case: " << result.error.message;
        EXPECT_EQ(b.mRuntime->serialise().bytes, before);
    }

    TEST(PapyrusSave, the_game_can_refuse_an_attach_key)
    {
        Node a;
        populate(a);
        Witness w;
        w.mNode.mWorld.mKeyCheck = [](const AttachKey& key) { return key.formId != 0x10003u; };
        const RestoreResult result = w.mNode.mRuntime->restore(a.mRuntime->serialise());
        ASSERT_FALSE(result.ok);
        EXPECT_NE(result.error.message.find("10003"), std::string::npos) << result.error.message;
        EXPECT_TRUE(w.unchanged());
        w.mNode.mWorld.mKeyCheck = nullptr;
        EXPECT_TRUE(w.mNode.mRuntime->restore(a.mRuntime->serialise()).ok);
    }

    TEST(PapyrusSave, a_blob_over_the_heap_cap_of_the_runtime_is_refused_whole)
    {
        Node a;
        populate(a);
        ScriptPapyrus::RuntimeOptions small = latentOptions();
        small.maxHeapBytes = 64;
        Node b(Target::SkyrimSE, small);
        const InstanceId id = b.make(77);
        const Bytes before = b.mRuntime->serialise().bytes;
        const RestoreResult result = b.mRuntime->restore(a.mRuntime->serialise());
        ASSERT_FALSE(result.ok);
        EXPECT_NE(result.error.message.find("heap"), std::string::npos) << result.error.message;
        EXPECT_EQ(b.mRuntime->serialise().bytes, before);
        EXPECT_NE(id, 0u);
    }

    TEST(PapyrusSave, restore_is_refused_inside_a_native_and_inside_a_world_callback)
    {
        Node a;
        populate(a);
        const SaveBlob blob = a.mRuntime->serialise();
        Node b;
        const InstanceId id = b.make(1);
        bool nativeOk = true;
        b.mHost.mInMark = [&] { nativeOk = b.mRuntime->restore(blob).ok; };
        b.send(id, "OnMark", { I(1) });
        b.tick(0.0);
        EXPECT_FALSE(nativeOk);
        b.mHost.mInMark = nullptr;
        EXPECT_EQ(b.mRuntime->stateOf(id), "") << "the instance is still the one b had";
        EXPECT_EQ(b.mRuntime->attach(b.keyFor(2), "Saver"), 2u) << "b still has exactly one instance";
        const Bytes before = b.mRuntime->serialise().bytes;
        // A world callback: the log of a runtime message.
        bool worldOk = true;
        b.mWorld.mOnLog = [&] { worldOk = b.mRuntime->restore(blob).ok; };
        b.mRuntime->attach(b.keyFor(3), "NoSuchScript");
        EXPECT_FALSE(worldOk);
        EXPECT_EQ(b.mRuntime->serialise().bytes, before);
    }

    TEST(PapyrusSave, serialise_inside_a_native_gives_no_blob_because_a_stack_is_running)
    {
        Node a;
        const InstanceId id = a.make(1);
        SaveBlob inside;
        inside.formatVersion = 99;
        a.mHost.mInMark = [&] { inside = a.mRuntime->serialise(); };
        a.send(id, "OnMark", { I(1) });
        a.tick(0.0);
        EXPECT_TRUE(inside.bytes.empty());
        EXPECT_EQ(inside.formatVersion, 0u);
        a.mHost.mInMark = nullptr;
        EXPECT_FALSE(a.mRuntime->serialise().bytes.empty());
    }

    // ---- a script that changed since the save
    // ------------------------------------------------------------------------

    TEST(PapyrusSave, a_changed_script_drops_its_started_stacks_keeps_matching_variables_and_warns)
    {
        Node a;
        const InstanceId id = a.make(1);
        a.call(id, "Setup");
        a.send(id, "OnLongWork", { F(5.0f), I(7) });
        a.tick(1.0);
        a.send(id, "OnMark", { I(33) }); // queued, not started
        ASSERT_EQ(a.mRuntime->suspendedStacks(), 1u);
        Node b(Target::SkyrimSE, latentOptions(), 1);
        const RestoreResult result = b.mRuntime->restore(a.mRuntime->serialise());
        ASSERT_TRUE(result.ok) << result.error.format();
        EXPECT_EQ(b.mRuntime->suspendedStacks(), 0u) << "the stack of the old code is dropped";
        EXPECT_TRUE(b.mWorld.logged("Saver")) << "a warning names the script";
        EXPECT_EQ(b.varInt(id, "iv"), 7);
        EXPECT_EQ(b.varText(id, "sv"), "Hello-World");
        EXPECT_EQ(b.varInt(id, "extra"), 0) << "a variable the old code did not have starts at its default";
        EXPECT_EQ(b.callInt(id, "Describe"), 5) << "the new code runs";
        b.tick(10.0);
        EXPECT_EQ(b.mHost.mMarks, (std::vector<std::int32_t>{ 33 })) << "an event that had not started is kept";
    }

    // ---- the fuzz
    // ----------------------------------------------------------------------------------------------------

    TEST(PapyrusSave, a_hundred_thousand_mutated_blobs_never_crash_and_what_is_accepted_is_sound)
    {
        Node source;
        populate(source);
        source.tick(2.0);
        const Bytes good = source.mRuntime->serialise().bytes;
        ASSERT_GT(good.size(), 200u);
        Node target;
        std::mt19937_64 rng(0x5AFE5AFEull);
        std::size_t accepted = 0;
        std::size_t refused = 0;
        const std::uint32_t interesting[]
            = { 0u, 1u, 2u, 3u, 0xFFu, 0x100u, 0xFFFFu, 0x10000u, 0x7FFFFFFFu, 0x80000000u, 0xFFFFFFFFu };
        for (int iteration = 0; iteration < 100000; ++iteration)
        {
            Bytes bytes = good;
            const int kind = static_cast<int>(rng() % 8);
            const std::size_t body = bytes.size() - Save::kHeaderBytes;
            auto at = [&] { return Save::kHeaderBytes + static_cast<std::size_t>(rng() % body); };
            switch (kind)
            {
                case 0:
                    bytes[at()] ^= static_cast<std::uint8_t>(1u << (rng() % 8));
                    break;
                case 1:
                    for (int i = 0; i < 1 + static_cast<int>(rng() % 4); ++i)
                    {
                        bytes[at()] = static_cast<std::uint8_t>(rng());
                    }
                    break;
                case 2:
                case 3:
                {
                    const std::size_t pos = at();
                    if (pos + 4 <= bytes.size())
                    {
                        const std::uint32_t value = interesting[rng() % (sizeof interesting / sizeof *interesting)];
                        std::memcpy(bytes.data() + pos, &value, 4);
                    }
                    break;
                }
                case 4:
                    bytes.resize(Save::kHeaderBytes + static_cast<std::size_t>(rng() % body));
                    break;
                case 5:
                {
                    const std::size_t pos = at();
                    const std::size_t n = 1 + static_cast<std::size_t>(rng() % 8);
                    bytes.erase(bytes.begin() + static_cast<std::ptrdiff_t>(pos),
                        bytes.begin() + static_cast<std::ptrdiff_t>(std::min(bytes.size(), pos + n)));
                    break;
                }
                case 6:
                {
                    const std::size_t pos = at();
                    for (std::size_t i = 0; i < 1 + rng() % 8; ++i)
                    {
                        bytes.insert(
                            bytes.begin() + static_cast<std::ptrdiff_t>(pos), static_cast<std::uint8_t>(rng()));
                    }
                    break;
                }
                default:
                {
                    // Two spans of the blob swapped.
                    const std::size_t p1 = at();
                    const std::size_t p2 = at();
                    const std::size_t n = std::min<std::size_t>({ 16, bytes.size() - p1, bytes.size() - p2 });
                    for (std::size_t i = 0; i < n; ++i)
                    {
                        std::swap(bytes[p1 + i], bytes[p2 + i]);
                    }
                    break;
                }
            }
            if (rng() % 10 != 0)
            {
                bytes = fixHeader(std::move(bytes));
            }
            const RestoreResult result = target.mRuntime->restore(blobOf(bytes));
            if (!result.ok)
            {
                ++refused;
                continue;
            }
            ++accepted;
            // What was accepted is a state the runtime can run, write and read again to the same bytes.
            target.tick(1.0e6);
            target.tick(2.0e6);
            const SaveBlob again = target.mRuntime->serialise();
            ASSERT_FALSE(again.bytes.empty()) << iteration;
            const RestoreResult second = target.mRuntime->restore(again);
            ASSERT_TRUE(second.ok) << iteration << ": " << second.error.format();
            ASSERT_EQ(target.mRuntime->serialise().bytes, again.bytes) << iteration;
        }
        std::printf("[ INFO ] save fuzz: %zu accepted, %zu refused\n", accepted, refused);
        EXPECT_GT(refused, 50000u);
        EXPECT_GT(accepted, 100u) << "mutations that keep the structure valid are accepted";
    }
}
