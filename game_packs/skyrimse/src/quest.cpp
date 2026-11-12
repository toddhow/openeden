#include "game_skyrimse/quest.hpp"

#include "game_skyrimse/pack_core.hpp"

#include <algorithm>
#include <cstring>
#include <memory>
#include <sstream>

// Layout status of every subrecord read here: see the header (task 0138). VERIFIED on the five installed Skyrim SE
// masters by tools/skyrimse_quest_check; RECALLED-VERIFY where the header says so. Nothing is copied from xEdit or the
// Creation Kit wiki.

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

        bool usable(FormId id)
        {
            return id != kNullFormId && id != kUnresolvedFormId;
        }

        /// The fill subrecords an alias showed (bits), to pick its kind once the record is read.
        namespace Seen
        {
            constexpr std::uint32_t Alfr = 1u << 0;
            constexpr std::uint32_t Alua = 1u << 1;
            constexpr std::uint32_t Alfl = 1u << 2;
            constexpr std::uint32_t Aleq = 1u << 3;
            constexpr std::uint32_t Created = 1u << 4;
            constexpr std::uint32_t Alfe = 1u << 5;
            constexpr std::uint32_t Alna = 1u << 6;
            constexpr std::uint32_t Alfa = 1u << 7;
        }

        AliasFillKind fillKindOf(std::uint32_t seen)
        {
            if ((seen & Seen::Alfr) != 0)
                return AliasFillKind::SpecificReference;
            if ((seen & Seen::Alua) != 0)
                return AliasFillKind::UniqueActor;
            if ((seen & Seen::Alfl) != 0)
                return AliasFillKind::SpecificLocation;
            if ((seen & Seen::Aleq) != 0)
                return AliasFillKind::ExternalAlias;
            if ((seen & Seen::Created) != 0)
                return AliasFillKind::CreatedObject;
            if ((seen & Seen::Alfe) != 0)
                return AliasFillKind::FromEvent;
            if ((seen & Seen::Alna) != 0)
                return AliasFillKind::NearAlias;
            if ((seen & Seen::Alfa) != 0)
                return AliasFillKind::InLocationAlias;
            return AliasFillKind::FindMatching;
        }

        class QuestParser final : public Content::Visitor
        {
        public:
            QuestParser(const Content::Plugin& plugin, const FormIdResolver& resolver, const QuestLimits& limits,
                QuestRecords& out)
                : mPlugin(plugin)
                , mResolver(resolver)
                , mLimits(limits)
                , mOut(out)
                , mLocalized(plugin.header().isLocalized())
            {
            }

            bool onRecord(const Content::RawRecord& record) override
            {
                if (record.mType != Content::fourCC("QUST"))
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
                        fail(record, "the record inflates to " + std::to_string(record.mUncompressedSize)
                                + " bytes, over the limit of " + std::to_string(mLimits.mMaxRecordBytes));
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
                    fail(record, "the record is " + std::to_string(record.mDataSize) + " bytes, over the limit of "
                            + std::to_string(mLimits.mMaxRecordBytes));
                    return true;
                }
                mWhy.clear();
                if (readQuest(record, bytes))
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
                    mOut.mErrors.push_back(makeError(mPlugin.name(), record.mOffset, record.mType, std::move(message)));
                else
                    ++mOut.mErrorsDropped;
            }

            void odd(std::uint32_t sub)
            {
                ++mOut.mOddSubrecords;
                ++mOut.mOddBy["QUST." + tag(sub)];
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

            /// A u32 field; false (and odd) when the subrecord is another size.
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

            bool formField(std::uint32_t sub, std::span<const std::byte> data, FormId& out)
            {
                std::uint32_t stored = 0;
                if (u32Field(sub, data, stored))
                    out = resolved(stored);
                return true;
            }

            /// `okFlag` is the record's "list not damaged" flag for the head lists, null for the others.
            bool pushCondition(std::vector<survey::Ctda>& list, std::span<const std::byte> data, bool* okFlag)
            {
                if (list.size() >= mLimits.mMaxConditions)
                {
                    mWhy = "more than " + std::to_string(mLimits.mMaxConditions) + " conditions in one list";
                    return false;
                }
                survey::Ctda c;
                if (!survey::decodeCtda(data, c))
                {
                    mLastList = nullptr;
                    if (okFlag != nullptr)
                        *okFlag = false;
                    else
                        odd(Content::fourCC("CTDA"));
                    return true;
                }
                list.push_back(std::move(c));
                mLastList = &list;
                return true;
            }

            bool readQuest(const Content::RawRecord& record, std::span<const std::byte> bytes)
            {
                enum class Section
                {
                    Head,
                    Stage,
                    Objective,
                    Alias,
                    After
                };
                QuestRecord q;
                q.mCommon.mStoredId = record.mFormId;
                q.mCommon.mId = resolved(record.mFormId);
                q.mCommon.mFlags = record.mFlags;
                q.mCommon.mOffset = record.mOffset;
                Section section = Section::Head;
                bool seenNext = false;
                QuestStage* stage = nullptr;
                QuestObjective* objective = nullptr;
                QuestAlias* alias = nullptr;
                std::vector<std::uint32_t> seen; // per alias
                mLastList = nullptr;

                const bool wellFormed = survey::forEachSubrecord(bytes, [&](const survey::SubrecordView& sub) {
                    if (!mWhy.empty())
                        return;
                    const std::uint32_t type = sub.mType;
                    const std::span<const std::byte> data = sub.mData;
                    if (type == Content::fourCC("CIS1") || type == Content::fourCC("CIS2"))
                    {
                        // the string form of parameter 1 or 2 of the CTDA before it
                        if (mLastList != nullptr && !mLastList->empty())
                        {
                            std::string text;
                            if (!zstring(type, data, text))
                                return;
                            (type == Content::fourCC("CIS1") ? mLastList->back().mString1
                                                             : mLastList->back().mString2)
                                = std::move(text);
                        }
                        return;
                    }
                    switch (type)
                    {
                        case Content::fourCC("EDID"):
                            zstring(type, data, q.mCommon.mEditorId);
                            return;
                        case Content::fourCC("FULL"):
                            lstring(type, data, q.mName);
                            return;
                        case Content::fourCC("DNAM"):
                            if (data.size() != 12)
                            {
                                odd(type);
                                return;
                            }
                            q.mHasDnam = true;
                            q.mFlags = readLe<std::uint16_t>(data.data());
                            q.mPriority = static_cast<std::uint8_t>(data[2]);
                            q.mType = readLe<std::uint32_t>(data.data() + 8);
                            return;
                        case Content::fourCC("ENAM"):
                            if (data.size() != 4)
                            {
                                odd(type);
                                return;
                            }
                            q.mEvent.assign(reinterpret_cast<const char*>(data.data()), 4);
                            while (!q.mEvent.empty() && q.mEvent.back() == '\0')
                                q.mEvent.pop_back();
                            return;
                        case Content::fourCC("FLTR"):
                            zstring(type, data, q.mFilter);
                            return;
                        case Content::fourCC("NEXT"):
                            seenNext = true;
                            mLastList = nullptr;
                            return;
                        case Content::fourCC("ANAM"):
                            u32Field(type, data, q.mNextAliasId);
                            return;
                        case Content::fourCC("CTDA"):
                            switch (section)
                            {
                                case Section::Head:
                                    // before NEXT the dialogue conditions, after it the start conditions
                                    if (seenNext)
                                        pushCondition(q.mStartConditions, data, &q.mStartConditionsOk);
                                    else
                                        pushCondition(q.mConditions, data, &q.mConditionsOk);
                                    return;
                                case Section::Stage:
                                    if (stage == nullptr || stage->mEntries.empty())
                                    {
                                        odd(type);
                                        return;
                                    }
                                    pushCondition(stage->mEntries.back().mConditions, data, nullptr);
                                    return;
                                case Section::Objective:
                                    if (objective == nullptr || objective->mTargets.empty())
                                    {
                                        odd(type);
                                        return;
                                    }
                                    pushCondition(objective->mTargets.back().mConditions, data, nullptr);
                                    return;
                                case Section::Alias:
                                    if (alias == nullptr)
                                    {
                                        odd(type);
                                        return;
                                    }
                                    pushCondition(alias->mConditions, data, nullptr);
                                    return;
                                default:
                                    odd(type);
                                    return;
                            }
                        case Content::fourCC("INDX"):
                            section = Section::Stage;
                            objective = nullptr;
                            alias = nullptr;
                            mLastList = nullptr;
                            if (data.size() != 4)
                            {
                                odd(type);
                                stage = nullptr;
                                return;
                            }
                            if (q.mStages.size() >= mLimits.mMaxStages)
                            {
                                mWhy = "more than " + std::to_string(mLimits.mMaxStages) + " stages";
                                return;
                            }
                            q.mStages.emplace_back();
                            stage = &q.mStages.back();
                            stage->mIndex = readLe<std::uint16_t>(data.data());
                            stage->mFlags = static_cast<std::uint8_t>(data[2]); // byte 3 is garbage in the data
                            return;
                        case Content::fourCC("QSDT"):
                            if (section != Section::Stage || stage == nullptr || data.size() != 1)
                            {
                                odd(type);
                                return;
                            }
                            if (stage->mEntries.size() >= mLimits.mMaxEntriesPerStage)
                            {
                                mWhy = "more than " + std::to_string(mLimits.mMaxEntriesPerStage)
                                    + " log entries in one stage";
                                return;
                            }
                            stage->mEntries.emplace_back();
                            stage->mEntries.back().mFlags = static_cast<std::uint8_t>(data[0]);
                            mLastList = nullptr;
                            return;
                        case Content::fourCC("CNAM"):
                            if (section == Section::Stage && stage != nullptr && !stage->mEntries.empty())
                                lstring(type, data, stage->mEntries.back().mText);
                            return;
                        case Content::fourCC("NAM0"):
                            if (section == Section::Stage && stage != nullptr && !stage->mEntries.empty())
                                formField(type, data, stage->mEntries.back().mNextQuest);
                            return;
                        case Content::fourCC("QOBJ"):
                            section = Section::Objective;
                            stage = nullptr;
                            alias = nullptr;
                            mLastList = nullptr;
                            if (data.size() != 2)
                            {
                                odd(type);
                                objective = nullptr;
                                return;
                            }
                            if (q.mObjectives.size() >= mLimits.mMaxObjectives)
                            {
                                mWhy = "more than " + std::to_string(mLimits.mMaxObjectives) + " objectives";
                                return;
                            }
                            q.mObjectives.emplace_back();
                            objective = &q.mObjectives.back();
                            objective->mIndex = readLe<std::uint16_t>(data.data());
                            return;
                        case Content::fourCC("NNAM"):
                            if (section == Section::Objective && objective != nullptr)
                                lstring(type, data, objective->mText);
                            return;
                        case Content::fourCC("QSTA"):
                            if (section != Section::Objective || objective == nullptr || data.size() != 8)
                            {
                                odd(type);
                                return;
                            }
                            objective->mTargets.emplace_back();
                            objective->mTargets.back().mAlias = readLe<std::uint32_t>(data.data());
                            objective->mTargets.back().mFlags = readLe<std::uint32_t>(data.data() + 4);
                            mLastList = nullptr;
                            return;
                        case Content::fourCC("ALST"):
                        case Content::fourCC("ALLS"):
                            section = Section::Alias;
                            stage = nullptr;
                            objective = nullptr;
                            mLastList = nullptr;
                            if (data.size() != 4)
                            {
                                odd(type);
                                alias = nullptr;
                                return;
                            }
                            if (q.mAliases.size() >= mLimits.mMaxAliases)
                            {
                                mWhy = "more than " + std::to_string(mLimits.mMaxAliases) + " aliases";
                                return;
                            }
                            q.mAliases.emplace_back();
                            alias = &q.mAliases.back();
                            alias->mId = readLe<std::uint32_t>(data.data());
                            alias->mIsLocation = type == Content::fourCC("ALLS");
                            seen.push_back(0);
                            return;
                        case Content::fourCC("ALED"):
                            section = Section::After;
                            alias = nullptr;
                            mLastList = nullptr;
                            return;
                        default:
                            break;
                    }
                    // the rest are alias subrecords
                    const bool aliasTag = type == Content::fourCC("ALID") || type == Content::fourCC("FNAM")
                        || type == Content::fourCC("ALFR") || type == Content::fourCC("ALUA")
                        || type == Content::fourCC("ALFL") || type == Content::fourCC("ALFA")
                        || type == Content::fourCC("ALFI") || type == Content::fourCC("ALEQ")
                        || type == Content::fourCC("ALEA") || type == Content::fourCC("ALNA")
                        || type == Content::fourCC("ALRT") || type == Content::fourCC("ALCO")
                        || type == Content::fourCC("ALCA") || type == Content::fourCC("ALCL")
                        || type == Content::fourCC("ALFE");
                    if (type == Content::fourCC("FNAM") && section == Section::Objective && objective != nullptr)
                    {
                        std::uint32_t flags = 0;
                        if (u32Field(type, data, flags))
                            objective->mFlags = flags;
                        return;
                    }
                    if (!aliasTag)
                        return; // VMAD, KSIZ, NEXT, SCHR, and the alias subrecords not used here
                    if (section != Section::Alias || alias == nullptr)
                    {
                        if (type != Content::fourCC("FNAM"))
                            odd(type);
                        return;
                    }
                    std::uint32_t& bits = seen.back();
                    std::uint32_t value = 0;
                    switch (type)
                    {
                        case Content::fourCC("ALID"):
                            zstring(type, data, alias->mName);
                            return;
                        case Content::fourCC("FNAM"):
                            u32Field(type, data, alias->mFlags);
                            return;
                        case Content::fourCC("ALFR"):
                            if (formField(type, data, alias->mReference))
                                bits |= Seen::Alfr;
                            return;
                        case Content::fourCC("ALUA"):
                            if (formField(type, data, alias->mActor))
                                bits |= Seen::Alua;
                            return;
                        case Content::fourCC("ALFL"):
                            if (formField(type, data, alias->mLocation))
                                bits |= Seen::Alfl;
                            return;
                        case Content::fourCC("ALEQ"):
                            formField(type, data, alias->mExternalQuest);
                            bits |= Seen::Aleq;
                            return;
                        case Content::fourCC("ALEA"):
                            if (u32Field(type, data, value) && (bits & (Seen::Alfa | Seen::Alna)) == 0)
                                alias->mOtherAlias = value;
                            return;
                        case Content::fourCC("ALFA"):
                            if (u32Field(type, data, value))
                            {
                                alias->mOtherAlias = value;
                                bits |= Seen::Alfa;
                            }
                            return;
                        case Content::fourCC("ALNA"):
                            if (u32Field(type, data, value))
                            {
                                alias->mOtherAlias = value;
                                bits |= Seen::Alna;
                            }
                            return;
                        case Content::fourCC("ALFI"): // an extra: also put the reference into this alias
                            u32Field(type, data, alias->mForceIntoAlias);
                            return;
                        case Content::fourCC("ALRT"):
                            formField(type, data, alias->mReferenceType);
                            return;
                        case Content::fourCC("ALCO"):
                        case Content::fourCC("ALCA"):
                        case Content::fourCC("ALCL"):
                            bits |= Seen::Created;
                            return;
                        case Content::fourCC("ALFE"):
                            bits |= Seen::Alfe;
                            return;
                        default:
                            return;
                    }
                });
                if (!mWhy.empty())
                    return false;
                if (!wellFormed)
                {
                    mWhy = "a subrecord is cut short or claims more bytes than the record holds";
                    return false;
                }
                for (std::size_t i = 0; i < q.mAliases.size(); ++i)
                    q.mAliases[i].mFill = fillKindOf(seen[i]);
                mOut.mQuests.push_back(std::move(q));
                return true;
            }

            const Content::Plugin& mPlugin;
            const FormIdResolver& mResolver;
            const QuestLimits& mLimits;
            QuestRecords& mOut;
            const bool mLocalized;
            std::vector<std::byte> mInflated;
            std::string mWhy;
            std::vector<survey::Ctda>* mLastList = nullptr;
        };
    }

    bool QuestStage::completesQuest() const
    {
        return std::any_of(mEntries.begin(), mEntries.end(),
            [](const QuestLogEntry& e) { return (e.mFlags & LogEntryFlags::CompleteQuest) != 0; });
    }

    bool QuestStage::failsQuest() const
    {
        return std::any_of(mEntries.begin(), mEntries.end(),
            [](const QuestLogEntry& e) { return (e.mFlags & LogEntryFlags::FailQuest) != 0; });
    }

    const QuestStage* QuestRecord::stage(std::uint16_t index) const
    {
        for (const QuestStage& s : mStages)
            if (s.mIndex == index)
                return &s;
        return nullptr;
    }

    const QuestStage* QuestRecord::startUpStage() const
    {
        const QuestStage* best = nullptr;
        for (const QuestStage& s : mStages)
            if (s.startUp() && (best == nullptr || s.mIndex < best->mIndex))
                best = &s;
        return best;
    }

    const QuestAlias* QuestRecord::alias(std::uint32_t id) const
    {
        for (const QuestAlias& a : mAliases)
            if (a.mId == id)
                return &a;
        return nullptr;
    }

    bool parseQuestRecords(const Content::Plugin& plugin, const LoadOrder& order, const QuestLimits& limits,
        QuestRecords& out)
    {
        out = QuestRecords();
        out.mName = plugin.name();
        if (!isSkyrimSeHeader(plugin.header()))
        {
            out.mWalkError = makeError(plugin.name(), 0, 0, "this plugin is not a Skyrim Special Edition one");
            return false;
        }
        const FormIdResolver resolver(plugin.header(), plugin.name(), order);
        QuestParser parser(plugin, resolver, limits, out);
        const std::optional<Content::Error> error = plugin.walk(parser);
        if (error)
        {
            out.mWalkError = *error;
            return false;
        }
        return true;
    }

    bool parseQuestRecordsBytes(const std::string& bytes, const std::string& name, const LoadOrder& order,
        const QuestLimits& limits, QuestRecords& out, Content::Error& error)
    {
        const std::unique_ptr<Content::Plugin> plugin
            = Content::Plugin::open(std::make_unique<std::istringstream>(bytes, std::ios::binary), name, error);
        if (plugin == nullptr)
            return false;
        if (!parseQuestRecords(*plugin, order, limits, out))
        {
            if (out.mWalkError)
                error = *out.mWalkError;
            return false;
        }
        return true;
    }

    // ---- QuestIndex ----------------------------------------------------------------------------------------------

    void QuestIndex::add(QuestRecords records, FormIdResolver resolver)
    {
        const std::uint32_t plugin = static_cast<std::uint32_t>(mResolvers.size());
        mResolvers.push_back(std::move(resolver));
        for (QuestRecord& q : records.mQuests)
        {
            if (!usable(q.mCommon.mId))
                continue;
            if (q.mCommon.deleted())
            {
                mQuests.erase(q.mCommon.mId);
                continue;
            }
            q.mPlugin = plugin;
            mQuests[q.mCommon.mId] = std::move(q);
        }
    }

    const QuestRecord* QuestIndex::quest(FormId id) const
    {
        const auto it = mQuests.find(id);
        return it == mQuests.end() ? nullptr : &it->second;
    }

    std::vector<FormId> QuestIndex::questIds() const
    {
        std::vector<FormId> ids;
        ids.reserve(mQuests.size());
        for (const auto& [id, q] : mQuests)
            ids.push_back(id);
        std::sort(ids.begin(), ids.end());
        return ids;
    }

    // ---- QuestState ----------------------------------------------------------------------------------------------

    QuestState QuestState::initial(const QuestIndex& index, InitialStateReport* report)
    {
        QuestState state;
        InitialStateReport local;
        InitialStateReport& r = report != nullptr ? *report : local;
        r = InitialStateReport();
        for (FormId id : index.questIds())
        {
            const QuestRecord& q = *index.quest(id);
            Entry e;
            ++r.mQuests;
            if ((q.mFlags & QuestFlags::Completed) != 0)
            {
                e.mCompleted = true;
                ++r.mAlreadyCompleted;
            }
            if (q.startsEnabled())
            {
                ++r.mStartEnabled;
                e.mRunning = true;
                if (const QuestStage* start = q.startUpStage())
                {
                    ++r.mStartEnabledWithStartUpStage;
                    e.mStage = start->mIndex;
                    e.mDone.insert(start->mIndex);
                }
            }
            for (const QuestAlias& a : q.mAliases)
            {
                ++r.mAliases;
                ++r.mByFill[a.mFill];
                if (a.mIsLocation)
                    ++r.mLocationAliases;
                else
                    ++r.mReferenceAliases;
                const bool fillable = (a.mFill == AliasFillKind::SpecificReference && usable(a.mReference))
                    || (a.mFill == AliasFillKind::UniqueActor && usable(a.mActor));
                if (!fillable)
                {
                    ++r.mAliasesConditional;
                    continue;
                }
                ++r.mAliasesDataFillable;
                AliasFill fill;
                fill.mKnown = true;
                if (a.mFill == AliasFillKind::SpecificReference)
                    fill.mReference = a.mReference;
                else
                    fill.mActor = a.mActor;
                e.mAliases[a.mId] = fill;
            }
            state.mQuests[id] = std::move(e);
        }
        return state;
    }

    bool QuestState::exists(FormId quest) const
    {
        return mQuests.find(quest) != mQuests.end();
    }

    bool QuestState::running(FormId quest) const
    {
        const auto it = mQuests.find(quest);
        return it != mQuests.end() && it->second.mRunning;
    }

    bool QuestState::completed(FormId quest) const
    {
        const auto it = mQuests.find(quest);
        return it != mQuests.end() && it->second.mCompleted;
    }

    float QuestState::stage(FormId quest) const
    {
        const auto it = mQuests.find(quest);
        return it == mQuests.end() ? 0.f : static_cast<float>(it->second.mStage);
    }

    bool QuestState::stageDone(FormId quest, std::uint32_t stage) const
    {
        const auto it = mQuests.find(quest);
        return it != mQuests.end() && it->second.mDone.count(stage) != 0;
    }

    AliasFill QuestState::aliasFill(FormId quest, std::uint32_t alias) const
    {
        const auto it = mQuests.find(quest);
        if (it == mQuests.end())
            return AliasFill();
        const auto a = it->second.mAliases.find(alias);
        return a == it->second.mAliases.end() ? AliasFill() : a->second;
    }

    void QuestState::start(FormId quest)
    {
        const auto it = mQuests.find(quest);
        if (it != mQuests.end())
            it->second.mRunning = true;
    }

    void QuestState::stop(FormId quest)
    {
        const auto it = mQuests.find(quest);
        if (it != mQuests.end())
            it->second.mRunning = false;
    }

    void QuestState::setStage(const QuestIndex& index, FormId quest, std::uint32_t stage)
    {
        const auto it = mQuests.find(quest);
        if (it == mQuests.end())
            return;
        Entry& e = it->second;
        e.mRunning = true;
        e.mStage = stage;
        e.mDone.insert(stage);
        const QuestRecord* record = index.quest(quest);
        const QuestStage* s = record != nullptr && stage <= 0xFFFF ? record->stage(static_cast<std::uint16_t>(stage)) : nullptr;
        if (s == nullptr)
            return;
        if (s->completesQuest())
            e.mCompleted = true;
        if (s->shutDown())
            e.mRunning = false;
    }

    void QuestState::setAliasFill(FormId quest, std::uint32_t alias, const AliasFill& fill)
    {
        const auto it = mQuests.find(quest);
        if (it != mQuests.end())
            it->second.mAliases[alias] = fill;
    }
}
