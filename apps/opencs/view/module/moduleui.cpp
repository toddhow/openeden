#include "moduleui.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <limits>

#include <QCheckBox>
#include <QComboBox>
#include <QDoubleSpinBox>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QLineEdit>
#include <QScrollArea>
#include <QSpinBox>
#include <QUndoCommand>
#include <QWidget>

#include <components/debug/debuglog.hpp>

#include <apps/opencs/model/doc/document.hpp>
#include <apps/opencs/model/module/moduleregistry.hpp>
#include <apps/opencs/model/physics/studioworld.hpp>

namespace CSVModule
{
    using namespace openeden::editor;

    ModuleUi* ModuleUi::sInstance = nullptr;

    namespace
    {
        // The owner name of Studio's own bus subscriptions (never a module id: those have no leading colon).
        const std::string sBusOwner = ":studio";
    }

    namespace
    {
        // QUndoCommand ids of module commands start here, so that they never merge with the editor's own.
        constexpr int cMergeIdBase = 0x4D4F4400;
        // A double spin box needs finite limits.
        constexpr double cSpinLimit = 1.0e9;
        constexpr int cSpinDecimals = 4;
        constexpr double cDefaultSpinStep = 0.1;

        /// The form of one PropertySource. Rebuilt when asked; refreshed from get() when the data changed.
        class PropertyForm final : public QWidget
        {
        public:
            PropertyForm(ModuleUi& ui, PropertySource& source, QWidget* parent)
                : QWidget(parent)
                , mUi(ui)
                , mSource(&source)
                , mLayout(new QFormLayout(this))
                , mGeneration(ui.generation())
            {
                setProperty(ModuleUi::sOwnedProperty, true);
                build();
                connect(&ui, &ModuleUi::changed, this, [this] { refresh(); });
            }

            PropertySource* source() const { return mSource; }

        private:
            struct Row
            {
                std::string mId;
                std::function<void(const PropertyValue&)> mShow;
            };

            /// True while the asset this form was built for is still the active one. A source usually belongs to its
            /// asset (a panel or a section of the active asset), and ModuleUi destroys the old asset BEFORE it emits
            /// changed(): the form of a replaced asset is still connected during that emission (its panel only
            /// deletes it later), and reading its source then read freed memory (task 0069 stage f, the second open
            /// of a module asset). Its panel or section builds a new form for the new asset.
            bool current() const { return mUi.generation() == mGeneration; }

            void edit(const std::string& id, const PropertyValue& value)
            {
                if (mUpdating || !current())
                    return;
                try
                {
                    if (std::unique_ptr<Command> command = mSource->makeSetCommand(id, value))
                    {
                        mUi.pushCommand(std::move(command));
                        EditorEvent event;
                        event.type = EditorEventType::ParameterChanged;
                        event.subject = id;
                        if (AssetDocument* active = mUi.activeAsset())
                            event.detail = std::string(active->assetId());
                        mUi.publish(event);
                    }
                    else
                        refresh(); // refused: show the value that stays
                }
                catch (const std::exception& e)
                {
                    Log(Debug::Error) << "A property edit failed: " << e.what();
                }
            }

