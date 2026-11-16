#include "game_skyrimse/journal.hpp"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdio>

// The quest journal and its bindings: see the header (task 0185).

namespace game_skyrimse::journal
{
    using openeden::sdk::BindingAccess;
    using openeden::sdk::BindingResult;
    using openeden::sdk::BindingShape;
    using openeden::sdk::BindingStatus;
    using openeden::sdk::BindingType;
    using openeden::sdk::BindingValue;

    const char* statusName(Status status)
    {
        switch (status)
        {
            case Status::Running:
                return "running";
            case Status::Completed:
                return "completed";
            case Status::Failed:
                return "failed";
        }
        return "";
    }

    const QuestView* Snapshot::find(FormId quest) const
    {
        for (const std::vector<QuestView>* list : { &mRunning, &mCompleted })
            for (const QuestView& q : *list)
                if (q.mQuest == quest)
                    return &q;
        return nullptr;
    }

    std::string formatQuestId(FormId id)
    {
        char buffer[16];
        std::snprintf(buffer, sizeof(buffer), "%08x", static_cast<unsigned>(id));
        return buffer;
    }

    bool parseQuestId(std::string_view text, FormId& out)
    {
        if (text.empty() || text.size() > 8)
            return false;
        std::uint32_t value = 0;
        for (const char c : text)
        {
            int digit;
            if (c >= '0' && c <= '9')
                digit = c - '0';
            else if (c >= 'a' && c <= 'f')
                digit = c - 'a' + 10;
            else if (c >= 'A' && c <= 'F')
                digit = c - 'A' + 10;
            else
                return false;
            value = (value << 4) | static_cast<std::uint32_t>(digit);
        }
        out = value;
        return true;
    }

    namespace
    {
        bool startsWithNoCase(std::string_view s, std::string_view prefix)
        {
            if (s.size() < prefix.size())
                return false;
            for (std::size_t i = 0; i < prefix.size(); ++i)
                if (std::tolower(static_cast<unsigned char>(s[i])) != std::tolower(static_cast<unsigned char>(prefix[i])))
                    return false;
            return true;
        }

        bool equalsNoCase(std::string_view a, std::string_view b)
        {
            return a.size() == b.size() && startsWithNoCase(a, b);
        }

        // A number the way the game prints a tag value: a whole number without decimals, else two places trimmed.
        std::string tagNumber(float value)
        {
            if (!std::isfinite(value))
                return "0";
            const double rounded = std::round(static_cast<double>(value));
            if (std::abs(static_cast<double>(value) - rounded) < 0.005)
                return std::to_string(static_cast<long long>(rounded));
            char buffer[32];
            std::snprintf(buffer, sizeof(buffer), "%.2f", static_cast<double>(value));
            std::string out = buffer;
            while (!out.empty() && out.back() == '0')
                out.pop_back();
            if (!out.empty() && out.back() == '.')
                out.pop_back();
            return out;
        }

        // `which` is the part after "Alias.Pronoun"; empty for plain "Pronoun". Empty result: not a pronoun tag.
        std::string_view pronounFor(std::string_view suffix, int female)
        {
            struct Row
            {
                std::string_view mSuffix, mMale, mFemale, mPlural;
            };
            static constexpr Row rows[] = {
                { "", "he", "she", "they" },
                { "Cap", "He", "She", "They" },
                { "Obj", "him", "her", "them" },
                { "Pos", "his", "her", "their" },
                { "PosObj", "his", "hers", "theirs" },
                { "Ref", "himself", "herself", "themselves" },
            };
            for (const Row& row : rows)
                if (equalsNoCase(suffix, row.mSuffix))
                    return female < 0 ? row.mPlural : (female > 0 ? row.mFemale : row.mMale);
            return {};
        }

