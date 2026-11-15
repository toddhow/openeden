#include "game_skyrimse/papyrus_host.hpp"

#include <components/script_papyrus/papyrusnatives.hpp>
#include <components/script_papyrus/pexfile.hpp>

#include <algorithm>
#include <array>
#include <cstdio>

namespace game_skyrimse
{
    using openeden::script::AttachKey;
    using openeden::script::AttachKind;
    using openeden::script::InstanceId;
    using openeden::script::NativeCall;
    using openeden::script::NativeDescriptor;
    using openeden::script::NativeResult;
    using openeden::script::PapyrusKind;
    using openeden::script::PapyrusValue;

    namespace
    {
        constexpr FormId kPlayerReference = 0x14;
        constexpr std::uint32_t kAliasTag = 0xFF000000u; // a handle of this host's alias table, not a form id

        std::string lower(std::string_view s)
        {
            std::string out(s);
            for (char& c : out)
                c = static_cast<char>(c >= 'A' && c <= 'Z' ? c + 32 : c);
            return out;
        }

        std::string hex(std::uint32_t v)
        {
            char buffer[16];
            std::snprintf(buffer, sizeof(buffer), "%08X", v);
            return buffer;
        }

        // A name worth looking up as a script file: what an identifier in a string table looks like.
        bool plausibleScriptName(const std::string& s)
        {
            if (s.empty() || s.size() > 96)
                return false;
            for (const char c : s)
            {
                const bool ok = (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '_';
                if (!ok)
                    return false;
            }
            return s[0] != '_' || s.size() > 1;
        }
    }

    struct PapyrusHost::Impl final : public openeden::script::PapyrusWorld
    {
        struct InstanceInfo
        {
            FormId mForm = 0; ///< a quest or a placed reference; for an alias script the quest
            std::uint32_t mAlias = 0; ///< the alias id of an alias script, else 0
            bool mIsAlias = false;
            std::string mScript; ///< lower case
        };

        explicit Impl(PapyrusGameData& data, PapyrusHostOptions options)
            : mData(data)
            , mOptions(options)
        {
            ScriptPapyrus::RuntimeOptions runtimeOptions;
            runtimeOptions.runLatentNatives = true;
            runtimeOptions.maxInstances = options.mMaxInstances;
            mRuntime = ScriptPapyrus::createRuntime(
                *this, openeden::script::UnknownFunctionPolicy::Tolerant, runtimeOptions);
            ScriptPapyrus::PureNativesOptions pure;
            pure.randomSeed = options.mRandomSeed;
            mPure = std::make_unique<ScriptPapyrus::PureNatives>(*this, pure);
            (void)mPure->registerAll(*mRuntime);
            registerWorldNatives();
        }

        // ---- PapyrusWorld ------------------------------------------------------------------------------------------

        bool formClass(openeden::script::FormId form, std::string& className) const override
        {
            if ((form & 0xFF000000u) == kAliasTag && form != 0)
            {
                className = "ReferenceAlias";
                return true;
            }
            const auto ref = mRefClass.find(form);
            if (ref != mRefClass.end())
            {
                className = ref->second;
                return true;
            }
            if (form == kPlayerReference)
            {
                className = "Actor";
                return true;
            }
            className = mData.formClass(form);
            return !className.empty();
        }

        void report(std::string_view message) override { say(false, "Papyrus: " + std::string(message)); }

        void log(openeden::script::Severity severity, std::string_view message) override
        {
            const std::string text(message);
            const std::size_t at = text.find("' is declared and not registered");
            if (text.rfind("native '", 0) == 0 && at != std::string::npos)
                mTolerant.insert(lower(text.substr(8, at - 8)));
            say(severity == openeden::script::Severity::Error, text);
        }

        void say(bool error, const std::string& line)
        {
            if (mLog)
                mLog(error, line);
        }

        // ---- names and handles -------------------------------------------------------------------------------------

        const std::string& intern(const std::string& name) { return *mNames.insert(name).first; }

        std::uint32_t aliasHandle(FormId quest, std::uint32_t alias)
        {
            const auto key = std::make_pair(quest, alias);
            const auto it = mAliasByKey.find(key);
            if (it != mAliasByKey.end())
                return it->second;
            mAliasKeys.push_back(key);
            const std::uint32_t handle = kAliasTag | static_cast<std::uint32_t>(mAliasKeys.size());
            mAliasByKey.emplace(key, handle);
            return handle;
        }

        bool aliasOf(std::uint32_t handle, FormId& quest, std::uint32_t& alias) const
        {
            if ((handle & 0xFF000000u) != kAliasTag)
                return false;
            const std::uint32_t index = handle & 0x00FFFFFFu;
            if (index == 0 || index > mAliasKeys.size())
                return false;
            quest = mAliasKeys[index - 1].first;
            alias = mAliasKeys[index - 1].second;
            return true;
        }

