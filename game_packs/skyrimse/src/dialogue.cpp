#include "game_skyrimse/dialogue.hpp"

#include "game_skyrimse/package.hpp"
#include "game_skyrimse/placed_refs.hpp"

#include "game_skyrimse/actor_mechanics.hpp"
#include "game_skyrimse/location.hpp"
#include "game_skyrimse/pack_core.hpp"
#include "game_skyrimse/quest_script.hpp"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstring>
#include <limits>
#include <memory>
#include <sstream>

// Layout status of every subrecord read here: see the header (task 0137). VERIFIED on the five installed Skyrim SE
// masters by tools/skyrimse_dialogue_check; RECALLED-VERIFY where the header says so. Nothing is copied from xEdit or
// the Creation Kit wiki.

namespace game_skyrimse
{
    std::string findFragmentScriptName(std::span<const std::byte> vmad)
    {
        constexpr std::size_t kLength = 13; // "TIF__" + 8 hexadecimal digits
        for (std::size_t i = 0; i + 2 + kLength <= vmad.size(); ++i)
        {
            std::uint16_t length;
            std::memcpy(&length, vmad.data() + i, 2);
            if (length != kLength)
                continue;
            const char* chars = reinterpret_cast<const char*>(vmad.data() + i + 2);
            if (std::memcmp(chars, "TIF__", 5) != 0)
                continue;
            bool hex = true;
            for (std::size_t k = 5; k < kLength; ++k)
                hex = hex && std::isxdigit(static_cast<unsigned char>(chars[k])) != 0;
            if (hex)
                return std::string(chars, kLength);
        }
        return std::string();
    }

    namespace
    {
        template <class T>
        T readLe(const std::byte* p)
        {
            T value;
            std::memcpy(&value, p, sizeof(T));
            return value;
        }

        std::string tag(std::uint32_t type)
        {
            return Content::fourCCToString(type);
        }

        Content::Error makeError(const std::string& file, std::uint64_t offset, std::uint32_t type, std::string message)
        {
            Content::Error error;
            error.mFile = file;
            error.mOffset = offset;
            error.mRecordType = type;
            error.mMessage = std::move(message);
            return error;
        }

        class DialogueParser final : public Content::Visitor
        {
        public:
            DialogueParser(const Content::Plugin& plugin, const FormIdResolver& resolver, const DialogueLimits& limits,
                DialogueRecords& out)
                : mPlugin(plugin)
                , mResolver(resolver)
                , mLimits(limits)
                , mOut(out)
                , mLocalized(plugin.header().isLocalized())
            {
            }

            bool onGroupBegin(const Content::GroupInfo& group) override
            {
                if (group.mType == 7)
                {
                    mInTopicGroup = true;
                    mGroupTopic = resolved(group.mLabel);
                }
                return true;
            }

            bool onGroupEnd(const Content::GroupInfo& group) override
            {
                if (group.mType == 7)
                {
                    mInTopicGroup = false;
                    mGroupTopic = kNullFormId;
                }
                return true;
            }

            bool onRecord(const Content::RawRecord& record) override
            {
                switch (record.mType)
                {
                    case Content::fourCC("DIAL"):
                    case Content::fourCC("INFO"):
                    case Content::fourCC("GLOB"):
                    case Content::fourCC("FLST"):
                    case Content::fourCC("VTYP"):
                        break;
                    default:
                        return true;
                }
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
                        fail(record,
                            "the record inflates to " + std::to_string(record.mUncompressedSize)
                                + " bytes, over the limit of " + std::to_string(mLimits.mMaxRecordBytes));
                        return true;
                    }
                    Content::Error error;
                    if (!inflateRecord(record, mPlugin.name(), mInflated, error))
                    {
                        addError(error, record.mType);
                        return true;
                    }
                    bytes = mInflated;
                }
                else if (record.mDataSize > mLimits.mMaxRecordBytes)
                {
                    fail(record,
                        "the record is " + std::to_string(record.mDataSize) + " bytes, over the limit of "
                            + std::to_string(mLimits.mMaxRecordBytes));
                    return true;
                }

                mWhy.clear();
                mCurrentType = record.mType;
                bool ok = false;
                switch (record.mType)
                {
                    case Content::fourCC("DIAL"):
                        ok = readTopic(record, bytes);
                        break;
                    case Content::fourCC("INFO"):
                        ok = readInfo(record, bytes);
                        break;
                    case Content::fourCC("GLOB"):
                        ok = readGlobal(record, bytes);
                        break;
                    case Content::fourCC("VTYP"):
                        ok = readVoiceType(record, bytes);
                        break;
                    default:
                        ok = readFormList(record, bytes);
                        break;
                }
                if (ok)
                    ++count.mParsed;
                else
                    fail(record, mWhy);
                return true;
            }

        private:
            void addError(Content::Error error, std::uint32_t type)
            {
                ++mOut.mCounts[type].mFailed;
                if (mOut.mErrors.size() < mLimits.mMaxErrors)
                    mOut.mErrors.push_back(std::move(error));
                else
                    ++mOut.mErrorsDropped;
            }

            void fail(const Content::RawRecord& record, std::string message)
            {
                addError(makeError(mPlugin.name(), record.mOffset, record.mType, std::move(message)), record.mType);
            }

