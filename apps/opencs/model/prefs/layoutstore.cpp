#include "layoutstore.hpp"

#include <QSettings>
#include <QString>

namespace
{
    // QSettings is used with an explicit file and the ini format, so that nothing goes to the Windows registry.
    QSettings open(const std::filesystem::path& file)
    {
        return QSettings(QString::fromStdString(file.generic_string()), QSettings::IniFormat);
    }

    QString stateKey(CSMPrefs::EditorLayout layout)
    {
        return QString("docks/") + CSMPrefs::layoutName(layout);
    }
}

const char* CSMPrefs::layoutName(EditorLayout layout)
{
    return layout == EditorLayout::Classic ? "classic" : "eden-studio";
}

CSMPrefs::EditorLayout CSMPrefs::layoutFromName(const std::string& name)
{
    return name == "classic" ? EditorLayout::Classic : EditorLayout::EdenStudio;
}

CSMPrefs::LayoutStore::LayoutStore(std::filesystem::path file)
    : mFile(std::move(file))
{
}

CSMPrefs::EditorLayout CSMPrefs::LayoutStore::layout() const
{
    QSettings settings = open(mFile);
    return layoutFromName(settings.value("layout").toString().toStdString());
}

void CSMPrefs::LayoutStore::setLayout(EditorLayout layout)
{
    QSettings settings = open(mFile);
    settings.setValue("layout", layoutName(layout));
    settings.sync();
}

QByteArray CSMPrefs::LayoutStore::dockState(EditorLayout layout) const
{
    QSettings settings = open(mFile);
    return settings.value(stateKey(layout)).toByteArray();
}

void CSMPrefs::LayoutStore::setDockState(EditorLayout layout, const QByteArray& state)
{
    QSettings settings = open(mFile);
    settings.setValue(stateKey(layout), state);
    settings.sync();
}

void CSMPrefs::LayoutStore::clearDockState(EditorLayout layout)
{
    QSettings settings = open(mFile);
    settings.remove(stateKey(layout));
    settings.sync();
}