        // The form (or alias handle) a native's self or argument stands for; 0 for None.
        std::uint32_t formOf(const PapyrusValue& v) const
        {
            if (v.kind != PapyrusKind::Object)
                return 0;
            if (!v.attached)
                return v.handle;
            const auto it = mInstances.find(v.handle);
            if (it == mInstances.end())
                return 0;
            return it->second.mIsAlias ? const_cast<Impl*>(this)->aliasHandle(it->second.mForm, it->second.mAlias)
                                       : it->second.mForm;
        }

        PapyrusValue formValue(FormId form)
        {
            if (form == 0)
                return PapyrusValue::makeNone();
            std::string cls;
            if (!formClass(form, cls))
                cls = "Form";
            return PapyrusValue::makeForm(intern(cls), form);
        }

        // ---- loading -----------------------------------------------------------------------------------------------

        // Loads scripts/<name>.pex, its parent, and every class its string table names that has a file. `counted`:
        // a record asked for this script, so a missing file is counted.
        bool load(const std::string& name, bool counted)
        {
            if (mLoaded.count(name) != 0)
                return true;
            if (mNotFile.count(name) != 0)
                return false;
            if (mLoaded.size() >= mOptions.mMaxLoadedScripts)
                return false;
            std::vector<std::uint8_t> bytes;
            if (!mData.readPex(name, bytes))
            {
                mNotFile.insert(name);
                if (counted)
                {
                    ++mCounters.mScriptsMissing;
                    say(false, "Papyrus: the script '" + name + "' has no .pex file");
                }
                return false;
            }
            const openeden::script::LoadResult result = mRuntime->loadScript(bytes, name);
            if (!result.ok)
            {
                mNotFile.insert(name);
                ++mCounters.mScriptsRefused;
                for (const openeden::script::ErrorReport& report : result.reports)
                    say(true, report.format());
                return false;
            }
            mLoaded.insert(name);
            ++mCounters.mScriptsLoaded;
            if (!result.info.parent.empty())
                (void)load(lower(result.info.parent), false);
            const ScriptPapyrus::Pex::ParseResult parsed = ScriptPapyrus::Pex::parse(bytes, name);
            if (parsed.mOk)
            {
                for (const std::string& text : parsed.mFile.mStrings)
                {
                    const std::string other = lower(text);
                    if (plausibleScriptName(other) && mLoaded.count(other) == 0 && mNotFile.count(other) == 0)
                        (void)load(other, false);
                }
            }
            return true;
        }

        // ---- attaching ---------------------------------------------------------------------------------------------

        InstanceId attachScript(const AttachKey& key, const InstanceInfo& info, const vmad::Script& script)
        {
            const std::string name = lower(script.mName);
            if (!load(name, true))
                return 0;
            if (mInstances.size() >= mOptions.mMaxInstances)
                return 0;
            const InstanceId instance = mRuntime->attach(key, script.mName);
            if (instance == 0)
            {
                ++mCounters.mAttachRefused;
                return 0;
            }
            InstanceInfo stored = info;
            stored.mScript = name;
            mInstances.emplace(instance, stored);
            if (info.mIsAlias)
                mByAlias[{ info.mForm, info.mAlias }].push_back(instance);
            else
                mByForm.emplace(info.mForm, instance);
            ++mCounters.mInstancesAttached;
            ++mCounters.mEvents["oninit"];
            for (const vmad::Property& property : script.mProperties)
                setProperty(instance, property);
            return instance;
        }

        void setProperty(InstanceId instance, const vmad::Property& property)
        {
            if (property.mArray)
            {
                ++mCounters.mPropertiesSkipped;
                return;
            }
            std::string cls;
            PapyrusValue value;
            const vmad::Value& v = property.mValue;
            switch (v.mKind)
            {
                case vmad::ValueKind::Object:
                {
                    if (v.mAlias >= 0 && v.mForm != 0)
                    {
                        const QuestAlias* alias = nullptr;
                        if (mIndex != nullptr)
                            if (const QuestRecord* quest = mIndex->quest(v.mForm))
                                alias = quest->alias(static_cast<std::uint32_t>(v.mAlias));
                        cls = alias != nullptr && alias->mIsLocation ? "LocationAlias" : "ReferenceAlias";
                        value = PapyrusValue::makeForm(cls, aliasHandle(v.mForm, static_cast<std::uint32_t>(v.mAlias)));
                    }
                    else if (v.mForm != 0)
                    {
                        if (!formClass(v.mForm, cls))
                            cls = "Form";
                        value = PapyrusValue::makeForm(cls, v.mForm);
                    }
                    else
                        value = PapyrusValue::makeNone();
                    break;
                }
                case vmad::ValueKind::String:
                    value = PapyrusValue::makeString(v.mText);
                    break;
                case vmad::ValueKind::Int:
                    value = PapyrusValue::makeInt(v.mInteger);
                    break;
                case vmad::ValueKind::Float:
                    value = PapyrusValue::makeFloat(v.mNumber);
                    break;
                case vmad::ValueKind::Bool:
                    value = PapyrusValue::makeBool(v.mInteger != 0);
                    break;
                default:
                    ++mCounters.mPropertiesSkipped;
                    return;
            }
            if (mRuntime->setProperty(instance, property.mName, value))
                ++mCounters.mPropertiesSet;
            else
                ++mCounters.mPropertiesRejected;
        }

