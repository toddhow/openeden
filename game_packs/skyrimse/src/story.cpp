#include "game_skyrimse/story.hpp"

#include "game_skyrimse/pack_core.hpp"
#include "game_skyrimse/survey.hpp"

#include <algorithm>
#include <cstring>
#include <memory>
#include <sstream>

// Layout and rules: see the header (task 0187). Facts read from Skyrim.esm are VERIFIED there; the walk is
// RECALLED-VERIFY.

namespace game_skyrimse
{
    namespace
    {
        template <class T>
        T readLe(const std::byte* p)
        {
            T value;
            std::memcpy(&value, p, sizeof(T));
            return value;
        }

        bool usable(FormId id)
        {
            return id != kNullFormId && id != kUnresolvedFormId;
        }

        std::uint64_t splitmix(std::uint64_t& state)
        {
            std::uint64_t z = (state += 0x9E3779B97F4A7C15ull);
            z = (z ^ (z >> 30)) * 0xBF58476D1CE4E5B9ull;
            z = (z ^ (z >> 27)) * 0x94D049BB133111EBull;
            return z ^ (z >> 31);
        }

        class StoryParser final : public Content::Visitor
        {
        public:
            StoryParser(const Content::Plugin& plugin, const FormIdResolver& resolver, const StoryLimits& limits,
                StoryRecords& out)
                : mPlugin(plugin)
                , mResolver(resolver)
                , mLimits(limits)
                , mOut(out)
            {
            }

            bool onRecord(const Content::RawRecord& record) override
            {
                const bool smbn = record.mType == Content::fourCC("SMBN");
                const bool smqn = record.mType == Content::fourCC("SMQN");
                const bool smen = record.mType == Content::fourCC("SMEN");
                if (!smbn && !smqn && !smen)
                    return true;
                TypeCount& count = mOut.mCounts[record.mType];
                ++count.mSeen;
                if ((record.mFlags & RecordFlags::Deleted) != 0)
                    ++count.mDeleted;
                if (count.mParsed >= mLimits.mMaxRecordsPerType)
                {
                    fail(record, "more than " + std::to_string(mLimits.mMaxRecordsPerType) + " records of one type");
                    return true;
                }
                std::span<const std::byte> bytes = record.mData;
                if (record.isCompressed())
                {
                    if (record.mUncompressedSize > mLimits.mMaxRecordBytes)
                    {
                        fail(record, "the record inflates to " + std::to_string(record.mUncompressedSize) + " bytes");
                        return true;
                    }
                    Content::Error error;
                    if (!inflateRecord(record, mPlugin.name(), mInflated, error))
                    {
                        ++count.mFailed;
                        if (mOut.mErrors.size() < mLimits.mMaxErrors)
                            mOut.mErrors.push_back(std::move(error));
                        else
                            ++mOut.mErrorsDropped;
                        return true;
                    }
                    bytes = mInflated;
                }
                else if (record.mDataSize > mLimits.mMaxRecordBytes)
                {
                    fail(record, "the record is " + std::to_string(record.mDataSize) + " bytes");
                    return true;
                }
                mWhy.clear();
                if (readNode(record, bytes, smbn ? StoryNodeKind::Branch : smqn ? StoryNodeKind::Quest : StoryNodeKind::Event))
                    ++count.mParsed;
                else
                    fail(record, mWhy);
                return true;
            }

        private:
            void fail(const Content::RawRecord& record, std::string message)
            {
                ++mOut.mCounts[record.mType].mFailed;
                if (mOut.mErrors.size() < mLimits.mMaxErrors)
                {
                    Content::Error error;
                    error.mFile = mPlugin.name();
                    error.mOffset = record.mOffset;
                    error.mRecordType = record.mType;
                    error.mMessage = std::move(message);
                    mOut.mErrors.push_back(std::move(error));
                }
                else
                    ++mOut.mErrorsDropped;
            }

            void odd(std::uint32_t sub)
            {
                ++mOut.mOddSubrecords;
                ++mOut.mOddBy[Content::fourCCToString(sub)];
            }

            FormId resolved(std::uint32_t stored)
            {
                const std::optional<FormId> id = mResolver.resolve(stored);
                if (!id)
                {
                    ++mOut.mUnresolvedFormIds;
                    return kUnresolvedFormId;
                }
                return *id;
            }

            bool u32Field(std::uint32_t sub, std::span<const std::byte> data, std::uint32_t& out)
            {
                if (data.size() != 4)
                {
                    odd(sub);
                    return false;
                }
                out = readLe<std::uint32_t>(data.data());
                return true;
            }

