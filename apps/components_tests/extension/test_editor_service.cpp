#include <components/extension/editor_service.hpp>
#include <components/extension/host_impl.hpp>
#include <openeden/editor.hpp>
#include <openeden/host.hpp>

#include <gtest/gtest.h>

#include <memory>
#include <string>
#include <string_view>
#include <vector>

namespace
{
    using openeden::extension::EditorServiceImpl;
    using openeden::extension::HostImpl;
    using openeden::sdk::EditorGizmo;
    using openeden::sdk::EditorInspector;
    using openeden::sdk::EditorService;
    using openeden::sdk::EditorWorkspace;

    class TestWorkspace : public EditorWorkspace
    {
    public:
        TestWorkspace(std::string id, std::string title)
            : mId(std::move(id))
            , mTitle(std::move(title))
        {
        }

        [[nodiscard]] std::string_view getId() const noexcept override { return mId; }
        [[nodiscard]] std::string_view getTitle() const noexcept override { return mTitle; }

    private:
        std::string mId;
        std::string mTitle;
    };

    class TestInspector : public EditorInspector
    {
    public:
        explicit TestInspector(std::string targetTypeName)
            : mTargetTypeName(std::move(targetTypeName))
        {
        }

        [[nodiscard]] std::string_view getTargetTypeName() const noexcept override { return mTargetTypeName; }

    private:
        std::string mTargetTypeName;
    };

    class TestGizmo : public EditorGizmo
    {
    public:
        explicit TestGizmo(std::string gizmoId)
            : mGizmoId(std::move(gizmoId))
        {
        }

        [[nodiscard]] std::string_view getGizmoId() const noexcept override { return mGizmoId; }

    private:
        std::string mGizmoId;
    };

    TEST(EditorServiceTest, InactiveByDefaultForGameHost)
    {
        EditorServiceImpl gameHostService(/*editorActive=*/false);

        EXPECT_FALSE(gameHostService.isEditorActive());

        auto workspace = std::make_shared<TestWorkspace>("anim_editor", "Animation Editor");
        auto inspector = std::make_shared<TestInspector>("MotionClip");
        auto gizmo = std::make_shared<TestGizmo>("bone_gizmo");

        // When editor is inactive (game / headless host), registrations must be safely accepted or no-oped
        EXPECT_NO_THROW(gameHostService.registerWorkspace("creativemotion", workspace));
        EXPECT_NO_THROW(gameHostService.registerInspector("creativemotion", inspector));
        EXPECT_NO_THROW(gameHostService.registerGizmo("creativemotion", gizmo));

        // When inactive, querying hooks returns empty collections
        EXPECT_TRUE(gameHostService.getWorkspaces().empty());
        EXPECT_TRUE(gameHostService.getInspectors().empty());
        EXPECT_TRUE(gameHostService.getGizmos().empty());
        EXPECT_EQ(gameHostService.findWorkspace("anim_editor"), nullptr);
        EXPECT_EQ(gameHostService.findInspector("MotionClip"), nullptr);
        EXPECT_EQ(gameHostService.findGizmo("bone_gizmo"), nullptr);
    }

    TEST(EditorServiceTest, ActiveInEdenStudioHost)
    {
        EditorServiceImpl studioHostService(/*editorActive=*/true);

        EXPECT_TRUE(studioHostService.isEditorActive());

        auto workspace = std::make_shared<TestWorkspace>("anim_editor", "Animation Editor");
        auto inspector = std::make_shared<TestInspector>("MotionClip");
        auto gizmo = std::make_shared<TestGizmo>("bone_gizmo");

        studioHostService.registerWorkspace("creativemotion", workspace);
        studioHostService.registerInspector("creativemotion", inspector);
        studioHostService.registerGizmo("creativemotion", gizmo);

        EXPECT_EQ(studioHostService.getWorkspaces().size(), 1u);
        EXPECT_EQ(studioHostService.getInspectors().size(), 1u);
        EXPECT_EQ(studioHostService.getGizmos().size(), 1u);
    }

    TEST(EditorServiceTest, WorkspaceRegistrationAndLookup)
    {
        EditorServiceImpl service(/*editorActive=*/true);

        auto ws1 = std::make_shared<TestWorkspace>("animgraph_editor", "Animation Graph Editor");
        auto ws2 = std::make_shared<TestWorkspace>("physics_studio", "Physics Debug Studio");

        service.registerWorkspace("creativemotion", ws1);
        service.registerWorkspace("jolt_physics", ws2);

        EXPECT_EQ(service.getWorkspaces().size(), 2u);

        auto foundWs1 = service.findWorkspace("animgraph_editor");
        ASSERT_NE(foundWs1, nullptr);
        EXPECT_EQ(foundWs1->getId(), "animgraph_editor");
        EXPECT_EQ(foundWs1->getTitle(), "Animation Graph Editor");

        auto foundWs2 = service.findWorkspace("physics_studio");
        ASSERT_NE(foundWs2, nullptr);
        EXPECT_EQ(foundWs2->getId(), "physics_studio");

        EXPECT_EQ(service.findWorkspace("nonexistent"), nullptr);
    }