        std::string expandImpl(std::string_view text, const TagHooks& hooks, std::string_view unknown, bool rawAliasNames)
        {
            constexpr std::size_t kMaxTag = 64;
            std::string out;
            out.reserve(text.size());
            for (std::size_t i = 0; i < text.size(); ++i)
            {
                const char c = text[i];
                if (c == '>')
                    continue;
                if (c != '<')
                {
                    out += c;
                    continue;
                }
                const std::size_t end = text.find('>', i + 1);
                if (end == std::string_view::npos || end - i > kMaxTag)
                    continue; // a stray '<'
                const std::string_view tag = text.substr(i + 1, end - i - 1);
                const std::size_t equals = tag.find('=');
                const std::string_view key = equals == std::string_view::npos ? tag : tag.substr(0, equals);
                const std::string_view arg
                    = equals == std::string_view::npos ? std::string_view() : tag.substr(equals + 1);
                std::string shown;
                bool filled = false;
                if (equals != std::string_view::npos
                    && (equalsNoCase(key, "Alias") || equalsNoCase(key, "Alias.ShortName")
                        || equalsNoCase(key, "Alias.Name")))
                {
                    if (rawAliasNames && !hooks.mAlias)
                    {
                        shown = std::string(arg);
                        filled = true;
                    }
                    else if (hooks.mAlias)
                    {
                        shown = hooks.mAlias(arg);
                        filled = !shown.empty();
                    }
                }
                else if (equals != std::string_view::npos && startsWithNoCase(key, "Alias.Pronoun") && hooks.mAliasFemale)
                {
                    const std::string_view word = pronounFor(key.substr(13), hooks.mAliasFemale(arg));
                    shown = std::string(word);
                    filled = !word.empty();
                }
                else if (equals != std::string_view::npos && equalsNoCase(key, "Global") && hooks.mGlobal)
                {
                    const std::optional<float> value = hooks.mGlobal(arg);
                    if (value)
                    {
                        shown = tagNumber(*value);
                        filled = true;
                    }
                }
                else if (equals == std::string_view::npos && equalsNoCase(key, "BaseName") && hooks.mBaseName)
                {
                    shown = hooks.mBaseName();
                    filled = !shown.empty();
                }
                else if (equals == std::string_view::npos && equalsNoCase(key, "BribeCost") && hooks.mBribeCost)
                {
                    const std::optional<float> value = hooks.mBribeCost();
                    if (value)
                    {
                        shown = tagNumber(*value);
                        filled = true;
                    }
                }
                else if (equals == std::string_view::npos && equalsNoCase(key, "CrimeGold") && hooks.mCrimeGold)
                {
                    const std::optional<float> value = hooks.mCrimeGold();
                    if (value)
                    {
                        shown = tagNumber(*value);
                        filled = true;
                    }
                }
                out.append(filled ? std::string_view(shown) : unknown);
                i = end;
            }
            return out;
        }
    }

    std::string expandTags(std::string_view text)
    {
        return expandTags(text, AliasNameResolver());
    }

    std::string expandTags(std::string_view text, const AliasNameResolver& aliasName)
    {
        TagHooks hooks;
        hooks.mAlias = aliasName;
        return expandImpl(text, hooks, "...", !aliasName);
    }

    std::string expandTags(std::string_view text, const TagHooks& hooks, std::string_view unknown)
    {
        return expandImpl(text, hooks, unknown, false);
    }

    namespace
    {
        // A localised string is looked up in the table the record kind expects and then in the other two (the layout of
        // which QUST text is in which table is from xEdit, RECALLED-VERIFY; the data decides at run time).
        std::string resolveText(
            const Inputs& in, FormId quest, const LString& text, std::uint32_t plugin, StringTableKind kind)
        {
            if (!text.mPresent)
                return {};
            std::string result;
            if (!text.mLocalized)
                result = text.mText;
            else if (in.mText)
            {
                const StringTableKind order[] = { kind, StringTableKind::Strings, StringTableKind::DlStrings,
                    StringTableKind::IlStrings };
                for (std::size_t i = 0; i < 4 && result.empty(); ++i)
                {
                    bool seen = false;
                    for (std::size_t j = 0; j < i; ++j)
                        seen = seen || order[j] == order[i];
                    if (!seen)
                        result = in.mText(text, plugin, order[i]);
                }
            }
            if (in.mAliasName)
                result = expandTags(
                    result, [&in, quest](std::string_view alias) { return in.mAliasName(quest, alias); });
            else
                result = expandTags(result);
            if (result.size() > kMaxTextBytes)
                result.resize(kMaxTextBytes);
            return result;
        }

        std::string lowered(const std::string& s)
        {
            std::string out = s;
            std::transform(out.begin(), out.end(), out.begin(), [](unsigned char c) { return std::tolower(c); });
            return out;
        }

        void sortAndCap(std::vector<QuestView>& list, std::uint64_t& dropped)
        {
            std::vector<std::pair<std::string, std::size_t>> keys;
            keys.reserve(list.size());
            for (std::size_t i = 0; i < list.size(); ++i)
                keys.emplace_back(lowered(list[i].mName), i);
            std::sort(keys.begin(), keys.end(), [&list](const auto& a, const auto& b) {
                if (a.first != b.first)
                    return a.first < b.first;
                return list[a.second].mQuest < list[b.second].mQuest;
            });
            std::vector<QuestView> sorted;
            sorted.reserve(std::min(list.size(), kMaxQuestsPerList));
            for (const auto& [key, index] : keys)
            {
                if (sorted.size() >= kMaxQuestsPerList)
                {
                    ++dropped;
                    continue;
                }
                sorted.push_back(std::move(list[index]));
            }
            list = std::move(sorted);
        }
    }

