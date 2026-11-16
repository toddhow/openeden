#include "game_skyrimse/inventory.hpp"

#include "game_skyrimse/pack_core.hpp"
#include "game_skyrimse/survey.hpp"

#include <algorithm>
#include <cstring>

// Layouts and rules: see the header (task 0182).

namespace game_skyrimse::inventory
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

        std::uint64_t splitmix(std::uint64_t& state)
        {
            state += 0x9E3779B97F4A7C15ull;
            std::uint64_t z = state;
            z = (z ^ (z >> 30)) * 0xBF58476D1CE4E5B9ull;
            z = (z ^ (z >> 27)) * 0x94D049BB133111EBull;
            return z ^ (z >> 31);
        }

        constexpr std::size_t kMaxItemEffects = 16; // task 0184: effects read from one ALCH
        constexpr std::size_t kMaxItemKeywords = 64; // task 0189: keywords read from one item

        class Parser final : public Content::Visitor
        {
        public:
            Parser(const Content::Plugin& plugin, const FormIdResolver& resolver, const InventoryLimits& limits,
                InventoryRecords& out)
                : mPlugin(plugin)
                , mResolver(resolver)
                , mLimits(limits)
                , mOut(out)
            {
            }

            bool onRecord(const Content::RawRecord& record) override
            {
                const std::uint32_t type = record.mType;
                const bool isCont = type == Content::fourCC("CONT");
                const std::optional<Category> category = categoryOfType(type);
                if (!isCont && !category)
                    return true;
                const bool deleted = (record.mFlags & RecordFlags::Deleted) != 0;
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

                if (isCont)
                {
                    ContainerStat cont;
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

                if (deleted)
                {
                    mOut.mRemoved.push_back({ id });
                    return true;
                }
                ItemStat item;
                item.mId = id;
                item.mCategory = *category;
                bool haveData = false;
                bool effectFilled = true;
                std::optional<std::int32_t> enitValue;
                const bool ok = survey::forEachSubrecord(bytes, [&](const survey::SubrecordView& sub) {
                    if (sub.mType == Content::fourCC("DATA"))
                        haveData = decodeData(type, sub.mData, item);
                    else if (sub.mType == Content::fourCC("ENIT") && type == Content::fourCC("ALCH"))
                    {
                        if (sub.mData.size() == 20)
                            enitValue = static_cast<std::int32_t>(le32(sub.mData.data()));
                        else
                            ++mOut.mOddSubrecords;
                    }
                    else if (sub.mType == Content::fourCC("EFID") && type == Content::fourCC("ALCH"))
                    {
                        // Task 0184: one effect per EFID, its numbers follow in the EFIT.
                        if (sub.mData.size() != 4)
                            ++mOut.mOddSubrecords;
                        else if (item.mEffects.size() < kMaxItemEffects)
                        {
                            ItemEffect effect;
                            effect.mEffect = resolved(le32(sub.mData.data()));
                            item.mEffects.push_back(effect);
                            effectFilled = false;
                        }
                    }
                    else if (sub.mType == Content::fourCC("KWDA"))
                    {
                        // Task 0189: the keywords the vendor lists name. A list of 4 byte ids; at most 64 are kept.
                        if (sub.mData.size() % 4 != 0)
                            ++mOut.mOddSubrecords;
                        else
                            for (std::size_t i = 0;
                                 i + 4 <= sub.mData.size() && item.mKeywords.size() < kMaxItemKeywords; i += 4)
                                item.mKeywords.push_back(resolved(le32(sub.mData.data() + i)));
                    }
                    else if (sub.mType == Content::fourCC("EFIT") && type == Content::fourCC("ALCH"))
                    {
                        if (sub.mData.size() != 12 || item.mEffects.empty() || effectFilled)
                            ++mOut.mOddSubrecords;
                        else
                        {
                            ItemEffect& effect = item.mEffects.back();
                            effect.mMagnitude = lef(sub.mData.data());
                            effect.mArea = le32(sub.mData.data() + 4);
                            effect.mDuration = le32(sub.mData.data() + 8);
                            effectFilled = true;
                        }
                    }
                });
                if (!ok)
                {
                    fail(record, "a subrecord is cut short or claims more bytes than the record holds");
                    return true;
                }
                if (type == Content::fourCC("ALCH"))
                {
                    if (enitValue)
                        item.mValue = *enitValue;
                    item.mDecoded = haveData && enitValue.has_value();
                }
                else
                    item.mDecoded = haveData;
                mOut.mItems.push_back(item);
                return true;
            }

        private:
            bool decodeData(std::uint32_t type, std::span<const std::byte> data, ItemStat& item)
            {
                const auto odd = [&] {
                    ++mOut.mOddSubrecords;
                    return false;
                };
                if (type == Content::fourCC("WEAP"))
                {
                    if (data.size() < 8)
                        return odd();
                    item.mValue = static_cast<std::int32_t>(le32(data.data()));
                    item.mWeight = lef(data.data() + 4);
                    return true;
                }
                if (type == Content::fourCC("BOOK"))
                {
                    if (data.size() != 16)
                        return odd();
                    item.mValue = static_cast<std::int32_t>(le32(data.data() + 8));
                    item.mWeight = lef(data.data() + 12);
                    return true;
                }
                if (type == Content::fourCC("AMMO"))
                {
                    if (data.size() < 16)
                        return odd();
                    item.mValue = static_cast<std::int32_t>(le32(data.data() + 12));
                    return true;
                }
                if (type == Content::fourCC("ALCH"))
                {
                    if (data.size() != 4)
                        return odd();
                    item.mWeight = lef(data.data());
                    return true;
                }
                // ARMO, MISC, INGR, KEYM, SLGM, APPA, SCRL: value u32, weight f32
                if (data.size() != 8)
                    return odd();
                item.mValue = static_cast<std::int32_t>(le32(data.data()));
                item.mWeight = lef(data.data() + 4);
                return true;
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
            const InventoryLimits& mLimits;
            InventoryRecords& mOut;
            std::vector<std::byte> mInflated;
        };

        constexpr std::int32_t clampCount(std::int64_t count)
        {
            return static_cast<std::int32_t>(std::min<std::int64_t>(count, kMaxStackCount));
        }
    }

    std::string_view categoryName(Category category)
    {
        switch (category)
        {
            case Category::Weapon:
                return "weapon";
            case Category::Armor:
                return "armor";
            case Category::Ammo:
                return "ammo";
            case Category::Potion:
                return "potion";
            case Category::Ingredient:
                return "ingredient";
            case Category::Book:
                return "book";
            case Category::Scroll:
                return "scroll";
            case Category::SoulGem:
                return "soulgem";
            case Category::Key:
                return "key";
            case Category::Apparatus:
                return "apparatus";
            case Category::Misc:
                return "misc";
        }
        return "misc";
    }

    std::optional<Category> categoryOfType(std::uint32_t type)
    {
        if (type == Content::fourCC("WEAP"))
            return Category::Weapon;
        if (type == Content::fourCC("ARMO"))
            return Category::Armor;
        if (type == Content::fourCC("AMMO"))
            return Category::Ammo;
        if (type == Content::fourCC("ALCH"))
            return Category::Potion;
        if (type == Content::fourCC("INGR"))
            return Category::Ingredient;
        if (type == Content::fourCC("BOOK"))
            return Category::Book;
        if (type == Content::fourCC("SCRL"))
            return Category::Scroll;
        if (type == Content::fourCC("SLGM"))
            return Category::SoulGem;
        if (type == Content::fourCC("KEYM"))
            return Category::Key;
        if (type == Content::fourCC("APPA"))
            return Category::Apparatus;
        if (type == Content::fourCC("MISC"))
            return Category::Misc;
        return std::nullopt;
    }

    bool parseInventoryRecords(
        const Content::Plugin& plugin, const LoadOrder& order, const InventoryLimits& limits, InventoryRecords& out)
    {
        out = InventoryRecords();
        out.mName = plugin.name();
        if (!isSkyrimSeHeader(plugin.header()))
        {
            out.mWalkError = makeError(plugin.name(), 0, 0, "this plugin is not a Skyrim Special Edition one");
            return false;
        }
        const FormIdResolver resolver(plugin.header(), plugin.name(), order);
        Parser parser(plugin, resolver, limits, out);
        const std::optional<Content::Error> error = plugin.walk(parser);
        if (error)
        {
            out.mWalkError = *error;
            return false;
        }
        return true;
    }

    void ItemTable::add(InventoryRecords&& records)
    {
        mOdd += records.mOddSubrecords;
        for (const ItemRemoval& removal : records.mRemoved)
            mItems.erase(removal.mId);
        for (const ItemStat& item : records.mItems)
            mItems[item.mId] = item;
        for (ContainerStat& c : records.mContainers)
        {
            if (c.mDeleted)
                mContainers.erase(c.mId);
            else
                mContainers[c.mId] = std::move(c);
        }
    }

    void ItemTable::addLeveledLists(const std::vector<LeveledList>& lists)
    {
        for (const LeveledList& list : lists)
        {
            if (list.mIsNpcList)
                continue;
            if (list.mCommon.deleted())
                mLeveled.erase(list.mCommon.mId);
            else
                mLeveled[list.mCommon.mId] = list;
        }
    }

    const ItemStat* ItemTable::item(FormId id) const
    {
        const auto it = mItems.find(id);
        return it == mItems.end() ? nullptr : &it->second;
    }

    const ContainerStat* ItemTable::container(FormId id) const
    {
        const auto it = mContainers.find(id);
        return it == mContainers.end() ? nullptr : &it->second;
    }

    const LeveledList* ItemTable::leveled(FormId id) const
    {
        const auto it = mLeveled.find(id);
        return it == mLeveled.end() ? nullptr : &it->second;
    }

    std::uint64_t contentsSeed(std::uint64_t owner)
    {
        std::uint64_t state = owner ^ 0x1F83D9ABFB41BD6Bull;
        return splitmix(state);
    }

    void normalise(std::vector<Stack>& stacks)
    {
        std::map<FormId, std::int64_t> sum;
        for (const Stack& s : stacks)
            if (s.mCount > 0)
                sum[s.mItem] += s.mCount;
        stacks.clear();
        for (const auto& [item, count] : sum)
            stacks.push_back({ item, clampCount(count) });
    }

    namespace
    {
        void resolveInto(const ItemTable& table, FormId id, std::int64_t count, std::uint16_t level,
            std::uint64_t& rng, std::uint32_t depth, std::vector<Stack>& out, ResolveReport& report)
        {
            if (count <= 0)
                return;
            if (table.item(id) != nullptr)
            {
                out.push_back({ id, clampCount(count) });
                return;
            }
            const LeveledList* list = table.leveled(id);
            if (list == nullptr)
            {
                ++report.mUnknownForms;
                return;
            }
            if (depth >= kMaxLeveledDepth)
            {
                ++report.mTooDeep;
                return;
            }
            // Chance none is drawn first, as the game does (RECALLED-VERIFY).
            if (list->mChanceNone > 0 && splitmix(rng) % 100 < list->mChanceNone)
                return;
            if ((list->mFlags & LeveledFlags::UseAll) != 0)
            {
                for (const LeveledEntry& e : list->mEntries)
                    if (e.mLevel <= level)
                        resolveInto(table, e.mReference, count * std::max<std::int64_t>(1, e.mCount), level, rng,
                            depth + 1, out, report);
                return;
            }
            std::uint16_t best = 0;
            bool any = false;
            for (const LeveledEntry& e : list->mEntries)
                if (e.mLevel <= level)
                {
                    any = true;
                    best = std::max(best, e.mLevel);
                }
            if (!any)
                return;
            const bool allBelow = (list->mFlags & LeveledFlags::CalcFromAllLevelsLessThanPlayer) != 0;
            std::vector<const LeveledEntry*> eligible;
            for (const LeveledEntry& e : list->mEntries)
                if (e.mLevel <= level && (allBelow || e.mLevel == best))
                    eligible.push_back(&e);
            if (eligible.empty())
                return;
            const LeveledEntry* chosen = eligible[splitmix(rng) % eligible.size()];
            resolveInto(table, chosen->mReference, count * std::max<std::int64_t>(1, chosen->mCount), level, rng,
                depth + 1, out, report);
        }
    }

    std::vector<Stack> resolveContents(const std::vector<InventoryEntry>& entries, const ItemTable& table,
        std::uint64_t seed, std::uint16_t level, ResolveReport* report)
    {
        ResolveReport local;
        ResolveReport& r = report != nullptr ? *report : local;
        std::uint64_t rng = seed;
        std::vector<Stack> out;
        for (const InventoryEntry& e : entries)
            resolveInto(table, e.mItem, e.mCount, level, rng, 0, out, r);
        normalise(out);
        return out;
    }

    std::string_view transferStatusName(TransferStatus status)
    {
        switch (status)
        {
            case TransferStatus::Ok:
                return "ok";
            case TransferStatus::UnknownOwner:
                return "unknown owner";
            case TransferStatus::SameOwner:
                return "same owner";
            case TransferStatus::BadCount:
                return "bad count";
            case TransferStatus::NotEnough:
                return "not enough";
        }
        return "?";
    }

    void InventoryModel::clear()
    {
        mOwners.clear();
        mTouched.clear();
        mEquipped.clear();
        mRevision = 0;
        ++mEquipmentRevision;
    }

    void InventoryModel::setPlayer(std::vector<Stack> stacks)
    {
        normalise(stacks);
        std::map<FormId, std::int32_t>& player = mOwners[kPlayerOwner];
        player.clear();
        for (const Stack& s : stacks)
            player[s.mItem] = s.mCount;
        ++mEquipmentRevision; // task 0184
        ++mRevision;
    }

    bool InventoryModel::ensureOwner(std::uint64_t owner, std::vector<Stack> stacks)
    {
        if (mOwners.count(owner) != 0)
            return true;
        if (mOwners.size() >= kMaxOwners)
            return false;
        normalise(stacks);
        if (stacks.size() > kMaxStacksPerOwner)
            stacks.resize(kMaxStacksPerOwner);
        std::map<FormId, std::int32_t>& entry = mOwners[owner];
        for (const Stack& s : stacks)
            entry[s.mItem] = s.mCount;
        return true;
    }

    bool InventoryModel::hasOwner(std::uint64_t owner) const
    {
        return mOwners.count(owner) != 0;
    }

    std::vector<Stack> InventoryModel::stacks(std::uint64_t owner) const
    {
        std::vector<Stack> out;
        const auto it = mOwners.find(owner);
        if (it == mOwners.end())
            return out;
        out.reserve(it->second.size());
        for (const auto& [item, count] : it->second)
            out.push_back({ item, count });
        return out;
    }

    std::int32_t InventoryModel::count(std::uint64_t owner, FormId item) const
    {
        const auto it = mOwners.find(owner);
        if (it == mOwners.end())
            return 0;
        const auto s = it->second.find(item);
        return s == it->second.end() ? 0 : s->second;
    }

    float InventoryModel::weightOf(std::uint64_t owner, const ItemTable& table) const
    {
        const auto it = mOwners.find(owner);
        if (it == mOwners.end())
            return 0.f;
        double total = 0.0;
        for (const auto& [item, count] : it->second)
            if (const ItemStat* stat = table.item(item))
                total += static_cast<double>(stat->mWeight) * count;
        return static_cast<float>(total);
    }

    TransferStatus InventoryModel::transfer(std::uint64_t from, std::uint64_t to, FormId item, std::int32_t count)
    {
        const auto src = mOwners.find(from);
        const auto dst = mOwners.find(to);
        if (src == mOwners.end() || dst == mOwners.end())
            return TransferStatus::UnknownOwner;
        if (from == to)
            return TransferStatus::SameOwner;
        if (count <= 0 || count > kMaxStackCount)
            return TransferStatus::BadCount;
        const auto have = src->second.find(item);
        if (have == src->second.end() || have->second < count)
            return TransferStatus::NotEnough;
        std::int32_t& target = dst->second[item];
        if (static_cast<std::int64_t>(target) + count > kMaxStackCount)
        {
            if (target == 0)
                dst->second.erase(item);
            return TransferStatus::BadCount;
        }
        target += count;
        have->second -= count;
        if (have->second == 0)
        {
            src->second.erase(have);
            if (from == kPlayerOwner && unequipRaw(item))
                ++mEquipmentRevision; // task 0184: the last one left the player's hands
        }
        mTouched.insert(from);
        mTouched.insert(to);
        ++mRevision;
        if (mSink)
            mSink(from, to, item, count);
        return TransferStatus::Ok;
    }

    TransferStatus InventoryModel::add(std::uint64_t owner, FormId item, std::int32_t count)
    {
        const auto it = mOwners.find(owner);
        if (it == mOwners.end())
            return TransferStatus::UnknownOwner;
        if (count <= 0 || count > kMaxStackCount || item == kNullFormId)
            return TransferStatus::BadCount;
        const auto have = it->second.find(item);
        const std::int64_t now = have == it->second.end() ? 0 : have->second;
        if (now + count > kMaxStackCount)
            return TransferStatus::BadCount;
        it->second[item] = static_cast<std::int32_t>(now + count);
        mTouched.insert(owner);
        ++mRevision;
        if (mSink)
            mSink(kNoOwner, owner, item, count);
        return TransferStatus::Ok;
    }

    TransferStatus InventoryModel::transferAll(std::uint64_t from, std::uint64_t to, std::int64_t& moved)
    {
        moved = 0;
        if (mOwners.count(from) == 0 || mOwners.count(to) == 0)
            return TransferStatus::UnknownOwner;
        if (from == to)
            return TransferStatus::SameOwner;
        for (const Stack& s : stacks(from))
            if (transfer(from, to, s.mItem, s.mCount) == TransferStatus::Ok)
                moved += s.mCount;
        return TransferStatus::Ok;
    }

    void InventoryModel::write(openeden::sdk::ChunkWriter& writer) const
    {
        const auto writeStacks = [&](const std::map<FormId, std::int32_t>& stacks) {
            writer.writeValue(static_cast<std::uint32_t>(stacks.size()));
            for (const auto& [item, count] : stacks)
            {
                writer.writeValue(static_cast<std::uint32_t>(item));
                writer.writeValue(static_cast<std::int32_t>(count));
            }
        };
        static const std::map<FormId, std::int32_t> empty;
        const auto player = mOwners.find(kPlayerOwner);
        writeStacks(player == mOwners.end() ? empty : player->second);
        std::uint32_t owners = 0;
        for (const std::uint64_t owner : mTouched)
            if (owner != kPlayerOwner && mOwners.count(owner) != 0)
                ++owners;
        writer.writeValue(owners);
        for (const std::uint64_t owner : mTouched)
        {
            const auto it = mOwners.find(owner);
            if (owner == kPlayerOwner || it == mOwners.end())
                continue;
            writer.writeValue(owner);
            writeStacks(it->second);
        }
    }

    bool InventoryModel::read(openeden::sdk::ChunkReader& reader, std::string& error)
    {
        std::map<std::uint64_t, std::map<FormId, std::int32_t>> owners;
        const auto readStacks = [&](std::map<FormId, std::int32_t>& out) {
            std::uint32_t n = 0;
            if (!reader.readValue(n) || n > kMaxStacksPerOwner || static_cast<std::uint64_t>(n) * 8 > reader.remaining())
            {
                error = "a stack list is damaged";
                return false;
            }
            for (std::uint32_t i = 0; i < n; ++i)
            {
                std::uint32_t item = 0;
                std::int32_t count = 0;
                if (!reader.readValue(item) || !reader.readValue(count) || count < 1 || count > kMaxStackCount
                    || item == 0 || out.count(item) != 0)
                {
                    error = "a stack is damaged";
                    return false;
                }
                out[item] = count;
            }
            return true;
        };
        if (!readStacks(owners[kPlayerOwner]))
            return false;
        std::uint32_t n = 0;
        if (!reader.readValue(n) || n > kMaxOwners)
        {
            error = "the owner count is damaged";
            return false;
        }
        std::set<std::uint64_t> touched;
        std::uint64_t previous = 0;
        for (std::uint32_t i = 0; i < n; ++i)
        {
            std::uint64_t owner = 0;
            if (!reader.readValue(owner) || owner == kPlayerOwner || (i > 0 && owner <= previous))
            {
                error = "an owner id is damaged";
                return false;
            }
            previous = owner;
            if (!readStacks(owners[owner]))
                return false;
            touched.insert(owner);
        }
        if (reader.remaining() != 0)
        {
            error = "the chunk has trailing bytes";
            return false;
        }
        mOwners = std::move(owners);
        mTouched = std::move(touched);
        ++mEquipmentRevision; // task 0184
        ++mRevision;
        return true;
    }

    bool parseStackList(std::string_view text, std::vector<Stack>& out)
    {
        const auto trim = [](std::string_view v) {
            while (!v.empty() && (v.front() == ' ' || v.front() == '\t'))
                v.remove_prefix(1);
            while (!v.empty() && (v.back() == ' ' || v.back() == '\t'))
                v.remove_suffix(1);
            return v;
        };
        const auto number = [](std::string_view v, int base, std::uint64_t& value) {
            if (v.empty() || v.size() > (base == 16 ? 8u : 7u))
                return false;
            value = 0;
            for (const char c : v)
            {
                int digit;
                if (c >= '0' && c <= '9')
                    digit = c - '0';
                else if (base == 16 && c >= 'a' && c <= 'f')
                    digit = c - 'a' + 10;
                else if (base == 16 && c >= 'A' && c <= 'F')
                    digit = c - 'A' + 10;
                else
                    return false;
                value = value * static_cast<std::uint64_t>(base) + static_cast<std::uint64_t>(digit);
            }
            return true;
        };
        std::vector<Stack> parsed;
        text = trim(text);
        while (!text.empty())
        {
            const std::size_t comma = text.find(',');
            std::string_view entry = trim(text.substr(0, comma));
            text = comma == std::string_view::npos ? std::string_view() : trim(text.substr(comma + 1));
            if (comma != std::string_view::npos && text.empty())
                return false; // a trailing comma
            const std::size_t colon = entry.find(':');
            std::uint64_t id = 0, count = 1;
            if (!number(trim(entry.substr(0, colon)), 16, id) || id == 0)
                return false;
            if (colon != std::string_view::npos
                && (!number(trim(entry.substr(colon + 1)), 10, count) || count < 1 || count > kMaxStackCount))
                return false;
            if (parsed.size() >= 64)
                return false;
            parsed.push_back({ static_cast<FormId>(id), static_cast<std::int32_t>(count) });
        }
        out = std::move(parsed);
        return true;
    }

    // ---- task 0184: equipped items
    std::string_view equipStatusName(EquipStatus status)
    {
        switch (status)
        {
            case EquipStatus::Ok:
                return "ok";
            case EquipStatus::NotHeld:
                return "not held";
            case EquipStatus::NotEquippable:
                return "not equippable";
            case EquipStatus::NotEquipped:
                return "not equipped";
        }
        return "?";
    }

    EquipStatus InventoryModel::equip(FormId item, const EquipInfo& info)
    {
        if (info.mKind != EquipKind::Weapon && info.mKind != EquipKind::Armor)
            return EquipStatus::NotEquippable;
        if (count(kPlayerOwner, item) < 1)
            return EquipStatus::NotHeld;
        const auto same = std::find_if(
            mEquipped.begin(), mEquipped.end(), [&](const EquippedItem& e) { return e.mItem == item; });
        if (same != mEquipped.end() && same->mKind == info.mKind && same->mSlotMask == info.mSlotMask)
            return EquipStatus::Ok; // already so: nothing changes
        // what this replaces: the equipped weapon, or every armour whose slots overlap
        std::erase_if(mEquipped, [&](const EquippedItem& e) {
            if (e.mItem == item)
                return true;
            if (info.mKind == EquipKind::Weapon)
                return e.mKind == EquipKind::Weapon;
            return e.mKind == EquipKind::Armor && (e.mSlotMask & info.mSlotMask) != 0;
        });
        if (mEquipped.size() >= kMaxEquipped)
            return EquipStatus::NotEquippable;
        mEquipped.push_back({ item, info.mKind, info.mSlotMask });
        std::sort(mEquipped.begin(), mEquipped.end(),
            [](const EquippedItem& a, const EquippedItem& b) { return a.mItem < b.mItem; });
        ++mEquipmentRevision;
        ++mRevision;
        return EquipStatus::Ok;
    }

    EquipStatus InventoryModel::unequip(FormId item)
    {
        const auto it
            = std::find_if(mEquipped.begin(), mEquipped.end(), [&](const EquippedItem& e) { return e.mItem == item; });
        if (it == mEquipped.end())
            return EquipStatus::NotEquipped;
        mEquipped.erase(it);
        ++mEquipmentRevision;
        ++mRevision;
        return EquipStatus::Ok;
    }

    bool InventoryModel::isEquipped(FormId item) const
    {
        return std::any_of(mEquipped.begin(), mEquipped.end(), [&](const EquippedItem& e) { return e.mItem == item; })
            && count(kPlayerOwner, item) >= 1;
    }

    std::vector<EquippedItem> InventoryModel::equipped() const
    {
        std::vector<EquippedItem> out;
        for (const EquippedItem& e : mEquipped)
            if (count(kPlayerOwner, e.mItem) >= 1)
                out.push_back(e);
        return out;
    }

    bool InventoryModel::unequipRaw(FormId item)
    {
        const std::size_t before = mEquipped.size();
        std::erase_if(mEquipped, [&](const EquippedItem& e) { return e.mItem == item; });
        return mEquipped.size() != before;
    }

    TransferStatus InventoryModel::consume(std::uint64_t owner, FormId item, std::int32_t count)
    {
        const auto it = mOwners.find(owner);
        if (it == mOwners.end())
            return TransferStatus::UnknownOwner;
        if (count <= 0 || count > kMaxStackCount)
            return TransferStatus::BadCount;
        const auto have = it->second.find(item);
        if (have == it->second.end() || have->second < count)
            return TransferStatus::NotEnough;
        have->second -= count;
        if (have->second == 0)
        {
            it->second.erase(have);
            if (owner == kPlayerOwner && unequipRaw(item))
                ++mEquipmentRevision;
        }
        mTouched.insert(owner);
        ++mRevision;
        if (mSink)
            mSink(owner, kNoOwner, item, count);
        return TransferStatus::Ok;
    }

    void InventoryModel::setEquipped(std::vector<EquippedItem> items)
    {
        std::vector<EquippedItem> kept;
        for (const EquippedItem& e : items)
        {
            if ((e.mKind != EquipKind::Weapon && e.mKind != EquipKind::Armor) || e.mItem == kNullFormId
                || kept.size() >= kMaxEquipped
                || std::any_of(kept.begin(), kept.end(), [&](const EquippedItem& k) { return k.mItem == e.mItem; }))
                continue;
            kept.push_back(e);
        }
        std::sort(
            kept.begin(), kept.end(), [](const EquippedItem& a, const EquippedItem& b) { return a.mItem < b.mItem; });
        mEquipped = std::move(kept);
        ++mEquipmentRevision;
        ++mRevision;
    }

    void EquipmentChunk::write(openeden::sdk::ChunkWriter& writer)
    {
        const std::vector<EquippedItem> items = mModel.equipped();
        writer.writeValue(static_cast<std::uint32_t>(items.size()));
        for (const EquippedItem& e : items)
        {
            writer.writeValue(static_cast<std::uint32_t>(e.mItem));
            writer.writeValue(static_cast<std::uint8_t>(e.mKind));
            writer.writeValue(static_cast<std::uint32_t>(e.mSlotMask));
        }
    }

    bool EquipmentChunk::read(openeden::sdk::ChunkReader& reader, std::uint32_t version)
    {
        mRefusal.clear();
        const auto refuse = [&](const std::string& why) {
            mRefusal = why;
            if (mSink)
                mSink(why);
            return false;
        };
        if (version != kVersion)
            return refuse("the equipment chunk has version " + std::to_string(version));
        constexpr std::uint8_t kWeaponByte = static_cast<std::uint8_t>(EquipKind::Weapon);
        constexpr std::uint8_t kArmorByte = static_cast<std::uint8_t>(EquipKind::Armor);
        std::uint32_t n = 0;
        if (!reader.readValue(n) || n > InventoryModel::kMaxEquipped
            || static_cast<std::uint64_t>(n) * 9 != reader.remaining())
            return refuse("the equipment count is damaged");
        std::vector<EquippedItem> items;
        std::uint32_t weapons = 0;
        std::uint32_t usedSlots = 0;
        std::uint64_t previous = 0;
        for (std::uint32_t i = 0; i < n; ++i)
        {
            std::uint32_t item = 0, mask = 0;
            std::uint8_t kind = 0;
            if (!reader.readValue(item) || !reader.readValue(kind) || !reader.readValue(mask) || item == 0
                || (i > 0 && item <= previous))
                return refuse("an equipped item is damaged");
            previous = item;
            if (kind != kWeaponByte && kind != kArmorByte)
                return refuse("an equipped item has an unknown kind");
            if (kind == kWeaponByte)
            {
                if (++weapons > 1)
                    return refuse("two weapons are equipped");
            }
            else
            {
                if ((usedSlots & mask) != 0)
                    return refuse("two armour pieces share a slot");
                usedSlots |= mask;
            }
            items.push_back({ static_cast<FormId>(item), static_cast<EquipKind>(kind), mask });
        }
        mModel.setEquipped(std::move(items));
        return true;
    }

    bool InventoryChunk::read(openeden::sdk::ChunkReader& reader, std::uint32_t version)
    {
        mRefusal.clear();
        std::string error;
        if (version != kVersion)
            error = "the inventory chunk has version " + std::to_string(version);
        else if (mModel.read(reader, error))
        {
            if (mRestored)
                mRestored("restored: " + std::to_string(mModel.ownerCount()) + " owners, "
                    + std::to_string(mModel.touchedCount()) + " touched");
            return true;
        }
        mRefusal = error;
        if (mSink)
            mSink(error);
        return false;
    }
}
