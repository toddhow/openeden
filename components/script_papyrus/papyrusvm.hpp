#ifndef OPENEDEN_COMPONENTS_SCRIPT_PAPYRUS_PAPYRUSVM_H
#define OPENEDEN_COMPONENTS_SCRIPT_PAPYRUS_PAPYRUSVM_H

// The inside of the Papyrus runtime (task 0093 stage c): values, loaded scripts and their compiled form, the VM heap
// and the Runtime class that the files papyrusruntime.cpp (contract surface, loading, link), papyrusprepare.cpp
// (linking a script to its parents and compiling its functions), papyrusheap.cpp (strings, arrays, structs, garbage
// collection) and papyrusinterp.cpp (the interpreter, calls and natives) share. Nothing here is part of the contract.
//
// Ownership: the Runtime owns every script, instance, stack and heap object; nothing holds a pointer that a saved game
// could not name (a stack is plain data: ids, indexes and values; stage e writes it as a blob). Main thread only.

#include <cstdint>
#include <deque>
#include <functional>
#include <map>
#include <memory>
#include <string>
#include <string_view>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include "papyruslimits.hpp"
#include "papyrusruntime.hpp"
#include "pexfile.hpp"

namespace ScriptPapyrus::Vm
{
    // ---- values
    // ------------------------------------------------------------------------------------------------------

    /// What a value holds. Strings, arrays and structs are handles into the Runtime (a string is a pool id, an array
    /// and a struct a heap id); a Form is a native form reference (form id and a class name id); an Instance is an
    /// InstanceId.
    enum class Kind : std::uint8_t
    {
        None,
        Int,
        Float,
        Bool,
        String,
        Form,
        Instance,
        Array,
        Struct,
    };

    struct Val
    {
        Kind kind = Kind::None;
        std::uint32_t a
            = 0; ///< Int and Float: the bits. Bool: 0 or 1. String, Array, Struct: the id. Form: the FormId.
        std::uint32_t b = 0; ///< Form: the id of the lower-cased class name, 0 when unknown.

        static Val makeInt(std::int32_t value);
        static Val makeFloat(float value);
        static Val makeBool(bool value);
        static Val makeString(std::uint32_t id) { return { Kind::String, id, 0 }; }
        static Val makeForm(std::uint32_t form, std::uint32_t classId) { return { Kind::Form, form, classId }; }
        static Val makeInstance(std::uint32_t id) { return { Kind::Instance, id, 0 }; }
        static Val makeArray(std::uint32_t id) { return { Kind::Array, id, 0 }; }
        static Val makeStruct(std::uint32_t id) { return { Kind::Struct, id, 0 }; }

        std::int32_t asInt() const;
        float asFloat() const;
    };

    /// A declared type, from the text of the file ("Int", "Float[]", "Actor", "Script#Struct", "Var").
    enum class TypeKind : std::uint8_t
    {
        None,
        Int,
        Float,
        Bool,
        String,
        Object,
        Array,
        Struct,
        Var,
    };

    struct TypeRef
    {
        TypeKind kind = TypeKind::None;
        TypeKind elem = TypeKind::None; ///< Array: the element's kind.
        std::uint32_t name = 0; ///< Object: the class name id. Struct: the declaring script's name id.
        std::uint32_t name2 = 0; ///< Struct: the struct's name id.
    };

    // ---- compiled code
    // -----------------------------------------------------------------------------------------------

    enum class OpKind : std::uint8_t
    {
        None, ///< No operand (a position the opcode does not have).
        Local, ///< A parameter or local: n is the slot in the frame.
        Var, ///< A variable of the instance: n is the slot in the instance (parents first).
        Self,
        StateVar, ///< "::State": the current state's name as a String.
        Discard, ///< "::NoneVar": reads None, writes go nowhere.
        Const,
        Name, ///< A function, class, member or type name: n is the lower-cased name id.
        Bad, ///< An identifier that names nothing: running it is an error.
    };

    struct COp
    {
        OpKind kind = OpKind::None;
        std::uint32_t n = 0;
        Val v;
    };

