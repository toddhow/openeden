#include "capture.hpp"

#include <array>
#include <cstdint>
#include <filesystem>
#include <iostream>
#include <memory>
#include <optional>
#include <sstream>
#include <string>
#include <string_view>
#include <vector>

#include <QApplication>
#include <QHBoxLayout>
#include <QWidget>

#include <boost/program_options.hpp>

#include <vsg/ui/PointerEvent.h>
#include <vsgQt/Window.h>

#include <components/debug/debugging.hpp>
#include <components/debug/debuglog.hpp>
#include <components/fallback/fallback.hpp>
#include <components/fallback/validate.hpp>
#include <components/files/collections.hpp>
#include <components/files/configurationmanager.hpp>
#include <components/files/qtconversion.hpp>
#include <components/settings/settings.hpp>
#include <components/toutf8/toutf8.hpp>

#include "model/doc/document.hpp"
#include "model/doc/documentmanager.hpp"
#include "model/prefs/state.hpp"
#include "view/render/rendercontext.hpp"
#include "view/render/scenehost.hpp"

namespace
{
    namespace bpo = boost::program_options;

    // The same frame size as the comparison captures of the game (scratchpad and tools/render_compare.sh).
    constexpr int sViewWidth = 1280;
    constexpr int sViewHeight = 720;

    struct Request
    {
        std::string mCell;
        std::uint64_t mScreenshotFrame = 0;
        std::uint64_t mQuitFrame = 0;
        int mViews = 1;
        std::optional<std::array<double, 9>> mCamera;
    };

    std::array<double, 9> parseCamera(const std::string& text)
    {
        std::array<double, 9> values{};
        std::istringstream stream(text);
        for (double& value : values)
        {
            std::string number;
            if (!std::getline(stream, number, ','))
                throw std::runtime_error("--camera needs nine numbers: eye, centre and up");
            value = std::stod(number);
        }
        return values;
    }

    /// The window of the views: shown, but never takes focus.
    std::unique_ptr<QWidget> createFrame(int views)
    {
        auto frame = std::make_unique<QWidget>();
        frame->setAttribute(Qt::WA_ShowWithoutActivating);
        frame->setWindowFlag(Qt::WindowDoesNotAcceptFocus);
        frame->setWindowTitle("OpenMW-CS capture");
        frame->setFixedSize(sViewWidth * views, sViewHeight);
        auto layout = new QHBoxLayout(frame.get());
        layout->setContentsMargins(0, 0, 0, 0);
        layout->setSpacing(0);
        return frame;
    }
}

bool CS::isCaptureRequest(int argc, char** argv)
{
    for (int i = 1; i < argc; ++i)
        if (std::string_view(argv[i]) == "--open-cell")
            return true;
    return false;
}

