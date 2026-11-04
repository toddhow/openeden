#include "capture.hpp"
#include "editor.hpp"
#include "lodcheck.hpp"
#include "view/render/rendercontext.hpp"

#include <exception>
#include <ostream>
#include <string>
#include <utility>

#include <QIcon>

#include <components/debug/debugging.hpp>
#include <components/platform/application.hpp>
#include <components/platform/platform.hpp>

#include "model/doc/messages.hpp"
#include "model/prefs/commandline.hpp"
#include "model/world/disabletag.hpp"
#include "model/world/universalid.hpp"

#ifdef Q_OS_MAC
#include <QDir>
#endif

Q_DECLARE_METATYPE(std::string)

class QEvent;
class QObject;

int runApplication(int argc, char* argv[])
{
    Platform::init();

    Q_INIT_RESOURCE(resources);

#ifdef WIN32
    Q_INIT_RESOURCE(dark);
#endif

    qRegisterMetaType<std::string>("std::string");
    qRegisterMetaType<CSMWorld::UniversalId>("CSMWorld::UniversalId");
    qRegisterMetaType<CSMWorld::DisableTag>("CSMWorld::DisableTag");
    qRegisterMetaType<CSMDoc::Message>("CSMDoc::Message");

    Platform::Application application(argc, argv);

    application.setWindowIcon(QIcon(":eden-studio"));

#ifdef __linux__
    setlocale(LC_NUMERIC, "C");
#endif

    // The tile viewer check draws nothing on a GPU, so it needs no render context (task 0055, stage S1e).
    if (CS::isLodPackRequest(argc, argv))
        return CS::runLodPackCheck(argc, argv);

    // An unattended capture parses its own options (capture.cpp). Otherwise the editor's command line is checked before
    // the render context starts the GPU. A mistake is reported on stderr with exit code 2 and no message box: the
    // command line comes from a terminal or a script, and a box would keep an unattended run waiting.
    const bool capture = CS::isCaptureRequest(argc, argv);
    CSMPrefs::CommandLine commandLine;
    if (!capture)
    {
        commandLine = CSMPrefs::parseCommandLine(argc, argv, CSMPrefs::makeEditorOptions());
        if (!commandLine.mErrors.empty())
        {
            for (const std::string& error : commandLine.mErrors)
                Debug::getRawStderr() << "eden-studio: " << error << std::endl;
            Debug::getRawStderr() << "Run eden-studio --help for the options." << std::endl;
            return 2;
        }
        if (commandLine.mHelp)
        {
            Debug::getRawStdout() << CSMPrefs::makeEditorOptions() << std::endl;
            return 0;
        }
    }

    // Before anything that opens a view, so that the device goes after every view and document.
    CSVRender::RenderContext::Lifetime renderContext;

    // Before Editor: an unattended capture opens no editor window, dialogue or IPC server.
    if (capture)
        return CS::runCapture(argc, argv);

    CS::Editor editor(std::move(commandLine));

    if (!editor.makeIPCServer())
    {
        editor.connectToIPCServer();
        return 0;
    }

    return editor.run();
}

int main(int argc, char* argv[])
{
    return Debug::wrapApplication(&runApplication, argc, argv, "Eden-Studio");
}
