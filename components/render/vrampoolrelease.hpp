#ifndef OPENEDEN_RENDER_VRAMPOOLRELEASE_H
#define OPENEDEN_RENDER_VRAMPOOLRELEASE_H

#include <algorithm>

namespace Render::VideoMemory
{
    /*
     * When the video memory scale-back asks VSG to hand its unused device-memory pools back to the driver
     * (task 0080, research note 0042 section 2 item 3). VSG keeps freed capacity in 16 MiB pool entries and returns
     * an entry only through MemoryBufferPools::releaseUnusedPools(), which VSG says to call "on application memory
     * pressure rather than routinely". So the release is asked for only after a reduction, a few times at growing
     * delays (the objects a reduction drops leave the deletion queue a few frames later, and the far field frees its
     * clusters over seconds), and never more often than a minimum interval.
     *
     * Pure: time is an argument. Main thread only; allocates nothing.
     */
    struct PoolReleaseParams
    {
        /// Seconds after a reduction at which an attempt is due.
        double delays[3] = { 1.0, 3.0, 8.0 };
        /// Seconds between two attempts at the least.
        double minInterval = 1.0;
        /// While something stays reduced, an attempt this many seconds after the last one (0: never). A pool entry
        /// drains only when the last live block in it goes, which can be long after the step that caused it.
        double holdInterval = 10.0;
    };

    class PoolReleasePlanner
    {
    public:
        explicit PoolReleasePlanner(PoolReleaseParams params = {})
            : mParams(params)
        {
        }

        /// A reduction was carried out at `now`: the schedule starts again from it.
        void reductionApplied(double now)
        {
            mCount = 0;
            for (const double delay : mParams.delays)
                mDue[mCount++] = now + std::max(0.0, delay);
        }

        /// Whether anything is reduced now (the caller says at every sample); starts the periodic attempts.
        void setReduced(bool reduced) { mReduced = reduced; }

        /// True once when an attempt should be made at `now`; the entries that have come due are then spent.
        bool due(double now)
        {
            const bool scheduled = mCount > 0 && mDue[0] <= now;
            const bool periodic
                = mReduced && mParams.holdInterval > 0.0 && (!mHaveLast || now - mLast >= mParams.holdInterval);
            if (!scheduled && !periodic)
                return false;
            if (mHaveLast && now - mLast < mParams.minInterval)
                return false;
            int kept = 0;
            for (int i = 0; i < mCount; ++i)
                if (mDue[i] > now)
                    mDue[kept++] = mDue[i];
            mCount = kept;
            mLast = now;
            mHaveLast = true;
            return true;
        }

        bool pending() const { return mCount > 0; }

    private:
        PoolReleaseParams mParams;
        double mDue[3] = {}; // ascending
        int mCount = 0;
        double mLast = 0.0;
        bool mHaveLast = false;
        bool mReduced = false;
    };
}

#endif
