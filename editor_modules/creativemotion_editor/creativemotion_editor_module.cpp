// CreativeMotion's editor module (docs/tasks/0032): the Rig & Motor Limits Inspector of the plan's section 5.1. It is
// a separate library that Eden Studio loads from its fixed list of core systems (decision 0007); the game never does.

#include "documents.hpp"

#include <openeden/editor_module/module.hpp>

namespace creativemotion_editor
{
    using namespace openeden::editor;

    namespace
    {
        constexpr const char* kJointKind = "joint";

        class CreativeMotionEditorModule final : public EditorModule
        {
        public:
            void registerUi(EditorRegistrar& registrar) override
            {
                mContext = &registrar.context();

                registrar.registerWorkspace({ "creativemotion", "CreativeMotion" });

                PanelDesc panel;
                panel.id = "creativemotion.panel";
                panel.title = "Rig and Motor Limits";
                panel.workspaceId = "creativemotion";
                panel.dock = DockArea::Right;
                // The panel shows the active rig or profile.
                panel.source = [this]() -> PropertySource* {
                    AssetDocument* active = mContext != nullptr ? mContext->activeAsset() : nullptr;
                    return active != nullptr ? &active->properties() : nullptr;
                };
                registrar.registerPanel(panel);

                CommandDesc save;
                save.id = "creativemotion.save";
                save.label = "Save";
                save.menuPath = "Rig/Save";
                save.defaultShortcut = "Ctrl+Alt+S";
                save.enabledWhen = [this] {
                    AssetDocument* active = mContext != nullptr ? mContext->activeAsset() : nullptr;
                    return active != nullptr && active->isDirty();
                };
                save.handler = [](EditorContext& context) {
                    AssetDocument* active = context.activeAsset();
                    if (active == nullptr)
                        return;
                    std::string error;
                    if (active->save(error))
                        context.log(LogLevel::Info, std::string("Saved ") + std::string(active->assetId()));
                    else
                        context.log(LogLevel::Error, error);
                };
                registrar.registerCommand(save);

                // Joint selection (API 1.2, task 0091): the host owns the selection; the module narrows the active rig.
                for (const bool next : { true, false })
                {
                    CommandDesc walk;
                    walk.id = next ? "creativemotion.nextJoint" : "creativemotion.previousJoint";
                    walk.label = next ? "Next Joint" : "Previous Joint";
                    walk.menuPath = next ? "Rig/Next Joint" : "Rig/Previous Joint";
                    walk.enabledWhen = [this] { return activeRig() != nullptr; };
                    walk.handler = [this, next](EditorContext& context) { walkJoints(context, next); };
                    registrar.registerCommand(walk);
                }
                registrar.subscribe([this](const EditorEvent& event) { onEvent(event); });

                GizmoProviderDesc gizmos;
                gizmos.id = "creativemotion.gizmos";
                gizmos.gizmos = [this]() -> std::vector<Gizmo*> {
                    AssetDocument* active = mContext != nullptr ? mContext->activeAsset() : nullptr;
                    return active != nullptr ? active->gizmos() : std::vector<Gizmo*>();
                };
                registrar.registerGizmoProvider(gizmos);

                AssetTypeDesc rig;
                rig.formatId = kRigFormat;
                rig.fileExtension = ".rig.json";
                rig.label = "CreativeMotion rig";
                rig.open = &openRig;
                registrar.registerAssetType(rig);

                AssetTypeDesc profile;
                profile.formatId = kProfileFormat;
                profile.fileExtension = ".profile.json";
                profile.label = "CreativeMotion profile";
                profile.open = &openProfile;
                registrar.registerAssetType(profile);

                for (const char* format : { kRigFormat, kProfileFormat })
                {
                    InspectorSectionDesc section;
                    section.id = std::string("creativemotion.section.") + format;
                    section.title = format == std::string(kRigFormat) ? "Rig and joint limits" : "Motors";
                    section.selectionKind = format;
                    section.create = [](AssetDocument& document) -> PropertySource* { return &document.properties(); };
                    registrar.registerInspectorSection(section);
                }
            }

            void shutdown() override { mContext = nullptr; }

        private:
            JointSelectable* activeRig() const
            {
                AssetDocument* active = mContext != nullptr ? mContext->activeAsset() : nullptr;
                return dynamic_cast<JointSelectable*>(active);
            }

            void walkJoints(EditorContext& context, bool next)
            {
                JointSelectable* rig = activeRig();
                if (rig == nullptr)
                    return;
                const std::vector<std::string> names = rig->jointNames();
                if (names.empty())
                    return;
                const Selection current = context.selection();
                // From nothing, next lands on the first joint and previous on the last.
                std::size_t target = next ? 0 : names.size() - 1;
                if (current.kind == kJointKind)
                    for (std::size_t i = 0; i < names.size(); ++i)
                        if (names[i] == current.id)
                            target = next ? (i + 1) % names.size() : (i + names.size() - 1) % names.size();
                context.select({ kJointKind, names[target] });
            }

            void onEvent(const EditorEvent& event)
            {
                if (event.type != EditorEventType::SelectionChanged || mContext == nullptr)
                    return;
                JointSelectable* rig = activeRig();
                if (rig == nullptr)
                    return;
                if (event.subject == "handle" && event.detail.rfind(kJointHandlePrefix, 0) == 0)
                {
                    // A click on a joint's handle: select that joint (queued behind this event).
                    mContext->select({ kJointKind, event.detail.substr(std::string(kJointHandlePrefix).size()) });
                    return;
                }
                rig->setSelectedJoint(event.subject == kJointKind ? event.detail : std::string());
                mContext->requestRedraw();
            }

            EditorContext* mContext = nullptr;
        };

        EditorModule* createModule()
        {
            return new CreativeMotionEditorModule();
        }

        void destroyModule(EditorModule* module)
        {
            delete module;
        }

        const EditorModuleDescriptor sDescriptor{ openeden::editor::kApiVersion, "creativemotion_editor", &createModule,
            &destroyModule };
    }

    const EditorModuleDescriptor* moduleDescriptor()
    {
        return &sDescriptor;
    }
}