            void build()
            {
                const std::vector<PropertyDesc> descs = mSource->describe();
                for (const PropertyDesc& desc : descs)
                {
                    const std::string id = desc.id;
                    const QString label = QString::fromStdString(desc.label.empty() ? desc.id : desc.label);
                    QWidget* editor = nullptr;
                    std::function<void(const PropertyValue&)> show;
                    switch (desc.type)
                    {
                        case PropertyType::Bool:
                        {
                            auto* box = new QCheckBox(this);
                            connect(
                                box, &QCheckBox::toggled, this, [this, id](bool on) { edit(id, PropertyValue(on)); });
                            show = [box](const PropertyValue& v) {
                                if (const bool* on = std::get_if<bool>(&v))
                                    box->setChecked(*on);
                            };
                            editor = box;
                            break;
                        }
                        case PropertyType::Int:
                        {
                            auto* spin = new QSpinBox(this);
                            spin->setRange(
                                static_cast<int>(std::max<double>(desc.min, std::numeric_limits<int>::min())),
                                static_cast<int>(std::min<double>(desc.max, std::numeric_limits<int>::max())));
                            if (!desc.unit.empty())
                                spin->setSuffix(" " + QString::fromStdString(desc.unit));
                            connect(spin, &QSpinBox::editingFinished, this, [this, id, spin] {
                                edit(id, PropertyValue(static_cast<std::int64_t>(spin->value())));
                            });
                            show = [spin](const PropertyValue& v) {
                                if (const auto* n = std::get_if<std::int64_t>(&v))
                                    spin->setValue(static_cast<int>(*n));
                            };
                            editor = spin;
                            break;
                        }
                        case PropertyType::Float:
                        {
                            auto* spin = new QDoubleSpinBox(this);
                            spin->setDecimals(cSpinDecimals);
                            spin->setRange(std::max(desc.min, -cSpinLimit), std::min(desc.max, cSpinLimit));
                            spin->setSingleStep(desc.step > 0 ? desc.step : cDefaultSpinStep);
                            if (!desc.unit.empty())
                                spin->setSuffix(" " + QString::fromStdString(desc.unit));
                            // Every change while a value is being dragged or typed: the module merges them.
                            connect(spin, &QDoubleSpinBox::valueChanged, this,
                                [this, id](double value) { edit(id, PropertyValue(value)); });
                            show = [spin](const PropertyValue& v) {
                                if (const double* n = std::get_if<double>(&v))
                                    if (std::abs(spin->value() - *n) > 1e-9)
                                        spin->setValue(*n);
                            };
                            editor = spin;
                            break;
                        }
                        case PropertyType::String:
                        {
                            auto* line = new QLineEdit(this);
                            connect(line, &QLineEdit::editingFinished, this,
                                [this, id, line] { edit(id, PropertyValue(line->text().toStdString())); });
                            show = [line](const PropertyValue& v) {
                                if (const auto* text = std::get_if<std::string>(&v))
                                    if (line->text().toStdString() != *text)
                                        line->setText(QString::fromStdString(*text));
                            };
                            editor = line;
                            break;
                        }
                        case PropertyType::Choice:
                        {
                            auto* combo = new QComboBox(this);
                            for (const std::string& choice : desc.choices)
                                combo->addItem(QString::fromStdString(choice));
                            connect(combo, &QComboBox::activated, this,
                                [this, id, combo] { edit(id, PropertyValue(combo->currentText().toStdString())); });
                            show = [combo](const PropertyValue& v) {
                                if (const auto* text = std::get_if<std::string>(&v))
                                    combo->setCurrentText(QString::fromStdString(*text));
                            };
                            editor = combo;
                            break;
                        }
                        case PropertyType::Vec3:
                        {
                            auto* holder = new QWidget(this);
                            auto* row = new QHBoxLayout(holder);
                            row->setContentsMargins(0, 0, 0, 0);
                            std::array<QDoubleSpinBox*, 3> spins{};
                            for (QDoubleSpinBox*& spin : spins)
                            {
                                spin = new QDoubleSpinBox(holder);
                                spin->setDecimals(cSpinDecimals);
                                spin->setRange(-cSpinLimit, cSpinLimit);
                                spin->setSingleStep(desc.step > 0 ? desc.step : cDefaultSpinStep);
                                row->addWidget(spin);
                                connect(spin, &QDoubleSpinBox::valueChanged, this, [this, id, spins](double) {
                                    edit(id,
                                        PropertyValue(Vec3{ spins[0]->value(), spins[1]->value(), spins[2]->value() }));
                                });
                            }
                            show = [spins](const PropertyValue& v) {
                                if (const Vec3* p = std::get_if<Vec3>(&v))
                                {
                                    spins[0]->setValue(p->x);
                                    spins[1]->setValue(p->y);
                                    spins[2]->setValue(p->z);
                                }
                            };
                            editor = holder;
                            break;
                        }
                    }
                    editor->setEnabled(!desc.readOnly);
                    editor->setToolTip(QString::fromStdString(desc.tooltip));
                    editor->setObjectName(QString::fromStdString("property_" + id));
                    mLayout->addRow(label, editor);
                    mRows.push_back({ id, std::move(show) });
                }
                refresh();
            }

            void refresh()
            {
                if (!current())
                    return;
                mUpdating = true;
                try
                {
                    for (const Row& row : mRows)
                        row.mShow(mSource->get(row.mId));
                }
                catch (const std::exception& e)
                {
                    Log(Debug::Error) << "A property read failed: " << e.what();
                }
                mUpdating = false;
            }

            ModuleUi& mUi;
            PropertySource* mSource;
            QFormLayout* mLayout;
            unsigned long mGeneration; ///< ModuleUi::generation() when the form was built
            std::vector<Row> mRows;
            bool mUpdating = false;
        };
    }

    /// A module's command on the editor's undo stack.
    class ModuleUi::UndoCommand final : public QUndoCommand
    {
    public:
        UndoCommand(std::unique_ptr<Command> command, std::weak_ptr<void> assetToken)
            : QUndoCommand(QString::fromUtf8(command->label().data(), static_cast<int>(command->label().size())))
            , mCommand(std::move(command))
            , mAssetToken(std::move(assetToken))
        {
        }

