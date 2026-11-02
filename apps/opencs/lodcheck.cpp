#include "lodcheck.hpp"

#include <cstdint>
#include <filesystem>
#include <string>
#include <string_view>

#include <QImage>
#include <QString>

#include <boost/program_options.hpp>

#include <components/debug/debugging.hpp>
#include <components/debug/debuglog.hpp>
#include <components/files/configurationmanager.hpp>

#include "model/lod/tilemodel.hpp"
#include "view/lod/tileviewer.hpp"

namespace bpo = boost::program_options;

namespace
{
    bool save(const QImage& image, const std::filesystem::path& file)
    {
        if (image.isNull())
            return false;
        if (!image.save(QString::fromStdU16String(file.u16string())))
        {
            Log(Debug::Error) << "Failed to write " << file.string();
            return false;
        }
        Log(Debug::Info) << "Wrote " << file.string();
        return true;
    }
}

bool CS::isLodPackRequest(int argc, char** argv)
{
    for (int i = 1; i < argc; ++i)
        if (std::string_view(argv[i]) == "--open-lod-pack" || std::string_view(argv[i]).starts_with("--open-lod-pack="))
            return true;
    return false;
}

int CS::runLodPackCheck(int argc, char** argv)
{
    Files::ConfigurationManager cfgMgr;
    bpo::variables_map variables;
    bpo::options_description desc("Syntax: eden-studio --open-lod-pack PATH [--lod-camera SPEC] [--lod-tile ROW]");
    auto addOption = desc.add_options();
    addOption("open-lod-pack", bpo::value<std::string>()->required());
    addOption("lod-camera", bpo::value<std::string>()->default_value(""));
    addOption("lod-tile", bpo::value<int>()->default_value(0));
    addOption("auto-screenshot", bpo::value<std::uint64_t>()->default_value(0));
    addOption("auto-quit", bpo::value<std::uint64_t>()->default_value(0));
    Files::ConfigurationManager::addCommonOptions(desc);

    bpo::store(bpo::command_line_parser(argc, argv).options(desc).allow_unregistered().run(), variables);
    cfgMgr.readConfiguration(variables, desc, false);
    bpo::notify(variables);
    Debug::setupLogging(cfgMgr.getLogPath(), "Eden-Studio");

    CSVLod::TileViewer viewer;
    QString error;
    if (!viewer.openPack(QString::fromStdString(variables["open-lod-pack"].as<std::string>()), error))
    {
        Log(Debug::Error) << "Cannot open the LOD pack: " << error.toStdString();
        return 1;
    }

    CSMLod::WorldGridOptions options = viewer.options();
    const std::string problem = CSMLod::parseCameraSpec(variables["lod-camera"].as<std::string>(), options);
    if (!problem.empty())
    {
        Log(Debug::Error) << "--lod-camera: " << problem;
        return 1;
    }
    viewer.setOptions(options);
    viewer.selectRow(variables["lod-tile"].as<int>());
    const CSMLod::WorldGridResult& result = viewer.refreshMap();
    Log(Debug::Info) << "LOD tile viewer: " << CSMLod::summarise(result);

    const std::filesystem::path folder = cfgMgr.getScreenshotPath();
    std::filesystem::create_directories(folder);
    bool ok = save(viewer.worldMapImage(), folder / "lod-map.png");
    ok = save(viewer.tileImage(CSVLod::TileViewer::Picture::Height), folder / "lod-height.png") && ok;
    ok = save(viewer.tileImage(CSVLod::TileViewer::Picture::Normal), folder / "lod-normal.png") && ok;
    // An absent albedo is not an error: S1 packs have none.
    save(viewer.tileImage(CSVLod::TileViewer::Picture::Albedo), folder / "lod-albedo.png");
    // An unshown widget can be grabbed: no window appears on the desktop.
    save(viewer.grab().toImage(), folder / "lod-panel.png");
    return ok ? 0 : 1;
}