        bool attachQuest(FormId id)
        {
            if (mIndex == nullptr || mIndex->quest(id) == nullptr || mQuestsAttached.count(id) != 0)
                return false;
            mQuestsAttached.insert(id);
            const vmad::Vmad* v = mVmads ? mVmads(id) : nullptr;
            if (v == nullptr)
                return false;
            std::uint32_t ordinal = 0;
            std::size_t attached = 0;
            bool fragmentAttached = v->mFragmentScript.empty();
            for (const vmad::Script& script : v->mScripts)
            {
                if (!v->mFragmentScript.empty() && lower(script.mName) == lower(v->mFragmentScript))
                    fragmentAttached = true;
                InstanceInfo info;
                info.mForm = id;
                attached += attachScript(AttachKey{ AttachKind::Form, id, 0, ordinal++ }, info, script) != 0 ? 1 : 0;
            }
            if (!fragmentAttached)
            {
                vmad::Script fragment;
                fragment.mName = v->mFragmentScript;
                InstanceInfo info;
                info.mForm = id;
                attached += attachScript(AttachKey{ AttachKind::Form, id, 0, ordinal++ }, info, fragment) != 0 ? 1 : 0;
            }
            for (const vmad::AliasScripts& alias : v->mAliases)
            {
                if (alias.mAlias < 0)
                    continue;
                std::uint32_t aliasOrdinal = 0;
                for (const vmad::Script& script : alias.mScripts)
                {
                    InstanceInfo info;
                    info.mForm = id;
                    info.mAlias = static_cast<std::uint32_t>(alias.mAlias);
                    info.mIsAlias = true;
                    attached
                        += attachScript(AttachKey{ AttachKind::Alias, id, info.mAlias, aliasOrdinal++ }, info, script)
                            != 0
                        ? 1
                        : 0;
                }
            }
            return attached != 0;
        }

        // ---- events ------------------------------------------------------------------------------------------------

        bool sendEvent(InstanceId instance, const std::string& name, const std::vector<PapyrusValue>& args = {})
        {
            const openeden::script::CallResult result = mRuntime->send(instance, name, args);
            if (!result.ok)
            {
                ++mCounters.mScriptErrors;
                say(true, "Papyrus: could not queue " + name + ": " + result.error.format());
                return false;
            }
            ++mCounters.mEvents[lower(name)];
            return true;
        }

        std::vector<InstanceId> instancesOf(FormId form) const
        {
            std::vector<InstanceId> out;
            const auto range = mByForm.equal_range(form);
            for (auto it = range.first; it != range.second; ++it)
                out.push_back(it->second);
            return out;
        }

        InstanceId findInstance(FormId form, std::string_view script) const
        {
            const std::string wanted = lower(script);
            const auto range = mByForm.equal_range(form);
            for (auto it = range.first; it != range.second; ++it)
            {
                const auto info = mInstances.find(it->second);
                if (info != mInstances.end() && info->second.mScript == wanted)
                    return it->second;
            }
            return 0;
        }

        InstanceId findAliasInstance(FormId quest, std::uint32_t alias, std::string_view script) const
        {
            const auto it = mByAlias.find({ quest, alias });
            if (it == mByAlias.end())
                return 0;
            const std::string wanted = lower(script);
            for (const InstanceId id : it->second)
            {
                const auto info = mInstances.find(id);
                if (info != mInstances.end() && info->second.mScript == wanted)
                    return id;
            }
            return 0;
        }

        // ---- quests ------------------------------------------------------------------------------------------------

