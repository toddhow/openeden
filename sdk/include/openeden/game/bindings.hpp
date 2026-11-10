#pragma once

#include <cstddef>
#include <cstdint>
#include <span>
#include <string>
#include <string_view>

#include <openeden/version.hpp>

// Script bindings of a game (task 0097, stage A; decision 0014 points 4 and 5; docs/design/game-packs.md section 5.8).
// Game packs only, main thread only to declare.
//
// A game pack declares named, versioned namespaces of functions. The core turns each namespace into a read-only
// package of its script host; this header has no type of that host and no type of any third-party library, and the pack
// never learns which language the core binds it to.
//
// A function either reads or mutates (BindingAccess).
//  - A Read handler is called by the core on the thread that runs the script (today the main thread), synchronously,
//    and must not touch rendering state, take locks of other threads or allocate without bound. It reads the pack's own
//    state; the pack writes that state during its own game phases, which never overlap a script update. It may return
//    one value.
//  - A Mutate handler is never run inside the script's call. The core validates the arguments, copies them into a
//    fixed-capacity queue and runs the handler later on the MAIN thread, in call order, at one fixed point of the
//    frame (where the game applies its own delayed actions). It returns nothing to the script: a failed call is logged,
//    and the pack reports results by events. A call made while the queue is full is dropped and counted. Mutate
//    functions are visible to global scripts only.
// A value is a scalar (bool, int32, double, UTF-8 string), an opaque handle, an optional of those, or a flat array of
// them (at most kMaxBindingArrayElements elements).

namespace openeden::sdk
{
    /// This header's own version; the SDK as a whole is versioned by kSdkVersion (openeden/version.hpp).
    ///
    /// Version policy. An additive change (a new virtual function at the END of a class, with a default; a new type; a
    /// new enumerator at the END of an enumeration) raises the minor number. A change to the layout or meaning of
    /// anything that exists (a member's type, a removed or reordered virtual function or member, a number's meaning)
    /// raises the major number. CI (CI/check_extension_sdk.sh) keeps this paragraph in place.
    ///
    /// 2.0 (task 0097 stage B): Handle, BindingShape and Mutate were added, and members were appended to BindingValue,
    /// FunctionDescriptor and NamespaceDescriptor; appended struct members change the struct's size, so this is a major
    /// change (nothing had used 1.0).
    inline constexpr Version kBindingsVersion{ 2, 0 };

    /// The kinds of value that cross a binding.
    enum class BindingType : std::uint8_t
    {
        Bool,
        Int32,
        Double,
        String, ///< UTF-8
        /// An opaque 64-bit value the pack mints (a generation-checked entity or form id). In a script it is an object
        /// that compares equal by value, has no fields or methods and cannot be saved. Two equal handles are still two
        /// different table keys. The pack validates a handle each time it is used: a stale one is NotFound.
        Handle,
    };

    /// How many values a parameter or a result carries.
    enum class BindingShape : std::uint8_t
    {
        Single, ///< Exactly one value
        Optional, ///< One value or none (nil in a script); a trailing optional parameter may also be left out
        Array, ///< A flat array of the declared type (no nesting, no optionals inside)
    };

    /// A value crossing a binding, either way. Only the member named by `type` is meaningful.
    struct BindingValue
    {
        BindingType type = BindingType::Bool;
        bool boolean = false;
        std::int32_t integer = 0;
        double number = 0.0;
        /// UTF-8 text. For an argument it points into the caller's memory and is valid for the call only; for a result
        /// it must stay valid until the handler is called again (the core copies it at once).
        std::string_view text = {};
        // The members below were added in version 2.0.
        std::uint64_t handle = 0; ///< Meaningful for Handle
        /// False for an Optional that is absent; then nothing else is meaningful. Always true for Single and Array.
        bool present = true;
        /// For an Array: `count` elements of the declared element type (their `present` is true and they are not
        /// arrays). For an argument the memory is the core's and valid for the call; for a result it is the pack's and
        /// must stay valid until the handler is called again.
        const BindingValue* elements = nullptr;
        std::size_t count = 0;
    };