    struct CInstr
    {
        std::uint8_t op = 0;
        std::uint16_t fixed = 0;
        std::uint16_t extra = 0;
        std::uint32_t first = 0; ///< Index into CFunc::ops.
        TypeRef dest; ///< The declared type of the destination operand, where the opcode has one.
    };

    struct CFunc
    {
        std::uint32_t name = 0; ///< Lower-cased name id; the accessor's "<property>.get" or ".set" for a property.
        std::uint32_t state = 0; ///< Lower-cased state name id; the empty state is 0.
        bool global = false;
        bool native = false;
        std::uint16_t paramCount = 0;
        std::uint16_t frameSize = 0;
        TypeRef ret;
        std::vector<TypeRef> slotTypes; ///< Parameters, then locals.
        std::vector<CInstr> code;
        std::vector<COp> ops;
        bool broken = false;
        std::string brokenWhy;
    };

    struct VarInfo
    {
        std::uint32_t name = 0;
        TypeRef type;
        Val initial;
    };

    struct PropInfo
    {
        TypeRef type;
        bool autoProperty = false;
        std::uint32_t backing = 0; ///< Auto: the index of the backing variable among the script's own variables.
        std::int32_t getter = -1; ///< Index into Script::mFuncs, or -1.
        std::int32_t setter = -1;
    };

    struct StructInfo
    {
        std::vector<std::uint32_t> memberNames;
        std::vector<TypeRef> memberTypes;
        std::vector<Val> memberDefaults;
    };

    struct StateInfo
    {
        std::uint32_t name = 0;
        std::unordered_map<std::uint32_t, std::uint32_t> functions; ///< Function name id to index in Script::mFuncs.
    };

    // A loaded script: the parsed file and what the link report needs (task 0093 stage b), then what running needs.
    struct Script
    {
        std::string mFileName; ///< As given to loadScript, for reports.
        std::string mClass; ///< The object's name.
        std::string mParent; ///< The parent's name, or empty.
        std::uint64_t mDigest = 0; ///< FNV-1a of the bytes the script was loaded from (what a saved game checks).
        Pex::File mFile;
        /// Lower-cased function name to "is native", over every state and property accessor of the object.
        std::unordered_map<std::string, bool> mFunctions;

        // Set by prepare() (papyrusprepare.cpp).
        bool mPrepared = false; ///< prepare() has finished, well or not (mPrepareFailed says which).
        bool mPreparing = false; ///< prepare() is running on it: meeting it again is an inheritance cycle.
        bool mPrepareFailed = false;
        std::string mPrepareError;
        std::uint32_t mClassId = 0; ///< Lower-cased class name id.
        std::uint32_t mParentId = 0; ///< Lower-cased parent name id, 0 when none.
        std::uint32_t mMissingParentId = 0; ///< The parent's name id when it is not loaded, else 0.
        std::vector<std::uint32_t> mChain; ///< This script, its parent, its grandparent, ... (indexes into mScripts).
        std::uint32_t mVarBase = 0; ///< Slots taken by the parents' variables.
        std::vector<VarInfo> mVars; ///< This script's own variables.
        std::unordered_map<std::uint32_t, std::uint32_t> mVarIndex; ///< Name id to index in mVars.
        std::uint32_t mAutoStateName = 0; ///< Pool id of the auto state's name as written.
        std::uint32_t mAutoStateId = 0; ///< Its lower-cased name id.
        std::vector<CFunc> mFuncs;
        std::vector<StateInfo> mStates;
        std::unordered_map<std::uint32_t, std::uint32_t> mStateIndex; ///< State name id to index in mStates.
        std::unordered_map<std::uint32_t, PropInfo> mProps;
        std::unordered_map<std::uint32_t, StructInfo> mStructs; ///< Struct name id to its definition.
        struct TypeCache
        {
            TypeRef mType;
            bool mSet = false;
        };
        std::vector<TypeCache> mStringTypes;
        std::vector<std::uint32_t> mStringIds; ///< internString id per string table entry, kNoScript until asked.
        std::vector<std::uint32_t> mStringNameIds; ///< nameId per string table entry, kNoScript until asked.

        std::uint32_t totalVars() const { return mVarBase + static_cast<std::uint32_t>(mVars.size()); }
    };

