#ifndef OPENEDEN_GAME_SKYRIMSE_INVENTORY_H
#define OPENEDEN_GAME_SKYRIMSE_INVENTORY_H

// Skyrim SE items, container contents and the inventory state of a session (task 0182, milestone M-S6).
//
// The pack OWNS inventory state (game rules); the core only draws what the pack's script bindings report. This header
// has four parts, none of which touches an engine type:
//
//  1. Item stats: value, weight and category of every carried item type, read from the records' DATA subrecords.
//     Layouts (the first four are VERIFIED against the installed masters by SkyrimInventoryRealData, which prints
//     any record whose size differs; the rest are RECALLED-VERIFY and listed as such there):
//       ARMO, MISC, INGR, KEYM, SLGM, APPA, SCRL  DATA  8 bytes: value u32, weight f32
//       WEAP                                       DATA  value u32, weight f32, damage u16 (10 bytes in SSE)
//       BOOK                                       DATA  16 bytes: flags u8, type u8, u16 unused, teaches u32, value u32,
//                                                        weight f32
//       AMMO                                       DATA  16 bytes: projectile u32, flags u32, damage f32, value u32; the
//                                                        record has no weight (arrows weigh 0 in Skyrim: RECALLED-VERIFY)
//       ALCH                                       DATA  4 bytes: weight f32; ENIT 20 bytes, value i32 first
//  2. Container contents: CONT / CNTO (8 bytes: form id, count i32), with leveled lists (LVLI) resolved ONCE, at the
//     first time a container is looked at, by a pick seeded from the container's own identity (no clock, no `rand`).
//  3. `InventoryModel`: who holds what. Owners are the player (0) and placements (a container's or a corpse's
//     placement id). A transfer is one function that either moves a whole count or changes nothing. The model keeps a
//     revision number that changes on every change, so a script can poll one number instead of re-reading lists.
//  4. `InventoryChunk`: the pack's save chunk ("inventory"): the player's stacks and every touched owner's stacks. An
//     untouched container is rebuilt from the data (the same seed gives the same contents); a looted corpse is
//     rebuilt at load by the engine (neutral actors are not saved, decision 0017), so its stacks come back with it.
//
// Threads: parseInventoryRecords and ItemTable::add/finish run on a worker; the model and the chunk run on the main
// thread (the host calls the chunk's write and read there).

#include <game_skyrimse/actors.hpp>
#include <game_skyrimse/records.hpp>

#include <components/contentsource/plugin.hpp>

#include <openeden/game/save_chunks.hpp>

