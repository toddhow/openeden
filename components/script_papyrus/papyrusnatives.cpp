// Task 0093, native support: the VM-pure natives. See papyrusnatives.hpp for what is in the set and what is not.

#include "papyrusnatives.hpp"

#include <array>
#include <cmath>
#include <limits>
#include <utility>

namespace ScriptPapyrus
{
    namespace PureMath
    {
        namespace
        {
            constexpr double kMaxFloat = static_cast<double>(std::numeric_limits<float>::max());

            // The argument is a float the VM already holds, so a NaN or infinity is the script's own doing.
            Float finiteOr(float input, double value) noexcept
            {
                if (!std::isfinite(input))
                {
                    return Float{ 0.0f, true };
                }
                return toFloat(value);
            }
        }

        Float toFloat(double value) noexcept
        {
            Float result;
            if (std::isnan(value))
            {
                result.domainError = true;
            }
            else if (value > kMaxFloat)
            {
                result.value = std::numeric_limits<float>::max();
                result.domainError = true;
            }
            else if (value < -kMaxFloat)
            {
                result.value = -std::numeric_limits<float>::max();
                result.domainError = true;
            }
            else
            {
                result.value = static_cast<float>(value);
            }
            return result;
        }

        std::int32_t toInt(double value) noexcept
        {
            if (std::isnan(value))
            {
                return 0;
            }
            constexpr double lo = static_cast<double>(std::numeric_limits<std::int32_t>::min());
            constexpr double hi = static_cast<double>(std::numeric_limits<std::int32_t>::max());
            if (value <= lo)
            {
                return std::numeric_limits<std::int32_t>::min();
            }
            if (value >= hi)
            {
                return std::numeric_limits<std::int32_t>::max();
            }
            return static_cast<std::int32_t>(value);
        }

        Float abs(float value) noexcept
        {
            return toFloat(std::fabs(static_cast<double>(value)));
        }

        Float sqrt(float value) noexcept
        {
            if (std::isnan(value) || value < 0.0f)
            {
                return Float{ 0.0f, true };
            }
            return toFloat(std::sqrt(static_cast<double>(value)));
        }

        Float pow(float base, float exponent) noexcept
        {
            return toFloat(std::pow(static_cast<double>(base), static_cast<double>(exponent)));
        }

        Float sin(float degrees) noexcept
        {
            return finiteOr(degrees, std::sin(static_cast<double>(degrees) / kDegreesPerRadian));
        }

        Float cos(float degrees) noexcept
        {
            return finiteOr(degrees, std::cos(static_cast<double>(degrees) / kDegreesPerRadian));
        }

        Float tan(float degrees) noexcept
        {
            return finiteOr(degrees, std::tan(static_cast<double>(degrees) / kDegreesPerRadian));
        }

        Float asin(float value) noexcept
        {
            if (!(value >= -1.0f && value <= 1.0f))
            {
                return Float{ 0.0f, true };
            }
            return toFloat(std::asin(static_cast<double>(value)) * kDegreesPerRadian);
        }

        Float acos(float value) noexcept
        {
            if (!(value >= -1.0f && value <= 1.0f))
            {
                return Float{ 0.0f, true };
            }
            return toFloat(std::acos(static_cast<double>(value)) * kDegreesPerRadian);
        }

        Float atan(float value) noexcept
        {
            return finiteOr(value, std::atan(static_cast<double>(value)) * kDegreesPerRadian);
        }

        Float degreesToRadians(float degrees) noexcept
        {
            return finiteOr(degrees, static_cast<double>(degrees) / kDegreesPerRadian);
        }

        Float radiansToDegrees(float radians) noexcept
        {
            return finiteOr(radians, static_cast<double>(radians) * kDegreesPerRadian);
        }
    }

    // ---- the generator -------------------------------------------------------------------------------------------