        void runFragments(FormId quest, std::uint32_t stage)
        {
            const vmad::Vmad* v = mVmads ? mVmads(quest) : nullptr;
            if (v == nullptr)
                return;
            for (const vmad::Fragment& fragment : v->mFragments)
            {
                if (fragment.mStage != stage)
                    continue;
                const InstanceId instance = findInstance(quest, fragment.mScript);
                if (instance == 0)
                    continue;
                if (sendEvent(instance, fragment.mFunction))
                {
                    ++mCounters.mFragmentsRun;
                    say(false,
                        "Papyrus: quest " + hex(quest) + " stage " + std::to_string(stage) + " runs " + fragment.mScript
                            + "." + fragment.mFunction);
                }
            }
        }

        bool setStage(FormId quest, std::uint32_t stage)
        {
            if (mIndex == nullptr || mState == nullptr)
                return false;
            const QuestRecord* record = mIndex->quest(quest);
            if (record == nullptr)
                return false;
            if (mState->stageDone(quest, stage) && (record->mFlags & QuestFlags::AllowRepeatedStages) == 0)
                return false;
            mState->setStage(*mIndex, quest, stage);
            ++mCounters.mStageSets;
            say(false, "Papyrus: quest " + hex(quest) + " set to stage " + std::to_string(stage));
            if (mQuestChanged)
                mQuestChanged(quest, stage);
            (void)attachQuest(quest); // a quest that starts by a stage gets its scripts now
            runFragments(quest, stage);
            std::int32_t logEntry = 0;
            if (const vmad::Vmad* v = mVmads ? mVmads(quest) : nullptr)
                for (const vmad::Fragment& fragment : v->mFragments)
                    if (fragment.mStage == stage)
                    {
                        logEntry = fragment.mLogEntry;
                        break;
                    }
            for (const InstanceId instance : instancesOf(quest))
                (void)sendEvent(instance, "OnStageSet",
                    { PapyrusValue::makeInt(static_cast<std::int32_t>(stage)), PapyrusValue::makeInt(logEntry) });
            return true;
        }

        void startQuest(FormId quest)
        {
            if (mState == nullptr)
                return;
            mState->start(quest);
            (void)attachQuest(quest);
            runStartUpStage(quest);
        }

        void runStartUpStage(FormId quest)
        {
            if (mIndex == nullptr)
                return;
            const QuestRecord* record = mIndex->quest(quest);
            const QuestStage* stage = record != nullptr ? record->startUpStage() : nullptr;
            if (stage != nullptr)
                runFragments(quest, stage->mIndex);
        }

        // ---- references --------------------------------------------------------------------------------------------

        void setEnabled(FormId reference, bool enabled)
        {
            if (referenceEnabled(reference) == enabled)
                return;
            mEnabled[reference] = enabled;
            say(false, "Papyrus: reference " + hex(reference) + (enabled ? " enabled" : " disabled"));
            if (mEnableChanged)
                mEnableChanged(reference, enabled);
        }

        bool referenceEnabled(FormId reference) const
        {
            const auto it = mEnabled.find(reference);
            return it == mEnabled.end() || it->second;
        }

        // ---- timers ------------------------------------------------------------------------------------------------

        void registerUpdate(InstanceId instance, double interval, bool repeat)
        {
            unregisterUpdate(instance);
            if (!(interval >= 0.0))
                interval = 0.0;
            const auto key = std::make_pair(mTime + interval, mTimerSeq++);
            mTimers.emplace(key, Timer{ instance, repeat ? std::max(interval, 0.01) : 0.0 });
        }

        void unregisterUpdate(InstanceId instance)
        {
            for (auto it = mTimers.begin(); it != mTimers.end();)
            {
                if (it->second.mInstance == instance)
                    it = mTimers.erase(it);
                else
                    ++it;
            }
        }

        void fireTimers()
        {
            while (!mTimers.empty() && mTimers.begin()->first.first <= mTime)
            {
                const auto node = mTimers.begin();
                const Timer timer = node->second;
                const double due = node->first.first;
                mTimers.erase(node);
                if (sendEvent(timer.mInstance, "OnUpdate"))
                    ++mCounters.mUpdatesFired;
                if (timer.mRepeat > 0.0)
                    mTimers.emplace(std::make_pair(std::max(due + timer.mRepeat, mTime + 0.001), mTimerSeq++), timer);
            }
        }

        // ---- natives -----------------------------------------------------------------------------------------------

        using Handler = std::function<NativeResult(const NativeCall&)>;

        void reg(const char* className, const char* function, bool global, Handler handler)
        {
            NativeDescriptor descriptor;
            descriptor.className = className;
            descriptor.functionName = function;
            descriptor.global = global;
            const std::string key = lower(std::string(className) + "." + function);
            openeden::script::ErrorReport error;
            const bool ok = mRuntime->registerNative(
                descriptor,
                [this, key, handler = std::move(handler)](const NativeCall& call) {
                    ++mCounters.mNatives[key];
                    return handler(call);
                },
                error);
            if (!ok)
                say(true, "Papyrus: native " + key + " was refused: " + error.format());
        }

