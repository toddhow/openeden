#include "game_skyrimse/placed_refs.hpp"

#include "game_skyrimse/pack_core.hpp"
#include "game_skyrimse/survey.hpp"

#include <algorithm>
#include <cstring>

// Layouts: see the header (task 0160).

namespace game_skyrimse
{
    namespace
    {
        std::uint32_t le32(const std::byte* p)
        {
            std::uint32_t v;
            std::memcpy(&v, p, 4);
            return v;
        }

        float lef(const std::byte* p)
        {
            float v;
            std::memcpy(&v, p, 4);
            return v;
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

        class PlacedParser final : public Content::Visitor
        {
        public:
            PlacedParser(const Content::Plugin& plugin, const FormIdResolver& resolver, const PlacedRefLimits& limits,
                PlacedRefRecords& out)
                : mPlugin(plugin)
                , mResolver(resolver)
                , mLimits(limits)
                , mOut(out)
            {
            }

            bool onGroupBegin(const Content::GroupInfo& group) override
            {
                if (group.mType == 6) // cell children: the label is the cell's stored id
                    mCell = resolved(group.mLabel);
                return true;
            }

            bool onGroupEnd(const Content::GroupInfo& group) override
            {
                if (group.mType == 6)
                    mCell = kNullFormId;
                return true;
            }

            bool onRecord(const Content::RawRecord& record) override
            {
                const std::uint32_t type = record.mType;
                if (type != Content::fourCC("REFR") && type != Content::fourCC("ACHR")
                    && type != Content::fourCC("CONT") && type != Content::fourCC("RELA"))
                    return true;
                const bool deleted = (record.mFlags & RecordFlags::Deleted) != 0;
                if (type == Content::fourCC("REFR") || type == Content::fourCC("ACHR"))
                    if (mOut.mRefs.size() >= mLimits.mMaxRefs)
                    {
                        fail(record, "too many placed references");
                        return true;
                    }
                std::span<const std::byte> bytes = record.mData;
                if (!deleted)
                {
                    if (record.isCompressed())
                    {
                        if (record.mUncompressedSize > mLimits.mMaxRecordBytes)
                        {
                            fail(record, "the record inflates past the limit");
                            return true;
                        }
                        Content::Error error;
                        if (!inflateRecord(record, mPlugin.name(), mInflated, error))
                        {
                            addError(std::move(error));
                            return true;
                        }
                        bytes = mInflated;
                    }
                    else if (record.mDataSize > mLimits.mMaxRecordBytes)
                    {
                        fail(record, "the record is over the size limit");
                        return true;
                    }
                }
                else
                    bytes = {};
                const FormId id = resolved(record.mFormId);
                if (id == kNullFormId || id == kUnresolvedFormId)
                    return true;

                if (type == Content::fourCC("REFR") || type == Content::fourCC("ACHR"))
                {
                    PlacedEntry ref;
                    ref.mId = id;
                    ref.mCell = mCell;
                    ref.mDeleted = deleted;
                    if (type == Content::fourCC("ACHR"))
                        ref.mFlags |= PlacedFlags::IsActor;
                    if ((record.mFlags & RecordFlags::Persistent) != 0)
                        ref.mFlags |= PlacedFlags::Persistent;
                    if ((record.mFlags & RecordFlags::InitiallyDisabled) != 0)
                        ref.mFlags |= PlacedFlags::Disabled;
                    if (type == Content::fourCC("ACHR") && (record.mFlags & kStartsDeadFlag) != 0)
                        ref.mFlags |= PlacedFlags::Dead;
                    if (!deleted)
                    {
                        const bool ok = survey::forEachSubrecord(bytes, [&](const survey::SubrecordView& sub) {
                            switch (sub.mType)
                            {
                                case Content::fourCC("NAME"):
                                    if (sub.mData.size() != 4)
                                        ++mOut.mOddSubrecords;
                                    else
                                        ref.mBase = resolved(le32(sub.mData.data()));
                                    break;
                                case Content::fourCC("DATA"):
                                    if (sub.mData.size() != 24)
                                        ++mOut.mOddSubrecords;
                                    else
                                    {
                                        ref.mX = lef(sub.mData.data());
                                        ref.mY = lef(sub.mData.data() + 4);
                                        ref.mZ = lef(sub.mData.data() + 8);
                                    }
                                    break;
                                case Content::fourCC("XESP"):
                                    if (sub.mData.size() != 8)
                                        ++mOut.mOddSubrecords;
                                    else
                                    {
                                        ref.mEnableParent = resolved(le32(sub.mData.data()));
                                        if ((le32(sub.mData.data() + 4) & 1u) != 0)
                                            ref.mFlags |= PlacedFlags::EnableOpposite;
                                    }
                                    break;
                                default:
                                    break;
                            }
                        });
                        if (!ok)
                        {
                            fail(record, "a subrecord is cut short or claims more bytes than the record holds");
                            return true;
                        }
                    }
                    mOut.mRefs.push_back(ref);
                    return true;
                }
                if (type == Content::fourCC("CONT"))
                {
                    ContainerRecord cont;
                    cont.mId = id;
                    cont.mDeleted = deleted;
                    bool tooMany = false;
                    if (!deleted)
                    {
                        const bool ok = survey::forEachSubrecord(bytes, [&](const survey::SubrecordView& sub) {
                            if (sub.mType != Content::fourCC("CNTO"))
                                return;
                            if (sub.mData.size() != 8)
                            {
                                ++mOut.mOddSubrecords;
                                return;
                            }
                            if (cont.mItems.size() >= mLimits.mMaxItems)
                            {
                                tooMany = true;
                                return;
                            }
                            InventoryEntry e;
                            e.mItem = resolved(le32(sub.mData.data()));
                            e.mCount = static_cast<std::int32_t>(le32(sub.mData.data() + 4));
                            cont.mItems.push_back(e);
                        });
                        if (!ok || tooMany)
                        {
                            fail(record, tooMany ? "too many items" : "a subrecord is cut short");
                            return true;
                        }
                    }
                    mOut.mContainers.push_back(std::move(cont));
                    return true;
                }
                // RELA
                RelationshipRecord rela;
                rela.mId = id;
                rela.mDeleted = deleted;
                if (!deleted)
                {
                    const bool ok = survey::forEachSubrecord(bytes, [&](const survey::SubrecordView& sub) {
                        if (sub.mType != Content::fourCC("DATA"))
                            return;
                        if (sub.mData.size() != 16)
                        {
                            ++mOut.mOddSubrecords;
                            return;
                        }
                        rela.mParent = resolved(le32(sub.mData.data()));
                        rela.mChild = resolved(le32(sub.mData.data() + 4));
                        rela.mRank = static_cast<std::uint8_t>(sub.mData[8]);
                        rela.mAssociation = resolved(le32(sub.mData.data() + 12));
                    });
                    if (!ok)
                    {
                        fail(record, "a subrecord is cut short");
                        return true;
                    }
                }
                mOut.mRelations.push_back(rela);
                return true;
            }

            /// ACHR record flag bit 9, "starts dead" (VERIFIED by data, see the header).
            static constexpr std::uint32_t kStartsDeadFlag = 0x200;

        private:
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

            void addError(Content::Error error)
            {
                ++mOut.mFailed;
                if (mOut.mErrors.size() < mLimits.mMaxErrors)
                    mOut.mErrors.push_back(std::move(error));
            }

            void fail(const Content::RawRecord& record, std::string message)
            {
                addError(makeError(mPlugin.name(), record.mOffset, record.mType, std::move(message)));
            }

            const Content::Plugin& mPlugin;
            const FormIdResolver& mResolver;
            const PlacedRefLimits& mLimits;
            PlacedRefRecords& mOut;
            FormId mCell = kNullFormId;
            std::vector<std::byte> mInflated;
        };
    }

