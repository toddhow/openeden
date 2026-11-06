#ifndef CSV_MODULE_MODULEUI_H
#define CSV_MODULE_MODULEUI_H

#include <filesystem>
#include <functional>
#include <memory>
#include <optional>
#include <set>
#include <string>
#include <vector>

#include <QObject>
#include <QPointer>
#include <QUndoStack>

#include <openeden/editor_module/registrar.hpp>
#include <openeden/editor_module/types.hpp>

#include <apps/opencs/model/doc/undorouting.hpp>
#include <apps/opencs/model/module/dragsession.hpp>
#include <apps/opencs/model/module/eventbus.hpp>
#include <apps/opencs/model/module/overlaybuffer.hpp>
#include <apps/opencs/model/module/overlaypick.hpp>
#include <apps/opencs/model/physics/debuglinedrawer.hpp>

class QWidget;

namespace CSMDoc
{
    class Document;
}

namespace CSMModule
{
    class ModuleRegistry;
}

namespace CSVModule
{
    /// What Eden Studio gives the editor modules (task 0031, stage B): the EditorContext they call, the active asset,
    /// and the drawing of what they registered (property forms, overlay lines, timeline keys). Qt types stay here:
    /// the API itself has none.
    ///
    /// Commands a module pushes go on the active document's module undo stack (CSMDoc::UndoRouting, not the content
    /// stack, whose clean flag is the document's modified state) wrapped in a QUndoCommand, so Edit > Undo undoes them
    /// while that stack is the one touched last. The objects a module hands over (commands, the active asset) have
    /// code in the module's library, so releaseModuleObjects() must run before the library is unloaded.
    ///
    /// Owned by CS::Editor, after the registry and before the module host. Main thread only.
    class ModuleUi : public QObject, public openeden::editor::EditorContext
    {
        Q_OBJECT

    public:
        /// \param documentProvider the document commands go to (the active window's), or null if there is none.
        ModuleUi(CSMModule::ModuleRegistry& registry, std::function<CSMDoc::Document*()> documentProvider);
        ~ModuleUi() override;

        /// The dynamic property that marks a widget as part of the module UI (a property form): the focus moving into
        /// it makes Edit > Undo act on the module stack when that holds a step (CSMDoc::UndoRouting::noteFocus).
        static constexpr const char* sOwnedProperty = "edenModuleOwned";

        /// The editor's, or null (capture mode and tests have none).
        static ModuleUi* get() { return sInstance; }

        CSMModule::ModuleRegistry& registry() { return mRegistry; }

        // EditorContext
        void pushCommand(std::unique_ptr<openeden::editor::Command> command) override;
        void markDirty(std::string_view assetId) override;
        void log(openeden::editor::LogLevel level, std::string_view message) override;
        void requestRedraw() override;
        openeden::editor::AssetDocument* openAsset(const std::filesystem::path& path) override;
        openeden::editor::AssetDocument* activeAsset() override { return mActive.get(); }
        std::filesystem::path userDataPath() const override { return mUserDataPath; }
        // API 1.2
        openeden::editor::Selection selection() const override { return mSelection.current(); }
        void select(const openeden::editor::Selection& selection) override;
        void publish(const openeden::editor::EditorEvent& event) override;

        /// Studio publishes the physics world's step (called by CSVRender::PhysicsHub for each fixed step it ran).
        void publishSimulationStep(std::uint64_t step);

        /// Studio's user data folder (the configuration's `user-data`), which modules may ask for (API 1.1).
        void setUserDataPath(std::filesystem::path path) { mUserDataPath = std::move(path); }

        /// The format ID of the active asset's type, or empty.
        const std::string& activeFormat() const { return mActiveFormat; }
        /// The module that owns the active asset (the one whose asset type opened it), or empty with none open. A
        /// module's panels, gizmos and timeline tracks apply only while the owner is that module (or none is open).
        const std::string& activeOwner() const { return mActiveOwner; }

        /// A panel's property source, null while another module's document is the active one: CreativeMotion's panel
        /// never shows a body package, and the body panel never shows a rig.
        openeden::editor::PropertySource* panelSource(std::string_view panelId);
        const std::string& openError() const { return mOpenError; }
        /// Changes whenever the active asset is replaced or closed (an address may be reused, a generation is not).
        unsigned long generation() const { return mGeneration; }
        /// Changes with the generation and whenever the selection changes: what a panel or section shows is built
        /// again then (a source may describe fewer fields for a selected joint), but a view is not reframed.
        unsigned long layoutRevision() const { return mLayoutRevision; }

        /// Closes the active asset and empties the undo stacks that hold module commands. Call before the module
        /// host unloads the libraries.
        void releaseModuleObjects();

        /// Where the Asset Browser looks for module assets (the editor sets it: the data folders, and for checks the
        /// folder in OPENEDEN_CS_ASSETDIR).
        void setAssetRoots(std::vector<std::filesystem::path> roots) { mAssetRoots = std::move(roots); }
        const std::vector<std::filesystem::path>& assetRoots() const { return mAssetRoots; }

        /// Files under the roots that a registered asset type handles, sorted by path; with a non-empty  ownerFilter
        /// only those the module of that id registered a type for.
        std::vector<std::filesystem::path> findAssets(
            const std::vector<std::filesystem::path>& roots, const std::string& ownerFilter = std::string()) const;

