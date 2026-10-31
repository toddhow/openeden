#include "editor_service.hpp"

#include <algorithm>

namespace openeden::extension
{
    EditorServiceImpl::EditorServiceImpl(bool editorActive)
        : mEditorActive(editorActive)
    {
    }

    bool EditorServiceImpl::isEditorActive() const noexcept
    {
        return mEditorActive;
    }

    void EditorServiceImpl::registerWorkspace(
        std::string_view extensionId, std::shared_ptr<openeden::sdk::EditorWorkspace> workspace)
    {
        if (!mEditorActive || !workspace)
            return;

        mWorkspaces.push_back({ std::string(extensionId), std::move(workspace) });
    }

    void EditorServiceImpl::registerInspector(
        std::string_view extensionId, std::shared_ptr<openeden::sdk::EditorInspector> inspector)
    {
        if (!mEditorActive || !inspector)
            return;

        mInspectors.push_back({ std::string(extensionId), std::move(inspector) });
    }

    void EditorServiceImpl::registerGizmo(
        std::string_view extensionId, std::shared_ptr<openeden::sdk::EditorGizmo> gizmo)
    {
        if (!mEditorActive || !gizmo)
            return;

        mGizmos.push_back({ std::string(extensionId), std::move(gizmo) });
    }

    void EditorServiceImpl::unregisterEditorHooks(std::string_view extensionId)
    {
        auto match = [extensionId](const auto& entry) { return entry.extensionId == extensionId; };
        mWorkspaces.erase(std::remove_if(mWorkspaces.begin(), mWorkspaces.end(), match), mWorkspaces.end());
        mInspectors.erase(std::remove_if(mInspectors.begin(), mInspectors.end(), match), mInspectors.end());
        mGizmos.erase(std::remove_if(mGizmos.begin(), mGizmos.end(), match), mGizmos.end());
    }

    std::vector<std::shared_ptr<openeden::sdk::EditorWorkspace>> EditorServiceImpl::getWorkspaces() const
    {
        if (!mEditorActive)
            return {};

        std::vector<std::shared_ptr<openeden::sdk::EditorWorkspace>> result;
        result.reserve(mWorkspaces.size());
        for (const auto& entry : mWorkspaces)
        {
            result.push_back(entry.workspace);
        }
        return result;
    }

    std::vector<std::shared_ptr<openeden::sdk::EditorInspector>> EditorServiceImpl::getInspectors() const
    {
        if (!mEditorActive)
            return {};

        std::vector<std::shared_ptr<openeden::sdk::EditorInspector>> result;
        result.reserve(mInspectors.size());
        for (const auto& entry : mInspectors)
        {
            result.push_back(entry.inspector);
        }
        return result;
    }

    std::vector<std::shared_ptr<openeden::sdk::EditorGizmo>> EditorServiceImpl::getGizmos() const
    {
        if (!mEditorActive)
            return {};

        std::vector<std::shared_ptr<openeden::sdk::EditorGizmo>> result;
        result.reserve(mGizmos.size());
        for (const auto& entry : mGizmos)
        {
            result.push_back(entry.gizmo);
        }
        return result;
    }

    std::shared_ptr<openeden::sdk::EditorWorkspace> EditorServiceImpl::findWorkspace(std::string_view id) const
    {
        if (!mEditorActive)
            return nullptr;

        for (const auto& entry : mWorkspaces)
        {
            if (entry.workspace && entry.workspace->getId() == id)
            {
                return entry.workspace;
            }
        }
        return nullptr;
    }

    std::shared_ptr<openeden::sdk::EditorInspector> EditorServiceImpl::findInspector(
        std::string_view targetTypeName) const
    {
        if (!mEditorActive)
            return nullptr;

        for (const auto& entry : mInspectors)
        {
            if (entry.inspector && entry.inspector->getTargetTypeName() == targetTypeName)
            {
                return entry.inspector;
            }
        }
        return nullptr;
    }

    std::shared_ptr<openeden::sdk::EditorGizmo> EditorServiceImpl::findGizmo(std::string_view gizmoId) const
    {
        if (!mEditorActive)
            return nullptr;

        for (const auto& entry : mGizmos)
        {
            if (entry.gizmo && entry.gizmo->getGizmoId() == gizmoId)
            {
                return entry.gizmo;
            }
        }
        return nullptr;
    }

    void EditorServiceImpl::releaseExtensionObjects()
    {
        mWorkspaces.clear();
        mInspectors.clear();
        mGizmos.clear();
    }
} // namespace openeden::extension
