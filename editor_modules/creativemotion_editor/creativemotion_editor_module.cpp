// CreativeMotion's editor module (docs/tasks/0032): the Rig & Motor Limits Inspector of the plan's section 5.1. It is
// a separate library that Eden Studio loads from its fixed list of core systems (decision 0007); the game never does.

#include "documents.hpp"

#include <openeden/editor_module/module.hpp>

namespace creativemotion_editor
{
    using namespace openeden::editor;

    namespace
    {
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