    void build(const Inputs& in, Snapshot& out)
    {
        const std::uint64_t revision = out.mRevision;
        out = Snapshot();
        out.mRevision = revision;
        if (in.mQuests == nullptr || in.mState == nullptr)
            return;
        const QuestState& state = *in.mState;
        for (const FormId id : in.mQuests->questIds())
        {
            const QuestRecord* record = in.mQuests->quest(id);
            if (record == nullptr || !state.exists(id))
                continue;
            const bool completed = state.completed(id);
            if (!completed && !state.running(id))
                continue;
            ++out.mActive;
            QuestView view;
            view.mQuest = id;
            view.mName
                = resolveText(in, record->mCommon.mId, record->mName, record->mPlugin, StringTableKind::Strings);
            if (view.mName.empty())
            {
                ++out.mNoName;
                continue;
            }
            const float stageValue = state.stage(id);
            const std::uint32_t current = stageValue > 0.f ? static_cast<std::uint32_t>(stageValue) : 0;

            std::vector<const QuestStage*> stages;
            for (const QuestStage& s : record->mStages)
                stages.push_back(&s);
            std::stable_sort(stages.begin(), stages.end(),
                [](const QuestStage* a, const QuestStage* b) { return a->mIndex < b->mIndex; });
            const QuestStage* currentStage = nullptr;
            std::string currentText;
            for (const QuestStage* s : stages)
            {
                const bool isCurrent = current != 0 && s->mIndex == current;
                if (!state.stageDone(id, s->mIndex) && !isCurrent)
                    continue;
                std::string text;
                for (const QuestLogEntry& e : s->mEntries)
                {
                    text = resolveText(
                        in, record->mCommon.mId, e.mText, record->mPlugin, StringTableKind::DlStrings);
                    if (!text.empty())
                        break;
                }
                if (isCurrent)
                {
                    currentStage = s;
                    currentText = text;
                }
                if (!text.empty())
                    view.mLog.push_back(std::move(text));
            }
            if (view.mLog.size() > kMaxLogEntries)
                view.mLog.erase(view.mLog.begin(), view.mLog.end() - static_cast<std::ptrdiff_t>(kMaxLogEntries));
            view.mEntry = !currentText.empty() ? currentText : (view.mLog.empty() ? std::string() : view.mLog.back());

            if (in.mObjective)
            {
                std::vector<const QuestObjective*> objectives;
                for (const QuestObjective& o : record->mObjectives)
                    objectives.push_back(&o);
                std::stable_sort(objectives.begin(), objectives.end(),
                    [](const QuestObjective* a, const QuestObjective* b) { return a->mIndex < b->mIndex; });
                for (const QuestObjective* o : objectives)
                {
                    if (view.mObjectives.size() >= kMaxObjectives)
                        break;
                    const ObjectiveFlags flags = in.mObjective(id, o->mIndex);
                    if (!flags.mDisplayed)
                        continue;
                    std::string text = resolveText(
                        in, record->mCommon.mId, o->mText, record->mPlugin, StringTableKind::DlStrings);
                    if (text.empty())
                        continue;
                    view.mObjectives.push_back({ o->mIndex, std::move(text), flags.mCompleted });
                }
            }
            if (view.mLog.empty() && view.mObjectives.empty())
            {
                ++out.mNoText;
                continue;
            }

            if (completed)
                view.mStatus = Status::Completed;
            else if (currentStage != nullptr && currentStage->failsQuest())
                view.mStatus = Status::Failed;
            (view.mStatus == Status::Running ? out.mRunning : out.mCompleted).push_back(std::move(view));
        }
        sortAndCap(out.mRunning, out.mDropped);
        sortAndCap(out.mCompleted, out.mDropped);
    }

