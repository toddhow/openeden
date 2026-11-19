#include "game_skyrimse/scene.hpp"

#include "game_skyrimse/pack_core.hpp"
#include "game_skyrimse/story.hpp"
#include "game_skyrimse/survey.hpp"

#include <algorithm>
#include <set>
#include <cstring>
#include <memory>
#include <sstream>

// Layout: see the header (task 0194). Facts read from the five masters are VERIFIED there; names are RECALLED-VERIFY.

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

        class SceneParser final : public Content::Visitor
        {
        public:
            SceneParser(const Content::Plugin& plugin, const FormIdResolver& resolver, const SceneLimits& limits,
                SceneRecords& out)
                : mPlugin(plugin)
                , mResolver(resolver)
                , mLimits(limits)
                , mOut(out)
            {
            }

            bool onRecord(const Content::RawRecord& record) override
            {
                if (record.mType != Content::fourCC("SCEN"))
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
                if (readScene(record, bytes))
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
                if (stored == 0)
                    return kNullFormId;
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

            static std::string text(std::span<const std::byte> data)
            {
                const char* begin = reinterpret_cast<const char*>(data.data());
                const void* nul = data.empty() ? nullptr : std::memchr(begin, '\0', data.size());
                return std::string(begin, nul != nullptr ? static_cast<const char*>(nul) : begin + data.size());
            }

            bool readScene(const Content::RawRecord& record, std::span<const std::byte> bytes)
            {
                SceneRecord s;
                s.mCommon.mStoredId = record.mFormId;
                s.mCommon.mId = resolved(record.mFormId);
                s.mCommon.mFlags = record.mFlags;
                s.mCommon.mOffset = record.mOffset;
                bool sawFlags = false;
                bool inPhase = false;
                bool inAction = false;
                bool afterVnam = false;
                bool afterActions = false;
                int nexts = 0;
                std::uint32_t snamInAction = 0;
                survey::Ctda* last = nullptr;
                std::vector<survey::Ctda>* target = nullptr;
                const bool wellFormed = survey::forEachSubrecord(bytes, [&](const survey::SubrecordView& sub) {
                    if (!mWhy.empty())
                        return;
                    const std::uint32_t type = sub.mType;
                    const std::span<const std::byte> data = sub.mData;
                    std::uint32_t v = 0;
                    switch (type)
                    {
                        case Content::fourCC("EDID"):
                            s.mCommon.mEditorId = text(data);
                            return;
                        case Content::fourCC("VMAD"):
                        {
                            vmad::ParseResult parsed = vmad::parseScene(data);
                            if (!parsed.mOk)
                            {
                                odd(type);
                                return;
                            }
                            (void)vmad::resolveForms(parsed.mVmad, [this](std::uint32_t stored) {
                                const std::optional<FormId> id = mResolver.resolve(stored);
                                return id ? *id : 0u;
                            });
                            s.mVmad = std::make_shared<const vmad::Vmad>(std::move(parsed.mVmad));
                            return;
                        }
                        case Content::fourCC("FNAM"):
                            if (inAction)
                                (void)u32Field(type, data, s.mActions.back().mFlags);
                            else if (!sawFlags && s.mPhases.empty() && u32Field(type, data, v))
                            {
                                s.mFlags = v;
                                sawFlags = true;
                            }
                            return;
                        case Content::fourCC("HNAM"):
                            if (!inPhase)
                            {
                                if (s.mPhases.size() >= mLimits.mMaxPhases)
                                {
                                    mWhy = "more than " + std::to_string(mLimits.mMaxPhases) + " phases";
                                    return;
                                }
                                s.mPhases.emplace_back();
                                inPhase = true;
                                nexts = 0;
                                target = &s.mPhases.back().mStartConditions;
                                last = nullptr;
                            }
                            else
                            {
                                inPhase = false;
                                target = nullptr;
                                last = nullptr;
                            }
                            return;
                        case Content::fourCC("NEXT"):
                            if (inPhase)
                            {
                                ++nexts;
                                target = nexts == 1 ? &s.mPhases.back().mCompletionConditions : nullptr;
                                last = nullptr;
                            }
                            return;
                        case Content::fourCC("NAM0"):
                            if (inAction)
                                s.mActions.back().mName = text(data);
                            else if (inPhase)
                                s.mPhases.back().mName = text(data);
                            return;
                        case Content::fourCC("ALID"):
                            if (inAction)
                                (void)u32Field(type, data, s.mActions.back().mAlias);
                            else if (!afterActions && u32Field(type, data, v))
                            {
                                if (s.mActors.size() >= mLimits.mMaxActors)
                                {
                                    mWhy = "more than " + std::to_string(mLimits.mMaxActors) + " actors";
                                    return;
                                }
                                SceneActor a;
                                a.mAlias = v;
                                s.mActors.push_back(a);
                            }
                            return;
                        case Content::fourCC("LNAM"):
                            if (!s.mActors.empty() && !inAction)
                                (void)u32Field(type, data, s.mActors.back().mFlags);
                            return;
                        case Content::fourCC("DNAM"):
                            if (!s.mActors.empty() && !inAction)
                                (void)u32Field(type, data, s.mActors.back().mBehavior);
                            return;
                        case Content::fourCC("ANAM"):
                            if (data.size() == 2)
                            {
                                if (s.mActions.size() >= mLimits.mMaxActions)
                                {
                                    mWhy = "more than " + std::to_string(mLimits.mMaxActions) + " actions";
                                    return;
                                }
                                SceneAction a;
                                a.mRawType = readLe<std::uint16_t>(data.data());
                                a.mType = a.mRawType == 0 ? SceneActionType::Dialogue
                                    : a.mRawType == 1     ? SceneActionType::Package
                                    : a.mRawType == 2     ? SceneActionType::Timer
                                                          : SceneActionType::Unknown;
                                s.mActions.push_back(std::move(a));
                                inAction = true;
                                snamInAction = 0;
                                target = nullptr;
                                last = nullptr;
                            }
                            else if (data.empty())
                            {
                                inAction = false;
                                afterActions = true;
                            }
                            else
                                odd(type);
                            return;
                        case Content::fourCC("INAM"):
                            if (inAction)
                                (void)u32Field(type, data, s.mActions.back().mIndex);
                            return;
                        case Content::fourCC("SNAM"):
                            if (inAction)
                            {
                                if (++snamInAction == 1)
                                    (void)u32Field(type, data, s.mActions.back().mStartPhase);
                                else if (data.size() == 4)
                                {
                                    s.mActions.back().mSeconds = readLe<float>(data.data());
                                    s.mActions.back().mHasSeconds = true;
                                }
                                else
                                    odd(type);
                            }
                            return;
                        case Content::fourCC("ENAM"):
                            if (inAction)
                                (void)u32Field(type, data, s.mActions.back().mEndPhase);
                            return;
                        case Content::fourCC("DATA"):
                            if (inAction && u32Field(type, data, v))
                                s.mActions.back().mTopic = resolved(v);
                            return;
                        case Content::fourCC("HTID"):
                            if (inAction)
                                (void)u32Field(type, data, s.mActions.back().mHeadtrackAlias);
                            return;
                        case Content::fourCC("PNAM"):
                            if (!u32Field(type, data, v))
                                return;
                            if (inAction)
                            {
                                if (s.mActions.back().mPackages.size() >= mLimits.mMaxPackages)
                                {
                                    mWhy = "more than " + std::to_string(mLimits.mMaxPackages)
                                        + " packages in one action";
                                    return;
                                }
                                s.mActions.back().mPackages.push_back(resolved(v));
                            }
                            else
                                s.mQuest = resolved(v);
                            return;
                        case Content::fourCC("VNAM"):
                            afterVnam = true;
                            target = &s.mConditions;
                            last = nullptr;
                            return;
                        case Content::fourCC("CTDA"):
                        {
                            if (target == nullptr)
                            {
                                odd(type);
                                return;
                            }
                            if (target->size() >= mLimits.mMaxConditions)
                            {
                                mWhy = "more than " + std::to_string(mLimits.mMaxConditions) + " conditions";
                                return;
                            }
                            survey::Ctda c;
                            if (!survey::decodeCtda(data, c))
                            {
                                if (afterVnam)
                                    s.mConditionsOk = false;
                                else if (inPhase)
                                    s.mPhases.back().mConditionsOk = false;
                                last = nullptr;
                                return;
                            }
                            target->push_back(std::move(c));
                            last = &target->back();
                            return;
                        }
                        case Content::fourCC("CIS1"):
                        case Content::fourCC("CIS2"):
                            if (last != nullptr)
                                (type == Content::fourCC("CIS1") ? last->mString1 : last->mString2) = text(data);
                            return;
                        default:
                            return; // DMAX, DMIN, DEMO, DEVA, WNAM, SCHR, SCDA, SCTX, QNAM, SCRO: not read
                    }
                });
                if (!wellFormed && mWhy.empty())
                    mWhy = "a subrecord runs past the end of the record";
                if (!mWhy.empty())
                    return false;
                mOut.mScenes.push_back(std::move(s));
                return true;
            }

            const Content::Plugin& mPlugin;
            const FormIdResolver& mResolver;
            const SceneLimits& mLimits;
            SceneRecords& mOut;
            std::vector<std::byte> mInflated;
            std::string mWhy;
        };
    }

    const SceneActor* SceneRecord::actor(std::uint32_t alias) const
    {
        for (const SceneActor& a : mActors)
            if (a.mAlias == alias)
                return &a;
        return nullptr;
    }

    const SceneAction* SceneRecord::actionByIndex(std::uint32_t index) const
    {
        for (const SceneAction& a : mActions)
            if (a.mIndex == index)
                return &a;
        return nullptr;
    }

    bool parseSceneRecords(
        const Content::Plugin& plugin, const LoadOrder& order, const SceneLimits& limits, SceneRecords& out)
    {
        out = SceneRecords();
        out.mName = plugin.name();
        const FormIdResolver resolver(plugin.header(), plugin.name(), order);
        SceneParser parser(plugin, resolver, limits, out);
        const std::optional<Content::Error> error = plugin.walk(parser);
        if (error)
        {
            out.mWalkError = *error;
            return false;
        }
        return true;
    }

    bool parseSceneRecordsBytes(const std::string& bytes, const std::string& name, const LoadOrder& order,
        const SceneLimits& limits, SceneRecords& out, Content::Error& error)
    {
        const std::unique_ptr<Content::Plugin> plugin
            = Content::Plugin::open(std::make_unique<std::istringstream>(bytes, std::ios::binary), name, error);
        if (plugin == nullptr)
            return false;
        if (!parseSceneRecords(*plugin, order, limits, out))
        {
            if (out.mWalkError)
                error = *out.mWalkError;
            return false;
        }
        return true;
    }

    void SceneIndex::add(SceneRecords records, FormIdResolver resolver)
    {
        const std::uint32_t plugin = static_cast<std::uint32_t>(mResolvers.size());
        mResolvers.push_back(std::move(resolver));
        for (SceneRecord& s : records.mScenes)
        {
            if (!usable(s.mCommon.mId))
                continue;
            if (s.mCommon.deleted())
            {
                mScenes.erase(s.mCommon.mId);
                continue;
            }
            s.mPlugin = plugin;
            mScenes[s.mCommon.mId] = std::move(s);
        }
        mByQuest.clear();
        for (const auto& [id, s] : mScenes)
            if (usable(s.mQuest))
                mByQuest[s.mQuest].push_back(id);
        for (auto& [quest, list] : mByQuest)
            std::sort(list.begin(), list.end());
    }

    namespace
    {
        float getSceneActionComplete(
            const survey::Ctda& c, conditions::Subject, const conditions::ConditionWorld& world)
        {
            const SceneStateSource* state = dynamic_cast<const SceneStateSource*>(&world);
            const StoryEventSource* forms = dynamic_cast<const StoryEventSource*>(&world);
            if (state == nullptr || forms == nullptr)
                return 0.f;
            return state->sceneActionComplete(forms->resolveStoryForm(c.mParam1), c.mParam2) ? 1.f : 0.f;
        }
    }

    const std::vector<conditions::ExtraFunction>& sceneExtraFunctions()
    {
        static const std::vector<conditions::ExtraFunction> table = { { 550, &getSceneActionComplete } };
        return table;
    }

    const SceneRecord* SceneIndex::scene(FormId id) const
    {
        const auto it = mScenes.find(id);
        return it == mScenes.end() ? nullptr : &it->second;
    }

    std::vector<FormId> SceneIndex::dialogueTopics() const
    {
        std::set<FormId> topics;
        for (const auto& [id, scene] : mScenes)
            for (const SceneAction& action : scene.mActions)
                if (action.mTopic != kNullFormId && action.mTopic != kUnresolvedFormId)
                    topics.insert(action.mTopic);
        return std::vector<FormId>(topics.begin(), topics.end());
    }

    std::vector<FormId> SceneIndex::scenesOfQuest(FormId quest) const
    {
        const auto it = mByQuest.find(quest);
        return it == mByQuest.end() ? std::vector<FormId>() : it->second;
    }
}
