#ifndef CSV_PREFS_MODULESETTINGSPAGE_H
#define CSV_PREFS_MODULESETTINGSPAGE_H

#include <QScrollArea>

namespace CSMModule
{
    class ModuleSettings;
}

namespace CSVPrefs
{
    /// The "Modules" page of the preferences window (Editor Module API 1.2, task 0091 stage e): every setting the
    /// loaded editor modules registered, grouped by module, each with the editor its type needs (a check box, a
    /// number box limited to the declared range, a text field or a list of choices). An edit goes to
    /// CSMModule::ModuleSettings::set at once (which clamps and writes the store); the window's own closing saves
    /// eden-studio.cfg, as for every other page.
    ///
    /// Ownership: a child of the dialogue's page stack; borrows the settings, which belong to the module registry and
    /// outlive the window.
    class ModuleSettingsPage : public QScrollArea
    {
        Q_OBJECT

    public:
        /// The page's name in the category list.
        static constexpr const char* sKey = "Modules";

        ModuleSettingsPage(CSMModule::ModuleSettings& settings, QWidget* parent);

    private:
        CSMModule::ModuleSettings& mSettings;
    };
}

#endif
