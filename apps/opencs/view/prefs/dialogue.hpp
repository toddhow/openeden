#ifndef CSV_PREFS_DIALOGUE_H
#define CSV_PREFS_DIALOGUE_H

#include <QMainWindow>

class QSplitter;
class QStackedWidget;
class QListWidgetItem;

namespace CSVPrefs
{
    class PageBase;
    class ModuleSettingsPage;

    class Dialogue : public QMainWindow
    {
        Q_OBJECT

        QStackedWidget* mContent;
        QWidget* mModulesPage = nullptr; ///< The editor modules' settings (API 1.2); made when first chosen.

    private:
        void buildCategorySelector(QSplitter* main);

        void buildContentArea(QSplitter* main);

        PageBase* makePage(const std::string& key);

    public:
        Dialogue();

        ~Dialogue() override;

    protected:
        void closeEvent(QCloseEvent* event) override;

    public slots:

        void show();

    private slots:

        void selectionChanged(QListWidgetItem* current, QListWidgetItem* previous);
    };
}

#endif
