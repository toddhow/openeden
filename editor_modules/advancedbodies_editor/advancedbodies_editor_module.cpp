// AdvancedBodies' editor module (task 0069, stage f): the body package panel. A separate library that Eden Studio loads
// from its fixed list of core systems (decision 0007); the game never does. It opens a body package (its folder's
// body.json, or a .edenpak) or the merged body cache (its `current`, or a merged-<digest>.edenpak), lists the
// validator's findings and the stage a numbers in its panel, and draws the regions and weld seams as gizmos.

#include "bodyview.hpp"
#include "documents.hpp"

#include <openeden/editor_module/module.hpp>

namespace advancedbodies_editor
{
    using namespace openeden::editor;

    namespace
    {
        class AdvancedBodiesEditorModule final : public EditorModule
        {
        public:
            void registerUi(EditorRegistrar& registrar) override
            {
                mContext = &registrar.context();

                registrar.registerWorkspace({ "advancedbodies", "AdvancedBodies" });

                PanelDesc panel;
                panel.id = "advancedbodies.panel";
                panel.title = "Body Package";
                panel.workspaceId = "advancedbodies";
                panel.dock = DockArea::Right;
                // The active asset's rows, only when it is a body source of this module (another module's asset has
                // a panel of its own).
                panel.source = [this]() -> PropertySource* {
                    AssetDocument* active = mContext != nullptr ? mContext->activeAsset() : nullptr;
                    return active != nullptr && viewOf(*active) != nullptr ? &active->properties() : nullptr;
                };
                registrar.registerPanel(panel);

                // Which body of a merged cache is shown. Not an undo step: Studio's undo stack is the content
                // document's, and its clean flag is that document's modified state (task 0069 stage f, the amendment).
                const struct
                {
                    const char* mId;
                    const char* mMenuPath;
                    const char* mShortcut;
                    bool mForward;
                } steps[] = {
                    { "advancedbodies.nextBody", "Bodies/Next Body", "Ctrl+Alt+PgDown", true },
                    { "advancedbodies.previousBody", "Bodies/Previous Body", "Ctrl+Alt+PgUp", false },
                };
                for (const auto& step : steps)
                {
                    CommandDesc command;
                    command.id = step.mId;
                    command.label = step.mForward ? "Next Body" : "Previous Body";
                    command.menuPath = step.mMenuPath;
                    command.defaultShortcut = step.mShortcut;
                    const bool forward = step.mForward;
                    command.handler = [forward](EditorContext& context) {
                        AssetDocument* active = context.activeAsset();
                        const BodyView* view = active != nullptr ? viewOf(*active) : nullptr;
                        if (view == nullptr || view->bodyCount() < 2)
                        {
                            return;
                        }
                        const std::size_t count = view->bodyCount();
                        const std::size_t next
                            = forward ? (view->selected() + 1) % count : (view->selected() + count - 1) % count;
                        if (selectBody(*active, next))
                        {
                            context.requestRedraw();
                        }
                    };
                    command.enabledWhen = [this] {
                        AssetDocument* active = mContext != nullptr ? mContext->activeAsset() : nullptr;
                        const BodyView* view = active != nullptr ? viewOf(*active) : nullptr;
                        return view != nullptr && view->bodyCount() > 1;
                    };
                    registrar.registerCommand(command);
                }

                // What the regions gizmo draws: the fill and the edges, the fill alone, or the edges alone (Editor
                // Module API 1.1 gave it filled triangles). Not an undo step either.
                CommandDesc cycle;
                cycle.id = "advancedbodies.cycleRegionDisplay";
                cycle.label = "Cycle Region Display";
                cycle.menuPath = "Bodies/Cycle Region Display";
                cycle.defaultShortcut = "Ctrl+Alt+R";
                cycle.handler = [](EditorContext& context) {
                    AssetDocument* active = context.activeAsset();
                    if (active == nullptr || viewOf(*active) == nullptr)
                    {
                        return;
                    }
                    const RegionDisplay next = regionDisplay(*active) == RegionDisplay::FillAndEdges
                        ? RegionDisplay::Fill
                        : (regionDisplay(*active) == RegionDisplay::Fill ? RegionDisplay::Edges
                                                                         : RegionDisplay::FillAndEdges);
                    if (setRegionDisplay(*active, next))
                    {
                        context.requestRedraw();
                    }
                };
                cycle.enabledWhen = [this] {
                    AssetDocument* active = mContext != nullptr ? mContext->activeAsset() : nullptr;
                    return active != nullptr && viewOf(*active) != nullptr;
                };
                registrar.registerCommand(cycle);

                const struct
                {
                    const char* mFormat;
                    const char* mSuffix;
                    const char* mLabel;
                } types[] = {
                    { kPackageFormat, kPackageSuffix, "AdvancedBodies body package (its body.json)" },
                    { kPackFormat, kPackSuffix, "AdvancedBodies packed body package or merged body cache" },
                    { kCacheFormat, kCacheSuffix, "AdvancedBodies merged body cache (its current pointer)" },
                };
                for (const auto& type : types)
                {
                    AssetTypeDesc asset;
                    asset.formatId = type.mFormat;
                    asset.fileExtension = type.mSuffix;
                    asset.label = type.mLabel;
                    asset.open = &openBodyAsset;
                    registrar.registerAssetType(asset);

                    InspectorSectionDesc section;
                    section.id = std::string("advancedbodies.section.") + type.mFormat;
                    section.title = "Body";
                    section.selectionKind = type.mFormat;
                    section.create = [](AssetDocument& document) -> PropertySource* { return summaryOf(document); };
                    registrar.registerInspectorSection(section);
                }
            }

            void shutdown() override { mContext = nullptr; }

        private:
            EditorContext* mContext = nullptr;
        };

        EditorModule* createModule()
        {
            return new AdvancedBodiesEditorModule();
        }

        void destroyModule(EditorModule* module)
        {
            delete module;
        }

        const EditorModuleDescriptor sDescriptor{ openeden::editor::kApiVersion, kModuleId, &createModule,
            &destroyModule };
    }

    const EditorModuleDescriptor* moduleDescriptor()
    {
        return &sDescriptor;
    }
}