    /// What a handler answers with. Anything but Ok becomes a located error in the calling script.
    enum class BindingStatus : std::uint8_t
    {
        Ok,
        BadArgument, ///< An argument is valid for its type but not for the pack ("no such actor value")
        NotFound, ///< A named thing, or a handle, does not exist (any more)
        Refused, ///< The pack will not do it now
        Internal, ///< The pack failed; the core also uses it for an exception that escaped a handler
    };

    struct BindingResult
    {
        BindingStatus status = BindingStatus::Ok;
        /// Meaningful when status is Ok and the function declared a result; its type must equal the declared one.
        BindingValue value = {};
        /// A short text for a status other than Ok, valid until the handler is called again.
        std::string_view message = {};
    };

    /// What a function may do.
    enum class BindingAccess : std::uint8_t
    {
        Read,
        /// Queued and run later on the main thread; no result; visible to global scripts only.
        Mutate,
    };

    /// The pack's side of one function. Owned by the pack; the host stops calling it before the pack is destroyed.
    class BindingHandler
    {
    public:
        virtual ~BindingHandler() = default;

        /// The core has checked the number and types of `args` against the declaration. No exception may leave it. The
        /// function is deliberately not declared noexcept: an exception that leaks anyway is caught by the core, as a
        /// second line, and becomes Internal (a noexcept function would end the program instead).
        [[nodiscard]] virtual BindingResult call(std::span<const BindingValue> args) = 0;
    };

    inline constexpr std::size_t kMaxBindingParameters = 8;
    inline constexpr std::size_t kMaxBindingStringBytes = 64 * 1024;
    inline constexpr std::size_t kMaxBindingNamespaces = 64;
    inline constexpr std::size_t kMaxBindingFunctions = 256;
    inline constexpr std::size_t kMaxBindingArrayElements = 256;
    /// Mutate calls waiting for the main thread; a call beyond this is dropped and counted.
    inline constexpr std::size_t kMaxBindingQueuedCalls = 4096;

    struct FunctionDescriptor
    {
        std::string_view name; ///< [a-z][a-z0-9_]*
        std::span<const BindingType> params;
        bool hasResult = false;
        BindingType result = BindingType::Bool; ///< Meaningful when hasResult
        BindingAccess access = BindingAccess::Read;
        BindingHandler* handler = nullptr;
        /// Microseconds a call is expected to take at most. The core logs a call that takes longer; it never cuts one
        /// off. Zero means "not stated".
        std::uint32_t maxCostMicroseconds = 0;
        // The members below were added in version 2.0.
        /// One shape per parameter, or empty for "all Single".
        std::span<const BindingShape> paramShapes = {};
        BindingShape resultShape = BindingShape::Single; ///< Meaningful when hasResult; a Mutate function has none
    };

    struct NamespaceDescriptor
    {
        /// "<game id>.<area>", both parts [a-z][a-z0-9_]*, the game id being the session's (the pack's id). The core
        /// decides how scripts name it.
        std::string_view name;
        Version version{ 1, 0 }; ///< major at least 1
        std::span<const FunctionDescriptor> functions;
        /// Added in 2.0. True lets the menu (no game loaded) call this namespace's Read functions; the handlers then
        /// have to answer sensibly with no game state. Default false: only the game's scripts see the namespace.
        bool menuSafe = false;
    };

    struct DeclareResult
    {
        bool ok = false;
        std::string message; ///< Located, for a refused declaration
    };

    class BindingsService
    {
    public:
        virtual ~BindingsService() = default;

        /// One call per namespace, from the pack's start(). The core copies the descriptor; the handlers must outlive
        /// the host's release of them. A refused declaration is a pack failure: the pack should fail its start().
        [[nodiscard]] virtual DeclareResult declareNamespace(const NamespaceDescriptor& descriptor) = 0;
    };
} // namespace openeden::sdk