            bool readNode(const Content::RawRecord& record, std::span<const std::byte> bytes, StoryNodeKind kind)
            {
                StoryNode n;
                n.mKind = kind;
                n.mCommon.mStoredId = record.mFormId;
                n.mCommon.mId = resolved(record.mFormId);
                n.mCommon.mFlags = record.mFlags;
                n.mCommon.mOffset = record.mOffset;
                survey::Ctda* last = nullptr;
                const bool wellFormed = survey::forEachSubrecord(bytes, [&](const survey::SubrecordView& sub) {
                    if (!mWhy.empty())
                        return;
                    const std::uint32_t type = sub.mType;
                    const std::span<const std::byte> data = sub.mData;
                    std::uint32_t v = 0;
                    switch (type)
                    {
                        case Content::fourCC("EDID"):
                        {
                            const char* begin = reinterpret_cast<const char*>(data.data());
                            const void* nul = data.empty() ? nullptr : std::memchr(begin, '\0', data.size());
                            n.mCommon.mEditorId.assign(
                                begin, nul != nullptr ? static_cast<const char*>(nul) : begin + data.size());
                            return;
                        }
                        case Content::fourCC("PNAM"):
                            if (u32Field(type, data, v))
                                n.mParent = v == 0 ? kNullFormId : resolved(v);
                            return;
                        case Content::fourCC("SNAM"):
                            if (u32Field(type, data, v))
                                n.mSibling = v == 0 ? kNullFormId : resolved(v);
                            return;
                        case Content::fourCC("DNAM"):
                            (void)u32Field(type, data, n.mFlags);
                            return;
                        case Content::fourCC("XNAM"):
                            (void)u32Field(type, data, n.mMaxConcurrent);
                            return;
                        case Content::fourCC("MNAM"):
                            (void)u32Field(type, data, n.mQuestsToRun);
                            return;
                        case Content::fourCC("ENAM"):
                            if (data.size() != 4)
                            {
                                odd(type);
                                return;
                            }
                            n.mEvent.assign(reinterpret_cast<const char*>(data.data()), 4);
                            return;
                        case Content::fourCC("NNAM"):
                            if (u32Field(type, data, v))
                            {
                                if (n.mQuests.size() >= mLimits.mMaxQuests)
                                {
                                    mWhy = "more than " + std::to_string(mLimits.mMaxQuests) + " quests in one node";
                                    return;
                                }
                                n.mQuests.push_back(resolved(v));
                            }
                            return;
                        case Content::fourCC("CTDA"):
                        {
                            if (n.mConditions.size() >= mLimits.mMaxConditions)
                            {
                                mWhy = "more than " + std::to_string(mLimits.mMaxConditions) + " conditions";
                                return;
                            }
                            survey::Ctda c;
                            if (!survey::decodeCtda(data, c))
                            {
                                n.mConditionsOk = false;
                                last = nullptr;
                                return;
                            }
                            n.mConditions.push_back(std::move(c));
                            last = &n.mConditions.back();
                            return;
                        }
                        case Content::fourCC("CIS1"):
                        case Content::fourCC("CIS2"):
                        {
                            if (last == nullptr)
                                return;
                            const char* begin = reinterpret_cast<const char*>(data.data());
                            const void* nul = data.empty() ? nullptr : std::memchr(begin, '\0', data.size());
                            (type == Content::fourCC("CIS1") ? last->mString1 : last->mString2)
                                .assign(begin, nul != nullptr ? static_cast<const char*>(nul) : begin + data.size());
                            return;
                        }
                        default:
                            return; // CITC (the count), QNAM (the count), RNAM, FNAM: not read
                    }
                });
                if (!wellFormed && mWhy.empty())
                    mWhy = "a subrecord runs past the end of the record";
                if (!mWhy.empty())
                    return false;
                mOut.mNodes.push_back(std::move(n));
                return true;
            }

            const Content::Plugin& mPlugin;
            const FormIdResolver& mResolver;
            const StoryLimits& mLimits;
            StoryRecords& mOut;
            std::vector<std::byte> mInflated;
            std::string mWhy;
        };
    }

    bool parseStoryRecords(
        const Content::Plugin& plugin, const LoadOrder& order, const StoryLimits& limits, StoryRecords& out)
    {
        out = StoryRecords();
        out.mName = plugin.name();
        const FormIdResolver resolver(plugin.header(), plugin.name(), order);
        StoryParser parser(plugin, resolver, limits, out);
        const std::optional<Content::Error> error = plugin.walk(parser);
        if (error)
        {
            out.mWalkError = *error;
            return false;
        }
        return true;
    }

