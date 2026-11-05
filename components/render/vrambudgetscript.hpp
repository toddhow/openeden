#ifndef OPENEDEN_RENDER_VRAMBUDGETSCRIPT_H
#define OPENEDEN_RENDER_VRAMBUDGETSCRIPT_H

#include <cstdint>
#include <cstdlib>
#include <string>
#include <vector>

namespace Render::VideoMemory
{
    /*
     * A scripted budget for in-game proof runs (task 0080): `OPENEDEN_VRAM_BUDGET_SCRIPT="0:900,40:380,100:900"` says
     * the card has 900 MiB from second 0, 380 MiB from second 40 and 900 MiB again from second 100 (seconds since
     * the scale-back started). It only replaces the driver's budget when `[VideoMemory] budget = auto`; unset or
     * invalid, nothing changes. Test aid, like the other OPENEDEN_* trace variables.
     */
    struct BudgetStep
    {
        double seconds = 0.0;
        std::int64_t mib = 0;
    };

    /// Parses "t:mib,t:mib,..." (times ascending, mib positive). Returns an empty list for any problem.
    inline std::vector<BudgetStep> parseBudgetScript(const std::string& text)
    {
        std::vector<BudgetStep> steps;
        if (!text.empty() && text.back() == ',')
            return {};
        std::size_t pos = 0;
        while (pos < text.size())
        {
            std::size_t end = text.find(',', pos);
            if (end == std::string::npos)
                end = text.size();
            const std::string item = text.substr(pos, end - pos);
            const std::size_t colon = item.find(':');
            if (colon == std::string::npos || colon == 0 || colon + 1 >= item.size())
                return {};
            char* stop = nullptr;
            const double seconds = std::strtod(item.c_str(), &stop);
            if (stop != item.c_str() + colon || seconds < 0.0)
                return {};
            const long long mib = std::strtoll(item.c_str() + colon + 1, &stop, 10);
            if (*stop != '\0' || mib <= 0)
                return {};
            if (!steps.empty() && seconds <= steps.back().seconds)
                return {};
            BudgetStep step;
            step.seconds = seconds;
            step.mib = mib;
            steps.push_back(step);
            pos = end + 1;
        }
        return steps;
    }

    /// The budget in MiB at `seconds`: the last step that has begun, -1 before the first or for an empty script.
    inline std::int64_t budgetAt(const std::vector<BudgetStep>& steps, double seconds)
    {
        std::int64_t result = -1;
        for (const BudgetStep& step : steps)
        {
            if (step.seconds > seconds)
                break;
            result = step.mib;
        }
        return result;
    }
}

#endif
