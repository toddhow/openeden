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
// Stage A: scalar arguments and results, functions that only read. A handler is called by the core on the thread that
// runs the script (today the main thread), synchronously, and must not touch rendering state, take locks of other
// threads or allocate without bound. It reads the pack's own state; the pack writes that state during its own game
// phases, which never overlap a script update.

namespace openeden::sdk
{
    /// This header's own version; the SDK as a whole is versioned by kSdkVersion (openeden/version.hpp).
    ///
    /// Version policy. An additive change (a new virtual function at the END of a class, with a default; a new type; a
    /// new enumerator at the END of an enumeration) raises the minor number. A change to the layout or meaning of
    /// anything that exists (a member's type, a removed or reordered virtual function or member, a number's meaning)
    /// raises the major number. CI (CI/check_extension_sdk.sh) keeps this paragraph in place.
    inline constexpr Version kBindingsVersion{ 1, 0 };

    /// The kinds of value that cross a binding. Stage A: scalars only.
    enum class BindingType : std::uint8_t
    {
        Bool,
        Int32,
        Double,
        String, ///< UTF-8
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

    /// What a function may do. Stage A has one value; mutating calls come with stage B.
    enum class BindingAccess : std::uint8_t
    {
        Read,
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
    };

    struct NamespaceDescriptor
    {
        /// "<game id>.<area>", both parts [a-z][a-z0-9_]*, the game id being the session's (the pack's id). The core
        /// decides how scripts name it.
        std::string_view name;
        Version version{ 1, 0 }; ///< major at least 1
        std::span<const FunctionDescriptor> functions;
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
