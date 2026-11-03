#include "tracetool.hpp"

#include "tracemetrics.hpp"

#include <charconv>
#include <fstream>
#include <ostream>

namespace AnimTrace
{
    namespace
    {
        bool readCount(const std::string& text, std::size_t& out)
        {
            const char* end = text.data() + text.size();
            const auto [stop, error] = std::from_chars(text.data(), end, out);
            return error == std::errc() && stop == end && !text.empty();
        }
    }

    int runTraceTool(const std::vector<std::string>& args, std::ostream& out, std::ostream& err)
    {
        Options options;
        std::string label;
        std::string path;
        bool json = false;
        for (std::size_t i = 0; i < args.size(); ++i)
        {
            const std::string& arg = args[i];
            const bool hasValue = i + 1 < args.size();
            if (arg == "--json")
            {
                json = true;
            }
            else if (arg == "--ref" && hasValue)
            {
                options.mRef = args[++i];
            }
            else if (arg == "--label" && hasValue)
            {
                label = args[++i];
            }
            else if (arg == "--window" && hasValue)
            {
                if (!readCount(args[++i], options.mWindowFrames))
                {
                    err << "creativemotioncheck: --window needs a whole number of frames\n";
                    return 2;
                }
            }
            else if (!arg.empty() && arg[0] == '-')
            {
                err << "creativemotioncheck: unknown or incomplete option " << arg << "\n";
                return 2;
            }
            else if (path.empty())
            {
                path = arg;
            }
            else
            {
                err << "creativemotioncheck: more than one log file\n";
                return 2;
            }
        }
        if (path.empty())
        {
            err << "usage: creativemotioncheck [--json] [--ref <actor>] [--label <text>] [--window <frames>] "
                   "<openmw.log>\n";
            return 2;
        }
        std::ifstream log(path, std::ios::binary);
        if (!log)
        {
            err << "creativemotioncheck: cannot read " << path << "\n";
            return 2;
        }
        const Trace trace = parseLog(log, options);
        if (trace.mSamples.empty())
        {
            err << "creativemotioncheck: no CMTRACE lines of \"" << options.mRef << "\" in " << path
                << " (is [Game] creativemotion animation debug on?)\n";
            return 1;
        }
        const Report report = measure(trace, options, label);
        out << (json ? formatJson(report) : formatText(report));
        return 0;
    }
}
