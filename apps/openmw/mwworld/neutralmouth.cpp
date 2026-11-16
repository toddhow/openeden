#include "neutralmouth.hpp"

#include <algorithm>

namespace MWWorld
{
    bool NeutralMouthDriver::step(bool saying, std::string_view file, float seconds, float loudness,
        const LipLoader& load, TriMorph::VisemeWeights& weights)
    {
        weights = {};
        if (saying)
        {
            if (file != mFile)
            {
                mFile = std::string(file);
                mLip.reset();
                if (load)
                    mLip = load(mFile);
            }
            // The line's own lip data at the playback position when it has some; else the loudness of the line.
            if (mLip && Lip::sampleVisemes(*mLip, seconds, weights))
                mMouth.reset();
            else
                weights = TriMorph::toVisemes(mMouth.step(loudness));
        }
        else
        {
            mFile.clear();
            mLip.reset();
            weights = TriMorph::toVisemes(mMouth.step(0.f)); // the jaw closes
        }
        const bool open = std::any_of(weights.begin(), weights.end(), [](float x) { return x > 0.f; });
        if (!open && !mOpen)
            return false; // closed and staying closed: nothing to send
        mOpen = open;
        return true;
    }

    void NeutralMouthDriver::reset()
    {
        mMouth.reset();
        mOpen = false;
        mFile.clear();
        mLip.reset();
    }
}