        // A step of an asset that was closed or replaced is left alone: its command points into that asset.
        void redo() override
        {
            if (!mAssetToken.expired())
                mCommand->redo();
        }
        void undo() override
        {
            if (!mAssetToken.expired())
                mCommand->undo();
        }
        int id() const override
        {
            const int mergeId = mCommand->mergeId();
            return mergeId >= 0 ? cMergeIdBase + mergeId : -1;
        }
        bool mergeWith(const QUndoCommand* other) override
        {
            const auto* next = dynamic_cast<const UndoCommand*>(other);
            return next != nullptr && mCommand->mergeWith(*next->mCommand);
        }

    private:
        std::unique_ptr<Command> mCommand;
        std::weak_ptr<void> mAssetToken;
    };

    ModuleUi::ModuleUi(CSMModule::ModuleRegistry& registry, std::function<CSMDoc::Document*()> documentProvider)
        : mRegistry(registry)
        , mDocumentProvider(std::move(documentProvider))
        , mSelection(registry.bus())
    {
        sInstance = this;
        // Panels, overlay and form follow the selection.
        mRegistry.bus().subscribe(sBusOwner, [this](const EditorEvent& event) {
            if (event.type == EditorEventType::SelectionChanged)
            {
                ++mLayoutRevision;
                emit changed();
            }
        });
        connect(&mOwnStack, &QUndoStack::indexChanged, this, &ModuleUi::changed);
    }

    ModuleUi::~ModuleUi()
    {
        mRegistry.bus().removeOwner(sBusOwner);
        sInstance = nullptr;
    }

    void ModuleUi::pushCommand(std::unique_ptr<Command> command)
    {
        if (!command)
            return;
        // The document's module stack, not its own: a module's step is not an edit of the content, and the content
        // stack's clean flag is the document's modified state (CSMDoc::UndoRouting).
        QUndoStack* stack = &mOwnStack;
        CSMDoc::UndoRouting* routing = nullptr;
        if (mDocumentProvider)
            if (CSMDoc::Document* document = mDocumentProvider())
            {
                routing = &document->getUndoRouting();
                stack = &routing->moduleStack();
            }
        if (std::none_of(
                mStacks.begin(), mStacks.end(), [&](const QPointer<QUndoStack>& known) { return known == stack; }))
        {
            mStacks.emplace_back(stack);
            if (stack != &mOwnStack)
                connect(stack, &QUndoStack::indexChanged, this, &ModuleUi::changed);
        }
        stack->push(new UndoCommand(std::move(command), mAssetToken));
        // Edit > Undo undoes what was just edited (also when the step came from a menu command, not a form).
        if (routing != nullptr)
        {
            if (std::none_of(mRoutings.begin(), mRoutings.end(),
                    [&](const QPointer<CSMDoc::UndoRouting>& known) { return known == routing; }))
                mRoutings.emplace_back(routing);
            routing->activate(CSMDoc::UndoRouting::Target::Module);
        }
        emit changed();
    }

    void ModuleUi::markDirty(std::string_view assetId)
    {
        mDirtyAssets.insert(std::string(assetId));
        emit changed();
    }

    void ModuleUi::log(LogLevel level, std::string_view message)
    {
        const Debug::Level debugLevel = level == LogLevel::Error ? Debug::Error
            : level == LogLevel::Warning                         ? Debug::Warning
            : level == LogLevel::Info                            ? Debug::Info
                                                                 : Debug::Verbose;
        Log(debugLevel) << "[editor module] " << message;
    }

    void ModuleUi::requestRedraw()
    {
        emit changed();
    }

    AssetDocument* ModuleUi::openAsset(const std::filesystem::path& path)
    {
        std::string error;
        std::unique_ptr<AssetDocument> document = mRegistry.openAsset(path, error);
        if (!document)
        {
            mOpenError = error;
            Log(Debug::Error) << "Cannot open '" << path.string() << "': " << error;
            return nullptr;
        }
        mOpenError.clear();
        const auto* type = mRegistry.findAssetType(path);
        mActiveFormat = type != nullptr ? type->mDesc.formatId : std::string();
        mActiveOwner = type != nullptr ? type->mModule : std::string();
        // Old steps stop touching the old asset before it is destroyed.
        mAssetToken.reset();
        mActive = std::move(document);
        mAssetToken = std::make_shared<int>(0);
        ++mGeneration;
        ++mLayoutRevision;
        Log(Debug::Info) << "Opened '" << path.string() << "' (" << mActiveFormat << ")";
        // The selection belongs to the asset it was made in.
        mSelection.clear();
        EditorEvent opened;
        opened.type = EditorEventType::AssetOpened;
        opened.subject = std::string(mActive->assetId());
        mRegistry.bus().publish(opened);
        emit changed();
        return mActive.get();
    }

