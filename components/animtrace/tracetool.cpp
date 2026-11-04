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
        std::vector<std::string> paths;
        std::vector<std::pair<std::string, std::string>> meta;
        bool json = false;
        bool summary = false;
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
            else if (arg == "--meta" && hasValue)
            {
                const std::string& pair = args[++i];
                const std::size_t equals = pair.find('=');
                if (equals == std::string::npos || equals == 0)
                {
                    err << "creativemotioncheck: --meta needs <key>=<value>\n";
                    return 2;
                }
                meta.emplace_back(pair.substr(0, equals), pair.substr(equals + 1));
            }
            else if (arg == "--summary")
            {
                summary = true;
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
            else
            {
                paths.push_back(arg);
            }
        }
        if (paths.empty())
        {
            err << "usage: creativemotioncheck [--json] [--ref <actor>] [--label <text>] [--window <frames>] "
                   "[--meta <key>=<value>]... [--summary] <openmw.log>...\n";
            return 2;
        }
        if (!summary && paths.size() > 1)
        {
            err << "creativemotioncheck: more than one log file (use --summary)\n";
            return 2;
        }
        std::vector<Report> reports;
        for (const std::string& path : paths)
        {
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
            reports.push_back(measure(trace, options, label));
            reports.back().mMeta = meta;
        }
        if (summary)
        {
            const Summary result = summarise(reports, label);
            out << (json ? formatSummaryJson(result) : formatSummaryText(result));
            return 0;
        }
        out << (json ? formatJson(reports.front()) : formatText(reports.front()));
        return 0;
    }
}
