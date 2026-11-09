#pragma once

// The Papyrus runtime contract (task 0093, decision 0014): load compiled Papyrus (".pex" of Skyrim SE and Fallout 4),
// run an object's OnInit and its events against a world the game pack supplies, suspend and resume latent calls on
// time the host passes in, and save every script instance and suspended stack as a bounded, versioned blob. The
// runtime lives in the common compatibility pack. It holds no authority over the world or the save: a game pack
// supplies the world (PapyrusWorld) and the native functions (registerNative), and stores the bytes serialise returns.
//
// See host.hpp for the rules every script contract keeps (standard types only, no exception across the contract, main
// thread only). The error, budget, save blob and policy types are shared with the ObScript contract unchanged; the
// value type is not, because Papyrus values have kinds the ObScript Value cannot hold (None, Bool, arrays, structs and
// stored strings; task 0093, open question 2).
//
// Time: tick takes script time as "monotonic seconds from the host". What that time is (whether it stops in menus) is
// the game pack's choice; the runtime reads no clock.
//
// The contract types are in a NAMED namespace (an anonymous one gives each library a type of its own and aborts on
// GCC).

#include <openeden/script/host.hpp>
#include <openeden/version.hpp>

#include <compare>
#include <cstdint>
#include <functional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace openeden::script
{
    /// Identifies a loaded script (one Papyrus object per file). 0 is never a valid id.
    using ScriptId = std::uint32_t;
    /// Identifies an attached copy of a script (its variables and states). 0 is never a valid id.
    using InstanceId = std::uint32_t;

    enum class PapyrusKind : std::uint8_t
    {
        None,
        Int, ///< Signed 32 bit.
        Float,
        Bool,
        String, ///< A view valid for the call at the native boundary; the VM owns the characters.
        Object, ///< An attached script instance, or a reference to a native form by class name and FormId.
        Array, ///< A handle into the VM's heap.
        Struct, ///< A handle into the VM's heap.
    };

    struct PapyrusValue
    {
        PapyrusKind kind = PapyrusKind::None;
        std::int32_t integer = 0; ///< Int, and Bool as 0 or 1.
        float number = 0.0f; ///< Float.
        /// Object: a FormId (attached false) or an InstanceId (attached true). Array and Struct: the heap handle.
        std::uint32_t handle = 0;
        bool attached = false; ///< Object only: handle is an InstanceId.
        /// String: the characters. Object that is a native form reference: the class name ("Actor", "Quest", ...).
        std::string_view text;

        static constexpr PapyrusValue makeNone() noexcept { return {}; }
        static constexpr PapyrusValue makeInt(std::int32_t value) noexcept
        {
            return { PapyrusKind::Int, value, 0.0f, 0, false, {} };
        }
        static constexpr PapyrusValue makeFloat(float value) noexcept
        {
            return { PapyrusKind::Float, 0, value, 0, false, {} };
        }
        static constexpr PapyrusValue makeBool(bool value) noexcept
        {
            return { PapyrusKind::Bool, value ? 1 : 0, 0.0f, 0, false, {} };
        }
        static constexpr PapyrusValue makeString(std::string_view value) noexcept
        {
            return { PapyrusKind::String, 0, 0.0f, 0, false, value };
        }
        static constexpr PapyrusValue makeForm(std::string_view className, FormId form) noexcept
        {
            return { PapyrusKind::Object, 0, 0.0f, form, false, className };
        }
        static constexpr PapyrusValue makeInstance(InstanceId instance) noexcept
        {
            return { PapyrusKind::Object, 0, 0.0f, instance, true, {} };
        }
        static constexpr PapyrusValue makeArray(std::uint32_t heapHandle) noexcept
        {
            return { PapyrusKind::Array, 0, 0.0f, heapHandle, false, {} };
        }
        static constexpr PapyrusValue makeStruct(std::uint32_t heapHandle) noexcept
        {
            return { PapyrusKind::Struct, 0, 0.0f, heapHandle, false, {} };
        }
    };

    /// What a script instance is attached to. The game pack owns the mapping from its object to an instance; this key
    /// is the stable identity written in the save blob.
    enum class AttachKind : std::uint8_t
    {
        Form,
        Alias, ///< A quest (formId) and an alias index.
        ActiveEffect, ///< A runtime-created active magic effect, named by the game pack's effectKey.
    };

    struct AttachKey
    {
        AttachKind kind = AttachKind::Form;
        FormId formId = 0;
        std::uint32_t aliasIndex = 0;
        std::uint32_t effectKey = 0;

        auto operator<=>(const AttachKey&) const = default;
    };

    /// How a ".pex" file wrote the size of its object (task 0093 reader). Bethesda's compilers write a count that
    /// includes the size field itself; 81 of the installed files write the bytes after the field, 30 of them in the
    /// other byte order.
    enum class PexSizeConvention : std::uint8_t
    {
        IncludesSelf,
        ExcludesSelf,
        ExcludesSelfOtherOrder,
    };

    struct PexInfo
    {
        std::string className; ///< The object's name.
        std::string parent; ///< Empty when the script has no parent.
        std::uint8_t major = 0;
        std::uint8_t minor = 0;
        std::uint16_t gameId = 0; ///< 1 Skyrim SE, 2 Fallout 4.
        bool bigEndian = false;
        PexSizeConvention sizeConvention = PexSizeConvention::IncludesSelf;
    };

    struct LoadResult
    {
        bool ok = false;
        ScriptId script = 0; ///< Valid only when ok.
        PexInfo info; ///< Valid only when ok.
        std::vector<ErrorReport> reports; ///< Errors and warnings, in file order.
    };

    /// What link() could resolve statically (informational: a script that cannot be linked still runs).
    struct LinkReport
    {
        std::uint64_t callSites = 0; ///< Every callmethod, callstatic and callparent in every loaded script.
        std::uint64_t resolvedScript = 0; ///< Target found, and it is a script function.
        std::uint64_t resolvedNative = 0; ///< Target found, and it is declared native.
        std::uint64_t missingClass = 0; ///< The target's class is not loaded.
        std::uint64_t missingFunction = 0; ///< The class is loaded and has no such function.
        std::uint64_t dynamicReceiver = 0; ///< The receiver's class cannot be known statically.
        std::vector<ErrorReport> unresolved; ///< The first of the missing ones, capped.
        std::uint64_t unresolvedOmitted = 0; ///< Missing ones beyond the cap.
    };

    struct CallResult
    {
        bool ok = false;
        bool suspended = false; ///< The stack is parked (a latent call, or the budget slice ran out).
        std::uint64_t instructionsExecuted = 0;
        PapyrusValue returned;
        ErrorReport error; ///< Valid when !ok.
    };

    struct TickResult
    {
        std::uint32_t stacksRun = 0;
        std::uint32_t stacksFinished = 0;
        std::uint32_t stacksWaiting = 0;
        std::uint64_t instructionsExecuted = 0;
        std::vector<ErrorReport> errors;
    };

    /// Which threads may call a native. The first slice honours MainOnly; the field exists so that a later game pack
    /// can declare AnyThread without a contract change.
    enum class NativeThreadRule : std::uint8_t
    {
        MainOnly,
        AnyThread,
    };

    /// A native is bound by (class, function). The parameter and return types come from the loaded class: a native
    /// function appears in its class's ".pex" with its native flag and its full types.
    struct NativeDescriptor
    {
        std::string className;
        std::string functionName;
        bool global = false; ///< Must equal the declaration's global flag.
        bool latent = false; ///< A property of the engine, not of the ".pex": only the game pack knows it.
        NativeThreadRule threadRule = NativeThreadRule::MainOnly;
    };

    struct NativeCall
    {
        PapyrusValue self; ///< None for a global function.
        std::span<const PapyrusValue> arguments;
    };

    struct NativeResult
    {
        PapyrusValue value;
        double waitSeconds = 0.0; ///< Used only for a latent native: seconds of script time until the stack resumes.
    };

    using NativeHandler = std::function<NativeResult(const NativeCall&)>;

    /// What the game pack implements. All calls come from the main thread. Implementations must not throw (a throw is
    /// caught by the runtime and ends that stack with an error).
    class PapyrusWorld
    {
    public:
        virtual ~PapyrusWorld() = default;

        /// The class name of a form ("Actor", "Quest", ...), for casts and "is" tests on a native form. False when the
        /// form does not exist.
        virtual bool formClass(FormId form, std::string& className) const = 0;

        /// A message the script printed (Debug.Trace and the runtime's one-line notices).
        virtual void report(std::string_view message) = 0;

        /// The runtime's own log lines (a deviant file accepted, a native that is declared and not registered).
        virtual void log(Severity severity, std::string_view message) = 0;
    };

    /// An OPTIONAL capability of a PapyrusWorld (review of 2026-11-08, finding H1). PapyrusWorld itself is unchanged
    /// since 1.0, so a game pack built against any earlier header keeps its layout. A game pack that can tell whether
    /// the form, alias or effect an attach key names still exists derives its world from this class as well; the
    /// runtime asks `dynamic_cast<const PapyrusAttachKeyCheck*>(&world)` when it restores. Without it restore() does
    /// NOT validate attach keys: it says so once in the log and RestoreDetails::attachKeysChecked is false.
    class PapyrusAttachKeyCheck
    {
    public:
        virtual ~PapyrusAttachKeyCheck() = default;

        /// False when the game no longer has what  key names. A false answer refuses the whole blob (an instance
        /// cannot be left out: other values refer to it by number), and the message names the key. Must not throw.
        virtual bool attachKeyExists(const AttachKey& key) const = 0;
    };

    class PapyrusContract
    {
    public:
        static constexpr std::string_view kId = "openeden.script.papyrus";
        /// 1.0 (2026-11-08): first version.
        /// 1.1 (2026-11-09, toddhow): stage e added serialise/restore semantics and the optional
        /// PapyrusAttachKeyCheck. The bump also covered five trailing fields that RestoreResult briefly had; H0
        /// (2026-11-09) took them out again (RestoreResult has its 1.0 layout) and put them in RestoreDetails,
        /// reachable through the optional RestoreDetailsSource of the runtime, which supersedes them. The number stays
        /// 1.1: a 1.0 consumer is now ABI-safe, and a 1.1 consumer reads the details the new way.
        static constexpr sdk::Version kVersion{ 1, 1 };

        virtual ~PapyrusContract() = default;

        /// Read and validate a ".pex" file. Never executes. \a name is used in error reports (usually the file name).
        /// A failure in one file does not affect the next. A second file with the same object name is refused.
        virtual LoadResult loadScript(std::span<const std::uint8_t> bytes, std::string_view name) noexcept = 0;

        /// Resolve every call target that can be known statically across the loaded scripts.
        virtual LinkReport link() const noexcept = 0;

        /// Bind a native handler. Fails (false, \a error filled) when the class is loaded and has no such native, or
        /// the global flag differs from the declaration, or the pair is already bound.
        virtual bool registerNative(
            const NativeDescriptor& descriptor, NativeHandler handler, ErrorReport& error) noexcept = 0;

        virtual void setPolicy(UnknownFunctionPolicy policy) noexcept = 0;

        /// Make an instance of a loaded script and queue its OnInit. Returns 0 for an unknown script or a key that is
        /// already attached.
        virtual InstanceId attach(const AttachKey& key, std::string_view scriptName) noexcept = 0;

        /// Queue an event (a non-global function named On...) for an instance.
        virtual CallResult send(
            InstanceId instance, std::string_view eventName, std::span<const PapyrusValue> arguments) noexcept = 0;

        /// Call a function of an instance and run it until it finishes or suspends.
        virtual CallResult call(
            InstanceId instance, std::string_view functionName, std::span<const PapyrusValue> arguments) noexcept = 0;

        /// Run the ready stacks and wake the ones whose time has come. \a scriptTime must not go backwards.
        virtual TickResult tick(double scriptTime, const Budget& budget = {}) noexcept = 0;

        /// Every instance, queued event and parked stack as versioned bytes (the game pack keeps them in its own save
        /// chunk). Call it between ticks, never from a native or a world callback: while a stack is running, or from
        /// inside a callback, there is no state anyone can name, and the answer is an EMPTY blob (formatVersion 0, no
        /// bytes), which must not be saved. The same empty blob comes back for a state over the blob size cap, for an
        /// instance whose variable count does not match its script's layout, and when writing threw. Every empty
        /// blob logs one Warning "serialise: no blob written (reason)" through PapyrusWorld::log.
        virtual SaveBlob serialise() const noexcept = 0;

        /// Replace the state of the runtime from \a blob: every instance, queued event and parked stack it held is
        /// gone. Scripts and natives must be loaded and registered again first. The blob is checked whole before
        /// anything changes; a refused blob changes nothing and the error says where in the blob it went wrong. A
        /// script that changed since the save keeps its variables that still match by name and type and loses its
        /// parked stacks (a warning is logged and RestoreDetails counts them); a script that is not loaded refuses the
        /// blob, and so does a key that the world's optional PapyrusAttachKeyCheck says is gone (without one, keys are
        /// not validated: RestoreDetails::attachKeysChecked is false). Refused from inside a native or a world callback,
        /// like call() and tick().
        virtual RestoreResult restore(const SaveBlob& blob) noexcept = 0;
    };
}
