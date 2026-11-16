#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

#include <openeden/game/world_model.hpp>
#include <openeden/version.hpp>

// What the core needs to play a game's MUSIC (task 0209, milestone M-S7): which music type fits the place and the
// state, and the next track of a type as a plan of VFS files and silences. The engine's director
// (apps/openmw/mwsound/neutralmusic.*) is the one place that decides WHEN to ask, starts the streams through the sound
// manager and times the silences; this interface only supplies the decisions, as plain values.
//
// ABI choice: a NEW interface found by dynamic_cast on the game's registered WorldCellSource (the precedent of
// WeatherSource in weather_source.hpp), so the frozen world_model.hpp and world_source.hpp are untouched. A pack that
// does not implement it leaves the music as it was. Plain values only: no engine, renderer, Jolt, Qt or sound type.
//
// Randomness: the pack never reads a clock. A choice is a pure function of the context, the session seed and the
// `pick` counter the engine keeps (number of plans asked for so far), so a replay of the same play gives the same
// tracks.
//
// Version policy: DRAFT 0.1. Additive changes (a new trailing field with a default that means "not given", a new
// trailing virtual with a "not supported" default) raise the minor number; anything else raises the major number.
// Threading: main thread only, like WorldCellSource.

namespace openeden::sdk::game
{
    inline constexpr Version kMusicSourceVersion{ 0, 1 };

    /// What the engine knows of the moment. The pack adds what only it knows (for example whether the player is in a
    /// fight).
    struct MusicContext
    {
        /// The neutral id of the player's cell (WorldCell::id), kNoWorldId when unknown.
        WorldId cell = kNoWorldId;
        /// The game hour, 0 up to 24.
        float gameHour = 12.f;
        /// The session's music seed (the pack setting); the same seed and the same plays give the same tracks.
        std::uint64_t seed = 1;
        /// How many plans were asked for before this one in the session.
        std::uint32_t pick = 0;
    };

    /// One music type (a MUSC record): what to play here and how it behaves.
    struct MusicTypeDescription
    {
        WorldId id = kNoWorldId;
        std::string editorId;
        /// The game's priority number; a LOWER number is more important (RECALLED-VERIFY, fits the data: combat 2,
        /// special events 1 to 3, exploration 48 to 55). The engine does not use it to refuse a change; it is traced.
        std::uint16_t priority = 0;
        /// The ducking the type asks for, in hundredths of a decibel (the data's unit, RECALLED-VERIFY). Carried and
        /// traced; the sound manager cannot duck a stream yet.
        std::uint16_t duckingCentiDb = 0;
        /// Seconds the previous music takes to fade out when this type takes over.
        float fadeSeconds = 0.f;
        /// The game's flag word: 0x01 plays one selection, 0x02 abrupt transition, 0x04 cycle tracks, 0x08 keep track
        /// order, 0x20 ducks the current track, 0x40 does not queue (RECALLED-VERIFY; the data's values are consistent
        /// with it: every type with a ducking value has 0x20).
        std::uint32_t flags = 0;
        /// True when the type was chosen because the player is in a fight.
        bool combat = false;
        /// Why this type: "cell", "location", "region", "world", "combat" (for the log).
        std::string reason;
        friend bool operator==(const MusicTypeDescription&, const MusicTypeDescription&) = default;
    };

    enum class MusicItemKind : std::uint8_t
    {
        File = 0, ///< stream a file
        Silence = 1, ///< wait `seconds`
    };

    struct MusicItem
    {
        MusicItemKind kind = MusicItemKind::File;
        /// The VFS path of the file in the engine's normal form ("music/tavern/mus_tavern_01.xwm": lower case,
        /// forward slashes); empty for a silence.
        std::string file;
        /// A silence's length in seconds.
        float seconds = 0.f;
        /// The game's own name of the track (for the log).
        std::string name;
        friend bool operator==(const MusicItem&, const MusicItem&) = default;
    };

    /// The most items one plan may hold.
    inline constexpr std::size_t kMaxMusicPlanItems = 64;

    /// What to play next for a type: one track (a single file or a silence) or a palette's group, as a list.
    struct MusicPlan
    {
        std::string trackName;
        std::vector<MusicItem> items;
        /// Seconds the track fades out at its end (the game's own value, 0 for none; carried and traced).
        float fadeOutSeconds = 0.f;
        /// The track's finale file (BNAM, played when the music is cut short by combat ending), empty for none. Carried
        /// and traced; the engine does not play it yet.
        std::string finaleFile;
        friend bool operator==(const MusicPlan&, const MusicPlan&) = default;
    };

    class MusicSource
    {
    public:
        virtual ~MusicSource() = default;

        /// Whether the game's music is switched on (the pack's setting). The engine takes the music channel over only
        /// when this is true.
        virtual bool musicEnabled() = 0;

        /// Whether the source has its data (it reads the records on a worker; false until then).
        /// The engine asks again later.
        virtual bool musicReady() = 0;

        /// The type that fits the context. False when none does (the engine then plays nothing new).
        virtual bool musicType(const MusicContext& context, MusicTypeDescription& out) = 0;

        /// The next plan of a type. False when no track of the type passes its conditions.
        virtual bool musicPlan(WorldId type, const MusicContext& context, MusicPlan& out) = 0;
    };
}
