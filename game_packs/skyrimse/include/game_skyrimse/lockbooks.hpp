#ifndef OPENEDEN_GAME_SKYRIMSE_LOCKBOOKS_H
#define OPENEDEN_GAME_SKYRIMSE_LOCKBOOKS_H

// Lock picking and books of a Skyrim SE session: the pure part (task 0203, milestone M-S6). No engine type, no thread.
//
//  1. Lock tiers and the lock picking attempt (a deterministic state machine the minigame window drives).
//  2. BOOK records: DATA (16 bytes: flags u8, type u8, unused u16, teaches u32, value u32, weight f32; VERIFIED on the
//     five masters), the DESC and FULL strings, and the index of them.
//  3. Book text: the markup of DESC rendered to plain pages (subset, see renderBookText).
//  4. LockBooksModel and its save chunk "lockbooks": the locks picked, the books read, the spells learned.
//
// Every tuning number is RECALLED-VERIFY and lives in PickSettings.

#include <game_skyrimse/records.hpp>

#include <components/contentsource/plugin.hpp>

#include <openeden/game/save_chunks.hpp>

#include <array>
#include <cstdint>
#include <functional>
#include <map>
#include <optional>
#include <set>
#include <string>
#include <string_view>
#include <vector>

namespace game_skyrimse
{
    namespace lockbooks
    {
        // ---- locks ---------------------------------------------------------------------------------------------

        /// XLOC level byte tiers: 0 to 24 Novice, 25 to 49 Apprentice, 50 to 74 Adept, 75 to 99 Expert, 100 to 254
        /// Master, 255 (and anything outside 0 to 255) requires its key. The data: levels 0, 1, 25, 50, 75, 100, 255.
        enum class LockTier : std::uint8_t
        {
            Novice,
            Apprentice,
            Adept,
            Expert,
            Master,
            RequiresKey,
        };

        [[nodiscard]] LockTier tierOf(std::int32_t level);
        [[nodiscard]] bool pickable(std::int32_t level);
        [[nodiscard]] std::string_view tierName(LockTier tier); ///< "novice" ... "requires key"

        struct PickSettings
        {
            /// half width of the sweet spot, degrees, per tier
            std::array<float, 5> mWindow{ 20.f, 14.f, 9.f, 5.f, 2.5f };
            float mWindowPerSkill = 0.05f; ///< degrees added per Lockpicking point
            float mWindowMin = 1.f;
            float mFalloff = 60.f; ///< degrees outside the window over which the turn limit falls from 1 to 0
            float mTurnSeconds = 0.6f; ///< a full turn inside the window
            float mRelaxPerSecond = 2.f; ///< progress lost per second when not turning
            float mStrainSeconds = 0.9f; ///< a pick held against the lock breaks after this long
            float mStrainRecover = 2.f; ///< strain lost per second when not turning
            float mXpPerTier = 10.f; ///< experience reported on success: this x (tier + 1)
            float mMaxStep = 0.1f; ///< seconds: a longer frame counts as this
        };

        /// The half width of the sweet spot for a tier and a Lockpicking skill.
        [[nodiscard]] float windowDegrees(LockTier tier, float skill, const PickSettings& settings);
        /// The sweet spot of a lock, degrees in [-85, 85), a deterministic function of the placement id.
        [[nodiscard]] float sweetSpotOf(std::uint64_t placement);

        class Attempt
        {
        public:
            enum class State : std::uint8_t
            {
                Idle,
                Picking,
                Opened,
                Broke,
                Cancelled,
                Refused, ///< the lock cannot be picked
            };

            /// Starts picking. False (state Refused) when the level cannot be picked. Resets everything.
            bool begin(std::uint64_t placement, std::int32_t level, float skill, const PickSettings& settings = {});
            /// One step. `angle` is the pick's angle in degrees (cut to [-90, 90], not a number counts as 0), `turning`
            /// whether the player turns the lock. Does nothing unless the state is Picking.
            void update(float dt, float angle, bool turning);
            void cancel();