    std::uint64_t PureRandom::next() noexcept
    {
        // SplitMix64 (Steele, Lea, Flood 2014): a counter run through a mixing function.
        constexpr std::uint64_t kStep = 0x9E3779B97F4A7C15ull;
        constexpr std::uint64_t kMixA = 0xBF58476D1CE4E5B9ull;
        constexpr std::uint64_t kMixB = 0x94D049BB133111EBull;
        mState += kStep;
        std::uint64_t z = mState;
        z = (z ^ (z >> 30)) * kMixA;
        z = (z ^ (z >> 27)) * kMixB;
        return z ^ (z >> 31);
    }

    std::int32_t PureRandom::intBetween(std::int32_t min, std::int32_t max) noexcept
    {
        if (min > max)
        {
            std::swap(min, max);
        }
        const std::uint64_t range = static_cast<std::uint64_t>(static_cast<std::int64_t>(max) - min) + 1u;
        // Reject the few draws that would make some values likelier: 2^64 - threshold is a multiple of range.
        const std::uint64_t threshold = (0u - range) % range;
        std::uint64_t draw = next();
        while (draw < threshold)
        {
            draw = next();
        }
        return static_cast<std::int32_t>(static_cast<std::int64_t>(min) + static_cast<std::int64_t>(draw % range));
    }

    float PureRandom::floatBetween(float min, float max) noexcept
    {
        if (min > max)
        {
            std::swap(min, max);
        }
        if (min == max)
        {
            return min;
        }
        constexpr int kMantissaDrawBits = 24; // a float holds 24 bits: the draw is exact
        constexpr double kScale = 1.0 / static_cast<double>(1u << kMantissaDrawBits);
        const double unit = static_cast<double>(next() >> (64 - kMantissaDrawBits)) * kScale;
        float result = static_cast<float>(static_cast<double>(min) + unit * (static_cast<double>(max) - min));
        if (result >= max)
        {
            result = std::nextafter(max, min); // rounding may not reach the excluded end
        }
        return result;
    }

    // ---- the set -------------------------------------------------------------------------------------------------

    namespace
    {
        using namespace openeden::script;

        enum class Id : std::uint8_t
        {
            Abs,
            Floor,
            Ceiling,
            Sqrt,
            Pow,
            Sin,
            Cos,
            Tan,
            Asin,
            Acos,
            Atan,
            DegreesToRadians,
            RadiansToDegrees,
            RandomInt,
            RandomFloat,
            Wait,
            WaitMenuMode,
            Trace,
            TraceStack,
            TraceFunction,
            Notification,
            MessageBox,
            Count,
        };

        struct Entry
        {
            Id id;
            const char* cls;
            const char* function;
            bool latent;
        };

        constexpr std::array<Entry, static_cast<std::size_t>(Id::Count)> kEntries = { {
            { Id::Abs, "Math", "Abs", false },
            { Id::Floor, "Math", "Floor", false },
            { Id::Ceiling, "Math", "Ceiling", false },
            { Id::Sqrt, "Math", "Sqrt", false },
            { Id::Pow, "Math", "Pow", false },
            { Id::Sin, "Math", "Sin", false },
            { Id::Cos, "Math", "Cos", false },
            { Id::Tan, "Math", "Tan", false },
            { Id::Asin, "Math", "Asin", false },
            { Id::Acos, "Math", "Acos", false },
            { Id::Atan, "Math", "Atan", false },
            { Id::DegreesToRadians, "Math", "DegreesToRadians", false },
            { Id::RadiansToDegrees, "Math", "RadiansToDegrees", false },
            { Id::RandomInt, "Utility", "RandomInt", false },
            { Id::RandomFloat, "Utility", "RandomFloat", false },
            { Id::Wait, "Utility", "Wait", true },
            { Id::WaitMenuMode, "Utility", "WaitMenuMode", true },
            { Id::Trace, "Debug", "Trace", false },
            { Id::TraceStack, "Debug", "TraceStack", false },
            { Id::TraceFunction, "Debug", "TraceFunction", false },
            { Id::Notification, "Debug", "Notification", false },
            { Id::MessageBox, "Debug", "MessageBox", false },
        } };

