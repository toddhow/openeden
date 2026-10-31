#pragma once

#include <openeden/editor.hpp>

#include <memory>
#include <string>
#include <string_view>
#include <vector>

namespace openeden::plugin
{
    /**
     * @brief Engine implementation of editor extension points.
     */
    class EditorServiceImpl : public openeden::sdk::EditorService
    {
    public:
        explicit EditorServiceImpl(bool editorActive = false);
        ~EditorServiceImpl() override = default;

        // Non-copyable
        EditorServiceImpl(const EditorServiceImpl&) = delete;
        EditorServiceImpl& operator=(const EditorServiceImpl&) = delete;

        // Movable
        EditorServiceImpl(EditorServiceImpl&&) noexcept = default;
        EditorServiceImpl& operator=(EditorServiceImpl&&) noexcept = default;

        [[nodiscard]] bool isEditorActive() const noexcept override;

        void registerWorkspace(
            std::string_view pluginId, std::shared_ptr<openeden::sdk::EditorWorkspace> workspace) override;

        void registerInspector(
            std::string_view pluginId, std::shared_ptr<openeden::sdk::EditorInspector> inspector) override;

        void registerGizmo(std::string_view pluginId, std::shared_ptr<openeden::sdk::EditorGizmo> gizmo) override;

        void unregisterExtensions(std::string_view pluginId) override;

        /// Drops every object a plugin left here, while its library is still loaded.
        void releasePluginObjects();

        [[nodiscard]] std::vector<std::shared_ptr<openeden::sdk::EditorWorkspace>> getWorkspaces() const override;
        [[nodiscard]] std::vector<std::shared_ptr<openeden::sdk::EditorInspector>> getInspectors() const override;
        [[nodiscard]] std::vector<std::shared_ptr<openeden::sdk::EditorGizmo>> getGizmos() const override;

        [[nodiscard]] std::shared_ptr<openeden::sdk::EditorWorkspace> findWorkspace(std::string_view id) const override;

        [[nodiscard]] std::shared_ptr<openeden::sdk::EditorInspector> findInspector(
            std::string_view targetTypeName) const override;

        [[nodiscard]] std::shared_ptr<openeden::sdk::EditorGizmo> findGizmo(std::string_view gizmoId) const override;

    private:
        struct WorkspaceEntry
        {
            std::string pluginId;
            std::shared_ptr<openeden::sdk::EditorWorkspace> workspace;
        };

        struct InspectorEntry
        {
            std::string pluginId;
            std::shared_ptr<openeden::sdk::EditorInspector> inspector;
        };

        struct GizmoEntry
        {
            std::string pluginId;
            std::shared_ptr<openeden::sdk::EditorGizmo> gizmo;
        };

        bool mEditorActive;
        std::vector<WorkspaceEntry> mWorkspaces;
        std::vector<InspectorEntry> mInspectors;
        std::vector<GizmoEntry> mGizmos;
    };
} // namespace openeden::plugin
