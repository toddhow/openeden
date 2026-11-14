#include "game_skyrimse/package.hpp"

#include "game_skyrimse/pack_core.hpp"

#include <algorithm>
#include <cctype>
#include <cstring>
#include <memory>
#include <set>
#include <sstream>

// Layout status of every subrecord read here: see the header (task 0135 stage S5). VERIFIED on the five installed
// Skyrim SE masters; RECALLED-VERIFY where the header says so. Nothing is copied from xEdit.

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

        std::string lowerAscii(const std::string& text)
        {
            std::string out = text;
            for (char& c : out)
                c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
            return out;
        }

        bool isFormValued(std::int32_t locationType)
        {
            return locationType == PackageLocationType::NearReference || locationType == PackageLocationType::InCell
                || locationType == PackageLocationType::ObjectId
                || locationType == PackageLocationType::NearLinkedReference;
        }

        class PackageParser final : public Content::Visitor
        {
        public:
            PackageParser(const Content::Plugin& plugin, const FormIdResolver& resolver, const PackageLimits& limits,
                PackageRecords& out)
                : mPlugin(plugin)
                , mResolver(resolver)
                , mLimits(limits)
                , mOut(out)
            {
            }

            bool onRecord(const Content::RawRecord& record) override
            {
                if (record.mType != Content::fourCC("PACK"))
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
                        fail(record,
                            "the record inflates to " + std::to_string(record.mUncompressedSize)
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
                    fail(record,
                        "the record is " + std::to_string(record.mDataSize) + " bytes, over the limit of "
                            + std::to_string(mLimits.mMaxRecordBytes));
                    return true;
                }
                mWhy.clear();
                if (readPackage(record, bytes))
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
                ++mOut.mOddBy["PACK." + tag(sub)];
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

            bool readPackage(const Content::RawRecord& record, std::span<const std::byte> bytes)
            {
                PackageRecord p;
                p.mCommon.mStoredId = record.mFormId;
                p.mCommon.mId = resolved(record.mFormId);
                p.mCommon.mFlags = record.mFlags;
                p.mCommon.mOffset = record.mOffset;
                bool seenPkcu = false;
                bool lastInputIsBool = false;
                mLastList = nullptr;

                const bool wellFormed = survey::forEachSubrecord(bytes, [&](const survey::SubrecordView& sub) {
                    if (!mWhy.empty())
                        return;
                    const std::uint32_t type = sub.mType;
                    const std::span<const std::byte> data = sub.mData;
                    if (type == Content::fourCC("CIS1") || type == Content::fourCC("CIS2"))
                    {
                        if (mLastList != nullptr && !mLastList->empty())
                        {
                            std::string text;
                            if (!zstring(type, data, text))
                                return;
                            (type == Content::fourCC("CIS1") ? mLastList->back().mString1 : mLastList->back().mString2)
                                = std::move(text);
                        }
                        return;
                    }
                    switch (type)
                    {
                        case Content::fourCC("EDID"):
                            zstring(type, data, p.mCommon.mEditorId);
                            return;
                        case Content::fourCC("PKDT"):
                            if (data.size() != 12)
                            {
                                odd(type);
                                return;
                            }
                            p.mHasPkdt = true;
                            p.mFlags = readLe<std::uint32_t>(data.data());
                            p.mType = static_cast<std::uint8_t>(data[4]);
                            p.mPreferredSpeed = static_cast<std::uint8_t>(data[6]);
                            p.mInterruptFlags = readLe<std::uint16_t>(data.data() + 8);
                            return;
                        case Content::fourCC("PSDT"):
                            if (data.size() != 12)
                            {
                                odd(type);
                                return;
                            }
                            p.mSchedule.mMonth = static_cast<std::int8_t>(data[0]);
                            p.mSchedule.mDayOfWeek = static_cast<std::int8_t>(data[1]);
                            p.mSchedule.mDate = static_cast<std::uint8_t>(data[2]);
                            p.mSchedule.mHour = static_cast<std::int8_t>(data[3]);
                            p.mSchedule.mMinute = static_cast<std::int8_t>(data[4]);
                            // bytes 5 to 7 hold garbage in the data: never read
                            p.mSchedule.mDurationMinutes = readLe<std::int32_t>(data.data() + 8);
                            return;
                        case Content::fourCC("CTDA"):
                            if (seenPkcu)
                                return; // a branch's condition, not the package's
                            if (p.mConditions.size() >= mLimits.mMaxConditions)
                            {
                                mWhy
                                    = "more than " + std::to_string(mLimits.mMaxConditions) + " conditions in one list";
                                return;
                            }
                            {
                                survey::Ctda c;
                                if (!survey::decodeCtda(data, c))
                                {
                                    mLastList = nullptr;
                                    p.mConditionsOk = false;
                                    return;
                                }
                                p.mConditions.push_back(std::move(c));
                                mLastList = &p.mConditions;
                            }
                            return;
                        case Content::fourCC("PKCU"):
                            seenPkcu = true;
                            mLastList = nullptr;
                            if (data.size() != 12)
                            {
                                odd(type);
                                return;
                            }
                            p.mTemplate = resolved(readLe<std::uint32_t>(data.data() + 4));
                            return;
                        case Content::fourCC("PLDT"):
                            if (data.size() != 12)
                            {
                                odd(type);
                                return;
                            }
                            if (p.mLocations.size() >= mLimits.mMaxLocations)
                            {
                                mWhy = "more than " + std::to_string(mLimits.mMaxLocations) + " locations";
                                return;
                            }
                            {
                                PackageLocation loc;
                                loc.mType = readLe<std::int32_t>(data.data());
                                const std::uint32_t value = readLe<std::uint32_t>(data.data() + 4);
                                loc.mValue = isFormValued(loc.mType) ? resolved(value) : value;
                                loc.mRadius = readLe<std::int32_t>(data.data() + 8);
                                p.mLocations.push_back(loc);
                            }
                            return;
                        case Content::fourCC("PTDA"):
                            if (data.size() != 12)
                            {
                                odd(type);
                                return;
                            }
                            if (p.mTargets.size() < kMaxPackageTargets)
                            {
                                PackageTarget target;
                                target.mType = readLe<std::int32_t>(data.data());
                                const std::uint32_t value = readLe<std::uint32_t>(data.data() + 4);
                                target.mValue = (target.mType == PackageTargetType::SpecificReference
                                                    || target.mType == PackageTargetType::ObjectId
                                                    || target.mType == PackageTargetType::LinkedReference)
                                    ? resolved(value)
                                    : value;
                                target.mCount = readLe<std::int32_t>(data.data() + 8);
                                p.mTargets.push_back(target);
                            }
                            return;
                        case Content::fourCC("ANAM"):
                            // A data input's type name; a Bool input's value is the CNAM that follows (VERIFIED: 1
                            // byte).
                            lastInputIsBool = data.size() == 5 && std::memcmp(data.data(), "Bool", 5) == 0;
                            return;
                        case Content::fourCC("CNAM"):
                            if (lastInputIsBool && data.size() == 1 && p.mBools.size() < kMaxPackageBools)
                                p.mBools.push_back(static_cast<std::uint8_t>(data[0]));
                            lastInputIsBool = false;
                            return;
                        case Content::fourCC("PNAM"):
                            // The alphabetic ones are procedure kinds; the others are padding (VERIFIED).
                            if (data.size() > 1 && std::isalpha(static_cast<unsigned char>(data[0])) != 0)
                            {
                                if (p.mProcedures.size() >= mLimits.mMaxProcedures)
                                {
                                    mWhy = "more than " + std::to_string(mLimits.mMaxProcedures) + " procedures";
                                    return;
                                }
                                std::string name;
                                if (zstring(type, data, name))
                                    p.mProcedures.push_back(std::move(name));
                            }
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
                mOut.mPackages.push_back(std::move(p));
                return true;
            }

            const Content::Plugin& mPlugin;
            const FormIdResolver& mResolver;
            const PackageLimits& mLimits;
            PackageRecords& mOut;
            std::vector<std::byte> mInflated;
            std::string mWhy;
            std::vector<survey::Ctda>* mLastList = nullptr;
        };
    }

    PackageBehavior classifyProcedures(std::span<const std::string> names)
    {
        static const std::set<std::string> standNames{ "patrol", "guard", "holdposition", "wait", "sit", "say",
            "activate", "useidlemarker", "keepaneyeon", "lockdoors", "unlockdoors", "find", "acquire" };
        bool forceStand = false, sleep = false, eat = false, sandbox = false, wander = false, travel = false,
             stand = false, other = false;
        for (const std::string& raw : names)
        {
            const std::string name = lowerAscii(raw);
            if (name == "forcegreet" || name == "dialogueactivate")
                forceStand = true;
            else if (name == "sleep")
                sleep = true;
            else if (name == "eat")
                eat = true;
            else if (name == "sandbox")
                sandbox = true;
            else if (name == "wander")
                wander = true;
            else if (name == "travel")
                travel = true;
            else if (standNames.count(name) != 0)
                stand = true;
            else
                other = true;
        }
        if (forceStand)
            return PackageBehavior::Stand;
        if (sleep)
            return PackageBehavior::Sleep;
        if (eat)
            return PackageBehavior::Eat;
        if (sandbox)
            return PackageBehavior::Sandbox;
        if (wander)
            return PackageBehavior::Wander;
        if (other)
            return PackageBehavior::Unsupported;
        if (stand)
            return PackageBehavior::Stand;
        if (travel)
            return PackageBehavior::Travel;
        return PackageBehavior::Stand;
    }

    PackageActivity classifyActivity(std::span<const std::string> names)
    {
        bool patrol = false, follow = false, escort = false;
        for (const std::string& raw : names)
        {
            const std::string name = lowerAscii(raw);
            if (name == "forcegreet" || name == "dialogueactivate" || name == "sleep" || name == "eat"
                || name == "sandbox" || name == "wander")
                return PackageActivity::None;
            if (name == "patrol")
                patrol = true;
            else if (name == "follow" || name == "followto")
                follow = true;
            else if (name == "escort")
                escort = true;
        }
        if (patrol)
            return PackageActivity::Patrol;
        if (follow)
            return PackageActivity::Follow;
        if (escort)
            return PackageActivity::Escort;
        return PackageActivity::None;
    }

    bool parsePackageRecords(
        const Content::Plugin& plugin, const LoadOrder& order, const PackageLimits& limits, PackageRecords& out)
    {
        out = PackageRecords();
        out.mName = plugin.name();
        if (!isSkyrimSeHeader(plugin.header()))
        {
            out.mWalkError = makeError(plugin.name(), 0, 0, "this plugin is not a Skyrim Special Edition one");
            return false;
        }
        const FormIdResolver resolver(plugin.header(), plugin.name(), order);
        PackageParser parser(plugin, resolver, limits, out);
        const std::optional<Content::Error> error = plugin.walk(parser);
        if (error)
        {
            out.mWalkError = *error;
            return false;
        }
        return true;
    }

    bool parsePackageRecordsBytes(const std::string& bytes, const std::string& name, const LoadOrder& order,
        const PackageLimits& limits, PackageRecords& out, Content::Error& error)
    {
        const std::unique_ptr<Content::Plugin> plugin
            = Content::Plugin::open(std::make_unique<std::istringstream>(bytes, std::ios::binary), name, error);
        if (plugin == nullptr)
            return false;
        if (!parsePackageRecords(*plugin, order, limits, out))
        {
            if (out.mWalkError)
                error = *out.mWalkError;
            return false;
        }
        return true;
    }

    void PackageIndex::add(PackageRecords records, FormIdResolver resolver)
    {
        const std::uint32_t plugin = static_cast<std::uint32_t>(mResolvers.size());
        mResolvers.push_back(std::move(resolver));
        for (PackageRecord& p : records.mPackages)
        {
            if (!usable(p.mCommon.mId))
                continue;
            if (p.mCommon.deleted())
            {
                mPackages.erase(p.mCommon.mId);
                continue;
            }
            p.mPlugin = plugin;
            mPackages[p.mCommon.mId] = std::move(p);
        }
    }

    const PackageRecord* PackageIndex::package(FormId id) const
    {
        const auto it = mPackages.find(id);
        return it == mPackages.end() ? nullptr : &it->second;
    }

    std::vector<FormId> PackageIndex::packageIds() const
    {
        std::vector<FormId> ids;
        ids.reserve(mPackages.size());
        for (const auto& [id, p] : mPackages)
            ids.push_back(id);
        std::sort(ids.begin(), ids.end());
        return ids;
    }

    PackageBehavior PackageIndex::behaviorOf(const PackageRecord& package) const
    {
        const PackageRecord* current = &package;
        std::set<FormId> seen;
        for (int hops = 0; hops < 8; ++hops)
        {
            if (current->mTemplate == kNullFormId)
                return classifyProcedures(current->mProcedures);
            if (!usable(current->mTemplate) || !seen.insert(current->mTemplate).second)
                return PackageBehavior::Stand;
            const PackageRecord* next = this->package(current->mTemplate);
            if (next == nullptr)
                return PackageBehavior::Stand;
            current = next;
        }
        return PackageBehavior::Stand;
    }

    PackageActivity PackageIndex::activityOf(const PackageRecord& package) const
    {
        const PackageRecord* current = &package;
        std::set<FormId> seen;
        for (int depth = 0; depth < 8 && current != nullptr; ++depth)
        {
            if (!current->mProcedures.empty() || current->mTemplate == kNullFormId
                || current->mTemplate == kUnresolvedFormId || !seen.insert(current->mTemplate).second)
                return classifyActivity(current->mProcedures);
            current = this->package(current->mTemplate);
        }
        return PackageActivity::None;
    }

    bool PackageIndex::targetOf(const PackageRecord& package, PackageTarget& out) const
    {
        const PackageRecord* current = &package;
        std::set<FormId> seen;
        for (int depth = 0; depth < 8 && current != nullptr; ++depth)
        {
            if (!current->mTargets.empty())
            {
                out = current->mTargets.front();
                return true;
            }
            if (current->mTemplate == kNullFormId || current->mTemplate == kUnresolvedFormId
                || !seen.insert(current->mTemplate).second)
                return false;
            current = this->package(current->mTemplate);
        }
        return false;
    }
}
