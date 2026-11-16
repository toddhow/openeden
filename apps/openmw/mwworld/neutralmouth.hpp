#ifndef OPENMW_MWWORLD_NEUTRALMOUTH_H
#define OPENMW_MWWORLD_NEUTRALMOUTH_H

// Task 0235: the mouth of a speaking neutral actor, one driver per speaker. Task 0146 gave the conversation's speaker a
// mouth that follows the `.lip` data of its voice line (or, without lip data, the loudness); the scene speech path of
// task 0194 (barks, scenes) now uses the same drive for every actor it makes speak. This is the pure step: what the
// mouth should be this frame, given what the sound manager says (is the line playing, which file, how far in, how
// loud). Standard library and the lip and morph components only; main thread, no allocation after a line's lip data is
// loaded.

#include <components/lip/lip.hpp>
#include <components/trimorph/mouth.hpp>

#include <functional>
#include <optional>
#include <string>
#include <string_view>

namespace MWWorld
{
    class NeutralMouthDriver
    {
    public:
        /// Reads the lip data of a voice file (a VFS path); nullopt when it has none or it cannot be used. Called once
        /// per file (also when it fails), not every frame.
        using LipLoader = std::function<std::optional<Lip::Track>(const std::string& file)>;

        /// One frame. `saying`: the actor's line is playing now, from `file` (its VFS path), `seconds` into it, with
        /// `loudness` 0..1 (the fallback for a line with no lip data). Returns true when `weights` must be sent to the
        /// renderer: the mouth moved, or it has just closed. False: nothing to send (closed and staying closed).
        [[nodiscard]] bool step(bool saying, std::string_view file, float seconds, float loudness,
            const LipLoader& load, TriMorph::VisemeWeights& weights);

        /// A nonzero weight was sent and not yet closed.
        [[nodiscard]] bool open() const { return mOpen; }
        /// Back to a closed mouth with no line (the caller sends the closed weights itself when open() was true).
        void reset();

    private:
        TriMorph::MouthDrive mMouth;
        bool mOpen = false;
        std::string mFile; ///< the voice file whose lip data mLip holds (or failed to give)
        std::optional<Lip::Track> mLip;
    };
}

#endif