    TEST(EditorServiceTest, InspectorRegistrationAndLookup)
    {
        EditorServiceImpl service(/*editorActive=*/true);

        auto ins1 = std::make_shared<TestInspector>("BlendTree");
        auto ins2 = std::make_shared<TestInspector>("RagdollProfile");

        service.registerInspector("creativemotion", ins1);
        service.registerInspector("creativemotion", ins2);

        EXPECT_EQ(service.getInspectors().size(), 2u);

        auto found = service.findInspector("BlendTree");
        ASSERT_NE(found, nullptr);
        EXPECT_EQ(found->getTargetTypeName(), "BlendTree");

        EXPECT_EQ(service.findInspector("UnknownTarget"), nullptr);
    }

    TEST(EditorServiceTest, GizmoRegistrationAndLookup)
    {
        EditorServiceImpl service(/*editorActive=*/true);

        auto gizmo = std::make_shared<TestGizmo>("bone_orientation_gizmo");
        service.registerGizmo("creativemotion", gizmo);

        EXPECT_EQ(service.getGizmos().size(), 1u);

        auto found = service.findGizmo("bone_orientation_gizmo");
        ASSERT_NE(found, nullptr);
        EXPECT_EQ(found->getGizmoId(), "bone_orientation_gizmo");

        EXPECT_EQ(service.findGizmo("unregistered_gizmo"), nullptr);
    }

    TEST(EditorServiceTest, UnregisterEditorHooksByExtension)
    {
        EditorServiceImpl service(/*editorActive=*/true);

        auto ws1 = std::make_shared<TestWorkspace>("ws_motion", "Motion");
        auto ins1 = std::make_shared<TestInspector>("TypeMotion");
        auto gizmo1 = std::make_shared<TestGizmo>("gizmo_motion");

        auto ws2 = std::make_shared<TestWorkspace>("ws_terrain", "Terrain");
        auto ins2 = std::make_shared<TestInspector>("TypeTerrain");
        auto gizmo2 = std::make_shared<TestGizmo>("gizmo_terrain");

        service.registerWorkspace("creativemotion", ws1);
        service.registerInspector("creativemotion", ins1);
        service.registerGizmo("creativemotion", gizmo1);

        service.registerWorkspace("extension_terrain", ws2);
        service.registerInspector("extension_terrain", ins2);
        service.registerGizmo("extension_terrain", gizmo2);

        EXPECT_EQ(service.getWorkspaces().size(), 2u);
        EXPECT_EQ(service.getInspectors().size(), 2u);
        EXPECT_EQ(service.getGizmos().size(), 2u);

        // Unregister only creativemotion
        service.unregisterEditorHooks("creativemotion");

        EXPECT_EQ(service.getWorkspaces().size(), 1u);
        EXPECT_EQ(service.getInspectors().size(), 1u);
        EXPECT_EQ(service.getGizmos().size(), 1u);

        EXPECT_EQ(service.findWorkspace("ws_motion"), nullptr);
        EXPECT_EQ(service.findInspector("TypeMotion"), nullptr);
        EXPECT_EQ(service.findGizmo("gizmo_motion"), nullptr);

        EXPECT_NE(service.findWorkspace("ws_terrain"), nullptr);
        EXPECT_NE(service.findInspector("TypeTerrain"), nullptr);
        EXPECT_NE(service.findGizmo("gizmo_terrain"), nullptr);
    }

    TEST(EditorServiceTest, NullEditorHookPointersIgnoredSafely)
    {
        EditorServiceImpl service(/*editorActive=*/true);

        EXPECT_NO_THROW(service.registerWorkspace("extension_test", nullptr));
        EXPECT_NO_THROW(service.registerInspector("extension_test", nullptr));
        EXPECT_NO_THROW(service.registerGizmo("extension_test", nullptr));

        EXPECT_TRUE(service.getWorkspaces().empty());
        EXPECT_TRUE(service.getInspectors().empty());
        EXPECT_TRUE(service.getGizmos().empty());
    }

    TEST(EditorServiceTest, HostImplWiresEditorService)
    {
        HostImpl host("OpenEdenHost");
        EditorService* editor = host.getEditorService();

        ASSERT_NE(editor, nullptr);
        // Default host implementation behaves as game host with editor inactive
        EXPECT_FALSE(editor->isEditorActive());

        auto ws = std::make_shared<TestWorkspace>("host_ws", "Host Workspace");
        EXPECT_NO_THROW(editor->registerWorkspace("host_extension", ws));
    }
}
