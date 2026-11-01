#ifndef CSM_PREFS_LAYOUTSTORE_H
#define CSM_PREFS_LAYOUTSTORE_H

#include <filesystem>
#include <string>

#include <QByteArray>

namespace CSMPrefs
{
    /// The two window arrangements of Eden Studio (task 0024, stage B).
    enum class EditorLayout
    {
        EdenStudio, ///< Asset Browser, viewport, Context Inspector and a bottom row. The default.
        Classic, ///< OpenMW-CS's arrangement: one dock window per sub view.
    };

    inline constexpr const char* layoutStateFileName = "eden-studio-layout.ini";

    const char* layoutName(EditorLayout layout);

    /// Parses the name written by layoutName. Anything else (a missing or damaged file) gives the default.
    EditorLayout layoutFromName(const std::string& name);

    /// Remembers, per user, which layout is chosen and how its docks were left (QMainWindow::saveState bytes).
    /// It is a small ini file next to eden-studio.cfg, so that the settings file keeps only settings. Reading and
    /// writing never throw: a file that cannot be read means defaults, one that cannot be written means the choice
    /// is not remembered.
    class LayoutStore
    {
    public:
        explicit LayoutStore(std::filesystem::path file);

        EditorLayout layout() const;
        void setLayout(EditorLayout layout);

        /// Empty when nothing was saved for \a layout.
        QByteArray dockState(EditorLayout layout) const;
        void setDockState(EditorLayout layout, const QByteArray& state);

        /// Forgets the saved dock state of \a layout (the "Reset layout" command).
        void clearDockState(EditorLayout layout);

    private:
        std::filesystem::path mFile;
    };
}

#endif
