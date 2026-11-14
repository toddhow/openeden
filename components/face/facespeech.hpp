#ifndef OPENEDEN_COMPONENTS_FACE_FACESPEECH_H
#define OPENEDEN_COMPONENTS_FACE_FACESPEECH_H

// OpenEden facial speech animation, task 0071 stage e (engine half, engine-independent part): where the curves of a
// voice line live, loading them through a caller-supplied reader, a small cache, and the per-NPC playback state that
// turns (voice path, stream position) into "talk" and "blink" amounts for a stock head. No engine, VFS or sound type:
// the engine passes a ReadFile callback and the stream position. Main thread only (the caller's rule, not enforced).

#include "facecurves.hpp"
#include "facesample.hpp"

#include <components/misc/speechframe.hpp>

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace Face
{
    /// Eden Archive entry type of the packed binary form (docs/spec/eden-archive-format-v0.1.md, section 11.2).
    inline constexpr std::uint16_t sEntryTypeFaceCurves = 0x0100;

    inline constexpr std::string_view sBinaryExtension = ".facecurves";
    inline constexpr std::string_view sJsonExtension = ".facecurves.json";

    // The frame type and the animation-time mapping live in the base layer (components/misc/speechframe.hpp) so that
    // the engine's MWAnim::Face can use them; the face library re-exports them.
    using Misc::speechAnimTime;
    using Misc::SpeechFrame;
    inline constexpr float sBlinkOverridesTalk = Misc::sBlinkOverridesTalk;

    /// Cache size limit (entries, misses included).
    inline constexpr std::size_t sDefaultCacheEntries = 64;

    struct CurvePaths
    {
        std::string mBinary; // empty when the voice path is empty
        std::string mJson;
    };

    /// The curve files of a voice line: the path with the last extension of its last component replaced (or appended
    /// when there is none). The path is used as given (the VFS normalises case).
    CurvePaths curvePathsFor(std::string_view voicePath);

    /// Reads a whole file through the VFS (loose or packed); nullopt when it does not exist.
    using ReadFile = std::function<std::optional<std::vector<std::uint8_t>>(std::string_view path)>;

    struct LoadResult
    {
        std::shared_ptr<const Curves> mCurves; // null: none found or unusable
        std::string mWarning; // non-empty when a file existed but could not be used
    };

    /// Binary first, then JSON. The first file that exists decides: if it is invalid the result has no curves and a
    /// located warning (the other form is not tried, so a broken file is noticed instead of hidden).
    LoadResult loadSpeechCurves(std::string_view voicePath, const ReadFile& read);

    class SpeechCurveCache
    {
    public:
        explicit SpeechCurveCache(std::size_t maxEntries = sDefaultCacheEntries);
        /// Cached lookup (a miss is cached too). `warning`, if given, receives the load warning of a fresh load.
        std::shared_ptr<const Curves> find(
            std::string_view voicePath, const ReadFile& read, std::string* warning = nullptr);
        std::size_t size() const { return mEntries.size(); }
        std::size_t loadCount() const { return mLoads; }

    private:
        struct Entry
        {
            std::string mPath;
            std::shared_ptr<const Curves> mCurves;
            std::uint64_t mLastUse = 0;
        };
        std::vector<Entry> mEntries;
        std::size_t mMax;
        std::size_t mLoads = 0;
        std::uint64_t mClock = 0;
    };

    /// One per NPC. update() allocates only when the voice path changes.
    class SpeechPlayback
    {
    public:
        /// voicePath empty = nothing is being said. streamSeconds < 0 is treated as 0.
        SpeechFrame update(SpeechCurveCache& cache, const ReadFile& read, std::string_view voicePath,
            double streamSeconds, std::string* warning = nullptr);
        void reset();

    private:
        std::string mPath;
        std::shared_ptr<const Curves> mCurves;
        std::optional<Driver> mDriver;
    };
}

#endif
