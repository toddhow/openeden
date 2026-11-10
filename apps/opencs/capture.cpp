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
#include <QMetaObject>
#include <QMouseEvent>
#include <QUndoStack>
#include <QWidget>

#include <boost/program_options.hpp>

#include <QPoint>
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
#include "model/world/data.hpp"
#include "model/world/idcollection.hpp"
#include "model/world/land.hpp"
#include "model/world/record.hpp"
#include "view/render/cameracontroller.hpp"
#include "view/render/camerapose.hpp"
#include "view/render/editmode.hpp"
#include "view/render/mask.hpp"
#include "view/render/meshscene.hpp"
#include "view/render/meshviewwidget.hpp"
#include "view/render/object.hpp"
#include "view/render/objectmarker.hpp"
#include "view/render/pagedworldspacewidget.hpp"
#include "view/render/previewwidget.hpp"
#include "view/render/rendercontext.hpp"
#include "view/render/scenehost.hpp"
#include "view/render/terrainshapemode.hpp"
#include "view/render/terraintexturemode.hpp"
#include "view/render/unpagedworldspacewidget.hpp"
#include "view/widget/scenetoolbar.hpp"
#include "view/widget/scenetoolmode.hpp"
#include "view/widget/scenetooltoggle2.hpp"

namespace
{
    namespace bpo = boost::program_options;

    // The same frame size as the comparison captures of the game (scratchpad and tools/render_compare.sh).
    constexpr int sViewWidth = 1280;
    constexpr int sViewHeight = 720;
    // Well before any capture a script is for.
    constexpr std::uint64_t sCameraScriptFrame = 5;
    // An arrow's tool tip is a long text.
    constexpr std::size_t sMaxLoggedTag = 40;
    constexpr int sToolbarButtonSize = 48;

    struct Request
    {
        std::string mCell;
        std::uint64_t mScreenshotFrame = 0;
        std::uint64_t mQuitFrame = 0;
        int mViews = 1;
        std::optional<std::array<double, 9>> mCamera;
        std::string mLighting;
        std::optional<double> mFieldOfView;
        int mNeighbours = 0;
        // Resized to each of these after a capture, capturing again each time.
        std::vector<std::pair<int, int>> mResizes;
        // How many views close after the first capture, to check those left.
        int mCloseViews = 0;
        // Timer ticks to wait after the last capture with no one asking for frames, logging how many were drawn.
        int mIdleTicks = 0;
        // After the first capture the view leaves its parent for a window of its own, as floating a dock does.
        bool mReparent = false;
        // Commands for the view's camera controller, run once before the first capture. See runCameraScript.
        std::string mCameraScript;
        // A referenceable to show in a preview, the widget of the Preview subview, in the frame beside the cell's view.
        std::string mPreview;
        // A model of the virtual file system to open in the mesh viewer instead of a cell (task 0043), with how to show
        // it: --mesh-mode, --mesh-light, --mesh-show (bounds,skeleton,axes,collision,grid), --mesh-select and
        // --mesh-hide (node name parts), --mesh-anim (group[:completion 0 to 1]).
        std::string mMesh;
        std::string mMeshMode = "textured";
        std::string mMeshLight = "headlight";
        std::string mMeshShow = "grid";
        std::string mMeshSelect;
        std::string mMeshHide;
        std::string mMeshAnim;
        // Where to pick in the view, "x,y" from 0 to 1, logging the tree node found.
        std::string mMeshPick;
        std::string mMeshCamera; // ex,ey,ez,cx,cy,cz
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

    CSVRender::WorldspaceWidget::InteractionType interactionType(const std::string& name)
    {
        using Widget = CSVRender::WorldspaceWidget;
        if (name == "edit")
            return Widget::InteractionType_PrimaryEdit;
        if (name == "edit2")
            return Widget::InteractionType_SecondaryEdit;
        if (name == "select")
            return Widget::InteractionType_PrimarySelect;
        if (name == "select2")
            return Widget::InteractionType_SecondarySelect;
        if (name == "select3")
            return Widget::InteractionType_TertiarySelect;
        return Widget::InteractionType_PrimaryOpen;
    }