            [[nodiscard]] State state() const { return mState; }
            [[nodiscard]] float progress() const { return mProgress; }
            [[nodiscard]] float strain() const { return mStrain; }
            [[nodiscard]] float window() const { return mWindow; }
            [[nodiscard]] float sweet() const { return mSweet; }
            [[nodiscard]] LockTier tier() const { return mTier; }
            [[nodiscard]] std::int32_t level() const { return mLevel; }
            [[nodiscard]] std::uint64_t placement() const { return mPlacement; }
            /// 1 inside the window, falling to 0 over mFalloff degrees outside it.
            [[nodiscard]] float turnLimit(float angle) const;

        private:
            State mState = State::Idle;
            PickSettings mSettings;
            std::uint64_t mPlacement = 0;
            std::int32_t mLevel = 0;
            LockTier mTier = LockTier::Novice;
            float mSweet = 0.f;
            float mWindow = 0.f;
            float mProgress = 0.f;
            float mStrain = 0.f;
        };

        /// What a success reports to the skill: mXpPerTier x (tier + 1).
        [[nodiscard]] float unlockExperience(LockTier tier, const PickSettings& settings);

        // ---- books ---------------------------------------------------------------------------------------------

        namespace BookFlags
        {
            inline constexpr std::uint8_t TeachesSkill = 0x01;
            inline constexpr std::uint8_t CannotBeTaken = 0x02;
            inline constexpr std::uint8_t TeachesSpell = 0x04;
        }

        enum class BookKind : std::uint8_t
        {
            Plain,
            Skill,
            Spell,
        };

        [[nodiscard]] std::string_view bookKindName(BookKind kind); ///< "book", "skill", "spell"

        struct BookRecord
        {
            FormId mId = kNullFormId;
            std::uint32_t mPlugin = 0; ///< the plugin's index in the load order of the file list (set by the index)
            std::uint8_t mFlags = 0;
            std::uint8_t mType = 0;
            std::uint32_t mTeaches = 0; ///< an actor value (skill) or, resolved, a SPEL form id
            std::int32_t mValue = 0;
            float mWeight = 0.f;
            std::string mEditorId;
            LString mName;
            LString mDesc;
            bool mDecoded = false;

            [[nodiscard]] BookKind kind() const;
            /// The skill slot (0 to 17) a skill book teaches, or -1.
            [[nodiscard]] int skillSlot() const;
        };

        struct BookLimits
        {
            std::uint64_t mMaxRecordBytes = 16ull * 1024 * 1024;
            std::uint32_t mMaxErrors = 256;
            std::size_t mMaxTextBytes = 256 * 1024; ///< DESC and FULL text of a non-localised plugin
        };

        struct BookRecords
        {
            std::string mName;
            bool mLocalized = false;
            std::vector<BookRecord> mBooks;
            std::vector<FormId> mRemoved; ///< deleted records
            std::uint64_t mFailed = 0;
            std::uint64_t mOdd = 0; ///< a DATA of another size, a form that does not resolve
            std::uint64_t mBothFlags = 0; ///< a skill and a spell flag together (the skill wins)
            std::vector<Content::Error> mErrors;
            std::optional<Content::Error> mWalkError;
        };

        /// Reads the BOOK records of one plugin. False with mWalkError when the file cannot be walked.
        [[nodiscard]] bool parseBookRecords(
            const Content::Plugin& plugin, const LoadOrder& order, const BookLimits& limits, BookRecords& out);

        class BookIndex
        {
        public:
            /// `pluginIndex` is the plugin's place in the list the text resolver is given.
            void add(BookRecords&& records, std::uint32_t pluginIndex);
            [[nodiscard]] const BookRecord* book(FormId id) const;
            [[nodiscard]] std::size_t size() const { return mBooks.size(); }
            [[nodiscard]] std::uint64_t odd() const { return mOdd; }

        private:
            std::map<FormId, BookRecord> mBooks;
            std::uint64_t mOdd = 0;
        };

