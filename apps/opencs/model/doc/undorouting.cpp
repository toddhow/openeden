#include "undorouting.hpp"

namespace CSMDoc
{
    UndoRouting::UndoRouting(QUndoStack& documentStack)
        : mDocumentStack(documentStack)
    {
        mGroup.addStack(&mDocumentStack);
        mGroup.addStack(&mModuleStack);
        mGroup.setActiveStack(&mDocumentStack);

        // The stack a step was pushed, undone or redone on is the one the Edit menu acts on from then on. Lambdas, not
        // member slots: they only touch the group and this object, and are cut in the destructor before the module
        // stack (a member) is destroyed and empties itself.
        connect(&mDocumentStack, &QUndoStack::indexChanged, this, [this] { activate(Target::Document); });
        connect(&mModuleStack, &QUndoStack::indexChanged, this, [this] { activate(Target::Module); });
    }

    UndoRouting::~UndoRouting()
    {
        disconnect(&mDocumentStack, nullptr, this, nullptr);
        disconnect(&mModuleStack, nullptr, this, nullptr);
    }

    UndoRouting::Target UndoRouting::active() const
    {
        return mGroup.activeStack() == &mModuleStack ? Target::Module : Target::Document;
    }

    void UndoRouting::activate(Target target)
    {
        if (active() == target)
            return;
        mGroup.setActiveStack(target == Target::Module ? &mModuleStack : &mDocumentStack);
        emit activeChanged();
    }

    void UndoRouting::noteFocus(bool inModuleUi)
    {
        if (!inModuleUi)
            activate(Target::Document);
        else if (mModuleStack.canUndo() || mModuleStack.canRedo())
            activate(Target::Module);
    }

    void UndoRouting::clearModuleStack()
    {
        mModuleStack.clear();
        activate(Target::Document);
    }

    bool UndoRouting::canUndo() const
    {
        const QUndoStack* stack = mGroup.activeStack();
        return stack != nullptr && stack->canUndo();
    }

    bool UndoRouting::canRedo() const
    {
        const QUndoStack* stack = mGroup.activeStack();
        return stack != nullptr && stack->canRedo();
    }
}