            void odd(std::uint32_t sub)
            {
                ++mOut.mOddSubrecords;
                ++mOut.mOddBy[tag(mCurrentType) + "." + tag(sub)];
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

            RecordCommon common(const Content::RawRecord& record)
            {
                RecordCommon c;
                c.mStoredId = record.mFormId;
                c.mId = resolved(record.mFormId);
                c.mFlags = record.mFlags;
                c.mOffset = record.mOffset;
                return c;
            }

            bool zstring(std::uint32_t sub, std::span<const std::byte> data, std::string& out)
            {
                if (data.size() > mLimits.mMaxStringBytes)
                {
                    mWhy = "subrecord " + tag(sub) + " is " + std::to_string(data.size())
                        + " bytes, over the string limit of " + std::to_string(mLimits.mMaxStringBytes);
                    return false;
                }
                const char* begin = reinterpret_cast<const char*>(data.data());
                const void* nul = data.empty() ? nullptr : std::memchr(begin, '\0', data.size());
                out.assign(begin, nul != nullptr ? static_cast<const char*>(nul) : begin + data.size());
                return true;
            }

            bool lstring(std::uint32_t sub, std::span<const std::byte> data, LString& out)
            {
                if (mLocalized)
                {
                    if (data.size() != 4)
                    {
                        odd(sub);
                        return true;
                    }
                    out.mPresent = true;
                    out.mLocalized = true;
                    out.mId = readLe<std::uint32_t>(data.data());
                    return true;
                }
                out.mPresent = true;
                return zstring(sub, data, out.mText);
            }

            bool formField(std::uint32_t sub, std::span<const std::byte> data, FormId& out)
            {
                if (data.size() != 4)
                {
                    odd(sub);
                    return true;
                }
                out = resolved(readLe<std::uint32_t>(data.data()));
                return true;
            }

            /// Runs `each` over the subrecords; a framing fault fails the record.
            template <class F>
            bool walkSubs(std::span<const std::byte> bytes, F&& each)
            {
                bool good = true;
                const bool wellFormed = survey::forEachSubrecord(bytes, [&](const survey::SubrecordView& sub) {
                    if (good)
                        good = each(sub.mType, sub.mData);
                });
                if (!wellFormed && mWhy.empty())
                {
                    mWhy = "a subrecord is cut short or claims more bytes than the record holds";
                    return false;
                }
                return good;
            }

            bool readTopic(const Content::RawRecord& record, std::span<const std::byte> bytes)
            {
                DialogTopic t;
                t.mCommon = common(record);
                const bool ok = walkSubs(bytes, [&](std::uint32_t type, std::span<const std::byte> data) {
                    switch (type)
                    {
                        case Content::fourCC("EDID"):
                            return zstring(type, data, t.mCommon.mEditorId);
                        case Content::fourCC("FULL"):
                            return lstring(type, data, t.mName);
                        case Content::fourCC("PNAM"):
                            if (data.size() != 4)
                            {
                                odd(type);
                                return true;
                            }
                            t.mPriority = readLe<float>(data.data());
                            if (!std::isfinite(t.mPriority))
                            {
                                t.mPriority = 0.0f;
                                odd(type);
                            }
                            return true;
                        case Content::fourCC("QNAM"):
                            return formField(type, data, t.mQuest);
                        case Content::fourCC("BNAM"):
                            return formField(type, data, t.mBranch);
                        case Content::fourCC("DATA"):
                            if (data.size() != 4)
                            {
                                odd(type);
                                return true;
                            }
                            t.mHasData = true;
                            t.mFlags = static_cast<std::uint8_t>(data[0]);
                            t.mCategory = static_cast<std::uint8_t>(data[1]);
                            t.mSubtype = readLe<std::uint16_t>(data.data() + 2);
                            return true;
                        case Content::fourCC("SNAM"):
                            if (data.size() != 4)
                            {
                                odd(type);
                                return true;
                            }
                            t.mSubtypeName.assign(reinterpret_cast<const char*>(data.data()), 4);
                            return true;
                        case Content::fourCC("TIFC"):
                            if (data.size() != 4)
                            {
                                odd(type);
                                return true;
                            }
                            t.mInfoCount = readLe<std::uint32_t>(data.data());
                            return true;
                        default:
                            return true;
                    }
                });
                if (ok)
                    mOut.mTopics.push_back(std::move(t));
                return ok;
            }

            bool readInfo(const Content::RawRecord& record, std::span<const std::byte> bytes)
            {
                DialogInfo info;
                info.mCommon = common(record);
                if (!mInTopicGroup)
                    ++mOut.mInfosOutsideTopicGroup;
                info.mTopic = mGroupTopic;
                bool tooManyResponses = false;
                const bool ok = walkSubs(bytes, [&](std::uint32_t type, std::span<const std::byte> data) {
                    switch (type)
                    {
                        case Content::fourCC("EDID"):
                            return zstring(type, data, info.mCommon.mEditorId);
                        case Content::fourCC("ENAM"):
                            if (data.size() != 4)
                            {
                                odd(type);
                                return true;
                            }
                            info.mFlags = readLe<std::uint16_t>(data.data());
                            info.mResetHours = readLe<std::uint16_t>(data.data() + 2);
                            return true;
                        case Content::fourCC("CNAM"):
                            if (data.size() != 1)
                            {
                                odd(type);
                                return true;
                            }
                            info.mFavorLevel = static_cast<std::uint8_t>(data[0]);
                            return true;
                        case Content::fourCC("QNAM"):
                            return formField(type, data, info.mQuest);
                        case Content::fourCC("ANAM"):
                            return formField(type, data, info.mSpeaker);
                        case Content::fourCC("VMAD"):
                            info.mHasScript = true; // fragments are not run (task 0141): only noted
                            info.mFragmentScript = findFragmentScriptName(data); // task 0155
                            return true;
                        case Content::fourCC("RNAM"):
                            return lstring(type, data, info.mPrompt);
                        case Content::fourCC("TCLT"):
                            if (data.size() != 4)
                            {
                                odd(type);
                                return true;
                            }
                            if (info.mLinksTo.size() >= mLimits.mMaxListEntries)
                            {
                                mWhy = "more than " + std::to_string(mLimits.mMaxListEntries) + " entries in TCLT";
                                return false;
                            }
                            info.mLinksTo.push_back(resolved(readLe<std::uint32_t>(data.data())));
                            return true;
                        case Content::fourCC("TRDT"):
                        {
                            if (data.size() != 24)
                            {
                                odd(type);
                                return true;
                            }
                            if (info.mResponses.size() >= mLimits.mMaxResponses)
                            {
                                tooManyResponses = true;
                                mWhy = "more than " + std::to_string(mLimits.mMaxResponses) + " responses in one INFO";
                                return false;
                            }
                            DialogResponse r;
                            r.mEmotionType = readLe<std::uint32_t>(data.data());
                            r.mEmotionValue = readLe<std::uint32_t>(data.data() + 4);
                            r.mNumber = static_cast<std::uint8_t>(data[12]);
                            r.mSound = resolved(readLe<std::uint32_t>(data.data() + 16));
                            r.mUseEmotionAnimation = static_cast<std::uint8_t>(data[20]) != 0;
                            info.mResponses.push_back(std::move(r));
                            return true;
                        }
                        case Content::fourCC("NAM1"):
                            if (info.mResponses.empty())
                            {
                                odd(type); // a text with no TRDT before it
                                return true;
                            }
                            return lstring(type, data, info.mResponses.back().mText);
                        default:
                            return true;
                    }
                });
                (void)tooManyResponses;
                if (!ok)
                    return false;
                std::vector<survey::Ctda> conditions;
                info.mConditionsOk = conditions::decodeConditions(bytes, conditions);
                if (conditions.size() > mLimits.mMaxConditions)
                {
                    mWhy = "more than " + std::to_string(mLimits.mMaxConditions) + " conditions in one INFO";
                    return false;
                }
                info.mConditions = std::move(conditions);
                mOut.mInfos.push_back(std::move(info));
                return true;
            }

            bool readGlobal(const Content::RawRecord& record, std::span<const std::byte> bytes)
            {
                GlobalValue g;
                g.mCommon = common(record);
                const bool ok = walkSubs(bytes, [&](std::uint32_t type, std::span<const std::byte> data) {
                    switch (type)
                    {
                        case Content::fourCC("EDID"):
                            return zstring(type, data, g.mCommon.mEditorId);
                        case Content::fourCC("FNAM"):
                            if (data.size() != 1)
                            {
                                odd(type);
                                return true;
                            }
                            g.mType = static_cast<char>(data[0]);
                            return true;
                        case Content::fourCC("FLTV"):
                            if (data.size() != 4)
                            {
                                odd(type);
                                return true;
                            }
                            g.mValue = readLe<float>(data.data());
                            if (!std::isfinite(g.mValue))
                            {
                                g.mValue = 0.0f;
                                odd(type);
                            }
                            return true;
                        default:
                            return true;
                    }
                });
                if (ok)
                    mOut.mGlobals.push_back(std::move(g));
                return ok;
            }

            bool readVoiceType(const Content::RawRecord& record, std::span<const std::byte> bytes)
            {
                VoiceTypeRecord v;
                v.mCommon = common(record);
                const bool ok = walkSubs(bytes, [&](std::uint32_t type, std::span<const std::byte> data) {
                    if (type == Content::fourCC("EDID"))
                        return zstring(type, data, v.mCommon.mEditorId);
                    return true;
                });
                if (ok)
                    mOut.mVoiceTypes.push_back(std::move(v));
                return ok;
            }

            bool readFormList(const Content::RawRecord& record, std::span<const std::byte> bytes)
            {
                FormListRecord f;
                f.mCommon = common(record);
                const bool ok = walkSubs(bytes, [&](std::uint32_t type, std::span<const std::byte> data) {
                    switch (type)
                    {
                        case Content::fourCC("EDID"):
                            return zstring(type, data, f.mCommon.mEditorId);
                        case Content::fourCC("LNAM"):
                            if (data.size() != 4)
                            {
                                odd(type);
                                return true;
                            }
                            if (f.mEntries.size() >= mLimits.mMaxListEntries)
                            {
                                mWhy = "more than " + std::to_string(mLimits.mMaxListEntries) + " entries in LNAM";
                                return false;
                            }
                            f.mEntries.push_back(resolved(readLe<std::uint32_t>(data.data())));
                            return true;
                        default:
                            return true;
                    }
                });
                if (ok)
                    mOut.mFormLists.push_back(std::move(f));
                return ok;
            }

            const Content::Plugin& mPlugin;
            const FormIdResolver& mResolver;
            const DialogueLimits& mLimits;
            DialogueRecords& mOut;
            const bool mLocalized;
            std::vector<std::byte> mInflated;
            std::string mWhy;
            std::uint32_t mCurrentType = 0;
            bool mInTopicGroup = false;
            FormId mGroupTopic = kNullFormId;
        };
    }

    bool parseDialogueRecords(
        const Content::Plugin& plugin, const LoadOrder& order, const DialogueLimits& limits, DialogueRecords& out)
    {
        out = DialogueRecords();
        out.mName = plugin.name();
        if (!isSkyrimSeHeader(plugin.header()))
        {
            out.mWalkError = makeError(plugin.name(), 0, 0, "this plugin is not a Skyrim Special Edition one");
            return false;
        }
        const FormIdResolver resolver(plugin.header(), plugin.name(), order);
        DialogueParser parser(plugin, resolver, limits, out);
        const std::optional<Content::Error> error = plugin.walk(parser);
        if (error)
        {
            out.mWalkError = *error;
            return false;
        }
        return true;
    }

    bool parseDialogueRecordsBytes(const std::string& bytes, const std::string& name, const LoadOrder& order,
        const DialogueLimits& limits, DialogueRecords& out, Content::Error& error)
    {
        const std::unique_ptr<Content::Plugin> plugin
            = Content::Plugin::open(std::make_unique<std::istringstream>(bytes, std::ios::binary), name, error);
        if (plugin == nullptr)
            return false;
        if (!parseDialogueRecords(*plugin, order, limits, out))
        {
            if (out.mWalkError)
                error = *out.mWalkError;
            return false;
        }
        return true;
    }

    // ---- DialogueIndex -------------------------------------------------------------------------------------------

    namespace
    {
        bool usable(FormId id)
        {
            return id != kNullFormId && id != kUnresolvedFormId;
        }
    }

    void DialogueIndex::add(DialogueRecords records, FormIdResolver resolver)
    {
        const std::uint32_t plugin = static_cast<std::uint32_t>(mResolvers.size());
        mResolvers.push_back(std::move(resolver));
        mPluginNames.push_back(records.mName);
        for (const VoiceTypeRecord& v : records.mVoiceTypes)
        {
            if (!usable(v.mCommon.mId))
                continue;
            if (v.mCommon.deleted())
                mVoiceTypes.erase(v.mCommon.mId);
            else
                mVoiceTypes[v.mCommon.mId] = v.mCommon.mEditorId;
        }
        for (DialogTopic& t : records.mTopics)
        {
            if (!usable(t.mCommon.mId))
                continue;
            if (t.mCommon.deleted())
            {
                mTopics.erase(t.mCommon.mId);
                continue;
            }
            t.mPlugin = plugin;
            mTopics[t.mCommon.mId] = std::move(t);
        }
        for (DialogInfo& info : records.mInfos)
        {
            if (!usable(info.mCommon.mId) || !usable(info.mTopic))
                continue;
            Slot& slot = mSlots[info.mTopic];
            const auto it = slot.mByInfo.find(info.mCommon.mId);
            const bool deleted = info.mCommon.deleted();
            info.mPlugin = plugin;
            if (it != slot.mByInfo.end())
            {
                slot.mAlive[it->second] = !deleted;
                if (!deleted)
                    slot.mList[it->second] = std::move(info);
                continue;
            }
            if (deleted)
                continue;
            slot.mByInfo[info.mCommon.mId] = slot.mList.size();
            slot.mList.push_back(std::move(info));
            slot.mAlive.push_back(true);
        }
        for (GlobalValue& g : records.mGlobals)
        {
            if (!usable(g.mCommon.mId))
                continue;
            if (g.mCommon.deleted())
                mGlobals.erase(g.mCommon.mId);
            else
                mGlobals[g.mCommon.mId] = std::move(g);
        }
        for (FormListRecord& f : records.mFormLists)
        {
            if (!usable(f.mCommon.mId))
                continue;
            if (f.mCommon.deleted())
                mFormLists.erase(f.mCommon.mId);
            else
                mFormLists[f.mCommon.mId] = std::move(f);
        }
        mInfoCount = 0;
        for (const auto& [id, slot] : mSlots)
            for (bool alive : slot.mAlive)
                mInfoCount += alive ? 1 : 0;
    }

    std::optional<DialogueIndex::VoiceOrigin> DialogueIndex::voiceOriginOf(const DialogInfo& info) const
    {
        // At most a few hundred plugins; a voice line is named a handful of times per conversation.
        for (std::size_t i = 0; i < mResolvers.size(); ++i)
        {
            if (mPluginNames[i].empty())
                continue;
            if (const std::optional<std::uint32_t> stored = mResolvers[i].ownStoredId(info.mCommon.mId))
                return VoiceOrigin{ mPluginNames[i], *stored };
        }
        return std::nullopt;
    }

    const std::string& DialogueIndex::pluginName(std::uint32_t index) const
    {
        static const std::string kEmpty;
        return index < mPluginNames.size() ? mPluginNames[index] : kEmpty;
    }

    const std::string& DialogueIndex::voiceTypeName(FormId id) const
    {
        static const std::string kEmpty;
        const auto it = mVoiceTypes.find(id);
        return it == mVoiceTypes.end() ? kEmpty : it->second;
    }

    FormId SkyrimConditionWorld::speakerVoiceType() const
    {
        if (!mHasResolved)
            return kNullFormId;
        const ActorNpc* traits = mActors.npc(mResolved.mTraitsFrom);
        FormId voice = traits != nullptr ? traits->mVoiceType : kNullFormId;
        if (voice == kNullFormId && traits != nullptr)
        {
            const RaceRecord* race = mActors.race(mResolved.mRace);
            if (race != nullptr)
                voice = race->mVoiceTypes[traits->female() ? 1 : 0];
        }
        return voice;
    }

    const DialogTopic* DialogueIndex::topic(FormId id) const
    {
        const auto it = mTopics.find(id);
        return it == mTopics.end() ? nullptr : &it->second;
    }

    const GlobalValue* DialogueIndex::global(FormId id) const
    {
        const auto it = mGlobals.find(id);
        return it == mGlobals.end() ? nullptr : &it->second;
    }

    const FormListRecord* DialogueIndex::formList(FormId id) const
    {
        const auto it = mFormLists.find(id);
        return it == mFormLists.end() ? nullptr : &it->second;
    }

    std::vector<const DialogInfo*> DialogueIndex::infosOf(FormId topic) const
    {
        std::vector<const DialogInfo*> out;
        const auto it = mSlots.find(topic);
        if (it == mSlots.end())
            return out;
        for (std::size_t i = 0; i < it->second.mList.size(); ++i)
            if (it->second.mAlive[i])
                out.push_back(&it->second.mList[i]);
        return out;
    }

    std::vector<FormId> DialogueIndex::topicIds(const std::string& subtypeName) const
    {
        std::vector<FormId> ids;
        for (const auto& [id, t] : mTopics)
            if (subtypeName.empty() || t.mSubtypeName == subtypeName)
                ids.push_back(id);
        std::sort(ids.begin(), ids.end());
        return ids;
    }

    namespace
    {
        constexpr FormId kPlayerReference = 0x00000014;
        constexpr FormId kPlayerBase = 0x00000007;
    }

    // ---- SkyrimConditionWorld ------------------------------------------------------------------------------------

    SkyrimConditionWorld::SkyrimConditionWorld(const ActorIndex& actors, const DialogueIndex& dialogue)
        : mActors(actors)
        , mDialogue(dialogue)
    {
    }

    void SkyrimConditionWorld::setSpeaker(const DialogueSpeaker& speaker)
    {
        mSpeaker = speaker;
        mStatsKnown = false;
        mResolved = ActorIndex::Resolved();
        mHasResolved = speaker.mBase != kNullFormId
            && mActors.resolve(speaker.mBase, speaker.mLevel, 0, mResolved) == ActorIndex::ResolveStatus::Ok;
    }

    // ---- the player and the speaker's numbers (task 0155) --------------------------------------------------------

    bool SkyrimConditionWorld::speakerStats() const
    {
        if (!mStatsKnown)
        {
            mStatsKnown = true;
            std::string error;
            mStatsOk = mPlayer != nullptr && mSpeaker.mBase != kNullFormId
                && computeActorStats(mActors, mSpeaker.mBase, mSpeaker.mReference, mPlayer->mLevel, mStats, error);
        }
        return mStatsOk;
    }

    float SkyrimConditionWorld::actorValue(conditions::Subject subject, std::uint32_t actorValueIndex) const
    {
        if (mPlayer == nullptr)
            return 0.f;
        if (subject == kPlayer)
            return actorValueOfPlayer(*mPlayer, actorValueIndex);
        return isSpeaker(subject) && speakerStats() ? actorValueOfActor(mStats, actorValueIndex) : 0.f;
    }

    float SkyrimConditionWorld::baseActorValue(conditions::Subject subject, std::uint32_t actorValueIndex) const
    {
        return actorValue(subject, actorValueIndex); // no modifiers exist yet: the base is the current value
    }

    float SkyrimConditionWorld::levelOf(conditions::Subject subject) const
    {
        if (mPlayer == nullptr)
            return 0.f;
        if (subject == kPlayer)
            return static_cast<float>(mPlayer->mLevel);
        return isSpeaker(subject) && speakerStats() ? static_cast<float>(mStats.level) : 0.f;
    }

    float SkyrimConditionWorld::goldOf(conditions::Subject subject) const
    {
        return mPlayer != nullptr && subject == kPlayer ? mPlayer->mGold : 0.f;
    }

    float SkyrimConditionWorld::itemCount(conditions::Subject subject, std::uint32_t item) const
    {
        constexpr FormId kGold001 = 0x0000000F;
        if (mPlayer != nullptr && subject == kPlayer)
            return resolve(item) == kGold001 ? mPlayer->mGold : 0.f;
        ActorView view;
        if (!viewOf(subject, view))
            return 0.f;
        const InventoryAnswer answer = inventoryOf(view, resolve(item));
        return answer.mKnown ? answer.mCount : 0.f;
    }

    // ---- task 0156: conversation and fresh-game state ----------------------------------------------------------

    bool SkyrimConditionWorld::isDead(conditions::Subject subject) const
    {
        // a corpse does not talk, and the player is alive: nobody has died in this session; another reference is dead
        // when its record says it starts dead (task 0160)
        ActorView view;
        if (isOtherSubject(subject) && viewOf(subject, view) && view.mRef != nullptr)
            return (view.mRef->mFlags & PlacedFlags::Dead) != 0;
        return false;
    }

    bool SkyrimConditionWorld::inDialogueWithPlayer(conditions::Subject subject) const
    {
        return mConversationOpen && isSpeaker(subject);
    }

    float SkyrimConditionWorld::randomPercent() const
    {
        if (!mRandomSeeded)
            return 0.f;
        // splitmix64: one draw per call, deterministic from the seed
        mRandomState += 0x9E3779B97F4A7C15ull;
        std::uint64_t z = mRandomState;
        z = (z ^ (z >> 30)) * 0xBF58476D1CE4E5B9ull;
        z = (z ^ (z >> 27)) * 0x94D049BB133111EBull;
        z ^= z >> 31;
        return static_cast<float>(z % 100u);
    }

    bool SkyrimConditionWorld::inInterior(conditions::Subject subject) const
    {
        return isSpeaker(subject) && mSpeaker.mInterior.value_or(false);
    }

    bool SkyrimConditionWorld::inLocation(conditions::Subject subject, std::uint32_t location) const
    {
        const FormId id = resolve(location);
        return mLocations != nullptr && isSpeaker(subject) && mSpeaker.mLocationKnown && id != kNullFormId
            && mLocations->isWithin(mSpeaker.mLocation, id);
    }

    bool SkyrimConditionWorld::locationHasKeyword(conditions::Subject subject, std::uint32_t keyword) const
    {
        return mLocations != nullptr && isSpeaker(subject) && mSpeaker.mLocationKnown
            && mLocations->hasKeyword(mSpeaker.mLocation, resolve(keyword));
    }

    bool SkyrimConditionWorld::talkedToPlayer(conditions::Subject subject) const
    {
        return mTalked && isSpeaker(subject);
    }

    float SkyrimConditionWorld::actorValuePercent(conditions::Subject subject, std::uint32_t actorValueIndex) const
    {
        // a fresh actor's health, magicka and stamina are full; no other value is guessed
        return isKnownActor(subject) && actorValueIndex >= kAvHealth && actorValueIndex <= kAvStamina ? 1.f : 0.f;
    }

    float SkyrimConditionWorld::questVariable(std::uint32_t quest, const std::string& name) const
    {
        if (mQuestVariables == nullptr)
            return 0.f;
        const QuestVariableValue v = mQuestVariables->value(resolve(quest), name);
        return v.mKnown ? v.mValue : 0.f;
    }

    bool SkyrimConditionWorld::playerCanIntimidate(conditions::Subject subject) const
    {
        return mPlayer != nullptr && isSpeaker(subject) && speakerStats()
            && intimidateSucceeds(mPlayer->mLevel, mStats.level);
    }

    bool SkyrimConditionWorld::playerCanBribe(conditions::Subject subject) const
    {
        return mPlayer != nullptr && isSpeaker(subject) && speakerStats()
            && bribeSucceeds(mPlayer->mGold, mStats.level);
    }

    FormId SkyrimConditionWorld::resolve(std::uint32_t stored) const
    {
        if (stored == 0)
            return kNullFormId;
        if (mResolver == nullptr)
            return stored;
        const std::optional<FormId> id = mResolver->resolve(stored);
        return id ? *id : kNullFormId;
    }

    bool SkyrimConditionWorld::isSpeaker(conditions::Subject subject) const
    {
        return mSpeaker.mBase != kNullFormId && subject == mSpeaker.mBase;
    }

    namespace
    {
        /// The functions whose fresh-game answer is the same for the speaker and for the player.
        bool isFreshStateFunction(std::uint16_t function)
        {
            switch (function)
            {
                case 46: // GetDead
                case 145: // IsTrespassing
                case 144:
                case 402:
                case 375:
                case 376:
                case 459:
                case 289: // IsInCombat
                case 49: // GetSleeping
                    return true;
                default:
                    return false;
            }
        }
    }

    bool SkyrimConditionWorld::answers(const survey::Ctda& c) const
    {
        switch (c.mFunction)
        {
            case 74: // GetGlobalValue: no subject
                return true;
            case 77: // GetRandomPercent: answered by the seeded generator (task 0156)
                return mRandomSeeded;
            case 84: // GetDeadCount: nobody has been killed in a fresh game
            case 606: // GetKeywordDataForLocation: no script has stored data yet
            case 314: // IsActorAVictim: no crime is under way
                return true;
            case 131: // GetPCIsSex
            case 130: // GetPCIsRace
                return mPlayer != nullptr;
            case 629:
            { // GetVMQuestVariable: the script's value before any script ran
                if (mQuestVariables == nullptr)
                    return false;
                return mQuestVariables->value(resolve(c.mParam1), c.mString2).mKnown;
            }
            case 149: // GetIsCurrentWeather
                return mWeather != kNullFormId;
            case 56: // GetQuestRunning
            case 58: // GetStage
            case 59: // GetStageDone
            case 543: // GetQuestCompleted
                return questKnown(c.mParam1);
            default:
                break;
        }
        // The run-on: 0 the speaker; 1 the player, only for GetIsID; 2 a reference, only the speaker's own.
        switch (c.mRunOn)
        {
            case 0:
                break;
            case 1:
                // fn_182 GetEquipped: the stand-in wears nothing (the Amulet of Articulation test is false)
                if (mPlayer != nullptr
                    && (c.mFunction == 14 || c.mFunction == 277 || c.mFunction == 80 || c.mFunction == 48
                        || c.mFunction == 182 || c.mFunction == 47))
                    return true;
                if (mPlayer != nullptr && isFreshStateFunction(c.mFunction))
                    return true;
                if (mPlayer != nullptr && (c.mFunction == 69 || c.mFunction == 70))
                    return true; // the stand-in's race and sex
                if (mPlayer != nullptr && c.mFunction == 640 && c.mParam1 >= kAvHealth && c.mParam1 <= kAvStamina)
                    return true;
                return c.mFunction == 72 || (c.mFunction == 566 && aliasKnown(c.mParam1));
            case 2:
            {
                const FormId ref = resolve(c.mReference);
                if (mSpeaker.mReference != kNullFormId && ref == mSpeaker.mReference)
                    break;
                if (ref == kPlayerReference)
                {
                    survey::Ctda onPlayer = c; // the player's own reference is the player
                    onPlayer.mRunOn = 1;
                    return answers(onPlayer);
                }
                return otherAnswers(c, resolveRunOn(2, c.mReference)); // task 0160: another placed reference
            }
            case 5: // a quest alias: answerable when the alias is the speaker (the data fills it) or names a reference
                if (aliasKnown(c.mParam3) && aliasIsSpeaker(mContextQuest, c.mParam3))
                    break;
                return otherAnswers(c, resolveSubject(c));
            default:
                return false;
        }
        switch (c.mFunction)
        {
            case 566: // GetIsAliasRef
                return aliasKnown(c.mParam1);
            case 69: // GetIsRace
            case 70: // GetIsSex
            case 71: // GetInFaction
            case 72: // GetIsID
            case 73: // GetFactionRank
            case 136: // GetIsReference
            case 372: // IsInList
            case 426: // GetIsVoiceType
            case 560: // HasKeyword
                return true;
            case 14: // GetActorValue
            case 277: // GetBaseActorValue
            case 80: // GetLevel
            case 654: // GetBribeSuccess
            case 655: // GetIntimidateSuccess
                return mPlayer != nullptr && mSpeaker.mBase != kNullFormId;
            case 67: // GetInCell
                return mSpeaker.mCell != kNullFormId;
            case 46: // GetDead
            case 453: // GetPlayerTeammate
            case 579: // GetEquippedShout
            case 66: // GetShouldAttack
                return true;
            case 249: // IsInDialogueWithPlayer
                return mConversationKnown;
            case 50: // GetTalkedToPC
                return true;
            case 300: // IsInInterior
                return mSpeaker.mInterior.has_value();
            case 359: // GetInCurrentLoc
            case 562: // LocationHasKeyword
                return mLocations != nullptr && mSpeaker.mLocationKnown;
            case 640: // GetActorValuePercent: the vitals are full, nothing else is guessed
                return c.mParam1 >= kAvHealth && c.mParam1 <= kAvStamina;
            case 145: // IsTrespassing, GetTrespassWarningLevel, IsBribedbyPlayer, GetCrimeGold*: a game with no crime
            case 144:
            case 402:
            case 375:
            case 376:
            case 459:
            case 289: // IsInCombat
            case 49: // GetSleeping
                return true;
            case 310: // GetInWorldspace: never true in an interior (task 0156)
                return mSpeaker.mWorldspace != kNullFormId || mSpeaker.mInterior.value_or(false);
            // task 0160: the speaker's own initial state, from the placed-reference table and the records
            case 35: // GetDisabled: a talking actor is enabled
            case 286: // IsSneaking: nobody sneaks in a fresh game
                return true;
            case 47: // GetItemCount of the speaker
            case 182:
            { // GetEquipped
                ActorView view;
                view.mBase = mSpeaker.mBase;
                view.mRef = mPlacedRefs != nullptr && mSpeaker.mReference != kNullFormId
                    ? mPlacedRefs->find(mSpeaker.mReference)
                    : nullptr;
                const InventoryAnswer a = inventoryOf(view, resolve(c.mParam1));
                return a.mKnown && (c.mFunction == 47 || a.mInOutfit || a.mCount == 0.f);
            }
            case 1: // GetDistance
                return distanceKnown(static_cast<conditions::Subject>(mSpeaker.mBase), c.mParam1);
            case 403: // GetRelationshipRank
                return relationKnown(static_cast<conditions::Subject>(mSpeaker.mBase), c.mParam1);
            case 161: // GetIsCurrentPackage
            case 255:
            { // GetOffersServicesNow
                if (!mCurrentPackage || mSpeaker.mReference == kNullFormId)
                    return false;
                return mCurrentPackage(mSpeaker.mReference).has_value() && (c.mFunction == 161 || mPackages != nullptr);
            }
            default:
                return false;
        }
    }

    bool SkyrimConditionWorld::aliasKnown(std::uint32_t alias) const
    {
        // answered only when the fact the comparison needs is given: a reference fill needs the speaker's reference,
        // a unique actor his base; the player's alias needs neither
        if (mQuestState == nullptr || mContextQuest == kNullFormId)
            return false;
        const AliasFill fill = mQuestState->aliasFill(mContextQuest, alias);
        if (!fill.mKnown)
            return false;
        if (fill.mReference == kPlayerReference || fill.mActor == kPlayerBase)
            return true;
        if (fill.mReference != kNullFormId)
            return mSpeaker.mReference != kNullFormId;
        return mSpeaker.mBase != kNullFormId;
    }

    conditions::Subject SkyrimConditionWorld::resolveRunOn(std::uint32_t runOn, std::uint32_t reference) const
    {
        switch (runOn)
        {
            case 0:
                return mSpeaker.mBase;
            case 1:
                return kPlayer;
            case 2:
            {
                const FormId ref = resolve(reference);
                if (mSpeaker.mReference != kNullFormId && ref == mSpeaker.mReference)
                    return static_cast<conditions::Subject>(mSpeaker.mBase);
                if (ref == kPlayerReference)
                    return kPlayer;
                if (mPlacedRefs != nullptr && ref != kNullFormId && mPlacedRefs->find(ref) != nullptr)
                    return kPlacedTag | ref;
                return 0;
            }
            default:
                return 0;
        }
    }

    bool SkyrimConditionWorld::hasBaseId(conditions::Subject subject, std::uint32_t formId) const
    {
        const FormId id = resolve(formId);
        if (id == kNullFormId)
            return false;
        if (subject == kPlayer)
            return id == 0x00000007u;
        if (isOtherSubject(subject))
        {
            ActorView view;
            return viewOf(subject, view) && (id == view.mBase || (view.mRef != nullptr && id == view.mRef->mId));
        }
        if (!isSpeaker(subject))
            return false;
        return id == mSpeaker.mBase || (mSpeaker.mReference != kNullFormId && id == mSpeaker.mReference);
    }

    bool SkyrimConditionWorld::inFaction(conditions::Subject subject, std::uint32_t faction) const
    {
        const FormId id = resolve(faction);
        const ActorIndex::Resolved* r = resolvedOf(subject);
        if (r == nullptr || id == kNullFormId)
            return false;
        return std::any_of(
            r->mFactions.begin(), r->mFactions.end(), [&](const FactionMembership& f) { return f.mFaction == id; });
    }

    float SkyrimConditionWorld::factionRank(conditions::Subject subject, std::uint32_t faction) const
    {
        const FormId id = resolve(faction);
        const ActorIndex::Resolved* r = resolvedOf(subject);
        if (r == nullptr || id == kNullFormId)
            return -1.f;
        for (const FactionMembership& f : r->mFactions)
            if (f.mFaction == id)
                return static_cast<float>(f.mRank);
        return -1.f;
    }

    float SkyrimConditionWorld::globalValue(std::uint32_t global) const
    {
        const GlobalValue* g = mDialogue.global(resolve(global));
        return g != nullptr ? g->mValue : 0.f;
    }

    bool SkyrimConditionWorld::inList(FormId list, FormId value, int depth) const
    {
        const FormListRecord* l = mDialogue.formList(list);
        if (l == nullptr)
            return false;
        for (FormId entry : l->mEntries)
        {
            if (entry == value)
                return true;
            if (depth < 4 && inList(entry, value, depth + 1))
                return true;
        }
        return false;
    }

    bool SkyrimConditionWorld::hasVoiceType(conditions::Subject subject, std::uint32_t voiceOrList) const
    {
        const FormId target = resolve(voiceOrList);
        if (!isSpeaker(subject) || !mHasResolved || target == kNullFormId)
            return false;
        const FormId voice = speakerVoiceType();
        return voice != kNullFormId && (voice == target || inList(target, voice, 0));
    }

    bool SkyrimConditionWorld::inCell(conditions::Subject subject, std::uint32_t cell) const
    {
        return isSpeaker(subject) && mSpeaker.mCell != kNullFormId && resolve(cell) == mSpeaker.mCell;
    }

    int SkyrimConditionWorld::sexOf(conditions::Subject subject) const
    {
        if (mPlayer != nullptr && subject == kPlayer)
            return mPlayer->mFemale ? 1 : 0;
        const ActorIndex::Resolved* r = resolvedOf(subject);
        if (r == nullptr)
            return -1;
        const ActorNpc* traits = mActors.npc(r->mTraitsFrom);
        return traits == nullptr ? -1 : (traits->female() ? 1 : 0);
    }

    bool SkyrimConditionWorld::hasKeyword(conditions::Subject subject, std::uint32_t keyword) const
    {
        const FormId id = resolve(keyword);
        const ActorIndex::Resolved* r = resolvedOf(subject);
        if (r == nullptr || id == kNullFormId)
            return false;
        // The NPC's own keywords (from its keyword template) and its race's (RECALLED-VERIFY: an actor has both).
        if (const ActorNpc* n = mActors.npc(r->mKeywordsFrom))
            if (std::find(n->mKeywords.begin(), n->mKeywords.end(), id) != n->mKeywords.end())
                return true;
        if (const RaceRecord* race = mActors.race(r->mRace))
            if (std::find(race->mKeywords.begin(), race->mKeywords.end(), id) != race->mKeywords.end())
                return true;
        return false;
    }

    bool SkyrimConditionWorld::isRace(conditions::Subject subject, std::uint32_t race) const
    {
        const FormId id = resolve(race);
        if (mPlayer != nullptr && subject == kPlayer)
            return id != kNullFormId && id == mPlayer->mRace;
        const ActorIndex::Resolved* r = resolvedOf(subject);
        return r != nullptr && id != kNullFormId && r->mRace == id;
    }

    bool SkyrimConditionWorld::inWorldspace(conditions::Subject subject, std::uint32_t worldspace) const
    {
        return isSpeaker(subject) && mSpeaker.mWorldspace != kNullFormId && resolve(worldspace) == mSpeaker.mWorldspace;
    }

    bool SkyrimConditionWorld::isReference(conditions::Subject subject, std::uint32_t reference) const
    {
        if (isOtherSubject(subject))
        {
            ActorView view;
            return viewOf(subject, view) && view.mRef != nullptr && resolve(reference) == view.mRef->mId;
        }
        return isSpeaker(subject) && mSpeaker.mReference != kNullFormId && resolve(reference) == mSpeaker.mReference;
    }

    bool SkyrimConditionWorld::isCurrentWeather(std::uint32_t weather) const
    {
        return mWeather != kNullFormId && resolve(weather) == mWeather;
    }

    bool SkyrimConditionWorld::baseInList(conditions::Subject subject, std::uint32_t formList) const
    {
        const FormId list = resolve(formList);
        return isSpeaker(subject) && list != kNullFormId && inList(list, mSpeaker.mBase, 0);
    }

    // ---- quests (task 0139) ----------------------------------------------------------------------------------

    bool SkyrimConditionWorld::questKnown(std::uint32_t storedQuest) const
    {
        return mQuestState != nullptr && mQuestState->exists(resolve(storedQuest));
    }

    float SkyrimConditionWorld::questStage(std::uint32_t quest) const
    {
        return mQuestState != nullptr ? mQuestState->stage(resolve(quest)) : 0.f;
    }

    bool SkyrimConditionWorld::questStageDone(std::uint32_t quest, std::uint32_t stage) const
    {
        return mQuestState != nullptr && mQuestState->stageDone(resolve(quest), stage);
    }

    bool SkyrimConditionWorld::questRunning(std::uint32_t quest) const
    {
        return mQuestState != nullptr && mQuestState->running(resolve(quest));
    }

    bool SkyrimConditionWorld::questCompleted(std::uint32_t quest) const
    {
        return mQuestState != nullptr && mQuestState->completed(resolve(quest));
    }

    bool SkyrimConditionWorld::aliasIsSpeaker(FormId quest, std::uint32_t alias) const
    {
        if (mQuestState == nullptr || quest == kNullFormId)
            return false;
        const AliasFill fill = mQuestState->aliasFill(quest, alias);
        if (!fill.mKnown)
            return false;
        if (fill.mReference != kNullFormId)
            return mSpeaker.mReference != kNullFormId && fill.mReference == mSpeaker.mReference;
        return fill.mActor != kNullFormId && mSpeaker.mBase != kNullFormId && fill.mActor == mSpeaker.mBase;
    }

    bool SkyrimConditionWorld::isAliasRef(conditions::Subject subject, std::uint32_t alias) const
    {
        if (mQuestState == nullptr || mContextQuest == kNullFormId)
            return false;
        if (subject == kPlayer)
        {
            const AliasFill fill = mQuestState->aliasFill(mContextQuest, alias);
            return fill.mKnown && (fill.mReference == kPlayerReference || fill.mActor == kPlayerBase);
        }
        if (isOtherSubject(subject))
        {
            ActorView view;
            const AliasFill fill = mQuestState->aliasFill(mContextQuest, alias);
            return fill.mKnown && viewOf(subject, view)
                && ((fill.mReference != kNullFormId && view.mRef != nullptr && fill.mReference == view.mRef->mId)
                    || (fill.mActor != kNullFormId && fill.mActor == view.mBase));
        }
        return isSpeaker(subject) && aliasIsSpeaker(mContextQuest, alias);
    }

    conditions::Subject SkyrimConditionWorld::resolveSubject(const survey::Ctda& c) const
    {
        if (c.mRunOn != 5) // quest alias: the alias id is in parameter 3
            return resolveRunOn(c.mRunOn, c.mReference);
        if (mQuestState == nullptr || mContextQuest == kNullFormId)
            return 0;
        if (aliasIsSpeaker(mContextQuest, c.mParam3))
            return static_cast<conditions::Subject>(mSpeaker.mBase);
        const AliasFill fill = mQuestState->aliasFill(mContextQuest, c.mParam3);
        if (fill.mKnown && (fill.mReference == kPlayerReference || fill.mActor == kPlayerBase))
            return kPlayer;
        if (!fill.mKnown || mPlacedRefs == nullptr)
            return 0;
        // task 0160: an alias that names a reference or an actor the data places
        if (fill.mReference != kNullFormId)
            return mPlacedRefs->find(fill.mReference) != nullptr ? (kPlacedTag | fill.mReference) : 0;
        if (fill.mActor != kNullFormId)
        {
            const PlacedRefIndex::Range range = mPlacedRefs->byBase(fill.mActor);
            if (range.size() == 1)
                return kPlacedTag | (*range.mBegin)->mId;
            return kBaseTag | fill.mActor;
        }
        return 0;
    }

    // ---- task 0160: other references, inventories, relationships ---------------------------------------------------

    bool SkyrimConditionWorld::viewOf(conditions::Subject subject, ActorView& out) const
    {
        out = ActorView();
        if (isSpeaker(subject))
        {
            out.mBase = mSpeaker.mBase;
            if (mPlacedRefs != nullptr && mSpeaker.mReference != kNullFormId)
                out.mRef = mPlacedRefs->find(mSpeaker.mReference);
            return true;
        }
        const std::uint64_t tag = subject & 0xFFFFFFFF00000000ull;
        const FormId low = static_cast<FormId>(subject & 0xFFFFFFFFull);
        if (tag == kPlacedTag && mPlacedRefs != nullptr)
        {
            out.mRef = mPlacedRefs->find(low);
            if (out.mRef == nullptr)
                return false;
            out.mBase = out.mRef->mBase;
            return out.mBase != kNullFormId && out.mBase != kUnresolvedFormId;
        }
        if (tag == kBaseTag)
        {
            out.mBase = low;
            return low != kNullFormId;
        }
        return false;
    }

    FormId SkyrimConditionWorld::referenceOf(conditions::Subject subject) const
    {
        ActorView view;
        return viewOf(subject, view) && view.mRef != nullptr ? view.mRef->mId : kNullFormId;
    }

    bool SkyrimConditionWorld::actorStable(FormId base) const
    {
        FormId id = base;
        for (std::uint32_t depth = 0; depth <= ActorIndex::kMaxTemplateDepth; ++depth)
        {
            const ActorNpc* npc = mActors.npc(id);
            if (npc == nullptr)
                return false;
            if (npc->mTemplate == kNullFormId)
                return true;
            if (mActors.leveledList(npc->mTemplate) != nullptr)
                return false; // a random pick of a leveled list decides what this actor is
            id = npc->mTemplate;
        }
        return false;
    }

    const ActorIndex::Resolved* SkyrimConditionWorld::resolvedOf(conditions::Subject subject) const
    {
        if (isSpeaker(subject))
            return mHasResolved ? &mResolved : nullptr;
        if (!isOtherSubject(subject))
            return nullptr;
        ActorView view;
        if (!viewOf(subject, view) || !actorStable(view.mBase))
            return nullptr;
        auto it = mResolvedCache.find(view.mBase);
        if (it == mResolvedCache.end())
        {
            if (mResolvedCache.size() > 8192)
                mResolvedCache.clear();
            ActorIndex::Resolved r;
            const bool ok = mActors.resolve(view.mBase, mSpeaker.mLevel, 0, r) == ActorIndex::ResolveStatus::Ok;
            it = mResolvedCache
                     .emplace(view.mBase, ok ? std::optional<ActorIndex::Resolved>(std::move(r)) : std::nullopt)
                     .first;
        }
        return it->second ? &*it->second : nullptr;
    }

    SkyrimConditionWorld::InventoryAnswer SkyrimConditionWorld::inventoryOf(const ActorView& view, FormId item) const
    {
        InventoryAnswer answer;
        if (item == kNullFormId || item == kUnresolvedFormId || view.mBase == kNullFormId)
            return answer;
        const auto matches = [&](FormId entry) { return entry == item || inList(item, entry, 0); };
        const auto leveled = [&](FormId entry) { return mActors.leveledList(entry) != nullptr; };
        if (mActors.npc(view.mBase) != nullptr)
        {
            if (!actorStable(view.mBase))
                return answer;
            // the speaker's own resolution is the one setSpeaker made; another actor's comes from the cache
            const ActorIndex::Resolved* r = nullptr;
            if (view.mBase == mSpeaker.mBase && mHasResolved)
                r = &mResolved;
            else
                r = resolvedOf(kBaseTag | view.mBase);
            if (r == nullptr)
                return answer;
            if (view.mRef != nullptr && (view.mRef->mFlags & PlacedFlags::Dead) != 0)
            {
                // a corpse carries its death item as well, a leveled list
                const ActorNpc* npc = mActors.npc(view.mBase);
                if (npc != nullptr && npc->mDeathItem != kNullFormId)
                    return answer;
            }
            for (const InventoryEntry& e : r->mInventory)
            {
                if (leveled(e.mItem))
                    return answer;
                if (matches(e.mItem))
                    answer.mCount += static_cast<float>(e.mCount);
            }
            if (const OutfitRecord* outfit = mActors.outfit(r->mDefaultOutfit))
                for (const FormId piece : outfit->mItems)
                {
                    if (leveled(piece))
                        return answer;
                    if (matches(piece))
                    {
                        answer.mCount += 1.f;
                        answer.mInOutfit = true;
                    }
                }
            answer.mKnown = true;
            return answer;
        }
        if (mPlacedRefs != nullptr)
            if (const ContainerRecord* container = mPlacedRefs->container(view.mBase))
            {
                for (const InventoryEntry& e : container->mItems)
                {
                    if (leveled(e.mItem))
                        return answer;
                    if (matches(e.mItem))
                        answer.mCount += static_cast<float>(e.mCount);
                }
                answer.mKnown = true;
            }
        return answer;
    }

    bool SkyrimConditionWorld::isDisabled(conditions::Subject subject) const
    {
        ActorView view;
        if (isSpeaker(subject) || !viewOf(subject, view) || view.mRef == nullptr || mPlacedRefs == nullptr)
            return false; // a talking actor is enabled
        return mPlacedRefs->initiallyDisabled(*view.mRef);
    }

    bool SkyrimConditionWorld::isSneaking(conditions::Subject subject) const
    {
        (void)subject;
        return false; // nobody sneaks in a fresh game (no package of the five masters sets a sneak flag we know of)
    }

    bool SkyrimConditionWorld::isEquipped(conditions::Subject subject, std::uint32_t object) const
    {
        ActorView view;
        if (!viewOf(subject, view))
            return false; // the stand-in player wears nothing
        const InventoryAnswer a = inventoryOf(view, resolve(object));
        return a.mKnown && a.mInOutfit; // an outfit is worn when the actor appears; a carried item may or may not be
    }

    bool SkyrimConditionWorld::distanceKnown(conditions::Subject subject, std::uint32_t reference) const
    {
        ActorView view;
        if (mPlacedRefs == nullptr || !viewOf(subject, view) || view.mRef == nullptr)
            return false;
        const PlacedEntry* other = mPlacedRefs->find(resolve(reference));
        return other != nullptr && other->mCell != kNullFormId && other->mCell == view.mRef->mCell;
    }

    float SkyrimConditionWorld::distanceTo(conditions::Subject subject, std::uint32_t reference) const
    {
        if (!distanceKnown(subject, reference))
            return std::numeric_limits<float>::max();
        ActorView view;
        (void)viewOf(subject, view);
        const PlacedEntry* other = mPlacedRefs->find(resolve(reference));
        const double dx = static_cast<double>(view.mRef->mX) - other->mX;
        const double dy = static_cast<double>(view.mRef->mY) - other->mY;
        const double dz = static_cast<double>(view.mRef->mZ) - other->mZ;
        return static_cast<float>(std::sqrt(dx * dx + dy * dy + dz * dz));
    }

    FormId SkyrimConditionWorld::baseOfActorReference(FormId reference) const
    {
        if (reference == kPlayerReference)
            return kPlayerBase;
        if (mPlacedRefs != nullptr)
            if (const PlacedEntry* ref = mPlacedRefs->find(reference))
                return mActors.npc(ref->mBase) != nullptr ? ref->mBase : kNullFormId;
        return mActors.npc(reference) != nullptr ? reference : kNullFormId;
    }

    bool SkyrimConditionWorld::relationKnown(conditions::Subject subject, std::uint32_t target) const
    {
        if (mPlacedRefs == nullptr)
            return false;
        ActorView view;
        const FormId a = subject == kPlayer ? kPlayerBase : (viewOf(subject, view) ? view.mBase : kNullFormId);
        const FormId b = baseOfActorReference(resolve(target));
        const bool aActor = a == kPlayerBase || (a != kNullFormId && mActors.npc(a) != nullptr);
        return aActor && b != kNullFormId;
    }

    float SkyrimConditionWorld::relationshipRank(conditions::Subject subject, std::uint32_t target) const
    {
        ActorView view;
        if (!relationKnown(subject, target))
            return 0.f;
        const FormId a = subject == kPlayer ? kPlayerBase : (viewOf(subject, view) ? view.mBase : kNullFormId);
        const FormId b = baseOfActorReference(resolve(target));
        const std::optional<std::uint8_t> rank = mPlacedRefs->relationRank(a, b);
        return rank ? relationshipRankValue(*rank) : 0.f; // no record: acquaintances (0)
    }

    bool SkyrimConditionWorld::runsPackage(conditions::Subject subject, std::uint32_t package) const
    {
        if (!mCurrentPackage)
            return false;
        const FormId reference = isSpeaker(subject) ? mSpeaker.mReference : referenceOf(subject);
        if (reference == kNullFormId)
            return false;
        const std::optional<FormId> current = mCurrentPackage(reference);
        return current && *current != kNullFormId && *current == resolve(package);
    }

    bool SkyrimConditionWorld::offersServicesNow(conditions::Subject subject) const
    {
        if (!mCurrentPackage || mPackages == nullptr)
            return false;
        const FormId reference = isSpeaker(subject) ? mSpeaker.mReference : referenceOf(subject);
        if (reference == kNullFormId)
            return false;
        const std::optional<FormId> current = mCurrentPackage(reference);
        if (!current || *current == kNullFormId)
            return false;
        const PackageRecord* record = mPackages->package(*current);
        // PKDT general flags bit 0 "offers services" (RECALLED-VERIFY from xEdit; the 85 packages of the five masters
        // that set it are innkeepers, traders and shopkeepers)
        return record != nullptr && (record->mFlags & 1u) != 0;
    }

    bool SkyrimConditionWorld::otherAnswers(const survey::Ctda& c, conditions::Subject subject) const
    {
        ActorView view;
        if (subject == 0 || !isOtherSubject(subject) || !viewOf(subject, view))
            return false;
        const bool isActorBase = mActors.npc(view.mBase) != nullptr;
        switch (c.mFunction)
        {
            case 71: // GetInFaction
            case 73: // GetFactionRank
            case 70: // GetIsSex
            case 69: // GetIsRace
            case 560: // HasKeyword
                return resolvedOf(subject) != nullptr;
            case 72: // GetIsID
                return true;
            case 136: // GetIsReference
                return view.mRef != nullptr;
            case 46: // GetDead
                return view.mRef != nullptr && (view.mRef->mFlags & PlacedFlags::IsActor) != 0;
            case 35: // GetDisabled
                return view.mRef != nullptr && mPlacedRefs != nullptr;
            case 286: // IsSneaking
                return isActorBase;
            case 47: // GetItemCount
                return inventoryOf(view, resolve(c.mParam1)).mKnown;
            case 182:
            { // GetEquipped
                const InventoryAnswer a = inventoryOf(view, resolve(c.mParam1));
                return a.mKnown && (a.mInOutfit || a.mCount == 0.f);
            }
            case 1:
                return distanceKnown(subject, c.mParam1);
            case 403:
                return relationKnown(subject, c.mParam1);
            case 161:
            case 255:
            {
                if (!mCurrentPackage || view.mRef == nullptr)
                    return false;
                return mCurrentPackage(view.mRef->mId).has_value() && (c.mFunction == 161 || mPackages != nullptr);
            }
            case 566: // GetIsAliasRef
                return mQuestState != nullptr && mContextQuest != kNullFormId
                    && mQuestState->aliasFill(mContextQuest, c.mParam1).mKnown;
            default:
                return false;
        }
    }

    // ---- selection -----------------------------------------------------------------------------------------------

    std::uint32_t unansweredConditions(const DialogInfo& info, const SkyrimConditionWorld& world)
    {
        std::uint32_t n = 0;
        for (const survey::Ctda& c : info.mConditions)
            if (!world.answers(c))
                ++n;
        return n;
    }

    Selection selectInfo(const DialogueIndex& index, FormId topicId, SkyrimConditionWorld& world,
        const DialogueSpeaker& speaker, const SelectionOptions& options, conditions::Evaluator* evaluator)
    {
        Selection result;
        const DialogTopic* topic = index.topic(topicId);
        result.mTopic = topic;
        world.setSpeaker(speaker);
        conditions::Evaluator local;
        conditions::Evaluator& eval = evaluator != nullptr ? *evaluator : local;

        const std::vector<const DialogInfo*> infos = index.infosOf(topicId);
        std::vector<const DialogInfo*> randoms;
        for (const DialogInfo* info : infos)
        {
            ++result.mConsidered;
            const FormId quest = info->mQuest != kNullFormId ? info->mQuest : (topic != nullptr ? topic->mQuest : 0);
            if (options.mQuestRunning && quest != kNullFormId && !options.mQuestRunning(quest))
                continue;
            if ((info->mFlags & InfoFlags::SayOnce) != 0 && options.mSaid && options.mSaid(info->mCommon.mId))
                continue;
            if (info->mSpeaker != kNullFormId && info->mSpeaker != speaker.mBase)
                continue;
            if (!info->mConditionsOk)
                continue;
            world.setQuestContext(quest);
            // the quest's own dialogue conditions gate all of its dialogue
            bool questUncertain = false;
            if (options.mQuests != nullptr)
            {
                if (const QuestRecord* record = options.mQuests->quest(quest))
                {
                    if (!record->mConditionsOk)
                        continue;
                    if (!record->mConditions.empty())
                    {
                        world.setPlugin(&options.mQuests->resolverOf(*record));
                        const bool questPass = eval.evaluate(record->mConditions, world);
                        for (const survey::Ctda& c : record->mConditions)
                            questUncertain = questUncertain || !world.answers(c);
                        world.setPlugin(nullptr);
                        if (!questPass)
                        {
                            if (result.mInfo == nullptr && questUncertain)
                                ++result.mUncertainBefore;
                            continue;
                        }
                    }
                }
            }
            world.setPlugin(&index.resolverOf(*info));
            const bool pass = eval.evaluate(info->mConditions, world);
            // counted with the INFO's plugin set: the answers resolve the stored ids through it
            const bool uncertain = questUncertain || unansweredConditions(*info, world) > 0;
            world.setPlugin(nullptr);
            if (result.mInfo == nullptr && uncertain)
                ++result.mUncertainBefore;
            if (!pass)
                continue;
            ++result.mPassing;
            if (result.mInfo == nullptr)
            {
                result.mInfo = info;
                if ((info->mFlags & InfoFlags::Random) == 0)
                    break;
            }
            if ((info->mFlags & InfoFlags::Random) != 0)
                randoms.push_back(info);
        }
        world.setQuestContext(kNullFormId);
        if (!randoms.empty())
            result.mInfo = randoms[options.mPick % randoms.size()];
        return result;
    }

    Selection pickFromTopics(const DialogueIndex& index, const std::string& subtypeName, SkyrimConditionWorld& world,
        const DialogueSpeaker& speaker, const SelectionOptions& options, conditions::Evaluator* evaluator)
    {
        std::vector<FormId> ids = index.topicIds(subtypeName);
        std::stable_sort(ids.begin(), ids.end(),
            [&](FormId a, FormId b) { return index.topic(a)->mPriority > index.topic(b)->mPriority; });
        Selection total;
        for (FormId id : ids)
        {
            Selection s = selectInfo(index, id, world, speaker, options, evaluator);
            total.mConsidered += s.mConsidered;
            total.mPassing += s.mPassing;
            if (s.mInfo != nullptr)
            {
                total.mInfo = s.mInfo;
                total.mTopic = s.mTopic;
                total.mUncertainBefore += s.mUncertainBefore;
                return total;
            }
            total.mUncertainBefore += s.mUncertainBefore;
        }
        return total;
    }
}
