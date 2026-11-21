#include "papyrusformnatives.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <string>
#include <vector>

namespace ScriptPapyrus
{
    using namespace openeden::script;

    namespace
    {
        constexpr double kDegreesPerRadian = 180.0 / 3.14159265358979323846;

        enum class Id : std::uint8_t
        {
            ActorGetLevel,
            ActorGetRace,
            ActorBaseGetRace,
            FormListGetSize,
            FormListGetAt,
            FormListHasForm,
            RefGetAngleX,
            RefGetAngleY,
            RefGetAngleZ,
            Cosmetic,
            CosmeticReturnsInt,
        };

        struct QueryEntry
        {
            const char* cls;
            const char* function;
            Id id;
        };
        // The class names are as the games declare them (case kept for registration; the name list is lower case).
        constexpr QueryEntry kQueries[] = {
            { "Actor", "GetLevel", Id::ActorGetLevel },
            { "Actor", "GetRace", Id::ActorGetRace },
            { "ActorBase", "GetRace", Id::ActorBaseGetRace },
            { "FormList", "GetAt", Id::FormListGetAt },
            { "FormList", "GetSize", Id::FormListGetSize },
            { "FormList", "HasForm", Id::FormListHasForm },
            { "ObjectReference", "GetAngleX", Id::RefGetAngleX },
            { "ObjectReference", "GetAngleY", Id::RefGetAngleY },
            { "ObjectReference", "GetAngleZ", Id::RefGetAngleZ },
        };

        struct CosmeticEntry
        {
            const char* cls;
            const char* function;
            bool global;
            bool returnsInt; ///< Sound.Play returns the playback instance id; 0 means none
        };
        // Cosmetic: no sound, shader, particle effect, camera shake or animation-graph variable exists to drive in a
        // neutral world. Whether each is a global or an instance function is the game's declaration; a wrong flag is
        // reported by registerAll as a failure and the gated survey asserts there are none.
        constexpr CosmeticEntry kCosmetic[] = {
            { "Actor", "PlaySubGraphAnimation", false, false },
            { "Actor", "SetAlpha", false, false },
            { "Actor", "SetSubGraphFloatVariable", false, false },
            { "EffectShader", "Play", false, false },
            { "EffectShader", "Stop", false, false },
            { "Game", "ShakeCamera", true, false },
            { "Game", "ShakeController", true, false },
            { "ObjectReference", "SetAnimationVariableFloat", false, false },
            { "Sound", "Play", false, true },
            { "Sound", "StopInstance", true, false },
            { "VisualEffect", "Play", false, false },
            { "VisualEffect", "Stop", false, false },
        };

        std::string lowerKey(const char* cls, const char* function)
        {
            std::string key = std::string(cls) + "." + function;
            for (char& c : key)
                c = static_cast<char>(c >= 'A' && c <= 'Z' ? c - 'A' + 'a' : c);
            return key;
        }

        const std::vector<std::string>& queryKeys()
        {
            static const std::vector<std::string> keys = [] {
                std::vector<std::string> out;
                for (const QueryEntry& e : kQueries)
                    out.push_back(lowerKey(e.cls, e.function));
                std::sort(out.begin(), out.end());
                return out;
            }();
            return keys;
        }
        const std::vector<std::string>& cosmeticKeys()
        {
            static const std::vector<std::string> keys = [] {
                std::vector<std::string> out;
                for (const CosmeticEntry& e : kCosmetic)
                    out.push_back(lowerKey(e.cls, e.function));
                std::sort(out.begin(), out.end());
                return out;
            }();
            return keys;
        }
        // string_view copies of the above, for the static accessors
        const std::vector<std::string_view>& views(const std::vector<std::string>& keys)
        {
            static std::vector<std::string_view> queryViews;
            static std::vector<std::string_view> cosmeticViews;
            std::vector<std::string_view>& target = &keys == &queryKeys() ? queryViews : cosmeticViews;
            if (target.empty())
                for (const std::string& k : keys)
                    target.emplace_back(k);
            return target;
        }
    }

    struct FormNatives::State
    {
        explicit State(PapyrusWorld& w)
            : mWorld(w)
            , mQueries(dynamic_cast<const PapyrusFormQueries*>(&w))
        {
        }
        PapyrusWorld& mWorld;
        const PapyrusFormQueries* mQueries; ///< null: the world cannot answer
        std::uint64_t mUnanswered = 0;
        bool mAlphaWarned = false; ///< Actor.SetAlpha does nothing; scripts use it to hide actors, so say so once
        std::string mClassScratch; ///< keeps the class name of a returned form alive until the next call
        std::vector<FormId> mEntries; ///< reused between calls: no allocation once warm

        PapyrusValue formValue(FormId form, const char* fallbackClass)
        {
            if (form == 0)
                return PapyrusValue::makeNone();
            if (!mWorld.formClass(form, mClassScratch))
                mClassScratch = fallbackClass;
            return PapyrusValue::makeForm(mClassScratch, form);
        }
        FormId formOf(const PapyrusValue& value) const { return mQueries != nullptr ? mQueries->formIdOf(value) : 0; }
    };