#include <cstdint>
#include <functional>
#include <map>
#include <optional>
#include <set>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace game_skyrimse
{
    namespace inventory
    {
        /// The order of the categories is the order of the lists in the window.
        enum class Category : std::uint8_t
        {
            Weapon,
            Armor,
            Ammo,
            Potion,
            Ingredient,
            Book,
            Scroll,
            SoulGem,
            Key,
            Apparatus,
            Misc,
        };

        [[nodiscard]] std::string_view categoryName(Category category);
        /// The category of a record type (fourCC), nullopt for a type that is not an item.
        [[nodiscard]] std::optional<Category> categoryOfType(std::uint32_t recordType);

        /// Task 0184: what an item can be used as by the player.
        enum class EquipKind : std::uint8_t
        {
            None,
            Weapon, ///< one at a time (the right hand)
            Armor, ///< replaces every equipped piece whose slot mask overlaps
            Potion, ///< used, not worn
        };

        struct EquipInfo
        {
            EquipKind mKind = EquipKind::None;
            std::uint32_t mSlotMask = 0; ///< BOD2 slots of an armour; 0 for a weapon
            friend bool operator==(const EquipInfo&, const EquipInfo&) = default;
        };

        /// One effect of an ALCH record (EFID + EFIT: magnitude f32, area u32, duration u32).
        struct ItemEffect
        {
            FormId mEffect = kNullFormId;
            float mMagnitude = 0.f;
            std::uint32_t mArea = 0;
            std::uint32_t mDuration = 0;
        };

        struct ItemStat
        {
            FormId mId = kNullFormId;
            Category mCategory = Category::Misc;
            std::int32_t mValue = 0;
            float mWeight = 0.f;
            bool mDecoded = false; ///< false when the DATA subrecord was missing or of an unexpected size
            std::vector<ItemEffect> mEffects; ///< task 0184: ALCH only
            std::vector<FormId> mKeywords; ///< task 0189: KWDA (the vendor lists name keywords); at most 64 are kept
        };

        struct ContainerStat
        {
            FormId mId = kNullFormId;
            std::vector<InventoryEntry> mItems; ///< CNTO
            bool mDeleted = false;
        };

        struct ItemRemoval
        {
            FormId mId = kNullFormId; ///< a deleted item record
        };

        struct InventoryLimits
        {
            std::uint64_t mMaxRecordBytes = 16ull * 1024 * 1024;
            std::uint32_t mMaxItems = 65536; ///< CNTO entries of one container
            std::uint32_t mMaxErrors = 256;
        };

        struct InventoryRecords
        {
            std::string mName;
            std::vector<ItemStat> mItems;
            std::vector<ItemRemoval> mRemoved;
            std::vector<ContainerStat> mContainers;
            std::uint64_t mFailed = 0;
            std::uint64_t mUnresolvedFormIds = 0;
            std::uint64_t mOddSubrecords = 0; ///< a DATA, ENIT or CNTO of an unexpected size
            std::vector<Content::Error> mErrors;
            std::optional<Content::Error> mWalkError;
        };

        /// Reads the item and CONT records of one plugin. False with mWalkError when the file cannot be walked.
        [[nodiscard]] bool parseInventoryRecords(const Content::Plugin& plugin, const LoadOrder& order,
            const InventoryLimits& limits, InventoryRecords& out);

        /// The merged table of a load order: a later record of the same id replaces an earlier one, a deleted record
        /// removes it. Add plugins in load order, then call finish() once.
        class ItemTable
        {
        public:
            void add(InventoryRecords&& records);
            void addLeveledLists(const std::vector<LeveledList>& lists); ///< LVLI only; the later list wins
            void finish() {}

            [[nodiscard]] const ItemStat* item(FormId id) const;
            [[nodiscard]] const ContainerStat* container(FormId id) const;
            [[nodiscard]] const LeveledList* leveled(FormId id) const;
            [[nodiscard]] std::size_t itemCount() const { return mItems.size(); }
            [[nodiscard]] std::size_t containerCount() const { return mContainers.size(); }
            [[nodiscard]] std::size_t leveledCount() const { return mLeveled.size(); }
            [[nodiscard]] std::uint64_t oddSubrecords() const { return mOdd; }

        private:
            std::unordered_map<FormId, ItemStat> mItems;
            std::unordered_map<FormId, ContainerStat> mContainers;
            std::unordered_map<FormId, LeveledList> mLeveled;
            std::uint64_t mOdd = 0;
        };

        struct Stack
        {
            FormId mItem = kNullFormId;
            std::int32_t mCount = 0;
            friend bool operator==(const Stack&, const Stack&) = default;
        };

        struct ResolveReport
        {
            std::uint32_t mUnknownForms = 0; ///< an entry that is neither an item nor a leveled list
            std::uint32_t mTooDeep = 0; ///< a leveled list nested past kMaxLeveledDepth
        };

        inline constexpr std::uint32_t kMaxLeveledDepth = 8;
        inline constexpr std::int32_t kMaxStackCount = 1000000; ///< a hostile count is cut here

        /// The deterministic seed of an owner's contents (splitmix64 of the owner's id). No clock, no unseeded random.
        [[nodiscard]] std::uint64_t contentsSeed(std::uint64_t owner);

        /// A container's or a corpse's entries as stacks: items as they are (counts of 0 or below dropped), leveled
        /// lists resolved by the seeded pick (UseAll lists give every entry; chance-none and the level rule as in
        /// ActorIndex::pickLeveled, RECALLED-VERIFY). The result is sorted by item id and equal items are merged.
        [[nodiscard]] std::vector<Stack> resolveContents(const std::vector<InventoryEntry>& entries,
            const ItemTable& table, std::uint64_t seed, std::uint16_t level, ResolveReport* report = nullptr);

        /// Sorts by item id and merges equal items (a count that would pass kMaxStackCount is cut).
        void normalise(std::vector<Stack>& stacks);

        /// Task 0184 (a test aid, the pack setting `inventory test items`): "0008715f,00012eb7:2" is a list of
        /// stacks, each a form id in hexadecimal (1 to 8 digits) and an optional count after a colon (default 1, 1 to
        /// kMaxStackCount). At most 64 entries. False, with `out` untouched, for any malformed entry; blanks around
        /// entries are ignored and an empty text is an empty list.
        [[nodiscard]] bool parseStackList(std::string_view text, std::vector<Stack>& out);

        inline constexpr std::uint64_t kPlayerOwner = 0;
        /// The form id of Gold001 as a load-order id when Skyrim.esm is first in the order (it always is).
        inline constexpr FormId kGoldItem = 0x0000000F;
        /// Skyrim's base carry weight (GMST fCarryWeightBase 300, RECALLED-VERIFY; a stamina bonus is not applied).
        inline constexpr float kBaseCarryWeight = 300.f;

        enum class TransferStatus : std::uint8_t
        {
            Ok,
            UnknownOwner,
            SameOwner,
            BadCount, ///< zero, negative or beyond kMaxStackCount
            NotEnough, ///< the source holds fewer
        };
        [[nodiscard]] std::string_view transferStatusName(TransferStatus status);

        /// Task 0184: the player's worn or wielded item.
        struct EquippedItem
        {
            FormId mItem = kNullFormId;
            EquipKind mKind = EquipKind::None;
            std::uint32_t mSlotMask = 0;
            friend bool operator==(const EquippedItem&, const EquippedItem&) = default;
        };

        enum class EquipStatus : std::uint8_t
        {
            Ok,
            NotHeld, ///< the player holds none of it
            NotEquippable, ///< a potion, a misc item, or a kind of None
            NotEquipped, ///< unequip of an item that is not equipped
        };
        [[nodiscard]] std::string_view equipStatusName(EquipStatus status);

        class InventoryModel
        {
        public:
            static constexpr std::size_t kMaxOwners = 4096;
            static constexpr std::size_t kMaxStacksPerOwner = 65536;

            /// The player's stacks, replacing what was there (a new game). The revision changes.
            void setPlayer(std::vector<Stack> stacks);
            /// Registers an owner with its starting stacks the first time; later calls do nothing. True when the owner
            /// exists afterwards (false: the table is full of owners). Does not change the revision.
            bool ensureOwner(std::uint64_t owner, std::vector<Stack> stacks);
            [[nodiscard]] bool hasOwner(std::uint64_t owner) const;
            [[nodiscard]] std::vector<Stack> stacks(std::uint64_t owner) const; ///< sorted by item id
            [[nodiscard]] std::int32_t count(std::uint64_t owner, FormId item) const;
            [[nodiscard]] float weightOf(std::uint64_t owner, const ItemTable& table) const;

            [[nodiscard]] TransferStatus transfer(std::uint64_t from, std::uint64_t to, FormId item, std::int32_t count);

            /// Task 0198 (the one hook of the Papyrus natives): items that appear from nowhere (a script's AddItem), and a
            /// sink that is told of every change of any list: the owner it left (kNoOwner: none), the owner it joined
            /// (kNoOwner: none), the item and the count. Main thread; a sink must not change the model.
            static constexpr std::uint64_t kNoOwner = ~std::uint64_t(0);
            using ChangeSink = std::function<void(std::uint64_t from, std::uint64_t to, FormId item, std::int32_t count)>;
            void setChangeSink(ChangeSink sink) { mSink = std::move(sink); }
            [[nodiscard]] TransferStatus add(std::uint64_t owner, FormId item, std::int32_t count);
            /// Moves every stack; `moved` is how many items (the sum of counts). SameOwner and UnknownOwner as above.
            [[nodiscard]] TransferStatus transferAll(std::uint64_t from, std::uint64_t to, std::int64_t& moved);

            [[nodiscard]] std::uint32_t revision() const { return mRevision; }
            [[nodiscard]] std::size_t ownerCount() const { return mOwners.size(); }
            [[nodiscard]] std::size_t touchedCount() const { return mTouched.size(); }

            /// Task 0184: the player's equipped items. A weapon replaces the equipped weapon; an armour replaces every
            /// equipped piece whose slot mask overlaps its own. `equipped()` lists only items the player still holds,
            /// sorted by item id. Every change moves the revision.
            [[nodiscard]] EquipStatus equip(FormId item, const EquipInfo& info);
            [[nodiscard]] EquipStatus unequip(FormId item);
            [[nodiscard]] bool isEquipped(FormId item) const;
            [[nodiscard]] std::vector<EquippedItem> equipped() const;
            /// Removes `count` of `item` from `owner` (a potion that was drunk). Same statuses as transfer.
            [[nodiscard]] TransferStatus consume(std::uint64_t owner, FormId item, std::int32_t count);
            /// Replaces the equipped list (a restore); no check against holdings (a read may come before the
            /// inventory's own). Duplicate items keep the first.
            void setEquipped(std::vector<EquippedItem> items);
            [[nodiscard]] std::size_t equippedRawCount() const { return mEquipped.size(); }
            /// Moves only when what the player wears or wields may have changed (an equip, an unequip, a transfer or
            /// a consume that took the last of an equipped item, a restore); a poll compares one number.
            [[nodiscard]] std::uint32_t equipmentRevision() const { return mEquipmentRevision; }
            static constexpr std::size_t kMaxEquipped = 64;

            /// The save form (see InventoryChunk): written canonically; a read that is damaged changes nothing.
            void write(openeden::sdk::ChunkWriter& writer) const;
            [[nodiscard]] bool read(openeden::sdk::ChunkReader& reader, std::string& error);

            /// Back to a new game (no owner, revision 0).
            void clear();

        private:
            bool unequipRaw(FormId item);
            std::map<std::uint64_t, std::map<FormId, std::int32_t>> mOwners;
            ChangeSink mSink; ///< task 0198
            std::set<std::uint64_t> mTouched; ///< owners that have had a transfer (the player is always saved)
            std::vector<EquippedItem> mEquipped; ///< task 0184
            std::uint32_t mEquipmentRevision = 0;
            std::uint32_t mRevision = 0;
        };

        /// The pack's save chunk. Version 1: u32 stack count and the player's (item u32, count i32) pairs, then u32
        /// owner count and, for each owner in ascending id, its u64 id, a u32 stack count and its pairs. Caps: 4096
        /// owners, 65536 stacks each, counts 1..kMaxStackCount; anything else refuses the chunk.
        class InventoryChunk final : public openeden::sdk::SaveChunkHandler
        {
        public:
            static constexpr std::uint32_t kVersion = 1;
            static constexpr std::string_view kName = "inventory";
            explicit InventoryChunk(InventoryModel& model)
                : mModel(model)
            {
            }
            void setRefusalSink(std::function<void(const std::string&)> sink) { mSink = std::move(sink); }
            /// Told once for every chunk read that was accepted (the pack logs it).
            void setRestoredSink(std::function<void(const std::string&)> sink) { mRestored = std::move(sink); }
            [[nodiscard]] const std::string& refusal() const { return mRefusal; }

            std::string_view chunkName() const override { return kName; }
            std::uint32_t chunkVersion() const override { return kVersion; }
            void write(openeden::sdk::ChunkWriter& writer) override { mModel.write(writer); }
            bool read(openeden::sdk::ChunkReader& reader, std::uint32_t version) override;

        private:
            InventoryModel& mModel;
            std::function<void(const std::string&)> mSink;
            std::function<void(const std::string&)> mRestored;
            std::string mRefusal;
        };

        /// Task 0184: the pack's save chunk "equipment" (version 1): u32 count (at most kMaxEquipped), then for each
        /// (item u32, kind u8 of Weapon, Armor, slot mask u32). The inventory chunk is untouched. A damaged chunk
        /// changes nothing.
        class EquipmentChunk final : public openeden::sdk::SaveChunkHandler
        {
        public:
            static constexpr std::uint32_t kVersion = 1;
            static constexpr std::string_view kName = "equipment";
            explicit EquipmentChunk(InventoryModel& model)
                : mModel(model)
            {
            }
            void setRefusalSink(std::function<void(const std::string&)> sink) { mSink = std::move(sink); }
            [[nodiscard]] const std::string& refusal() const { return mRefusal; }

            std::string_view chunkName() const override { return kName; }
            std::uint32_t chunkVersion() const override { return kVersion; }
            void write(openeden::sdk::ChunkWriter& writer) override;
            bool read(openeden::sdk::ChunkReader& reader, std::uint32_t version) override;

        private:
            InventoryModel& mModel;
            std::function<void(const std::string&)> mSink;
            std::string mRefusal;
        };
    }
}

#endif
