// CSMDoc::UndoRouting: the editor modules' undo steps on a stack of their own, so that a module edit never marks the
// ESM document modified, and the Edit menu's Undo and Redo following the stack touched last (task 0031 follow-up,
// toddhow 2026-11-04). Written from the request alone, before the code.
//
// Assumptions where the request left room:
//  - UndoRouting(QUndoStack& documentStack) is built over the document's own stack; group(), documentStack() and
//    moduleStack() give the group the Edit menu's actions come from and the two stacks.
//  - The active stack is the one a step was last pushed, undone or redone on; the document's at the start.
//  - noteFocus(true) (focus entered a module's own widgets) makes the module stack active only when it holds a step
//    (an undo or a redo); noteFocus(false) makes the document's active.
//  - clearModuleStack() empties the module stack and makes the document's stack active.
#include "apps/opencs/model/doc/undorouting.hpp"

#include <gtest/gtest.h>

#include <QUndoCommand>
#include <QUndoStack>

namespace CSMDoc
{
    namespace
    {
        // Counts how often it ran, so that "undo went to this stack" is seen without a document.
        class CountingStep final : public QUndoCommand
        {
        public:
            CountingStep(int& value, const char* text)
                : QUndoCommand(QString::fromLatin1(text))
                , mValue(value)
            {
            }
            void redo() override { ++mValue; }
            void undo() override { --mValue; }

        private:
            int& mValue;
        };

        class UndoRoutingTest : public ::testing::Test
        {
        protected:
            UndoRoutingTest()
                : mRouting(mDocumentStack)
            {
            }

            QUndoStack mDocumentStack;
            UndoRouting mRouting;
            int mDocumentValue = 0;
            int mModuleValue = 0;
        };

        TEST_F(UndoRoutingTest, aModuleStepNeverTouchesTheDocumentsStackOrItsCleanFlag)
        {
            mDocumentStack.setClean();
            mRouting.moduleStack().push(new CountingStep(mModuleValue, "module edit"));
            EXPECT_EQ(mModuleValue, 1);
            EXPECT_EQ(mDocumentStack.count(), 0);
            EXPECT_TRUE(mDocumentStack.isClean()) << "the ESM document must not turn modified";
            EXPECT_EQ(mRouting.moduleStack().count(), 1);
        }

        TEST_F(UndoRoutingTest, aDocumentStepNeverTouchesTheModuleStack)
        {
            mDocumentStack.push(new CountingStep(mDocumentValue, "record edit"));
            EXPECT_EQ(mRouting.moduleStack().count(), 0);
            EXPECT_FALSE(mDocumentStack.isClean());
        }

        TEST_F(UndoRoutingTest, theDocumentsStackIsActiveAtTheStart)
        {
            EXPECT_EQ(mRouting.active(), UndoRouting::Target::Document);
            EXPECT_EQ(mRouting.group().activeStack(), &mDocumentStack);
            EXPECT_FALSE(mRouting.canUndo());
            EXPECT_FALSE(mRouting.canRedo());
        }

        TEST_F(UndoRoutingTest, theStackTouchedLastIsTheOneUndoActsOn)
        {
            mDocumentStack.push(new CountingStep(mDocumentValue, "record edit"));
            EXPECT_EQ(mRouting.active(), UndoRouting::Target::Document);

            mRouting.moduleStack().push(new CountingStep(mModuleValue, "module edit"));
            EXPECT_EQ(mRouting.active(), UndoRouting::Target::Module);
            EXPECT_EQ(mRouting.group().activeStack(), &mRouting.moduleStack());
            EXPECT_TRUE(mRouting.canUndo());

            // Edit > Undo: the group's undo acts on the active stack only.
            mRouting.group().undo();
            EXPECT_EQ(mModuleValue, 0);
            EXPECT_EQ(mDocumentValue, 1) << "the record edit stays";
            EXPECT_TRUE(mRouting.canRedo());
            mRouting.group().redo();
            EXPECT_EQ(mModuleValue, 1);

            // Another record edit: back to the document.
            mDocumentStack.push(new CountingStep(mDocumentValue, "another record edit"));
            EXPECT_EQ(mRouting.active(), UndoRouting::Target::Document);
            mRouting.group().undo();
            EXPECT_EQ(mDocumentValue, 1);
            EXPECT_EQ(mModuleValue, 1) << "the module edit stays";
        }

