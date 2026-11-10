#ifndef OPENEDEN_COMPONENTS_SCRIPT_PAPYRUS_PAPYRUSNATIVES_H
#define OPENEDEN_COMPONENTS_SCRIPT_PAPYRUS_PAPYRUSNATIVES_H

// Task 0093, native support: the "VM-pure" natives.
//
// In plain words: a Papyrus script calls two kinds of native functions. A WORLD native needs the game (a quest, a
// reference, a faction, a sound): only a game pack can answer it, so it stays behind the PapyrusWorld contract and,
// until a game pack registers it, is "declared and not registered" (tolerant mode logs it once and returns the zero of
// its type). A VM-PURE native needs nothing from the game: square roots, a random number from a seed the host chose, a
// wait of so many seconds of script time, a line for the log. Those can be answered by the runtime's own library, once,
// for every game, and that is this file. Nothing here reads a clock, a file or the game.
//
// What is in the set (22 natives; the names are facts from the installed Skyrim SE and Fallout 4 class files, the
// semantics are noted per function and are RECALLED-VERIFY unless the data decides them):
//   Math     Abs Floor Ceiling Sqrt Pow Sin Cos Tan Asin Acos Atan DegreesToRadians RadiansToDegrees
//   Utility  RandomInt RandomFloat Wait WaitMenuMode
//   Debug    Trace TraceStack TraceFunction Notification MessageBox
// What is NOT in it, and why (all stay world natives): Utility.GetCurrentRealTime (a clock), GetCurrentGameTime,
// WaitGameTime and IsInMenuMode (the game's time and menus), Debug.TraceUser and OpenUserLog (files), everything that
// names a form, a quest, an actor or a reference, and the SKSE and library classes (StringUtil, PapyrusUtil, ...),
// whose signatures the data does not give until their own ".pex" is installed.
//
// Threading: every handler runs on the thread that called into the runtime (the main thread), like all natives.
// Ownership: a PureNatives owns its state; the handlers it registers share that state (std::shared_ptr, because the
// std::function the runtime stores must stay valid however the runtime and this object are destroyed). The world given
// to the constructor is NOT owned and must outlive every call into the runtime that can reach a handler.

#include <cstddef>
#include <cstdint>
#include <memory>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include <openeden/script/papyrus.hpp>

namespace ScriptPapyrus
{
    /// The pure functions behind the Math natives, exposed so that they can be tested without a VM. They compute in
    /// double precision and round to float once, so the answer does not depend on the C library's float versions.
    namespace PureMath
    {
        /// Papyrus's trigonometric natives work in DEGREES (RECALLED-VERIFY: Creation Kit wiki, Math script).
        inline constexpr double kPi = 3.14159265358979323846;
        inline constexpr double kDegreesPerRadian = 180.0 / kPi;

        /// Result of a function that can leave its domain.
        struct Float
        {
            float value = 0.0f;
            bool domainError = false; ///< The mathematically undefined case (a NaN would have come out).
        };

        /// A non-finite double becomes a float: NaN is a domain error and gives 0; an infinity (an overflow) is
        /// clamped to the largest finite float of its sign and is also reported as a domain error.
        Float toFloat(double value) noexcept;
        /// Floor and ceiling as the Int the natives return: clamped to the int32 range, NaN gives 0.
        std::int32_t toInt(double value) noexcept;

        Float abs(float value) noexcept; ///< NaN: domain error, 0; an infinity becomes the largest float.
        Float sqrt(float value) noexcept; ///< Below 0: domain error, 0.
        Float pow(float base, float exponent) noexcept; ///< NaN result: domain error, 0.
        Float sin(float degrees) noexcept;
        Float cos(float degrees) noexcept;
        Float tan(float degrees) noexcept;
        Float asin(float value) noexcept; ///< Outside [-1, 1]: domain error, 0. Result in degrees.
        Float acos(float value) noexcept; ///< Outside [-1, 1]: domain error, 0. Result in degrees.
        Float atan(float value) noexcept; ///< Result in degrees.
        Float degreesToRadians(float degrees) noexcept;
        Float radiansToDegrees(float radians) noexcept;
    }

    /// The random number generator of Utility.RandomInt and RandomFloat: SplitMix64, one 64-bit state, so a game pack
    /// can store and restore it. Not a secret and not for anything but gameplay variety.
    class PureRandom
    {
    public:
        explicit PureRandom(std::uint64_t seed) noexcept
            : mState(seed)
        {
        }
        std::uint64_t next() noexcept;
        /// Uniform over [min, max], both included; the bounds may come in either order. No modulo bias.
        std::int32_t intBetween(std::int32_t min, std::int32_t max) noexcept;
        /// Uniform over [min, max) (min itself when they are equal); the bounds may come in either order. Not finite
        /// bounds are the caller's to refuse.
        float floatBetween(float min, float max) noexcept;
        std::uint64_t state() const noexcept { return mState; }
        void setState(std::uint64_t state) noexcept { mState = state; }

    private:
        std::uint64_t mState;
    };

    struct PureNativesOptions
    {
        /// The seed of the generator. A fixed default: two runs of the same scripts give the same numbers (the
        /// project's determinism rule); a game pack that wants variety chooses a seed itself.
        std::uint64_t randomSeed = 0x9E3779B97F4A7C15ull;
    };

    struct PureNativesReport
    {
        std::size_t registered = 0;
        /// The class is loaded and does not declare that function (a game that lacks it): skipped, not a failure.
        std::size_t notDeclared = 0;
        /// Anything else the runtime refused ("class.function: reason"): already registered, declared with a
        /// different global flag.
        std::vector<std::string> failures;
        bool ok() const noexcept { return failures.empty(); }
    };

    class PureNatives
    {
    public:
        /// \a world receives the text of Debug.Trace and its relatives (PapyrusWorld::report) and one warning, the
        /// first time each function meets an undefined input (PapyrusWorld::log).
        explicit PureNatives(openeden::script::PapyrusWorld& world, const PureNativesOptions& options = {});
        ~PureNatives();
        PureNatives(const PureNatives&) = delete;
        PureNatives& operator=(const PureNatives&) = delete;

        /// Registers every native of the set that the loaded classes declare (and all of them for a class that is
        /// not loaded yet). Call it after loading the scripts, so that the declarations are checked. Safe to call
        /// when a game pack has registered some of the same natives first: those are reported in failures and the
        /// game pack's handler stays.
        PureNativesReport registerAll(openeden::script::PapyrusContract& contract);

        /// The set, as "class.function" in lower case, sorted. The gated check uses it to count call sites.
        static std::span<const std::string_view> names() noexcept;

        std::uint64_t randomState() const noexcept;
        void setRandomState(std::uint64_t state) noexcept;
        /// Calls that met an undefined input since construction.
        std::uint64_t domainErrors() const noexcept;

        /// The shared state the handlers keep (an implementation detail; public only so the .cpp can name it).
        struct State;

    private:
        std::shared_ptr<State> mState;
    };
}

#endif
