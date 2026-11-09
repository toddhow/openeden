#ifndef OPENEDEN_COMPONENTS_SCRIPT_PAPYRUS_PAPYRUSRUNTIME_H
#define OPENEDEN_COMPONENTS_SCRIPT_PAPYRUS_PAPYRUSRUNTIME_H

#include <array>
#include <cstdint>
#include <limits>
#include <memory>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include <openeden/script/papyrus.hpp>

namespace ScriptPapyrus
{
    /// What ended a stack (task 0093 stage c). The name is written in brackets at the start of the error message, so
    /// a report and a counter agree.
    enum class ErrorKind : std::uint8_t
    {
        None,
        BadScript, ///< The script cannot be run (a name that resolves nowhere, a parent that is missing in strict
                   ///< mode).
        NoSuchInstance,
        NoSuchFunction,
        NoneReceiver, ///< A call or a property access on None (strict mode), an array access on a None array.
        MissingClass, ///< A call to a class that is not loaded (strict mode).
        TypeMismatch,
        ArgumentCount,
        DivideByZero,
        IndexOutOfRange,
        LimitExceeded, ///< An array, a string or the heap over its cap.
        BudgetExceeded,
        DepthExceeded,
        UnsupportedOpcode,
        UnregisteredNative, ///< Strict mode: a native that the class declares and nobody registered.
        LatentUnsupported, ///< Strict mode: a latent native (stage d).
        NativeFailed, ///< A native handler threw.
        Internal,
        Count,
    };

    const char* errorKindName(ErrorKind kind) noexcept;

    /// Settings of one runtime. Not part of the contract: the game pack that creates the runtime chooses them.
    struct RuntimeOptions
    {
        /// Used where a Budget field is 0. The engine's own limits are RECALLED-VERIFY.
        openeden::script::Budget defaultBudget{ 1'000'000, 64 };
        /// Longest array a script may make. The engine's own default is 128 (RECALLED-VERIFY); the hard ceiling is
        /// 65,536 and a larger number is clamped to it.
        std::uint32_t maxArrayLength = 128;
        std::uint32_t maxStringBytes = 64 * 1024;
        /// Bytes of strings, arrays and structs a runtime may hold (counted as the characters and 12 bytes a value).
        std::uint64_t maxHeapBytes = 16u << 20;
        /// Instances one runtime may hold.
        std::uint32_t maxInstances = 1u << 20;
        /// Instructions all the stacks of one tick() may run together. No new stack is started once the total has
        /// reached it (the one running finishes within its own budget); stacks not started stay queued for the next
        /// tick.
        std::uint64_t maxInstructionsPerTick = 20'000'000;
        /// Whole-runtime bounds on the tables scripts and hosts can grow (review of 2026-11-08, finding 3): distinct
        /// names (every identifier, function, class and state name, case-folded), distinct permanent strings (script
        /// constants and state names) and the bytes the last two hold. Past one of them the instruction or call
        /// that would add an entry fails with a located error. The defaults are far above the installed games'.
        std::uint32_t maxNames = 1u << 21;
        std::uint32_t maxInternedStrings = 1u << 21;
        std::uint64_t maxInternedBytes = 256u << 20;
        /// Task 0093 stage d. False (the stage c behaviour): a latent native that the host registered is not run;
        /// tolerant mode returns the zero of its type and counts Stats::latentSkipped, strict mode fails the stack.
        /// True: its handler runs, the stack is parked for NativeResult::waitSeconds of script time (or until the host
        /// calls completeLatent) and the other stacks go on. Off by default so that a runtime built for stage c does
        /// not change; a game pack that registers latent natives turns it on.
        bool runLatentNatives = false;
        /// Stacks parked on latent calls at one time (timed and waiting for the host together). A latent call over it
        /// ends that stack with [limit exceeded]; the others are unaffected.
        std::uint32_t maxSuspendedStacks = 1u << 16;
        /// The longest timed wait, in seconds of script time (a longer one is clamped to it, an infinite one too).
        double maxWaitSeconds = 1.0e7;
        /// How long, in seconds of script time, a stack may wait for the host's completeLatent() (a latent native
        /// that returned a negative wait). Past it the stack wakes by timeout, as a timed wait would, and
        /// Stats::hostWaitsExpired counts it. Infinity (the default, the stage d behaviour): never. A host that can
        /// lose a completion should set a finite value, or such stacks fill maxSuspendedStacks for good. A value
        /// that is NaN or not above zero is replaced by the default with one warning.
        double maxHostWaitSeconds = std::numeric_limits<double>::infinity();
    };