    void ModuleUi::publish(const EditorEvent& event)
    {
        mRegistry.bus().publish(event);
    }

    void ModuleUi::publishSimulationStep(std::uint64_t step)
    {
        EditorEvent event;
        event.type = EditorEventType::SimulationStepped;
        event.step = step;
        mRegistry.bus().publish(event);
    }

    void ModuleUi::releaseModuleObjects()
    {
        mSelection.clear();
        mAssetToken.reset();
        ++mGeneration;
        ++mLayoutRevision;
        mActive.reset();
        mActiveFormat.clear();
        mActiveOwner.clear();
        for (const QPointer<QUndoStack>& stack : mStacks)
            if (stack)
                stack->clear();
        mStacks.clear();
        for (const QPointer<CSMDoc::UndoRouting>& routing : mRoutings)
            if (routing)
                routing->clearModuleStack(); // also hands Edit > Undo back to the document
        mRoutings.clear();
        mOwnStack.clear();
    }

    std::vector<std::filesystem::path> ModuleUi::findAssets(
        const std::vector<std::filesystem::path>& roots, const std::string& ownerFilter) const
    {
        return mRegistry.findAssets(roots, ownerFilter);
    }

    openeden::editor::PropertySource* ModuleUi::panelSource(std::string_view panelId)
    {
        // A module's panel shows only that module's documents (ModuleRegistry::appliesTo).
        return mRegistry.panelSource(panelId, mActiveOwner);
    }

    void ModuleUi::setAssetOwnerFilter(std::string moduleId)
    {
        if (moduleId == mAssetOwnerFilter)
            return;
        mAssetOwnerFilter = std::move(moduleId);
        emit assetOwnerFilterChanged();
    }

    std::vector<CSMPhysics::DebugLine> ModuleUi::overlayLines(const float origin[3]) const
    {
        return overlay(origin).mLines;
    }

    CSMModule::OverlayData ModuleUi::overlay(const float origin[3]) const
    {
        CSMModule::OverlayBuffer sink(origin, CSMPhysics::StudioWorld::sUnitsPerMeter);
        std::vector<Gizmo*> gizmos;
        if (mActive)
            gizmos = mActive->gizmos();
        for (const auto& provider : mRegistry.gizmoProviders())
            if (CSMModule::ModuleRegistry::appliesTo(provider.mModule, mActiveOwner))
                for (Gizmo* gizmo : mRegistry.gizmosOf(provider.mDesc.id))
                    gizmos.push_back(gizmo);
        // A gizmo may appear twice (the sample's provider returns the active asset's own): draw each once.
        std::sort(gizmos.begin(), gizmos.end());
        gizmos.erase(std::unique(gizmos.begin(), gizmos.end()), gizmos.end());
        for (Gizmo* gizmo : gizmos)
        {
            try
            {
                gizmo->draw(sink);
            }
            catch (const std::exception& e)
            {
                Log(Debug::Error) << "A gizmo failed to draw: " << e.what();
            }
        }
        CSMModule::OverlayData data = sink.take();
        if (data.mRefused > 0 || data.mDropped > 0)
            Log(Debug::Warning) << "An editor module's overlay: " << data.mRefused
                                << " primitives refused (not finite, or out of reach), " << data.mDropped
                                << " dropped (over the caps)";
        return data;
    }

    std::vector<std::pair<std::string, TimelineKey>> ModuleUi::timelineKeys() const
    {
        std::vector<std::pair<std::string, TimelineKey>> keys;
        std::vector<TimelineTrack*> tracks;
        if (mActive)
            tracks = mActive->tracks();
        for (const auto& provider : mRegistry.trackProviders())
            if (CSMModule::ModuleRegistry::appliesTo(provider.mModule, mActiveOwner))
                for (TimelineTrack* track : mRegistry.tracksOf(provider.mDesc.id))
                    tracks.push_back(track);
        std::sort(tracks.begin(), tracks.end());
        tracks.erase(std::unique(tracks.begin(), tracks.end()), tracks.end());
        for (TimelineTrack* track : tracks)
            for (const TimelineKey& key : track->keys())
                keys.emplace_back(std::string(track->title()), key);
        return keys;
    }

    QWidget* ModuleUi::makePropertyForm(PropertySource& source, QWidget* parent)
    {
        return new PropertyForm(*this, source, parent);
    }

    QWidget* ModuleUi::makeScrollingForm(PropertySource& source, QWidget* parent, int maxHeight)
    {
        auto* area = new QScrollArea(parent);
        area->setWidgetResizable(true);
        area->setFrameShape(QFrame::NoFrame);
        if (maxHeight > 0)
            area->setMaximumHeight(maxHeight);
        area->setWidget(new PropertyForm(*this, source, area));
        return area;
    }
}