    bool parsePlacedRefs(
        const Content::Plugin& plugin, const LoadOrder& order, const PlacedRefLimits& limits, PlacedRefRecords& out)
    {
        out = PlacedRefRecords();
        out.mName = plugin.name();
        if (!isSkyrimSeHeader(plugin.header()))
        {
            out.mWalkError = makeError(plugin.name(), 0, 0, "this plugin is not a Skyrim Special Edition one");
            return false;
        }
        const FormIdResolver resolver(plugin.header(), plugin.name(), order);
        PlacedParser parser(plugin, resolver, limits, out);
        const std::optional<Content::Error> error = plugin.walk(parser);
        if (error)
        {
            out.mWalkError = *error;
            return false;
        }
        return true;
    }

    float relationshipRankValue(std::uint8_t storedRank)
    {
        if (storedRank > 8)
            return 0.f;
        return static_cast<float>(4 - static_cast<int>(storedRank));
    }

    void PlacedRefIndex::add(PlacedRefRecords&& records)
    {
        mFinished = false;
        mRefs.insert(mRefs.end(), records.mRefs.begin(), records.mRefs.end());
        for (RelationshipRecord& r : records.mRelations)
            mRelations.push_back(r);
        for (ContainerRecord& c : records.mContainers)
        {
            if (c.mDeleted)
                mContainers.erase(c.mId);
            else
                mContainers[c.mId] = std::move(c);
        }
        records.mRefs.clear();
        records.mRefs.shrink_to_fit();
    }

