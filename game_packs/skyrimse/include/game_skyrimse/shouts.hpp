#ifndef OPENEDEN_GAME_SKYRIMSE_SHOUTS_H
#define OPENEDEN_GAME_SKYRIMSE_SHOUTS_H

// The shouts of a Skyrim SE session (task 0205, milestone M-S7). Off unless [extension.skyrimse] shouts.
//
// This file is the pure half: the SHOU and WOOP records, the rules (which words a hold reaches, which words a shout
// has unlocked), the player's shout state (learned words, unlocked words, dragon souls, the equipped shout, the
// recovery cooldown) and its save chunk. The casting itself is in combat_shout.cpp (SkyrimCombat), the script
// bindings and the Papyrus natives in shouts_service.*. No engine, renderer, script-language or clock type: the
// cooldown counts the combat clock the caller hands to tick().
//
// Layouts (settled by the data, docs/tasks/0205-skyrim-shouts.md; Skyrim.esm and the DLC plugins, VERIFIED):
//  - SHOU: EDID, FULL (a string id when localised), MDOB (a form id), DESC (a string id), then exactly three SNAM of
//    12 bytes: the word (a WOOP form id), the word's spell (a SPEL form id) and the recovery time (float, seconds).
//  - WOOP: EDID, FULL, TNAM (the translation).
//  - The GMSTs fShoutTime1 (0.2) and fShoutTime2 (0.9) are in the data; what they mean is RECALLED-VERIFY, see
//    wordsReached().
//
// What is ours (the data has no rule for it): a word is unlocked by learning it and spending one dragon soul (or by a
// script's UnlockWord); the words of a shout are used in order, so a shout's level is the leading run of unlocked
// words; a hold reaches word n after (n - 1) x fShoutTime2 seconds, a release before fShoutTime1 cancels.
//
// Hostile input: every size is checked against the layout, a float that is not finite or negative is refused, a
// record with the wrong number of SNAM is kept with the words it has (mWordCount), nothing throws. A save chunk that
// does not fit its size exactly, has an id out of order, a count over the cap or a value that is not finite is
// refused whole and changes nothing.

#include "game_skyrimse/land.hpp"
#include "game_skyrimse/records.hpp"

#include <openeden/game/save_chunks.hpp>

#include <array>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace game_skyrimse
{
    inline constexpr std::size_t kShoutWordCount = 3;

    struct ShoutWordSlot
    {
        FormId mWord = kNullFormId; ///< WOOP
        FormId mSpell = kNullFormId; ///< SPEL, cast when the word is the last one spoken
        float mRecovery = 0.f; ///< seconds of cooldown when this is the last word spoken
    };

    struct ShoutRecord
    {
        RecordCommon mCommon;
        bool mHasData = false; ///< at least one SNAM was read
        LString mName;
        FormId mMenuObject = kNullFormId;
        std::array<ShoutWordSlot, kShoutWordCount> mWords{};
        std::size_t mWordCount = 0; ///< SNAM read, 0 to 3
    };

    struct WordRecord
    {
        RecordCommon mCommon;
        LString mName;
        LString mTranslation; ///< TNAM (task 0266: what the word means, shown by the shout menu)
    };

    [[nodiscard]] bool decodeShout(std::span<const std::byte> bytes, const Content::RawRecord& record,
        const DecodeContext& context, ShoutRecord& out, std::string& why);
    [[nodiscard]] bool decodeWord(std::span<const std::byte> bytes, const Content::RawRecord& record,
        const DecodeContext& context, WordRecord& out, std::string& why);

    /// The shouts and words of a load order by form id (later plugins override whole records).
    class ShoutIndex
    {
    public:
        void add(const std::vector<ShoutRecord>& shouts, const std::vector<WordRecord>& words);
        [[nodiscard]] const ShoutRecord* shout(FormId id) const;
        [[nodiscard]] const WordRecord* word(FormId id) const;
        /// Case-insensitive editor id; null when none.
        [[nodiscard]] const ShoutRecord* shoutByEditorId(std::string_view editorId) const;
        [[nodiscard]] const WordRecord* wordByEditorId(std::string_view editorId) const;
        /// The shout that has `word` as one of its words (the lowest form id when several do), null when none.
        [[nodiscard]] const ShoutRecord* shoutOfWord(FormId word) const;
        [[nodiscard]] const std::vector<ShoutRecord>& shouts() const { return mShouts; }
        [[nodiscard]] std::size_t shoutCount() const { return mShouts.size(); }
        [[nodiscard]] std::size_t wordCount() const { return mWords.size(); }

    private:
        std::vector<ShoutRecord> mShouts; ///< sorted by form id
        std::vector<WordRecord> mWords; ///< sorted by form id
    };
}

namespace game_skyrimse::shouts
{
    /// The GMSTs fShoutTime1 and fShoutTime2 (a GMST the data has replaces these defaults).
    struct Settings
    {
        float mMinimumHold = 0.2f; ///< fShoutTime1: a release before this is not a shout
        float mWordTime = 0.9f; ///< fShoutTime2: seconds of hold for each further word
    };