    bool parseStoryRecordsBytes(const std::string& bytes, const std::string& name, const LoadOrder& order,
        const StoryLimits& limits, StoryRecords& out, Content::Error& error)
    {
        const std::unique_ptr<Content::Plugin> plugin
            = Content::Plugin::open(std::make_unique<std::istringstream>(bytes, std::ios::binary), name, error);
        if (plugin == nullptr)
            return false;
        if (!parseStoryRecords(*plugin, order, limits, out))
        {
            if (out.mWalkError)
                error = *out.mWalkError;
            return false;
        }
        return true;
    }

    // ---- StoryIndex ------------------------------------------------------------------------------------------------

    const std::vector<FormId> StoryIndex::kNoChildren;

    void StoryIndex::add(StoryRecords records, FormIdResolver resolver)
    {
        const std::uint32_t plugin = static_cast<std::uint32_t>(mResolvers.size());
        mResolvers.push_back(std::move(resolver));
        for (StoryNode& n : records.mNodes)
        {
            if (!usable(n.mCommon.mId))
                continue;
            if (n.mCommon.deleted())
            {
                mNodes.erase(n.mCommon.mId);
                continue;
            }
            n.mPlugin = plugin;
            mNodes[n.mCommon.mId] = std::move(n);
        }
        mDirty = true;
    }

    const StoryNode* StoryIndex::node(FormId id) const
    {
        const auto it = mNodes.find(id);
        return it == mNodes.end() ? nullptr : &it->second;
    }

    void StoryIndex::rebuild() const
    {
        mChildren.clear();
        std::map<FormId, std::vector<FormId>> byParent; // sorted: the fallback order
        for (const auto& [id, n] : mNodes)
            byParent[n.mParent].push_back(id);
        for (auto& [parent, kids] : byParent)
        {
            std::sort(kids.begin(), kids.end());
            // SNAM is the previous sibling: the head is the child whose sibling is none or not a child of this parent
            std::map<FormId, FormId> next; // previous -> this
            std::vector<FormId> heads;
            bool ok = true;
            for (FormId k : kids)
            {
                const FormId prev = mNodes.at(k).mSibling;
                if (prev == kNullFormId || std::find(kids.begin(), kids.end(), prev) == kids.end())
                    heads.push_back(k);
                else if (!next.emplace(prev, k).second)
                    ok = false; // two children name the same previous sibling
            }
            std::vector<FormId> chain;
            if (ok && heads.size() == 1)
            {
                FormId at = heads[0];
                while (chain.size() <= kids.size())
                {
                    chain.push_back(at);
                    const auto it = next.find(at);
                    if (it == next.end())
                        break;
                    at = it->second;
                }
            }
            mChildren[parent] = chain.size() == kids.size() ? chain : kids;
        }
        mDirty = false;
    }

    const std::vector<FormId>& StoryIndex::children(FormId parent) const
    {
        if (mDirty)
            rebuild();
        const auto it = mChildren.find(parent);
        return it == mChildren.end() ? kNoChildren : it->second;
    }

    std::vector<FormId> StoryIndex::eventNodes(const std::string& event) const
    {
        std::vector<FormId> out;
        for (const auto& [id, n] : mNodes)
            if (n.mKind == StoryNodeKind::Event && n.mEvent == event)
                out.push_back(id);
        std::sort(out.begin(), out.end());
        return out;
    }

    std::size_t StoryIndex::count(StoryNodeKind kind) const
    {
        std::size_t n = 0;
        for (const auto& [id, node] : mNodes)
            n += node.mKind == kind ? 1 : 0;
        return n;
    }

    // ---- fn_576 GetEventData -----------------------------------------------------------------------------------------

    namespace
    {
        float getEventData(const survey::Ctda& c, conditions::Subject, const conditions::ConditionWorld& world)
        {
            const StoryEventSource* source = dynamic_cast<const StoryEventSource*>(&world);
            const StoryEvent* e = source != nullptr ? source->currentStoryEvent() : nullptr;
            if (e == nullptr)
                return 0.f;
            const std::uint32_t function = c.mParam1 & 0xFFFFu;
            const char kind = static_cast<char>((c.mParam1 >> 16) & 0xFF);
            const char which = static_cast<char>((c.mParam1 >> 24) & 0xFF);
            const FormId form = source->resolveStoryForm(c.mParam2);
            switch (kind)
            {
                case 'K':
                    return which == '1' && function == 0 && usable(form) && e->mKeyword == form ? 1.f : 0.f;
                case 'L':
                {
                    const FormId location = which == '1' ? e->mLocation1 : which == '2' ? e->mLocation2 : kNullFormId;
                    if (location == kNullFormId || !usable(form))
                        return 0.f;
                    if (function == 0)
                        return source->storyLocationWithin(location, form) ? 1.f : 0.f;
                    if (function == 3)
                        return source->storyLocationHasKeyword(location, form) ? 1.f : 0.f;
                    return 0.f;
                }
                case 'R':
                {
                    const FormId ref = which == '1' ? e->mRef1 : which == '2' ? e->mRef2 : kNullFormId;
                    if (function != 0 || ref == kNullFormId || !usable(form))
                        return 0.f;
                    return ref == form || source->storyBaseOf(ref) == form ? 1.f : 0.f;
                }
                case 'V':
                    if (function != 2)
                        return 0.f;
                    return static_cast<float>(which == '1' ? e->mValue1 : which == '2' ? e->mValue2 : 0);
                default:
                    return 0.f;
            }
        }
    }