    void logSelection(CSVRender::WorldspaceWidget& view)
    {
        const auto selection = view.getSelection(CSVRender::Mask_Reference);
        Log(Debug::Info) << "Selected " << selection.size();
        for (const auto& tag : selection)
            if (const auto* object = dynamic_cast<CSVRender::ObjectTag*>(tag.get()))
            {
                const ESM::Position position = object->mObject->getPosition();
                Log(Debug::Info) << "  " << object->mObject->getReferenceId() << " "
                                 << object->mObject->getReferenceableId() << " at " << position.pos[0] << " "
                                 << position.pos[1] << " " << position.pos[2] << " rot " << position.rot[0] << " "
                                 << position.rot[1] << " " << position.rot[2] << " scale "
                                 << object->mObject->getScale();
            }
    }

    void logUndo(CSVRender::WorldspaceWidget& view)
    {
        const QUndoStack& stack = view.getDocument().getUndoStack();
        Log(Debug::Info) << "Undo stack: " << stack.count() << " at " << stack.index()
                         << ", next undo: " << stack.undoText().toStdString();
    }

    void logCamera(const CSVRender::SceneHost& host)
    {
        const auto lookAt = CSVRender::CameraPose::lookAtFrom(host.getViewMatrix());
        Log(Debug::Info) << "Camera eye " << lookAt.mEye.x << " " << lookAt.mEye.y << " " << lookAt.mEye.z << " centre "
                         << lookAt.mCenter.x << " " << lookAt.mCenter.y << " " << lookAt.mCenter.z << " up "
                         << lookAt.mUp.x << " " << lookAt.mUp.y << " " << lookAt.mUp.z;
    }

