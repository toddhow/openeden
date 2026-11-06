// Task 0091, stage c: a drag of a gizmo handle is one undo step. Written from the spec alone, against a stub.
//
// Pinned: the handle moves on the camera-facing plane through its start, keeping the grab offset; every update undoes
// the previous preview and redoes the gizmo's new command; finish() leaves the document as it was and returns the
// last command for the host to push once; cancel() restores; a refused position or a parallel ray changes nothing;
// the destructor undoes a live preview; a throwing gizmo leaves no preview applied.
#include "apps/opencs/model/module/dragsession.hpp"

#include <gtest/gtest.h>

#include <stdexcept>
#include <string>

namespace
{
    using namespace CSMModule;
    using namespace openeden::editor;

    /// A gizmo with one handle whose position a command changes (captured at construction, as the sample does).
    class FakeGizmo final : public Gizmo
    {
    public:
        class MoveCommand final : public Command
        {
        public:
            MoveCommand(FakeGizmo& gizmo, Vec3 to)
                : mGizmo(gizmo)
                , mOld(gizmo.mPosition)
                , mNew(to)
            {
            }
            std::string_view label() const override { return "Move"; }
            void redo() override
            {
                mGizmo.mPosition = mNew;
                ++mGizmo.mRedos;
            }
            void undo() override
            {
                mGizmo.mPosition = mOld;
                ++mGizmo.mUndos;
            }

        private:
            FakeGizmo& mGizmo;
            Vec3 mOld;
            Vec3 mNew;
        };

        std::string_view id() const override { return "fake"; }
        void draw(OverlaySink&) const override {}
        std::vector<GizmoHandle> handles() const override { return { { "h", mPosition } }; }
        std::unique_ptr<Command> drag(std::string_view handleId, const Vec3& to) override
        {
            ++mDragCalls;
            if (mThrow)
                throw std::runtime_error("drag boom");
            if (handleId != "h" || mRefuse)
                return nullptr;
            return std::make_unique<MoveCommand>(*this, to);
        }

        Vec3 mPosition{ 1, 0, 2 };
        int mRedos = 0;
        int mUndos = 0;
        int mDragCalls = 0;
        bool mRefuse = false;
        bool mThrow = false;
    };

    // A camera looking along +y from y = -10: the press ray hits the drag plane (normal along the ray) at the origin.
    PickRay pressRay()
    {
        return { { 0, -10, 0 }, { 0, 1, 0 } };
    }

    PickRay rayAt(double x, double z)
    {
        return { { x, -10, z }, { 0, 1, 0 } };
    }

    void expectNear(const Vec3& a, const Vec3& b)
    {
        EXPECT_NEAR(a.x, b.x, 1e-9);
        EXPECT_NEAR(a.y, b.y, 1e-9);
        EXPECT_NEAR(a.z, b.z, 1e-9);
    }

    TEST(CSMDragSession, theHandleFollowsTheRayOnThePlaneKeepingTheGrabOffset)
    {
        FakeGizmo gizmo;
        DragSession session(gizmo, "h", gizmo.mPosition, pressRay());
        ASSERT_TRUE(session.valid());
        EXPECT_EQ(session.handleId(), "h");
        EXPECT_FALSE(session.changed());

        ASSERT_TRUE(session.update(rayAt(3, 5)));
        // press hit (0,0,0); handle (1,0,2): offset (1,0,2); the ray now hits (3,0,5) -> target (4,0,7)
        expectNear(gizmo.mPosition, { 4, 0, 7 });
        expectNear(session.position(), { 4, 0, 7 });
        EXPECT_TRUE(session.changed());
    }

    TEST(CSMDragSession, everyUpdateUndoesThePreviousPreviewSoTheCommandsDoNotStack)
    {
        FakeGizmo gizmo;
        DragSession session(gizmo, "h", gizmo.mPosition, pressRay());
        session.update(rayAt(1, 1));
        session.update(rayAt(2, 2));
        session.update(rayAt(3, 3));
        EXPECT_EQ(gizmo.mRedos, 3);
        EXPECT_EQ(gizmo.mUndos, 2);
        expectNear(gizmo.mPosition, { 4, 0, 5 });
    }