    // ---- names, strings, heap
    // ----------------------------------------------------------------------------------------

    struct TextHash
    {
        using is_transparent = void;
        std::size_t operator()(std::string_view text) const noexcept { return std::hash<std::string_view>{}(text); }
        std::size_t operator()(const std::string& text) const noexcept { return std::hash<std::string_view>{}(text); }
    };

    struct TextEqual
    {
        using is_transparent = void;
        bool operator()(std::string_view a, std::string_view b) const noexcept { return a == b; }
    };

    /// Names compared without regard to case: the table holds the lower-cased spelling and the first spelling seen.
    struct NameTable
    {
        std::unordered_map<std::string, std::uint32_t, TextHash, TextEqual> mIds;
        std::deque<std::string> mShown; ///< The first spelling seen, for messages (stable addresses).
        std::vector<std::uint8_t> mIsEvent; ///< The name starts with "on".
    };

    struct StrEntry
    {
        std::string text;
        bool dynamic = false;
        bool mark = false;
        bool live = true;
    };

    struct HeapObj
    {
        enum class Type : std::uint8_t
        {
            Free,
            Array,
            Struct,
        };
        Type type = Type::Free;
        bool mark = false;
        TypeKind elem = TypeKind::None; ///< Array: the element kind.
        std::uint32_t script = 0; ///< Struct: index of the declaring script.
        std::uint32_t structName = 0; ///< Struct: the struct's name id.
        std::vector<Val> items; ///< Array elements, or struct members in definition order.
    };

    // ---- instances, stacks
    // -------------------------------------------------------------------------------------------

    struct Instance
    {
        openeden::script::AttachKey key;
        std::uint32_t script = 0;
        std::uint32_t stateName = 0; ///< Pool id of the state's name as written.
        std::uint32_t stateId = 0; ///< Lower-cased name id of the state.
        std::vector<Val> vars;
    };

    struct Frame
    {
        std::uint32_t script = 0;
        std::uint32_t func = 0;
        std::uint32_t pc = 0;
        std::uint32_t base = 0; ///< Index in Stack::values of this frame's first slot.
        Val self;
        COp retDest; ///< Where the caller wants the result.
    };

    struct Stack
    {
        std::uint32_t instance = 0; ///< The instance the event or call is for.
        std::uint32_t eventName = 0; ///< Lower-cased name id of the function to start.
        std::uint32_t argCount = 0; ///< Arguments waiting in values[0, argCount) until the stack starts.
        bool started = false;
        std::uint64_t seq = 0; ///< Creation sequence: the tie-break of equal wake times.
        double wake = 0.0; ///< While parked: the script time it is due at (-infinity: the host completed it).
        /// The latent token of the park in progress or the last one (a new one for every park, so a token kept from an
        /// earlier wait never matches a later one): pool index + 1 in the high half, a counter in the low half.
        std::uint64_t parkToken = 0;
        std::uint32_t poolIndex = 0; ///< Position in Runtime::mStackPool, part of the token.
        static constexpr std::uint32_t kNotInHeap = 0xFFFFFFFFu;
        std::uint32_t heapPos = kNotInHeap; ///< Index in Runtime::mWaiting while parked there (else kNotInHeap).
        bool hostWait = false; ///< Parked for the host's completeLatent (it may still time out).
        std::vector<Frame> frames;
        std::vector<Val> values;
        Val result;
    };

    /// A stack parked on a latent call (task 0093 stage d): ordered by (wake, seq); an infinite wake waits for the
    /// host.
    struct WaitEntry
    {
        double wake = 0.0;
        std::uint64_t seq = 0;
        Stack* stack = nullptr;
    };

    struct NativeEntry
    {
        openeden::script::NativeDescriptor desc;
        openeden::script::NativeHandler fn;
        std::uint32_t classId = 0;
        std::uint32_t funcId = 0;
    };

    struct Resolved
    {
        std::uint32_t script = 0;
        std::uint32_t func = 0;
        bool found = false;
    };

    /// "Not found" for a script index or a name id (the empty name is id 0).
    inline constexpr std::uint32_t kNoScript = 0xFFFFFFFFu;
    inline constexpr std::size_t kMaxCallArgs = 256;
    inline constexpr std::size_t kMaxParentDepth = 64;