    /// Drives the view's camera controller as keys and the mouse would, without Qt events. Commands, separated by ';':
    /// mode 1st|free|orbit, pose ex ey ez cx cy cz ux uy uz, center x y z (orbit), slot name 0|1 (the controller's
    /// slot, as its shortcut calls it), mouse dx dy, wheel d, update dt, ortho size, ray x y hexmask (logs
    /// WorldspaceWidget::mousePick at that pixel). The camera is logged after each. For the edit modes, through the
    /// same calls as the shortcuts: editmode id, press edit|edit2|select|select2|select3 x y, drag type x0 y0 x1 y1
    /// steps, hover x y (a real mouse move event), submode 0|1|2, modeslot name [int] (a slot of the current edit
    /// mode), selection, undo, redo, undolog. `wait` stops until the next tick, so that what follows the camera has
    /// followed it (cell markers): the rest of the script is returned.
    std::string runCameraScript(CSVRender::WorldspaceWidget& view, const std::string& script)
    {
        std::istringstream commands(script);
        std::string command;
        while (std::getline(commands, command, ';'))
        {
            std::istringstream words(command);
            std::string verb;
            words >> verb;
            if (verb == "wait")
            {
                std::string rest;
                std::getline(commands, rest, '\0');
                return rest;
            }
            auto* controller = view.getCameraController();
            if (verb == "mode")
            {
                std::string mode;
                words >> mode;
                view.setNavigationMode(mode);
            }
            else if (verb == "pose")
            {
                vsg::dvec3 eye, center, up;
                words >> eye.x >> eye.y >> eye.z >> center.x >> center.y >> center.z >> up.x >> up.y >> up.z;
                view.setCameraPose(eye, center, up);
            }
            else if (verb == "center")
            {
                vsg::dvec3 center;
                words >> center.x >> center.y >> center.z;
                if (auto* orbit = dynamic_cast<CSVRender::OrbitCameraController*>(controller))
                    orbit->setCenter(center);
            }
            else if (verb == "slot")
            {
                std::string name;
                int active = 0;
                words >> name >> active;
                if (!QMetaObject::invokeMethod(controller, name.c_str(), Q_ARG(bool, active != 0)))
                    Log(Debug::Error) << "The camera controller has no slot " << name;
            }
            else if (verb == "ray")
            {
                int x = 0;
                int y = 0;
                unsigned int mask = 0;
                words >> x >> y >> std::hex >> mask;
                const CSVRender::WorldspaceHitResult hit = view.mousePick(QPoint(x, y), mask);
                Log(Debug::Info) << "Ray at " << x << " " << y << " mask " << std::hex << mask << std::dec
                                 << (hit.hit ? ": hit" : ": no hit") << " at " << hit.worldPos.x << " "
                                 << hit.worldPos.y << " " << hit.worldPos.z << " tag "
                                 << (hit.tag ? "of mask " + std::to_string(hit.tag->getMask()) + " "
                                                + hit.tag->getToolTip(true, hit).toStdString().substr(0, sMaxLoggedTag)
                                             : std::string("none"))
                                 << " triangle " << hit.index0 << " " << hit.index1 << " " << hit.index2;
                if (const auto* marker = dynamic_cast<const CSVRender::ObjectMarkerTag*>(hit.tag.get()))
                    Log(Debug::Info) << "  marker part " << marker->mNode << " axis " << marker->mAxis;
            }
            else if (verb == "editmode")
            {
                std::string id;
                words >> id;
                view.getEditModeSelector()->setButton(id);
            }
            else if (verb == "press")
            {
                std::string type;
                int x = 0;
                int y = 0;
                words >> type >> x >> y;
                view.scriptPress(interactionType(type), QPoint(x, y));
            }
            else if (verb == "drag")
            {
                std::string type;
                int x0 = 0, y0 = 0, x1 = 0, y1 = 0, steps = 1;
                words >> type >> x0 >> y0 >> x1 >> y1 >> steps;
                view.scriptDrag(interactionType(type), QPoint(x0, y0), QPoint(x1, y1), steps);
            }
            else if (verb == "hover")
            {
                int x = 0;
                int y = 0;
                words >> x >> y;
                QMouseEvent event(QEvent::MouseMove, QPointF(x, y), QPointF(view.mapToGlobal(QPoint(x, y))),
                    Qt::NoButton, Qt::NoButton, Qt::NoModifier);
                QApplication::sendEvent(&view, &event);
            }
            else if (verb == "submode")
            {
                int subMode = 0;
                words >> subMode;
                view.setSubMode(subMode, CSVRender::Mask_Reference);
            }
            else if (verb == "modeslot")
            {
                std::string name;
                words >> name;
                int value = 0;
                const bool ok = (words >> value)
                    ? QMetaObject::invokeMethod(view.getEditMode(), name.c_str(), Q_ARG(int, value))
                    : QMetaObject::invokeMethod(view.getEditMode(), name.c_str());
                if (!ok)
                    Log(Debug::Error) << "The edit mode has no slot " << name;
            }
            else if (verb == "shapebrush")
            {
                int shape = 0, size = 1, tool = 0, strength = 8;
                words >> shape >> size >> tool >> strength;
                if (auto* mode = dynamic_cast<CSVRender::TerrainShapeMode*>(view.getEditMode()))
                {
                    mode->setBrushShape(static_cast<CSVWidget::BrushShape>(shape));
                    mode->setBrushSize(size);
                    mode->setShapeEditTool(tool);
                    mode->setShapeEditToolStrength(strength);
                }
            }
            else if (verb == "texbrush")
            {
                int shape = 0, size = 1;
                std::string texture;
                words >> shape >> size >> texture;
                if (auto* mode = dynamic_cast<CSVRender::TerrainTextureMode*>(view.getEditMode()))
                {
                    mode->setBrushShape(static_cast<CSVWidget::BrushShape>(shape));
                    mode->setBrushSize(size);
                    mode->setBrushTexture(ESM::RefId::stringRefId(texture));
                }
            }
            else if (verb == "landheight")
            {
                std::string first, second;
                int x = 0, y = 0;
                words >> first >> second >> x >> y;
                const ESM::Land& land = view.getDocument()
                                            .getData()
                                            .getLand()
                                            .getRecord(ESM::RefId::stringRefId(first + " " + second))
                                            .get();
                const auto* data = land.getLandData(ESM::Land::DATA_VHGT);
                Log(Debug::Info) << "Land " << first << " " << second << " at " << x << " " << y << ": "
                                 << data->mHeights[y * ESM::Land::LAND_SIZE + x];
            }
            else if (verb == "selection")
                logSelection(view);
            else if (verb == "undo")
                view.getDocument().getUndoStack().undo();
            else if (verb == "redo")
                view.getDocument().getUndoStack().redo();
            else if (verb == "undolog")
                logUndo(view);
            else if (verb == "ortho")
            {
                int size = 0;
                words >> size;
                view.getHost()->setProjection(0, true, size);
            }
            else if (verb == "mouse")
            {
                int x = 0;
                int y = 0;
                words >> x >> y;
                controller->handleMouseMoveEvent(x, y);
            }
            else if (verb == "wheel")
            {
                int delta = 0;
                words >> delta;
                controller->handleMouseScrollEvent(delta);
            }
            else if (verb == "update")
            {
                double dt = 0;
                words >> dt;
                controller->update(dt);
            }
            else
                Log(Debug::Error) << "Unknown camera command: " << command;
            Log(Debug::Info) << "After \"" << command << "\":";
            logCamera(*view.getHost());
        }
        return {};
    }