int CS::runCapture(int argc, char** argv)
{
    Files::ConfigurationManager cfgMgr;
    bpo::variables_map variables;
    bpo::options_description desc("Syntax: openmw-cs --open-cell X --auto-screenshot N --auto-quit M");
    auto addOption = desc.add_options();
    addOption("open-cell", bpo::value<std::string>()->required());
    addOption("auto-screenshot", bpo::value<std::uint64_t>()->default_value(0));
    addOption("auto-quit", bpo::value<std::uint64_t>()->required());
    addOption("camera", bpo::value<std::string>());
    addOption("capture-views", bpo::value<int>()->default_value(1));
    addOption("data",
        bpo::value<Files::MaybeQuotedPathContainer>()
            ->default_value(Files::MaybeQuotedPathContainer(), "data")
            ->multitoken()
            ->composing());
    addOption("data-local",
        bpo::value<Files::MaybeQuotedPathContainer::value_type>()->default_value(
            Files::MaybeQuotedPathContainer::value_type(), ""));
    addOption("content",
        bpo::value<std::vector<std::string>>()->default_value(std::vector<std::string>(), "")->multitoken()->composing());
    addOption("encoding", bpo::value<std::string>()->default_value("win1252"));
    addOption("fallback-archive",
        bpo::value<std::vector<std::string>>()->default_value(std::vector<std::string>(), "fallback-archive")->multitoken());
    addOption("fallback",
        bpo::value<Fallback::FallbackMap>()->default_value(Fallback::FallbackMap(), "")->multitoken()->composing(),
        "fallback values");
    Files::ConfigurationManager::addCommonOptions(desc);

    bpo::store(bpo::command_line_parser(argc, argv).options(desc).allow_unregistered().run(), variables);
    cfgMgr.readConfiguration(variables, desc, false);
    bpo::notify(variables);
    Settings::Manager::load(cfgMgr, true);
    Debug::setupLogging(cfgMgr.getLogPath(), "OpenMW-CS");
    Fallback::Map::init(variables["fallback"].as<Fallback::FallbackMap>().mMap);

    Request request;
    request.mCell = variables["open-cell"].as<std::string>();
    request.mScreenshotFrame = variables["auto-screenshot"].as<std::uint64_t>();
    request.mQuitFrame = variables["auto-quit"].as<std::uint64_t>();
    request.mViews = variables["capture-views"].as<int>();
    if (variables.count("camera"))
        request.mCamera = parseCamera(variables["camera"].as<std::string>());

    // As CS::Editor::readConfig.
    const std::filesystem::path resources = variables["resources"].as<Files::MaybeQuotedPath>().u8string();
    Files::PathContainer dataDirs = Files::asPathContainer(variables["data"].as<Files::MaybeQuotedPathContainer>());
    cfgMgr.filterOutNonExistingPaths(dataDirs);
    const std::filesystem::path local = variables["data-local"].as<Files::MaybeQuotedPathContainer::value_type>().u8string();
    if (!local.empty() && std::filesystem::exists(local))
        dataDirs.push_back(local);
    dataDirs.insert(dataDirs.begin(), resources / "vfs");

    const auto content = variables["content"].as<std::vector<std::string>>();
    if (content.empty())
    {
        Log(Debug::Error) << "The configuration has no content files to open";
        return 1;
    }
    const Files::Collections collections(dataDirs);
    std::vector<std::filesystem::path> files;
    for (const std::string& name : content)
        files.push_back(collections.getPath(name));

    CSMPrefs::State settings(cfgMgr);
    CSMDoc::DocumentManager documents(cfgMgr);
    documents.setEncoding(ToUTF8::calculateEncoding(variables["encoding"].as<std::string>()));
    documents.setResourceDir(resources);
    documents.setFileData(dataDirs, variables["fallback-archive"].as<std::vector<std::string>>());

    const std::filesystem::path screenshots = cfgMgr.getScreenshotPath();
    std::filesystem::create_directories(screenshots);

    int result = 0;
    // Declared in this order so that views go first, then what they share, and the frame's windows last.
    std::unique_ptr<QWidget> frame;
    std::unique_ptr<CSVRender::RenderContext> context;
    std::vector<std::unique_ptr<CSVRender::SceneHost>> hosts;
    bool captured = false;
    bool quitting = false;

    const auto finish = [&](int code) {
        if (quitting)
            return;
        quitting = true;
        result = code;
        if (context)
            context->stop();
        QApplication::exit(code);
    };

    QObject::connect(&documents, &CSMDoc::DocumentManager::loadingStopped,
        [&](CSMDoc::Document* document, bool completed, const std::string& error) {
            if (!completed)
            {
                Log(Debug::Error) << "Failed to load the content files: " << error;
                finish(1);
                return;
            }

            CSMWorld::Data& data = document->getData();
            const ESM::RefId cell = CSVRender::SceneHost::findCell(data, request.mCell);
            if (cell.empty())
            {
                Log(Debug::Error) << "There is no cell \"" << request.mCell << "\"";
                finish(1);
                return;
            }

            try
            {
                context = std::make_unique<CSVRender::RenderContext>();
                frame = createFrame(request.mViews);
                frame->show();
                for (int i = 0; i < request.mViews; ++i)
                {
                    // One at a time: RenderContext::createWindow explains why.
                    vsgQt::Window* window = context->createWindow();
                    QWidget* container = QWidget::createWindowContainer(window, frame.get());
                    container->setFixedSize(sViewWidth, sViewHeight);
                    frame->layout()->addWidget(container);
                    container->show();
                    // A window has its size only after this; a SceneHost refuses one with none.
                    QApplication::processEvents();

                    hosts.push_back(std::make_unique<CSVRender::SceneHost>(*context, *window, data));
                    if (hosts.back()->showCell(cell) == 0)
                        Log(Debug::Warning) << "Cell \"" << request.mCell << "\" shows no objects";
                    if (const auto& camera = request.mCamera)
                        hosts.back()->setCamera({ (*camera)[0], (*camera)[1], (*camera)[2] },
                            { (*camera)[3], (*camera)[4], (*camera)[5] }, { (*camera)[6], (*camera)[7], (*camera)[8] });
                }
            }
            catch (const std::exception& e)
            {
                Log(Debug::Error) << "Failed to open the views: " << e.what();
                finish(1);
                return;
            }

            context->start([&] {
                const std::uint64_t frameCount = context->frameCount();
                if (request.mScreenshotFrame != 0 && !captured && frameCount >= request.mScreenshotFrame)
                {
                    captured = true;
                    Log(Debug::Info) << "Auto-screenshot at frame " << frameCount;
                    for (std::size_t i = 0; i < hosts.size(); ++i)
                    {
                        const auto file = screenshots / ("editor" + std::to_string(i) + ".png");
                        if (!hosts[i]->capture(file))
                        {
                            Log(Debug::Error) << "Failed to capture view " << i;
                            finish(1);
                            return;
                        }
                    }
                }
                if (frameCount >= request.mQuitFrame)
                {
                    Log(Debug::Info) << "Auto-quit at frame " << frameCount;
                    finish(0);
                }
            });
        });

    documents.addDocument(files, files.back(), false);

    const int code = QApplication::exec();
    hosts.clear();
    context.reset();
    frame.reset();
    return result != 0 ? result : code;
}