        // Sorted, lower case. A test pins that the set has these 22 names.
        constexpr std::array<std::string_view, 22> kNames
            = { "debug.messagebox", "debug.notification", "debug.trace", "debug.tracefunction", "debug.tracestack",
                  "math.abs", "math.acos", "math.asin", "math.atan", "math.ceiling", "math.cos",
                  "math.degreestoradians", "math.floor", "math.pow", "math.radianstodegrees", "math.sin", "math.sqrt",
                  "math.tan", "utility.randomfloat", "utility.randomint", "utility.wait", "utility.waitmenumode" };
        static_assert(kNames.size() == kEntries.size());

        float floatArgument(const NativeCall& call, std::size_t index) noexcept
        {
            if (index >= call.arguments.size())
            {
                return 0.0f;
            }
            const PapyrusValue& value = call.arguments[index];
            if (value.kind == PapyrusKind::Float)
            {
                return value.number;
            }
            return value.kind == PapyrusKind::Int ? static_cast<float>(value.integer) : 0.0f;
        }

        std::int32_t intArgument(const NativeCall& call, std::size_t index) noexcept
        {
            return index < call.arguments.size() && call.arguments[index].kind == PapyrusKind::Int
                ? call.arguments[index].integer
                : 0;
        }

        std::string_view textArgument(const NativeCall& call, std::size_t index) noexcept
        {
            return index < call.arguments.size() && call.arguments[index].kind == PapyrusKind::String
                ? call.arguments[index].text
                : std::string_view();
        }
    }

    struct PureNatives::State
    {
        State(PapyrusWorld& world, std::uint64_t seed)
            : mWorld(world)
            , mRandom(seed)
        {
        }

        NativeResult floatResult(Id id, const PureMath::Float& answer)
        {
            if (answer.domainError)
            {
                noteDomain(id);
            }
            return NativeResult{ PapyrusValue::makeFloat(answer.value) };
        }

        void noteDomain(Id id)
        {
            ++mDomainErrors;
            bool& warned = mWarned[static_cast<std::size_t>(id)];
            if (!warned)
            {
                warned = true;
                const Entry& entry = kEntries[static_cast<std::size_t>(id)];
                mWorld.log(Severity::Warning,
                    std::string(entry.cls) + "." + entry.function
                        + ": an input outside the function's domain; it returns 0 (or the largest float), and this "
                          "warning is not repeated");
            }
        }

        void report(std::string_view prefix, std::string_view text)
        {
            // One buffer for the life of the object, so a warm call allocates nothing.
            mBuffer.assign(prefix);
            mBuffer.append(text);
            mWorld.report(mBuffer);
        }

        PapyrusWorld& mWorld; // not owned
        PureRandom mRandom;
        std::uint64_t mDomainErrors = 0;
        std::array<bool, static_cast<std::size_t>(Id::Count)> mWarned = {};
        std::string mBuffer;
    };

    namespace
    {
        using StatePtr = std::shared_ptr<PureNatives::State>;

        template <typename Fn>
        NativeHandler unary(const StatePtr& state, Id id, Fn fn)
        {
            return
                [state, id, fn](const NativeCall& call) { return state->floatResult(id, fn(floatArgument(call, 0))); };
        }