    std::size_t applyTestQuests(
        const QuestIndex& index, QuestState& state, const TextFn& text, std::size_t count, std::vector<FormId>* moved)
    {
        Inputs in;
        in.mQuests = &index;
        in.mText = text;
        std::size_t running = 0;
        bool completedOne = false;
        std::size_t set = 0;
        for (const FormId id : index.questIds())
        {
            if (running >= count && completedOne)
                break;
            const QuestRecord* record = index.quest(id);
            if (record == nullptr || !state.exists(id)
                || resolveText(in, record->mCommon.mId, record->mName, record->mPlugin, StringTableKind::Strings)
                       .empty())
                continue;
            std::vector<const QuestStage*> stages;
            for (const QuestStage& s : record->mStages)
                stages.push_back(&s);
            std::stable_sort(stages.begin(), stages.end(),
                [](const QuestStage* a, const QuestStage* b) { return a->mIndex < b->mIndex; });
            const QuestStage* pick = nullptr;
            const bool wantCompleted = running >= count;
            for (const QuestStage* s : stages)
            {
                bool hasText = false;
                for (const QuestLogEntry& e : s->mEntries)
                    hasText = hasText
                        || !resolveText(
                               in, record->mCommon.mId, e.mText, record->mPlugin, StringTableKind::DlStrings)
                                .empty();
                if (!hasText || s->failsQuest() || s->completesQuest() != wantCompleted)
                    continue;
                pick = s;
                break;
            }
            if (pick == nullptr)
                continue;
            state.setStage(index, id, pick->mIndex);
            if (moved != nullptr)
                moved->push_back(id);
            ++set;
            if (wantCompleted)
                completedOne = true;
            else
                ++running;
        }
        return set;
    }

    namespace
    {
        class Fn final : public openeden::sdk::BindingHandler
        {
        public:
            using Body = std::function<BindingResult(std::span<const BindingValue>)>;
            explicit Fn(Body body)
                : mBody(std::move(body))
            {
            }
            BindingResult call(std::span<const BindingValue> args) override { return mBody(args); }

        private:
            Body mBody;
        };

        BindingValue boolean(bool v)
        {
            BindingValue b;
            b.type = BindingType::Bool;
            b.boolean = v;
            return b;
        }
        BindingValue integer(std::int32_t v)
        {
            BindingValue b;
            b.type = BindingType::Int32;
            b.integer = v;
            return b;
        }
        BindingValue text(std::string_view v)
        {
            BindingValue b;
            b.type = BindingType::String;
            b.text = v;
            return b;
        }
    }

    struct JournalService::Impl
    {
        std::vector<std::unique_ptr<Fn>> mHandlers;
        std::vector<openeden::sdk::FunctionDescriptor> mFunctions;
        std::vector<std::string> mStrings; ///< ids of the last quests() answer (the elements point into it)
        std::vector<BindingValue> mElements;
    };

    JournalService::JournalService(Hooks hooks)
        : mImpl(std::make_unique<Impl>())
        , mHooks(std::move(hooks))
    {
    }

    JournalService::~JournalService() = default;

    bool JournalService::poll()
    {
        if (mReady)
            return true;
        Access access;
        if (mHooks.mAccess && mHooks.mAccess(access) && access.mQuests != nullptr && access.mState != nullptr)
        {
            mAccess = std::move(access);
            mReady = true;
        }
        return mReady;
    }

    std::uint64_t JournalService::revision() const
    {
        if (!mReady)
            return 0;
        // both parts only ever grow, so the sum changes whenever either does; 1 + so that "ready" is never 0
        return 1 + mAccess.mState->revision() + (mHooks.mObjectiveRevision ? mHooks.mObjectiveRevision() : 0);
    }

    const Snapshot& JournalService::snapshot()
    {
        if (!poll())
            return mSnapshot;
        const std::uint64_t now = revision();
        if (!mBuilt || mSnapshot.mRevision != now)
        {
            Inputs in;
            in.mQuests = mAccess.mQuests;
            in.mState = mAccess.mState;
            in.mText = mAccess.mText;
            in.mObjective = mHooks.mObjective;
            in.mAliasName = mAccess.mAliasName;
            mSnapshot.mRevision = now;
            build(in, mSnapshot);
            mBuilt = true;
            ++mBuilds;
            if (mHooks.mLog)
                mHooks.mLog("JOURNAL rebuilt: revision " + std::to_string(now) + ", "
                    + std::to_string(mSnapshot.mRunning.size()) + " running, "
                    + std::to_string(mSnapshot.mCompleted.size()) + " completed (of "
                    + std::to_string(mSnapshot.mActive) + " active quests: "
                    + std::to_string(mSnapshot.mNoName) + " with no name, "
                    + std::to_string(mSnapshot.mNoText) + " with no log text or objective)");
        }
        return mSnapshot;
    }