    // ---- the runtime
    // -------------------------------------------------------------------------------------------------

    class Runtime final : public PapyrusRuntime
    {
    public:
        Runtime(openeden::script::PapyrusWorld& world, openeden::script::UnknownFunctionPolicy policy,
            const RuntimeOptions& options);

        // The contract.
        openeden::script::LoadResult loadScript(
            std::span<const std::uint8_t> bytes, std::string_view name) noexcept override;
        openeden::script::LinkReport link() const noexcept override;
        bool registerNative(const openeden::script::NativeDescriptor& descriptor,
            openeden::script::NativeHandler handler, openeden::script::ErrorReport& error) noexcept override;
        void setPolicy(openeden::script::UnknownFunctionPolicy policy) noexcept override { mPolicy = policy; }
        openeden::script::InstanceId attach(
            const openeden::script::AttachKey& key, std::string_view scriptName) noexcept override;
        openeden::script::CallResult send(openeden::script::InstanceId instance, std::string_view eventName,
            std::span<const openeden::script::PapyrusValue> arguments) noexcept override;
        openeden::script::CallResult call(openeden::script::InstanceId instance, std::string_view functionName,
            std::span<const openeden::script::PapyrusValue> arguments) noexcept override;
        openeden::script::TickResult tick(double scriptTime, const openeden::script::Budget& budget) noexcept override;
        /// Script state (papyrussave.cpp). The blob's layout is in papyrussave.hpp.
        openeden::script::SaveBlob serialise() const noexcept override;
        openeden::script::RestoreResult restore(const openeden::script::SaveBlob& blob) noexcept override;

        // The additions of PapyrusRuntime.
        const Stats& stats() const noexcept override { return mStats; }
        bool getVariable(openeden::script::InstanceId instance, std::string_view name,
            openeden::script::PapyrusValue& out) const noexcept override;
        bool setVariable(openeden::script::InstanceId instance, std::string_view name,
            const openeden::script::PapyrusValue& value) noexcept override;
        std::string stateOf(openeden::script::InstanceId instance) const override;
        std::vector<std::pair<std::string, std::uint64_t>> nativeCallSites() const override;
        std::uint32_t suspendedStacks() const noexcept override { return mSuspendedCount; }
        std::uint64_t latentToken() const noexcept override { return mLatentToken; }
        bool completeLatent(std::uint64_t token) noexcept override;
        std::uint32_t cancelWaiting(openeden::script::InstanceId instance) noexcept override;

    private:
        // papyrusruntime.cpp
        openeden::script::LoadResult load(std::span<const std::uint8_t> bytes, std::string_view name);
        openeden::script::LinkReport buildLinkReport(std::map<std::string, std::uint64_t>* natives) const;
        const Script* find(const std::string& lowerClass) const
        {
            auto found = mByName.find(lowerClass);
            return found == mByName.end() ? nullptr : &mScripts[found->second];
        }
        openeden::script::CallResult callImpl(openeden::script::InstanceId instance, std::string_view functionName,
            std::span<const openeden::script::PapyrusValue> arguments);
        openeden::script::CallResult sendImpl(openeden::script::InstanceId instance, std::string_view eventName,
            std::span<const openeden::script::PapyrusValue> arguments);
        openeden::script::TickResult tickImpl(double scriptTime, const openeden::script::Budget& budget);
        openeden::script::InstanceId attachImpl(const openeden::script::AttachKey& key, std::string_view scriptName);
        bool registerNativeImpl(const openeden::script::NativeDescriptor& descriptor,
            openeden::script::NativeHandler handler, openeden::script::ErrorReport& error);
        bool getVariableImpl(
            openeden::script::InstanceId instance, std::string_view name, openeden::script::PapyrusValue& out) const;
        bool setVariableImpl(
            openeden::script::InstanceId instance, std::string_view name, const openeden::script::PapyrusValue& value);

