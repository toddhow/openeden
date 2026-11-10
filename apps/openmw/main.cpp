#include <components/debug/debugging.hpp>
#include <components/debug/debuglog.hpp>
#include <components/fallback/fallback.hpp>
#include <components/fallback/validate.hpp>
#include <components/files/configurationmanager.hpp>
#include <components/misc/rng.hpp>
#include <components/platform/platform.hpp>
#include <components/settings/settings.hpp>
#include <components/version/version.hpp>

#include <game_morrowind/dialogue_rules.hpp>
#include <game_morrowind/game_rules.hpp>
#include <game_morrowind/pack.hpp>

#include "mwdialogue/dialoguefactory.hpp"
#include "mwrender/lodbakemode.hpp"
#include "mwworld/gamerules.hpp"
#include "options.hpp"
#include "vsgengine.hpp"

#include <boost/program_options/variables_map.hpp>

#if defined(_WIN32)
#include <components/misc/windows.hpp>
#include <cstdlib>

extern "C" __declspec(dllexport) DWORD AmdPowerXpressRequestHighPerformance = 0x00000001;
extern "C" __declspec(dllexport) DWORD NvOptimusEnablement = 0x00000001;
#endif

#include <filesystem>
#include <memory>

/**
 * \brief Parses application command line and calls \ref Cfg::ConfigurationManager
 * to parse configuration files.
 */
std::optional<OMW::Arguments> parseArgs(int argc, char** argv, Files::ConfigurationManager& cfgMgr)
{
    // Create a local alias for brevity
    namespace bpo = boost::program_options;
    typedef std::vector<std::string> StringsVector;

    bpo::options_description desc = OpenMW::makeOptionsDescription();
    bpo::variables_map variables;

    Files::parseArgs(argc, argv, variables, desc);
    bpo::notify(variables);

    if (variables.count("help"))
    {
        Debug::getRawStdout() << desc << std::endl;
        return {};
    }

    if (variables.count("version"))
    {
        Debug::getRawStdout() << Version::getOpenmwVersionDescription() << std::endl;
        return {};
    }

    cfgMgr.processPaths(variables, std::filesystem::current_path());
    cfgMgr.readConfiguration(variables, desc);

    Debug::setupLogging(cfgMgr.getLogPath(), "OpenMW");
    Log(Debug::Info) << Version::getOpenmwVersionDescription();

    Settings::Manager::load(cfgMgr);

    OMW::Arguments args;
    std::string encoding(variables["encoding"].as<std::string>());
    Log(Debug::Info) << ToUTF8::encodingUsingMessage(encoding);
    args.encoding = ToUTF8::calculateEncoding(encoding);

    Files::PathContainer dataDirs(asPathContainer(variables["data"].as<Files::MaybeQuotedPathContainer>()));

    Files::PathContainer::value_type local(variables["data-local"].as<Files::MaybeQuotedPathContainer::value_type>());
    if (!local.empty())
        dataDirs.push_back(local);

    cfgMgr.filterOutNonExistingPaths(dataDirs);

    args.resourceDir = variables["resources"].as<Files::MaybeQuotedPath>();
    args.dataDirs = dataDirs;
    args.dataLocal = local;

    args.archives = variables["fallback-archive"].as<StringsVector>();

    StringsVector content = variables["content"].as<StringsVector>();
    if (content.empty())
    {
        Log(Debug::Error) << "No content file given (esm/esp, nor omwgame/omwaddon). Aborting...";
        return {};
    }
    // OpenMW 0.51 always loads its built-in Lua scripts first.
    content.insert(content.begin(), "builtin.omwscripts");
    std::set<std::string> contentDedupe{ "builtin.omwscripts" };
    for (auto contentFile = content.begin() + 1; contentFile != content.end(); ++contentFile)
    {
        if (!contentDedupe.insert(*contentFile).second)
        {
            Log(Debug::Error) << "Content file specified more than once: " << *contentFile << ". Aborting...";
            return {};
        }
    }

    args.contentFiles = content;
    // openeden-vsg: groundcover content files (task 0012), as OpenMW 0.51 passed them to the engine.
    args.groundcoverFiles = variables["groundcover"].as<StringsVector>();

    // startup-settings
    args.saveGameFile = variables["load-savegame"].as<Files::MaybeQuotedPath>();
    args.cellName = variables["start"].as<std::string>();
    args.skipMenu = variables["skip-menu"].as<bool>() || variables["new-game"].as<bool>();

    // scripts
    args.compileAll = variables["script-all"].as<bool>();
    args.compileAllDialogue = variables["script-all-dialogue"].as<bool>();
    args.scriptConsoleMode = variables["script-console"].as<bool>();
    args.startupScript = variables["script-run"].as<std::string>();
    args.autoScreenshotFrame = variables["auto-screenshot"].as<unsigned int>();
    args.autoQuitFrame = variables["auto-quit"].as<unsigned int>();
    args.bakeGiCell = variables["bake-gi"].as<std::string>();
    // --bake-out is the general name; --bake-gi-out is the older one of the same folder and keeps working.
    args.bakeGiOut = variables["bake-out"].as<std::string>();
    if (args.bakeGiOut.empty())
        args.bakeGiOut = variables["bake-gi-out"].as<std::string>();
    args.bakeGiExterior = variables["bake-gi-exterior"].as<std::string>();
    args.bakeLodTerrain = variables["bake-lod-terrain"].as<bool>();
    args.bakeLodObjects = variables["bake-lod-objects"].as<bool>();
    args.analyseBodies = variables["analyse-bodies"].as<bool>();
    args.bakeBodies = variables["bake-bodies"].as<bool>();
    args.checkBodies = variables["check-bodies"].as<bool>();
    const bool bodyRun = args.analyseBodies || args.bakeBodies || args.checkBodies;
    if (!args.bakeGiExterior.empty() || args.bakeLodTerrain || args.bakeLodObjects || bodyRun)
        args.skipMenu = true;
    MWRender::sLodBakeRun = args.bakeLodTerrain || args.bakeLodObjects || bodyRun;
    if (!args.bakeGiCell.empty())
    {
        // The baker needs the cell loaded: start in it, without the menu.
        args.cellName = args.bakeGiCell;
        args.skipMenu = true;
    }
    args.warningsMode = variables["script-warn"].as<int>();

    // other settings
    Fallback::Map::init(variables["fallback"].as<Fallback::FallbackMap>().mMap);
    args.useSound = !variables["no-sound"].as<bool>();
    if (!variables["no-extensions"].as<bool>())
    {
        args.extensions = variables["extension"].as<StringsVector>();
        args.gamePack = variables["game"].as<std::string>();
    }
    // The Morrowind pack, usable by game=morrowind: the host starts it before the managers and windows are built and
    // takes the game rules from it (stage I1b). Without game= the lines in runApplication install the same rules.
    args.builtInGamePacks.push_back({ game_morrowind::packManifest(), &game_morrowind::makePackExtension });
    args.activationDistanceOverride = variables["activate-dist"].as<int>();
    args.randomSeed = variables["random-seed"].as<unsigned int>();
    args.grabMouse = !variables["no-grab"].as<bool>();
    return args;
}