    void PlacedRefIndex::finish()
    {
        // later records of one id replace earlier ones: stable sort keeps the load order inside an id
        std::stable_sort(
            mRefs.begin(), mRefs.end(), [](const PlacedEntry& a, const PlacedEntry& b) { return a.mId < b.mId; });
        std::size_t out = 0;
        for (std::size_t i = 0; i < mRefs.size();)
        {
            std::size_t j = i;
            PlacedEntry merged = mRefs[i];
            bool deleted = merged.mDeleted;
            for (++j; j < mRefs.size() && mRefs[j].mId == merged.mId; ++j)
            {
                const PlacedEntry& later = mRefs[j];
                if (later.mDeleted)
                {
                    deleted = true;
                    continue;
                }
                deleted = false;
                const FormId base = merged.mBase;
                const FormId cell = merged.mCell;
                merged = later;
                if (merged.mBase == kNullFormId)
                    merged.mBase = base;
                if (merged.mCell == kNullFormId)
                    merged.mCell = cell;
            }
            merged.mDeleted = deleted;
            if (!deleted)
                mRefs[out++] = merged;
            i = j;
        }
        mRefs.resize(out);
        mRefs.shrink_to_fit();

        mByBase.clear();
        mByBase.reserve(mRefs.size());
        for (const PlacedEntry& r : mRefs)
            if (r.mBase != kNullFormId && r.mBase != kUnresolvedFormId)
                mByBase.push_back(&r);
        std::stable_sort(mByBase.begin(), mByBase.end(), [](const PlacedEntry* a, const PlacedEntry* b) {
            return a->mBase != b->mBase ? a->mBase < b->mBase : a->mId < b->mId;
        });

        // relations: a later record of one id replaces the earlier
        std::stable_sort(mRelations.begin(), mRelations.end(),
            [](const RelationshipRecord& a, const RelationshipRecord& b) { return a.mId < b.mId; });
        std::vector<RelationshipRecord> merged;
        for (const RelationshipRecord& r : mRelations)
        {
            if (!merged.empty() && merged.back().mId == r.mId)
                merged.pop_back();
            if (!r.mDeleted)
                merged.push_back(r);
        }
        mRelations = std::move(merged);
        mFinished = true;
    }

    const PlacedEntry* PlacedRefIndex::find(FormId reference) const
    {
        const auto it = std::lower_bound(
            mRefs.begin(), mRefs.end(), reference, [](const PlacedEntry& r, FormId id) { return r.mId < id; });
        return it != mRefs.end() && it->mId == reference ? &*it : nullptr;
    }

    PlacedRefIndex::Range PlacedRefIndex::byBase(FormId base) const
    {
        const auto lo = std::lower_bound(
            mByBase.begin(), mByBase.end(), base, [](const PlacedEntry* r, FormId id) { return r->mBase < id; });
        auto hi = lo;
        while (hi != mByBase.end() && (*hi)->mBase == base)
            ++hi;
        Range range;
        if (lo != hi)
        {
            range.mBegin = &*lo;
            range.mEnd = &*lo + (hi - lo);
        }
        return range;
    }

    const ContainerRecord* PlacedRefIndex::container(FormId base) const
    {
        const auto it = mContainers.find(base);
        return it == mContainers.end() ? nullptr : &it->second;
    }

    std::optional<std::uint8_t> PlacedRefIndex::relationRank(FormId a, FormId b) const
    {
        std::optional<std::uint8_t> reverse;
        for (const RelationshipRecord& r : mRelations)
        {
            if (r.mParent == a && r.mChild == b)
                return r.mRank;
            if (r.mParent == b && r.mChild == a && !reverse)
                reverse = r.mRank;
        }
        return reverse;
    }

    bool PlacedRefIndex::initiallyDisabled(const PlacedEntry& ref) const
    {
        const PlacedEntry* current = &ref;
        for (int depth = 0; depth < 4; ++depth)
        {
            if (current->mEnableParent == kNullFormId)
            {
                bool state = (current->mFlags & PlacedFlags::Disabled) != 0;
                // fold the chain back down: every child follows its parent, or is the opposite of it
                const PlacedEntry* walk = &ref;
                std::vector<bool> opposite;
                while (walk != current)
                {
                    opposite.push_back((walk->mFlags & PlacedFlags::EnableOpposite) != 0);
                    walk = find(walk->mEnableParent);
                }
                for (auto it = opposite.rbegin(); it != opposite.rend(); ++it)
                    state = *it ? !state : state;
                return state;
            }
            const PlacedEntry* parent = find(current->mEnableParent);
            if (parent == nullptr)
                break;
            current = parent;
        }
        // the chain ends at the player or an unknown parent (or is too deep): that end counts as enabled
        std::vector<bool> opposite;
        const PlacedEntry* walk = &ref;
        for (int i = 0; i < 5 && walk != nullptr; ++i)
        {
            opposite.push_back((walk->mFlags & PlacedFlags::EnableOpposite) != 0);
            const PlacedEntry* next = walk->mEnableParent == kNullFormId ? nullptr : find(walk->mEnableParent);
            if (next == nullptr)
                break;
            walk = next;
        }
        bool state = false;
        for (auto it = opposite.rbegin(); it != opposite.rend(); ++it)
            state = *it ? !state : state;
        return state;
    }

    std::uint64_t PlacedRefIndex::memoryBytes() const
    {
        std::uint64_t bytes = static_cast<std::uint64_t>(mRefs.capacity()) * sizeof(PlacedEntry)
            + static_cast<std::uint64_t>(mByBase.capacity()) * sizeof(const PlacedEntry*)
            + static_cast<std::uint64_t>(mRelations.capacity()) * sizeof(RelationshipRecord);
        for (const auto& [id, c] : mContainers)
            bytes += sizeof(ContainerRecord) + static_cast<std::uint64_t>(c.mItems.capacity()) * sizeof(InventoryEntry);
        return bytes;
    }
}