        // papyrusprepare.cpp
        bool prepare(std::uint32_t scriptIndex);
        void prepareFail(Script& script, const std::string& why);
        TypeRef parseType(std::string_view text);
        Val valueOfPex(Script& script, const Pex::Value& value);
        /// parseType and internString of a string table entry, worked out once per entry.
        TypeRef scriptType(Script& script, std::uint32_t stringIndex);
        std::uint32_t scriptStringId(Script& script, std::uint32_t stringIndex);
        Val zeroOf(const TypeRef& type) const;
        void compileFunction(Script& script, const Pex::Function& source, std::uint32_t stateId, CFunc& out);
        std::uint32_t scriptByClassId(std::uint32_t classId) const;
        const VarInfo* findVar(const Instance& instance, std::uint32_t nameId, std::uint32_t& slot) const;

        // names and strings (papyrusheap.cpp)
        std::uint32_t nameId(std::string_view text);
        /// nameId of string  stringIndex of  script's string table, worked out once per entry (a long name used
        /// by many operands is hashed once).
        std::uint32_t scriptNameId(Script& script, std::uint32_t stringIndex);
        std::uint32_t findNameId(std::string_view text) const; ///< 0 when never seen (and the text is not empty).
        std::string_view nameText(std::uint32_t id) const;
        std::uint32_t internString(std::string_view text);
        /// Empty when  text can be added to the name table / the permanent strings; else why not (the table is full).
        std::string nameTableFull(std::string_view text) const;
        std::string stringTableFull(std::string_view text) const;
        std::string_view text(std::uint32_t stringId) const { return mStrings[stringId].text; }
        std::string_view textOf(const Val& value) const;
        bool newString(std::string_view text, Val& out);
        bool newArray(TypeKind elem, std::size_t length, Val& out);
        bool newStruct(std::uint32_t scriptIndex, std::uint32_t structName, Val& out);
        bool chargeHeap(std::uint64_t bytes);
        void collect();
        void markVal(const Val& value, std::vector<std::uint32_t>& pending);

        // papyrusinterp.cpp
        bool execute(Stack& stack, std::uint64_t maxInstructions, std::uint32_t maxDepth, std::uint64_t& executed);
        bool startStack(Stack& stack, std::uint32_t maxDepth);
        bool pushFrame(Stack& stack, std::uint32_t script, std::uint32_t func, const Val& self, const Val* args,
            std::size_t argCount, const COp& dest, std::uint32_t maxDepth);
        Resolved findFunction(std::uint32_t startScript, std::uint32_t stateId, std::uint32_t nameId);
        bool findProperty(
            std::uint32_t startScript, std::uint32_t nameId, std::uint32_t& scriptIndex, const PropInfo*& info) const;
        bool classDerives(std::uint32_t startScript, std::uint32_t targetId) const;
        bool valueIsClass(const Val& value, std::uint32_t targetId);
        std::uint32_t classOfForm(const Val& value);
        Val readOp(const Stack& stack, const Frame& frame, const COp& op);
        void writeOp(Stack& stack, const Frame& frame, const COp& op, const Val& value);
        Instance* instanceOf(const Val& self);
        const Instance* instanceOf(const Val& self) const;
        bool truthy(const Val& value) const;
        bool valuesEqual(const Val& a, const Val& b) const;
        bool stringsEqual(std::uint32_t a, std::uint32_t b) const;
        void appendText(std::string& out, const Val& value);
        bool castTo(const Val& source, const TypeRef& target, Val& out);
        Val coerce(const Val& value, const TypeRef& type);
        bool doCall(Stack& stack, const CInstr& instr, const COp* ops, std::uint32_t pcOfInstr, std::uint32_t maxDepth);
        bool callResolved(Stack& stack, const Resolved& resolved, const Val& self, const Val* args,
            std::size_t argCount, const COp& dest, const TypeRef& destType, std::uint32_t maxDepth);
        bool callNative(Stack& stack, const Resolved& resolved, const Val& self, const Val* args, std::size_t argCount,
            const COp& dest);
        bool unresolvedCall(Stack& stack, ErrorKind kind, std::uint32_t classId, std::uint32_t nameId, const COp& dest,
            const TypeRef& destType, const char* what);
        bool doProperty(Stack& stack, const CInstr& instr, const COp* ops, bool set, std::uint32_t maxDepth);
        bool doArrayOp(Stack& stack, const CInstr& instr, const COp* ops);
        bool doStructOp(Stack& stack, const CInstr& instr, const COp* ops);
        bool toPapyrus(const Val& value, openeden::script::PapyrusValue& out) const;
        bool fromPapyrus(const openeden::script::PapyrusValue& value, Val& out);