    namespace
    {
        NativeHandler makeQueryHandler(const std::shared_ptr<FormNatives::State>& state, Id id)
        {
            switch (id)
            {
                case Id::ActorGetLevel:
                    return [state](const NativeCall& call) {
                        std::int32_t level = 0;
                        if (!state->mQueries->actorLevel(state->formOf(call.self), level))
                        {
                            ++state->mUnanswered;
                            level = 0;
                        }
                        return NativeResult{ PapyrusValue::makeInt(level) };
                    };
                case Id::ActorGetRace:
                case Id::ActorBaseGetRace:
                    return [state](const NativeCall& call) {
                        FormId race = 0;
                        if (!state->mQueries->raceOf(state->formOf(call.self), race))
                        {
                            ++state->mUnanswered;
                            return NativeResult{};
                        }
                        return NativeResult{ state->formValue(race, "Race") };
                    };
                case Id::FormListGetSize:
                    return [state](const NativeCall& call) {
                        state->mEntries.clear();
                        if (!state->mQueries->formListEntries(state->formOf(call.self), state->mEntries))
                        {
                            ++state->mUnanswered;
                            state->mEntries.clear();
                        }
                        return NativeResult{ PapyrusValue::makeInt(static_cast<std::int32_t>(state->mEntries.size())) };
                    };
                case Id::FormListGetAt:
                    return [state](const NativeCall& call) {
                        state->mEntries.clear();
                        if (!state->mQueries->formListEntries(state->formOf(call.self), state->mEntries))
                        {
                            ++state->mUnanswered;
                            return NativeResult{};
                        }
                        const std::int32_t index = call.arguments.empty() ? -1 : call.arguments[0].integer;
                        if (index < 0 || static_cast<std::size_t>(index) >= state->mEntries.size())
                            return NativeResult{};
                        return NativeResult{ state->formValue(
                            state->mEntries[static_cast<std::size_t>(index)], "Form") };
                    };
                case Id::FormListHasForm:
                    return [state](const NativeCall& call) {
                        const FormId wanted = call.arguments.empty() ? 0 : state->formOf(call.arguments[0]);
                        state->mEntries.clear();
                        if (!state->mQueries->formListEntries(state->formOf(call.self), state->mEntries))
                        {
                            ++state->mUnanswered;
                            return NativeResult{ PapyrusValue::makeBool(false) };
                        }
                        const bool found = wanted != 0
                            && std::find(state->mEntries.begin(), state->mEntries.end(), wanted)
                                != state->mEntries.end();
                        return NativeResult{ PapyrusValue::makeBool(found) };
                    };
                case Id::RefGetAngleX:
                case Id::RefGetAngleY:
                case Id::RefGetAngleZ:
                {
                    const std::size_t axis = static_cast<std::size_t>(id) - static_cast<std::size_t>(Id::RefGetAngleX);
                    return [state, axis](const NativeCall& call) {
                        double xyz[3] = { 0.0, 0.0, 0.0 };
                        if (!state->mQueries->referenceRotation(state->formOf(call.self), xyz))
                        {
                            ++state->mUnanswered;
                            return NativeResult{ PapyrusValue::makeFloat(0.0f) };
                        }
                        const double degrees = xyz[axis] * kDegreesPerRadian;
                        return NativeResult{ PapyrusValue::makeFloat(
                            std::isfinite(degrees) ? static_cast<float>(degrees) : 0.0f) };
                    };
                }
                default:
                    break;
            }
            return {};
        }
    }

    FormNatives::FormNatives(PapyrusWorld& world)
        : mState(std::make_shared<State>(world))
    {
    }

    FormNatives::~FormNatives() = default;

    std::span<const std::string_view> FormNatives::queryNames() noexcept
    {
        return views(queryKeys());
    }

    std::span<const std::string_view> FormNatives::cosmeticNames() noexcept
    {
        return views(cosmeticKeys());
    }

    bool FormNatives::queriesMissing() const noexcept
    {
        return mState->mQueries == nullptr;
    }

    std::uint64_t FormNatives::unanswered() const noexcept
    {
        return mState->mUnanswered;
    }

    PureNativesReport FormNatives::registerAll(PapyrusContract& contract)
    {
        PureNativesReport report;
        auto bind = [&](const char* cls, const char* function, bool global, NativeHandler handler) {
            NativeDescriptor descriptor;
            descriptor.className = cls;
            descriptor.functionName = function;
            descriptor.global = global;
            ErrorReport error;
            if (contract.registerNative(descriptor, std::move(handler), error))
            {
                ++report.registered;
                return;
            }
            if (error.message.find("has no function") != std::string::npos)
            {
                ++report.notDeclared;
                return;
            }
            report.failures.push_back(lowerKey(cls, function) + ": " + error.message);
        };
        if (mState->mQueries != nullptr)
            for (const QueryEntry& e : kQueries)
                bind(e.cls, e.function, false, makeQueryHandler(mState, e.id));
        for (const CosmeticEntry& e : kCosmetic)
        {
            if (e.returnsInt)
                bind(e.cls, e.function, e.global,
                    [](const NativeCall&) { return NativeResult{ PapyrusValue::makeInt(0) }; });
            else if (std::string_view(e.function) == "SetAlpha")
                bind(e.cls, e.function, e.global, [state = mState](const NativeCall&) {
                    if (!state->mAlphaWarned)
                    {
                        state->mAlphaWarned = true;
                        state->mWorld.log(openeden::script::Severity::Warning,
                            "Papyrus: Actor.SetAlpha is not drawn here (an actor a script fades or hides stays "
                            "visible)");
                    }
                    return NativeResult{};
                });
            else
                bind(e.cls, e.function, e.global, [](const NativeCall&) { return NativeResult{}; });
        }
        return report;
    }
}
