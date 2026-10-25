#pragma once

#include <memory>
#include <string_view>
#include <vector>

namespace openeden::sdk
{
    /**
     * @brief Abstract editor dockable workspace registered by a plugin for Eden Studio.
     */
    class EditorWorkspace
    {
    public:
        virtual ~EditorWorkspace() = default;

        [[nodiscard]] virtual std::string_view getId() const noexcept = 0;
        [[nodiscard]] virtual std::string_view getTitle() const noexcept = 0;
    };

    /**
     * @brief Abstract custom inspector registered for a specific data or asset type.
     */
    class EditorInspector
    {
    public:
        virtual ~EditorInspector() = default;

        [[nodiscard]] virtual std::string_view getTargetTypeName() const noexcept = 0;
    };

    /**
     * @brief Abstract viewport manipulator or gizmo registered by a plugin.
     */
    class EditorGizmo
    {
    public:
        virtual ~EditorGizmo() = default;

        [[nodiscard]] virtual std::string_view getGizmoId() const noexcept = 0;
    };

    /**
     * @brief Service managing editor extension points.
     * Inactive and safely no-oped in game and headless test hosts.
     */
    class EditorService
    {
    public:
        virtual ~EditorService() = default;

        [[nodiscard]] virtual bool isEditorActive() const noexcept = 0;

        virtual void registerWorkspace(std::string_view pluginId, std::shared_ptr<EditorWorkspace> workspace) = 0;
        virtual void registerInspector(std::string_view pluginId, std::shared_ptr<EditorInspector> inspector) = 0;
        virtual void registerGizmo(std::string_view pluginId, std::shared_ptr<EditorGizmo> gizmo) = 0;

        virtual void unregisterExtensions(std::string_view pluginId) = 0;

        [[nodiscard]] virtual std::vector<std::shared_ptr<EditorWorkspace>> getWorkspaces() const = 0;
        [[nodiscard]] virtual std::vector<std::shared_ptr<EditorInspector>> getInspectors() const = 0;
        [[nodiscard]] virtual std::vector<std::shared_ptr<EditorGizmo>> getGizmos() const = 0;

        [[nodiscard]] virtual std::shared_ptr<EditorWorkspace> findWorkspace(std::string_view id) const = 0;
        [[nodiscard]] virtual std::shared_ptr<EditorInspector> findInspector(std::string_view targetTypeName) const = 0;
        [[nodiscard]] virtual std::shared_ptr<EditorGizmo> findGizmo(std::string_view gizmoId) const = 0;
    };
} // namespace openeden::sdk