        // errors and logging
        bool fail(ErrorKind kind, const std::string& message);
        void logOnce(const std::string& key, const std::string& message);
        /// Every call into the world goes through here or through WorldScope: the runtime refuses call, tick,
        /// loadScript and registerNative while a native or a world callback is running.
        void worldLog(openeden::script::Severity severity, std::string_view message);
        struct WorldScope
        {
            explicit WorldScope(Runtime& runtime)
                : mRuntime(runtime)
                , mPrevious(runtime.mInNative)
            {
                runtime.mInNative = true;
            }
            ~WorldScope() { mRuntime.mInNative = mPrevious; }
            WorldScope(const WorldScope&) = delete;
            WorldScope& operator=(const WorldScope&) = delete;
            Runtime& mRuntime;
            bool mPrevious;
        };
        /// Gives a stack back to the pool when it goes out of scope, however the scope is left.
        struct StackLease
        {
            StackLease(Runtime& runtime, Stack* stack)
                : mRuntime(runtime)
                , mStack(stack)
            {
            }
            ~StackLease()
            {
                if (mStack != nullptr)
                {
                    mRuntime.releaseStack(mStack);
                }
            }
            void keep() { mStack = nullptr; }
            StackLease(const StackLease&) = delete;
            StackLease& operator=(const StackLease&) = delete;
            Runtime& mRuntime;
            Stack* mStack;
        };
        /// The error of the instruction that failed as a report (and counted and logged); clears the pending error.
        openeden::script::ErrorReport takeError();
        /// An error of the contract surface itself (no stack involved), counted and logged.
        openeden::script::ErrorReport apiError(ErrorKind kind, const std::string& message);

        // stacks (papyrusruntime.cpp)
        Stack* acquireStack();
        void releaseStack(Stack* stack);
        bool fillArguments(Stack& stack, std::span<const openeden::script::PapyrusValue> arguments);
        /// 0: the stack ran to its end; 1: the function is not defined and  isEvent was set; 2: it failed (the error
        /// is pending: takeError()).
        int runStack(
            Stack& stack, bool isEvent, std::uint64_t maxInstructions, std::uint32_t maxDepth, std::uint64_t& executed);
        int runStackBody(
            Stack& stack, bool isEvent, std::uint64_t maxInstructions, std::uint32_t maxDepth, std::uint64_t& executed);
        bool queueEvent(std::uint32_t instanceId, std::uint32_t eventName,
            std::span<const openeden::script::PapyrusValue> arguments, openeden::script::ErrorReport& error);
        bool storable(const TypeRef& type, const openeden::script::PapyrusValue& value) const;
        std::uint64_t effectiveInstructions(const openeden::script::Budget& budget) const;
        std::uint32_t effectiveDepth(const openeden::script::Budget& budget) const;
        void gatherRoots(std::vector<std::uint32_t>& pending);
        // papyrussave.cpp
        openeden::script::SaveBlob serialiseImpl(std::string& why) const;
        void logEmptyBlob(std::string_view why) const noexcept;
        openeden::script::RestoreResult restoreImpl(const openeden::script::SaveBlob& blob);
        /// The digest of a script and of every script it inherits from: what a saved instance was written against.
        std::uint64_t chainDigest(std::uint32_t scriptIndex) const;
        // papyrussched.cpp
        /// Puts mWaiting in heap order and each stack's position in its heapPos (restore fills mWaiting unordered).
        void rebuildWaiting();
        /// Makes room for one more parked stack (and for the due ones that may go back): may throw, so it runs before
        /// the handler of a latent native, never after.
        void reserveForPark();
        void parkStack(Stack& stack);
        /// execute() with the suspension request turned into a result: 0 finished, 2 failed, 3 parked.
        int executeStack(Stack& stack, std::uint64_t maxInstructions, std::uint32_t maxDepth, std::uint64_t& executed);
        double acceptTime(double scriptTime);
        std::uint32_t arrayLimit() const { return mOptions.maxArrayLength; }