    /// The window of the views: shown, but never takes focus.
    std::unique_ptr<QWidget> createFrame(int views)
    {
        auto frame = std::make_unique<QWidget>();
        frame->setAttribute(Qt::WA_ShowWithoutActivating);
        frame->setWindowFlag(Qt::WindowDoesNotAcceptFocus);
        frame->setWindowTitle("Eden Studio capture");
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
        if (std::string_view(argv[i]) == "--open-cell" || std::string_view(argv[i]) == "--open-mesh")
            return true;
    return false;
}

int CS::runCapture(int argc, char** argv)
{
    Files::ConfigurationManager cfgMgr;
    bpo::variables_map variables;
    bpo::options_description desc("Syntax: eden-studio --open-cell X --auto-screenshot N --auto-quit M");
    auto addOption = desc.add_options();
    addOption("open-cell", bpo::value<std::string>()->default_value(""));
    addOption("open-mesh", bpo::value<std::string>());
    addOption("mesh-mode", bpo::value<std::string>()->default_value("textured"));
    addOption("mesh-light", bpo::value<std::string>()->default_value("headlight"));
    addOption("mesh-show", bpo::value<std::string>()->default_value("grid"));
    addOption("mesh-select", bpo::value<std::string>()->default_value(""));
    addOption("mesh-hide", bpo::value<std::string>()->default_value(""));
    addOption("mesh-anim", bpo::value<std::string>()->default_value(""));
    addOption("mesh-pick", bpo::value<std::string>()->default_value(""));
    addOption("mesh-camera", bpo::value<std::string>()->default_value(""));
    addOption("auto-screenshot", bpo::value<std::uint64_t>()->default_value(0));
    addOption("auto-quit", bpo::value<std::uint64_t>()->required());
    addOption("camera", bpo::value<std::string>());
    addOption("capture-views", bpo::value<int>()->default_value(1));
    addOption("lighting", bpo::value<std::string>()->default_value("day"));
    addOption("fov", bpo::value<double>());
    addOption("neighbours", bpo::value<int>()->default_value(0));
    addOption("capture-resize", bpo::value<std::string>());
    addOption("capture-close-views", bpo::value<int>()->default_value(0));
    addOption("capture-idle-ticks", bpo::value<int>()->default_value(0));
    addOption("capture-reparent", bpo::bool_switch());
    addOption("camera-script", bpo::value<std::string>());
    addOption("capture-preview", bpo::value<std::string>());
    addOption("data",
        bpo::value<Files::MaybeQuotedPathContainer>()
            ->default_value(Files::MaybeQuotedPathContainer(), "data")
            ->multitoken()
            ->composing());
    addOption("data-local",
        bpo::value<Files::MaybeQuotedPathContainer::value_type>()->default_value(
            Files::MaybeQuotedPathContainer::value_type(), ""));
    addOption("content",
        bpo::value<std::vector<std::string>>()
            ->default_value(std::vector<std::string>(), "")
            ->multitoken()
            ->composing());
    addOption("encoding", bpo::value<std::string>()->default_value("win1252"));
    addOption("fallback-archive",
        bpo::value<std::vector<std::string>>()
            ->default_value(std::vector<std::string>(), "fallback-archive")
            ->multitoken());
    addOption("fallback",
        bpo::value<Fallback::FallbackMap>()->default_value(Fallback::FallbackMap(), "")->multitoken()->composing(),
        "fallback values");
    Files::ConfigurationManager::addCommonOptions(desc);

    bpo::store(bpo::command_line_parser(argc, argv).options(desc).allow_unregistered().run(), variables);
    cfgMgr.readConfiguration(variables, desc, false);
    bpo::notify(variables);
    Settings::Manager::load(cfgMgr, true);
    Debug::setupLogging(cfgMgr.getLogPath(), "Eden-Studio");
    Fallback::Map::init(variables["fallback"].as<Fallback::FallbackMap>().mMap);

    Request request;
    request.mCell = variables["open-cell"].as<std::string>();
    if (variables.count("open-mesh"))
        request.mMesh = variables["open-mesh"].as<std::string>();
    request.mMeshMode = variables["mesh-mode"].as<std::string>();
    request.mMeshLight = variables["mesh-light"].as<std::string>();
    request.mMeshShow = variables["mesh-show"].as<std::string>();
    request.mMeshSelect = variables["mesh-select"].as<std::string>();
    request.mMeshHide = variables["mesh-hide"].as<std::string>();
    request.mMeshAnim = variables["mesh-anim"].as<std::string>();
    request.mMeshPick = variables["mesh-pick"].as<std::string>();
    request.mMeshCamera = variables["mesh-camera"].as<std::string>();
    request.mScreenshotFrame = variables["auto-screenshot"].as<std::uint64_t>();
    request.mQuitFrame = variables["auto-quit"].as<std::uint64_t>();
    request.mViews = variables["capture-views"].as<int>();
    request.mLighting = variables["lighting"].as<std::string>();
    if (variables.count("fov"))
        request.mFieldOfView = variables["fov"].as<double>();
    request.mNeighbours = variables["neighbours"].as<int>();
    request.mCloseViews = variables["capture-close-views"].as<int>();
    request.mIdleTicks = variables["capture-idle-ticks"].as<int>();
    request.mReparent = variables["capture-reparent"].as<bool>();
    if (variables.count("camera-script"))
        request.mCameraScript = variables["camera-script"].as<std::string>();
    if (variables.count("capture-preview"))
        request.mPreview = variables["capture-preview"].as<std::string>();
    if (variables.count("capture-resize"))
    {
        std::istringstream sizes(variables["capture-resize"].as<std::string>());
        std::string size;
        while (std::getline(sizes, size, ','))
            request.mResizes.emplace_back(std::stoi(size), std::stoi(size.substr(size.find('x') + 1)));
    }
    if (variables.count("camera"))
        request.mCamera = parseCamera(variables["camera"].as<std::string>());

    // As CS::Editor::readConfig.
    const std::filesystem::path resources = variables["resources"].as<Files::MaybeQuotedPath>().u8string();
    Files::PathContainer dataDirs = Files::asPathContainer(variables["data"].as<Files::MaybeQuotedPathContainer>());
    cfgMgr.filterOutNonExistingPaths(dataDirs);
    const std::filesystem::path local
        = variables["data-local"].as<Files::MaybeQuotedPathContainer::value_type>().u8string();
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
    // Declared in this order so that views go first, then their frame's windows.
    std::unique_ptr<QWidget> frame;
    std::vector<CSVRender::WorldspaceWidget*> views;
    bool quitting = false;
    std::size_t captures = 0;
    std::size_t resizes = 0;
    std::uint64_t nextCapture = request.mScreenshotFrame;
    std::uint64_t quitFrame = request.mQuitFrame;
    bool closed = false;
    bool reparented = false;
    std::unique_ptr<QWidget> floatingFrame;
    CSVRender::PreviewWidget* preview = nullptr;
    int idleTicks = 0;
    std::uint64_t idleStartFrame = 0;

    const auto finish = [&](int code) {
        if (quitting)
            return;
        quitting = true;
        result = code;
        CSVRender::RenderContext::get().stop();
        QApplication::exit(code);
    };

    const auto setViewSize = [&](int width, int height) {
        frame->setFixedSize(width * static_cast<int>(views.size()), height);
        for (auto* view : views)
            view->setFixedSize(width, height);
        // A window gets its new size through Qt's events.
        QApplication::processEvents();
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

            if (!request.mMesh.empty())
            {
                auto& meshContext = CSVRender::RenderContext::get();
                frame = createFrame(1);
                frame->show();
                auto* meshView = new CSVRender::MeshViewWidget(data, frame.get());
                meshView->setFixedSize(sViewWidth, sViewHeight);
                frame->layout()->addWidget(meshView);
                meshView->show();
                QApplication::processEvents();
                if (!meshView->ensureHost())
                {
                    finish(1);
                    return;
                }
                CSMMesh::MeshViewState state;
                if (auto mode = CSMMesh::parseViewMode(request.mMeshMode))
                    state.mode = *mode;
                if (auto light = CSMMesh::parseLightingPreset(request.mMeshLight))
                    state.lighting = *light;
                state.showGrid = false;
                std::istringstream shown(request.mMeshShow);
                for (std::string item; std::getline(shown, item, ',');)
                {
                    state.showBounds = state.showBounds || item == "bounds";
                    state.showSkeleton = state.showSkeleton || item == "skeleton";
                    state.showNodeAxes = state.showNodeAxes || item == "axes";
                    state.showCollision = state.showCollision || item == "collision";
                    state.showGrid = state.showGrid || item == "grid";
                }
                meshView->setViewState(state);
                meshView->openMesh(request.mMesh);
                bool prepared = false;
                meshContext.setFrameCallback([&, meshView, prepared]() mutable {
                    const std::uint64_t frameCount = CSVRender::RenderContext::get().frameCount();
                    if (quitting)
                        return;
                    CSVRender::MeshScene* scene = meshView->scene();
                    if (!prepared && frameCount >= 3)
                    {
                        prepared = true;
                        if (!scene)
                        {
                            Log(Debug::Error) << "The mesh \"" << request.mMesh << "\" did not load";
                            finish(1);
                            return;
                        }
                        const auto& tree = scene->tree();
                        Log(Debug::Info) << "Mesh " << scene->path() << ": " << tree.size() << " tree nodes";
                        if (!request.mMeshCamera.empty())
                        {
                            double v[6] = {};
                            std::istringstream numbers(request.mMeshCamera);
                            bool ok = true;
                            for (int i = 0; i < 6 && ok; ++i)
                            {
                                std::string item;
                                ok = static_cast<bool>(std::getline(numbers, item, ',')) && !item.empty();
                                if (ok)
                                    v[i] = std::stod(item);
                            }
                            if (ok)
                                meshView->lookFrom(v, v + 3);
                            else
                                Log(Debug::Error) << "--mesh-camera wants ex,ey,ez,cx,cy,cz";
                        }
                        if (!request.mMeshHide.empty())
                            for (CSMMesh::NodeId node : tree.findByName(request.mMeshHide))
                                scene->setNodeVisible(node, false);
                        if (!request.mMeshSelect.empty())
                            if (auto found = tree.findByName(request.mMeshSelect); !found.empty())
                            {
                                scene->setSelected(found.front());
                                Log(Debug::Info) << scene->describeNode(found.front());
                            }
                        if (!request.mMeshAnim.empty())
                        {
                            const auto colon = request.mMeshAnim.find(':');
                            const std::string group = request.mMeshAnim.substr(0, colon);
                            float at = 0.f;
                            if (colon != std::string::npos)
                                try
                                {
                                    at = std::stof(request.mMeshAnim.substr(colon + 1));
                                }
                                catch (const std::exception&)
                                {
                                    Log(Debug::Error) << "--mesh-anim wants group[:completion]";
                                }
                            for (const auto& g : scene->animationGroups())
                                Log(Debug::Info) << "Animation group " << g.name << " " << g.start << " to " << g.stop;
                            scene->playGroup(group, at);
                            scene->setAnimationPaused(true);
                        }
                        for (const auto& warning : scene->textureReport().warnings)
                            Log(Debug::Info) << warning;
                    }
                    if (!request.mMeshPick.empty() && scene && frameCount >= 10)
                    {
                        const auto comma = request.mMeshPick.find(',');
                        try
                        {
                            const CSMMesh::NodeId hit = meshView->pickAt(std::stod(request.mMeshPick.substr(0, comma)),
                                std::stod(request.mMeshPick.substr(comma + 1)));
                            Log(Debug::Info)
                                << "Mesh pick " << request.mMeshPick << ": "
                                << (hit == CSMMesh::kNoNode ? std::string("nothing") : scene->tree().path(hit));
                        }
                        catch (const std::exception&)
                        {
                            Log(Debug::Error) << "--mesh-pick wants x,y";
                        }
                        request.mMeshPick.clear();
                    }
                    if (nextCapture != 0 && frameCount >= nextCapture)
                    {
                        const std::filesystem::path file = screenshots / ("mesh" + std::to_string(captures) + ".png");
                        if (!meshView->saveScreenshot(file))
                        {
                            Log(Debug::Error) << "Failed to capture the mesh view";
                            finish(1);
                            return;
                        }
                        Log(Debug::Info) << "Auto-screenshot at frame " << frameCount;
                        ++captures;
                        nextCapture = 0;
                    }
                    meshView->flagAsModified();
                    if (frameCount >= quitFrame && nextCapture == 0)
                    {
                        Log(Debug::Info) << "Auto-quit at frame " << frameCount;
                        finish(0);
                    }
                });
                return;
            }

            const ESM::RefId cell = CSVRender::SceneHost::findCell(data, request.mCell);
            if (cell.empty())
            {
                Log(Debug::Error) << "There is no cell \"" << request.mCell << "\"";
                finish(1);
                return;
            }

            auto& context = CSVRender::RenderContext::get();
            frame = createFrame(request.mViews);
            frame->show();
            for (int i = 0; i < request.mViews; ++i)
            {
                // The same widgets as the scene subview, so that a capture runs what the editor runs.
                CSVRender::WorldspaceWidget* view = nullptr;
                const auto coordinates = CSMWorld::CellCoordinates::fromId(cell.getRefIdString());
                if (coordinates.second)
                {
                    auto* paged = new CSVRender::PagedWorldspaceWidget(frame.get(), *document);
                    CSMWorld::CellSelection selection;
                    const int neighbours = request.mNeighbours;
                    for (int x = -neighbours; x <= neighbours; ++x)
                        for (int y = -neighbours; y <= neighbours; ++y)
                            selection.add(coordinates.first.move(x, y));
                    paged->setCellSelection(selection);
                    view = paged;
                }
                else
                    view = new CSVRender::UnpagedWorldspaceWidget(cell.getRefIdString(), *document, frame.get());
                views.push_back(view);
                view->setFixedSize(sViewWidth, sViewHeight);
                frame->layout()->addWidget(view);
                view->show();
                QApplication::processEvents();
                if (!view->ensureHost())
                {
                    finish(1);
                    return;
                }
                // Hidden, but the same tools as a scene subview's, which the edit modes add themselves to.
                auto* toolbar = new CSVWidget::SceneToolbar(sToolbarButtonSize, frame.get());
                toolbar->hide();
                view->makeSceneVisibilitySelector(toolbar);
                if (auto* paged = dynamic_cast<CSVRender::PagedWorldspaceWidget*>(view))
                    paged->makeControlVisibilitySelector(toolbar);
                view->makeEditModeSelector(toolbar);
                view->selectLightingMode(request.mLighting);
                if (const auto& camera = request.mCamera)
                    view->setCameraPose({ (*camera)[0], (*camera)[1], (*camera)[2] },
                        { (*camera)[3], (*camera)[4], (*camera)[5] }, { (*camera)[6], (*camera)[7], (*camera)[8] });
                if (request.mFieldOfView)
                    view->getHost()->setProjection(*request.mFieldOfView, false, 0);
            }

            if (!request.mPreview.empty())
            {
                frame->setFixedSize(sViewWidth * (request.mViews + 1), sViewHeight);
                preview = new CSVRender::PreviewWidget(data, request.mPreview, true, frame.get());
                preview->setFixedSize(sViewWidth, sViewHeight);
                frame->layout()->addWidget(preview);
                preview->show();
                QApplication::processEvents();
                if (!preview->ensureHost())
                {
                    finish(1);
                    return;
                }
            }

            context.setFrameCallback([&] {
                const std::uint64_t frameCount = CSVRender::RenderContext::get().frameCount();
                if (quitting)
                    return;
                // After the view has had its first frames, so that its default camera is there to start from.
                if (!request.mCameraScript.empty() && frameCount >= sCameraScriptFrame)
                {
                    request.mCameraScript = runCameraScript(*views.front(), request.mCameraScript);
                }
                if (nextCapture != 0 && frameCount >= nextCapture)
                {
                    Log(Debug::Info) << "Auto-screenshot at frame " << frameCount;
                    for (std::size_t i = 0; i < views.size(); ++i)
                    {
                        std::string name = "editor" + std::to_string(i);
                        if (captures > 0)
                            name += "-" + std::to_string(captures);
                        if (!views[i]->getHost()->capture(screenshots / (name + ".png")))
                        {
                            Log(Debug::Error) << "Failed to capture view " << i;
                            finish(1);
                            return;
                        }
                    }
                    if (preview
                        && !preview->getHost()->capture(screenshots / ("preview" + std::to_string(captures) + ".png")))
                    {
                        Log(Debug::Error) << "Failed to capture the preview";
                        finish(1);
                        return;
                    }
                    ++captures;
                    nextCapture = 0;
                    if (request.mCloseViews > 0 && !closed)
                    {
                        closed = true;
                        for (int i = 0; i < request.mCloseViews && views.size() > 1; ++i)
                        {
                            delete views.back();
                            views.pop_back();
                        }
                        nextCapture = frameCount + request.mScreenshotFrame;
                        quitFrame = nextCapture + request.mQuitFrame;
                    }
                    else if (request.mReparent && !reparented)
                    {
                        reparented = true;
                        floatingFrame = createFrame(1);
                        floatingFrame->layout()->addWidget(views.front());
                        floatingFrame->show();
                        QApplication::processEvents();
                        Log(Debug::Info) << "Reparented; the view "
                                         << (views.front()->getHost()->hasWindow() ? "kept" : "lost") << " its window";
                        nextCapture = frameCount + request.mScreenshotFrame;
                        quitFrame = nextCapture + request.mQuitFrame;
                    }
                    else if (resizes < request.mResizes.size())
                    {
                        const auto [width, height] = request.mResizes[resizes++];
                        setViewSize(width, height);
                        nextCapture = frameCount + request.mScreenshotFrame;
                        quitFrame = nextCapture + request.mQuitFrame;
                    }
                }
                if (frameCount >= quitFrame && nextCapture == 0 && idleTicks < request.mIdleTicks)
                {
                    // Nothing asks for a frame here, so only a view that animates by itself draws.
                    if (idleTicks++ == 0)
                        idleStartFrame = frameCount;
                    if (idleTicks == request.mIdleTicks)
                        Log(Debug::Info) << "Idle: " << frameCount - idleStartFrame << " frames in " << idleTicks
                                         << " ticks";
                    return;
                }
                // A frame must reach the capture and quit frames even in a view that nothing moves in.
                for (auto* view : views)
                    view->flagAsModified();
                if (preview)
                    preview->flagAsModified();
                if (frameCount >= quitFrame && nextCapture == 0)
                {
                    Log(Debug::Info) << "Auto-quit at frame " << frameCount;
                    finish(0);
                }
            });
        });

    documents.addDocument(files, files.back(), false);

    const int code = QApplication::exec();
    frame.reset();
    floatingFrame.reset();
    return result != 0 ? result : code;
}
