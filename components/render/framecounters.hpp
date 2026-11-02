#ifndef OPENEDEN_RENDER_FRAMECOUNTERS_H
#define OPENEDEN_RENDER_FRAMECOUNTERS_H

#include <functional>
#include <string>
#include <vector>

namespace Render
{
    /*
     * Task 0054 (S0). Named numbers that the frame stats file gets as extra columns when [Rendering] frame counters is
     * on. Nothing registered means empty text for both functions, so the file is exactly as before. Owned by the
     * Engine; the functions it holds are called on the main thread, once per line.
     */
    class FrameCounters
    {
    public:
        using Reader = std::function<double()>;

        // The column is called ctr_<name> with spaces turned into underscores.
        void add(std::string name, Reader reader);
        bool empty() const { return mCounters.empty(); }
        // " ctr_a ctr_b" (a leading space before each), for the header line.
        std::string headerText() const;
        // " 1.5 3" the current values, in the same order, for one frame's line.
        std::string lineText() const;

    private:
        struct Counter
        {
            std::string mName;
            Reader mReader;
        };
        std::vector<Counter> mCounters;
    };
}

#endif
