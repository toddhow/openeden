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
        // QUndoCommand ids of module commands start here, so that they never merge with the editor's own.
        constexpr int cMergeIdBase = 0x4D4F4400;
        // How far a point's cross reaches each way, in meters (a point has no size in the world).
        constexpr double cPointCrossHalfLengthM = 0.05;
        // A double spin box needs finite limits.
        constexpr double cSpinLimit = 1.0e9;
        constexpr int cSpinDecimals = 4;
        constexpr double cDefaultSpinStep = 0.1;

        /// Draws an overlay into lines, converting meters to the world's unit (the one boundary, see overlayLines).
        class LineSink final : public OverlaySink
        {
        public:
            LineSink(std::vector<CSMPhysics::DebugLine>& lines, const float origin[3])
                : mLines(lines)
                , mOrigin{ origin[0], origin[1], origin[2] }
            {
            }

            void line(const Vec3& a, const Vec3& b, const Colour& c) override
            {
                CSMPhysics::DebugLine line;
                convert(a, line.mFrom);
                convert(b, line.mTo);
                line.mColour[0] = c.r;
                line.mColour[1] = c.g;
                line.mColour[2] = c.b;
                line.mColour[3] = c.a;
                mLines.push_back(line);
            }

            void point(const Vec3& p, const Colour& c, float) override
            {
                const double r = cPointCrossHalfLengthM;
                line({ p.x - r, p.y, p.z }, { p.x + r, p.y, p.z }, c);
                line({ p.x, p.y - r, p.z }, { p.x, p.y + r, p.z }, c);
                line({ p.x, p.y, p.z - r }, { p.x, p.y, p.z + r }, c);
            }

            void label(const Vec3&, std::string_view, const Colour&) override
            {
                // The debug geometry has no text (task 0031: labels are not drawn yet).
            }

        private:
            void convert(const Vec3& meters, float out[3]) const
            {
                constexpr double units = CSMPhysics::StudioWorld::sUnitsPerMeter;
                out[0] = static_cast<float>(meters.x * units + mOrigin[0]);
                out[1] = static_cast<float>(meters.y * units + mOrigin[1]);
                out[2] = static_cast<float>(meters.z * units + mOrigin[2]);
            }

            std::vector<CSMPhysics::DebugLine>& mLines;
            double mOrigin[3];
        };

        /// The form of one PropertySource. Rebuilt when asked; refreshed from get() when the data changed.
        class PropertyForm final : public QWidget
        {
        public:
            PropertyForm(ModuleUi& ui, PropertySource& source, QWidget* parent)
                : QWidget(parent)
                , mUi(ui)
                , mSource(&source)
                , mLayout(new QFormLayout(this))
            {
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

            void edit(const std::string& id, const PropertyValue& value)
            {
                if (mUpdating)
                    return;
                try
                {
                    if (std::unique_ptr<Command> command = mSource->makeSetCommand(id, value))
                        mUi.pushCommand(std::move(command));
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
                            connect(box, &QCheckBox::toggled, this, [this, id](bool on) { edit(id, PropertyValue(on)); });
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
                            spin->setRange(static_cast<int>(std::max<double>(desc.min, std::numeric_limits<int>::min())),
                                static_cast<int>(std::min<double>(desc.max, std::numeric_limits<int>::max())));
                            if (!desc.unit.empty())
                                spin->setSuffix(" " + QString::fromStdString(desc.unit));
                            connect(spin, &QSpinBox::editingFinished, this,
                                [this, id, spin] { edit(id, PropertyValue(static_cast<std::int64_t>(spin->value()))); });
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
                                    edit(id, PropertyValue(Vec3{ spins[0]->value(), spins[1]->value(), spins[2]->value() }));
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
            std::vector<Row> mRows;
            bool mUpdating = false;
        };
    }

    /// A module's command on the editor's undo stack.
    class ModuleUi::UndoCommand final : public QUndoCommand
    {
    public:
        explicit UndoCommand(std::unique_ptr<Command> command)
            : QUndoCommand(QString::fromUtf8(command->label().data(), static_cast<int>(command->label().size())))
            , mCommand(std::move(command))
        {
        }

        void redo() override { mCommand->redo(); }
        void undo() override { mCommand->undo(); }
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
    };

    ModuleUi::ModuleUi(CSMModule::ModuleRegistry& registry, std::function<CSMDoc::Document*()> documentProvider)
        : mRegistry(registry)
        , mDocumentProvider(std::move(documentProvider))
    {
        sInstance = this;
        connect(&mOwnStack, &QUndoStack::indexChanged, this, &ModuleUi::changed);
    }

    ModuleUi::~ModuleUi()
    {
        sInstance = nullptr;
    }

    void ModuleUi::pushCommand(std::unique_ptr<Command> command)
    {
        if (!command)
            return;
        QUndoStack* stack = &mOwnStack;
        if (mDocumentProvider)
            if (CSMDoc::Document* document = mDocumentProvider())
                stack = &document->getUndoStack();
        if (std::none_of(mStacks.begin(), mStacks.end(), [&](const QPointer<QUndoStack>& known) { return known == stack; }))
        {
            mStacks.emplace_back(stack);
            if (stack != &mOwnStack)
                connect(stack, &QUndoStack::indexChanged, this, &ModuleUi::changed);
        }
        stack->push(new UndoCommand(std::move(command)));
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
        mActive = std::move(document);
        Log(Debug::Info) << "Opened '" << path.string() << "' (" << mActiveFormat << ")";
        emit changed();
        return mActive.get();
    }

    void ModuleUi::releaseModuleObjects()
    {
        mActive.reset();
        mActiveFormat.clear();
        for (const QPointer<QUndoStack>& stack : mStacks)
            if (stack)
                stack->clear();
        mStacks.clear();
        mOwnStack.clear();
    }

    std::vector<std::filesystem::path> ModuleUi::findAssets(const std::vector<std::filesystem::path>& roots) const
    {
        std::vector<std::filesystem::path> found;
        for (const std::filesystem::path& root : roots)
        {
            std::error_code error;
            if (!std::filesystem::is_directory(root, error))
                continue;
            std::filesystem::recursive_directory_iterator it(
                root, std::filesystem::directory_options::skip_permission_denied, error);
            for (const std::filesystem::recursive_directory_iterator end; !error && it != end; it.increment(error))
                if (it->is_regular_file(error) && mRegistry.findAssetType(it->path()) != nullptr)
                    found.push_back(it->path());
        }
        std::sort(found.begin(), found.end());
        found.erase(std::unique(found.begin(), found.end()), found.end());
        return found;
    }

    std::vector<CSMPhysics::DebugLine> ModuleUi::overlayLines(const float origin[3]) const
    {
        std::vector<CSMPhysics::DebugLine> lines;
        LineSink sink(lines, origin);
        std::vector<Gizmo*> gizmos;
        if (mActive)
            gizmos = mActive->gizmos();
        for (const auto& provider : mRegistry.gizmoProviders())
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
        return lines;
    }

    std::vector<std::pair<std::string, TimelineKey>> ModuleUi::timelineKeys() const
    {
        std::vector<std::pair<std::string, TimelineKey>> keys;
        std::vector<TimelineTrack*> tracks;
        if (mActive)
            tracks = mActive->tracks();
        for (const auto& provider : mRegistry.trackProviders())
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
