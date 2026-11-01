#ifndef CSV_DOC_CONTEXTINSPECTOR_H
#define CSV_DOC_CONTEXTINSPECTOR_H

#include <QWidget>

#include <array>
#include <functional>
#include <string>
#include <vector>

#include <apps/opencs/model/world/universalid.hpp>

class QDoubleSpinBox;
class QLabel;
class QPushButton;
class QVBoxLayout;
class QTimer;

namespace CSMDoc
{
    class Document;
}

namespace CSVRender
{
    class WorldspaceWidget;
}

namespace CSVDoc
{
    /// The right panel of Eden Studio (plan section 2.1, item 2): what is selected in the viewport.
    ///
    /// For one selected object (an instance, a "reference") it shows its id, the object it is an instance of and
    /// that object's record type, its cell, and fields for position, rotation and scale. Editing a field writes
    /// through the document's undo stack as one CSMWorld::ModifyCommand, exactly as editing the cell in the
    /// References table does, so Undo and Redo work and the viewport follows the table's change signal. A button
    /// opens the full record editor. For several selected objects it only says how many.
    ///
    /// How it learns of a selection: it asks the viewport 4 times a second while visible (the viewport has no
    /// selection signal, and adding one would change code the task leaves alone). It reads only; the main thread
    /// does everything.
    ///
    /// Ownership: a child of the dock that shows it. The viewport comes from a callback, because a scene sub view
    /// can be closed and replaced at any time; the inspector never keeps a pointer to it.
    class ContextInspector : public QWidget
    {
        Q_OBJECT

    public:
        using ViewportProvider = std::function<CSVRender::WorldspaceWidget*()>;

        ContextInspector(CSMDoc::Document& document, ViewportProvider provider, QWidget* parent = nullptr);

        /// Looks at the selection now (also what the timer calls).
        void poll();

        void setEditLock(bool locked);

        /// The instance ids shown (empty for none). For checks.
        const std::vector<std::string>& selectedIds() const { return mIds; }

    signals:
        void openRecordRequested(const CSMWorld::UniversalId& id);

    protected:
        void showEvent(QShowEvent* event) override;
        void hideEvent(QHideEvent* event) override;

    private slots:
        void tableDataChanged();

    private:
        enum Field
        {
            Field_PosX,
            Field_PosY,
            Field_PosZ,
            Field_RotX,
            Field_RotY,
            Field_RotZ,
            Field_Scale,
            FieldCount
        };

        std::vector<std::string> readSelection() const;
        void showSelection();
        void refreshValues();
        void fieldEdited(int field);
        void setFieldsEnabled(bool enabled);
        /// The sections editor modules registered for the active asset (task 0031).
        void rebuildModuleSections();

        CSMDoc::Document& mDocument;
        ViewportProvider mProvider;
        QTimer* mTimer;
        QLabel* mTitle;
        QLabel* mRecordType;
        QLabel* mObjectId;
        QLabel* mCell;
        std::array<QDoubleSpinBox*, FieldCount> mFields{};
        QPushButton* mOpen;
        QVBoxLayout* mModuleSections = nullptr;
        std::vector<std::string> mModuleSectionIds;
        unsigned long mModuleSectionGeneration = ~0ul;
        std::vector<std::string> mIds;
        bool mUpdating = false; // true while the fields are being filled from the table
        bool mLocked = false;
    };
}

#endif