        openeden::script::PapyrusWorld& mWorld;
        openeden::script::UnknownFunctionPolicy mPolicy;
        RuntimeOptions mOptions;
        Stats mStats;

        std::vector<Script> mScripts; ///< In load order.
        std::map<std::string, std::size_t> mByName; ///< Lower-cased class name to index in mScripts.
        std::unordered_map<std::uint32_t, std::uint32_t> mScriptByClassId;

        NameTable mNames;
        std::deque<StrEntry> mStrings;
        std::unordered_map<std::string, std::uint32_t, TextHash, TextEqual> mInterned;
        std::vector<std::uint32_t> mFreeStrings;
        std::vector<std::uint32_t>
            mLiveStrings; ///< Ids of the dynamic strings that are alive (what a collection visits).
        std::uint64_t mPermanentBytes = 0; ///< Bytes of the names and permanent strings (counted twice: key and copy).
        std::deque<HeapObj> mHeap;
        std::vector<std::uint32_t> mFreeHeap;
        std::vector<std::uint32_t> mLiveHeap; ///< Ids of the arrays and structs that are alive.
        std::uint64_t mHeapBytes = 0;
        std::vector<std::uint32_t> mMarkScratch;

        std::vector<Instance> mInstances; ///< InstanceId is the index plus one.
        std::map<openeden::script::AttachKey, std::uint32_t> mByKey;

        std::vector<NativeEntry> mNatives;
        std::unordered_map<std::uint64_t, std::uint32_t> mNativeByKey;
        std::unordered_map<std::uint64_t, Resolved> mFunctionCache;
        /// Class name id to the scripts that were prepared while it was not loaded (see load()).
        std::unordered_map<std::uint32_t, std::vector<std::uint32_t>> mWaitingForParent;

        std::vector<std::unique_ptr<Stack>> mStackPool;
        std::vector<Stack*> mFreeStacks;
        std::vector<Stack*> mReady; ///< Queued stacks, oldest first (from mReadyHead).
        std::size_t mReadyHead = 0;
        std::vector<Stack*> mRunning; ///< Stacks being executed (calls can nest through natives).
        // The scheduler (papyrussched.cpp). mWaiting is a binary heap on (wake, seq); mDue holds the entries a tick
        // took out of it (from mDueHead on, the ones not run yet).
        std::vector<WaitEntry> mWaiting;
        std::vector<WaitEntry> mDue;
        std::size_t mDueHead = 0;
        std::uint32_t mSuspendedCount = 0; ///< Parked stacks: mWaiting and the not yet run part of mDue.
        std::uint64_t mNextSeq = 1;
        std::uint64_t mLatentToken = 0;
        std::uint32_t mParkCounter = 0; ///< Low half of the next latent token.
        bool mSuspendHost = false; ///< The park being requested waits for the host.
        double mNow = 0.0; ///< The time of the last tick.
        bool mSuspendRequested = false; ///< A latent native asked for the running stack to be parked.
        double mSuspendWake = 0.0;

        std::unordered_set<std::string> mLoggedOnce;
        std::string mScratch; ///< Reused text buffer.
        std::string mScratchClass;
        std::string mNameScratch;
        mutable std::string mNameScratchConst;
        std::vector<openeden::script::PapyrusValue> mNativeArgs;
        std::vector<Val> mArgScratch; ///< Arguments of the call being made (kMaxCallArgs).
        std::vector<Val> mMatchScratch;
        std::size_t mPrepareDepth = 0;
        bool mInNative = false; ///< A native handler is running: the contract refuses calls and ticks from it.
        bool mRestoring = false; ///< restore() is building the new state: heap charges are counted, never collected.

        // The error of the instruction being executed (None when there is none).
        ErrorKind mErrKind = ErrorKind::None;
        std::string mErrMsg;
        std::string mErrFile; ///< The script file of the frame that failed.
        std::string mErrLoc; ///< "Class.function@instruction" of the frame that failed.
    };
}

#endif