        NativeHandler makeHandler(const StatePtr& state, Id id)
        {
            switch (id)
            {
                case Id::Abs:
                    return unary(state, id, [](float v) { return PureMath::abs(v); });
                case Id::Floor:
                    return [](const NativeCall& call) {
                        return NativeResult{ PapyrusValue::makeInt(
                            PureMath::toInt(std::floor(static_cast<double>(floatArgument(call, 0))))) };
                    };
                case Id::Ceiling:
                    return [](const NativeCall& call) {
                        return NativeResult{ PapyrusValue::makeInt(
                            PureMath::toInt(std::ceil(static_cast<double>(floatArgument(call, 0))))) };
                    };
                case Id::Sqrt:
                    return unary(state, id, [](float v) { return PureMath::sqrt(v); });
                case Id::Pow:
                    return [state, id](const NativeCall& call) {
                        return state->floatResult(id, PureMath::pow(floatArgument(call, 0), floatArgument(call, 1)));
                    };
                case Id::Sin:
                    return unary(state, id, [](float v) { return PureMath::sin(v); });
                case Id::Cos:
                    return unary(state, id, [](float v) { return PureMath::cos(v); });
                case Id::Tan:
                    return unary(state, id, [](float v) { return PureMath::tan(v); });
                case Id::Asin:
                    return unary(state, id, [](float v) { return PureMath::asin(v); });
                case Id::Acos:
                    return unary(state, id, [](float v) { return PureMath::acos(v); });
                case Id::Atan:
                    return unary(state, id, [](float v) { return PureMath::atan(v); });
                case Id::DegreesToRadians:
                    return unary(state, id, [](float v) { return PureMath::degreesToRadians(v); });
                case Id::RadiansToDegrees:
                    return unary(state, id, [](float v) { return PureMath::radiansToDegrees(v); });
                case Id::RandomInt:
                    return [state](const NativeCall& call) {
                        return NativeResult{ PapyrusValue::makeInt(
                            state->mRandom.intBetween(intArgument(call, 0), intArgument(call, 1))) };
                    };
                case Id::RandomFloat:
                    return [state, id](const NativeCall& call) {
                        const float lo = floatArgument(call, 0);
                        const float hi = floatArgument(call, 1);
                        if (!std::isfinite(lo) || !std::isfinite(hi))
                        {
                            state->noteDomain(id);
                            return NativeResult{ PapyrusValue::makeFloat(0.0f) };
                        }
                        return NativeResult{ PapyrusValue::makeFloat(state->mRandom.floatBetween(lo, hi)) };
                    };
                case Id::Wait:
                case Id::WaitMenuMode:
                    return [](const NativeCall& call) {
                        // A negative wait would mean "until the host completes it" to the scheduler: never pass one.
                        const float seconds = floatArgument(call, 0);
                        NativeResult result;
                        result.waitSeconds = seconds > 0.0f ? static_cast<double>(seconds) : 0.0;
                        return result;
                    };
                case Id::Trace:
                case Id::TraceStack:
                case Id::TraceFunction:
                    return [state](const NativeCall& call) {
                        state->report({}, textArgument(call, 0));
                        return NativeResult{};
                    };
                case Id::Notification:
                    return [state](const NativeCall& call) {
                        state->report("[Notification] ", textArgument(call, 0));
                        return NativeResult{};
                    };
                case Id::MessageBox:
                    return [state](const NativeCall& call) {
                        state->report("[MessageBox] ", textArgument(call, 0));
                        return NativeResult{};
                    };
                case Id::Count:
                    break;
            }
            return {};
        }
    }

    PureNatives::PureNatives(PapyrusWorld& world, const PureNativesOptions& options)
        : mState(std::make_shared<State>(world, options.randomSeed))
    {
    }

    PureNatives::~PureNatives() = default;

    std::span<const std::string_view> PureNatives::names() noexcept
    {
        return kNames;
    }

    std::uint64_t PureNatives::randomState() const noexcept
    {
        return mState->mRandom.state();
    }

    void PureNatives::setRandomState(std::uint64_t state) noexcept
    {
        mState->mRandom.setState(state);
    }

    std::uint64_t PureNatives::domainErrors() const noexcept
    {
        return mState->mDomainErrors;
    }

    PureNativesReport PureNatives::registerAll(PapyrusContract& contract)
    {
        PureNativesReport report;
        for (const Entry& entry : kEntries)
        {
            NativeDescriptor descriptor;
            descriptor.className = entry.cls;
            descriptor.functionName = entry.function;
            descriptor.global = true;
            descriptor.latent = entry.latent;
            ErrorReport error;
            if (contract.registerNative(descriptor, makeHandler(mState, entry.id), error))
            {
                ++report.registered;
                continue;
            }
            // The runtime says "has no function" when the loaded class does not declare the name (see
            // PapyrusRuntime::registerNative); a test pins the wording.
            if (error.message.find("has no function") != std::string::npos)
            {
                ++report.notDeclared;
                continue;
            }
            std::string key = std::string(entry.cls) + "." + entry.function;
            for (char& c : key)
            {
                c = static_cast<char>(c >= 'A' && c <= 'Z' ? c - 'A' + 'a' : c);
            }
            report.failures.push_back(key + ": " + error.message);
        }
        return report;
    }
}
