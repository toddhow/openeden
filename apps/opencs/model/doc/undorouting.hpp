#ifndef CSM_DOC_UNDOROUTING_H
#define CSM_DOC_UNDOROUTING_H

#include <QObject>
#include <QUndoGroup>
#include <QUndoStack>

namespace CSMDoc
{
    /// The two undo stacks of one document and which of them Edit > Undo and Redo act on (task 0031 follow-up,
    /// toddhow 2026-11-04). The document's own stack holds the content edits, and its clean flag is the document's
    /// modified state (Document::getState): a step an editor module pushes there marked the ESM document modified, and
    /// closing it asked "save changes?". An editor module's steps (CSVModule::ModuleUi) therefore go on a stack of
    /// their own, which no state depends on; the module's asset reports its own dirty flag.
    ///
    /// The Edit menu's actions are made from group(), which acts on its active stack. The active stack follows what the
    /// user touched last: a step pushed, undone or redone on a stack makes it the active one, and moving the focus into
    /// a module's own widgets (noteFocus) or out of them chooses between the two. Focus alone never activates an empty
    /// module stack, so a click in a module panel before any module edit leaves Undo on the document.
    ///
    /// Ownership: owned by the Document, which constructs it with its own stack (that stack must outlive this object's
    /// use of it). Main thread only.
    class UndoRouting : public QObject
    {
        Q_OBJECT

    public:
        enum class Target
        {
            Document,
            Module
        };

        explicit UndoRouting(QUndoStack& documentStack);
        ~UndoRouting() override;

        UndoRouting(const UndoRouting&) = delete;
        UndoRouting& operator=(const UndoRouting&) = delete;

        /// Holds both stacks; the Edit menu's actions come from it.
        QUndoGroup& group() { return mGroup; }
        QUndoStack& documentStack() { return mDocumentStack; }
        /// The editor modules' steps.
        QUndoStack& moduleStack() { return mModuleStack; }

        Target active() const;
        void activate(Target target);

        /// The focus moved into a widget of the module UI (\a inModuleUi true) or elsewhere in the document's window.
        void noteFocus(bool inModuleUi);

        /// Empties the module stack (before the module libraries are unloaded: its steps have code in them) and makes
        /// the document's stack the active one.
        void clearModuleStack();

        bool canUndo() const;
        bool canRedo() const;

    signals:
        /// The active stack changed.
        void activeChanged();

    private:
        QUndoStack& mDocumentStack;
        QUndoGroup mGroup;
        QUndoStack mModuleStack;
    };
}

#endif