        TEST_F(UndoRoutingTest, undoingOnAStackDirectlyAlsoMakesItActive)
        {
            mDocumentStack.push(new CountingStep(mDocumentValue, "record edit"));
            mRouting.moduleStack().push(new CountingStep(mModuleValue, "module edit"));
            mDocumentStack.undo();
            EXPECT_EQ(mRouting.active(), UndoRouting::Target::Document);
            mRouting.moduleStack().undo();
            EXPECT_EQ(mRouting.active(), UndoRouting::Target::Module);
        }

        TEST_F(UndoRoutingTest, activateChoosesAndAnActiveChangeIsSignalled)
        {
            int changes = 0;
            QObject::connect(&mRouting, &UndoRouting::activeChanged, [&changes] { ++changes; });
            mRouting.activate(UndoRouting::Target::Module);
            EXPECT_EQ(mRouting.active(), UndoRouting::Target::Module);
            EXPECT_EQ(changes, 1);
            mRouting.activate(UndoRouting::Target::Module);
            EXPECT_EQ(changes, 1) << "no change, no signal";
            mRouting.activate(UndoRouting::Target::Document);
            EXPECT_EQ(mRouting.active(), UndoRouting::Target::Document);
            EXPECT_EQ(changes, 2);
        }

        TEST_F(UndoRoutingTest, focusInTheModuleUiActivatesTheModuleStackOnlyWhenItHoldsAStep)
        {
            mRouting.noteFocus(true);
            EXPECT_EQ(mRouting.active(), UndoRouting::Target::Document) << "nothing to undo there";

            mRouting.moduleStack().push(new CountingStep(mModuleValue, "module edit"));
            mDocumentStack.push(new CountingStep(mDocumentValue, "record edit"));
            ASSERT_EQ(mRouting.active(), UndoRouting::Target::Document);
            mRouting.noteFocus(true);
            EXPECT_EQ(mRouting.active(), UndoRouting::Target::Module);
            mRouting.noteFocus(false);
            EXPECT_EQ(mRouting.active(), UndoRouting::Target::Document);
        }

        TEST_F(UndoRoutingTest, aModuleStackWithOnlyAnUndoneStepStillCountsAsHoldingOne)
        {
            mRouting.moduleStack().push(new CountingStep(mModuleValue, "module edit"));
            mRouting.moduleStack().undo();
            mRouting.noteFocus(false);
            ASSERT_EQ(mRouting.active(), UndoRouting::Target::Document);
            mRouting.noteFocus(true);
            EXPECT_EQ(mRouting.active(), UndoRouting::Target::Module) << "its redo is still there";
        }

        TEST_F(UndoRoutingTest, clearModuleStackEmptiesItAndReturnsToTheDocument)
        {
            mDocumentStack.push(new CountingStep(mDocumentValue, "record edit"));
            mRouting.moduleStack().push(new CountingStep(mModuleValue, "module edit"));
            ASSERT_EQ(mRouting.active(), UndoRouting::Target::Module);
            mRouting.clearModuleStack();
            EXPECT_EQ(mRouting.moduleStack().count(), 0);
            EXPECT_EQ(mRouting.active(), UndoRouting::Target::Document);
            EXPECT_EQ(mDocumentStack.count(), 1) << "the document's steps are not touched";
        }

        TEST_F(UndoRoutingTest, canUndoAndRedoFollowTheActiveStack)
        {
            mDocumentStack.push(new CountingStep(mDocumentValue, "record edit"));
            EXPECT_TRUE(mRouting.canUndo());
            mRouting.activate(UndoRouting::Target::Module);
            EXPECT_FALSE(mRouting.canUndo()) << "the module stack is empty";
            mRouting.moduleStack().push(new CountingStep(mModuleValue, "module edit"));
            mRouting.moduleStack().undo();
            EXPECT_FALSE(mRouting.canUndo());
            EXPECT_TRUE(mRouting.canRedo());
        }

        TEST(UndoRoutingLifetime, theRoutingMayBeDestroyedBeforeOrAfterItsStacksWithoutHarm)
        {
            int value = 0;
            {
                QUndoStack stack;
                auto* routing = new UndoRouting(stack);
                routing->moduleStack().push(new CountingStep(value, "module edit"));
                stack.push(new CountingStep(value, "record edit"));
                delete routing; // the document's stack outlives it and still works
                stack.undo();
                stack.redo();
            }
            EXPECT_EQ(value, 2);
        }
    }
}