        /// Which module's assets the Asset Browser lists: the module whose workspace tab is chosen, or empty (all
        /// modules) on the built-in tab. Changing it emits assetOwnerFilterChanged().
        const std::string& assetOwnerFilter() const { return mAssetOwnerFilter; }
        void setAssetOwnerFilter(std::string moduleId);

        /// Every gizmo of the active asset and of the registered providers drawn into lines, in the world's unit
        /// (Morrowind units), moved by \a origin (the world position that module coordinates are relative to). This is
        /// the one place where the modules' meters become world units.
        std::vector<CSMPhysics::DebugLine> overlayLines(const float origin[3]) const;

        /// The same drawing with its filled triangles (Editor Module API 1.1) and the counts of what Studio refused or
        /// dropped (CSMModule::OverlayBuffer). overlayLines() is this one's lines.
        CSMModule::OverlayData overlay(const float origin[3]) const;

        /// Handle picking and dragging in a 3D view (API 1.2, task 0091 stage c). Rays are in the world's unit, as the
        /// view makes them from a mouse position; \a origin is the point that module coordinates are relative to (the
        /// one given to overlay()), \a slope the pixel tolerance as CSMModule::pickSlope. The press either takes a
        /// handle (true: the view must not start a camera drag) or leaves the mouse to the view.
        bool pressHandle(const CSMModule::PickRay& ray, double slope, const float origin[3]);
        /// A move with the button down: the first one past the click tolerance starts the drag, then each moves the
        /// preview. Does nothing without a pressed handle.
        void dragHandle(const CSMModule::PickRay& ray, bool pastClickTolerance);
        /// The release: ends a drag with ONE command pushed on the module undo stack, or, if the mouse never moved
        /// past the tolerance, selects the handle ({"handle", id} on the bus). \return whether a handle was pressed.
        bool releaseHandle(const CSMModule::PickRay& ray);
        /// Undoes any preview and forgets the press.
        void cancelHandle();
        /// Highlights the handle under a mouse that moves with no button down (nothing hit clears it). \return whether
        /// the highlighted handle changed (the view then draws again).
        bool hoverHandle(const CSMModule::PickRay& ray, double slope, const float origin[3]);
        /// The id of the handle that is pressed or highlighted, or empty.
        std::string highlightedHandle() const { return mHighlightId; }
        bool isDraggingHandle() const { return mDrag != nullptr; }

        /// The keys of every track of the active asset and of the registered providers: (track title, key).
        std::vector<std::pair<std::string, openeden::editor::TimelineKey>> timelineKeys() const;

        /// A form for a property source: one editor widget per described property, writing through
        /// makeSetCommand. It follows changed() and is rebuilt if the source's description changes. Owned by \a parent.
        QWidget* makePropertyForm(openeden::editor::PropertySource& source, QWidget* parent);

        /// The same form in a scroll area of at most \a maxHeight pixels (0: no limit), so that a source with dozens of
        /// fields does not stretch the window it is in.
        QWidget* makeScrollingForm(openeden::editor::PropertySource& source, QWidget* parent, int maxHeight);

    signals:
        void assetOwnerFilterChanged();

        /// The active asset, or something in it, changed: panels, overlay and timeline redraw.
        void changed();

    private:
        class UndoCommand;

        struct HandleRef
        {
            openeden::editor::Gizmo* mGizmo = nullptr;
            std::string mId;
            openeden::editor::Vec3 mPosition; ///< Meters.
        };

        std::vector<openeden::editor::Gizmo*> activeGizmos() const;
        std::vector<HandleRef> collectHandles() const;
        std::optional<HandleRef> pickHandle(const CSMModule::PickRay& metersRay, double slopeMeters) const;
        CSMModule::PickRay toMeters(const CSMModule::PickRay& worldRay, const float origin[3]) const;
        void setHighlight(const std::string& id, const openeden::editor::Vec3& position);
        void drawHighlight(CSMModule::OverlayBuffer& sink) const;

        CSMModule::ModuleRegistry& mRegistry;
        std::function<CSMDoc::Document*()> mDocumentProvider;
        std::unique_ptr<openeden::editor::AssetDocument> mActive;
        // Alive while mActive is the same asset: the undo steps of an asset that was replaced do nothing (their
        // commands point into that asset), see UndoCommand.
        std::shared_ptr<void> mAssetToken;
        std::string mActiveFormat;
        std::string mActiveOwner;
        std::string mAssetOwnerFilter;
        unsigned long mGeneration = 0;
        unsigned long mLayoutRevision = 0;
        std::string mOpenError;
        std::vector<std::filesystem::path> mAssetRoots;
        std::filesystem::path mUserDataPath;
        std::set<std::string> mDirtyAssets;
        std::vector<QPointer<QUndoStack>> mStacks;
        std::vector<QPointer<CSMDoc::UndoRouting>> mRoutings; // the documents' module stacks that were used
        QUndoStack mOwnStack; // used when there is no document
        std::optional<HandleRef> mPressed; ///< The handle under the button.
        CSMModule::PickRay mPressRay; ///< Meters.
        std::unique_ptr<CSMModule::DragSession> mDrag;
        float mViewOrigin[3] = { 0.f, 0.f, 0.f };
        std::string mHighlightId;
        openeden::editor::Vec3 mHighlightPosition;
        CSMModule::SelectionModel mSelection; // after mRegistry: it publishes on the registry's bus
        static ModuleUi* sInstance;
    };
}

#endif