int runApplication(int argc, char* argv[])
{
    Platform::init();

    // The Morrowind game pack's rules, handed over through GameRules (task 0084 leftovers, stage I1) before the engine
    // makes its first window (the crafting windows begin their sessions when they are built). openmw-lib reaches them
    // only through the contract; this executable is the one place that names the pack. They are installed whatever
    // game= says, so a default session plays Morrowind exactly as before; the lifecycle does not carry them yet (see
    // the stage log of task 0084).
    const std::unique_ptr<openeden::sdk::game::GameRules> morrowindRules = game_morrowind::makeGameRules();
    MWWorld::installGameRules(*morrowindRules);
    // and its journal and dialogue manager (task 0085 move 3), made later by the Game state through this hook.
    MWDialogue::installFactories({ &game_morrowind::makeJournal, &game_morrowind::makeDialogueManager,
        &game_morrowind::compileAllDialogueScripts });

    Files::ConfigurationManager cfgMgr;
    OMW::Engine engine(cfgMgr);

    if (auto args = parseArgs(argc, argv, cfgMgr))
    {
        engine.go(*args);
    }

    return engine.exitCode();
}

#ifdef ANDROID
extern "C" int SDL_main(int argc, char** argv)
#else
int main(int argc, char** argv)
#endif
{
    return Debug::wrapApplication(&runApplication, argc, argv, "OpenMW");
}

#if defined(_WIN32) && !defined(_CONSOLE)
int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance, LPSTR lpCmdLine, int nShowCmd)
{
    return main(__argc, __argv);
}
#endif
