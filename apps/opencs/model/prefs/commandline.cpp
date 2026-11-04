#include "commandline.hpp"

#include <components/fallback/validate.hpp>
#include <components/files/configurationmanager.hpp>
#include <components/files/conversion.hpp>

#include <boost/program_options/parsers.hpp>
#include <boost/program_options/positional_options.hpp>

#include <exception>
#include <system_error>

namespace CSMPrefs
{
    namespace
    {
        // The hidden option that collects positional arguments; only one is allowed.
        constexpr const char* fileOption = "file-to-load";
    }

    boost::program_options::options_description makeEditorOptions()
    {
        namespace bpo = boost::program_options;
        bpo::options_description desc("Syntax: eden-studio [options] [content file]\nAllowed options");

        auto addOption = desc.add_options();
        addOption("help", "print help message");
        addOption("data",
            bpo::value<Files::MaybeQuotedPathContainer>()
                ->default_value(Files::MaybeQuotedPathContainer(), "data")
                ->multitoken()
                ->composing(),
            "set data directories (later directories have higher priority)");
        addOption("data-local",
            bpo::value<Files::MaybeQuotedPathContainer::value_type>()->default_value(
                Files::MaybeQuotedPathContainer::value_type(), ""),
            "set local data directory (highest priority)");
        // Read like the game's (apps/openmw/options.cpp). The editor opens files through its dialogues or the
        // positional argument; content= lines are accepted so that the game's openmw.cfg and command line work here.
        addOption("content",
            bpo::value<std::vector<std::string>>()
                ->default_value(std::vector<std::string>(), "")
                ->multitoken()
                ->composing(),
            "content file(s): esm/esp, or omwgame/omwaddon/omwscripts");
        addOption("encoding", bpo::value<std::string>()->default_value("win1252"),
            "character encoding used in the content files: win1250, win1251 or win1252");
        // The same names as the game's (apps/openmw/options.cpp): extension= lines in openmw.cfg are shared.
        addOption("extension",
            bpo::value<std::vector<std::string>>()
                ->default_value(std::vector<std::string>(), "")
                ->multitoken()
                ->composing(),
            "OpenEden extension ID(s) to enable (extension= lines in openmw.cfg)");
        addOption("fallback-archive",
            bpo::value<std::vector<std::string>>()
                ->default_value(std::vector<std::string>(), "fallback-archive")
                ->multitoken(),
            "set fallback BSA archives (later archives have higher priority)");
        addOption("fallback",
            bpo::value<Fallback::FallbackMap>()->default_value(Fallback::FallbackMap(), "")->multitoken()->composing(),
            "fallback values");
        Files::ConfigurationManager::addCommonOptions(desc);
        return desc;
    }

    CommandLine parseCommandLine(
        int argc, const char* const argv[], const boost::program_options::options_description& description)
    {
        namespace bpo = boost::program_options;
        CommandLine result;

        bpo::options_description all(description);
        all.add_options()(fileOption, bpo::value<std::vector<std::string>>()->multitoken());
        bpo::positional_options_description positional;
        positional.add(fileOption, -1);

        try
        {
            // Unlike Files::parseArgs, no allow_unregistered: an option the editor does not know is an error, not a
            // file to open.
            bpo::store(
                bpo::command_line_parser(argc, argv).options(all).positional(positional).run(), result.mVariables);
        }
        catch (const std::exception& e)
        {
            result.mErrors.emplace_back(e.what());
            result.mVariables.clear();
            return result;
        }

        result.mHelp = result.mVariables.count("help") > 0;

        const auto files = result.mVariables.find(fileOption);
        if (files != result.mVariables.end())
        {
            const auto& names = files->second.as<std::vector<std::string>>();
            if (names.size() > 1)
                result.mErrors.emplace_back("only one content file can be opened, got " + std::to_string(names.size()));
            else if (!names.empty())
            {
                const std::filesystem::path file(names.front());
                std::error_code error;
                if (std::filesystem::is_regular_file(file, error))
                    result.mFileToLoad = file;
                else
                    result.mErrors.emplace_back("\"" + names.front() + "\" is not a file (and not an option)");
            }
            result.mVariables.erase(fileOption);
        }

        // The list options take every following word, as the game's do, so a file written after one of them is read as
        // one of its values. Catch the two cases where that cannot be meant.
        const auto replaced = result.mVariables.find("replace");
        if (replaced != result.mVariables.end() && !replaced->second.defaulted())
        {
            for (const std::string& name : replaced->second.as<std::vector<std::string>>())
            {
                if (description.find_nothrow(name, false) == nullptr)
                    result.mErrors.emplace_back(
                        "--replace names no option: \"" + name + "\" (a content file goes first, or after --)");
            }
        }
        const auto configs = result.mVariables.find("config");
        if (configs != result.mVariables.end() && !configs->second.defaulted())
        {
            for (const Files::MaybeQuotedPath& path : configs->second.as<Files::MaybeQuotedPathContainer>())
            {
                std::error_code error;
                if (std::filesystem::is_regular_file(path, error))
                    result.mErrors.emplace_back("--config names a file, not a folder: \""
                        + Files::pathToUnicodeString(path) + "\" (a content file goes first, or after --)");
            }
        }

        if (!result.mErrors.empty())
        {
            result.mFileToLoad.clear();
            result.mVariables.clear();
        }
        return result;
    }
}