    openeden::sdk::DeclareResult JournalService::declare(openeden::sdk::BindingsService& service)
    {
        Impl& m = *mImpl;
        JournalService* self = this;
        static const BindingType noParams[] = { BindingType::Bool };
        static const BindingType idParam[] = { BindingType::String };

        const auto add = [&](std::string_view name, std::span<const BindingType> params, BindingType result,
                             Fn::Body body, BindingShape shape = BindingShape::Single) {
            m.mHandlers.push_back(std::make_unique<Fn>(std::move(body)));
            openeden::sdk::FunctionDescriptor f;
            f.name = name;
            f.params = params;
            f.hasResult = true;
            f.result = result;
            f.access = BindingAccess::Read;
            f.handler = m.mHandlers.back().get();
            f.resultShape = shape;
            m.mFunctions.push_back(f);
        };

        const auto found = [self](const BindingValue& id) -> const QuestView* {
            FormId quest = 0;
            if (!parseQuestId(id.text, quest))
                return nullptr;
            return self->snapshot().find(quest);
        };
        const auto textArray = [&m](const std::vector<std::string>& values) { // `values` must outlive the answer
            m.mElements.clear();
            for (const std::string& s : values)
                m.mElements.push_back(text(s));
            BindingResult r;
            r.value.type = BindingType::String;
            r.value.elements = m.mElements.data();
            r.value.count = m.mElements.size();
            return r;
        };

        add("ready", {}, BindingType::Bool, [self](std::span<const BindingValue>) {
            BindingResult r;
            r.value = boolean(self->poll());
            return r;
        });
        add("revision", {}, BindingType::Int32, [self](std::span<const BindingValue>) {
            (void)self->poll();
            BindingResult r;
            r.value = integer(static_cast<std::int32_t>(self->revision() & 0x7FFFFFFF));
            return r;
        });
        add("quests", noParams, BindingType::String,
            [self, &m](std::span<const BindingValue> a) {
                const Snapshot& s = self->snapshot();
                const std::vector<QuestView>& list = a[0].boolean ? s.mCompleted : s.mRunning;
                m.mStrings.clear();
                for (const QuestView& q : list)
                    m.mStrings.push_back(formatQuestId(q.mQuest));
                m.mElements.clear();
                for (const std::string& id : m.mStrings)
                    m.mElements.push_back(text(id));
                BindingResult r;
                r.value.type = BindingType::String;
                r.value.elements = m.mElements.data();
                r.value.count = m.mElements.size();
                return r;
            },
            BindingShape::Array);
        add("name", idParam, BindingType::String, [found](std::span<const BindingValue> a) {
            const QuestView* q = found(a[0]);
            BindingResult r;
            r.value = text(q != nullptr ? std::string_view(q->mName) : std::string_view());
            return r;
        });
        add("entry", idParam, BindingType::String, [found](std::span<const BindingValue> a) {
            const QuestView* q = found(a[0]);
            BindingResult r;
            r.value = text(q != nullptr ? std::string_view(q->mEntry) : std::string_view());
            return r;
        });
        add("status", idParam, BindingType::String, [found](std::span<const BindingValue> a) {
            const QuestView* q = found(a[0]);
            BindingResult r;
            r.value = text(q != nullptr ? std::string_view(statusName(q->mStatus)) : std::string_view());
            return r;
        });
        add("log", idParam, BindingType::String,
            [found, textArray](std::span<const BindingValue> a) {
                static const std::vector<std::string> none;
                const QuestView* q = found(a[0]);
                return textArray(q != nullptr ? q->mLog : none); // by reference: the elements point into it
            },
            BindingShape::Array);
        add("objectives", idParam, BindingType::String,
            [found, &m](std::span<const BindingValue> a) {
                const QuestView* q = found(a[0]);
                m.mElements.clear();
                if (q != nullptr)
                    for (const ObjectiveView& o : q->mObjectives)
                        m.mElements.push_back(text(o.mText));
                BindingResult r;
                r.value.type = BindingType::String;
                r.value.elements = m.mElements.data();
                r.value.count = m.mElements.size();
                return r;
            },
            BindingShape::Array);
        add("objective_done", idParam, BindingType::Bool,
            [found, &m](std::span<const BindingValue> a) {
                const QuestView* q = found(a[0]);
                m.mElements.clear();
                if (q != nullptr)
                    for (const ObjectiveView& o : q->mObjectives)
                        m.mElements.push_back(boolean(o.mDone));
                BindingResult r;
                r.value.type = BindingType::Bool;
                r.value.elements = m.mElements.data();
                r.value.count = m.mElements.size();
                return r;
            },
            BindingShape::Array);

        openeden::sdk::NamespaceDescriptor ns;
        ns.name = "skyrimse.journal";
        ns.version = { 1, 0 };
        ns.functions = m.mFunctions;
        return service.declareNamespace(ns);
    }
}
