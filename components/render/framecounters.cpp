#include "framecounters.hpp"

#include <algorithm>
#include <iomanip>
#include <sstream>
#include <utility>

namespace Render
{
    void FrameCounters::add(std::string name, Reader reader)
    {
        std::replace(name.begin(), name.end(), ' ', '_');
        mCounters.push_back({ "ctr_" + name, std::move(reader) });
    }

    std::string FrameCounters::headerText() const
    {
        std::string text;
        for (const Counter& c : mCounters)
            text += " " + c.mName;
        return text;
    }

    std::string FrameCounters::lineText() const
    {
        std::ostringstream out;
        out << std::setprecision(15); // the default 6 digits would write 12345678 as 1.23457e+07
        for (const Counter& c : mCounters)
            out << ' ' << c.mReader();
        return out.str();
    }
}