        static double argNumber(const NativeCall& call, std::size_t i)
        {
            if (i >= call.arguments.size())
                return 0.0;
            const PapyrusValue& v = call.arguments[i];
            return v.kind == PapyrusKind::Float ? static_cast<double>(v.number) : static_cast<double>(v.integer);
        }

        static std::int32_t argInt(const NativeCall& call, std::size_t i)
        {
            return static_cast<std::int32_t>(argNumber(call, i));
        }

        static NativeResult result(const PapyrusValue& v)
        {
            NativeResult r;
            r.value = v;
            return r;
        }

        void registerWorldNatives()
        {
            // Game
            reg("Game", "GetPlayer", true, [this](const NativeCall&) { return result(formValue(kPlayerReference)); });
            reg("Game", "GetForm", true,
                [this](const NativeCall& c) { return result(formValue(static_cast<FormId>(argInt(c, 0)))); });
            // Utility
            reg("Utility", "GetCurrentGameTime", true, [this](const NativeCall&) {
                return result(PapyrusValue::makeFloat(static_cast<float>(mTime * mOptions.mTimeScale / 86400.0)));
            });
            reg("Utility", "GetCurrentRealTime", true,
                [this](const NativeCall&) { return result(PapyrusValue::makeFloat(static_cast<float>(mTime))); });
            reg("Utility", "IsInMenuMode", true,
                [](const NativeCall&) { return result(PapyrusValue::makeBool(false)); });
            // Form
            reg("Form", "GetFormID", false, [this](const NativeCall& c) {
                return result(PapyrusValue::makeInt(static_cast<std::int32_t>(formOf(c.self))));
            });
            reg("Form", "GetName", false, [this](const NativeCall& c) {
                mStringScratch = mData.nameOf(formOf(c.self));
                return result(PapyrusValue::makeString(mStringScratch));
            });
            reg("Form", "RegisterForSingleUpdate", false, [this](const NativeCall& c) {
                if (c.self.attached)
                    registerUpdate(c.self.handle, argNumber(c, 0), false);
                return NativeResult{};
            });
            reg("Form", "RegisterForUpdate", false, [this](const NativeCall& c) {
                if (c.self.attached)
                    registerUpdate(c.self.handle, argNumber(c, 0), true);
                return NativeResult{};
            });
            reg("Form", "UnregisterForUpdate", false, [this](const NativeCall& c) {
                if (c.self.attached)
                    unregisterUpdate(c.self.handle);
                return NativeResult{};
            });
            // ObjectReference
            const auto enable = [this](bool on) {
                return [this, on](const NativeCall& c) {
                    const FormId ref = formOf(c.self);
                    if (ref != 0)
                        setEnabled(ref, on);
                    return NativeResult{};
                };
            };
            reg("ObjectReference", "Enable", false, enable(true));
            reg("ObjectReference", "EnableNoWait", false, enable(true));
            reg("ObjectReference", "Disable", false, enable(false));
            reg("ObjectReference", "DisableNoWait", false, enable(false));
            reg("ObjectReference", "IsDisabled", false, [this](const NativeCall& c) {
                return result(PapyrusValue::makeBool(!referenceEnabled(formOf(c.self))));
            });
            reg("ObjectReference", "IsEnabled", false, [this](const NativeCall& c) {
                return result(PapyrusValue::makeBool(referenceEnabled(formOf(c.self))));
            });
            const auto position = [this](int axis) {
                return [this, axis](const NativeCall& c) {
                    double xyz[3] = { 0.0, 0.0, 0.0 };
                    (void)mData.referencePosition(formOf(c.self), xyz[0], xyz[1], xyz[2]);
                    return result(PapyrusValue::makeFloat(static_cast<float>(xyz[axis])));
                };
            };
            reg("ObjectReference", "GetPositionX", false, position(0));
            reg("ObjectReference", "GetPositionY", false, position(1));
            reg("ObjectReference", "GetPositionZ", false, position(2));
            reg("ObjectReference", "GetBaseObject", false, [this](const NativeCall& c) {
                const FormId base = mData.baseOf(formOf(c.self));
                return result(base != 0 ? PapyrusValue::makeForm(intern("Form"), base) : PapyrusValue::makeNone());
            });
            reg("ObjectReference", "Activate", false, [this](const NativeCall& c) {
                const FormId target = formOf(c.self);
                const FormId activator = c.arguments.empty() ? 0 : formOf(c.arguments[0]);
                for (const InstanceId instance : instancesOf(target))
                    (void)sendEvent(instance, "OnActivate", { formValue(activator) });
                return result(PapyrusValue::makeBool(true));
            });
            // Quest
            reg("Quest", "SetStage", false, [this](const NativeCall& c) {
                return result(
                    PapyrusValue::makeBool(setStage(formOf(c.self), static_cast<std::uint32_t>(argInt(c, 0)))));
            });
            const auto currentStage = [this](const NativeCall& c) {
                return result(PapyrusValue::makeInt(
                    mState != nullptr ? static_cast<std::int32_t>(mState->stage(formOf(c.self))) : 0));
            };
            reg("Quest", "GetStage", false, currentStage);
            reg("Quest", "GetCurrentStageID", false, currentStage);
            reg("Quest", "GetStageDone", false, [this](const NativeCall& c) {
                return result(PapyrusValue::makeBool(
                    mState != nullptr && mState->stageDone(formOf(c.self), static_cast<std::uint32_t>(argInt(c, 0)))));
            });
            reg("Quest", "IsRunning", false, [this](const NativeCall& c) {
                return result(PapyrusValue::makeBool(mState != nullptr && mState->running(formOf(c.self))));
            });
            reg("Quest", "IsCompleted", false, [this](const NativeCall& c) {
                return result(PapyrusValue::makeBool(mState != nullptr && mState->completed(formOf(c.self))));
            });
            reg("Quest", "Start", false, [this](const NativeCall& c) {
                const FormId quest = formOf(c.self);
                const bool was = mState != nullptr && mState->running(quest);
                if (!was)
                    startQuest(quest);
                return result(PapyrusValue::makeBool(!was));
            });
            reg("Quest", "Stop", false, [this](const NativeCall& c) {
                if (mState != nullptr)
                    mState->stop(formOf(c.self));
                return NativeResult{};
            });
            const auto objective = [this](bool completed) {
                return [this, completed](const NativeCall& c) {
                    const auto key = std::make_pair(formOf(c.self), static_cast<std::uint32_t>(argInt(c, 0)));
                    const bool on = c.arguments.size() > 1 ? argInt(c, 1) != 0 : true;
                    (completed ? mObjectivesCompleted : mObjectivesDisplayed)[key] = on;
                    return NativeResult{};
                };
            };
            reg("Quest", "SetObjectiveDisplayed", false, objective(false));
            reg("Quest", "SetObjectiveCompleted", false, objective(true));
            reg("Quest", "IsObjectiveDisplayed", false, [this](const NativeCall& c) {
                const auto it = mObjectivesDisplayed.find({ formOf(c.self), static_cast<std::uint32_t>(argInt(c, 0)) });
                return result(PapyrusValue::makeBool(it != mObjectivesDisplayed.end() && it->second));
            });
            reg("Quest", "IsObjectiveCompleted", false, [this](const NativeCall& c) {
                const auto it = mObjectivesCompleted.find({ formOf(c.self), static_cast<std::uint32_t>(argInt(c, 0)) });
                return result(PapyrusValue::makeBool(it != mObjectivesCompleted.end() && it->second));
            });
            // Alias and ReferenceAlias
            reg("Alias", "GetOwningQuest", false, [this](const NativeCall& c) {
                FormId quest = 0;
                std::uint32_t alias = 0;
                return result(aliasOf(formOf(c.self), quest, alias) ? formValue(quest) : PapyrusValue::makeNone());
            });
            const auto reference = [this](const NativeCall& c) {
                FormId quest = 0;
                std::uint32_t alias = 0;
                if (!aliasOf(formOf(c.self), quest, alias))
                    return result(PapyrusValue::makeNone());
                const auto forced = mAliasForced.find({ quest, alias });
                if (forced != mAliasForced.end())
                    return result(formValue(forced->second));
                if (mState != nullptr)
                {
                    const AliasFill fill = mState->aliasFill(quest, alias);
                    if (fill.mKnown && fill.mReference != 0)
                        return result(formValue(fill.mReference));
                }
                return result(PapyrusValue::makeNone());
            };
            reg("ReferenceAlias", "GetReference", false, reference);
            reg("ReferenceAlias", "GetActorReference", false, reference);
            reg("ReferenceAlias", "ForceRefTo", false, [this](const NativeCall& c) {
                FormId quest = 0;
                std::uint32_t alias = 0;
                if (aliasOf(formOf(c.self), quest, alias))
                    mAliasForced[{ quest, alias }] = c.arguments.empty() ? 0 : formOf(c.arguments[0]);
                return NativeResult{};
            });
            reg("ReferenceAlias", "Clear", false, [this](const NativeCall& c) {
                FormId quest = 0;
                std::uint32_t alias = 0;
                if (aliasOf(formOf(c.self), quest, alias))
                    mAliasForced.erase({ quest, alias });
                return NativeResult{};
            });
        }