        // ---- text ----------------------------------------------------------------------------------------------

        /// Renders the DESC markup (Windows-1252 bytes) as plain pages of UTF-8: paragraphs are separated by one empty
        /// line. Rendered: <p>, </p>, <br>, <div>, </div> (paragraph or line breaks), <font ...> and </font> (dropped,
        /// the text stays), <img ...> (becomes "[picture]"), [pagebreak] (a page break), the entities &nbsp; &amp; &lt;
        /// &gt; &quot; &apos; &#N; and &#xN;. Every other tag is dropped. NOT rendered: alignment, font size, face and
        /// colour, pictures, links. Pages are cut at paragraph boundaries near `pageChars`; a longer paragraph is cut
        /// at its last space before the limit. Input over kMaxBookTextBytes is cut. Never throws.
        inline constexpr std::size_t kMaxBookTextBytes = 256 * 1024;
        inline constexpr std::size_t kDefaultPageChars = 1100;
        [[nodiscard]] std::vector<std::string> renderBookText(
            std::string_view raw, std::size_t pageChars = kDefaultPageChars);

        // ---- state and the save chunk --------------------------------------------------------------------------

        class LockBooksModel
        {
        public:
            static constexpr std::size_t kMaxLocks = 4096;
            static constexpr std::size_t kMaxBooks = 4096;
            static constexpr std::size_t kMaxSpells = 1024;

            /// False when already picked or the table is full.
            bool markPicked(std::uint64_t placement);
            [[nodiscard]] bool isPicked(std::uint64_t placement) const { return mPicked.count(placement) != 0; }
            bool markRead(FormId book);
            [[nodiscard]] bool isRead(FormId book) const { return mRead.count(book) != 0; }
            /// False when the spell is known already or the list is full.
            bool learnSpell(FormId spell);
            [[nodiscard]] bool knowsSpell(FormId spell) const;
            [[nodiscard]] const std::vector<FormId>& spells() const { return mSpells; }
            void addBroken()
            {
                ++mBroken;
                ++mRevision;
            }
            [[nodiscard]] std::uint32_t broken() const { return mBroken; }
            [[nodiscard]] std::size_t pickedCount() const { return mPicked.size(); }
            [[nodiscard]] std::size_t readCount() const { return mRead.size(); }
            [[nodiscard]] std::uint32_t revision() const { return mRevision; }
            void clear();

            void write(openeden::sdk::ChunkWriter& writer) const;
            /// False with `error` for a damaged chunk; nothing changes then.
            [[nodiscard]] bool read(openeden::sdk::ChunkReader& reader, std::string& error);

        private:
            std::set<std::uint64_t> mPicked;
            std::set<FormId> mRead;
            std::vector<FormId> mSpells;
            std::uint32_t mBroken = 0;
            std::uint32_t mRevision = 0;
        };

        /// The pack's save chunk "lockbooks" version 1: u32 locks and their u64 ids ascending, u32 books and their u32
        /// ids ascending, u32 spells and their u32 ids in the order learned, u32 picks broken. A damaged chunk, or one
        /// over the caps, refuses.
        class LockBooksChunk final : public openeden::sdk::SaveChunkHandler
        {
        public:
            static constexpr std::uint32_t kVersion = 1;
            static constexpr std::string_view kName = "lockbooks";
            explicit LockBooksChunk(LockBooksModel& model)
                : mModel(model)
            {
            }
            void setRefusalSink(std::function<void(const std::string&)> sink) { mSink = std::move(sink); }
            [[nodiscard]] const std::string& refusal() const { return mRefusal; }

            std::string_view chunkName() const override { return kName; }
            std::uint32_t chunkVersion() const override { return kVersion; }
            void write(openeden::sdk::ChunkWriter& writer) override { mModel.write(writer); }
            bool read(openeden::sdk::ChunkReader& reader, std::uint32_t version) override;

        private:
            LockBooksModel& mModel;
            std::function<void(const std::string&)> mSink;
            std::string mRefusal;
        };
    }
}

#endif
