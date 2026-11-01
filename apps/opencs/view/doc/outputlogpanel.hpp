#ifndef CSV_DOC_OUTPUTLOGPANEL_H
#define CSV_DOC_OUTPUTLOGPANEL_H

#include <QWidget>

#include <cstddef>

class QPlainTextEdit;
class QTimer;

namespace CSVDoc
{
    /// The Output Log of Eden Studio: the editor's own log (everything `Log(...)` writes) as it happens, with a
    /// Clear button. The verifier's findings and the run log keep their own tables (File > Verify, Simulation > Run
    /// Log); the panel says where they are. Reads LogTap on the main thread, 4 times a second, while visible.
    class OutputLogPanel : public QWidget
    {
        Q_OBJECT

    public:
        explicit OutputLogPanel(QWidget* parent = nullptr);

        /// Reads new lines now (also what the timer calls).
        void refresh();

    protected:
        void showEvent(QShowEvent* event) override;
        void hideEvent(QHideEvent* event) override;

    private:
        QPlainTextEdit* mText;
        QTimer* mTimer;
        std::size_t mCursor = 0;
    };
}

#endif