    /// Counters of one runtime (task 0093 stage c). Deterministic: they depend only on the scripts and the calls.
    struct Stats
    {
        std::uint64_t instructions = 0;
        std::uint64_t stacksRun = 0;
        std::uint64_t stacksFinished = 0;
        std::uint64_t scriptCalls = 0;
        std::uint64_t nativeCalls = 0; ///< A registered handler ran.
        std::uint64_t unregisteredNativeCalls = 0; ///< Tolerant mode: a declared native with no handler, zero returned.
        std::uint64_t latentSkipped = 0; ///< Tolerant mode: a latent native, zero returned (stage d runs them).
        std::uint64_t noneReceiverCalls = 0; ///< Tolerant mode: a call on None, zero returned.
        std::uint64_t missingClassCalls = 0; ///< Tolerant mode: a call into a class that is not loaded, zero returned.
        std::uint64_t ignoredEvents = 0; ///< An On... function that the receiver does not define.
        std::uint64_t collections = 0; ///< Garbage collections of the VM heap.
        std::uint64_t collectionVisits = 0; ///< Strings and heap objects the collections looked at, added up.
        std::uint64_t readyQueueSlots = 0; ///< Slots the queue of waiting stacks holds after the last tick.
        std::uint64_t latentSuspended = 0; ///< Stacks parked by a latent native, counted at each park.
        std::uint64_t stacksWoken = 0; ///< Parked stacks resumed by tick() (a timer or completeLatent).
        std::uint64_t hostWaitsExpired = 0; ///< Stacks that waited for the host and woke by RuntimeOptions::maxHostWaitSeconds.
        std::uint64_t schedulerGrowths = 0; ///< Times the scheduler's lists had to get more room (geometric: few).
        std::uint64_t timeMovedBackwards = 0; ///< tick() calls whose time was below the last one (clamped to it).
        std::array<std::uint64_t, static_cast<std::size_t>(ErrorKind::Count)> errors{};
    };

    /// The Papyrus runtime of the common compatibility pack (task 0093): implements openeden.script.papyrus and adds
    /// what a game pack and the tests need but the contract does not carry (settings, counters, reading a variable).
    ///
    /// Ownership: the caller owns the PapyrusWorld and must keep it alive for as long as the runtime exists; the
    /// runtime owns everything else. Main thread only.
    class PapyrusRuntime : public openeden::script::PapyrusContract
    {
    public:
        virtual const Stats& stats() const noexcept = 0;

        /// A variable or property of an instance by name (a property reads its backing variable or runs nothing: a
        /// full property is not evaluated). The characters of a string stay valid until the next call into the
        /// runtime. An array or struct handle (here, in a CallResult, in a native's result) is valid the same way:
        /// until the next call into the runtime that can allocate; nothing roots it, so do not keep it. False when
        /// the instance or the name does not exist.
        virtual bool getVariable(openeden::script::InstanceId instance, std::string_view name,
            openeden::script::PapyrusValue& out) const noexcept = 0;

        /// Sets a variable or an auto property, as the editor's property values do. False when the instance or the
        /// name does not exist or the value cannot be stored in it.
        virtual bool setVariable(openeden::script::InstanceId instance, std::string_view name,
            const openeden::script::PapyrusValue& value) noexcept = 0;

        /// The current state of an instance ("" is the empty state); empty for an unknown instance.
        virtual std::string stateOf(openeden::script::InstanceId instance) const = 0;

        /// Every native call site that link() can resolve, as "class.function" (lower case, the class that declares
        /// the native) and a count, most called first, ties by name. Informational: the gated check uses it to name
        /// the natives a game pack would implement first.
        virtual std::vector<std::pair<std::string, std::uint64_t>> nativeCallSites() const = 0;

        // ---- the scheduler (task 0093 stage d; not in the contract header) -----------------------------------------

        /// Stacks parked on a latent call right now (timed or waiting for the host).
        virtual std::uint32_t suspendedStacks() const noexcept = 0;

        /// Inside a latent native's handler: the token of the stack that is calling it (never 0), which the handler can
        /// keep to pass to completeLatent. 0 anywhere else. A handler that returns a negative waitSeconds parks the
        /// stack until completeLatent(token) is called; it then resumes at the next tick.
        virtual std::uint64_t latentToken() const noexcept = 0;

        /// Wakes the parked stack of  token at the next tick(), whatever its timer says. False when no stack is
        /// parked with that token. Refused (false) from inside a native.
        virtual bool completeLatent(std::uint64_t token) noexcept = 0;

        /// Drops every parked stack of  instance (0: of all instances) without running it again: what a game pack
        /// does when it unloads the object the stacks belong to, and what restore will do first. Returns the number of
        /// stacks dropped. Does nothing inside a native.
        virtual std::uint32_t cancelWaiting(openeden::script::InstanceId instance) noexcept = 0;
    };

    std::unique_ptr<PapyrusRuntime> createRuntime(openeden::script::PapyrusWorld& world,
        openeden::script::UnknownFunctionPolicy policy = openeden::script::UnknownFunctionPolicy::Tolerant,
        const RuntimeOptions& options = {});
}

#endif
