#include "game_skyrimse/dialogue.hpp"

#include "game_skyrimse/package.hpp"
#include "game_skyrimse/placed_refs.hpp"

#include "game_skyrimse/actor_mechanics.hpp"
#include "game_skyrimse/location.hpp"
#include "game_skyrimse/pack_core.hpp"
#include "game_skyrimse/quest_script.hpp"

#include <algorithm>
#include <unordered_set>
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
                    case Content::fourCC("DLBR"): // task 0261
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
                    case Content::fourCC("DLBR"):
                        ok = readBranch(record, bytes);
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
                            {
                                // Task 0187: the scripts and fragments, parsed; a damaged VMAD stays "noted only".
                                vmad::ParseResult parsed = vmad::parseInfo(data);
                                if (parsed.mOk)
                                {
                                    (void)vmad::resolveForms(parsed.mVmad, [this](std::uint32_t stored) {
                                        const std::optional<FormId> id = mResolver.resolve(stored);
                                        return id ? *id : 0u;
                                    });
                                    info.mVmad = std::make_shared<const vmad::Vmad>(std::move(parsed.mVmad));
                                }
                                else
                                    odd(type);
                            }
                            return true;
                        case Content::fourCC("RNAM"):
                            return lstring(type, data, info.mPrompt);
                        case Content::fourCC("DNAM"): // task 0261: the shared info
                            return formField(type, data, info.mSharedInfo);
                        case Content::fourCC("TWAT"): // task 0315: the walk-away topic
                            return formField(type, data, info.mWalkAwayTopic);
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

            // Task 0261: a DLBR record: the starting topic (SNAM) and the flags (DNAM).
            bool readBranch(const Content::RawRecord& record, std::span<const std::byte> bytes)
            {
                DialogBranch branch;
                branch.mCommon = common(record);
                const bool ok = walkSubs(bytes, [&](std::uint32_t type, std::span<const std::byte> data) {
                    switch (type)
                    {
                        case Content::fourCC("EDID"):
                            return zstring(type, data, branch.mCommon.mEditorId);
                        case Content::fourCC("SNAM"):
                            return formField(type, data, branch.mStartTopic);
                        case Content::fourCC("DNAM"):
                            if (data.size() != 4)
                            {
                                odd(type);
                                return true;
                            }
                            branch.mFlags = readLe<std::uint32_t>(data.data());
                            return true;
                        default:
                            return true;
                    }
                });
                if (ok)
                    mOut.mBranches.push_back(std::move(branch));
                return ok;
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
            mInfoTopic[info.mCommon.mId] = info.mTopic; // task 0261
            const auto it = slot.mByInfo.find(info.mCommon.mId);
            const bool deleted = info.mCommon.deleted();
            info.mPlugin = plugin;
            if (it != slot.mByInfo.end())
            {
                slot.mAlive[it->second] = !deleted;
                mScriptedInfos.erase(info.mCommon.mId);
                if (!deleted)
                {
                    if (info.mVmad != nullptr)
                        mScriptedInfos[info.mCommon.mId] = info.mTopic;
                    slot.mList[it->second] = std::move(info);
                }
                continue;
            }
            if (deleted)
                continue;
            if (info.mVmad != nullptr)
                mScriptedInfos[info.mCommon.mId] = info.mTopic;
            slot.mByInfo[info.mCommon.mId] = slot.mList.size();
            slot.mList.push_back(std::move(info));
            slot.mAlive.push_back(true);
        }
        for (const DialogBranch& b : records.mBranches) // task 0261
        {
            if (!usable(b.mCommon.mId))
                continue;
            if (b.mCommon.deleted())
            {
                mBranchStart.erase(b.mCommon.mId);
                mBlockingStart.erase(b.mCommon.mId);
            }
            else
            {
                mBranchStart[b.mCommon.mId] = b.mStartTopic;
                if ((b.mFlags & kBranchBlocking) != 0 && b.mStartTopic != kNullFormId)
                    mBlockingStart[b.mCommon.mId] = b.mStartTopic;
                else
                    mBlockingStart.erase(b.mCommon.mId);
            }
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

    const DialogInfo* DialogueIndex::scriptedInfo(FormId id) const
    {
        const auto topic = mScriptedInfos.find(id);
        if (topic == mScriptedInfos.end())
            return nullptr;
        const auto slot = mSlots.find(topic->second);
        if (slot == mSlots.end())
            return nullptr;
        const auto at = slot->second.mByInfo.find(id);
        if (at == slot->second.mByInfo.end() || !slot->second.mAlive[at->second])
            return nullptr;
        const DialogInfo& info = slot->second.mList[at->second];
        return info.mVmad != nullptr ? &info : nullptr;
    }

    const DialogInfo* DialogueIndex::findInfo(FormId id) const
    {
        const auto topic = mInfoTopic.find(id);
        if (topic == mInfoTopic.end())
            return nullptr;
        const auto slot = mSlots.find(topic->second);
        if (slot == mSlots.end())
            return nullptr;
        const auto at = slot->second.mByInfo.find(id);
        if (at == slot->second.mByInfo.end() || !slot->second.mAlive[at->second])
            return nullptr;
        return &slot->second.mList[at->second];
    }

    std::vector<FormId> DialogueIndex::blockingStarts() const
    {
        std::vector<FormId> out;
        out.reserve(mBlockingStart.size());
        for (const auto& [branch, start] : mBlockingStart)
            out.push_back(start);
        std::sort(out.begin(), out.end());
        out.erase(std::unique(out.begin(), out.end()), out.end());
        return out;
    }

    bool DialogueIndex::isBranchStart(FormId topicId) const
    {
        const DialogTopic* t = topic(topicId);
        if (t == nullptr || t->mBranch == kNullFormId)
            return true;
        const auto branch = mBranchStart.find(t->mBranch);
        return branch == mBranchStart.end() || branch->second == kNullFormId || branch->second == topicId;
    }

    namespace
    {
        constexpr std::uint16_t kFnGetIsId = 72;
        constexpr std::uint16_t kFnGetIsAliasRef = 566;
    }

    namespace
    {
        FormId sSelectDebugTopic = kNullFormId;
    }

    std::vector<FormId> DialogueIndex::questGreetingTopics() const
    {
        // a topic some INFO links to (TCLT) is a reply inside a conversation: MG01FaraldaYoureInBranchTopic and the
        // Nirya greeting are such starts; the greeting is a start nothing links to (MG01FaraldaInitialBranchTopic)
        std::unordered_set<FormId> linked;
        for (const auto& [topicId, slot] : mSlots)
            for (std::size_t i = 0; i < slot.mList.size(); ++i)
                if (slot.mAlive[i])
                    linked.insert(slot.mList[i].mLinksTo.begin(), slot.mList[i].mLinksTo.end());
        std::vector<FormId> ids;
        for (const auto& [id, t] : mTopics)
        {
            if (id == sSelectDebugTopic) // task 0324 (the debug aid): why a topic is or is not a quest greeting
                conditions::conditionDebugReport("GREETING topic " + std::to_string(id) + ": category "
                    + std::to_string(static_cast<int>(t.mCategory)) + " subtype " + t.mSubtypeName + " named "
                    + std::to_string(t.mName.mPresent) + " branch " + std::to_string(t.mBranch) + " linked "
                    + std::to_string(linked.count(id)));
            if (t.mCategory != TopicCategory::Topic || t.mSubtypeName != "CUST" || t.mName.mPresent
                || t.mBranch == kNullFormId || linked.count(id) != 0)
                continue;
            // a quest's greeting is for one actor: some INFO of the topic names its speaker (GetIsID or
            // GetIsAliasRef). A shared quest's start (WESharedDialogue...) names none and would greet everybody.
            bool namesSpeaker = false;
            if (const auto slot = mSlots.find(id); slot != mSlots.end())
                for (std::size_t i = 0; i < slot->second.mList.size() && !namesSpeaker; ++i)
                    if (slot->second.mAlive[i])
                        for (const survey::Ctda& c : slot->second.mList[i].mConditions)
                            if (c.mFunction == kFnGetIsId || c.mFunction == kFnGetIsAliasRef)
                            {
                                namesSpeaker = true;
                                break;
                            }
            if (id == sSelectDebugTopic)
                conditions::conditionDebugReport("GREETING topic " + std::to_string(id) + ": names its speaker "
                    + std::to_string(namesSpeaker) + ", the start of its branch "
                    + std::to_string(mBranchStart.count(t.mBranch) != 0 && mBranchStart.at(t.mBranch) == id));
            if (!namesSpeaker)
                continue;
            const auto branch = mBranchStart.find(t.mBranch);
            if (branch == mBranchStart.end() || branch->second != id)
                continue;
            ids.push_back(id);
        }
        std::sort(ids.begin(), ids.end(), [this](FormId a, FormId b) {
            const float pa = mTopics.at(a).mPriority;
            const float pb = mTopics.at(b).mPriority;
            return pa != pb ? pa > pb : a > b;
        });
        return ids;
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

    const GlobalValue* DialogueIndex::globalByEditorId(std::string_view name) const
    {
        for (const auto& [id, value] : mGlobals)
        {
            const std::string& editor = value.mCommon.mEditorId;
            if (editor.size() != name.size())
                continue;
            bool same = true;
            for (std::size_t i = 0; i < name.size() && same; ++i)
                same = std::tolower(static_cast<unsigned char>(editor[i]))
                    == std::tolower(static_cast<unsigned char>(name[i]));
            if (same)
                return &value;
        }
        return nullptr;
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

    std::uint16_t SkyrimConditionWorld::speakerLevel() const
    {
        return speakerStats() ? mStats.level : 0;
    }

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

    FormId SkyrimConditionWorld::overlayReferenceOf(conditions::Subject subject) const
    {
        if (subject == kPlayer)
            return kNullFormId;
        if (isSpeaker(subject))
            return mSpeaker.mReference;
        return referenceOf(subject);
    }

    float SkyrimConditionWorld::actorValue(conditions::Subject subject, std::uint32_t actorValueIndex) const
    {
        if (mFollowerOverlay != nullptr && subject != kPlayer)
        {
            const FormId reference = overlayReferenceOf(subject);
            if (reference != kNullFormId)
                if (const std::optional<float> value = mFollowerOverlay->actorValue(reference, actorValueIndex))
                    return *value; // task 0239: WaitingForPlayer, set by "Wait here" and "Follow me"
        }
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
            return subject == kPlayer && mStoryPlayerLevel.has_value() ? static_cast<float>(*mStoryPlayerLevel) : 0.f;
        if (subject == kPlayer)
            return static_cast<float>(mPlayer->mLevel);
        return isSpeaker(subject) && speakerStats() ? static_cast<float>(mStats.level) : 0.f;
    }

    // Task 0184: the inventory's gold when a source is set (read at the moment of the check), else the stand-in's.
    float SkyrimConditionWorld::playerGold() const
    {
        if (mGoldSource)
        {
            const float gold = mGoldSource();
            return std::isfinite(gold) && gold > 0.f ? gold : 0.f;
        }
        return mPlayer != nullptr ? mPlayer->mGold : 0.f;
    }

    float SkyrimConditionWorld::goldOf(conditions::Subject subject) const
    {
        return mPlayer != nullptr && subject == kPlayer ? playerGold() : 0.f;
    }

    float SkyrimConditionWorld::itemCount(conditions::Subject subject, std::uint32_t item) const
    {
        constexpr FormId kGold001 = 0x0000000F;
        if (subject == kPlayer && mItemSource && resolve(item) != kGold001)
        {
            const float count = mItemSource(resolve(item)); // task 0270: read at the moment of the check
            return std::isfinite(count) && count > 0.f ? count : 0.f;
        }
        if (mPlayer != nullptr && subject == kPlayer)
            return resolve(item) == kGold001 ? playerGold() : 0.f;
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

    float SkyrimConditionWorld::refTypeAliveCount(std::uint32_t location, std::uint32_t refType) const
    {
        if (!mCountRefTypes || mLocations == nullptr)
            return 0.f;
        const FormId place = resolve(location);
        const FormId type = resolve(refType);
        return static_cast<float>(mLocations->countSpecialRefs(place, type, [this](FormId reference) {
            const PlacedEntry* entry = mPlacedRefs != nullptr ? mPlacedRefs->find(reference) : nullptr;
            if (mDeadLookup && mDeadLookup(reference))
                return false; // killed in this session (task 0326)
            return entry == nullptr || (entry->mFlags & PlacedFlags::Dead) == 0;
        }));
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
        if (subject == kPlayer && mPlayerPlaceKnown)
            return mPlayerInterior;
        return isSpeaker(subject) && mSpeaker.mInterior.value_or(false);
    }

    bool SkyrimConditionWorld::storyLocationWithin(FormId location, FormId ancestor) const
    {
        return mLocations != nullptr && mLocations->isWithin(location, ancestor);
    }

    bool SkyrimConditionWorld::storyLocationHasKeyword(FormId location, FormId keyword) const
    {
        return mLocations != nullptr && mLocations->hasKeyword(location, keyword);
    }

    FormId SkyrimConditionWorld::storyBaseOf(FormId reference) const
    {
        const PlacedEntry* entry = mPlacedRefs != nullptr ? mPlacedRefs->find(reference) : nullptr;
        return entry != nullptr ? entry->mBase : kNullFormId;
    }

    bool SkyrimConditionWorld::inLocation(conditions::Subject subject, std::uint32_t location) const
    {
        const FormId id = resolve(location);
        // task 0211: in the story manager the subject is the player, who is where the event says (the location entered,
        // else the one left)
        if (subject == kPlayer && mStoryEvent != nullptr && mSpeaker.mBase == kNullFormId)
        {
            const FormId at
                = mStoryEvent->mLocation2 != kNullFormId ? mStoryEvent->mLocation2 : mStoryEvent->mLocation1;
            return mLocations != nullptr && id != kNullFormId && at != kNullFormId && mLocations->isWithin(at, id);
        }
        // task 0312: the player's own location, where the pack told it (a quest's stage entry: DLC2MQ01's "on Solstheim")
        if (subject == kPlayer && mPlayerLocation != kNullFormId)
            return mLocations != nullptr && id != kNullFormId && mLocations->isWithin(mPlayerLocation, id);
        return mLocations != nullptr && isSpeaker(subject) && mSpeaker.mLocationKnown && id != kNullFormId
            && mLocations->isWithin(mSpeaker.mLocation, id);
    }

    // Task 0305 (fn_360 GetInCurrentLocAlias): the player stands in the location an alias holds, or inside it. Only the
    // player is answered, from the location the pack was told he stands in.
    bool SkyrimConditionWorld::inLocationAlias(conditions::Subject subject, std::uint32_t alias) const
    {
        if (subject != kPlayer || mLocations == nullptr || mPlayerLocation == kNullFormId)
            return false;
        const FormId held = aliasReference(alias);
        return held != kNullFormId && mLocations->isWithin(mPlayerLocation, held);
    }

    FormId SkyrimConditionWorld::resolveLocationForm(std::uint32_t stored) const
    {
        return resolve(stored);
    }

    bool SkyrimConditionWorld::locationHasRefType(FormId location, FormId refType) const
    {
        return mLocations != nullptr && mLocations->hasRefType(location, refType);
    }

    bool SkyrimConditionWorld::locationWithin(FormId location, FormId ancestor) const
    {
        return mLocations != nullptr && mLocations->isWithin(location, ancestor);
    }

    FormId SkyrimConditionWorld::aliasReference(std::uint32_t alias) const
    {
        if (mAliasView)
            return mAliasView(alias);
        if (mQuestState == nullptr || mContextQuest == kNullFormId)
            return kNullFormId;
        return mQuestState->aliasFill(mContextQuest, alias).mReference;
    }

    FormId SkyrimConditionWorld::locationAncestorWithKeyword(FormId location, FormId keyword) const
    {
        return mLocations != nullptr ? mLocations->ancestorWithKeyword(location, keyword) : kNullFormId;
    }

    // Task 0230: the editor location of a reference, from the first source that knows it: the reference's own XLCN (the
    // data's "persistent location": 5,639 of 10,504 placed actors of the five masters have one), the location a
    // unique-NPC list gives its actor, the location whose special or persistent reference list names it, the location
    // of its cell. RECALLED-VERIFY: the order (the Creation Kit wiki is unreachable); each source is a named fallback
    // of the one before.
    FormId SkyrimConditionWorld::editorLocationOf(FormId reference, FormId base) const
    {
        const PlacedEntry* entry
            = mPlacedRefs != nullptr && reference != kNullFormId ? mPlacedRefs->find(reference) : nullptr;
        if (entry != nullptr && entry->mLocation != kNullFormId && entry->mLocation != kUnresolvedFormId)
            return entry->mLocation;
        if (base == kNullFormId && entry != nullptr)
            base = entry->mBase;
        FormId at = kNullFormId;
        if (mLocations != nullptr)
        {
            at = mLocations->uniqueNpcLocation(base, reference);
            if (at == kNullFormId)
                at = mLocations->editorLocationOf(reference);
        }
        if (at == kNullFormId && mReferenceLocation && reference != kNullFormId)
            at = mReferenceLocation(reference);
        return at;
    }

    FormId SkyrimConditionWorld::editorLocationOfReference(FormId reference) const
    {
        return editorLocationOf(reference, kNullFormId);
    }

    FormId SkyrimConditionWorld::editorLocationOfSubject(conditions::Subject subject) const
    {
        ActorView view;
        if (!viewOf(subject, view))
            return kNullFormId;
        return editorLocationOf(view.mRef != nullptr ? view.mRef->mId : kNullFormId, view.mBase);
    }

    bool SkyrimConditionWorld::locationHasKeyword(conditions::Subject subject, std::uint32_t keyword) const
    {
        // task 0211: a candidate location of a location alias
        if ((subject & 0xFFFFFFFF00000000ull) == kLocationTag)
            return mLocations != nullptr
                && mLocations->hasKeyword(static_cast<FormId>(subject & 0xFFFFFFFFull), resolve(keyword));
        return mLocations != nullptr && isSpeaker(subject) && mSpeaker.mLocationKnown
            && mLocations->hasKeyword(mSpeaker.mLocation, resolve(keyword));
    }

    float SkyrimConditionWorld::keywordDataForLocation(std::uint32_t location, std::uint32_t keyword) const
    {
        return mKeywordData != nullptr ? mKeywordData->get(resolve(location), resolve(keyword)) : 0.f;
    }

    float SkyrimConditionWorld::keywordDataForCurrentLocation(conditions::Subject subject, std::uint32_t keyword) const
    {
        if (mKeywordData == nullptr)
            return 0.f;
        FormId at = kNullFormId;
        if (subject == kPlayer && mStoryEvent != nullptr && mSpeaker.mBase == kNullFormId)
            at = mStoryEvent->mLocation2 != kNullFormId ? mStoryEvent->mLocation2 : mStoryEvent->mLocation1;
        else if (isSpeaker(subject) && mSpeaker.mLocationKnown)
            at = mSpeaker.mLocation;
        return at != kNullFormId ? mKeywordData->get(at, resolve(keyword)) : 0.f;
    }

    bool SkyrimConditionWorld::talkedToPlayer(conditions::Subject subject) const
    {
        if (!isSpeaker(subject))
            return false;
        return mTalked || (mTalkedLookup && mSpeaker.mReference != kNullFormId && mTalkedLookup(mSpeaker.mReference));
    }

    float SkyrimConditionWorld::actorValuePercent(conditions::Subject subject, std::uint32_t actorValueIndex) const
    {
        // a fresh actor's health, magicka and stamina are full; no other value is guessed
        return isKnownActor(subject) && actorValueIndex >= kAvHealth && actorValueIndex <= kAvStamina ? 1.f : 0.f;
    }

    float SkyrimConditionWorld::scriptVariable(std::uint32_t object, const std::string& name) const
    {
        if (mLiveQuestVariable) // task 0292: the live source answers for any form a script holds a variable on
            if (const std::optional<float> live = mLiveQuestVariable(resolve(object), name))
                return *live;
        return 0.f;
    }

    float SkyrimConditionWorld::questVariable(std::uint32_t quest, const std::string& name) const
    {
        if (mLiveQuestVariable) // task 0261: what the running script holds wins
            if (const std::optional<float> live = mLiveQuestVariable(resolve(quest), name))
                return *live;
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
        return mPlayer != nullptr && isSpeaker(subject) && speakerStats() && bribeSucceeds(playerGold(), mStats.level);
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

    // Task 0258: what the crime rules tell the conversation. Only the speaker is asked about (run on the speaker's
    // crime faction when the condition names none); without a source the fresh-game answers stand.
    float SkyrimConditionWorld::crimeGold(conditions::Subject subject, std::uint32_t faction) const
    {
        if (mCrimeSource == nullptr || !mCrimeSource->mCrimeGold || (faction == 0 && !isSpeaker(subject)))
            return 0.f;
        const float gold = mCrimeSource->mCrimeGold(mSpeaker.mBase, faction == 0 ? kNullFormId : resolve(faction));
        return std::isfinite(gold) && gold > 0.f ? gold : 0.f;
    }

    bool SkyrimConditionWorld::canPayCrimeGold(conditions::Subject subject, std::uint32_t faction) const
    {
        if (mCrimeSource == nullptr || !mCrimeSource->mCanPay || (faction == 0 && !isSpeaker(subject)))
            return false;
        return mCrimeSource->mCanPay(mSpeaker.mBase, faction == 0 ? kNullFormId : resolve(faction));
    }

    bool SkyrimConditionWorld::isAlarmed(conditions::Subject subject) const
    {
        return mCrimeSource != nullptr && mCrimeSource->mAlarmed && isSpeaker(subject)
            && mCrimeSource->mAlarmed(mSpeaker.mBase);
    }

    // fn_125 IsGuard (task 0258): the NPC's class (after templates) carries the Guard flag, OR the NPC is a member of
    // the game's own guard faction (IsGuardFaction, the marker task 0197 settled on: the street guards' class is the
    // civil war soldier's, which has no Guard flag, yet the quest DialogueCrimeGuards asks IsGuard of every guard it
    // speaks for). RECALLED-VERIFY: what the engine reads for IsGuard.
    bool SkyrimConditionWorld::isGuardClass(conditions::Subject subject) const
    {
        const ActorIndex::Resolved* r = resolvedOf(subject);
        if (r == nullptr)
            return false;
        if (const ClassRecord* record = mActors.classRecord(r->mClass); record != nullptr && record->mGuard)
            return true;
        for (const FactionMembership& membership : r->mFactions)
        {
            const FactionRecord* faction = mActors.faction(membership.mFaction);
            if (faction == nullptr || membership.mRank < 0)
                continue;
            const std::string& id = faction->mCommon.mEditorId;
            const std::string_view guardId = "IsGuardFaction";
            if (id.size() == guardId.size() && std::equal(id.begin(), id.end(), guardId.begin(), [](char a, char b) {
                    return std::tolower(static_cast<unsigned char>(a)) == std::tolower(static_cast<unsigned char>(b));
                }))
                return true;
        }
        return false;
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
            case 360: // GetInCurrentLocAlias (task 0305): answered where the player's location is told
                return mLocations != nullptr && mPlayerLocation != kNullFormId;
            case 358: // IsPlayerMovingIntoNewSpace (task 0271): always 0, answered only where the player's place is
                      // told
                return mPlayerPlaceKnown;
            case 61: // GetAlarmed and CanPayCrimeGold: the crime rules answer (task 0258)
            case 497:
                return mCrimeSource != nullptr;
            case 125: // IsGuard: the class flag (task 0258)
                return true;
            case 77: // GetRandomPercent: answered by the seeded generator (task 0156)
                return mRandomSeeded;
            case 84: // GetDeadCount: nobody has been killed in a fresh game
            case 606: // GetKeywordDataForLocation: 0 until a script stores data (task 0217: Location.SetKeywordData)
            case 314: // IsActorAVictim: no crime is under way
                return true;
            case 131: // GetPCIsSex
            case 130: // GetPCIsRace
                return mPlayer != nullptr;
            case 630: // task 0292: GetVMScriptVariable of a reference's script (the bridge trigger of Winterhold)
                return static_cast<bool>(mLiveQuestVariable)
                    && mLiveQuestVariable(resolve(c.mParam1), c.mString2).has_value();
            case 629:
            { // GetVMQuestVariable: the script's value before any script ran
                if (mLiveQuestVariable && mLiveQuestVariable(resolve(c.mParam1), c.mString2))
                    return true; // task 0261
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
                if (c.mFunction == 80 && mStoryPlayerLevel.has_value())
                    return true; // task 0305: the story world knows the level only
                if (c.mFunction == 286 || c.mFunction == 263 || c.mFunction == 101)
                    return true; // task 0235: the player's stance is told by the pack
                if (mPlayer != nullptr && isFreshStateFunction(c.mFunction))
                    return true;
                if (mPlayer != nullptr && (c.mFunction == 69 || c.mFunction == 70))
                    return true; // the stand-in's race and sex
                if (c.mFunction == 1 && (c.mFlags & 0x02) != 0) // task 0276: GetDistance(alias) of the player
                    return distanceToAliasKnown(kPlayer, c.mParam1);
                if (mPlayerPlaceKnown && (c.mFunction == 67 || c.mFunction == 300 || c.mFunction == 310))
                    return true; // task 0271: the place the core told
                if (mPlayer != nullptr && c.mFunction == 640 && c.mParam1 >= kAvHealth && c.mParam1 <= kAvStamina)
                    return true;
                if (c.mFunction == 359 && mLocations != nullptr && mPlayerLocation != kNullFormId)
                    return true; // task 0312: GetInCurrentLoc of the player, where the pack told his location
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
            case 651: // GetKeywordDataForCurrentLocation (task 0217)
                return mKeywordData != nullptr && (mSpeaker.mLocationKnown || mStoryEvent != nullptr);
            case 359: // GetInCurrentLoc (task 0312: also the player's own location, where the pack told it)
                return mLocations != nullptr
                    && (mSpeaker.mLocationKnown || mSubjectLocation != kNullFormId || mStoryEvent != nullptr
                        || mPlayerLocation != kNullFormId);
            case 562: // LocationHasKeyword
                return mLocations != nullptr
                    && (mSpeaker.mLocationKnown || mSubjectLocation != kNullFormId || mStoryEvent != nullptr);
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
            case 286: // IsSneaking: nobody sneaks in a fresh game, and the player's stance is told (task 0235)
            case 263: // IsWeaponOut and IsWeaponMagicOut: the player's stance is told, an actor's is never drawn
            case 101:
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
                if (c.mFunction == 255 && mVendorServices && mSpeaker.mBase != kNullFormId
                    && mVendorServices(mSpeaker.mBase))
                    return true; // task 0189: a merchant offers its services
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
                if (mSubjectLocation != kNullFormId) // task 0211: a location alias candidate
                    return kLocationTag | mSubjectLocation;
                if (mStoryEvent != nullptr && mSpeaker.mBase == kNullFormId)
                    return kPlayer; // task 0211: the story manager's subject is the player
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
                if (mPlacedRefs != nullptr && ref != kNullFormId && findReference(ref) != nullptr)
                    return kPlacedTag | ref;
                return 0;
            }
            case 7: // task 0194: a reference of the story event; the reference field holds 'R' and '1' or '2'
            {
                if (mStoryEvent == nullptr || mPlacedRefs == nullptr || (reference & 0xFF) != 'R')
                    return 0;
                const std::uint32_t which = (reference >> 8) & 0xFF;
                const FormId ref = which == '1' ? mStoryEvent->mRef1 : which == '2' ? mStoryEvent->mRef2 : kNullFormId;
                if (ref == kNullFormId || findReference(ref) == nullptr)
                    return 0;
                return kPlacedTag | ref;
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
        if (mFollowerOverlay != nullptr && id != kNullFormId)
        {
            const FormId reference = overlayReferenceOf(subject);
            if (reference != kNullFormId)
                if (const std::optional<float> rank = mFollowerOverlay->factionRank(reference, id))
                    return *rank >= 0.f; // task 0239: added or removed by a script
        }
        const ActorIndex::Resolved* r = resolvedOf(subject);
        if (r == nullptr || id == kNullFormId)
            return false;
        // Task 0239: a rank of -1 (255 in the stored byte) is a placeholder, not a membership: the hirelings are
        // listed in CurrentFollowerFaction and CurrentHireling that way until they are hired (GetInFaction needs
        // rank 0 or more)
        return std::any_of(r->mFactions.begin(), r->mFactions.end(), [&](const FactionMembership& f) {
            return f.mFaction == id && (f.mRank >= 0 || mRankMinusOneMembers.count(id) != 0); // task 0261
        });
    }

    float SkyrimConditionWorld::factionRank(conditions::Subject subject, std::uint32_t faction) const
    {
        const FormId id = resolve(faction);
        if (mFollowerOverlay != nullptr && id != kNullFormId)
        {
            const FormId reference = overlayReferenceOf(subject);
            if (reference != kNullFormId)
                if (const std::optional<float> rank = mFollowerOverlay->factionRank(reference, id))
                    return *rank;
        }
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
        if (mFollowerOverlay != nullptr)
            if (const std::optional<float> value = mFollowerOverlay->globalValue(resolve(global)))
                return *value; // task 0239: a global a script set (PlayerFollowerCount ...)
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
        if (subject == kPlayer && mPlayerPlaceKnown)
            return mPlayerInterior && mPlayerCell != kNullFormId && resolve(cell) == mPlayerCell;
        if (isOtherSubject(subject))
        {
            // task 0194: another reference stands in the cell the data places it in
            ActorView view;
            return viewOf(subject, view) && view.mRef != nullptr && view.mRef->mCell == resolve(cell);
        }
        return isSpeaker(subject) && mSpeaker.mCell != kNullFormId && resolve(cell) == mSpeaker.mCell;
    }

    bool SkyrimConditionWorld::inSameCell(conditions::Subject subject, std::uint32_t reference) const
    {
        // Task 0325 (Astrid's forced greeting: DBAstridSleepGreet needs GetInSameCell(player)): the subject's cell and the
        // reference's. The player's cell is known for an interior only (the place the core told); a placed reference or
        // actor stands in the cell the data places it in, which is also where a script's MoveTo put it inside its cell.
        FormId mine = kNullFormId;
        if (subject == kPlayer)
            mine = mPlayerPlaceKnown && mPlayerInterior ? mPlayerCell : kNullFormId;
        else if (isOtherSubject(subject))
        {
            ActorView view;
            if (viewOf(subject, view) && view.mRef != nullptr)
                mine = view.mRef->mCell;
        }
        else if (isSpeaker(subject))
            mine = mSpeaker.mCell;
        if (mine == kNullFormId)
            return false;
        const FormId target = resolve(reference);
        FormId theirs = kNullFormId;
        if (target == kPlayerReference)
            theirs = mPlayerPlaceKnown && mPlayerInterior ? mPlayerCell : kNullFormId;
        else if (const PlacedEntry* placed = findReference(target))
            theirs = placed->mCell;
        return theirs != kNullFormId && theirs == mine;
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
        if (subject == kPlayer && mPlayerPlaceKnown)
            return !mPlayerInterior && mPlayerWorld != kNullFormId && resolve(worldspace) == mPlayerWorld;
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
            return findReference(fill.mReference) != nullptr ? (kPlacedTag | fill.mReference) : 0;
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
            out.mRef = findReference(low);
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
        // Nobody else sneaks in a fresh game (no package of the five masters sets a sneak flag we know of); the player
        // does when the pack tells the stance (task 0235).
        return subject == kPlayer && mPlayerSneaking.load(std::memory_order_relaxed);
    }

    conditions::Subject SkyrimConditionWorld::crimeVictim() const
    {
        // Task 0243: the bark of a witness that is the victim; no crime is under way otherwise
        return static_cast<conditions::Subject>(mCrimeVictim.load(std::memory_order_relaxed));
    }

    bool SkyrimConditionWorld::weaponOrMagicOut(conditions::Subject subject) const
    {
        const std::uint8_t drawn = mPlayerDrawState.load(std::memory_order_relaxed);
        return subject == kPlayer && (drawn == 1 || drawn == 2);
    }

    float SkyrimConditionWorld::weaponOutState(conditions::Subject subject) const
    {
        // 0 nothing drawn, 2 a weapon in either hand (fists, 1, cannot be told from the engine's draw state); a spell
        // does not count (CK wiki, shown by toddhow, 2026-11-11).
        return subject == kPlayer && mPlayerDrawState.load(std::memory_order_relaxed) == 1 ? 2.f : 0.f;
    }

    bool SkyrimConditionWorld::isEquipped(conditions::Subject subject, std::uint32_t object) const
    {
        if (subject == kPlayer && mWornSource)
            return mWornSource(resolve(object)); // task 0270: what the inventory says the player wears
        ActorView view;
        if (!viewOf(subject, view))
            return false; // the stand-in player wears nothing
        const InventoryAnswer a = inventoryOf(view, resolve(object));
        return a.mKnown && a.mInOutfit; // an outfit is worn when the actor appears; a carried item may or may not be
    }

    bool SkyrimConditionWorld::distanceKnown(conditions::Subject subject, std::uint32_t reference) const
    {
        ActorView view;
        double lx = 0, ly = 0, lz = 0;
        if (livePlaces(subject, reference, lx, ly, lz, lx, ly, lz))
            return true;
        if (mPlacedRefs == nullptr || !viewOf(subject, view) || view.mRef == nullptr)
            return false;
        const PlacedEntry* other = mPlacedRefs->find(resolve(reference));
        return other != nullptr && other->mCell != kNullFormId && other->mCell == view.mRef->mCell;
    }

    bool SkyrimConditionWorld::livePlaces(conditions::Subject subject, std::uint32_t reference, double& ax, double& ay,
        double& az, double& tx, double& ty, double& tz) const
    {
        if (!mLiveActorPlace || !mLiveTargetPlace || subject == 0)
            return false;
        ActorView view;
        if (!viewOf(subject, view) || view.mRef == nullptr)
            return false;
        return mLiveActorPlace(view.mRef->mId, ax, ay, az) && mLiveTargetPlace(resolve(reference), tx, ty, tz);
    }

    const PlacedEntry* SkyrimConditionWorld::findReference(FormId reference) const
    {
        if (mPlacedRefs == nullptr || reference == kNullFormId)
            return nullptr;
        if (const PlacedEntry* placed = mPlacedRefs->find(reference))
            return placed;
        const auto known = mCreatedEntries.find(reference);
        if (known != mCreatedEntries.end())
            return &known->second;
        const FormId base = mCreatedBase ? mCreatedBase(reference) : kNullFormId;
        if (base == kNullFormId)
            return nullptr;
        PlacedEntry entry;
        entry.mId = reference;
        entry.mBase = base;
        return &mCreatedEntries.emplace(reference, entry).first->second;
    }

    // Task 0318: where a subject stands for an alias distance: the player (his position), else a placed actor (its live
    // place when the core tells, else where it is placed) and the cell it is in (0: not told).
    bool SkyrimConditionWorld::aliasSubjectPlace(
        conditions::Subject subject, std::array<double, 3>& place, FormId& cell) const
    {
        if (subject == kPlayer)
        {
            if (!mPlayerPositionKnown)
                return false;
            place = mPlayerPosition;
            cell = mPlayerPlaceKnown && mPlayerInterior ? mPlayerCell : kNullFormId;
            return true;
        }
        ActorView view;
        if (mPlacedRefs == nullptr || !viewOf(subject, view) || view.mRef == nullptr)
            return false;
        cell = view.mRef->mCell;
        if (!mLiveActorPlace || !mLiveActorPlace(view.mRef->mId, place[0], place[1], place[2]))
            place = { static_cast<double>(view.mRef->mX), static_cast<double>(view.mRef->mY),
                static_cast<double>(view.mRef->mZ) };
        return true;
    }

    bool SkyrimConditionWorld::aliasPlace(std::uint32_t alias, std::array<double, 3>& place, FormId& cell) const
    {
        const FormId filled = aliasReference(alias);
        if (filled == kNullFormId)
            return false;
        if (filled == kPlayerReference)
            return aliasSubjectPlace(kPlayer, place, cell);
        const PlacedEntry* other = mPlacedRefs != nullptr ? mPlacedRefs->find(filled) : nullptr;
        if (other == nullptr)
            return false;
        cell = other->mCell;
        place = { static_cast<double>(other->mX), static_cast<double>(other->mY), static_cast<double>(other->mZ) };
        return true;
    }

    bool SkyrimConditionWorld::distanceToAliasKnown(conditions::Subject subject, std::uint32_t alias) const
    {
        std::array<double, 3> from{};
        std::array<double, 3> to{};
        FormId fromCell = kNullFormId;
        FormId toCell = kNullFormId;
        if (!aliasSubjectPlace(subject, from, fromCell) || !aliasPlace(alias, to, toCell))
            return false;
        // task 0276: the player is measured to an alias wherever it is placed (the story manager's world has no cell
        // for him); two placed references must be in one cell, and the player's cell is checked when it is told
        if (subject == kPlayer || fromCell == kNullFormId || toCell == kNullFormId)
            return true;
        return fromCell == toCell;
    }

    float SkyrimConditionWorld::distanceToAlias(conditions::Subject subject, std::uint32_t alias) const
    {
        std::array<double, 3> from{};
        std::array<double, 3> to{};
        FormId fromCell = kNullFormId;
        FormId toCell = kNullFormId;
        if (!distanceToAliasKnown(subject, alias) || !aliasSubjectPlace(subject, from, fromCell)
            || !aliasPlace(alias, to, toCell))
            return std::numeric_limits<float>::max();
        const double dx = from[0] - to[0];
        const double dy = from[1] - to[1];
        const double dz = from[2] - to[2];
        return static_cast<float>(std::sqrt(dx * dx + dy * dy + dz * dz));
    }

    float SkyrimConditionWorld::distanceTo(conditions::Subject subject, std::uint32_t reference) const
    {
        double ax = 0, ay = 0, az = 0, tx = 0, ty = 0, tz = 0;
        if (livePlaces(subject, reference, ax, ay, az, tx, ty, tz)) // task 0277
            return static_cast<float>(std::sqrt((ax - tx) * (ax - tx) + (ay - ty) * (ay - ty) + (az - tz) * (az - tz)));
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
        // task 0324: a target of 0 is the player (the follow and recruit lines are written that way)
        const FormId b = target == 0 ? kPlayerBase : baseOfActorReference(resolve(target));
        const bool aActor = a == kPlayerBase || (a != kNullFormId && mActors.npc(a) != nullptr);
        return aActor && b != kNullFormId;
    }

    bool SkyrimConditionWorld::isPlayerTeammate(conditions::Subject subject) const
    {
        if (mFollowerOverlay == nullptr)
            return false;
        const FormId reference = overlayReferenceOf(subject);
        return reference != kNullFormId && mFollowerOverlay->isPlayerTeammate(reference);
    }

    float SkyrimConditionWorld::relationshipRank(conditions::Subject subject, std::uint32_t target) const
    {
        if (mFollowerOverlay != nullptr)
        {
            // a target of 0 is the player (the follow lines are written that way); a rank a script set wins
            const FormId reference = overlayReferenceOf(subject);
            const FormId other = target == 0 ? kPlayerReference : resolve(target);
            if (reference != kNullFormId && other != kNullFormId)
                if (const std::optional<float> rank = mFollowerOverlay->relationshipRank(reference, other))
                    return *rank;
        }
        ActorView view;
        if (!relationKnown(subject, target))
            return 0.f;
        const FormId a = subject == kPlayer ? kPlayerBase : (viewOf(subject, view) ? view.mBase : kNullFormId);
        const FormId b = target == 0 ? kPlayerBase : baseOfActorReference(resolve(target)); // task 0324: 0 is the player
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
        if (mVendorServices && isSpeaker(subject) && mSpeaker.mBase != kNullFormId && mVendorServices(mSpeaker.mBase))
            return true; // task 0189: a merchant offers its services
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

    void setSelectDebugTopic(FormId topic)
    {
        sSelectDebugTopic = topic;
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
        const bool debug = sSelectDebugTopic != kNullFormId && topicId == sSelectDebugTopic;
        const auto why = [&](const DialogInfo* info, const char* reason) {
            if (debug)
                conditions::conditionDebugReport(
                    "SELECT info " + std::to_string(info->mCommon.mId) + " skipped: " + reason);
        };
        struct DebugAll
        {
            bool mOn;
            explicit DebugAll(bool on)
                : mOn(on)
            {
                if (mOn)
                    conditions::setConditionDebugAll(true);
            }
            ~DebugAll()
            {
                if (mOn)
                    conditions::setConditionDebugAll(false);
            }
        } debugAll(debug);
        if (debug)
            conditions::conditionDebugReport(
                "SELECT topic " + std::to_string(topicId) + ": " + std::to_string(infos.size()) + " info(s)");
        for (const DialogInfo* info : infos)
        {
            ++result.mConsidered;
            const FormId quest = info->mQuest != kNullFormId ? info->mQuest : (topic != nullptr ? topic->mQuest : 0);
            if (options.mQuestRunning && quest != kNullFormId && !options.mQuestRunning(quest))
            {
                why(info, "its quest is not running");
                continue;
            }
            if ((info->mFlags & InfoFlags::SayOnce) != 0 && options.mSaid && options.mSaid(info->mCommon.mId))
            {
                why(info, "said once");
                continue;
            }
            if (info->mSpeaker != kNullFormId && info->mSpeaker != speaker.mBase)
            {
                why(info, "another speaker");
                continue;
            }
            if (!info->mConditionsOk)
            {
                why(info, "damaged conditions");
                continue;
            }
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
            {
                why(info, "its conditions fail");
                continue;
            }
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