        // ---- tick --------------------------------------------------------------------------------------------------

        void tick(float dt)
        {
            if (mInTick)
                return;
            mInTick = true;
            if (dt > 0.f)
                mTime += static_cast<double>(dt);
            ++mCounters.mTicks;
            fireTimers();
            const openeden::script::TickResult r = mRuntime->tick(mTime);
            mCounters.mStacksRun += r.stacksRun;
            for (const openeden::script::ErrorReport& error : r.errors)
            {
                ++mCounters.mScriptErrors;
                say(true, "Papyrus: " + error.format());
            }
            mInTick = false;
        }

        struct Timer
        {
            InstanceId mInstance = 0;
            double mRepeat = 0.0;
        };

        PapyrusGameData& mData;
        PapyrusHostOptions mOptions;
        std::unique_ptr<ScriptPapyrus::PapyrusRuntime> mRuntime;
        std::unique_ptr<ScriptPapyrus::PureNatives> mPure;
        PapyrusHostCounters mCounters;
        LogFunction mLog;
        QuestChanged mQuestChanged;
        EnableChanged mEnableChanged;
        const QuestIndex* mIndex = nullptr;
        QuestState* mState = nullptr;
        QuestVmadLookup mVmads;
        bool mStarted = false;
        bool mInTick = false;
        double mTime = 0.0;
        std::uint64_t mTimerSeq = 0;
        std::set<std::string> mLoaded;
        std::set<std::string> mNotFile;
        std::set<std::string> mNames;
        std::set<std::string> mTolerant;
        std::map<InstanceId, InstanceInfo> mInstances;
        std::multimap<FormId, InstanceId> mByForm;
        std::map<std::pair<FormId, std::uint32_t>, std::vector<InstanceId>> mByAlias;
        std::set<FormId> mQuestsAttached;
        std::set<FormId> mRefAttached;
        std::map<FormId, std::string> mRefClass;
        std::map<std::pair<double, std::uint64_t>, Timer> mTimers;
        std::map<FormId, bool> mEnabled;
        std::vector<std::pair<FormId, std::uint32_t>> mAliasKeys;
        std::map<std::pair<FormId, std::uint32_t>, std::uint32_t> mAliasByKey;
        std::map<std::pair<FormId, std::uint32_t>, FormId> mAliasForced;
        std::map<std::pair<FormId, std::uint32_t>, bool> mObjectivesDisplayed;
        std::map<std::pair<FormId, std::uint32_t>, bool> mObjectivesCompleted;
        std::string mStringScratch;
    };

