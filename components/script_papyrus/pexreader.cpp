#include "pexfile.hpp"

namespace ScriptPapyrus::Pex
{
    // Stage a stub: the reader is written in stage b.
    ParseResult parse(std::span<const std::uint8_t>, std::string_view name, const ParseOptions&) noexcept
    {
        ParseResult result;
        openeden::script::ErrorReport report;
        report.script = std::string(name);
        report.message = "the .pex reader is not implemented yet";
        result.mReports.push_back(std::move(report));
        return result;
    }

    std::string lowered(std::string_view text)
    {
        std::string result(text);
        for (char& c : result)
        {
            if (c >= 'A' && c <= 'Z')
            {
                c = static_cast<char>(c - 'A' + 'a');
            }
        }
        return result;
    }
}