    TEST(CSMDragSession, finishRestoresTheDocumentAndHandsOverOneCommandForTheHostToPush)
    {
        FakeGizmo gizmo;
        DragSession session(gizmo, "h", gizmo.mPosition, pressRay());
        session.update(rayAt(1, 1));
        session.update(rayAt(3, 3));

        std::unique_ptr<Command> command = session.finish();
        ASSERT_NE(command, nullptr);
        expectNear(gizmo.mPosition, { 1, 0, 2 }); // undone: the host's push runs redo()
        command->redo();
        expectNear(gizmo.mPosition, { 4, 0, 5 });
        command->undo();
        expectNear(gizmo.mPosition, { 1, 0, 2 });
        EXPECT_EQ(session.finish(), nullptr); // the session ended
    }

    TEST(CSMDragSession, finishWithoutAnUpdateGivesNoCommand)
    {
        FakeGizmo gizmo;
        DragSession session(gizmo, "h", gizmo.mPosition, pressRay());
        EXPECT_EQ(session.finish(), nullptr);
        expectNear(gizmo.mPosition, { 1, 0, 2 });
        EXPECT_EQ(gizmo.mRedos, 0);
    }

    TEST(CSMDragSession, cancelRestoresAndEndsTheSession)
    {
        FakeGizmo gizmo;
        DragSession session(gizmo, "h", gizmo.mPosition, pressRay());
        session.update(rayAt(3, 3));
        session.cancel();
        expectNear(gizmo.mPosition, { 1, 0, 2 });
        EXPECT_FALSE(session.changed());
        EXPECT_EQ(session.finish(), nullptr);
        EXPECT_FALSE(session.update(rayAt(4, 4))); // ended: nothing more happens
        expectNear(gizmo.mPosition, { 1, 0, 2 });
    }

    TEST(CSMDragSession, theDestructorUndoesALivePreview)
    {
        FakeGizmo gizmo;
        {
            DragSession session(gizmo, "h", gizmo.mPosition, pressRay());
            session.update(rayAt(3, 3));
            expectNear(gizmo.mPosition, { 4, 0, 5 });
        }
        expectNear(gizmo.mPosition, { 1, 0, 2 });
    }

    TEST(CSMDragSession, aRayParallelToThePlaneChangesNothing)
    {
        FakeGizmo gizmo;
        DragSession session(gizmo, "h", gizmo.mPosition, pressRay());
        session.update(rayAt(1, 1));
        const PickRay parallel{ { 0, -10, 0 }, { 1, 0, 0 } };
        EXPECT_FALSE(session.update(parallel));
        expectNear(gizmo.mPosition, { 2, 0, 3 }); // the earlier preview stays
    }

    TEST(CSMDragSession, aRefusedPositionKeepsTheLastAcceptedPreview)
    {
        FakeGizmo gizmo;
        DragSession session(gizmo, "h", gizmo.mPosition, pressRay());
        ASSERT_TRUE(session.update(rayAt(1, 1)));
        gizmo.mRefuse = true;
        EXPECT_FALSE(session.update(rayAt(9, 9)));
        EXPECT_TRUE(session.changed());
        expectNear(gizmo.mPosition, { 2, 0, 3 });
        gizmo.mRefuse = false;
        ASSERT_NE(session.finish(), nullptr);
    }

    TEST(CSMDragSession, aSessionWhosePressRayMissesThePlaneIsNotValidAndNeverMoves)
    {
        FakeGizmo gizmo;
        const PickRay away{ { 0, -10, 0 }, { 0, -1, 0 } }; // looks away from the handle's plane
        DragSession session(gizmo, "h", gizmo.mPosition, away);
        // The plane faces the ray, so a ray pointing away still meets it behind the origin: not valid.
        EXPECT_FALSE(session.valid());
        EXPECT_FALSE(session.update(rayAt(1, 1)));
        EXPECT_EQ(gizmo.mDragCalls, 0);
    }

    TEST(CSMDragSession, aThrowingGizmoLeavesNoPreviewAppliedAndTheExceptionPropagates)
    {
        FakeGizmo gizmo;
        DragSession session(gizmo, "h", gizmo.mPosition, pressRay());
        ASSERT_TRUE(session.update(rayAt(1, 1)));
        gizmo.mThrow = true;
        EXPECT_THROW(session.update(rayAt(2, 2)), std::runtime_error);
        expectNear(gizmo.mPosition, { 1, 0, 2 });
        EXPECT_FALSE(session.changed());
    }
}