    PapyrusHost::PapyrusHost(PapyrusGameData& data, PapyrusHostOptions options)
        : mImpl(std::make_unique<Impl>(data, options))
    {
    }

    PapyrusHost::~PapyrusHost() = default;

    void PapyrusHost::setLog(LogFunction log)
    {
        mImpl->mLog = std::move(log);
    }

    void PapyrusHost::setQuestChanged(QuestChanged changed)
    {
        mImpl->mQuestChanged = std::move(changed);
    }

    void PapyrusHost::setEnableChanged(EnableChanged changed)
    {
        mImpl->mEnableChanged = std::move(changed);
    }

    void PapyrusHost::setQuests(const QuestIndex* index, QuestState* state, QuestVmadLookup vmads)
    {
        mImpl->mIndex = index;
        mImpl->mState = state;
        mImpl->mVmads = std::move(vmads);
    }

    void PapyrusHost::startGame()
    {
        Impl& m = *mImpl;
        if (m.mStarted || m.mIndex == nullptr || m.mState == nullptr)
            return;
        m.mStarted = true;
        for (const FormId quest : m.mIndex->questIds())
        {
            const QuestRecord* record = m.mIndex->quest(quest);
            if (record == nullptr || !record->startsEnabled() || !m.mState->running(quest))
                continue;
            (void)m.attachQuest(quest);
            m.runStartUpStage(quest);
        }
    }

    bool PapyrusHost::attachQuest(FormId quest)
    {
        return mImpl->attachQuest(quest);
    }

    std::size_t PapyrusHost::attachReference(
        FormId reference, FormId base, bool actor, const std::vector<vmad::Script>& scripts)
    {
        (void)base;
        Impl& m = *mImpl;
        if (!m.mRefAttached.insert(reference).second)
            return 0; // its cell loaded before: the scripts are attached already
        m.mRefClass[reference] = actor ? "Actor" : "ObjectReference";
        std::size_t attached = 0;
        std::uint32_t ordinal = 0;
        for (const vmad::Script& script : scripts)
        {
            Impl::InstanceInfo info;
            info.mForm = reference;
            attached
                += m.attachScript(AttachKey{ AttachKind::Form, reference, 0, ordinal++ }, info, script) != 0 ? 1 : 0;
        }
        return attached;
    }

