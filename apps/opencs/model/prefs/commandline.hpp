#ifndef CSM_PREFS_COMMANDLINE_H
#define CSM_PREFS_COMMANDLINE_H

#include <filesystem>
#include <string>
#include <vector>

#include <boost/program_options/options_description.hpp>
#include <boost/program_options/variables_map.hpp>

namespace CSMPrefs
{
    /// What Eden Studio's own command line asked for. The unattended modes (--open-cell captures and
    /// --open-lod-pack) parse their own options before the editor starts (capture.cpp, lodcheck.cpp).
    struct CommandLine
    {
        /// The options given, as the game stores them (apps/openmw/main.cpp, parseArgs). The config files are read
        /// later, by CS::Editor::readConfiguration, which adds their values below these.
        boost::program_options::variables_map mVariables;

        /// The one positional argument: a content file to open. Empty when none was given.
        std::filesystem::path mFileToLoad;

        bool mHelp = false;

        /// Not empty: the command line was refused, and nothing else here may be used.
        std::vector<std::string> mErrors;
    };

    /// The options the editor accepts: the game's configuration options (data, data-local, content, encoding,
    /// extension, fallback-archive, fallback and Files::ConfigurationManager::addCommonOptions: config, replace,
    /// user-data, resources) and help.
    boost::program_options::options_description makeEditorOptions();

    /// Parses the editor's command line against \a description. Unknown options, a second positional argument and a
    /// positional argument that is not an existing file are refused with a message each.
    CommandLine parseCommandLine(
        int argc, const char* const argv[], const boost::program_options::options_description& description);
}

#endif