    const std::vector<conditions::ExtraFunction>& storyExtraFunctions()
    {
        static const std::vector<conditions::ExtraFunction> table = { { 576, &getEventData } };
        return table;
    }

    // ---- StoryManager ----------------------------------------------------------------------------------------------

    struct StoryManager::Walk
    {
        const StoryEvent& mEvent;
        const Evaluate& mEvaluate;
        const Startable& mStartable;
        std::vector<FormId> mStarted;
        bool mStop = false;
    };

    StoryManager::StoryManager(const StoryIndex& nodes, const QuestIndex& quests, Options options)
        : mNodes(nodes)
        , mQuests(quests)
        , mOptions(options)
    {
    }

    bool StoryManager::visit(FormId id, Walk& walk)
    {
        const StoryNode* n = mNodes.node(id);
        if (n == nullptr)
            return false;
        ++mStats.mNodesVisited;
        if (!n->mConditionsOk || !walk.mEvaluate(n->mConditions, mNodes.resolverOf(*n)))
            return false;
        ++mStats.mNodesPassed;
        if (n->mKind != StoryNodeKind::Quest)
        {
            std::vector<FormId> kids = mNodes.children(id);
            if ((n->mFlags & StoryFlags::Random) != 0 && kids.size() > 1)
            {
                std::uint64_t state = (mEventCounter * 0x9E3779B97F4A7C15ull) ^ id;
                for (std::size_t i = kids.size() - 1; i > 0; --i)
                    std::swap(kids[i], kids[static_cast<std::size_t>(splitmix(state) % (i + 1))]);
            }
            for (FormId kid : kids)
            {
                visit(kid, walk);
                if (walk.mStop)
                    break;
            }
            return false;
        }
        // a quest node
        const bool doAll = (n->mFlags & StoryFlags::DoAllBeforeRepeating) != 0;
        std::set<FormId>& done = mCycle[id];
        if (doAll && !n->mQuests.empty()
            && std::all_of(n->mQuests.begin(), n->mQuests.end(), [&done](FormId q) { return done.count(q) != 0; }))
            done.clear();
        const std::uint32_t wanted = (n->mFlags & StoryFlags::NumQuestsToRun) != 0 && n->mQuestsToRun != 0
            ? n->mQuestsToRun
            : mOptions.mDefaultQuestsToRun;
        std::uint32_t startedHere = 0;
        for (FormId q : n->mQuests)
        {
            if (startedHere >= wanted)
                break;
            if (doAll && done.count(q) != 0)
                continue;
            if (std::find(walk.mStarted.begin(), walk.mStarted.end(), q) != walk.mStarted.end())
                continue;
            const QuestRecord* record = usable(q) ? mQuests.quest(q) : nullptr;
            if (record == nullptr || !walk.mStartable(q))
            {
                ++mStats.mQuestsSkipped;
                continue;
            }
            if (!record->mStartConditionsOk || !walk.mEvaluate(record->mStartConditions, mQuests.resolverOf(*record)))
            {
                ++mStats.mQuestsSkipped;
                continue;
            }
            walk.mStarted.push_back(q);
            done.insert(q);
            ++mStats.mQuestsStarted;
            ++startedHere;
        }
        if (startedHere > 0 && mOptions.mStopAfterStart && (n->mFlags & StoryFlags::SharesEvent) == 0)
            walk.mStop = true;
        return startedHere > 0;
    }

    std::vector<FormId> StoryManager::raise(const StoryEvent& event, const Evaluate& evaluate, const Startable& startable)
    {
        ++mEventCounter;
        ++mStats.mEvents;
        const std::vector<FormId> roots = mNodes.eventNodes(event.mType);
        if (roots.empty())
        {
            ++mStats.mEventsWithNoNode;
            return {};
        }
        Walk walk{ event, evaluate, startable, {}, false };
        for (FormId root : roots)
        {
            visit(root, walk);
            if (walk.mStop)
                break;
        }
        return std::move(walk.mStarted);
    }
}