    void PapyrusHost::onCellLoaded(FormId cell, const std::vector<FormId>& references)
    {
        (void)cell;
        Impl& m = *mImpl;
        for (const FormId reference : references)
            for (const InstanceId instance : m.instancesOf(reference))
            {
                (void)m.sendEvent(instance, "OnCellLoad");
                (void)m.sendEvent(instance, "OnLoad");
            }
    }

    void PapyrusHost::onActivate(FormId target, FormId actionRef)
    {
        Impl& m = *mImpl;
        for (const InstanceId instance : m.instancesOf(target))
            (void)m.sendEvent(instance, "OnActivate", { m.formValue(actionRef) });
    }

    void PapyrusHost::tick(float dt)
    {
        mImpl->tick(dt);
    }

    openeden::script::SaveBlob PapyrusHost::serialiseState() const
    {
        return mImpl->mRuntime->serialise();
    }

    bool PapyrusHost::restoreState(const openeden::script::SaveBlob& blob)
    {
        return mImpl->mRuntime->restore(blob).ok;
    }

    const PapyrusHostCounters& PapyrusHost::counters() const
    {
        return mImpl->mCounters;
    }

    const ScriptPapyrus::Stats& PapyrusHost::runtimeStats() const
    {
        return mImpl->mRuntime->stats();
    }

    std::vector<std::string> PapyrusHost::tolerantNatives() const
    {
        return std::vector<std::string>(mImpl->mTolerant.begin(), mImpl->mTolerant.end());
    }

    bool PapyrusHost::referenceEnabled(FormId reference) const
    {
        return mImpl->referenceEnabled(reference);
    }

    std::size_t PapyrusHost::instanceCount() const
    {
        return mImpl->mInstances.size();
    }

    double PapyrusHost::scriptTime() const
    {
        return mImpl->mTime;
    }

    namespace
    {
        std::optional<PapyrusValue> variableOf(const PapyrusHost::Impl& m, InstanceId instance, std::string_view name)
        {
            if (instance == 0)
                return std::nullopt;
            PapyrusValue value;
            if (!m.mRuntime->getVariable(instance, name, value))
                return std::nullopt;
            return value;
        }
    }

    std::optional<double> PapyrusHost::numberVariable(FormId form, std::string_view script, std::string_view name) const
    {
        const auto value = variableOf(*mImpl, mImpl->findInstance(form, script), name);
        if (!value)
            return std::nullopt;
        switch (value->kind)
        {
            case PapyrusKind::Int:
            case PapyrusKind::Bool:
                return static_cast<double>(value->integer);
            case PapyrusKind::Float:
                return static_cast<double>(value->number);
            default:
                return std::nullopt;
        }
    }

    std::optional<std::string> PapyrusHost::stringVariable(
        FormId form, std::string_view script, std::string_view name) const
    {
        const auto value = variableOf(*mImpl, mImpl->findInstance(form, script), name);
        if (!value || value->kind != PapyrusKind::String)
            return std::nullopt;
        return std::string(value->text);
    }

    std::optional<FormId> PapyrusHost::formVariable(FormId form, std::string_view script, std::string_view name) const
    {
        const auto value = variableOf(*mImpl, mImpl->findInstance(form, script), name);
        if (!value || value->kind != PapyrusKind::Object || value->attached
            || (value->handle & 0xFF000000u) == 0xFF000000u)
            return std::nullopt;
        return value->handle;
    }

    std::optional<double> PapyrusHost::aliasScriptNumber(
        FormId quest, std::uint32_t alias, std::string_view script, std::string_view name) const
    {
        const auto value = variableOf(*mImpl, mImpl->findAliasInstance(quest, alias, script), name);
        if (!value)
            return std::nullopt;
        switch (value->kind)
        {
            case PapyrusKind::Int:
            case PapyrusKind::Bool:
                return static_cast<double>(value->integer);
            case PapyrusKind::Float:
                return static_cast<double>(value->number);
            default:
                return std::nullopt;
        }
    }

    bool PapyrusHost::callFunction(FormId form, std::string_view script, std::string_view function)
    {
        const InstanceId instance = mImpl->findInstance(form, script);
        if (instance == 0 || mImpl->mInTick)
            return false;
        const openeden::script::CallResult r = mImpl->mRuntime->call(instance, function, {});
        if (!r.ok)
        {
            ++mImpl->mCounters.mScriptErrors;
            mImpl->say(true, "Papyrus: " + std::string(function) + ": " + r.error.format());
        }
        return r.ok;
    }
}