    /// How many words a hold of `held` seconds has reached (1 to `available`; 0 when the hold is shorter than the
    /// minimum or nothing is available). Word n is reached at (n - 1) x mWordTime. Not finite or negative: 0.
    [[nodiscard]] std::size_t wordsReached(float held, std::size_t available, const Settings& settings);

    /// Seconds a hold takes to reach word n (1 based); 0 for word 1.
    [[nodiscard]] float timeToWord(std::size_t n, const Settings& settings);

    inline constexpr std::size_t kMaxWords = 4096; ///< learned or unlocked words kept (and read back)
    inline constexpr std::uint32_t kMaxSouls = 100000;

    /// The player's shout state. Not thread safe (the main thread). Every change that matters bumps revision().
    class ShoutState
    {
    public:
        /// Learns a word (a word wall). True when it was new.
        bool learn(FormId word);
        /// Unlocks a LEARNED word with one dragon soul. False (nothing changes) when the word is unknown to the
        /// player, already unlocked, or there is no soul.
        bool unlock(FormId word);
        /// Unlocks a word with no soul (a script's UnlockWord also learns it). True when it was not unlocked.
        bool forceUnlock(FormId word);
        [[nodiscard]] bool learned(FormId word) const;
        [[nodiscard]] bool unlocked(FormId word) const;
        [[nodiscard]] std::size_t learnedCount() const { return mLearned.size(); }
        [[nodiscard]] std::size_t unlockedCount() const { return mUnlocked.size(); }

        void addSouls(std::uint32_t count);
        [[nodiscard]] std::uint32_t souls() const { return mSouls; }

        void equip(FormId shout);
        [[nodiscard]] FormId equipped() const { return mEquipped; }

        /// The leading run of unlocked words of the shout (0 to 3).
        [[nodiscard]] std::size_t availableWords(const ShoutRecord& shout) const;

        /// Starts the recovery cooldown (seconds; a value that is not finite or not above 0 does nothing).
        void startCooldown(float seconds);
        /// Counts the cooldown down by `seconds` (not finite or negative: nothing).
        void tick(float seconds);
        [[nodiscard]] float cooldown() const { return mCooldown; }
        [[nodiscard]] float cooldownTotal() const { return mCooldownTotal; }
        [[nodiscard]] bool ready() const { return mCooldown <= 0.f; }

        /// Task 0253: a message for the player ("Dragon soul absorbed"). Not saved, a ring of kMaxNotices; the first
        /// is number 1. An empty text is ignored, one over kMaxNoticeBytes is cut.
        void announce(std::string text);
        [[nodiscard]] std::uint64_t noticeCount() const { return mNoticeCount; }
        [[nodiscard]] std::string noticeAt(std::uint64_t number) const;
        static constexpr std::size_t kMaxNotices = 32;
        static constexpr std::size_t kMaxNoticeBytes = 256;

        [[nodiscard]] std::uint64_t revision() const { return mRevision; }

        /// The canonical bytes of the whole state (see ShoutChunk) and the exact inverse; false changes nothing.
        void write(openeden::sdk::ChunkWriter& writer) const;
        [[nodiscard]] bool read(openeden::sdk::ChunkReader& reader, std::uint32_t version, std::string& error);

    private:
        std::vector<FormId> mLearned; ///< ascending
        std::vector<FormId> mUnlocked; ///< ascending
        std::uint32_t mSouls = 0;
        FormId mEquipped = kNullFormId;
        float mCooldown = 0.f;
        float mCooldownTotal = 0.f;
        std::uint64_t mRevision = 0;
        std::vector<std::string> mNotices; ///< task 0253: a ring, number n is at (n - 1) % kMaxNotices
        std::uint64_t mNoticeCount = 0;
    };

    /// The save chunk "shouts" (version 1) of a ShoutState: u32 learned count, the ids ascending, u32 unlocked count,
    /// the ids ascending (every unlocked word is also learned), u32 souls, u32 equipped, f32 cooldown, f32 total.
    class ShoutChunk final : public openeden::sdk::SaveChunkHandler
    {
    public:
        static constexpr std::uint32_t kVersion = 1;
        static constexpr std::string_view kName = "shouts";
        explicit ShoutChunk(ShoutState& state)
            : mState(state)
        {
        }
        void setRefusalSink(std::function<void(const std::string&)> sink) { mSink = std::move(sink); }
        std::string_view chunkName() const override { return kName; }
        std::uint32_t chunkVersion() const override { return kVersion; }
        void write(openeden::sdk::ChunkWriter& writer) override { mState.write(writer); }
        bool read(openeden::sdk::ChunkReader& reader, std::uint32_t version) override;

    private:
        ShoutState& mState;
        std::function<void(const std::string&)> mSink;
    };

    /// The shout's name for the HUD from its editor id: a trailing "Shout" and a trailing number are dropped and the
    /// camel case is split ("UnrelentingForceShout" gives "Unrelenting Force").
    [[nodiscard]] std::string prettyShoutName(std::string_view editorId);
}

#endif
