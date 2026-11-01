#include "capture.hpp"
#include "editor.hpp"
#include "view/render/rendercontext.hpp"

#include <exception>
#include <string>

#include <QIcon>

#include <components/debug/debugging.hpp>
#include <components/platform/application.hpp>
#include <components/platform/platform.hpp>

#include "model/doc/messages.hpp"
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

    // Before anything that opens a view, so that the device goes after every view and document.
    CSVRender::RenderContext::Lifetime renderContext;

    // Before Editor: an unattended capture opens no editor window, dialogue or IPC server.
    if (CS::isCaptureRequest(argc, argv))
        return CS::runCapture(argc, argv);

    CS::Editor editor(argc, argv);

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
