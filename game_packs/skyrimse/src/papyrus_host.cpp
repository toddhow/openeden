#include "game_skyrimse/papyrus_host.hpp"

#include <openeden/game/world_source_service.hpp>
#include <components/script_papyrus/papyrusnatives.hpp>
#include <components/script_papyrus/pexfile.hpp>

#include <algorithm>
#include <array>
#include <bit>
#include <cmath>
#include <cstdlib>
#include <cstdio>
#include <deque>

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
        constexpr std::int32_t kMaxPlaceCount = 16; // PlaceAtMe's aiCount is cut to this (task 0202)
        constexpr std::size_t kMaxCreatedKept = 65536; // created references the state keeps
        constexpr std::size_t kMaxNotificationBytes = 256;
        constexpr std::size_t kMaxAvOffsets = 4096; // actor value changes a script may leave behind

        // "Max Health" and "maxhealth" are the same: lower case without spaces
        std::string avKey(std::string_view s)
        {
            std::string out;
            for (const char c : s)
                if (c != ' ' && c != '_')
                    out.push_back(static_cast<char>(c >= 'A' && c <= 'Z' ? c + 32 : c));
            return out;
        }

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

    struct PapyrusHost::Impl final : public openeden::script::PapyrusWorld, public openeden::script::PapyrusAttachKeyCheck
    {
        struct InstanceInfo
        {
            std::uint32_t mOrdinal = 0; ///< AttachKey::effectKey: the script's place in its form's list
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
            registerStoneNatives();
        }

        // ---- PapyrusWorld ------------------------------------------------------------------------------------------

        bool formClass(openeden::script::FormId form, std::string& className) const override
        {
            if (mCreated.count(form) != 0)
            {
                className = "ObjectReference";
                return true;
            }
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
            if (className.empty() && mData.isTopicInfo(form))
                className = "TopicInfo";
            if (className.empty()) // task 0223: a GLOB is a GlobalVariable (CarriageCost.value reaches GetValue)
            {
                float unused = 0.f;
                if (mData.globalValue(form, unused))
                    className = "GlobalVariable";
            }
            if (className.empty() && mData.isScene(form))
                className = "Scene";
            return !className.empty();
        }

        void report(std::string_view message) override
        {
            constexpr std::string_view kNote = "[Notification] ";
            if (message.substr(0, kNote.size()) == kNote)
                note(std::string(message.substr(kNote.size())));
            say(false, "Papyrus: " + std::string(message));
        }

        // Task 0198: Debug.Notification. A ring of the last few, and the pack's hook (the HUD) is told at once.
        void note(std::string text)
        {
            if (text.size() > kMaxNotificationBytes)
                text.resize(kMaxNotificationBytes);
            ++mNoteCount;
            mNotes.push_back(text);
            if (mNotes.size() > PapyrusHost::kMaxNotifications)
                mNotes.pop_front();
            if (mHooks != nullptr)
                mHooks->notification(text);
        }

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

        // Task 0223: the value of a quest as a script sees it. A scripted quest IS its script in Papyrus, so a cast to the
        // script's type (the carriage topics' fragments: GetOwningQuest() as CarriageSystemScript) needs the attached
        // instance; a quest of several scripts gives the first that is not a fragment script (QF_...).
        PapyrusValue questValue(FormId quest)
        {
            if (quest == 0)
                return PapyrusValue::makeNone();
            for (const InstanceId instance : instancesOf(quest))
            {
                const auto it = mInstances.find(instance);
                if (it != mInstances.end() && !it->second.mIsAlias && it->second.mScript.rfind("qf_", 0) != 0)
                    return PapyrusValue::makeInstance(instance);
            }
            return formValue(quest);
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
            stored.mOrdinal = key.effectKey;
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

        // Task 0187: the aliases of a quest that starts get their references (`matching`: also the find-matching ones).
        void fillAliases(FormId quest, bool matching)
        {
            if (mFiller == nullptr || mState == nullptr)
                return;
            const std::size_t filled = mFiller->fillQuest(quest, *mState, matching);
            mCounters.mAliasesFilled += filled;
            if (filled != 0 && matching)
                say(false,
                    "Papyrus: quest " + hex(quest) + " started with " + std::to_string(filled) + " aliases filled");
        }

        // Task 0187: the runtime refuses to load a script from inside a native, and attaching a quest that no script
        // named before loads its scripts. So the attach, the fragments and the events of a stage change or a start that a
        // script asked for run right after the native (and the script that called it) returned.
        void afterNative(std::function<void()> work)
        {
            if (mNativeDepth > 0)
                mDeferred.push_back(std::move(work));
            else
                work();
        }

        bool runDeferred()
        {
            bool any = false;
            for (int round = 0; !mDeferred.empty() && round < 16; ++round)
            {
                std::vector<std::function<void()>> work = std::move(mDeferred);
                mDeferred.clear();
                for (std::function<void()>& f : work)
                    f();
                any = true;
            }
            return any;
        }

        // The classes a native hands to scripts (an Actor as akSpeakerRef, the owning Quest of an alias, ...) must be
        // loaded before the first call on them, and cannot be loaded inside a native.
        void preloadClasses()
        {
            if (mPreloaded)
                return;
            mPreloaded = true;
            for (const char* name : { "form", "objectreference", "actor", "quest", "alias", "referencealias",
                     "locationalias", "location", "keyword", "topicinfo", "scene", "cell", "spell", "game", "utility", "debug" })
                (void)load(name, false);
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
            if (!mState->running(quest))
                fillAliases(quest, true);
            mState->setStage(*mIndex, quest, stage);
            ++mCounters.mStageSets;
            say(false, "Papyrus: quest " + hex(quest) + " set to stage " + std::to_string(stage));
            if (mQuestChanged)
                mQuestChanged(quest, stage);
            afterNative([this, quest, stage] {
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
            });
            return true;
        }

        void startQuest(FormId quest)
        {
            if (mState == nullptr)
                return;
            fillAliases(quest, true);
            mState->start(quest);
            afterNative([this, quest] {
                (void)attachQuest(quest);
                runStartUpStage(quest);
                if (mScenes != nullptr)
                    mScenes->questStarted(quest); // task 0194: its scenes that begin on quest start
            });
        }

        // Task 0187: the story manager's answer to an event: the quests it chose are started (once each).
        std::size_t raiseStory(const StoryEvent& event)
        {
            if (!mStorySink || mState == nullptr)
                return 0;
            ++mCounters.mStoryEvents;
            const std::vector<FormId> chosen = mStorySink(event);
            std::size_t started = 0;
            for (const FormId quest : chosen)
            {
                if (mState->running(quest) || mState->completed(quest))
                    continue;
                if (mFiller != nullptr)
                    (void)mFiller->fillEventAliases(quest, *mState, event); // task 0194: R1 and R2 of the event
                startQuest(quest);
                ++started;
                ++mCounters.mStoryQuestsStarted;
                say(false, "Papyrus: story event " + event.mType + " started quest " + hex(quest));
                if (mStoryStarted)
                    mStoryStarted(quest, event);
            }
            return started;
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

        void setEnabled(FormId reference, bool enabled, float fadeSeconds = 0.f)
        {
            if (referenceEnabled(reference) == enabled)
                return;
            mEnabled[reference] = enabled;
            say(false,
                "Papyrus: reference " + hex(reference) + (enabled ? " enabled" : " disabled")
                    + (fadeSeconds > 0.f ? " with a fade" : ""));
            // Task 0202: the world first (it can fade); the old callback only when the world did not take it
            if (mHooks != nullptr && mHooks->setObjectEnabled(reference, enabled, fadeSeconds))
                return;
            if (mEnableChanged)
                mEnableChanged(reference, enabled);
        }

        bool referenceEnabled(FormId reference) const
        {
            const auto it = mEnabled.find(reference);
            return it != mEnabled.end() ? it->second : mData.initiallyEnabled(reference);
        }

        // Where a reference is now: where MoveTo put it, else where the data placed it.
        bool positionOf(FormId reference, double (&xyz)[3]) const
        {
            const auto it = mPositions.find(reference);
            if (it != mPositions.end())
            {
                xyz[0] = it->second[0];
                xyz[1] = it->second[1];
                xyz[2] = it->second[2];
                return true;
            }
            // Task 0202: a created reference is where it was created until a script moves it
            const auto created = mCreated.find(reference);
            if (created != mCreated.end())
            {
                xyz[0] = created->second.mPosition[0];
                xyz[1] = created->second.mPosition[1];
                xyz[2] = created->second.mPosition[2];
                return true;
            }
            return mData.referencePosition(reference, xyz[0], xyz[1], xyz[2]);
        }

        // Task 0202: the angles (radians) a script gave a reference, else those it was created with; none for a placed
        // reference nobody turned (the data's own angles are not kept by this host).
        bool anglesOf(FormId reference, std::array<double, 3>& angles) const
        {
            const auto it = mAngles.find(reference);
            if (it != mAngles.end())
            {
                angles = it->second;
                return true;
            }
            const auto created = mCreated.find(reference);
            if (created != mCreated.end())
            {
                angles = created->second.mAngles;
                return true;
            }
            return false;
        }

        FormId baseOfReference(FormId reference) const
        {
            const auto created = mCreated.find(reference);
            return created != mCreated.end() ? created->second.mBase : mData.baseOf(reference);
        }

        FormId cellOfReference(FormId reference) const
        {
            const auto created = mCreated.find(reference);
            return created != mCreated.end() ? created->second.mCell : mData.cellOf(reference);
        }

        // MoveTo between cells of which one is an interior is refused: the engine keeps an object in its own cell.
        bool crossesCells(FormId a, FormId b) const
        {
            const FormId cellA = cellOfReference(a);
            const FormId cellB = cellOfReference(b);
            return cellA != 0 && cellB != 0 && cellA != cellB
                && (mData.cellIsInterior(cellA) || mData.cellIsInterior(cellB));
        }

        void moveReference(FormId reference, const double* position, const std::array<double, 3>* angles)
        {
            ObjectMoveRequest request;
            if (position != nullptr)
            {
                mPositions[reference] = { position[0], position[1], position[2] };
                request.hasPosition = true;
                for (int i = 0; i < 3; ++i)
                    request.position[i] = position[i];
            }
            if (angles != nullptr)
            {
                mAngles[reference] = *angles;
                request.hasAngles = true;
                for (int i = 0; i < 3; ++i)
                    request.angles[i] = (*angles)[static_cast<std::size_t>(i)];
            }
            say(false, "Papyrus: reference " + hex(reference) + " moved");
            if (mHooks != nullptr)
                (void)mHooks->moveObject(reference, request);
        }

        // The runtime asks this when it restores a state: does the game still have what the key names?
        bool attachKeyExists(const AttachKey& key) const override
        {
            if (key.kind == AttachKind::Form)
            {
                if (key.formId == kPlayerReference || (mIndex != nullptr && mIndex->quest(key.formId) != nullptr))
                    return true;
                // task 0239: the fragment script a dialogue line left on its INFO (the follower topics' own)
                if (mData.isTopicInfo(key.formId))
                    return true;
                return !mData.formClass(key.formId).empty();
            }
            if (key.kind == AttachKind::Alias)
            {
                const QuestRecord* quest = mIndex != nullptr ? mIndex->quest(key.formId) : nullptr;
                return quest != nullptr && quest->alias(key.aliasIndex) != nullptr;
            }
            return false;
        }

        std::vector<std::uint8_t> save() const;
        PapyrusRestoreOutcome restore(std::span<const std::uint8_t> bytes);

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
                    if (mTraceNatives && mTraceCount < kMaxTracedNatives) // task 0223: OPENEDEN_PAPYRUS_NATIVE_TRACE
                    {
                        ++mTraceCount;
                        say(false, "Papyrus native " + key + " self " + hex(formOf(call.self)));
                    }
                    struct Depth
                    {
                        int& mValue;
                        explicit Depth(int& v)
                            : mValue(v)
                        {
                            ++mValue;
                        }
                        ~Depth() { --mValue; }
                    } depth(mNativeDepth);
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

        // Task 0237: the natives of the standing stones and the shrines (powerShrineScript, TempleBlessingScript).
        void registerStoneNatives()
        {
            reg("Actor", "HasSpell", false, [this](const NativeCall& c) {
                const FormId spell = c.arguments.empty() ? 0 : formOf(c.arguments[0]);
                return result(PapyrusValue::makeBool(spell != 0 && mSpells.count({ formOf(c.self), spell }) != 0));
            });
            reg("Actor", "RemoveSpell", false, [this](const NativeCall& c) {
                const FormId spell = c.arguments.empty() ? 0 : formOf(c.arguments[0]);
                const bool removed = spell != 0 && mSpells.erase({ formOf(c.self), spell }) != 0;
                if (removed)
                    ++mSpellRevision;
                return result(PapyrusValue::makeBool(removed));
            });
            reg("Actor", "AddPerk", false, [this](const NativeCall& c) {
                const FormId perk = c.arguments.empty() ? 0 : formOf(c.arguments[0]);
                if (perk != 0 && mPerks.size() < kMaxRecordedPerks && mPerks.emplace(formOf(c.self), perk).second)
                    ++mPerkRevision; // task 0241: the pack gives the player's perks to the combat when this moves
                return NativeResult{};
            });
            // Spell.Cast(source, target): queued for the pack, which applies the spell's effects to the target
            reg("Spell", "Cast", false, [this](const NativeCall& c) {
                PapyrusHost::SpellCast cast;
                cast.mSpell = formOf(c.self);
                cast.mSource = c.arguments.empty() ? 0 : formOf(c.arguments[0]);
                cast.mTarget = c.arguments.size() < 2 ? 0 : formOf(c.arguments[1]);
                if (cast.mSpell != 0 && mCasts.size() < PapyrusHost::kMaxQueuedCasts)
                    mCasts.push_back(cast);
                return NativeResult{};
            });
            reg("Message", "Show", false, [this](const NativeCall& c) {
                return result(PapyrusValue::makeInt(showMessage(formOf(c.self))));
            });
            reg("Game", "AddAchievement", true, [](const NativeCall&) { return NativeResult{}; });
            reg("ObjectReference", "PlayAnimation", false,
                [](const NativeCall&) { return result(PapyrusValue::makeBool(true)); });
        }

        // Message.Show: a corner text shows at once and answers 0; a message box asks the player (once) and answers
        // the last button, the usual "No", unless the player's answer is waiting.
        std::int32_t showMessage(FormId message)
        {
            PapyrusMessageInfo info;
            if (message == 0 || !mData.messageInfo(message, info))
            {
                say(false, "Papyrus: message " + hex(message) + " is not a message of the data");
                return 0;
            }
            if (!info.mBox || info.mButtons.size() < 2)
            {
                say(false, "Papyrus: message " + hex(message) + " shown: " + info.mText);
                if (!info.mText.empty())
                    note(info.mText);
                return 0;
            }
            const auto answer = mAnswers.find(message);
            if (answer != mAnswers.end())
            {
                const std::int32_t button = answer->second;
                mAnswers.erase(answer);
                return button;
            }
            const bool alreadyAsked = std::any_of(
                mAsked.begin(), mAsked.end(), [message](const Replay& r) { return r.mMessage == message; });
            if (mMessageSink && !alreadyAsked && mAsked.size() < kMaxAskedMessages)
            {
                mAsked.push_back({ message, mLastTarget, mLastActor });
                mMessageSink(message, info);
            }
            return static_cast<std::int32_t>(info.mButtons.size()) - 1;
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
            // Task 0217: what the radiant quests' start-up scripts call (the tolerant calls of a story-started Favor,
            // WI or bounty quest): alias timers like a form's, the location of a reference, animation events
            // (nothing raises them here, so the registration is accepted and never fires), destruction stages (no
            // destruction here: stage 0) and the user log (OpenUserLog opens a log file: accepted)
            reg("Alias", "RegisterForSingleUpdate", false, [this](const NativeCall& c) {
                if (c.self.attached)
                    registerUpdate(c.self.handle, argNumber(c, 0), false);
                return NativeResult{};
            });
            reg("Alias", "RegisterForUpdate", false, [this](const NativeCall& c) {
                if (c.self.attached)
                    registerUpdate(c.self.handle, argNumber(c, 0), true);
                return NativeResult{};
            });
            reg("Alias", "UnregisterForUpdate", false, [this](const NativeCall& c) {
                if (c.self.attached)
                    unregisterUpdate(c.self.handle);
                return NativeResult{};
            });
            reg("Form", "RegisterForAnimationEvent", false,
                [](const NativeCall&) { return result(PapyrusValue::makeBool(true)); });
            reg("ObjectReference", "GetCurrentDestructionStage", false,
                [](const NativeCall&) { return result(PapyrusValue::makeInt(0)); });
            reg("Debug", "OpenUserLog", true, [](const NativeCall&) { return result(PapyrusValue::makeBool(true)); });
            const auto locationOfSelf = [this](const NativeCall& c) {
                const FormId reference = formOf(c.self);
                return result(formValue(reference != 0 ? mData.locationOf(reference) : 0));
            };
            reg("ObjectReference", "GetCurrentLocation", false, locationOfSelf);
            reg("ObjectReference", "GetEditorLocation", false, locationOfSelf);
            // ObjectReference
            const auto enable = [this](bool on) {
                return [this, on](const NativeCall& c) {
                    const FormId ref = formOf(c.self);
                    if (ref != 0)
                        setEnabled(ref, on, argNumber(c, 0) != 0.0 ? kPapyrusFadeSeconds : 0.f);
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
                    (void)positionOf(formOf(c.self), xyz);
                    return result(PapyrusValue::makeFloat(static_cast<float>(xyz[axis])));
                };
            };
            reg("ObjectReference", "GetPositionX", false, position(0));
            reg("ObjectReference", "GetPositionY", false, position(1));
            reg("ObjectReference", "GetPositionZ", false, position(2));
            reg("ObjectReference", "MoveTo", false, [this](const NativeCall& c) {
                const FormId self = formOf(c.self);
                const FormId target = c.arguments.empty() ? 0 : formOf(c.arguments[0]);
                double where[3] = { 0.0, 0.0, 0.0 };
                if (self == 0 || target == 0 || !positionOf(target, where))
                    return NativeResult{};
                if (crossesCells(self, target))
                {
                    say(false,
                        "Papyrus: MoveTo of " + hex(self) + " to " + hex(target)
                            + " is in another cell: not done (an object stays in its own cell)");
                    return NativeResult{};
                }
                const double position[3] = { where[0] + argNumber(c, 1), where[1] + argNumber(c, 2),
                    where[2] + argNumber(c, 3) };
                // abMatchRotation: the target's angles, when this host knows them
                std::array<double, 3> angles{};
                const bool turn = argNumber(c, 4) != 0.0 && anglesOf(target, angles);
                moveReference(self, position, turn ? &angles : nullptr);
                return NativeResult{};
            });
            reg("ObjectReference", "SetPosition", false, [this](const NativeCall& c) {
                const FormId self = formOf(c.self);
                if (self != 0)
                {
                    const double position[3] = { argNumber(c, 0), argNumber(c, 1), argNumber(c, 2) };
                    if (std::isfinite(position[0]) && std::isfinite(position[1]) && std::isfinite(position[2]))
                        moveReference(self, position, nullptr);
                }
                return NativeResult{};
            });
            reg("ObjectReference", "SetAngle", false, [this](const NativeCall& c) {
                const FormId self = formOf(c.self);
                constexpr double kDegrees = 3.14159265358979323846 / 180.0;
                const std::array<double, 3> angles{ argNumber(c, 0) * kDegrees, argNumber(c, 1) * kDegrees,
                    argNumber(c, 2) * kDegrees };
                if (self != 0 && std::isfinite(angles[0]) && std::isfinite(angles[1]) && std::isfinite(angles[2]))
                    moveReference(self, nullptr, &angles);
                return NativeResult{};
            });
            reg("ObjectReference", "MoveToMyEditorLocation", false, [this](const NativeCall& c) {
                const FormId self = formOf(c.self);
                if (self == 0)
                    return NativeResult{};
                mPositions.erase(self);
                mAngles.erase(self);
                ObjectMoveRequest request;
                request.toOriginal = true;
                say(false, "Papyrus: reference " + hex(self) + " moved back to its editor location");
                if (mHooks != nullptr)
                    (void)mHooks->moveObject(self, request);
                return NativeResult{};
            });
            reg("ObjectReference", "PlaceAtMe", false, [this](const NativeCall& c) {
                const FormId self = formOf(c.self);
                const FormId base = c.arguments.empty() ? 0 : formOf(c.arguments[0]);
                double where[3] = { 0.0, 0.0, 0.0 };
                if (self == 0 || base == 0 || mHooks == nullptr || !positionOf(self, where))
                {
                    if (self != 0 && base != 0 && mHooks != nullptr)
                        say(false, "Papyrus: PlaceAtMe near " + hex(self) + " has no position to place at");
                    return result(PapyrusValue::makeNone());
                }
                std::array<double, 3> angles{};
                (void)anglesOf(self, angles);
                const auto nearCreated = mCreated.find(self);
                const FormId nearCell = nearCreated != mCreated.end() ? nearCreated->second.mCell : 0;
                const std::int32_t count = std::clamp<std::int32_t>(argInt(c, 1), 1, kMaxPlaceCount);
                const bool disabled = argNumber(c, 3) != 0.0;
                FormId first = 0;
                for (std::int32_t i = 0; i < count; ++i)
                {
                    ObjectPlaceRequest request;
                    request.base = base;
                    request.nearReference = self;
                    request.nearCell = nearCell;
                    for (std::size_t k = 0; k < 3; ++k)
                    {
                        request.position[k] = where[k];
                        request.angles[k] = angles[k];
                    }
                    const ObjectPlaceResult placed = mHooks->placeObject(request);
                    if (placed.reference == 0 || mCreated.size() >= kMaxCreatedKept)
                        break;
                    CreatedObject created;
                    created.mReference = placed.reference;
                    created.mBase = base;
                    created.mCell = placed.cell;
                    created.mPosition = { where[0], where[1], where[2] };
                    created.mAngles = angles;
                    mCreated[placed.reference] = created;
                    say(false, "Papyrus: " + hex(base) + " placed as " + hex(placed.reference) + " near " + hex(self));
                    if (disabled)
                        setEnabled(placed.reference, false, 0.f);
                    if (first == 0)
                        first = placed.reference;
                }
                return first != 0 ? result(PapyrusValue::makeForm(intern("ObjectReference"), first))
                                  : result(PapyrusValue::makeNone());
            });
            reg("Form", "HasKeyword", false, [this](const NativeCall& c) {
                const FormId keyword = c.arguments.empty() ? 0 : formOf(c.arguments[0]);
                return result(PapyrusValue::makeBool(keyword != 0 && mData.hasKeyword(formOf(c.self), keyword)));
            });
            reg("Actor", "AddSpell", false, [this](const NativeCall& c) {
                const FormId spell = c.arguments.empty() ? 0 : formOf(c.arguments[0]);
                const FormId actor = formOf(c.self);
                const bool added = spell != 0 && actor != 0 && mSpells.emplace(actor, spell).second;
                if (added)
                    ++mSpellRevision; // task 0237
                return result(PapyrusValue::makeBool(added));
            });
            reg("ObjectReference", "GetBaseObject", false, [this](const NativeCall& c) {
                const FormId base = baseOfReference(formOf(c.self));
                return result(base != 0 ? PapyrusValue::makeForm(intern("Form"), base) : PapyrusValue::makeNone());
            });
            reg("ObjectReference", "Activate", false, [this](const NativeCall& c) {
                const FormId target = formOf(c.self);
                const FormId activator = c.arguments.empty() ? 0 : formOf(c.arguments[0]);
                mLastTarget = target; // task 0237
                mLastActor = activator;
                for (const InstanceId instance : instancesOf(target))
                    (void)sendEvent(instance, "OnActivate", { formValue(activator) });
                return result(PapyrusValue::makeBool(true));
            });
            // Quest
            // SetCurrentStageID is an alias of SetStage in the game (task 0181 slice 2b, the Creation Kit wiki through search
            // summaries, toddhow's approval of 2026-11-15): done mark, fragment and OnStageSet.
            const auto setStageNative = [this](const NativeCall& c) {
                return result(
                    PapyrusValue::makeBool(setStage(formOf(c.self), static_cast<std::uint32_t>(argInt(c, 0)))));
            };
            reg("Quest", "SetStage", false, setStageNative);
            const auto currentStage = [this](const NativeCall& c) {
                return result(PapyrusValue::makeInt(
                    mState != nullptr ? static_cast<std::int32_t>(mState->stage(formOf(c.self))) : 0));
            };
            reg("Quest", "GetStage", false, currentStage);
            reg("Quest", "GetCurrentStageID", false, currentStage);
            const auto stageDone = [this](const NativeCall& c) {
                return result(PapyrusValue::makeBool(
                    mState != nullptr && mState->stageDone(formOf(c.self), static_cast<std::uint32_t>(argInt(c, 0)))));
            };
            reg("Quest", "GetStageDone", false, stageDone);
            reg("Quest", "IsStageDone", false, stageDone);
            reg("Quest", "SetCurrentStageID", false, setStageNative);
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
                if (mScenes != nullptr)
                    mScenes->questStopped(formOf(c.self));
                return NativeResult{};
            });
            // task 0194: scenes. A start runs after the calling script returned (it runs fragments of scripts that may
            // not be loaded yet)
            const auto sceneStart = [this](bool force) {
                return [this, force](const NativeCall& c) {
                    const FormId scene = formOf(c.self);
                    if (mScenes == nullptr)
                        return result(PapyrusValue::makeBool(false));
                    afterNative([this, scene, force] { (void)mScenes->startScene(scene, force); });
                    return result(PapyrusValue::makeBool(true));
                };
            };
            reg("Scene", "Start", false, sceneStart(false));
            reg("Scene", "ForceStart", false, sceneStart(true));
            reg("Scene", "Stop", false, [this](const NativeCall& c) {
                if (mScenes != nullptr)
                    (void)mScenes->stopScene(formOf(c.self));
                return NativeResult{};
            });
            reg("Scene", "IsPlaying", false, [this](const NativeCall& c) {
                return result(PapyrusValue::makeBool(mScenes != nullptr && mScenes->scenePlaying(formOf(c.self))));
            });
            const auto objective = [this](bool completed) {
                return [this, completed](const NativeCall& c) {
                    const auto key = std::make_pair(formOf(c.self), static_cast<std::uint32_t>(argInt(c, 0)));
                    const bool on = c.arguments.size() > 1 ? argInt(c, 1) != 0 : true;
                    (completed ? mObjectivesCompleted : mObjectivesDisplayed)[key] = on;
                    ++mObjectiveRevision;
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
            reg("TopicInfo", "GetOwningQuest", false, [this](const NativeCall& c) {
                return result(questValue(mData.questOfInfo(formOf(c.self))));
            });
            // Task 0187: Keyword.SendStoryEvent(akLoc, akRef1, akRef2, aiValue1, aiValue2): the keyword is the event's K1
            const auto sendStory = [this](const NativeCall& c) {
                StoryEvent event;
                event.mType = "SCPT";
                event.mKeyword = formOf(c.self);
                event.mLocation1 = c.arguments.size() > 0 ? formOf(c.arguments[0]) : 0;
                event.mRef1 = c.arguments.size() > 1 ? formOf(c.arguments[1]) : 0;
                event.mRef2 = c.arguments.size() > 2 ? formOf(c.arguments[2]) : 0;
                event.mValue1 = argInt(c, 3);
                event.mValue2 = argInt(c, 4);
                return result(PapyrusValue::makeBool(raiseStory(event) != 0));
            };
            reg("Keyword", "SendStoryEvent", false, sendStory);
            reg("Keyword", "SendStoryEventAndWait", false, sendStory);
            // Alias and ReferenceAlias
            reg("Alias", "GetOwningQuest", false, [this](const NativeCall& c) {
                FormId quest = 0;
                std::uint32_t alias = 0;
                return result(aliasOf(formOf(c.self), quest, alias) ? questValue(quest) : PapyrusValue::makeNone());
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
                    AliasFill fill = mState->aliasFill(quest, alias);
                    if ((!fill.mKnown || fill.mReference == 0) && mFiller != nullptr && mState->running(quest))
                    {
                        // Task 0187: an alias nobody filled at the start is filled when a script asks (once)
                        if (mFiller->fillAlias(quest, alias, *mState))
                            ++mCounters.mAliasesFilled;
                        fill = mState->aliasFill(quest, alias);
                    }
                    if (fill.mKnown && fill.mReference != 0)
                        return result(formValue(fill.mReference));
                }
                return result(PapyrusValue::makeNone());
            };
            reg("ReferenceAlias", "GetReference", false, reference);
            reg("ReferenceAlias", "GetActorReference", false, reference);
            // task 0239: the spelling DialogueFollowerScript uses
            reg("ReferenceAlias", "GetActorRef", false, reference);
            // Task 0217: a location alias keeps its location in the fill's reference word (decision 5 of task 0211), so
            // LocationAlias.GetLocation reads it the way GetReference does
            reg("LocationAlias", "GetLocation", false, reference);
            reg("ReferenceAlias", "ForceRefTo", false, [this](const NativeCall& c) {
                FormId quest = 0;
                std::uint32_t alias = 0;
                if (aliasOf(formOf(c.self), quest, alias))
                {
                    const FormId now = c.arguments.empty() ? 0 : formOf(c.arguments[0]);
                    const auto before = mAliasForced.find({ quest, alias });
                    const FormId old = before != mAliasForced.end() ? before->second : 0;
                    mAliasForced[{ quest, alias }] = now;
                    aliasFactionChange(quest, alias, old, now); // task 0239
                }
                return NativeResult{};
            });
            reg("LocationAlias", "ForceLocationTo", false, [this](const NativeCall& c) {
                FormId quest = 0;
                std::uint32_t alias = 0;
                if (aliasOf(formOf(c.self), quest, alias))
                    mAliasForced[{ quest, alias }] = c.arguments.empty() ? 0 : formOf(c.arguments[0]);
                return NativeResult{};
            });
            reg("LocationAlias", "Clear", false, [this](const NativeCall& c) {
                FormId quest = 0;
                std::uint32_t alias = 0;
                if (aliasOf(formOf(c.self), quest, alias))
                    mAliasForced.erase({ quest, alias });
                return NativeResult{};
            });
            // Task 0217: what scripts store on a location for a keyword; the story manager's conditions read it
            //   (fn_606,
            // fn_651). Location.SetKeywordData(akKeyword, afData) and GetKeywordData(akKeyword): RECALLED-VERIFY.
            reg("Location", "SetKeywordData", false, [this](const NativeCall& c) {
                if (mKeywordData != nullptr && c.arguments.size() > 1)
                    (void)mKeywordData->set(
                        formOf(c.self), formOf(c.arguments[0]), static_cast<float>(argNumber(c, 1)));
                return NativeResult{};
            });
            reg("Location", "GetKeywordData", false, [this](const NativeCall& c) {
                const float value = mKeywordData != nullptr && !c.arguments.empty()
                    ? mKeywordData->get(formOf(c.self), formOf(c.arguments[0]))
                    : 0.f;
                return result(PapyrusValue::makeFloat(value));
            });
            reg("ReferenceAlias", "Clear", false, [this](const NativeCall& c) {
                FormId quest = 0;
                std::uint32_t alias = 0;
                if (aliasOf(formOf(c.self), quest, alias))
                {
                    const auto before = mAliasForced.find({ quest, alias });
                    const FormId old = before != mAliasForced.end() ? before->second : 0;
                    mAliasForced.erase({ quest, alias });
                    aliasFactionChange(quest, alias, old, 0); // task 0239
                }
                return NativeResult{};
            });
            registerInventoryNatives();
            registerActorNatives();
            registerCarriageNatives();
            registerFollowerNatives();
        }

        // ---- task 0239: what DialogueFollowerScript, HirelingQuest and the follower topics fragments call ----------
        // The effects live in `FollowerState` (not owned; null: every native here is tolerant). Signatures are the
        // Creation Kit wiki pages (recalled, RECALLED-VERIFY): Actor.AddToFaction(akFaction),
        // RemoveFromFaction(akFaction), IsInFaction(akFaction) -> Bool,
        // SetPlayerTeammate(abTeammate = true, abCanDoFavor = true),
        // IsPlayerTeammate() -> Bool, SetRelationshipRank(akOther, aiRank), GetRelationshipRank(akOther) -> Int,
        // SetAV(asValueName, afValue) (the same as SetActorValue), GetActorBase() -> ActorBase. The scripts also call
        // StopCombatAlarm, EvaluatePackage, RegisterForUpdateGameTime, Message.Show and ActorBase compares: those stay
        // tolerant (a message is not shown; a waiting follower is not dismissed after 72 game hours).
        void aliasFactionChange(FormId quest, std::uint32_t alias, FormId oldReference, FormId newReference)
        {
            std::vector<FormId> factions;
            if (mFollowers == nullptr || !mData.aliasFactions(quest, alias, factions))
                return;
            if (oldReference != 0 && oldReference != newReference)
                for (const FormId faction : factions)
                    (void)mFollowers->removeFromFaction(oldReference, faction);
            if (newReference != 0)
                for (const FormId faction : factions)
                    (void)mFollowers->addToFaction(newReference, faction);
        }

        void registerFollowerNatives()
        {
            const auto factionArgument = [this](const NativeCall& c) {
                return c.arguments.empty() ? FormId(0) : formOf(c.arguments[0]);
            };
            reg("Actor", "AddToFaction", false, [this, factionArgument](const NativeCall& c) {
                if (mFollowers != nullptr)
                    (void)mFollowers->addToFaction(formOf(c.self), factionArgument(c));
                return NativeResult{};
            });
            reg("Actor", "RemoveFromFaction", false, [this, factionArgument](const NativeCall& c) {
                if (mFollowers != nullptr)
                    (void)mFollowers->removeFromFaction(formOf(c.self), factionArgument(c));
                return NativeResult{};
            });
            reg("Actor", "IsInFaction", false, [this, factionArgument](const NativeCall& c) {
                bool in = false;
                if (mFollowers != nullptr)
                    if (const std::optional<float> rank = mFollowers->factionRank(formOf(c.self), factionArgument(c)))
                        in = *rank >= 0.f;
                return result(PapyrusValue::makeBool(in));
            });
            // Actor.AddToFaction is a SCRIPT function in the game's Actor.pex (IsInFaction, then SetFactionRank(f,
            // 0)): this is the native that makes a script's membership real. A rank below 0 is "not a member".
            reg("Actor", "SetFactionRank", false, [this, factionArgument](const NativeCall& c) {
                if (mFollowers != nullptr)
                {
                    const std::int32_t rank = argInt(c, 1);
                    if (rank < 0)
                        (void)mFollowers->removeFromFaction(formOf(c.self), factionArgument(c));
                    else
                        (void)mFollowers->addToFaction(formOf(c.self), factionArgument(c), rank);
                }
                return NativeResult{};
            });
            reg("Actor", "GetFactionRank", false, [this, factionArgument](const NativeCall& c) {
                std::int32_t rank = -2; // not a member
                if (mFollowers != nullptr)
                    if (const std::optional<float> set = mFollowers->factionRank(formOf(c.self), factionArgument(c)))
                        rank = *set >= 0.f ? static_cast<std::int32_t>(*set) : -2;
                return result(PapyrusValue::makeInt(rank));
            });
            reg("Actor", "SetPlayerTeammate", false, [this](const NativeCall& c) {
                const bool on = c.arguments.empty() || argNumber(c, 0) != 0.0;
                if (mFollowers != nullptr)
                    (void)mFollowers->setTeammate(formOf(c.self), on);
                return NativeResult{};
            });
            reg("Actor", "IsPlayerTeammate", false, [this](const NativeCall& c) {
                return result(
                    PapyrusValue::makeBool(mFollowers != nullptr && mFollowers->isPlayerTeammate(formOf(c.self))));
            });
            reg("Actor", "SetRelationshipRank", false, [this](const NativeCall& c) {
                if (mFollowers != nullptr && !c.arguments.empty())
                    (void)mFollowers->setRelationshipRank(formOf(c.self), formOf(c.arguments[0]), argInt(c, 1));
                return NativeResult{};
            });
            reg("Actor", "GetRelationshipRank", false, [this](const NativeCall& c) {
                float rank = 0.f; // no record and nothing set: acquaintances
                if (mFollowers != nullptr && !c.arguments.empty())
                    if (const std::optional<float> set
                        = mFollowers->relationshipRank(formOf(c.self), formOf(c.arguments[0])))
                        rank = *set;
                return result(PapyrusValue::makeInt(static_cast<std::int32_t>(rank)));
            });
            reg("Actor", "SetAV", false, [this](const NativeCall& c) {
                const std::string key = !c.arguments.empty() && c.arguments[0].kind == PapyrusKind::String
                    ? avKey(c.arguments[0].text)
                    : std::string();
                double now = 0.0, max = 0.0;
                const FormId actor = formOf(c.self);
                (void)avNow(actor, key, now, max);
                avMove(actor, key, argNumber(c, 1) - now);
                return NativeResult{};
            });
            reg("Actor", "GetActorBase", false,
                [this](const NativeCall& c) { return result(formValue(mData.baseOf(formOf(c.self)))); });
        }

        // ---- task 0223: what the carriage scripts (CarriageSystemScript, CarriageDriverScript) call ------------------
        // Game.FastTravel(akDestination) leaves the travel to the pack's travel service (a global script teleports the
        // player and passes the game clock). The driver's script pays the fee itself (RemoveItem(Gold, CarriageCost.value
        // as int)), so GlobalVariable.GetValue answers the GLOB's value. The fades, the player controls, the vehicle, the
        // driver's idles and talk are no-ops that only count (the ride has no animation), the cart seat counts as taken
        // at once (the player "sits" so the script goes straight to its travel), and nobody is in the BYOH faction or in a
        // fight (Actor.IsInCombat stays tolerant: it answers false anyway, and an existing test names it as its example of a
        // tolerant native). Signatures are the Creation Kit wiki's (recalled, RECALLED-VERIFY).
        void registerCarriageNatives()
        {
            reg("Game", "FastTravel", true, [this](const NativeCall& c) {
                const FormId destination = c.arguments.empty() ? 0 : formOf(c.arguments[0]);
                if (mFastTravel && destination != 0)
                    (void)mFastTravel(destination);
                return NativeResult{};
            });
            reg("GlobalVariable", "GetValue", false, [this](const NativeCall& c) {
                const FormId global = formOf(c.self);
                const auto set = mGlobalValues.find(global);
                float value = 0.f;
                if (set != mGlobalValues.end())
                    value = set->second;
                else if (const std::optional<float> kept
                    = mFollowers != nullptr ? mFollowers->globalValue(global) : std::optional<float>())
                    value = *kept; // task 0239: kept in the follower state, which a save restores
                else
                    (void)mData.globalValue(global, value);
                return result(PapyrusValue::makeFloat(value));
            });
            reg("GlobalVariable", "SetValue", false, [this](const NativeCall& c) {
                const FormId global = formOf(c.self);
                if (global != 0 && (mGlobalValues.size() < kMaxAvOffsets || mGlobalValues.count(global) != 0))
                    mGlobalValues[global] = static_cast<float>(argNumber(c, 0));
                if (mFollowers != nullptr)
                    (void)mFollowers->setGlobal(global, static_cast<float>(argNumber(c, 0))); // task 0239
                return NativeResult{};
            });
            const auto nothing = [](const NativeCall&) { return NativeResult{}; };
            for (const char* name : { "DisablePlayerControls", "EnablePlayerControls", "FadeOutGame", "ForceFirstPerson",
                     "ShowFirstPersonGeometry", "SetInChargen" })
                reg("Game", name, true, nothing);
            for (const char* name : { "Apply", "PopTo", "Remove" })
                reg("ImageSpaceModifier", name, false, nothing);
            for (const char* name : { "SetVehicle", "SetRestrained", "Say", "EvaluatePackage" })
                reg("Actor", name, false, nothing);
            reg("ObjectReference", "Say", false, nothing); // the declaring class of Say in the game's scripts
            reg("Actor", "PlayIdle", false, [](const NativeCall&) { return result(PapyrusValue::makeBool(true)); });
            reg("ObjectReference", "IsFurnitureInUse", false,
                [](const NativeCall&) { return result(PapyrusValue::makeBool(true)); });
        }

        // ---- task 0198: inventory, actor values, deaths ------------------------------------------------------------
        // Signatures from the Creation Kit wiki function pages (recalled, RECALLED-VERIFY): AddItem(akItemToAdd,
        // aiCount = 1, abSilent = false), RemoveItem(akItemToRemove, aiCount = 1, abSilent = false, akOtherContainer =
        // None), GetItemCount(akItem), RemoveAllItems(akTransferTo = None, ...). The hook (the pack's inventory) raises
        // OnItemAdded and OnItemRemoved itself, because the inventory windows change the same lists.

        std::int32_t countArgument(const NativeCall& c, std::size_t i) const
        {
            return c.arguments.size() > i ? argInt(c, i) : 1;
        }

        void registerInventoryNatives()
        {
            reg("ObjectReference", "AddItem", false, [this](const NativeCall& c) {
                const FormId owner = formOf(c.self);
                const FormId item = c.arguments.empty() ? 0 : formOf(c.arguments[0]);
                const std::int32_t count = countArgument(c, 1);
                if (mHooks != nullptr && owner != 0 && item != 0 && count > 0)
                    (void)mHooks->addItem(owner, item, count);
                return NativeResult{};
            });
            reg("ObjectReference", "RemoveItem", false, [this](const NativeCall& c) {
                const FormId owner = formOf(c.self);
                const FormId item = c.arguments.empty() ? 0 : formOf(c.arguments[0]);
                const std::int32_t count = countArgument(c, 1);
                const FormId other = c.arguments.size() > 3 ? formOf(c.arguments[3]) : 0;
                if (mHooks != nullptr && owner != 0 && item != 0 && count > 0)
                {
                    if (other != 0)
                        (void)mHooks->transferItem(owner, other, item, count);
                    else
                        (void)mHooks->removeItem(owner, item, count);
                }
                return NativeResult{};
            });
            reg("ObjectReference", "GetItemCount", false, [this](const NativeCall& c) {
                const FormId item = c.arguments.empty() ? 0 : formOf(c.arguments[0]);
                const std::int32_t held = mHooks != nullptr ? mHooks->itemCount(formOf(c.self), item) : -1;
                return result(PapyrusValue::makeInt(std::max(held, 0)));
            });
            reg("ObjectReference", "RemoveAllItems", false, [this](const NativeCall& c) {
                const FormId owner = formOf(c.self);
                const FormId target = c.arguments.empty() ? 0 : formOf(c.arguments[0]);
                if (mHooks != nullptr && owner != 0)
                    (void)mHooks->removeAllItems(owner, target);
                return NativeResult{};
            });
            for (const char* cls : { "ObjectReference", "ReferenceAlias" })
            {
                reg(cls, "AddInventoryEventFilter", false, [this](const NativeCall& c) {
                    const FormId key = formOf(c.self);
                    const FormId filter = c.arguments.empty() ? 0 : formOf(c.arguments[0]);
                    if (key != 0 && filter != 0 && (mInvFilters.size() < kMaxAvOffsets || mInvFilters.count(key) != 0))
                    {
                        std::set<FormId>& set = mInvFilters[key];
                        if (set.size() < 256)
                            set.insert(filter);
                    }
                    return NativeResult{};
                });
                reg(cls, "RemoveInventoryEventFilter", false, [this](const NativeCall& c) {
                    const auto it = mInvFilters.find(formOf(c.self));
                    if (it != mInvFilters.end() && !c.arguments.empty())
                    {
                        it->second.erase(formOf(c.arguments[0]));
                        if (it->second.empty())
                            mInvFilters.erase(it);
                    }
                    return NativeResult{};
                });
            }
            reg("ObjectReference", "GetLinkedRef", false, [this](const NativeCall& c) {
                const FormId keyword = c.arguments.empty() ? 0 : formOf(c.arguments[0]);
                return result(formValue(mData.linkedRef(formOf(c.self), keyword)));
            });
            reg("ObjectReference", "GetDistance", false, [this](const NativeCall& c) {
                double a[3] = { 0, 0, 0 }, b[3] = { 0, 0, 0 };
                const FormId other = c.arguments.empty() ? 0 : formOf(c.arguments[0]);
                if (!positionOf(formOf(c.self), a) || !positionOf(other, b))
                    return result(PapyrusValue::makeFloat(0.f));
                const double dx = a[0] - b[0], dy = a[1] - b[1], dz = a[2] - b[2];
                return result(PapyrusValue::makeFloat(static_cast<float>(std::sqrt(dx * dx + dy * dy + dz * dz))));
            });
        }

        void registerActorNatives()
        {
            const auto valueName = [](const NativeCall& c) {
                return !c.arguments.empty() && c.arguments[0].kind == PapyrusKind::String
                    ? avKey(c.arguments[0].text)
                    : std::string();
            };
            reg("Actor", "GetActorValue", false, [this, valueName](const NativeCall& c) {
                double now = 0.0, max = 0.0;
                (void)avNow(formOf(c.self), valueName(c), now, max);
                return result(PapyrusValue::makeFloat(static_cast<float>(now)));
            });
            reg("Actor", "GetBaseActorValue", false, [this, valueName](const NativeCall& c) {
                double now = 0.0, max = 0.0;
                if (mHooks == nullptr || !mHooks->actorValue(formOf(c.self), valueName(c), now, max))
                    return result(PapyrusValue::makeFloat(0.f));
                return result(PapyrusValue::makeFloat(static_cast<float>(max > 0.0 ? max : now)));
            });
            reg("Actor", "GetActorValuePercentage", false, [this, valueName](const NativeCall& c) {
                double now = 0.0, max = 0.0;
                (void)avNow(formOf(c.self), valueName(c), now, max);
                return result(PapyrusValue::makeFloat(max > 0.0 ? static_cast<float>(now / max) : 0.f));
            });
            reg("Actor", "SetActorValue", false, [this, valueName](const NativeCall& c) {
                double now = 0.0, max = 0.0;
                const FormId actor = formOf(c.self);
                const std::string key = valueName(c);
                (void)avNow(actor, key, now, max);
                avMove(actor, key, argNumber(c, 1) - now);
                return NativeResult{};
            });
            const auto change = [this, valueName](double sign) {
                return [this, valueName, sign](const NativeCall& c) {
                    avMove(formOf(c.self), valueName(c), sign * argNumber(c, 1));
                    return NativeResult{};
                };
            };
            reg("Actor", "ModActorValue", false, change(1.0));
            reg("Actor", "RestoreActorValue", false, change(1.0));
            reg("Actor", "DamageActorValue", false, change(-1.0));
            reg("Actor", "IsDead", false,
                [this](const NativeCall& c) { return result(PapyrusValue::makeBool(actorDead(formOf(c.self)))); });
            reg("Actor", "Kill", false, [this](const NativeCall& c) {
                const FormId actor = formOf(c.self);
                if (actor != 0 && actor != kPlayerReference && !actorDead(actor) && mHooks != nullptr
                    && mHooks->killActor(actor))
                    mKilling.insert(actor);
                return NativeResult{};
            });
            // Task 0205: a word wall's script teaches words of power (the dragon souls unlock them by hand).
            reg("Game", "TeachWord", true, [this](const NativeCall& c) {
                const FormId word = c.arguments.empty() ? 0 : formOf(c.arguments[0]);
                if (mHooks != nullptr && word != 0)
                    (void)mHooks->teachWord(word);
                return NativeResult{};
            });
            reg("Game", "UnlockWord", true, [this](const NativeCall& c) {
                const FormId word = c.arguments.empty() ? 0 : formOf(c.arguments[0]);
                if (mHooks != nullptr && word != 0)
                    (void)mHooks->unlockWord(word);
                return NativeResult{};
            });
            reg("Game", "IsWordUnlocked", true, [this](const NativeCall& c) {
                const FormId word = c.arguments.empty() ? 0 : formOf(c.arguments[0]);
                return result(PapyrusValue::makeBool(mHooks != nullptr && word != 0 && mHooks->isWordUnlocked(word)));
            });
            reg("Game", "GetFormFromFile", true, [this](const NativeCall& c) {
                const std::string file = c.arguments.size() > 1 && c.arguments[1].kind == PapyrusKind::String
                    ? lower(c.arguments[1].text)
                    : std::string();
                const FormId form = file.empty() ? 0 : mData.formFromFile(file, static_cast<std::uint32_t>(argInt(c, 0)));
                return result(formValue(form));
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
            runReplays();
            fireTimers();
            for (int round = 0; round < 8; ++round)
            {
                const openeden::script::TickResult r = mRuntime->tick(mTime);
                mCounters.mStacksRun += r.stacksRun;
                for (const openeden::script::ErrorReport& error : r.errors)
                {
                    ++mCounters.mScriptErrors;
                    say(true, "Papyrus: " + error.format());
                }
                // work a native left for after it (a quest the stage change attached) queues events: run them now
                if (!runDeferred())
                    break;
            }
            mInTick = false;
        }

        struct Replay
        {
            FormId mMessage = 0;
            FormId mTarget = 0;
            FormId mActor = 0;
        };
        static constexpr std::size_t kMaxAskedMessages = 16;
        static constexpr std::size_t kMaxRecordedPerks = 256;

        // Task 0237: a message the player answered runs its activation again once no stack waits.
        void runReplays()
        {
            if (mReplays.empty() || mRuntime->suspendedStacks() != 0)
                return;
            std::vector<Replay> replays;
            replays.swap(mReplays);
            for (const Replay& r : replays)
            {
                mLastTarget = r.mTarget;
                mLastActor = r.mActor;
                for (const InstanceId instance : instancesOf(r.mTarget))
                    (void)sendEvent(instance, "OnActivate", { formValue(r.mActor) });
            }
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
        StoryStarted mStoryStarted; ///< task 0217
        EnableChanged mEnableChanged;
        const QuestIndex* mIndex = nullptr;
        QuestState* mState = nullptr;
        QuestVmadLookup mVmads;
        QuestAliasFiller* mFiller = nullptr; ///< task 0187
        int mNativeDepth = 0; ///< > 0 while a native runs: a script cannot be loaded then (task 0187)
        std::vector<std::function<void()>> mDeferred; ///< work that needs a script loaded, run after the native returned
        bool mPreloaded = false;
        SceneControl* mScenes = nullptr; ///< task 0194
        StoryEventSink mStorySink; ///< task 0187
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
        std::uint64_t mTolerantBase = 0; ///< tolerant calls counted before the state that was restored
        std::map<FormId, bool> mEnabled;
        std::map<FormId, std::array<double, 3>> mPositions; ///< MoveTo, SetPosition (also sent to the world, task 0202)
        std::map<FormId, std::array<double, 3>> mAngles; ///< SetAngle, MoveTo with matched rotation (radians)
        std::map<FormId, PapyrusHost::CreatedObject> mCreated; ///< PlaceAtMe
        std::set<std::pair<FormId, FormId>> mSpells; ///< Actor.AddSpell (the pack reads it: spellsOf)
        // task 0237
        std::uint64_t mSpellRevision = 0;
        std::set<std::pair<FormId, FormId>> mPerks; ///< Actor.AddPerk (task 0241: saved, state version 3)
        std::uint64_t mPerkRevision = 0;
        std::vector<PapyrusHost::SpellCast> mCasts;
        PapyrusHost::MessageSink mMessageSink;
        std::map<FormId, std::int32_t> mAnswers; ///< message -> the button answered, until Message.Show takes it
        std::vector<Replay> mAsked; ///< message boxes asked and not answered yet
        std::vector<Replay> mReplays; ///< answered: the activation runs again
        FormId mLastTarget = 0;
        FormId mLastActor = 0;
        std::vector<std::pair<FormId, std::uint32_t>> mAliasKeys;
        std::map<std::pair<FormId, std::uint32_t>, std::uint32_t> mAliasByKey;
        std::map<std::pair<FormId, std::uint32_t>, FormId> mAliasForced;
        std::map<std::pair<FormId, std::uint32_t>, bool> mObjectivesDisplayed;
        std::map<std::pair<FormId, std::uint32_t>, bool> mObjectivesCompleted;
        std::uint64_t mObjectiveRevision = 0;
        std::string mStringScratch;
        // ---- task 0198
        PapyrusWorldHooks* mHooks = nullptr;
        LocationKeywordData* mKeywordData = nullptr; ///< task 0217
        FollowerState* mFollowers = nullptr; ///< task 0239
        PapyrusHost::FastTravelSink mFastTravel; ///< task 0223
        bool mTraceNatives = std::getenv("OPENEDEN_PAPYRUS_NATIVE_TRACE") != nullptr; ///< a developer aid: one line a native
        std::uint64_t mTraceCount = 0;
        static constexpr std::uint64_t kMaxTracedNatives = 100000;
        std::map<FormId, float> mGlobalValues; ///< task 0223: values scripts set (SetValue); the data gives the rest
        std::deque<std::string> mNotes;
        std::uint64_t mNoteCount = 0;
        std::map<std::pair<FormId, std::string>, double> mAvOffsets; ///< what scripts did to an actor value
        std::set<FormId> mDead; ///< actors that died (OnDeath was raised)
        std::set<FormId> mKilling; ///< Actor.Kill asked, the death not yet reported
        /// AddInventoryEventFilter: per script instance owner (a form id, or an alias handle) the items its events are
        /// limited to. No entry: every item.
        std::map<FormId, std::set<FormId>> mInvFilters;

        FormId instanceKey(const InstanceInfo& info)
        {
            return info.mIsAlias ? aliasHandle(info.mForm, info.mAlias) : info.mForm;
        }

        bool inventoryEventWanted(InstanceId instance, FormId item)
        {
            const auto info = mInstances.find(instance);
            if (info == mInstances.end())
                return false;
            const auto filter = mInvFilters.find(instanceKey(info->second));
            if (filter == mInvFilters.end())
                return true;
            bool any = false;
            for (const FormId f : filter->second)
            {
                if (mHooks == nullptr || !mHooks->isItem(f))
                    continue; // a form list or something unknown: cannot be expanded, so it restricts nothing
                if (f == item)
                    return true;
                any = true;
            }
            return !any;
        }

        [[nodiscard]] bool actorDead(FormId actor) const
        {
            return mDead.count(actor) != 0 || mKilling.count(actor) != 0
                || (mHooks != nullptr && mHooks->isDead(actor)) || mData.initiallyDead(actor);
        }

        // The reference an alias holds now: a script's ForceRefTo, else what the quest state filled (never fills).
        FormId aliasReferenceOf(FormId quest, std::uint32_t alias) const
        {
            const auto forced = mAliasForced.find({ quest, alias });
            if (forced != mAliasForced.end())
                return forced->second;
            if (mState != nullptr)
            {
                const AliasFill fill = mState->aliasFill(quest, alias);
                if (fill.mKnown)
                    return fill.mReference;
            }
            return 0;
        }

        // The instances on the reference and on every reference alias that holds it (events go to both).
        std::vector<InstanceId> instancesHolding(FormId reference) const
        {
            std::vector<InstanceId> out = instancesOf(reference);
            if (reference == 0)
                return out;
            for (const auto& [key, ids] : mByAlias)
                if (aliasReferenceOf(key.first, key.second) == reference)
                    out.insert(out.end(), ids.begin(), ids.end());
            return out;
        }

        // The number an actor value has now: the pack's current value plus what scripts did to it (never below 0, never
        // above the pack's maximum when it has one).
        bool avNow(FormId actor, const std::string& key, double& current, double& maximum) const
        {
            current = 0.0;
            maximum = 0.0;
            const bool known = mHooks != nullptr && mHooks->actorValue(actor, key, current, maximum);
            const auto offset = mAvOffsets.find({ actor, key });
            if (offset != mAvOffsets.end())
                current += offset->second;
            if (known && maximum > 0.0)
                current = std::min(current, maximum);
            current = std::max(current, 0.0);
            return known || offset != mAvOffsets.end();
        }

        // Moves an actor value by `delta` (health goes to the actor itself when the pack can write it).
        void avMove(FormId actor, const std::string& key, double delta)
        {
            double current = 0.0, maximum = 0.0;
            const bool known = avNow(actor, key, current, maximum);
            if (key == "health" && known && mHooks != nullptr && mHooks->setHealth(actor, current + delta))
            {
                // the pack holds the number now: nothing of ours stays
                mAvOffsets.erase({ actor, key });
                return;
            }
            double target = current + delta;
            if (maximum > 0.0)
                target = std::min(target, maximum);
            target = std::max(target, 0.0);
            double base = 0.0, ignore = 0.0;
            if (mHooks == nullptr || !mHooks->actorValue(actor, key, base, ignore))
                base = 0.0;
            if (mAvOffsets.size() >= kMaxAvOffsets && mAvOffsets.count({ actor, key }) == 0)
                return;
            mAvOffsets[{ actor, key }] = target - base;
        }
    };

    namespace
    {
        // ---- the host's state as bytes (task 0181) -----------------------------------------------------------------
        // Little endian, canonical (maps and sets in key order), ended by an FNV-1a 64 checksum of everything before it.
        constexpr std::uint32_t kStateMagic = 0x31534850u; // "PHS1"
        // 4 (task 0241) adds the perks; 3 (task 0202) adds created references and angles; 2 (task 0198) adds actor
        // value changes and deaths; 1, 2 and 3 are still read
        constexpr std::uint32_t kStateVersion = 4;
        constexpr std::uint32_t kMaxEntries = 1u << 22; // a hostile count never allocates more than the bytes allow

        std::uint64_t fnv1a(const std::uint8_t* data, std::size_t size)
        {
            std::uint64_t hash = 0xcbf29ce484222325ull;
            for (std::size_t i = 0; i < size; ++i)
            {
                hash ^= data[i];
                hash *= 0x100000001b3ull;
            }
            return hash;
        }

        class ByteWriter
        {
        public:
            void u8(std::uint8_t v) { mBytes.push_back(v); }
            void u32(std::uint32_t v)
            {
                for (int i = 0; i < 4; ++i)
                    mBytes.push_back(static_cast<std::uint8_t>(v >> (8 * i)));
            }
            void u64(std::uint64_t v)
            {
                for (int i = 0; i < 8; ++i)
                    mBytes.push_back(static_cast<std::uint8_t>(v >> (8 * i)));
            }
            void f64(double v) { u64(std::bit_cast<std::uint64_t>(v)); }
            void str(const std::string& s)
            {
                u32(static_cast<std::uint32_t>(s.size()));
                mBytes.insert(mBytes.end(), s.begin(), s.end());
            }
            void raw(const std::vector<std::uint8_t>& bytes)
            {
                u32(static_cast<std::uint32_t>(bytes.size()));
                mBytes.insert(mBytes.end(), bytes.begin(), bytes.end());
            }
            std::vector<std::uint8_t> finish()
            {
                const std::uint64_t sum = fnv1a(mBytes.data(), mBytes.size());
                u64(sum);
                return std::move(mBytes);
            }

        private:
            std::vector<std::uint8_t> mBytes;
        };

        class ByteReader
        {
        public:
            explicit ByteReader(std::span<const std::uint8_t> data)
                : mData(data)
            {
            }
            [[nodiscard]] bool failed() const { return mFailed; }
            [[nodiscard]] std::size_t remaining() const { return mData.size() - mPos; }
            std::uint8_t u8()
            {
                if (!take(1))
                    return 0;
                return mData[mPos - 1];
            }
            std::uint32_t u32()
            {
                if (!take(4))
                    return 0;
                std::uint32_t v = 0;
                for (int i = 0; i < 4; ++i)
                    v |= static_cast<std::uint32_t>(mData[mPos - 4 + i]) << (8 * i);
                return v;
            }
            std::uint64_t u64()
            {
                if (!take(8))
                    return 0;
                std::uint64_t v = 0;
                for (int i = 0; i < 8; ++i)
                    v |= static_cast<std::uint64_t>(mData[mPos - 8 + i]) << (8 * i);
                return v;
            }
            double f64() { return std::bit_cast<double>(u64()); }
            std::string str()
            {
                const std::uint32_t n = u32();
                if (!take(n))
                    return {};
                return std::string(reinterpret_cast<const char*>(mData.data() + mPos - n), n);
            }
            std::vector<std::uint8_t> raw()
            {
                const std::uint32_t n = u32();
                if (!take(n))
                    return {};
                return std::vector<std::uint8_t>(mData.begin() + static_cast<std::ptrdiff_t>(mPos - n),
                    mData.begin() + static_cast<std::ptrdiff_t>(mPos));
            }
            // A count of items that each need at least \a minBytes: it must fit in what is left.
            std::uint32_t count(std::size_t minBytes)
            {
                const std::uint32_t n = u32();
                if (n > kMaxEntries || static_cast<std::uint64_t>(n) * minBytes > remaining())
                {
                    mFailed = true;
                    return 0;
                }
                return n;
            }

        private:
            bool take(std::size_t n)
            {
                if (mFailed || n > remaining())
                {
                    mFailed = true;
                    return false;
                }
                mPos += n;
                return true;
            }
            std::span<const std::uint8_t> mData;
            std::size_t mPos = 0;
            bool mFailed = false;
        };

        using PairKey = std::pair<FormId, std::uint32_t>;
    }

    std::vector<std::uint8_t> PapyrusHost::Impl::save() const
    {
        if (!mStarted || mInTick)
            return {};
        // Instance ids must be 1..n: the ledger is written in id order and a restore numbers instances the same way.
        InstanceId expected = 1;
        for (const auto& [id, info] : mInstances)
        {
            if (id != expected++)
                return {};
        }
        const openeden::script::SaveBlob blob = mRuntime->serialise();
        if (blob.formatVersion == 0 && blob.bytes.empty())
            return {};

        ByteWriter w;
        w.u32(kStateMagic);
        // version 4 only when perks are held: a state with none keeps the version 3 layout byte for byte
        w.u32(mPerks.empty() ? 3u : kStateVersion);
        w.f64(mTime);
        w.u64(mTimerSeq);
        const PapyrusHostCounters& c = mCounters;
        for (const std::uint64_t v : { c.mScriptsLoaded, c.mScriptsMissing, c.mScriptsRefused, c.mInstancesAttached,
                 c.mAttachRefused, c.mPropertiesSet, c.mPropertiesRejected, c.mPropertiesSkipped, c.mFragmentsRun,
                 c.mStageSets, c.mUpdatesFired, c.mTicks, c.mStacksRun, c.mScriptErrors })
            w.u64(v);
        for (const auto* map : { &c.mEvents, &c.mNatives })
        {
            w.u32(static_cast<std::uint32_t>(map->size()));
            for (const auto& [name, count] : *map)
            {
                w.str(name);
                w.u64(count);
            }
        }
        w.u32(static_cast<std::uint32_t>(mTolerant.size()));
        for (const std::string& name : mTolerant)
            w.str(name);
        w.u64(mTolerantBase + mRuntime->stats().unregisteredNativeCalls);
        w.u32(blob.formatVersion);
        w.raw(blob.bytes);

        w.u32(static_cast<std::uint32_t>(mInstances.size()));
        for (const auto& [id, info] : mInstances)
        {
            w.u8(info.mIsAlias ? 1 : 0);
            w.u32(info.mForm);
            w.u32(info.mAlias);
            w.u32(info.mOrdinal);
            w.str(info.mScript);
        }
        w.u32(static_cast<std::uint32_t>(mTimers.size()));
        for (const auto& [key, timer] : mTimers)
        {
            w.f64(key.first);
            w.u64(key.second);
            w.u32(timer.mInstance);
            w.f64(timer.mRepeat);
        }
        w.u32(static_cast<std::uint32_t>(mEnabled.size()));
        for (const auto& [ref, on] : mEnabled)
        {
            w.u32(ref);
            w.u8(on ? 1 : 0);
        }
        w.u32(static_cast<std::uint32_t>(mPositions.size()));
        for (const auto& [ref, xyz] : mPositions)
        {
            w.u32(ref);
            for (const double v : xyz)
                w.f64(v);
        }
        w.u32(static_cast<std::uint32_t>(mSpells.size()));
        for (const auto& [actor, spell] : mSpells)
        {
            w.u32(actor);
            w.u32(spell);
        }
        w.u32(static_cast<std::uint32_t>(mAliasKeys.size()));
        for (const PairKey& key : mAliasKeys)
        {
            w.u32(key.first);
            w.u32(key.second);
        }
        w.u32(static_cast<std::uint32_t>(mAliasForced.size()));
        for (const auto& [key, form] : mAliasForced)
        {
            w.u32(key.first);
            w.u32(key.second);
            w.u32(form);
        }
        for (const auto* map : { &mObjectivesDisplayed, &mObjectivesCompleted })
        {
            w.u32(static_cast<std::uint32_t>(map->size()));
            for (const auto& [key, on] : *map)
            {
                w.u32(key.first);
                w.u32(key.second);
                w.u8(on ? 1 : 0);
            }
        }
        w.u32(static_cast<std::uint32_t>(mQuestsAttached.size()));
        for (const FormId quest : mQuestsAttached)
            w.u32(quest);
        w.u32(static_cast<std::uint32_t>(mRefAttached.size()));
        for (const FormId ref : mRefAttached)
            w.u32(ref);
        w.u32(static_cast<std::uint32_t>(mRefClass.size()));
        for (const auto& [ref, cls] : mRefClass)
        {
            w.u32(ref);
            w.u8(cls == "Actor" ? 1 : 0);
        }
        std::vector<QuestState::SnapshotEntry> quests;
        if (mState != nullptr)
            quests = mState->snapshot();
        w.u32(static_cast<std::uint32_t>(quests.size()));
        for (const QuestState::SnapshotEntry& q : quests)
        {
            w.u32(q.mQuest);
            w.u8(q.mRunning ? 1 : 0);
            w.u8(q.mCompleted ? 1 : 0);
            w.u32(q.mStage);
            w.u32(static_cast<std::uint32_t>(q.mDone.size()));
            for (const std::uint32_t d : q.mDone)
                w.u32(d);
            w.u32(static_cast<std::uint32_t>(q.mAliases.size()));
            for (const auto& [alias, fill] : q.mAliases)
            {
                w.u32(alias);
                w.u8(fill.mKnown ? 1 : 0);
                w.u32(fill.mReference);
                w.u32(fill.mActor);
            }
        }
        w.u32(static_cast<std::uint32_t>(mAvOffsets.size()));
        for (const auto& [key, offset] : mAvOffsets)
        {
            w.u32(key.first);
            w.str(key.second);
            w.f64(offset);
        }
        w.u32(static_cast<std::uint32_t>(mDead.size()));
        for (const FormId actor : mDead)
            w.u32(actor);
        w.u32(static_cast<std::uint32_t>(mInvFilters.size()));
        for (const auto& [key, items] : mInvFilters)
        {
            w.u32(key);
            w.u32(static_cast<std::uint32_t>(items.size()));
            for (const FormId item : items)
                w.u32(item);
        }
        // version 3 (task 0202): the references scripts created, then the angles scripts gave
        w.u32(static_cast<std::uint32_t>(mCreated.size()));
        for (const auto& [ref, created] : mCreated)
        {
            w.u32(ref);
            w.u32(created.mBase);
            w.u32(created.mCell);
            for (const double v : created.mPosition)
                w.f64(v);
            for (const double v : created.mAngles)
                w.f64(v);
        }
        w.u32(static_cast<std::uint32_t>(mAngles.size()));
        for (const auto& [ref, angles] : mAngles)
        {
            w.u32(ref);
            for (const double v : angles)
                w.f64(v);
        }
        if (!mPerks.empty())
        {
            w.u32(static_cast<std::uint32_t>(mPerks.size())); // version 4 (task 0241)
            for (const auto& [actor, perk] : mPerks)
            {
                w.u32(actor);
                w.u32(perk);
            }
        }
        return w.finish();
    }

    PapyrusRestoreOutcome PapyrusHost::Impl::restore(std::span<const std::uint8_t> bytes)
    {
        const auto refuse = [](const std::string& why) { return PapyrusRestoreOutcome{ false, why }; };
        if (mInTick)
            return refuse("a state cannot be restored inside a tick");
        if (mIndex == nullptr || mState == nullptr)
            return refuse("the quest data is not set");
        if (bytes.size() < 8 + 8)
            return refuse("the Papyrus state is too short");
        const std::size_t bodySize = bytes.size() - 8;
        {
            ByteReader tail(bytes.subspan(bodySize));
            if (tail.u64() != fnv1a(bytes.data(), bodySize))
                return refuse("the Papyrus state is damaged (checksum)");
        }
        ByteReader r(bytes.first(bodySize));
        if (r.u32() != kStateMagic)
            return refuse("this is not a Papyrus state");
        const std::uint32_t version = r.u32();
        if (version != 1 && version != 2 && version != 3 && version != kStateVersion)
            return refuse("the Papyrus state has version " + std::to_string(version));

        struct LedgerEntry
        {
            bool mAlias = false;
            FormId mForm = 0;
            std::uint32_t mAliasId = 0;
            std::uint32_t mOrdinal = 0;
            std::string mScript;
        };
        struct SavedTimer
        {
            double mDue = 0;
            std::uint64_t mSeq = 0;
            InstanceId mInstance = 0;
            double mRepeat = 0;
        };
        const double time = r.f64();
        const std::uint64_t timerSeq = r.u64();
        PapyrusHostCounters counters;
        std::uint64_t* const fields[] = { &counters.mScriptsLoaded, &counters.mScriptsMissing,
            &counters.mScriptsRefused, &counters.mInstancesAttached, &counters.mAttachRefused, &counters.mPropertiesSet,
            &counters.mPropertiesRejected, &counters.mPropertiesSkipped, &counters.mFragmentsRun, &counters.mStageSets,
            &counters.mUpdatesFired, &counters.mTicks, &counters.mStacksRun, &counters.mScriptErrors };
        for (std::uint64_t* field : fields)
            *field = r.u64();
        for (auto* map : { &counters.mEvents, &counters.mNatives })
        {
            const std::uint32_t n = r.count(12);
            for (std::uint32_t i = 0; i < n; ++i)
            {
                std::string name = r.str();
                (*map)[std::move(name)] = r.u64();
            }
        }
        std::set<std::string> tolerant;
        for (std::uint32_t i = 0, n = r.count(4); i < n; ++i)
            tolerant.insert(r.str());
        const std::uint64_t tolerantCalls = r.u64();
        openeden::script::SaveBlob blob;
        blob.formatVersion = r.u32();
        blob.bytes = r.raw();
        std::vector<LedgerEntry> ledger;
        for (std::uint32_t i = 0, n = r.count(17); i < n; ++i)
        {
            LedgerEntry e;
            e.mAlias = r.u8() != 0;
            e.mForm = r.u32();
            e.mAliasId = r.u32();
            e.mOrdinal = r.u32();
            e.mScript = r.str();
            ledger.push_back(std::move(e));
        }
        std::vector<SavedTimer> timers;
        for (std::uint32_t i = 0, n = r.count(28); i < n; ++i)
        {
            SavedTimer t;
            t.mDue = r.f64();
            t.mSeq = r.u64();
            t.mInstance = r.u32();
            t.mRepeat = r.f64();
            timers.push_back(t);
        }
        std::map<FormId, bool> enabled;
        for (std::uint32_t i = 0, n = r.count(5); i < n; ++i)
        {
            const FormId ref = r.u32();
            enabled[ref] = r.u8() != 0;
        }
        std::map<FormId, std::array<double, 3>> positions;
        for (std::uint32_t i = 0, n = r.count(28); i < n; ++i)
        {
            const FormId ref = r.u32();
            std::array<double, 3> xyz{};
            for (double& v : xyz)
                v = r.f64();
            positions[ref] = xyz;
        }
        std::set<std::pair<FormId, FormId>> spells;
        for (std::uint32_t i = 0, n = r.count(8); i < n; ++i)
        {
            const FormId actor = r.u32();
            spells.emplace(actor, r.u32());
        }
        std::vector<PairKey> aliasKeys;
        for (std::uint32_t i = 0, n = r.count(8); i < n; ++i)
        {
            const FormId quest = r.u32();
            aliasKeys.emplace_back(quest, r.u32());
        }
        std::map<PairKey, FormId> aliasForced;
        for (std::uint32_t i = 0, n = r.count(12); i < n; ++i)
        {
            const FormId quest = r.u32();
            const std::uint32_t alias = r.u32();
            aliasForced[{ quest, alias }] = r.u32();
        }
        std::map<PairKey, bool> displayed;
        std::map<PairKey, bool> completed;
        for (auto* map : { &displayed, &completed })
            for (std::uint32_t i = 0, n = r.count(9); i < n; ++i)
            {
                const FormId quest = r.u32();
                const std::uint32_t objective = r.u32();
                (*map)[{ quest, objective }] = r.u8() != 0;
            }
        std::set<FormId> questsAttached;
        for (std::uint32_t i = 0, n = r.count(4); i < n; ++i)
            questsAttached.insert(r.u32());
        std::set<FormId> refAttached;
        for (std::uint32_t i = 0, n = r.count(4); i < n; ++i)
            refAttached.insert(r.u32());
        std::map<FormId, std::string> refClass;
        for (std::uint32_t i = 0, n = r.count(5); i < n; ++i)
        {
            const FormId ref = r.u32();
            refClass[ref] = r.u8() != 0 ? "Actor" : "ObjectReference";
        }
        std::vector<QuestState::SnapshotEntry> quests;
        for (std::uint32_t i = 0, n = r.count(18); i < n; ++i)
        {
            QuestState::SnapshotEntry q;
            q.mQuest = r.u32();
            q.mRunning = r.u8() != 0;
            q.mCompleted = r.u8() != 0;
            q.mStage = r.u32();
            for (std::uint32_t d = 0, nd = r.count(4); d < nd; ++d)
                q.mDone.push_back(r.u32());
            for (std::uint32_t a = 0, na = r.count(13); a < na; ++a)
            {
                const std::uint32_t alias = r.u32();
                AliasFill fill;
                fill.mKnown = r.u8() != 0;
                fill.mReference = r.u32();
                fill.mActor = r.u32();
                q.mAliases.emplace_back(alias, fill);
            }
            quests.push_back(std::move(q));
        }
        std::map<std::pair<FormId, std::string>, double> avOffsets;
        std::set<FormId> dead;
        std::map<FormId, std::set<FormId>> filters;
        if (version >= 2)
        {
            for (std::uint32_t i = 0, n = r.count(16); i < n; ++i)
            {
                const FormId actor = r.u32();
                std::string name = r.str();
                const double offset = r.f64();
                if (!std::isfinite(offset) || avOffsets.size() >= kMaxAvOffsets)
                    return refuse("the Papyrus state has a bad actor value change");
                avOffsets[{ actor, std::move(name) }] = offset;
            }
            for (std::uint32_t i = 0, n = r.count(4); i < n; ++i)
                dead.insert(r.u32());
            for (std::uint32_t i = 0, n = r.count(8); i < n; ++i)
            {
                const FormId key = r.u32();
                std::set<FormId>& items = filters[key];
                for (std::uint32_t k = 0, nk = r.count(4); k < nk; ++k)
                    items.insert(r.u32());
            }
        }
        std::map<FormId, PapyrusHost::CreatedObject> createdObjects;
        std::map<FormId, std::array<double, 3>> angleOverrides;
        if (version >= 3)
        {
            for (std::uint32_t i = 0, n = r.count(12 + 48); i < n; ++i)
            {
                PapyrusHost::CreatedObject created;
                created.mReference = r.u32();
                created.mBase = r.u32();
                created.mCell = r.u32();
                for (double& v : created.mPosition)
                    v = r.f64();
                for (double& v : created.mAngles)
                    v = r.f64();
                const bool finite = std::isfinite(created.mPosition[0]) && std::isfinite(created.mPosition[1])
                    && std::isfinite(created.mPosition[2]) && std::isfinite(created.mAngles[0])
                    && std::isfinite(created.mAngles[1]) && std::isfinite(created.mAngles[2]);
                const std::uint32_t base = openeden::sdk::game::kCreatedObjectBase;
                if (created.mReference < base || created.mReference - base > openeden::sdk::game::kMaxCreatedObjects
                    || created.mBase == 0 || !finite || createdObjects.size() >= kMaxCreatedKept)
                    return refuse("the Papyrus state has a bad created reference");
                createdObjects[created.mReference] = created;
            }
            for (std::uint32_t i = 0, n = r.count(4 + 24); i < n; ++i)
            {
                const FormId ref = r.u32();
                std::array<double, 3> angles{};
                for (double& v : angles)
                    v = r.f64();
                if (!std::isfinite(angles[0]) || !std::isfinite(angles[1]) || !std::isfinite(angles[2])
                    || angleOverrides.size() >= kMaxCreatedKept)
                    return refuse("the Papyrus state has a bad angle");
                angleOverrides[ref] = angles;
            }
        }
        std::set<std::pair<FormId, FormId>> perks;
        if (version >= 4)
            for (std::uint32_t i = 0, n = r.count(8); i < n; ++i)
            {
                const FormId actor = r.u32();
                const FormId perk = r.u32();
                if (perks.size() >= kMaxRecordedPerks)
                    return refuse("the Papyrus state holds too many perks");
                perks.emplace(actor, perk);
            }
        if (r.failed())
            return refuse("the Papyrus state is damaged (a field runs past the end)");
        if (r.remaining() != 0)
            return refuse("the Papyrus state has bytes after its end");
        if (!(time >= 0.0) || !std::isfinite(time))
            return refuse("the Papyrus state has a bad script time");
        for (const SavedTimer& t : timers)
            if (t.mInstance == 0 || t.mInstance > ledger.size())
                return refuse("a timer names an instance that does not exist");
        if (ledger.size() > mOptions.mMaxInstances)
            return refuse("the state holds more instances than the host allows");

        // Task 0202: the runtime restore asks what class a form is (a script variable may hold a created reference), so
        // the created table is in place for it and goes back if the state is refused
        std::map<FormId, PapyrusHost::CreatedObject> previousCreated = std::move(mCreated);
        mCreated = std::move(createdObjects);
        const auto rollback = [&]() { mCreated = std::move(previousCreated); };

        // the scripts first: the runtime refuses a state whose script is not loaded
        for (const LedgerEntry& e : ledger)
            if (!load(e.mScript, false))
            {
                rollback();
                return refuse("the script '" + e.mScript + "' of a saved instance cannot be loaded");
            }
        const openeden::script::RestoreResult restored = mRuntime->restore(blob);
        if (!restored.ok)
        {
            rollback();
            return refuse("the runtime refused the state: " + restored.error.format());
        }

        // everything is accepted: replace the host's tables
        mInstances.clear();
        mByForm.clear();
        mByAlias.clear();
        InstanceId next = 1;
        for (const LedgerEntry& e : ledger)
        {
            InstanceInfo info;
            info.mForm = e.mForm;
            info.mAlias = e.mAliasId;
            info.mIsAlias = e.mAlias;
            info.mScript = e.mScript;
            info.mOrdinal = e.mOrdinal;
            mInstances.emplace(next, info);
            if (e.mAlias)
                mByAlias[{ e.mForm, e.mAliasId }].push_back(next);
            else
                mByForm.emplace(e.mForm, next);
            ++next;
        }
        mTimers.clear();
        for (const SavedTimer& t : timers)
            mTimers.emplace(std::make_pair(t.mDue, t.mSeq), Timer{ t.mInstance, t.mRepeat });
        mTime = time;
        mTimerSeq = timerSeq;
        mCounters = std::move(counters);
        mTolerant = std::move(tolerant);
        mTolerantBase = tolerantCalls - std::min(tolerantCalls, mRuntime->stats().unregisteredNativeCalls);
        mEnabled = std::move(enabled);
        mPositions = std::move(positions);
        mAngles = std::move(angleOverrides);
        mSpells = std::move(spells);
        ++mSpellRevision; // task 0237: the pack reads the list again
        mPerks = std::move(perks);
        ++mPerkRevision; // task 0241: so it reads the perks again
        mAvOffsets = std::move(avOffsets);
        mDead = std::move(dead);
        mInvFilters = std::move(filters);
        mKilling.clear();
        mAliasKeys = std::move(aliasKeys);
        mAliasByKey.clear();
        for (std::size_t i = 0; i < mAliasKeys.size(); ++i)
            mAliasByKey.emplace(mAliasKeys[i], kAliasTag | static_cast<std::uint32_t>(i + 1));
        mAliasForced = std::move(aliasForced);
        mObjectivesDisplayed = std::move(displayed);
        mObjectivesCompleted = std::move(completed);
        ++mObjectiveRevision;
        mQuestsAttached = std::move(questsAttached);
        mRefAttached = std::move(refAttached);
        mRefClass = std::move(refClass);
        mState->restoreSnapshot(quests);
        mStarted = true;
        if (mQuestChanged)
            for (const QuestState::SnapshotEntry& q : quests)
                if (q.mStage > 0)
                    mQuestChanged(q.mQuest, q.mStage);
        return { true, {} };
    }

    PapyrusHost::PapyrusHost(PapyrusGameData& data, PapyrusHostOptions options)
        : mImpl(std::make_unique<Impl>(data, options))
    {
    }

    PapyrusHost::~PapyrusHost() = default;

    void PapyrusHost::setAliasFiller(QuestAliasFiller* filler)
    {
        mImpl->mFiller = filler;
    }

    void PapyrusHost::setStoryEventSink(StoryEventSink sink)
    {
        mImpl->mStorySink = std::move(sink);
    }

    void PapyrusHost::setSceneControl(SceneControl* control)
    {
        mImpl->mScenes = control;
    }

    // ---- task 0198 ----

    void PapyrusHost::setWorldHooks(PapyrusWorldHooks* hooks)
    {
        mImpl->mHooks = hooks;
    }

    void PapyrusHost::setFollowerState(FollowerState* state)
    {
        mImpl->mFollowers = state;
        if (state == nullptr)
            return;
        Impl* impl = mImpl.get();
        state->setActorValueSource([impl](FormId reference, std::string_view key) -> std::optional<float> {
            const auto found = impl->mAvOffsets.find({ reference, std::string(key) });
            if (found == impl->mAvOffsets.end())
                return std::nullopt;
            return static_cast<float>(found->second);
        });
    }

    void PapyrusHost::setLocationKeywordData(LocationKeywordData* data)
    {
        mImpl->mKeywordData = data;
    }

    void PapyrusHost::setFastTravelSink(FastTravelSink sink)
    {
        mImpl->mFastTravel = std::move(sink);
    }

    void PapyrusHost::onItemMoved(FormId from, FormId to, FormId item, std::int32_t count)
    {
        Impl& m = *mImpl;
        if (count <= 0 || item == 0)
            return;
        const PapyrusValue base = m.formValue(item);
        if (to != 0)
            for (const InstanceId instance : m.instancesHolding(to))
                if (m.inventoryEventWanted(instance, item)
                    && m.sendEvent(instance, "OnItemAdded",
                        { base, PapyrusValue::makeInt(count), PapyrusValue::makeNone(), m.formValue(from) }))
                    ++m.mCounters.mItemEvents;
        if (from != 0)
            for (const InstanceId instance : m.instancesHolding(from))
                if (m.inventoryEventWanted(instance, item)
                    && m.sendEvent(instance, "OnItemRemoved",
                        { base, PapyrusValue::makeInt(count), PapyrusValue::makeNone(), m.formValue(to) }))
                    ++m.mCounters.mItemEvents;
        // scripts on the item form itself (a script on a MISC base) hear of the new container
        for (const InstanceId instance : m.instancesOf(item))
            if (m.sendEvent(instance, "OnContainerChanged", { m.formValue(to), m.formValue(from) }))
                ++m.mCounters.mItemEvents;
    }

    void PapyrusHost::onActorHit(FormId victim, FormId attacker, bool blocked, bool bash)
    {
        Impl& m = *mImpl;
        for (const InstanceId instance : m.instancesHolding(victim))
            if (m.sendEvent(instance, "OnHit",
                    { m.formValue(attacker), PapyrusValue::makeNone(), PapyrusValue::makeNone(),
                        PapyrusValue::makeBool(false), PapyrusValue::makeBool(false), PapyrusValue::makeBool(bash),
                        PapyrusValue::makeBool(blocked) }))
                ++m.mCounters.mCombatEvents;
    }

    void PapyrusHost::onActorDied(FormId victim, FormId killer)
    {
        Impl& m = *mImpl;
        m.mKilling.erase(victim);
        if (!m.mDead.insert(victim).second)
            return;
        for (const InstanceId instance : m.instancesHolding(victim))
        {
            if (m.sendEvent(instance, "OnDying", { m.formValue(killer) }))
                ++m.mCounters.mCombatEvents;
            if (m.sendEvent(instance, "OnDeath", { m.formValue(killer) }))
                ++m.mCounters.mCombatEvents;
        }
    }

    std::uint64_t PapyrusHost::notificationCount() const
    {
        return mImpl->mNoteCount;
    }

    std::string PapyrusHost::notificationAt(std::uint64_t number) const
    {
        const Impl& m = *mImpl;
        if (number == 0 || number > m.mNoteCount || m.mNoteCount - number >= m.mNotes.size())
            return {};
        return m.mNotes[m.mNotes.size() - 1 - static_cast<std::size_t>(m.mNoteCount - number)];
    }

    std::vector<std::pair<std::string, std::uint64_t>> PapyrusHost::tolerantRanking() const
    {
        std::vector<std::pair<std::string, std::uint64_t>> out;
        for (const auto& [name, calls] : mImpl->mRuntime->stats().unregisteredByName)
            out.emplace_back(lower(name), calls);
        std::sort(out.begin(), out.end(), [](const auto& a, const auto& b) {
            return a.second != b.second ? a.second > b.second : a.first < b.first;
        });
        return out;
    }

    std::size_t PapyrusHost::runSceneFragment(
        FormId scene, const vmad::Vmad& vmad, const std::string& script, const std::string& function)
    {
        Impl& m = *mImpl;
        m.preloadClasses();
        std::uint32_t ordinal = 0;
        for (const vmad::Script& s : vmad.mScripts)
        {
            const std::uint32_t at = ordinal++;
            if (m.findInstance(scene, s.mName) != 0)
                continue;
            Impl::InstanceInfo instanceInfo;
            instanceInfo.mForm = scene;
            (void)m.attachScript(AttachKey{ AttachKind::Form, scene, 0, at }, instanceInfo, s);
        }
        const InstanceId instance = m.findInstance(scene, script);
        if (instance == 0 || !m.sendEvent(instance, function))
            return 0;
        m.say(false, "Papyrus: scene " + hex(scene) + " runs " + script + "." + function);
        return 1;
    }

    bool PapyrusHost::startQuestForTest(FormId quest, std::int32_t stage)
    {
        Impl& m = *mImpl;
        if (m.mState == nullptr || !m.mState->exists(quest) || m.mState->completed(quest))
            return false;
        if (m.mState->running(quest))
        {
            // task 0226: a quest the story manager started already: the aid only sets the stage (what a dialogue line
            // would do), it does not start anything
            if (stage < 0)
                return false;
            m.say(false, "Papyrus: quest " + hex(quest) + " stage set by the test aid");
            (void)m.setStage(quest, static_cast<std::uint32_t>(stage));
            return true;
        }
        m.startQuest(quest);
        m.say(false, "Papyrus: quest " + hex(quest) + " started by the test aid");
        if (stage >= 0)
            (void)m.setStage(quest, static_cast<std::uint32_t>(stage));
        return true;
    }

    std::size_t PapyrusHost::raiseStoryEvent(const StoryEvent& event)
    {
        return mImpl->raiseStory(event);
    }

    std::size_t PapyrusHost::runInfoFragments(FormId info, const vmad::Vmad& vmad, FormId ref, FormId base)
    {
        (void)base;
        Impl& m = *mImpl;
        if (!vmad.mHasInfoFragments)
            return 0;
        m.preloadClasses();
        {
            std::string speakerClass;
            if (ref != 0 && m.formClass(ref, speakerClass))
                (void)m.load(lower(speakerClass), false);
        }
        std::uint32_t ordinal = 0;
        for (const vmad::Script& script : vmad.mScripts)
        {
            const std::uint32_t at = ordinal++;
            if (m.findInstance(info, script.mName) != 0)
                continue;
            Impl::InstanceInfo instanceInfo;
            instanceInfo.mForm = info;
            (void)m.attachScript(AttachKey{ AttachKind::Form, info, 0, at }, instanceInfo, script);
        }
        std::size_t queued = 0;
        const auto run = [&](bool wanted, const std::string& scriptName, const std::string& function) {
            if (!wanted)
                return;
            const InstanceId instance = m.findInstance(info, scriptName);
            if (instance == 0)
                return;
            if (m.sendEvent(instance, function, { m.formValue(ref) }))
            {
                ++queued;
                ++m.mCounters.mInfoFragmentsRun;
                m.say(false, "Papyrus: dialogue line " + hex(info) + " runs " + scriptName + "." + function);
            }
        };
        run(vmad.mInfo.hasBegin(), vmad.mInfo.mBeginScript, vmad.mInfo.mBeginFunction);
        run(vmad.mInfo.hasEnd(), vmad.mInfo.mEndScript, vmad.mInfo.mEndFunction);
        return queued;
    }

    void PapyrusHost::setLog(LogFunction log)
    {
        mImpl->mLog = std::move(log);
    }

    void PapyrusHost::setStoryStarted(StoryStarted started)
    {
        mImpl->mStoryStarted = std::move(started);
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
        m.preloadClasses();
        for (const FormId quest : m.mIndex->questIds())
        {
            const QuestRecord* record = m.mIndex->quest(quest);
            if (record == nullptr || !record->startsEnabled() || !m.mState->running(quest))
                continue;
            m.fillAliases(quest, false); // the find-matching ones wait until a script asks (task 0187)
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
        m.mLastTarget = target; // task 0237
        m.mLastActor = actionRef;
        for (const InstanceId instance : m.instancesOf(target))
            (void)m.sendEvent(instance, "OnActivate", { m.formValue(actionRef) });
    }

    void PapyrusHost::onRead(FormId target)
    {
        Impl& m = *mImpl;
        for (const InstanceId instance : m.instancesOf(target))
            (void)m.sendEvent(instance, "OnRead");
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

    std::vector<std::uint8_t> PapyrusHost::saveState() const
    {
        return mImpl->save();
    }

    PapyrusRestoreOutcome PapyrusHost::restoreSavedState(std::span<const std::uint8_t> bytes)
    {
        mImpl->preloadClasses();
        return mImpl->restore(bytes);
    }

    bool PapyrusHost::started() const
    {
        return mImpl->mStarted;
    }

    std::optional<bool> PapyrusHost::enableOverride(FormId reference) const
    {
        const auto it = mImpl->mEnabled.find(reference);
        if (it == mImpl->mEnabled.end())
            return std::nullopt;
        return it->second;
    }

    std::vector<std::pair<FormId, bool>> PapyrusHost::enableOverrides() const
    {
        return std::vector<std::pair<FormId, bool>>(mImpl->mEnabled.begin(), mImpl->mEnabled.end());
    }

    std::vector<std::pair<FormId, std::array<double, 3>>> PapyrusHost::positionOverrides() const
    {
        return std::vector<std::pair<FormId, std::array<double, 3>>>(
            mImpl->mPositions.begin(), mImpl->mPositions.end());
    }

    std::optional<std::array<double, 3>> PapyrusHost::angleOverride(FormId reference) const
    {
        const auto it = mImpl->mAngles.find(reference);
        if (it == mImpl->mAngles.end())
            return std::nullopt;
        return it->second;
    }

    std::vector<std::pair<FormId, std::array<double, 3>>> PapyrusHost::angleOverrides() const
    {
        return std::vector<std::pair<FormId, std::array<double, 3>>>(mImpl->mAngles.begin(), mImpl->mAngles.end());
    }

    std::vector<PapyrusHost::CreatedObject> PapyrusHost::createdObjects() const
    {
        std::vector<CreatedObject> out;
        out.reserve(mImpl->mCreated.size());
        for (const auto& [ref, created] : mImpl->mCreated)
            out.push_back(created);
        return out;
    }

    std::optional<std::array<double, 3>> PapyrusHost::positionOverride(FormId reference) const
    {
        const auto it = mImpl->mPositions.find(reference);
        if (it == mImpl->mPositions.end())
            return std::nullopt;
        return it->second;
    }

    PapyrusHost::ObjectiveState PapyrusHost::objectiveState(FormId quest, std::uint32_t objective) const
    {
        const std::pair<FormId, std::uint32_t> key{ quest, objective };
        const auto d = mImpl->mObjectivesDisplayed.find(key);
        const auto c = mImpl->mObjectivesCompleted.find(key);
        return { d != mImpl->mObjectivesDisplayed.end() && d->second,
            c != mImpl->mObjectivesCompleted.end() && c->second };
    }

    std::uint64_t PapyrusHost::objectiveRevision() const
    {
        return mImpl->mObjectiveRevision;
    }

    FormId PapyrusHost::forcedAlias(FormId quest, std::uint32_t alias) const
    {
        const auto found = mImpl->mAliasForced.find({ quest, alias });
        return found != mImpl->mAliasForced.end() ? found->second : 0;
    }

    bool PapyrusHost::globalSetByScript(FormId global, float& value) const
    {
        const auto found = mImpl->mGlobalValues.find(global);
        if (found == mImpl->mGlobalValues.end())
            return false;
        value = found->second;
        return true;
    }

    bool PapyrusHost::hasSpell(FormId actor, FormId spell) const
    {
        return mImpl->mSpells.count({ actor, spell }) != 0;
    }

    // ---- task 0237
    std::vector<PapyrusHost::SpellCast> PapyrusHost::takeCasts()
    {
        std::vector<SpellCast> out;
        out.swap(mImpl->mCasts);
        return out;
    }

    std::vector<FormId> PapyrusHost::spellsOf(FormId actor) const
    {
        std::vector<FormId> out;
        for (const auto& [who, spell] : mImpl->mSpells)
            if (who == actor)
                out.push_back(spell);
        return out; // the set is ordered by (actor, spell): already sorted
    }

    std::uint64_t PapyrusHost::spellRevision() const
    {
        return mImpl->mSpellRevision;
    }

    std::vector<FormId> PapyrusHost::perksOf(FormId actor) const
    {
        std::vector<FormId> out;
        for (const auto& [who, perk] : mImpl->mPerks)
            if (who == actor)
                out.push_back(perk);
        return out;
    }

    std::uint64_t PapyrusHost::perkRevision() const
    {
        return mImpl->mPerkRevision;
    }

    double PapyrusHost::gameSeconds() const
    {
        return mImpl->mTime * mImpl->mOptions.mTimeScale;
    }

    void PapyrusHost::setMessageSink(MessageSink sink)
    {
        mImpl->mMessageSink = std::move(sink);
    }

    bool PapyrusHost::answerMessage(FormId message, std::int32_t button)
    {
        Impl& m = *mImpl;
        const auto asked = std::find_if(
            m.mAsked.begin(), m.mAsked.end(), [message](const Impl::Replay& r) { return r.mMessage == message; });
        if (asked == m.mAsked.end() || button < 0)
            return false;
        const Impl::Replay replay = *asked;
        m.mAsked.erase(asked);
        PapyrusMessageInfo info;
        const bool cancel
            = !m.mData.messageInfo(message, info) || button + 1 >= static_cast<std::int32_t>(info.mButtons.size());
        if (cancel)
            return true; // the last button is "No": nothing runs again
        m.mAnswers[message] = button;
        m.mReplays.push_back(replay);
        return true;
    }

    std::size_t PapyrusHost::messagesAsked() const
    {
        return mImpl->mAsked.size();
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

    std::uint64_t PapyrusHost::tolerantCalls() const
    {
        return mImpl->mTolerantBase + mImpl->mRuntime->stats().unregisteredNativeCalls;
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
        (void)mImpl->runDeferred();
        if (!r.ok)
        {
            ++mImpl->mCounters.mScriptErrors;
            mImpl->say(true, "Papyrus: " + std::string(function) + ": " + r.error.format());
        }
        return r.ok;
    }
}
