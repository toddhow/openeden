#include "options.hpp"

#include <components/fallback/validate.hpp>
#include <components/files/configurationmanager.hpp>
#include <components/misc/rng.hpp>

namespace
{
    namespace bpo = boost::program_options;
    typedef std::vector<std::string> StringsVector;
}

namespace OpenMW
{
    bpo::options_description makeOptionsDescription()
    {
        bpo::options_description desc("Syntax: openmw <options>\nAllowed options");
        Files::ConfigurationManager::addCommonOptions(desc);

        auto addOption = desc.add_options();
        addOption("help", "print help message");
        addOption("version", "print version information and quit");

        addOption("data",
            bpo::value<Files::MaybeQuotedPathContainer>()
                ->default_value(Files::MaybeQuotedPathContainer(), "data")
                ->multitoken()
                ->composing(),
            "set data directories (later directories have higher priority)");

        addOption("data-local", bpo::value<Files::MaybeQuotedPath>()->default_value(Files::MaybeQuotedPath(), ""),
            "set local data directory (highest priority)");

        addOption("fallback-archive",
            bpo::value<StringsVector>()->default_value(StringsVector(), "fallback-archive")->multitoken()->composing(),
            "set fallback BSA archives (later archives have higher priority)");

        addOption("start", bpo::value<std::string>()->default_value(""), "set initial cell");

        addOption("content", bpo::value<StringsVector>()->default_value(StringsVector(), "")->multitoken()->composing(),
            "content file(s): esm/esp, or omwgame/omwaddon/omwscripts");

        addOption("groundcover",
            bpo::value<StringsVector>()->default_value(StringsVector(), "")->multitoken()->composing(),
            "groundcover content file(s): esm/esp, or omwgame/omwaddon");

        addOption("extension",
            bpo::value<StringsVector>()->default_value(StringsVector(), "")->multitoken()->composing(),
            "OpenEden extension ID(s) to enable; adds to the extension= lines in openmw.cfg");

        addOption("game", bpo::value<std::string>()->default_value(""),
            "ID of the OpenEden game pack of this session (a pack of kind \"game\"); one per session, recorded in "
            "saves");

        addOption("no-extensions", bpo::value<bool>()->implicit_value(true)->default_value(false),
            "load no OpenEden extensions, whatever is enabled");

        addOption("no-sound", bpo::value<bool>()->implicit_value(true)->default_value(false), "disable all sounds");

        addOption("script-all", bpo::value<bool>()->implicit_value(true)->default_value(false),
            "compile all scripts (excluding dialogue scripts) at startup");

        addOption("script-all-dialogue", bpo::value<bool>()->implicit_value(true)->default_value(false),
            "compile all dialogue scripts at startup");

        addOption("script-console", bpo::value<bool>()->implicit_value(true)->default_value(false),
            "enable console-only script functionality");

        addOption("script-run", bpo::value<std::string>()->default_value(""),
            "select a file containing a list of console commands that is executed on startup");

        addOption("script-warn", bpo::value<int>()->implicit_value(1)->default_value(1),
            "handling of warnings when compiling scripts\n"
            "\t0 - ignore warnings\n"
            "\t1 - show warnings but consider script as correctly compiled anyway\n"
            "\t2 - treat warnings as errors");

        addOption("load-savegame", bpo::value<Files::MaybeQuotedPath>()->default_value(Files::MaybeQuotedPath(), ""),
            "load a save game file on game startup (specify an absolute filename or a filename relative to the current "
            "working directory)");

        addOption("skip-menu", bpo::value<bool>()->implicit_value(true)->default_value(false),
            "skip main menu on game startup");

        addOption("new-game", bpo::value<bool>()->implicit_value(true)->default_value(false),
            "run new game sequence (ignored if skip-menu=0)");

        addOption("auto-screenshot", bpo::value<unsigned int>()->default_value(0),
            "fire a screenshot after this many frames of Running state (0 = disabled)");

        addOption("auto-quit", bpo::value<unsigned int>()->default_value(0),
            "request quit after this many frames of Running state (0 = disabled)");

        addOption("bake-gi", bpo::value<std::string>()->default_value(""),
            "bake light probes for this interior cell (needs the game data; writes openeden/probes/ and quits)");

        addOption("bake-gi-exterior", bpo::value<std::string>()->default_value(""),
            "bake the sky visibility of exterior cells, \"<x>,<y>\" or \"<x0>,<y0>:<x1>,<y1>\" (needs the game data; "
            "writes openeden/probes/exterior/ and quits)");

        addOption("bake-lod-terrain", bpo::bool_switch()->default_value(false),
            "bake the terrain tiles of the world LOD into openeden/lod/terrain.edenpak (draft layout; needs the game "
            "data; quits when done)");

        addOption("bake-lod-objects", bpo::bool_switch()->default_value(false),
            "bake the object clusters of the world LOD into openeden/lod/objects.edenpak (DRAFT layout; needs the game "
            "data; quits when done)");

        addOption("analyse-bodies", bpo::bool_switch()->default_value(false),
            "measure the stock body parts and the seams between them and write analysis/bodies.json and bodies.txt "
            "to the folder of --bake-gi-out or the user data folder (read-only; needs the game data; quits when "
            "done)");

        addOption("bake-bodies", bpo::bool_switch()->default_value(false),
            "build the merged body of every race and sex from the stock parts and publish them as a derived cache "
            "in openeden/bodies of the folder of --bake-gi-out or the user data folder (DRAFT layout; needs the "
            "game data; quits when done)");

        addOption("check-bodies", bpo::bool_switch()->default_value(false),
            "read the merged body cache back and compare it with the stock parts, writing openeden/bodies/check.txt "
            "(read-only; needs the game data; quits when done)");

        addOption("bake-gi-out", bpo::value<std::string>()->default_value(""),
            "folder the baked probes are written to (default: the user data folder); see --bake-out");

        addOption("bake-out", bpo::value<std::string>()->default_value(""),
            "folder every bake and body analysis writes to (default: the user data folder); the older name of the "
            "same option is --bake-gi-out");

        addOption("encoding", bpo::value<std::string>()->default_value("win1252"),
            "Character encoding used in OpenMW game messages:\n"
            "\n\twin1250 - Central and Eastern European such as Polish, Czech, Slovak, Hungarian, Slovene, Bosnian, "
            "Croatian, Serbian (Latin script), Romanian and Albanian languages\n"
            "\n\twin1251 - Cyrillic alphabet such as Russian, Bulgarian, Serbian Cyrillic and other languages\n"
            "\n\twin1252 - Western European (Latin) alphabet, used by default");

        addOption("fallback",
            bpo::value<Fallback::FallbackMap>()->default_value(Fallback::FallbackMap(), "")->multitoken()->composing(),
            "fallback values");

        addOption("no-grab", bpo::value<bool>()->implicit_value(true)->default_value(false), "Don't grab mouse cursor");

        addOption("export-fonts", bpo::value<bool>()->implicit_value(true)->default_value(false),
            "Export Morrowind .fnt fonts to PNG image and XML file in current directory");

        addOption("activate-dist", bpo::value<int>()->default_value(-1), "activation distance override");

        addOption("random-seed", bpo::value<unsigned int>()->default_value(Misc::Rng::generateDefaultSeed()),
            "seed value for random number generator");

        return desc;
    }
}
