#include "game_skyrimse/papyrus_host.hpp"

#include "game_skyrimse/forsworn.hpp"

#include <components/script_papyrus/papyrusformnatives.hpp>
#include <components/script_papyrus/papyrusnatives.hpp>
#include <components/script_papyrus/pexfile.hpp>
#include <openeden/game/world_source_service.hpp>

#include <algorithm>
#include <array>
#include <bit>
#include <cmath>
#include <cstdio>
#include <cstdlib>
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

    struct PapyrusHost::Impl final : public openeden::script::PapyrusWorld,
                                     public openeden::script::PapyrusAttachKeyCheck,
                                     public openeden::script::PapyrusFormQueries
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
            mRuntime->setAliasResolver([this](std::uint32_t handle, FormId& quest, std::uint32_t& alias) {
                return aliasOf(handle, quest, alias);
            });
            registerWorldNatives();
            registerStoneNatives();
            // Task 0321: what Skyrim SE and Fallout 4 share is answered by the common library, after the pack's own
            // registrations (a pair the pack registered stays the pack's), through the queries below
            mForm = std::make_unique<ScriptPapyrus::FormNatives>(*this);
            // a developer's A/B switch (task 0321): the run of before this task, in the same executable
            if (std::getenv("OPENEDEN_PAPYRUS_NO_COMMON_NATIVES") == nullptr)
                (void)mForm->registerAll(*mRuntime);
        }

        // ---- PapyrusFormQueries (task 0321) ------------------------------------------------------------------------

        openeden::script::FormId formIdOf(const PapyrusValue& value) const override { return formOf(value); }
        bool actorLevel(FormId actor, std::int32_t& level) const override { return mData.actorLevel(actor, level); }
        bool raceOf(FormId actor, FormId& race) const override { return mData.actorRace(actor, race); }
        bool formListEntries(FormId list, std::vector<FormId>& entries) const override
        {
            return mData.formListEntries(list, entries);
        }
        bool referenceRotation(FormId reference, double (&xyz)[3]) const override
        {
            std::array<double, 3> angles{};
            if (anglesOf(reference, angles))
            {
                std::copy(angles.begin(), angles.end(), xyz);
                return true;
            }
            return mData.referenceRotation(reference, xyz);
        }

        // ---- PapyrusWorld ------------------------------------------------------------------------------------------

        bool formClass(openeden::script::FormId form, std::string& className) const override
        {
            if (mCreated.count(form) != 0)
            {
                const auto known = mRefClass.find(form); // task 0274: a relocated actor is an Actor
                className = known != mRefClass.end() ? known->second
                                                     : (mCreatedActors.count(form) != 0 ? "Actor" : "ObjectReference"); // task 0276
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

        // Task 0223: the value of a quest as a script sees it. A scripted quest IS its script in Papyrus, so a cast to
        // the script's type (the carriage topics' fragments: GetOwningQuest() as CarriageSystemScript) needs the
        // attached instance; a quest of several scripts gives the first that is not a fragment script (QF_...).
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
            {
                mParentOf[name] = lower(result.info.parent); // task 0302
                (void)load(lower(result.info.parent), false);
            }
            const ScriptPapyrus::Pex::ParseResult parsed = ScriptPapyrus::Pex::parse(bytes, name);
            if (parsed.mOk)
            {
                for (const std::string& text : parsed.mFile.mStrings)
                {
                    const std::string other = lower(text);
                    if (other == "ongetup") // task 0302: the script defines the event (the string table names it)
                        mGetUpScripts.insert(name);
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

        // One stored value as a Papyrus value; false for a kind this host does not carry
        bool convertValue(const vmad::Value& v, PapyrusValue& value)
        {
            std::string cls;
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
                        // the class name is kept in the host's name table: the value only views it
                        const std::string& named
                            = intern(alias != nullptr && alias->mIsLocation ? "LocationAlias" : "ReferenceAlias");
                        value = PapyrusValue::makeForm(named, aliasHandle(v.mForm, static_cast<std::uint32_t>(v.mAlias)));
                    }
                    else if (v.mForm != 0)
                    {
                        if (!formClass(v.mForm, cls))
                            cls = "Form";
                        value = PapyrusValue::makeForm(intern(cls), v.mForm);
                    }
                    else
                        value = PapyrusValue::makeNone();
                    return true;
                }
                case vmad::ValueKind::String:
                    value = PapyrusValue::makeString(v.mText);
                    return true;
                case vmad::ValueKind::Int:
                    value = PapyrusValue::makeInt(v.mInteger);
                    return true;
                case vmad::ValueKind::Float:
                    value = PapyrusValue::makeFloat(v.mNumber);
                    return true;
                case vmad::ValueKind::Bool:
                    value = PapyrusValue::makeBool(v.mInteger != 0);
                    return true;
                default:
                    return false;
            }
        }

        void setProperty(InstanceId instance, const vmad::Property& property)
        {
            if (property.mArray)
            {
                // Task 0311: an array property (Hearthfire's enable lists, house flags) is built in the runtime from
                // its stored elements; an element of a kind this host does not carry leaves the array out
                std::vector<PapyrusValue> elements;
                elements.reserve(property.mElements.size());
                for (const vmad::Value& element : property.mElements)
                {
                    PapyrusValue value;
                    if (!convertValue(element, value))
                    {
                        ++mCounters.mPropertiesSkipped;
                        return;
                    }
                    elements.push_back(std::move(value));
                }
                if (mRuntime->setArrayProperty(instance, property.mName, elements))
                    ++mCounters.mPropertiesSet;
                else
                {
                    ++mCounters.mPropertiesRejected;
                    if (mTraceNatives && mTraceCount < kMaxTracedNatives) // task 0317: which property, and how long
                    {
                        ++mTraceCount;
                        const auto info = mInstances.find(instance);
                        say(false, "Papyrus: array property " + property.mName + " of "
                                + (info != mInstances.end() ? info->second.mScript : std::string("?")) + " rejected ("
                                + std::to_string(elements.size()) + " elements)");
                    }
                }
                return;
            }
            PapyrusValue value;
            if (!convertValue(property.mValue, value))
            {
                ++mCounters.mPropertiesSkipped;
                return;
            }
            if (mRuntime->setProperty(instance, property.mName, value))
                ++mCounters.mPropertiesSet;
            else
            {
                ++mCounters.mPropertiesRejected;
                if (mTraceNatives && mTraceCount < kMaxTracedNatives) // task 0317
                {
                    ++mTraceCount;
                    const auto info = mInstances.find(instance);
                    say(false, "Papyrus: property " + property.mName + " of "
                            + (info != mInstances.end() ? info->second.mScript : std::string("?")) + " rejected");
                }
            }
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
            if (mTraceNatives && mTraceCount < mTraceCap) // task 0299: OPENEDEN_PAPYRUS_NATIVE_TRACE too
            {
                const auto info = mInstances.find(instance);
                if (info != mInstances.end())
                {
                    ++mTraceCount;
                    say(false,
                        "Papyrus event " + name + " to " + info->second.mScript + " of " + hex(info->second.mForm) + " in state '"
                            + mRuntime->stateOf(instance) + "'");
                }
            }
            return true;
        }

        // Task 0302: does the instance's script, or one of its parents, define OnGetUp?
        bool scriptHandlesGetUp(const std::string& script) const
        {
            std::string at = script;
            for (int depth = 0; depth < 16 && !at.empty(); ++depth)
            {
                if (mGetUpScripts.count(at) != 0)
                    return true;
                const auto parent = mParentOf.find(at);
                if (parent == mParentOf.end())
                    return false;
                at = parent->second;
            }
            return false;
        }
        std::set<std::string> mGetUpScripts; ///< loaded scripts whose string table names OnGetUp
        std::map<std::string, std::string> mParentOf; ///< loaded script to its parent (lower case)

        /// Task 0317: the entries of a form list, kept after the first read. Null when `list` is no list (or not read).
        const std::vector<FormId>* formList(FormId list)
        {
            if (list == 0)
                return nullptr;
            const auto known = mFormLists.find(list);
            if (known != mFormLists.end())
                return &known->second;
            std::vector<FormId> entries;
            if (!mData.formListEntries(list, entries))
                return nullptr;
            return &mFormLists.emplace(list, std::move(entries)).first->second;
        }

        /// Task 0317 (option `mFormScripts`): attaches the scripts of an unplaced item form once, so its
        /// OnContainerChanged runs and `(form as script)` finds it. A form without scripts is only looked at once.
        void attachFormScripts(FormId form)
        {
            if (!mOptions.mFormScripts || form == 0 || form == kPlayerReference || mFormScriptTried.count(form) != 0
                || mFormScriptTried.size() >= mOptions.mMaxFormScripts)
                return;
            std::vector<vmad::Script> scripts;
            if (!mData.baseScripts(form, scripts) || scripts.empty())
            {
                mFormScriptTried.insert(form);
                return;
            }
            // a script cannot be loaded from inside a native: a list's entry gets its script only when the class is
            // loaded already (the script that holds the list names the part's class, so it is); else a later call does
            if (mNativeDepth > 0)
                for (const vmad::Script& script : scripts)
                    if (mLoaded.count(lower(script.mName)) == 0)
                        return;
            mFormScriptTried.insert(form);
            if (mNativeDepth == 0)
                preloadClasses();
            std::uint32_t ordinal = 0;
            for (const vmad::Script& script : scripts)
            {
                const std::uint32_t at = ordinal++;
                if (findInstance(form, script.mName) != 0)
                    continue;
                InstanceInfo info;
                info.mForm = form;
                const InstanceId made = attachScript(AttachKey{ AttachKind::Form, form, 0, at }, info, script);
                say(false, "Papyrus: the unplaced form " + hex(form) + " gets its script " + script.mName
                        + (made != 0 ? "" : " (refused)"));
            }
        }

        std::vector<InstanceId> instancesOf(FormId form) const
        {
            std::vector<InstanceId> out;
            const auto range = mByForm.equal_range(form);
            for (auto it = range.first; it != range.second; ++it)
                out.push_back(it->second);
            return out;
        }

        /// Task 0310: the instances of the scripts on the quest aliases that hold `reference` (a trigger volume that has
        /// no script of its own, the game's defaultSetStageAliasScript on its alias)
        std::vector<InstanceId> aliasInstancesHolding(FormId reference) const
        {
            std::vector<InstanceId> out;
            for (const auto& [key, ids] : mByAlias)
                if (reference != 0 && aliasReferenceOf(key.first, key.second) == reference)
                    out.insert(out.end(), ids.begin(), ids.end());
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
                if (mStageEntryCheck && !mStageEntryCheck(quest, stage, fragment.mLogEntry))
                    continue; // task 0261: the log entry's conditions do not hold (HousePurchase: the hold)
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
            if (mState == nullptr)
                return;
            if (mFiller != nullptr)
            {
                const std::size_t filled = mFiller->fillQuest(quest, *mState, matching);
                mCounters.mAliasesFilled += filled;
                if (filled != 0 && matching)
                    say(false,
                        "Papyrus: quest " + hex(quest) + " started with " + std::to_string(filled) + " aliases filled");
            }
            if (matching)
                createAliasObjects(quest);
        }

        // Task 0306: an alias that creates a reference (ALCO, ALCA: the DLC's cultist ambush makes its cultists and the
        // note at the town's centre marker) gets one when the quest starts. The new reference is placed as PlaceAtMe
        // places one (an actor through the actor system) near the reference of the alias the data names.
        void createAliasObjects(FormId quest, bool onlyInside = false)
        {
            if (mIndex == nullptr || mState == nullptr || mHooks == nullptr)
                return;
            const QuestRecord* record = mIndex->quest(quest);
            if (record == nullptr)
                return;
            std::uint32_t madeHere = 0; // how many this call has put at the same place: they do not stand on each other
            for (const QuestAlias& alias : record->mAliases)
            {
                if (alias.mFill != AliasFillKind::CreatedObject || alias.mCreateObject == 0
                    || alias.mCreateAt == kNoCreateAt || mState->aliasFill(quest, alias.mId).mReference != 0)
                    continue;
                // Task 0318: ALCA bit 31 is "In": created in the inventory of the alias, not at its feet. The alias then
                // holds the item (the pack has no item references): the game's own AddItem(alias.GetRef()) hands it over.
                constexpr std::uint32_t kCreateIn = 0x80000000u;
                const bool inside = (alias.mCreateAt & kCreateIn) != 0;
                if (onlyInside && !inside) // task 0335: the quests that start with the game make only items in inventories
                    continue;
                const std::uint32_t atAlias = alias.mCreateAt & ~kCreateIn;
                if (inside)
                {
                    const FormId item = mHooks->resolveLeveledItem(alias.mCreateObject);
                    if (item == 0)
                    {
                        say(false,
                            "Papyrus: quest " + hex(quest) + " alias " + alias.mName + " is created in alias "
                                + std::to_string(atAlias) + ", and " + hex(alias.mCreateObject) + " resolves to nothing");
                        continue;
                    }
                    mState->setAliasFill(quest, alias.mId, AliasFill{ true, item, kNullFormId });
                    ++mAliasRevision;
                    if (mCreatedInInventory) // task 0335: the game puts it in the inventory of the alias it names
                        if (const FormId holder = aliasReferenceOf(quest, atAlias); holder != 0)
                            (void)mHooks->addItem(holder, item, 1);
                    say(false,
                        "Papyrus: quest " + hex(quest) + " alias " + alias.mName + " holds the item " + hex(item)
                            + " made from " + hex(alias.mCreateObject));
                    continue;
                }
                const FormId at = aliasReferenceOf(quest, atAlias);
                if (at == 0)
                {
                    say(false,
                        "Papyrus: quest " + hex(quest) + " alias " + alias.mName + " is created at alias "
                            + std::to_string(atAlias) + ", which holds nothing");
                    continue;
                }
                // the first stands at the place, the next ones on a spiral round it (110 units, then 155 ...: a capsule
                // is 44 wide, and two actors in one spot would block each other's first step)
                const double turn = static_cast<double>(madeHere) * 2.399963229728653; // the golden angle
                const double reach = 110.0 * std::sqrt(static_cast<double>(madeHere));
                const FormId made = placeNear(
                    alias.mCreateObject, at, false, true, reach * std::cos(turn), reach * std::sin(turn));
                if (made == 0)
                    continue;
                ++madeHere;
                mState->setAliasFill(quest, alias.mId, AliasFill{ true, made, kNullFormId });
                ++mAliasRevision; // task 0305's lookup: the alias now holds a reference
                aliasFactionChange(quest, alias.mId, 0, made); // ALFC: the alias fill joins its factions
                say(false,
                    "Papyrus: quest " + hex(quest) + " alias " + alias.mName + " holds the new "
                        + hex(alias.mCreateObject) + " " + hex(made));
            }
        }

        // PlaceAtMe's one placement: a new reference of `base` at the place of `self`; the host keeps it. 0: refused.
        FormId placeNear(FormId base, FormId self, bool disabled, bool allowActor, double offsetX = 0.0,
            double offsetY = 0.0)
        {
            double where[3] = { 0.0, 0.0, 0.0 };
            if (self == 0 || base == 0 || mHooks == nullptr || !positionOf(self, where))
            {
                if (self != 0 && base != 0 && mHooks != nullptr)
                    say(false, "Papyrus: PlaceAtMe near " + hex(self) + " has no position to place at");
                return 0;
            }
            std::array<double, 3> angles{};
            (void)anglesOf(self, angles);
            const auto nearCreated = mCreated.find(self);
            ObjectPlaceRequest request;
            request.base = base;
            request.nearReference = self;
            request.nearCell = nearCreated != mCreated.end() ? nearCreated->second.mCell : 0;
            request.allowActor = allowActor;
            where[0] += offsetX;
            where[1] += offsetY;
            for (std::size_t k = 0; k < 3; ++k)
            {
                request.position[k] = where[k];
                request.angles[k] = angles[k];
            }
            const ObjectPlaceResult placed = mHooks->placeObject(request);
            if (placed.reference == 0 || mCreated.size() >= kMaxCreatedKept)
                return 0;
            CreatedObject created;
            created.mReference = placed.reference;
            created.mBase = base;
            created.mCell = placed.cell;
            created.mPosition = { where[0], where[1], where[2] };
            created.mAngles = angles;
            mCreated[placed.reference] = created;
            if (placed.actor)
                mCreatedActors.insert(placed.reference); // task 0306: a quest alias's actor is a script Actor
            say(false, "Papyrus: " + hex(base) + " placed as " + hex(placed.reference) + " near " + hex(self));
            if (disabled)
                setEnabled(placed.reference, false, 0.f);
            return placed.reference;
        }

        // Task 0187: the runtime refuses to load a script from inside a native, and attaching a quest that no script
        // named before loads its scripts. So the attach, the fragments and the events of a stage change or a start that
        // a script asked for run right after the native (and the script that called it) returned.
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
            for (const char* name :
                { "form", "objectreference", "actor", "quest", "alias", "referencealias", "locationalias", "location",
                    "keyword", "topicinfo", "scene", "cell", "spell", "actorbase", "game", "utility", "debug" })
                (void)load(name, false);
        }

        bool setStage(FormId quest, std::uint32_t stage)
        {
            if (mIndex == nullptr || mState == nullptr)
                return false;
            const QuestRecord* record = mIndex->quest(quest);
            if (record == nullptr)
            {
                say(false, "Papyrus: SetStage " + std::to_string(stage) + " on " + hex(quest) + ": no such quest");
                return false;
            }
            if (mState->stageDone(quest, stage) && (record->mFlags & QuestFlags::AllowRepeatedStages) == 0)
            {
                say(false, "Papyrus: SetStage " + std::to_string(stage) + " on " + hex(quest) + ": the stage is done"); // 0338
                return false;
            }
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
            if (mQuestStarted)
                mQuestStarted(quest); // task 0306: the package brain's copy of the quests runs it
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

        // Task 0335: the items the quests that start with the game create in inventories, once the world can take them.
        void runPendingCreates()
        {
            if (mPendingCreate.empty() || mHooks == nullptr || !mHooks->itemsReady())
                return;
            const std::vector<FormId> quests = std::move(mPendingCreate);
            mPendingCreate.clear();
            for (const FormId quest : quests)
                createAliasObjects(quest, true);
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
            if (!enabled && mKeepEnabled && mKeepEnabled(reference)) // task 0320
            {
                say(false, "Papyrus: reference " + hex(reference) + " stays enabled (kept by the pack)");
                return;
            }
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
            if (reference == kPlayerReference && mPlayerView.mKnown)
            {
                xyz[0] = mPlayerView.mX;
                xyz[1] = mPlayerView.mY;
                xyz[2] = mPlayerView.mZ;
                return true;
            }
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
            // Task 0313: an actor is where the world has it now (a dead dragon lies where it fell)
            if (mHooks != nullptr && mHooks->livePosition(reference, xyz[0], xyz[1], xyz[2]))
                return true;
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
            const auto moved = mCellMoved.find(reference); // task 0286: a reference the world moved into another cell
            if (moved != mCellMoved.end())
                return moved->second;
            const auto created = mCreated.find(reference);
            return created != mCreated.end() ? created->second.mCell : mData.cellOf(reference);
        }

        // Task 0286: MoveTo of `self` to `at` through the world's cell move. False: not done (the older ways apply).
        bool tryCellMove(FormId self, FormId target, const double* at, const std::array<double, 3>* angles)
        {
            if (!mCellMoves || mHooks == nullptr || self == kPlayerReference)
                return false;
            const FormId destination = cellOfReference(target);
            ObjectMoveRequest request;
            request.hasPosition = true;
            for (int i = 0; i < 3; ++i)
                request.position[i] = at[i];
            if (angles != nullptr)
            {
                request.hasAngles = true;
                for (std::size_t i = 0; i < 3; ++i)
                    request.angles[i] = (*angles)[i];
            }
            double from[3] = { 0.0, 0.0, 0.0 };
            if (positionOf(self, from))
            {
                request.hasFrom = true;
                for (int i = 0; i < 3; ++i)
                    request.from[i] = from[i];
            }
            if (!mHooks->moveToCell(self, destination, request))
                return false;
            mPositions[self] = { at[0], at[1], at[2] };
            if (angles != nullptr)
                mAngles[self] = *angles;
            mCellMoved[self] = destination;
            say(false, "Papyrus: MoveTo of " + hex(self) + " into cell " + hex(destination) + " by the world");
            return true;
        }

        // Task 0268: the reference a script's old reference stands for now (an actor it moved was made again).
        FormId liveReference(FormId reference) const
        {
            for (int hops = 0; hops < 16; ++hops)
            {
                const auto it = mRedirect.find(reference);
                if (it == mRedirect.end())
                    return reference;
                reference = it->second;
            }
            return reference;
        }

        // Task 0268: a MoveTo of `self` to `at`, near the player, that the world cannot do in place: asks the hook for
        // a new reference there, hides the old one, points the quest's aliases at the new one. False: not done.
        bool tryRelocate(FormId self, FormId target, const double* at, const std::array<double, 3>* angles)
        {
            if (!mRelocate || mHooks == nullptr || self == kPlayerReference)
                return false;
            double player[3] = { 0.0, 0.0, 0.0 };
            if (!positionOf(kPlayerReference, player))
                return false;
            const double dx = at[0] - player[0];
            const double dy = at[1] - player[1];
            if (dx * dx + dy * dy > mRelocateRange * mRelocateRange)
                return false;
            const FormId base = baseOfReference(self);
            if (base == 0 || mCreated.size() >= kMaxCreatedKept)
                return false;
            const FormId targetCell = cellOfReference(target);
            const FormId destination = targetCell;
            ObjectMoveRequest request;
            request.hasPosition = true;
            for (int i = 0; i < 3; ++i)
                request.position[i] = at[i];
            if (angles != nullptr)
            {
                request.hasAngles = true;
                for (std::size_t i = 0; i < 3; ++i)
                    request.angles[i] = (*angles)[i];
            }
            const ObjectPlaceResult made = mHooks->relocateObject(self, base, request, destination);
            if (made.reference == 0)
                return false;
            CreatedObject created;
            created.mReference = made.reference;
            created.mBase = base;
            created.mCell = made.cell;
            created.mPosition = { at[0], at[1], at[2] };
            if (angles != nullptr)
                created.mAngles = *angles;
            mCreated[made.reference] = created;
            if (made.actor)
                mCreatedActors.insert(made.reference); // task 0276: not saved (created actors are not)
                mRefClass[made.reference] = "Actor"; // task 0274: a script casts the relocated actor to Actor
            mPositions[made.reference] = { at[0], at[1], at[2] };
            mPositions[self] = { at[0], at[1], at[2] };
            if (angles != nullptr)
                mAngles[made.reference] = *angles;
            mRedirect[self] = made.reference;
            setEnabled(self, false, 0.f);
            const std::size_t aliases = mState != nullptr ? mState->replaceReference(self, made.reference) : 0;
            say(false,
                "Papyrus: MoveTo of " + hex(self) + " near the player made " + hex(made.reference) + " ("
                    + std::to_string(aliases) + " aliases follow it)");
            return true;
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
                // task 0264: the fragment script a scene left on its SCEN (`SF_...`): a save made after any scene
                // fragment ran was refused as a whole ("the game no longer has what its attach key names")
                if (mData.isScene(key.formId))
                    return true;
                if (!mData.formClass(key.formId).empty())
                    return true;
                // Task 0342: a script on a reference a script CREATED (0xFF8xxxxx: PlaceAtMe) is saved with the host's
                // created table; the data does not know it before the load has rebuilt it, so a save made after any
                // such script was attached was refused as a whole
                if (mQuestUpdatesReachAliases && (key.formId & 0xFF800000u) == 0xFF800000u)
                    return true;
                // task 0317: the script of an unplaced item form (a Hearthfire house part) left by a form list or an
                // inventory change
                if (mOptions.mFormScripts)
                {
                    std::vector<vmad::Script> scripts;
                    return mData.baseScripts(key.formId, scripts) && key.effectKey < scripts.size();
                }
                return false;
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

        // Task 0325: the scripts a Form.RegisterForUpdate call is for: the calling script and the other scripts of its
        // form (not the scripts of the quest's aliases: an alias has its own registration)
        std::vector<InstanceId> formUpdateTargets(const NativeCall& c) const
        {
            std::vector<InstanceId> out;
            if (!c.self.attached)
                return out;
            const auto info = mInstances.find(c.self.handle);
            if (info != mInstances.end() && !info->second.mIsAlias)
                for (const InstanceId other : instancesOf(info->second.mForm))
                {
                    const auto otherInfo = mInstances.find(other);
                    if (otherInfo != mInstances.end() && !otherInfo->second.mIsAlias)
                        out.push_back(other);
                }
            if (std::find(out.begin(), out.end(), c.self.handle) == out.end())
                out.push_back(c.self.handle);
            // Task 0342: a quest's registration also reaches the scripts of its aliases (MS14: MS14BurnedHouseScript and
            // MS14AlvasCoffinScript handle OnUpdate and nothing in the data registers them, so the quest's
            // RegisterForUpdate(5) is what feeds them). RECALLED-VERIFY: the engine's own rule.
            if (mQuestUpdatesReachAliases && info != mInstances.end() && !info->second.mIsAlias)
                for (const auto& [key, ids] : mByAlias)
                    if (key.first == info->second.mForm)
                        for (const InstanceId aliasScript : ids)
                            if (std::find(out.begin(), out.end(), aliasScript) == out.end())
                                out.push_back(aliasScript);
            return out;
        }

        // The scripts a game-time registration is for: the script itself when it calls on its own object, else the
        // scripts attached to the object the receiver stands for (an alias or a form: QF_RelationshipMarriageFIN calls
        // Alias_LoveInterest.RegisterForUpdateGameTime(24) and LoveInterestFINScript gets the updates).
        std::vector<InstanceId> gameTimerTargets(const NativeCall& c) const
        {
            if (c.self.attached)
                return { c.self.handle };
            FormId quest = 0;
            std::uint32_t alias = 0;
            std::vector<InstanceId> out;
            if (aliasOf(formOf(c.self), quest, alias))
            {
                const auto it = mByAlias.find({ quest, alias });
                if (it != mByAlias.end())
                    out = it->second;
                return out;
            }
            return instancesOf(formOf(c.self));
        }

        void registerGameUpdate(InstanceId instance, double hours, bool repeat)
        {
            if (!mGameTimersOn)
                return;
            const auto info = mInstances.find(instance);
            if (info != mInstances.end() && mGameTimeIgnore.count(info->second.mForm) != 0)
                return;
            // a repeating and a single registration are separate (the wedding quest has both); each kind replaces
            // its own earlier one (RECALLED-VERIFY)
            for (auto it = mGameTimers.begin(); it != mGameTimers.end();)
            {
                if (it->second.mInstance == instance && (it->second.mRepeat > 0.0) == repeat)
                    it = mGameTimers.erase(it);
                else
                    ++it;
            }
            if (mGameTimers.size() >= kMaxGameTimers)
                return;
            if (!(hours >= 0.0) || !std::isfinite(hours))
                hours = 0.0;
            const double seconds = hours * 3600.0;
            const double base = mGameClockKnown ? mGameClock : 0.0;
            if (info != mInstances.end())
                say(false,
                    std::string(repeat ? "every " : "in ") + std::to_string(hours) + " game hours: update of "
                        + info->second.mScript + " on " + hex(info->second.mForm));
            mGameTimers.emplace(
                std::make_pair(base + seconds, mTimerSeq++), Timer{ instance, repeat ? std::max(seconds, 1.0) : 0.0 });
        }

        void unregisterGameUpdate(InstanceId instance)
        {
            for (auto it = mGameTimers.begin(); it != mGameTimers.end();)
            {
                if (it->second.mInstance == instance)
                    it = mGameTimers.erase(it);
                else
                    ++it;
            }
        }

        void setGameClock(double seconds)
        {
            if (!mGameTimersOn || !std::isfinite(seconds))
                return;
            if (!mGameClockKnown)
            {
                // registrations made before the first clock were deltas
                std::map<std::pair<double, std::uint64_t>, Timer> moved;
                for (const auto& [key, timer] : mGameTimers)
                    moved.emplace(std::make_pair(key.first + seconds, key.second), timer);
                mGameTimers = std::move(moved);
                mGameClockKnown = true;
            }
            mGameClock = seconds;
        }

        void fireGameTimers()
        {
            if (!mGameTimersOn || !mGameClockKnown)
                return;
            while (!mGameTimers.empty() && mGameTimers.begin()->first.first <= mGameClock)
            {
                const auto node = mGameTimers.begin();
                const Timer timer = node->second;
                const double due = node->first.first;
                mGameTimers.erase(node);
                const auto info = mInstances.find(timer.mInstance);
                // a quest that stopped takes its registrations with it (RECALLED-VERIFY)
                if (info != mInstances.end() && mIndex != nullptr && mState != nullptr
                    && mIndex->quest(info->second.mForm) != nullptr && !mState->running(info->second.mForm))
                    continue;
                if (info != mInstances.end())
                    say(false,
                        "game-time update of " + info->second.mScript + " on " + hex(info->second.mForm) + " at "
                            + std::to_string(mGameClock / 3600.0) + " game hours");
                (void)sendEvent(timer.mInstance, "OnUpdateGameTime");
                if (timer.mRepeat > 0.0)
                {
                    // the next multiple of the interval after the clock: a long wait fires once, not once per hour
                    const double missed = std::floor((mGameClock - due) / timer.mRepeat) + 1.0;
                    mGameTimers.emplace(std::make_pair(due + missed * timer.mRepeat, mTimerSeq++), timer);
                }
            }
        }

        // Task 0274: animation events a pack announced (the cart riders' exit idles end with "ExitCartEnd")
        struct AnimRegistration
        {
            InstanceId mInstance;
            FormId mSource = 0;
            std::string mEvent; ///< lower case
        };
        struct PendingAnimEvent
        {
            double mDue = 0.0;
            FormId mSource = 0;
            std::string mEvent;
        };
        static constexpr std::size_t kMaxAnimEvents = 64;
        static constexpr std::size_t kMaxAnimRegistrations = 256;
        std::map<FormId, FormId> mPackageQuest; ///< task 0274: package -> the quest its fragment answers
        std::vector<AnimRegistration> mAnimRegistrations;
        static constexpr std::size_t kMaxSleepers = 256; ///< task 0325: scripts registered for the player's sleep
        std::vector<InstanceId> mSleepers;
        // Task 0289: the open state (GetOpenState: 1 open, 3 closed) of the objects that scripts opened or closed
        static constexpr std::int32_t kOpenStateOpen = 1;
        static constexpr std::int32_t kOpenStateClosed = 3;
        static constexpr double kMaxAnimationWaitSeconds = 30.0;
        static constexpr double kAnimationEventWaitSeconds = 8.0; ///< task 0312: ours (the ride's clip is not played)
        static constexpr double kMaxGameWaitSeconds = 3.0 * 24.0 * 3600.0 / 20.0; // three game days at the default scale
        static constexpr std::size_t kMaxOpenStates = 4096;
        std::map<FormId, std::int32_t> mOpenStates;
        std::map<FormId, std::uint32_t> mDartShots; // task 0289: Weapon.Fire calls per source reference

        // An animation named Open or Close was asked of a reference: its open state follows.
        void noteOpenState(FormId reference, const std::string& event)
        {
            const std::string key = lower(event);
            if (key != "open" && key != "close")
                return;
            if (mOpenStates.size() >= kMaxOpenStates && mOpenStates.find(reference) == mOpenStates.end())
                return;
            mOpenStates[reference] = key == "open" ? kOpenStateOpen : kOpenStateClosed;
        }

        // The game's activation of an openable object without a script of its own: it plays Open or Close.
        void defaultActivation(FormId reference, bool open)
        {
            const std::string event = open ? "Open" : "Close";
            double seconds = 0.0;
            if (mHooks != nullptr)
                (void)mHooks->playObjectAnimation(reference, event, seconds);
            noteOpenState(reference, event);
        }
        std::vector<PendingAnimEvent> mAnimEvents;

        void fireAnimEvents()
        {
            for (std::size_t i = 0; i < mAnimEvents.size();)
            {
                if (mAnimEvents[i].mDue > mTime)
                {
                    ++i;
                    continue;
                }
                const PendingAnimEvent due = mAnimEvents[i];
                mAnimEvents.erase(mAnimEvents.begin() + static_cast<std::ptrdiff_t>(i));
                const std::string key = lower(due.mEvent);
                for (std::size_t r = 0; r < mAnimRegistrations.size();)
                {
                    const AnimRegistration reg = mAnimRegistrations[r];
                    if (reg.mSource != due.mSource || reg.mEvent != key)
                    {
                        ++r;
                        continue;
                    }
                    // the game's scripts unregister inside the handler; a registration fires once per event
                    mAnimRegistrations.erase(mAnimRegistrations.begin() + static_cast<std::ptrdiff_t>(r));
                    (void)sendEvent(reg.mInstance, "OnAnimationEvent",
                        { formValue(due.mSource), PapyrusValue::makeString(due.mEvent) });
                }
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

        void reg(const char* className, const char* function, bool global, Handler handler, bool latent = false)
        {
            NativeDescriptor descriptor;
            descriptor.latent = latent; // task 0289: the script parks for the result's waitSeconds
            descriptor.className = className;
            descriptor.functionName = function;
            descriptor.global = global;
            const std::string key = lower(std::string(className) + "." + function);
            mRegisteredNatives.insert(key); // task 0321
            openeden::script::ErrorReport error;
            const bool ok = mRuntime->registerNative(
                descriptor,
                [this, key, handler = std::move(handler)](const NativeCall& call) {
                    ++mCounters.mNatives[key];
                    if (mTraceNatives && mTraceCount < mTraceCap) // task 0223: OPENEDEN_PAPYRUS_NATIVE_TRACE
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

        /// Task 0321: a native that is registered and does nothing on purpose (a fade, a camera or HUD effect, a flag
        /// the neutral world has no use for); the coverage survey lists it apart from the natives that do work.
        void regNoop(const char* className, const char* function, bool global)
        {
            mNoopNatives.insert(lower(std::string(className) + "." + function));
            reg(className, function, global, [](const NativeCall&) { return NativeResult{}; });
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
            reg("Message", "Show", false,
                [this](const NativeCall& c) { return result(PapyrusValue::makeInt(showMessage(formOf(c.self)))); });
            regNoop("Game", "AddAchievement", true);
            // Task 0294: Utility.WaitGameTime(hours) parks the script for that many game hours (the time scale is the
            // game's; the host counts script seconds). Unregistered it returned at once, and the game's
            // WIDeadBodyCleanupScript loops on it and used up the whole instruction budget.
            reg("Utility", "WaitGameTime", true,
                [this](const NativeCall& c) {
                    NativeResult r;
                    const double hours = argNumber(c, 0);
                    if (std::isfinite(hours) && hours > 0.0 && mOptions.mTimeScale > 0.0)
                        r.waitSeconds = std::min(hours * 3600.0 / mOptions.mTimeScale, kMaxGameWaitSeconds);
                    return r;
                },
                true);
            // Task 0289 (dungeon mechanics): PlayAnimation and PlayAnimationAndWait go to the game through the hooks.
            // The latent form parks the script for the animation's own length (the hook says it), which stands for
            // the animation event the script names as the second argument.
            const auto playObjectAnimation = [this](const NativeCall& c, bool wait) {
                const FormId reference = formOf(c.self);
                const std::string event = !c.arguments.empty() && c.arguments[0].kind == PapyrusKind::String
                    ? std::string(c.arguments[0].text)
                    : std::string();
                NativeResult r;
                r.value = PapyrusValue::makeBool(true);
                if (reference == 0 || event.empty())
                    return r;
                double seconds = 0.0;
                const bool played
                    = mHooks != nullptr && mHooks->playObjectAnimation(reference, event, seconds);
                noteOpenState(reference, event);
                if (wait && played && std::isfinite(seconds) && seconds > 0.0)
                    r.waitSeconds = std::min(seconds, kMaxAnimationWaitSeconds);
                return r;
            };
            reg("ObjectReference", "PlayAnimation", false,
                [playObjectAnimation](const NativeCall& c) { return playObjectAnimation(c, false); });
            reg("ObjectReference", "PlayAnimationAndWait", false,
                [playObjectAnimation](const NativeCall& c) { return playObjectAnimation(c, true); }, true);
            reg("ObjectReference", "GetOpenState", false, [this](const NativeCall& c) {
                const auto found = mOpenStates.find(formOf(c.self));
                return result(PapyrusValue::makeInt(found != mOpenStates.end() ? found->second : kOpenStateClosed));
            });
            regNoop("ObjectReference", "SetNoFavorAllowed", false);
            // Task 0289: a dart trap's shot (Weapon.Fire(source, ammo), the `DartTrap` script). No projectile flies and
            // nothing is hurt in this slice: the shot is counted and the first of each trap port is logged.
            // Task 0299: the shot reaches the world (the combat's trap shot) from the source's place along its facing.
            reg("Weapon", "Fire", false, [this](const NativeCall& c) {
                const FormId source = c.arguments.empty() ? 0 : formOf(c.arguments[0]);
                if (source == 0)
                    return NativeResult{};
                PapyrusWorldHooks::WeaponFireRequest request;
                request.mWeapon = formOf(c.self);
                request.mSource = source;
                request.mAmmo = c.arguments.size() > 1 ? formOf(c.arguments[1]) : 0;
                if (!positionOf(source, request.mPosition))
                    return NativeResult{};
                std::array<double, 3> angles{};
                if (anglesOf(source, angles))
                    std::copy(angles.begin(), angles.end(), request.mAngles);
                else
                    (void)mData.referenceRotation(source, request.mAngles);
                const std::uint32_t seen = mDartShots[source]++;
                const bool fired = mHooks != nullptr && mHooks->fireWeapon(request);
                if (!fired && seen == 0)
                    say(false, "Papyrus: a shot from " + hex(source) + " could not be fired");
                return NativeResult{};
            });
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
            // Task 0325: an update is registered for the FORM: every script on it that handles OnUpdate gets it. A quest's
            // fragment script (QF_DarkBrotherhood) registers RegisterForUpdate(5) and its OnUpdate is in the quest's other
            // script (DarkBrotherhood), so the timer is kept for each script of the form
            reg("Form", "RegisterForSingleUpdate", false, [this](const NativeCall& c) {
                for (const InstanceId target : formUpdateTargets(c))
                    registerUpdate(target, argNumber(c, 0), false);
                return NativeResult{};
            });
            reg("Form", "RegisterForUpdate", false, [this](const NativeCall& c) {
                for (const InstanceId target : formUpdateTargets(c))
                    registerUpdate(target, argNumber(c, 0), true);
                return NativeResult{};
            });
            reg("Form", "UnregisterForUpdate", false, [this](const NativeCall& c) {
                for (const InstanceId target : formUpdateTargets(c))
                    unregisterUpdate(target);
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
            for (const char* cls : { "Form", "Alias" })
            {
                reg(cls, "RegisterForSingleUpdateGameTime", false, [this](const NativeCall& c) {
                    for (const InstanceId target : gameTimerTargets(c))
                        registerGameUpdate(target, argNumber(c, 0), false);
                    return NativeResult{};
                });
                reg(cls, "RegisterForUpdateGameTime", false, [this](const NativeCall& c) {
                    for (const InstanceId target : gameTimerTargets(c))
                        registerGameUpdate(target, argNumber(c, 0), true);
                    return NativeResult{};
                });
                reg(cls, "UnregisterForUpdateGameTime", false, [this](const NativeCall& c) {
                    for (const InstanceId target : gameTimerTargets(c))
                        unregisterGameUpdate(target);
                    return NativeResult{};
                });
            }
            // Task 0325: RegisterForSleep (the Dark Brotherhood's entrance quest waits for the player's sleep);
            // `onSleepStart` and `onSleepStop` raise the events
            for (const char* cls : { "Form", "Alias" })
            {
                reg(cls, "RegisterForSleep", false, [this](const NativeCall& c) {
                    for (const InstanceId target : gameTimerTargets(c))
                        if (mSleepers.size() < kMaxSleepers
                            && std::find(mSleepers.begin(), mSleepers.end(), target) == mSleepers.end())
                            mSleepers.push_back(target);
                    return NativeResult{};
                });
                reg(cls, "UnregisterForSleep", false, [this](const NativeCall& c) {
                    for (const InstanceId target : gameTimerTargets(c))
                        mSleepers.erase(std::remove(mSleepers.begin(), mSleepers.end(), target), mSleepers.end());
                    return NativeResult{};
                });
            }
            // Task 0274: the registrations are kept; `PlayIdle` above (through the pack) raises the event
            const auto registerAnimation = [this](const NativeCall& c) {
                if (c.self.attached && c.arguments.size() >= 2 && c.arguments[1].kind == PapyrusKind::String
                    && mAnimRegistrations.size() < kMaxAnimRegistrations)
                    mAnimRegistrations.push_back(
                        { c.self.handle, liveReference(formOf(c.arguments[0])), lower(c.arguments[1].text) });
                return result(PapyrusValue::makeBool(true));
            };
            reg("Form", "RegisterForAnimationEvent", false, registerAnimation);
            reg("Alias", "RegisterForAnimationEvent", false, registerAnimation);
            reg("ObjectReference", "GetCurrentDestructionStage", false,
                [](const NativeCall&) { return result(PapyrusValue::makeInt(0)); });
            reg("Debug", "OpenUserLog", true, [](const NativeCall&) { return result(PapyrusValue::makeBool(true)); });
            const auto locationOfSelf = [this](const NativeCall& c) {
                const FormId reference = formOf(c.self);
                return result(formValue(reference != 0 ? mData.locationOf(reference) : 0));
            };
            reg("ObjectReference", "GetCurrentLocation", false, locationOfSelf);
            // Task 0276: IsInLocation(akLocation): the reference's location (a created reference: its cell's) is the
            // location or inside it. The wedding quest asks it of the priest, the lover and the witnesses.
            reg("ObjectReference", "IsInLocation", false, [this](const NativeCall& c) {
                const FormId reference = formOf(c.self);
                FormId here = reference != 0 ? mData.locationOf(reference) : 0;
                if (here == 0)
                {
                    const auto created = mCreated.find(reference);
                    if (created != mCreated.end())
                        here = mData.locationOfCell(created->second.mCell);
                }
                const FormId place = c.arguments.empty() ? FormId(0) : formOf(c.arguments[0]);
                return result(PapyrusValue::makeBool(here != 0 && place != 0 && mData.locationWithin(here, place)));
            });
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
            // Task 0296: the Bards College's stage 75 shows Dead Men's Respite on the map.
            reg("ObjectReference", "AddToMap", false, [this](const NativeCall& c) {
                const FormId reference = formOf(c.self);
                if (mHooks != nullptr && reference != 0)
                    (void)mHooks->addToMap(reference);
                return NativeResult{};
            });
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
                const FormId self = liveReference(formOf(c.self));
                const FormId target = c.arguments.empty() ? 0 : liveReference(formOf(c.arguments[0]));
                double where[3] = { 0.0, 0.0, 0.0 };
                if (self == 0 || target == 0 || !positionOf(target, where))
                    return NativeResult{};
                if (self == kPlayerReference && mPlayerMove) // task 0325: the player goes through the pack's travel
                {
                    const double at[3]
                        = { where[0] + argNumber(c, 1), where[1] + argNumber(c, 2), where[2] + argNumber(c, 3) };
                    std::array<double, 3> turned{ 0.0, 0.0, 0.0 };
                    double stored[3] = { 0.0, 0.0, 0.0 };
                    bool known = anglesOf(target, turned);
                    if (!known && mData.referenceRotation(target, stored)) // the marker's angles as the data stores them
                    {
                        turned = { stored[0], stored[1], stored[2] };
                        known = true;
                    }
                    const bool done = mPlayerMove(cellOfReference(target), at, known ? turned[2] : 0.0);
                    say(false, std::string("Papyrus: MoveTo of the player to ") + hex(target) + (done ? "" : ": refused"));
                    return NativeResult{};
                }
                {
                    // Task 0268: what the world cannot move in place is made again at the place (actors, objects of
                    // other cells), near the player only
                    const double at[3]
                        = { where[0] + argNumber(c, 1), where[1] + argNumber(c, 2), where[2] + argNumber(c, 3) };
                    std::array<double, 3> turned{};
                    const bool turnTo = argNumber(c, 4) != 0.0 && anglesOf(target, turned);
                    if (tryCellMove(self, target, at, turnTo ? &turned : nullptr)) // task 0286
                        return NativeResult{};
                    if (tryRelocate(self, target, at, turnTo ? &turned : nullptr))
                        return NativeResult{};
                }
                if (crossesCells(self, target))
                {
                    say(false,
                        "Papyrus: MoveTo of " + hex(self) + " to " + hex(target)
                            + " is in another cell: not done (an object stays in its own cell)");
                    return NativeResult{};
                }
                const double newPosition[3]
                    = { where[0] + argNumber(c, 1), where[1] + argNumber(c, 2), where[2] + argNumber(c, 3) };
                // abMatchRotation: the target's angles, when this host knows them
                std::array<double, 3> angles{};
                const bool turn = argNumber(c, 4) != 0.0 && anglesOf(target, angles);
                moveReference(self, newPosition, turn ? &angles : nullptr);
                return NativeResult{};
            });
            reg("ObjectReference", "SetPosition", false, [this](const NativeCall& c) {
                const FormId self = formOf(c.self);
                if (self != 0)
                {
                    const double newPosition[3] = { argNumber(c, 0), argNumber(c, 1), argNumber(c, 2) };
                    if (std::isfinite(newPosition[0]) && std::isfinite(newPosition[1]) && std::isfinite(newPosition[2]))
                        moveReference(self, newPosition, nullptr);
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
                const std::int32_t count = std::clamp<std::int32_t>(argInt(c, 1), 1, kMaxPlaceCount);
                const bool disabled = argNumber(c, 3) != 0.0;
                FormId first = 0;
                for (std::int32_t i = 0; i < count; ++i)
                {
                    const FormId made = placeNear(base, self, disabled, false);
                    if (made == 0)
                        break;
                    if (first == 0)
                        first = made;
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
                return result(base != 0 ? formValue(base) : PapyrusValue::makeNone()); // task 0274: the base has its own class
            });
            reg("ObjectReference", "Activate", false, [this](const NativeCall& c) {
                const FormId target = formOf(c.self);
                const FormId activator = c.arguments.empty() ? 0 : formOf(c.arguments[0]);
                mLastTarget = target; // task 0237
                mLastActor = activator;
                const std::vector<InstanceId> instances = instancesOf(target);
                for (const InstanceId instance : instances)
                    (void)sendEvent(instance, "OnActivate", { formValue(activator) });
                // Task 0289: an object with no script of its own opens or closes by the game's default activation (a
                // portcullis the pillar puzzle's lever activates through its activator marker)
                if (instances.empty() && target != 0 && target != kPlayerReference)
                    defaultActivation(target, mOpenStates[target] != kOpenStateOpen);
                return result(PapyrusValue::makeBool(true));
            });
            // Quest
            // SetCurrentStageID is an alias of SetStage in the game (task 0181 slice 2b, the Creation Kit wiki through
            // search summaries, toddhow's approval of 2026-11-15): done mark, fragment and OnStageSet.
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
            // Task 0306: the journal's active quest (the DLC's quest sets it at stage 5): no journal marker is kept
            regNoop("Quest", "SetActive", false);
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
                    say(false,
                        "Papyrus: Scene." + std::string(force ? "ForceStart " : "Start ") + hex(scene)
                            + (mScenes == nullptr ? " (no scene runner)" : ""));
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
            // Task 0319: every objective the quest has shown is finished (DLC1VQ01MiscObjective stage 180)
            reg("Quest", "CompleteAllObjectives", false, [this](const NativeCall& c) {
                const FormId quest = formOf(c.self);
                bool changed = false;
                for (const auto& [key, shown] : mObjectivesDisplayed)
                    if (key.first == quest && shown && !mObjectivesCompleted[key])
                    {
                        mObjectivesCompleted[key] = true;
                        changed = true;
                    }
                if (changed)
                    ++mObjectiveRevision;
                return NativeResult{};
            });
            reg("Quest", "IsObjectiveDisplayed", false, [this](const NativeCall& c) {
                const auto it = mObjectivesDisplayed.find({ formOf(c.self), static_cast<std::uint32_t>(argInt(c, 0)) });
                return result(PapyrusValue::makeBool(it != mObjectivesDisplayed.end() && it->second));
            });
            reg("Quest", "IsObjectiveCompleted", false, [this](const NativeCall& c) {
                const auto it = mObjectivesCompleted.find({ formOf(c.self), static_cast<std::uint32_t>(argInt(c, 0)) });
                return result(PapyrusValue::makeBool(it != mObjectivesCompleted.end() && it->second));
            });
            // Task 0274: the package fragment of a quest alias's package reaches its quest
            reg("Package", "GetOwningQuest", false, [this](const NativeCall& c) {
                const auto found = mPackageQuest.find(formOf(c.self));
                return result(found != mPackageQuest.end() && found->second != 0 ? questValue(found->second)
                                                                                 : PapyrusValue::makeNone());
            });
            reg("TopicInfo", "GetOwningQuest", false,
                [this](const NativeCall& c) { return result(questValue(mData.questOfInfo(formOf(c.self)))); });
            // Task 0187: Keyword.SendStoryEvent(akLoc, akRef1, akRef2, aiValue1, aiValue2): the keyword is the event's
            // K1
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
            // Task 0264: a scene's fragment script reaches the quest it belongs to (the Companions' scenes set stages)
            reg("Scene", "GetOwningQuest", false, [this](const NativeCall& c) {
                const FormId quest = mData.questOfScene(formOf(c.self));
                return result(quest != 0 ? questValue(quest) : PapyrusValue::makeNone());
            });
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
                    ++mAliasRevision; // task 0305
                    aliasFactionChange(quest, alias, old, now); // task 0239
                    if (mState != nullptr && now != 0) // task 0270: conditions run on the alias read the quest state
                        mState->setAliasFill(quest, alias, AliasFill{ true, now, kNullFormId });
                }
                return NativeResult{};
            });
            reg("LocationAlias", "ForceLocationTo", false, [this](const NativeCall& c) {
                FormId quest = 0;
                std::uint32_t alias = 0;
                if (aliasOf(formOf(c.self), quest, alias))
                {
                    mAliasForced[{ quest, alias }] = c.arguments.empty() ? 0 : formOf(c.arguments[0]);
                    ++mAliasRevision; // task 0305
                }
                return NativeResult{};
            });
            reg("LocationAlias", "Clear", false, [this](const NativeCall& c) {
                FormId quest = 0;
                std::uint32_t alias = 0;
                if (aliasOf(formOf(c.self), quest, alias))
                {
                    mAliasForced.erase({ quest, alias });
                    ++mAliasRevision; // task 0305
                }
                return NativeResult{};
            });
            // Task 0217: what scripts store on a location for a keyword; the story manager's conditions read it
            //   (fn_606,
            // fn_651). Location.SetKeywordData(akKeyword, afData) and GetKeywordData(akKeyword): RECALLED-VERIFY.
            // Task 0317: IsSameLocation(akOther, akKeyword): the same place, or one inside the other (the game compares
            // the ancestors that carry the keyword; the pack's locations are a tree, RECALLED-VERIFY the keyword rule).
            // the shipped Location.psc builds IsSameLocation on the native HasCommonParent(akOther, akFilter)
            reg("Location", "HasCommonParent", false, [this](const NativeCall& c) {
                const FormId a = formOf(c.self);
                const FormId b = c.arguments.empty() ? FormId(0) : formOf(c.arguments[0]);
                return result(PapyrusValue::makeBool(
                    a != 0 && b != 0 && (a == b || mData.locationWithin(a, b) || mData.locationWithin(b, a))));
            });
            reg("Location", "IsSameLocation", false, [this](const NativeCall& c) {
                const FormId a = formOf(c.self);
                const FormId b = c.arguments.empty() ? FormId(0) : formOf(c.arguments[0]);
                return result(PapyrusValue::makeBool(
                    a != 0 && b != 0 && (a == b || mData.locationWithin(a, b) || mData.locationWithin(b, a))));
            });
            reg("Location", "SetKeywordData", false, [this](const NativeCall& c) {
                if (mKeywordData != nullptr && c.arguments.size() > 1)
                    (void)mKeywordData->set(
                        formOf(c.self), formOf(c.arguments[0]), static_cast<float>(argNumber(c, 1)));
                return NativeResult{};
            });
            // Task 0326: CW01Script.checkLocRefTypeAliveCount (run after each of Fort Hraggstad's bandits dies): the
            // quest reaches its stage 100 when none of the location's CWFortMonster references is left alive
            reg("Location", "GetRefTypeAliveCount", false, [this](const NativeCall& c) {
                const float alive = c.arguments.empty()
                    ? 0.f
                    : mData.refTypeAliveCount(formOf(c.self), formOf(c.arguments[0]));
                return result(PapyrusValue::makeInt(static_cast<std::int32_t>(alive)));
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
                    ++mAliasRevision; // task 0305
                    aliasFactionChange(quest, alias, old, 0); // task 0239
                    if (mState != nullptr && old != 0)
                        mState->setAliasFill(quest, alias, AliasFill{});
                }
                return NativeResult{};
            });
            registerInventoryNatives();
            registerActorNatives();
            registerCarriageNatives();
            registerFollowerNatives();
            registerCrimeNatives();
        }

        // ---- task 0258: what the guards' arrest dialogue fragments call (TIF__000267E0 and its kin)
        // ------------------- Actor.GetCrimeFaction() -> Faction, Faction.PlayerPayCrimeGold(abRemoveStolenItems,
        // abGoToJail), Faction.SendPlayerToJail(abRemoveInventory, abRealJail), Actor.SetPlayerResistingArrest(),
        // Faction.GetCrimeGold() -> Int (Creation Kit wiki, recalled, RECALLED-VERIFY). The effects are the pack's
        // (PapyrusWorldHooks); with no hooks every one is tolerant. The decompiled fragments pass (true, true) for the
        // payment and (true, true) for the jail: what the flags change in the game is not known, the pack's rules
        // ignore them.
        void registerCrimeNatives()
        {
            const auto flag
                = [](const NativeCall& c, std::size_t i) { return i < c.arguments.size() && argNumber(c, i) != 0.0; };
            reg("Actor", "GetCrimeFaction", false, [this](const NativeCall& c) {
                const FormId faction = mHooks != nullptr ? mHooks->crimeFactionOf(formOf(c.self)) : 0;
                return result(formValue(faction));
            });
            reg("Faction", "PlayerPayCrimeGold", false, [this, flag](const NativeCall& c) {
                if (mHooks != nullptr)
                    (void)mHooks->payCrimeGold(formOf(c.self), flag(c, 0), flag(c, 1));
                return NativeResult{};
            });
            reg("Faction", "SendPlayerToJail", false, [this, flag](const NativeCall& c) {
                if (mHooks != nullptr)
                    (void)mHooks->sendToJail(formOf(c.self), flag(c, 0), flag(c, 1));
                return NativeResult{};
            });
            reg("Actor", "SetPlayerResistingArrest", false, [this](const NativeCall& c) {
                if (mHooks != nullptr)
                    (void)mHooks->resistArrest(formOf(c.self));
                return NativeResult{};
            });
            reg("Faction", "GetCrimeGold", false, [this](const NativeCall& c) {
                const std::int32_t gold = mHooks != nullptr ? mHooks->crimeGold(formOf(c.self)) : -1;
                return result(PapyrusValue::makeInt(gold > 0 ? gold : 0));
            });
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
            const auto factionArgument
                = [this](const NativeCall& c) { return c.arguments.empty() ? FormId(0) : formOf(c.arguments[0]); };
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
            // Task 0324: Faction.SetAlly/SetEnemy (the Greybeards become the player's allies at MQ105 stage 5): the relation
            // goes to the follower state, which the combat AI asks through the pack's hook
            reg("Faction", "SetAlly", false, [this, factionArgument](const NativeCall& c) {
                if (mFollowers != nullptr)
                    (void)mFollowers->setFactionRelation(formOf(c.self), factionArgument(c), 2);
                return NativeResult{};
            });
            reg("Faction", "SetEnemy", false, [this, factionArgument](const NativeCall& c) {
                if (mFollowers != nullptr)
                    (void)mFollowers->setFactionRelation(formOf(c.self), factionArgument(c), 1);
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

        // ---- task 0223: what the carriage scripts (CarriageSystemScript, CarriageDriverScript) call
        // ------------------ Game.FastTravel(akDestination) leaves the travel to the pack's travel service (a global
        // script teleports the player and passes the game clock). The driver's script pays the fee itself
        // (RemoveItem(Gold, CarriageCost.value as int)), so GlobalVariable.GetValue answers the GLOB's value. The
        // fades, the player controls, the vehicle, the driver's idles and talk are no-ops that only count (the ride has
        // no animation), the cart seat counts as taken at once (the player "sits" so the script goes straight to its
        // travel), and nobody is in the BYOH faction or in a fight (Actor.IsInCombat stays tolerant: it answers false
        // anyway, and an existing test names it as its example of a tolerant native). Signatures are the Creation Kit
        // wiki's (recalled, RECALLED-VERIFY).
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
            // Task 0285: GlobalVariable.GetValueInt (the whole part, truncated toward zero) and Form.GetGoldValue (the
            // item's base value from the pack's inventory model, 0 when it does not know the item); the favour jobs'
            // sales pay count times value.
            reg("GlobalVariable", "GetValueInt", false, [this](const NativeCall& c) {
                const FormId global = formOf(c.self);
                const auto set = mGlobalValues.find(global);
                float value = 0.f;
                if (set != mGlobalValues.end())
                    value = set->second;
                else if (const std::optional<float> kept
                    = mFollowers != nullptr ? mFollowers->globalValue(global) : std::optional<float>())
                    value = *kept;
                else
                    (void)mData.globalValue(global, value);
                return result(PapyrusValue::makeInt(static_cast<std::int32_t>(value)));
            });
            reg("Form", "GetGoldValue", false, [this](const NativeCall& c) {
                const std::int32_t value = mHooks != nullptr ? mHooks->goldValue(formOf(c.self)) : -1;
                return result(PapyrusValue::makeInt(std::max(value, 0)));
            });
            reg("GlobalVariable", "SetValue", false, [this](const NativeCall& c) {
                const FormId global = formOf(c.self);
                if (global != 0 && (mGlobalValues.size() < kMaxAvOffsets || mGlobalValues.count(global) != 0))
                    mGlobalValues[global] = static_cast<float>(argNumber(c, 0));
                if (mFollowers != nullptr)
                    (void)mFollowers->setGlobal(global, static_cast<float>(argNumber(c, 0))); // task 0239
                return NativeResult{};
            });
            for (const char* name : { "DisablePlayerControls", "EnablePlayerControls", "FadeOutGame",
                     "ForceFirstPerson", "ShowFirstPersonGeometry", "SetInChargen" })
                regNoop("Game", name, true);
            for (const char* name : { "Apply", "PopTo", "Remove", "ApplyCrossFade" })
                regNoop("ImageSpaceModifier", name, false);
            regNoop("ImageSpaceModifier", "RemoveCrossFade", true); // task 0256: a static
            // Task 0312 (Gjalund's ship, DLC2DialogueRRQuestScript.SetSail): the player's world, the end of a talk (the
            // world is paused while the box is open, so a script that waits for it to end already runs after it),
            // the combat alarm, a follower's AI and the autosave are no-ops; the boat ride waits for an animation
            // event the neutral player does not play, for a limited time (OURS)
            reg("ObjectReference", "GetWorldSpace", false, [this](const NativeCall& c) {
                const FormId reference = formOf(c.self);
                return result(formValue(reference != 0 ? mData.worldOf(reference) : 0));
            });
            reg("ObjectReference", "IsInDialogueWithPlayer", false,
                [](const NativeCall&) { return result(PapyrusValue::makeBool(false)); });
            regNoop("Actor", "StopCombatAlarm", false);
            regNoop("Actor", "EnableAI", false);
            regNoop("Game", "RequestAutoSave", true);
            reg("ObjectReference", "WaitForAnimationEvent", false,
                [](const NativeCall&) {
                    NativeResult r;
                    r.value = PapyrusValue::makeBool(true);
                    r.waitSeconds = kAnimationEventWaitSeconds;
                    return r;
                },
                true);
            // Task 0256 (a word wall's script): Actor.HasLOS of the player is where he looks.
            // Task 0282: Actor.IsInCombat asks the world (the combat AI's engaged state); with no hooks it is false.
            reg("Actor", "IsInCombat", false, [this](const NativeCall& c) {
                const FormId actor = formOf(c.self);
                return result(PapyrusValue::makeBool(mHooks != nullptr && actor != 0 && mHooks->inCombat(actor)));
            });
            reg("Actor", "HasLOS", false, [this](const NativeCall& c) {
                const FormId target = c.arguments.empty() ? 0 : formOf(c.arguments[0]);
                double t[3] = { 0, 0, 0 };
                bool sees = false;
                if (formOf(c.self) == kPlayerReference && mPlayerView.mKnown && positionOf(target, t))
                    sees = inViewCone(mPlayerView.mX, mPlayerView.mY, mPlayerView.mZ + kLosEyeHeight,
                        mPlayerView.mHeading, mPlayerView.mPitch, t[0], t[1], t[2], kViewHalfAngle);
                return result(PapyrusValue::makeBool(sees));
            });

            for (const char* name : { "SetRestrained", "Say", "EvaluatePackage" })
                regNoop("Actor", name, false);
            // Task 0274 (the Helgen cart ride): the quest's stage 12 fragment tethers each cart to its horse and puts
            // the riders (the player too) on it; the pack makes the cart trail the horse and carries the riders
            reg("Actor", "SetVehicle", false, [this](const NativeCall& c) {
                const FormId rider = liveReference(formOf(c.self));
                const FormId vehicle = c.arguments.empty() ? 0 : liveReference(formOf(c.arguments[0]));
                if (mHooks != nullptr && rider != 0)
                    (void)mHooks->setVehicle(rider, vehicle);
                return NativeResult{};
            });
            // Task 0268 (the main quest's opening): the cart ride's setup waits for the actors to be loaded, and sets
            // up what has no meaning here yet (the cart HUD, the title sequence, the gate, a cart's tether to its
            // horse)
            reg("ObjectReference", "Is3DLoaded", false, [this](const NativeCall& c) {
                const FormId self = liveReference(formOf(c.self));
                return result(PapyrusValue::makeBool(
                    self == kPlayerReference || mHooks == nullptr || mHooks->referenceLoaded(self)));
            });
            for (const char* name :
                { "SetHudCartMode", "StartTitleSequence", "ShowTitleSequenceMenu", "HideTitleSequenceMenu" })
                regNoop("Game", name, true);
            // Task 0289: SetOpen(true) plays the object's Open animation, SetOpen(false) its Close
            reg("ObjectReference", "SetOpen", false, [this](const NativeCall& c) {
                const FormId reference = formOf(c.self);
                const bool open = c.arguments.empty() || argNumber(c, 0) != 0.0;
                if (reference != 0)
                    defaultActivation(reference, open);
                return NativeResult{};
            });
            reg("ObjectReference", "TetherToHorse", false, [this](const NativeCall& c) {
                const FormId cart = liveReference(formOf(c.self));
                const FormId horse = c.arguments.empty() ? 0 : liveReference(formOf(c.arguments[0]));
                double at[3] = { 0, 0, 0 };
                if (mHooks != nullptr && cart != 0 && horse != 0 && positionOf(cart, at))
                {
                    std::array<double, 3> angles{};
                    const double heading = anglesOf(cart, angles) ? angles[2] : std::nan("");
                    (void)mHooks->tetherCart(cart, horse, at, heading);
                }
                return NativeResult{};
            });
            regNoop("ObjectReference", "Say", false); // the declaring class of Say in the game's scripts
            // Task 0274: an idle the game's script waits for ends with an animation event (the cart's exit idles)
            reg("Actor", "PlayIdle", false, [this](const NativeCall& c) {
                const FormId actor = liveReference(formOf(c.self));
                const FormId idle = c.arguments.empty() ? 0 : formOf(c.arguments[0]);
                std::string event;
                double seconds = 0.0;
                if (mHooks != nullptr && actor != 0 && idle != 0 && mHooks->playIdle(actor, idle, event, seconds)
                    && mAnimEvents.size() < kMaxAnimEvents)
                    mAnimEvents.push_back({ mTime + std::max(seconds, 0.0), actor, std::move(event) });
                return result(PapyrusValue::makeBool(true));
            });
            // Task 0337: a paired kill move a script plays (Weylin's SneakKillMove on Margret) kills its target; the clip is
            // not played (HAND-WRITTEN: the death comes at once, not at the clip's end). Any other idle is a tolerant play.
            reg("Actor", "PlayIdleWithTarget", false, [this](const NativeCall& c) {
                if (!mPairedKillMoves || c.arguments.size() < 2)
                    return result(PapyrusValue::makeBool(false));
                const FormId idle = formOf(c.arguments[0]);
                const FormId target = liveReference(formOf(c.arguments[1]));
                if (idle != 0 && target != 0 && target != kPlayerReference && mHooks != nullptr
                    && forsworn::isKillMoveName(mData.nameOf(idle)) && !actorDead(target) && mHooks->killActor(target))
                    mKilling.insert(target);
                return result(PapyrusValue::makeBool(true));
            });
            reg("ObjectReference", "IsFurnitureInUse", false,
                [](const NativeCall&) { return result(PapyrusValue::makeBool(true)); });
            // Task 0260: the vampire quest's game-time update only acts while the player's controls work (the pack has
            // no control lock: always enabled)
            for (const char* name : { "IsMovementControlsEnabled", "IsFightingControlsEnabled" })
                reg("Game", name, true, [](const NativeCall&) { return result(PapyrusValue::makeBool(true)); });
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
                FormId item = c.arguments.empty() ? 0 : formOf(c.arguments[0]);
                // Task 0271: an item REFERENCE (TG00's ring alias) adds the item it is an instance of
                if (const FormId viaReference = item != 0 ? baseOfReference(item) : 0; viaReference != 0)
                    item = viaReference;
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
                if (mHooks == nullptr || owner == 0 || item == 0 || count <= 0)
                    return NativeResult{};
                // Task 0317: a form list stands for every form in it: up to `count` of each the owner holds
                if (const std::vector<FormId>* list = formList(item))
                {
                    for (const FormId entry : *list)
                    {
                        const std::int32_t held = mHooks->itemCount(owner, entry);
                        const std::int32_t n = std::min(held, count);
                        if (n <= 0)
                            continue;
                        if (other != 0)
                            (void)mHooks->transferItem(owner, other, entry, n);
                        else
                            (void)mHooks->removeItem(owner, entry, n);
                    }
                    return NativeResult{};
                }
                if (other != 0)
                    (void)mHooks->transferItem(owner, other, item, count);
                else
                    (void)mHooks->removeItem(owner, item, count);
                return NativeResult{};
            });
            // Task 0317: FormList (Hearthfire's building lists hold hundreds of parts). A list the data cannot give yet
            // is empty, said once.
            const auto listOf = [this](const NativeCall& c) -> const std::vector<FormId>* {
                const std::vector<FormId>* list = formList(formOf(c.self));
                if (list == nullptr && !mFormListWarned)
                {
                    mFormListWarned = true;
                    say(false, "Papyrus: a form list was asked for before its data was read: " + hex(formOf(c.self)));
                }
                return list;
            };
            reg("FormList", "GetSize", false, [listOf](const NativeCall& c) {
                const std::vector<FormId>* list = listOf(c);
                return result(PapyrusValue::makeInt(list != nullptr ? static_cast<std::int32_t>(list->size()) : 0));
            });
            reg("FormList", "GetAt", false, [this, listOf](const NativeCall& c) {
                const std::vector<FormId>* list = listOf(c);
                const std::int32_t at = argInt(c, 0);
                if (list == nullptr || at < 0 || static_cast<std::size_t>(at) >= list->size())
                    return result(PapyrusValue::makeNone());
                const FormId entry = (*list)[static_cast<std::size_t>(at)];
                attachFormScripts(entry);
                return result(formValue(entry));
            });
            reg("FormList", "Find", false, [this, listOf](const NativeCall& c) {
                const std::vector<FormId>* list = listOf(c);
                const FormId wanted = c.arguments.empty() ? 0 : formOf(c.arguments[0]);
                if (list == nullptr || wanted == 0)
                    return result(PapyrusValue::makeInt(-1));
                const auto at = std::find(list->begin(), list->end(), wanted);
                return result(PapyrusValue::makeInt(at == list->end() ? -1 : static_cast<std::int32_t>(at - list->begin())));
            });
            reg("FormList", "HasForm", false, [this, listOf](const NativeCall& c) {
                const std::vector<FormId>* list = listOf(c);
                const FormId wanted = c.arguments.empty() ? 0 : formOf(c.arguments[0]);
                return result(PapyrusValue::makeBool(
                    list != nullptr && wanted != 0 && std::find(list->begin(), list->end(), wanted) != list->end()));
            });
            // Task 0261: the owner of a cell (HousePurchaseScript.PurchaseHouse: SetFactionOwner(PlayerFaction)); the
            // pack's house ledger keeps it. Game.IncrementStat is a statistic nobody here reads.
            for (const char* name : { "SetFactionOwner", "SetActorOwner" })
                reg("Cell", name, false, [this](const NativeCall& c) {
                    const FormId cell = formOf(c.self);
                    const FormId owner = c.arguments.empty() ? 0 : formOf(c.arguments[0]);
                    if (mHooks != nullptr && cell != 0 && owner != 0)
                        (void)mHooks->setCellOwner(cell, owner);
                    return NativeResult{};
                });
            regNoop("Game", "IncrementStat", true);
            // Task 0278: TIF__00068CF1 (the stablemaster's sale) calls SetFactionOwner on the horse; riding reads the
            // faction rank it also sets, so the owner of a placed actor is not kept.
            regNoop("ObjectReference", "SetFactionOwner", false);
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
            // Task 0302 (masterAmbushScript.OnGetUp): the cell a reference stands in, and whether that cell is attached. The
            // pack only attaches the scripts of references in cells it built, so a cell that a script asks about for a
            // reference of its own is loaded.
            reg("ObjectReference", "GetParentCell", false, [this](const NativeCall& c) {
                return result(formValue(cellOfReference(formOf(c.self))));
            });
            reg("Cell", "IsAttached", false, [this](const NativeCall& c) {
                return result(PapyrusValue::makeBool(formOf(c.self) != 0));
            });
            reg("ObjectReference", "GetLinkedRef", false, [this](const NativeCall& c) {
                const FormId keyword = c.arguments.empty() ? 0 : formOf(c.arguments[0]);
                return result(formValue(mData.linkedRef(formOf(c.self), keyword)));
            });
            // Task 0299: the n-th reference of the unkeyworded link chain (1 is the object's own link); a trap asks for
            // a random one of its ports. A chain that loops ends at the bound.
            reg("ObjectReference", "GetNthLinkedRef", false, [this](const NativeCall& c) {
                FormId at = formOf(c.self);
                const std::int32_t n = static_cast<std::int32_t>(argInt(c, 0));
                if (n < 1 || n > 256)
                    return result(formValue(0));
                for (std::int32_t i = 0; i < n && at != 0; ++i)
                    at = mData.linkedRef(at, 0);
                return result(formValue(at));
            });
            reg("ObjectReference", "GetDistance", false, [this](const NativeCall& c) {
                double a[3] = { 0, 0, 0 }, b[3] = { 0, 0, 0 };
                const FormId other = c.arguments.empty() ? 0 : formOf(c.arguments[0]);
                if (!positionOf(formOf(c.self), a) || !positionOf(other, b))
                    return result(PapyrusValue::makeFloat(0.f));
                const double dx = a[0] - b[0], dy = a[1] - b[1], dz = a[2] - b[2];
                if (mTraceNatives && mTraceCount < kMaxTracedNatives) // task 0313: where the two were taken to be
                {
                    ++mTraceCount;
                    say(false, "Papyrus GetDistance " + hex(formOf(c.self)) + " at " + std::to_string(a[0]) + " "
                            + std::to_string(a[1]) + " " + std::to_string(a[2]) + " to " + hex(other) + " at "
                            + std::to_string(b[0]) + " " + std::to_string(b[1]) + " " + std::to_string(b[2]));
                }
                return result(PapyrusValue::makeFloat(static_cast<float>(std::sqrt(dx * dx + dy * dy + dz * dz))));
            });
        }

        void registerActorNatives()
        {
            const auto valueName = [](const NativeCall& c) {
                return !c.arguments.empty() && c.arguments[0].kind == PapyrusKind::String ? avKey(c.arguments[0].text)
                                                                                          : std::string();
            };
            reg("Actor", "GetActorValue", false, [this, valueName](const NativeCall& c) {
                double now = 0.0, max = 0.0;
                (void)avNow(formOf(c.self), valueName(c), now, max);
                return result(PapyrusValue::makeFloat(static_cast<float>(now)));
            });
            reg("Actor", "GetAV", false, [this, valueName](const NativeCall& c) { // task 0264: the short form
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
            // Task 0264: the Companions' training fight (C00VilkasTrainingScript) and the like
            reg("Actor", "StartCombat", false, [this](const NativeCall& c) {
                // only a fight against the player is built (the AI's target is always the player)
                const FormId actor = formOf(c.self);
                if (mHooks != nullptr && actor != 0 && actor != kPlayerReference && !c.arguments.empty()
                    && formOf(c.arguments[0]) == kPlayerReference)
                    (void)mHooks->startCombat(actor);
                else if (mActorFights && mHooks != nullptr && actor != 0 && actor != kPlayerReference
                    && !c.arguments.empty()) // task 0337: a fight against another actor
                {
                    const FormId target = liveReference(formOf(c.arguments[0]));
                    if (target != 0 && target != kPlayerReference && target != actor)
                        (void)mHooks->startCombatAt(actor, target);
                }
                return NativeResult{};
            });
            reg("Actor", "StopCombat", false, [this](const NativeCall& c) {
                const FormId actor = formOf(c.self);
                if (mHooks != nullptr && actor != 0 && actor != kPlayerReference)
                    (void)mHooks->stopCombat(actor);
                return NativeResult{};
            });
            reg("Actor", "ResetHealthAndLimbs", false, [this](const NativeCall& c) {
                const FormId actor = formOf(c.self);
                if (mHooks != nullptr && actor != 0 && actor != kPlayerReference)
                    (void)mHooks->resetHealth(actor);
                return NativeResult{};
            });
            reg("Actor", "GetCombatState", false, [this](const NativeCall& c) {
                const FormId actor = formOf(c.self);
                return result(
                    PapyrusValue::makeInt(mHooks != nullptr && actor != 0 && mHooks->inCombat(actor) ? 1 : 0));
            });
            regNoop("Actor", "EvaluatePackage", false);
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
                const FormId form
                    = file.empty() ? 0 : mData.formFromFile(file, static_cast<std::uint32_t>(argInt(c, 0)));
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
            runPendingCreates();
            runReplays();
            fireTimers();
            fireGameTimers();
            fireAnimEvents(); // task 0274
            for (int round = 0; round < 8; ++round)
            {
                const openeden::script::TickResult r = mRuntime->tick(mTime);
                mCounters.mStacksRun += r.stacksRun;
                if (mTraceNatives && r.stacksRun != 0 && mTraceCount < kMaxTracedNatives) // task 0330
                {
                    ++mTraceCount;
                    say(false,
                        "Papyrus tick ran " + std::to_string(r.stacksRun) + " stacks, " + std::to_string(r.stacksFinished)
                            + " finished, " + std::to_string(r.stacksWaiting) + " waiting, "
                            + std::to_string(r.errors.size()) + " errors");
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
        // Task 0276: game-time timers. The key's first member is the due time in game seconds (a delta from the first
        // clock while the clock is unknown), the second the sequence number.
        std::map<std::pair<double, std::uint64_t>, Timer> mGameTimers;
        std::set<FormId> mCreatedActors; // task 0276: relocated actors (class Actor)
        bool mGameTimersOn = false;
        bool mGameClockKnown = false;
        double mGameClock = 0.0;
        std::set<FormId> mGameTimeIgnore;
        static constexpr std::size_t kMaxGameTimers = 1024;

        PapyrusGameData& mData;
        PapyrusHostOptions mOptions;
        std::unique_ptr<ScriptPapyrus::PapyrusRuntime> mRuntime;
        std::unique_ptr<ScriptPapyrus::PureNatives> mPure;
        std::unique_ptr<ScriptPapyrus::FormNatives> mForm; ///< task 0321
        PapyrusHostCounters mCounters;
        LogFunction mLog;
        QuestChanged mQuestChanged;
        QuestStarted mQuestStarted; // task 0306
        bool mCreatedInInventory = false; // task 0335
        std::vector<FormId> mPendingCreate; // task 0335: start-game quests whose in-inventory items wait for the item table
        StoryStarted mStoryStarted; ///< task 0217
        EnableChanged mEnableChanged;
        const QuestIndex* mIndex = nullptr;
        QuestState* mState = nullptr;
        QuestVmadLookup mVmads;
        QuestAliasFiller* mFiller = nullptr; ///< task 0187
        int mNativeDepth = 0; ///< > 0 while a native runs: a script cannot be loaded then (task 0187)
        std::vector<std::function<void()>>
            mDeferred; ///< work that needs a script loaded, run after the native returned
        bool mPreloaded = false;
        SceneControl* mScenes = nullptr; ///< task 0194
        std::function<bool(FormId)> mKeepEnabled; ///< task 0320
        StoryEventSink mStorySink; ///< task 0187
        StageEntryCheck mStageEntryCheck; ///< task 0261
        bool mRelocate = false; ///< task 0268
        bool mPairedKillMoves = false; ///< task 0337
        bool mQuestUpdatesReachAliases = false; ///< task 0342
        bool mActorFights = false; ///< task 0337
        bool mCellMoves = false; ///< task 0286
        std::map<FormId, FormId> mCellMoved; ///< task 0286: a reference the world moved -> the cell it stands in now
        double mRelocateRange = 6000.0;
        std::map<FormId, FormId> mRedirect; ///< task 0268: a relocated reference -> the new one
        bool mStarted = false;
        bool mInTick = false;
        double mTime = 0.0;
        std::uint64_t mTimerSeq = 0;
        std::set<std::string> mLoaded;
        std::set<std::string> mNotFile;
        std::set<std::string> mNames;
        std::set<std::string> mTolerant;
        std::set<std::string> mRegisteredNatives; ///< task 0321: every "class.function" a reg() call registered
        std::set<std::string> mNoopNatives; ///< task 0321: the ones registered by regNoop
        std::map<InstanceId, InstanceInfo> mInstances;
        std::multimap<FormId, InstanceId> mByForm;
        std::map<std::pair<FormId, std::uint32_t>, std::vector<InstanceId>> mByAlias;
        std::set<FormId> mQuestsAttached;
        std::set<FormId> mRefAttached;
        std::map<FormId, std::string> mRefClass;
        std::map<std::pair<double, std::uint64_t>, Timer> mTimers;
        std::uint64_t mTolerantBase = 0; ///< tolerant calls counted before the state that was restored
        std::map<FormId, bool> mEnabled;
        TriggerTracker mTracker; ///< task 0256: the trigger volumes of the loaded cells
        PapyrusPlayerView mPlayerView; ///< task 0256: where the player is and looks (not saved)
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
        std::uint32_t mAliasRevision = 0; ///< task 0305: moves when a script forces or clears an alias
        std::map<std::pair<FormId, std::uint32_t>, bool> mObjectivesDisplayed;
        std::map<std::pair<FormId, std::uint32_t>, bool> mObjectivesCompleted;
        std::uint64_t mObjectiveRevision = 0;
        std::string mStringScratch;
        // ---- task 0198
        PapyrusWorldHooks* mHooks = nullptr;
        LocationKeywordData* mKeywordData = nullptr; ///< task 0217
        FollowerState* mFollowers = nullptr; ///< task 0239
        PapyrusHost::FastTravelSink mFastTravel; ///< task 0223
        PapyrusHost::PlayerMoveSink mPlayerMove; ///< task 0325
        bool mTraceNatives
            = std::getenv("OPENEDEN_PAPYRUS_NATIVE_TRACE") != nullptr; ///< a developer aid: one line a native
        std::uint64_t mTraceCount = 0;
        static constexpr std::uint64_t kMaxTracedNatives = 100000;
        /// task 0333: OPENEDEN_PAPYRUS_NATIVE_TRACE=<number> raises the cap of traced calls (a long run hit 100,000)
        std::uint64_t mTraceCap = []() {
            const char* text = std::getenv("OPENEDEN_PAPYRUS_NATIVE_TRACE");
            const unsigned long long wanted = text != nullptr ? std::strtoull(text, nullptr, 10) : 0ull;
            return wanted > kMaxTracedNatives ? static_cast<std::uint64_t>(wanted) : kMaxTracedNatives;
        }();
        std::map<FormId, float> mGlobalValues; ///< task 0223: values scripts set (SetValue); the data gives the rest
        std::deque<std::string> mNotes;
        std::uint64_t mNoteCount = 0;
        std::map<std::pair<FormId, std::string>, double> mAvOffsets; ///< what scripts did to an actor value
        std::set<FormId> mDead; ///< actors that died (OnDeath was raised)
        std::set<FormId> mKilling; ///< Actor.Kill asked, the death not yet reported
        /// AddInventoryEventFilter: per script instance owner (a form id, or an alias handle) the items its events are
        /// limited to. No entry: every item.
        std::map<FormId, std::set<FormId>> mInvFilters;
        // Task 0317: form lists read once (the data is read by a worker; a list that is not ready is asked again), and
        // the forms whose own scripts were looked for (an unplaced MISC: Hearthfire's house parts)
        std::map<FormId, std::vector<FormId>> mFormLists;
        std::set<FormId> mFormScriptTried;
        bool mFormListWarned = false;

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
            return mDead.count(actor) != 0 || mKilling.count(actor) != 0 || (mHooks != nullptr && mHooks->isDead(actor))
                || mData.initiallyDead(actor);
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
            if (key == "dragonsouls" && actor == kPlayerReference && known)
            {
                // Task 0313: when the pack knows the soul count (the shout state) it is that number: the script's
                // change goes to it and nothing stays here, as with health
                if (mHooks != nullptr)
                    mHooks->actorValueSet(actor, key, target);
                return;
            }
            double base = 0.0, ignore = 0.0;
            if (mHooks == nullptr || !mHooks->actorValue(actor, key, base, ignore))
                base = 0.0;
            if (mAvOffsets.size() >= kMaxAvOffsets && mAvOffsets.count({ actor, key }) == 0)
                return;
            mAvOffsets[{ actor, key }] = target - base;
            if (mHooks != nullptr) // task 0302: the pack passes Aggression on to the combat AI
                mHooks->actorValueSet(actor, key, target);
        }
    };

    namespace
    {
        // ---- the host's state as bytes (task 0181) -----------------------------------------------------------------
        // Little endian, canonical (maps and sets in key order), ended by an FNV-1a 64 checksum of everything before
        // it.
        constexpr std::uint32_t kStateMagic = 0x31534850u; // "PHS1"
        // 4 (task 0241) adds the perks; 3 (task 0202) adds created references and angles; 2 (task 0198) adds actor
        // value changes and deaths; 1, 2 and 3 are still read
        constexpr std::uint32_t kStateVersion = 5;
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
        // version 4 only when perks are held, 5 only with game-time timers (task 0276): a state with none keeps the
        // version 3 layout byte for byte
        w.u32(!mGameTimers.empty() ? 5u : mPerks.empty() ? 3u : 4u);
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
        if (!mPerks.empty() || !mGameTimers.empty())
        {
            w.u32(static_cast<std::uint32_t>(mPerks.size())); // version 4 (task 0241)
            for (const auto& [actor, perk] : mPerks)
            {
                w.u32(actor);
                w.u32(perk);
            }
        }
        if (!mGameTimers.empty())
        {
            w.u32(static_cast<std::uint32_t>(mGameTimers.size())); // version 5 (task 0276)
            for (const auto& [key, timer] : mGameTimers)
            {
                w.f64(key.first);
                w.u64(key.second);
                w.u32(timer.mInstance);
                w.f64(timer.mRepeat);
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
        if (version < 1 || version > kStateVersion)
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
        std::vector<SavedTimer> gameTimers;
        if (version >= 5)
            for (std::uint32_t i = 0, n = r.count(28); i < n; ++i)
            {
                SavedTimer t;
                t.mDue = r.f64();
                t.mSeq = r.u64();
                t.mInstance = r.u32();
                t.mRepeat = r.f64();
                if (gameTimers.size() >= kMaxGameTimers || !std::isfinite(t.mDue) || !std::isfinite(t.mRepeat)
                    || t.mRepeat < 0.0)
                    return refuse("the Papyrus state has a bad game-time timer");
                gameTimers.push_back(t);
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
        for (const SavedTimer& t : gameTimers)
            if (t.mInstance == 0 || t.mInstance > ledger.size())
                return refuse("a game-time timer names an instance that does not exist");
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
        mGameTimers.clear();
        for (const SavedTimer& t : gameTimers)
            mGameTimers.emplace(std::make_pair(t.mDue, t.mSeq), Timer{ t.mInstance, t.mRepeat });
        if (!mGameTimers.empty())
            mGameClockKnown = true; // the dues are absolute game seconds
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

    void PapyrusHost::setFormScripts(bool on)
    {
        mImpl->mOptions.mFormScripts = on;
        // the house lists hold 224 references: the runtime's default of 128 refused every one of them
        constexpr std::uint32_t kHouseArrayLength = 1024;
        if (on)
            mImpl->mRuntime->setMaxArrayLength(kHouseArrayLength);
    }

    void PapyrusHost::setCellMoves(bool on)
    {
        mImpl->mCellMoves = on;
    }

    FormId PapyrusHost::aliasReference(FormId quest, std::uint32_t alias) const
    {
        return mImpl->aliasReferenceOf(quest, alias);
    }

    FormId PapyrusHost::forcedAliasReference(FormId quest, std::uint32_t alias) const
    {
        const auto it = mImpl->mAliasForced.find({ quest, alias });
        return it != mImpl->mAliasForced.end() ? it->second : 0;
    }

    std::uint32_t PapyrusHost::aliasRevision() const
    {
        return mImpl->mAliasRevision;
    }

    void PapyrusHost::setRelocation(bool on, double maxPlayerDistance)
    {
        mImpl->mRelocate = on;
        if (maxPlayerDistance > 0.0)
            mImpl->mRelocateRange = maxPlayerDistance;
    }

    void PapyrusHost::setActorFights(bool on)
    {
        mImpl->mActorFights = on;
    }

    void PapyrusHost::setPairedKillMoves(bool on)
    {
        mImpl->mPairedKillMoves = on;
    }

    void PapyrusHost::setQuestUpdatesReachAliases(bool on)
    {
        mImpl->mQuestUpdatesReachAliases = on;
    }

    void PapyrusHost::setStageEntryCheck(StageEntryCheck check)
    {
        mImpl->mStageEntryCheck = std::move(check);
    }

    void PapyrusHost::setStoryEventSink(StoryEventSink sink)
    {
        mImpl->mStorySink = std::move(sink);
    }

    void PapyrusHost::setKeepEnabled(std::function<bool(FormId)> keep)
    {
        mImpl->mKeepEnabled = std::move(keep);
    }

    void PapyrusHost::setCreatedInInventory(bool on)
    {
        mImpl->mCreatedInInventory = on;
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

    void PapyrusHost::setPlayerMoveSink(PlayerMoveSink sink)
    {
        mImpl->mPlayerMove = std::move(sink);
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
        // Task 0271: an alias that holds a placed reference of this item (TG00's ring) hears of the new container too
        for (const auto& [key, ids] : m.mByAlias)
        {
            const FormId held = m.aliasReferenceOf(key.first, key.second);
            // task 0335: with `golden claw` an alias made IN an inventory holds the item form itself (the claw)
            if (held == 0 || (!(m.mCreatedInInventory && held == item) && m.baseOfReference(held) != item))
                continue;
            for (const InstanceId instance : ids)
                if (m.sendEvent(instance, "OnContainerChanged", { m.formValue(to), m.formValue(from) }))
                    ++m.mCounters.mItemEvents;
        }
        // scripts on the item form itself (a script on a MISC base) hear of the new container
        m.attachFormScripts(item); // task 0317: an unplaced form's script is attached the first time it is handed out
        for (const InstanceId instance : m.instancesOf(item))
        {
            const bool sent = m.sendEvent(instance, "OnContainerChanged", { m.formValue(to), m.formValue(from) });
            if (sent)
                ++m.mCounters.mItemEvents;
            if (m.mOptions.mFormScripts)
                m.say(false, "Papyrus: OnContainerChanged of " + hex(item) + (sent ? " queued" : " not handled"));
        }
        // Task 0306: a script on a reference a quest alias created of this base (the cultists' orders) hears of it too
        for (const auto& [reference, created] : m.mCreated)
            if (created.mBase == item)
                for (const InstanceId instance : m.instancesOf(reference))
                    if (m.sendEvent(instance, "OnContainerChanged", { m.formValue(to), m.formValue(from) }))
                        ++m.mCounters.mItemEvents;
    }

    void PapyrusHost::onItemRead(FormId base)
    {
        Impl& m = *mImpl;
        if (base == 0)
            return;
        for (const auto& [reference, created] : m.mCreated)
            if (created.mBase == base)
                for (const InstanceId instance : m.instancesOf(reference))
                    (void)m.sendEvent(instance, "OnRead");
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

    // Task 0292: what a spell did reaches the scripts that hold the target (or the references near where it ended).
    void PapyrusHost::onSpellHit(FormId caster, FormId spell, FormId target, double x, double y, double z)
    {
        Impl& m = *mImpl;
        const std::vector<PapyrusValue> args = { m.formValue(caster), m.formValue(spell), PapyrusValue::makeNone(),
            PapyrusValue::makeBool(false), PapyrusValue::makeBool(false), PapyrusValue::makeBool(false),
            PapyrusValue::makeBool(false) };
        std::vector<InstanceId> instances;
        if (target != 0)
            instances = m.instancesHolding(target);
        else
        {
            const auto nearPoint = [&](FormId reference) {
                double rx = 0.0, ry = 0.0, rz = 0.0;
                if (reference == 0 || !m.mData.referencePosition(reference, rx, ry, rz))
                    return false;
                const double dx = rx - x, dy = ry - y, dz = rz - z;
                return dx * dx + dy * dy + dz * dz <= kSpellHitRadius * kSpellHitRadius;
            };
            std::set<FormId> seen;
            for (const auto& entry : m.mByForm)
                if (seen.insert(entry.first).second && nearPoint(entry.first))
                    for (const InstanceId instance : m.instancesOf(entry.first))
                        instances.push_back(instance);
            for (const auto& [key, ids] : m.mByAlias)
            {
                const FormId held = m.aliasReferenceOf(key.first, key.second);
                if (held != 0 && seen.insert(held).second && nearPoint(held))
                    instances.insert(instances.end(), ids.begin(), ids.end());
            }
        }
        std::size_t sent = 0;
        for (const InstanceId instance : instances)
            if (m.sendEvent(instance, "OnHit", args))
            {
                ++m.mCounters.mCombatEvents;
                ++sent;
            }
        if (target == 0) // task 0304: where a spell ended on the world and how many scripts it reached
            m.say(false, "Papyrus: spell " + hex(spell) + " ended at " + std::to_string(static_cast<long long>(x)) + " "
                    + std::to_string(static_cast<long long>(y)) + " " + std::to_string(static_cast<long long>(z))
                    + ", OnHit sent to " + std::to_string(sent) + " script(s) of " + std::to_string(instances.size()));
    }

    void PapyrusHost::onMagicEffectApplied(FormId caster, FormId target, FormId effect)
    {
        Impl& m = *mImpl;
        for (const InstanceId instance : m.instancesHolding(target))
            if (m.sendEvent(instance, "OnMagicEffectApply", { m.formValue(caster), m.formValue(effect) }))
                ++m.mCounters.mCombatEvents;
    }

    void PapyrusHost::onSpellCast(FormId caster, FormId spell)
    {
        Impl& m = *mImpl;
        for (const InstanceId instance : m.instancesHolding(caster))
            if (m.sendEvent(instance, "OnSpellCast", { m.formValue(spell) }))
                ++m.mCounters.mCombatEvents;
    }

    void PapyrusHost::onLocationChange(FormId actor, FormId oldLocation, FormId newLocation)
    {
        Impl& m = *mImpl;
        for (const InstanceId instance : m.instancesHolding(actor))
            if (m.sendEvent(instance, "OnLocationChange", { m.formValue(oldLocation), m.formValue(newLocation) }))
                ++m.mCounters.mCombatEvents;
    }

    void PapyrusHost::onObjectEquipped(FormId actor, FormId baseObject, FormId reference)
    {
        Impl& m = *mImpl;
        for (const InstanceId instance : m.instancesHolding(actor))
            if (m.sendEvent(instance, "OnObjectEquipped", { m.formValue(baseObject), m.formValue(reference) }))
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

    bool PapyrusHost::handlesGetUp(FormId actor) const
    {
        const Impl& m = *mImpl;
        if (actor == 0)
            return false;
        for (const InstanceId instance : m.instancesOf(actor))
        {
            const auto info = m.mInstances.find(instance);
            if (info != m.mInstances.end() && m.scriptHandlesGetUp(info->second.mScript))
                return true;
        }
        return false;
    }

    void PapyrusHost::onGetUp(FormId actor, FormId furniture)
    {
        Impl& m = *mImpl;
        for (const InstanceId instance : m.instancesHolding(actor))
            (void)m.sendEvent(instance, "OnGetUp", { m.formValue(furniture) });
    }

    void PapyrusHost::onSleepStart(double startDays, double desiredEndDays)
    {
        Impl& m = *mImpl;
        const std::vector<InstanceId> targets = m.mSleepers; // a handler may register or unregister
        for (const InstanceId instance : targets)
            (void)m.sendEvent(instance, "OnSleepStart",
                { PapyrusValue::makeFloat(static_cast<float>(startDays)),
                    PapyrusValue::makeFloat(static_cast<float>(desiredEndDays)) });
    }

    void PapyrusHost::onSleepStop(bool interrupted)
    {
        Impl& m = *mImpl;
        const std::vector<InstanceId> targets = m.mSleepers;
        for (const InstanceId instance : targets)
            (void)m.sendEvent(instance, "OnSleepStop", { PapyrusValue::makeBool(interrupted) });
    }

    void PapyrusHost::onCombatStateChanged(FormId actor, FormId target, std::int32_t state)
    {
        Impl& m = *mImpl;
        for (const InstanceId instance : m.instancesHolding(actor))
            if (m.sendEvent(instance, "OnCombatStateChanged", { m.formValue(target), PapyrusValue::makeInt(state) }))
                ++m.mCounters.mCombatEvents;
    }

    void PapyrusHost::onActorBleedout(FormId actor)
    {
        Impl& m = *mImpl;
        for (const InstanceId instance : m.instancesHolding(actor))
            if (m.sendEvent(instance, "OnEnterBleedout", {}))
                ++m.mCounters.mCombatEvents;
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

    bool PapyrusHost::markStageDoneForTest(FormId quest, std::uint32_t stage)
    {
        Impl& m = *mImpl;
        if (m.mIndex == nullptr || m.mState == nullptr || m.mIndex->quest(quest) == nullptr)
            return false;
        m.mState->setStage(*m.mIndex, quest, stage); // state only: no fragment, no event, no script attach
        m.say(
            false, "Papyrus: quest " + hex(quest) + " stage " + std::to_string(stage) + " marked done by the test aid");
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

    bool PapyrusHost::runPackageEnd(FormId package, FormId actor, FormId quest, const std::string& scriptName,
        const std::string& function)
    {
        Impl& m = *mImpl;
        if (package == 0 || scriptName.empty() || function.empty())
            return false;
        m.preloadClasses();
        m.mPackageQuest[package] = quest;
        if (m.findInstance(package, scriptName) == 0)
        {
            vmad::Script script;
            script.mName = scriptName;
            Impl::InstanceInfo instanceInfo;
            instanceInfo.mForm = package;
            (void)m.attachScript(AttachKey{ AttachKind::Form, package, 0, 0 }, instanceInfo, script);
        }
        const InstanceId instance = m.findInstance(package, scriptName);
        if (instance == 0)
            return false;
        const bool queued = m.sendEvent(instance, function, { m.formValue(actor) });
        if (queued)
            m.say(false, "Papyrus: package " + hex(package) + " ended for " + hex(actor) + ": " + scriptName + "."
                + function);
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

    void PapyrusHost::setQuestStarted(QuestStarted started)
    {
        mImpl->mQuestStarted = std::move(started);
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
            if (m.mCreatedInInventory) // task 0335: the claw of The Golden Claw is made in Arvel's inventory at the start
                m.mPendingCreate.push_back(quest);
            (void)m.attachQuest(quest);
            m.runStartUpStage(quest);
        }
        m.runPendingCreates();
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
                (void)m.sendEvent(instance, "OnCellAttach"); // task 0289: the dart traps load their ports with it
            }
    }

    std::size_t PapyrusHost::replayAliasCellLoad(FormId quest)
    {
        Impl& m = *mImpl;
        if (m.mState == nullptr)
            return 0;
        std::size_t sent = 0;
        for (const auto& [key, ids] : m.mByAlias)
        {
            if (key.first != quest)
                continue;
            const AliasFill fill = m.mState->aliasFill(quest, key.second);
            if (!fill.mKnown || fill.mReference == kNullFormId)
                continue;
            const std::vector<InstanceId> copy = ids;
            for (const InstanceId instance : copy)
                if (m.sendEvent(instance, "OnCellLoad"))
                    ++sent;
        }
        return sent;
    }

    void PapyrusHost::onActivate(FormId target, FormId actionRef)
    {
        Impl& m = *mImpl;
        m.mLastTarget = target; // task 0237
        m.mLastActor = actionRef;
        std::vector<InstanceId> hearers = m.instancesOf(target);
        // task 0338: the script of a quest alias that holds the reference hears it too (DA01ShrineofAzuraScript)
        for (const InstanceId alias : m.aliasInstancesHolding(target))
            hearers.push_back(alias);
        for (const InstanceId instance : hearers)
            (void)m.sendEvent(instance, "OnActivate", { m.formValue(actionRef) });
        // Task 0342: the scripts of a quest alias that holds the activated reference hear it too (MS14's Helgi coffin and
        // Alva's journal are activators with a ReferenceAlias script and no script of their own)
        for (const InstanceId alias : m.aliasInstancesHolding(target))
            (void)m.sendEvent(alias, "OnActivate", { m.formValue(actionRef) });
    }

    bool PapyrusHost::hasHearers(FormId reference) const
    {
        return mImpl->mByForm.count(reference) != 0 || !mImpl->aliasInstancesHolding(reference).empty();
    }

    bool PapyrusHost::addTrigger(FormId reference, const TriggerVolume& volume)
    {
        return mImpl->mTracker.add(reference, volume);
    }

    void PapyrusHost::setPlayerView(const PapyrusPlayerView& view)
    {
        mImpl->mPlayerView = view;
    }

    std::size_t PapyrusHost::updateTriggers()
    {
        Impl& m = *mImpl;
        if (!m.mPlayerView.mKnown)
            return 0;
        std::vector<TriggerEdge> edges;
        m.mTracker.update(m.mPlayerView.mPlace, m.mPlayerView.mX, m.mPlayerView.mY, m.mPlayerView.mZ, edges);
        std::size_t queued = 0;
        for (const TriggerEdge& edge : edges)
        {
            std::vector<InstanceId> hearers = m.instancesOf(edge.mReference);
            for (const InstanceId alias : m.aliasInstancesHolding(edge.mReference)) // task 0310
                hearers.push_back(alias);
            for (const InstanceId instance : hearers)
                if (m.sendEvent(instance, edge.mEntered ? "OnTriggerEnter" : "OnTriggerLeave",
                        { m.formValue(kPlayerReference) }))
                    ++queued;
        }
        return queued;
    }

    std::size_t PapyrusHost::updateSubjectTriggers(const std::vector<SubjectPlace>& subjects)
    {
        Impl& m = *mImpl;
        if (!m.mPlayerView.mKnown)
            return 0;
        std::vector<TriggerEdge> edges;
        for (const SubjectPlace& s : subjects)
            m.mTracker.updateSubject(s.mReference, m.mPlayerView.mPlace, s.mX, s.mY, s.mZ, edges);
        std::size_t queued = 0;
        for (const TriggerEdge& edge : edges)
        {
            m.say(false,
                "Papyrus: trigger " + hex(edge.mReference) + (edge.mEntered ? " entered by " : " left by ")
                    + hex(edge.mSubject) + " (" + std::to_string(m.instancesOf(edge.mReference).size())
                    + " scripts)");
            std::vector<InstanceId> hearers = m.instancesOf(edge.mReference);
            for (const InstanceId alias : m.aliasInstancesHolding(edge.mReference)) // task 0310
                hearers.push_back(alias);
            for (const InstanceId instance : hearers)
                if (m.sendEvent(instance, edge.mEntered ? "OnTriggerEnter" : "OnTriggerLeave",
                        { m.formValue(edge.mSubject) }))
                    ++queued;
        }
        return queued;
    }

    std::size_t PapyrusHost::triggerCount() const
    {
        return mImpl->mTracker.count();
    }

    void PapyrusHost::resetTriggers()
    {
        mImpl->mTracker.resetInside();
    }

    void PapyrusHost::onRead(FormId target)
    {
        Impl& m = *mImpl;
        std::vector<InstanceId> hearers = m.instancesOf(target);
        // task 0330: the script of a quest alias that holds the book hears it too (DLC1VQ01BookScript sets stage 17)
        for (const InstanceId alias : m.aliasInstancesHolding(target))
            hearers.push_back(alias);
        std::size_t heard = 0;
        for (const InstanceId instance : hearers)
            if (m.sendEvent(instance, "OnRead"))
                ++heard;
        m.say(false, "Papyrus: OnRead of " + hex(target) + " sent to " + std::to_string(heard) + " of "
                + std::to_string(hearers.size()) + " scripts");
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

    bool PapyrusHost::hasInstances(FormId reference) const
    {
        return mImpl->mByForm.count(reference) != 0;
    }

    bool PapyrusHost::hasScript(FormId reference, std::string_view script) const
    {
        return mImpl->findInstance(reference, script) != 0;
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

    void PapyrusHost::enableGameTimers(bool on)
    {
        mImpl->mGameTimersOn = on;
    }

    void PapyrusHost::setGameClock(double gameSeconds)
    {
        mImpl->setGameClock(gameSeconds);
    }

    void PapyrusHost::ignoreGameTimersOf(FormId form)
    {
        mImpl->mGameTimeIgnore.insert(form);
    }

    std::size_t PapyrusHost::gameTimerCount() const
    {
        return mImpl->mGameTimers.size();
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

    std::vector<std::pair<std::string, bool>> PapyrusHost::registeredNatives() const
    {
        std::map<std::string, bool> all; // "class.function" -> does nothing on purpose
        for (const std::string& key : mImpl->mRegisteredNatives)
            all[key] = mImpl->mNoopNatives.count(key) != 0;
        for (const std::string_view name : ScriptPapyrus::PureNatives::names())
            all.emplace(std::string(name), false);
        for (const std::string_view name : ScriptPapyrus::FormNatives::queryNames())
            all.emplace(std::string(name), false);
        for (const std::string_view name : ScriptPapyrus::FormNatives::cosmeticNames())
            all.emplace(std::string(name), true);
        return std::vector<std::pair<std::string, bool>>(all.begin(), all.end());
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

    std::optional<double> PapyrusHost::anyNumberVariable(FormId form, std::string_view name) const
    {
        for (const InstanceId instance : mImpl->instancesOf(form))
        {
            const auto value = variableOf(*mImpl, instance, name);
            if (!value)
                continue;
            switch (value->kind)
            {
                case PapyrusKind::Int:
                case PapyrusKind::Bool:
                    return static_cast<double>(value->integer);
                case PapyrusKind::Float:
                    return static_cast<double>(value->number);
                default:
                    break;
            }
        }
        return std::nullopt;
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

    bool PapyrusHost::callFunctionWith(
        FormId form, std::string_view script, std::string_view function, std::span<const CallArgument> arguments)
    {
        Impl& m = *mImpl;
        const InstanceId instance = m.findInstance(form, script);
        if (instance == 0 || m.mInTick || arguments.size() > 8)
            return false;
        std::vector<PapyrusValue> values;
        values.reserve(arguments.size());
        for (const CallArgument& a : arguments)
        {
            switch (a.mKind)
            {
                case CallArgument::Kind::Form:
                    values.push_back(m.formValue(a.mForm));
                    break;
                case CallArgument::Kind::Float:
                    values.push_back(PapyrusValue::makeFloat(a.mFloat));
                    break;
                case CallArgument::Kind::Int:
                    values.push_back(PapyrusValue::makeInt(a.mInt));
                    break;
            }
        }
        const openeden::script::CallResult r = m.mRuntime->call(instance, function, values);
        (void)m.runDeferred();
        if (!r.ok)
        {
            ++m.mCounters.mScriptErrors;
            m.say(true, "Papyrus: " + std::string(function) + ": " + r.error.format());
        }
        return r.ok;
    }

    bool PapyrusHost::sendEventTo(FormId form, std::string_view script, std::string_view event)
    {
        const InstanceId instance = mImpl->findInstance(form, script);
        return instance != 0 && mImpl->sendEvent(instance, std::string(event));
    }

    void PapyrusHost::setGlobalValue(FormId global, float value)
    {
        Impl& m = *mImpl;
        if (global != 0 && (m.mGlobalValues.size() < kMaxAvOffsets || m.mGlobalValues.count(global) != 0))
            m.mGlobalValues[global] = value;
    }

    bool PapyrusHost::setNumberVariable(FormId form, std::string_view script, std::string_view name, double value)
    {
        Impl& m = *mImpl;
        const InstanceId instance = m.findInstance(form, script);
        if (instance == 0)
            return false;
        const auto current = variableOf(m, instance, name);
        if (!current)
            return false;
        PapyrusValue next;
        switch (current->kind)
        {
            case PapyrusKind::Int:
                next = PapyrusValue::makeInt(static_cast<std::int32_t>(value));
                break;
            case PapyrusKind::Bool:
                next = PapyrusValue::makeBool(value != 0.0);
                break;
            case PapyrusKind::Float:
                next = PapyrusValue::makeFloat(static_cast<float>(value));
                break;
            default:
                return false;
        }
        return m.mRuntime->setVariable(instance, name, next);
    }
}
