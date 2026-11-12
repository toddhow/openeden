#include "face_editor_module.hpp"

#include "face_document.hpp"
#include "models_folder.hpp"

#include <openeden/editor_module/registrar.hpp>

#include <algorithm>
#include <atomic>
#include <filesystem>
#include <map>
#include <memory>
#include <mutex>
#include <system_error>

namespace face_editor
{
    using namespace openeden::editor;
    namespace fs = std::filesystem;

    namespace
    {
        constexpr const char* kAudioRootSetting = "face.audioRoot";
        constexpr int kMinFrameRate = 10;
        constexpr int kMaxFrameRate = 120;
        constexpr int kDefaultFrameRate = 30;

        FaceDocument* faceOf(AssetDocument* document)
        {
            return dynamic_cast<FaceDocument*>(document);
        }

        PropertyDesc textField(const char* id, const char* label, const char* tooltip)
        {
            PropertyDesc p;
            p.id = id;
            p.label = label;
            p.type = PropertyType::String;
            p.tooltip = tooltip;
            return p;
        }

        /// The panel that lists the model variants Studio found and what is wrong with them.
        class ModelsSource final : public PropertySource
        {
        public:
            explicit ModelsSource(std::function<fs::path()> root)
                : mRoot(std::move(root))
            {
            }

            std::vector<PropertyDesc> describe() const override
            {
                refresh();
                std::vector<PropertyDesc> rows;
                PropertyDesc folder;
                folder.id = "models.folder";
                folder.label = "Models folder";
                folder.type = PropertyType::String;
                folder.readOnly = true;
                folder.group = "Models";
                rows.push_back(folder);
                for (const auto& status : mStatuses)
                {
                    PropertyDesc row;
                    row.id = "models." + status.mId;
                    row.label = status.mLabel;
                    row.type = PropertyType::String;
                    row.readOnly = true;
                    row.group = "Models";
                    row.tooltip = status.mDetail;
                    rows.push_back(row);
                }
                return rows;
            }

            PropertyValue get(std::string_view id) const override
            {
                refresh();
                if (id == "models.folder")
                    return mRoot().generic_string();
                for (const auto& status : mStatuses)
                    if ("models." + status.mId == id)
                        return stateText(status.mState) + (status.mDetail.empty() ? "" : " - " + status.mDetail);
                return std::string();
            }

            std::unique_ptr<Command> makeSetCommand(std::string_view, const PropertyValue&) override { return nullptr; }

            /// Records a finished hash check so the rows show it until the next scan.
            void noteVerified(const VariantStatus& status)
            {
                std::lock_guard lock(mMutex);
                mVerified[status.mId] = status;
            }

            std::vector<VariantStatus> statuses() const
            {
                refresh();
                return mStatuses;
            }

        private:
            void refresh() const
            {
                std::lock_guard lock(mMutex);
                mStatuses = scanModels(mRoot());
                for (auto& status : mStatuses)
                {
                    auto it = mVerified.find(status.mId);
                    if (it != mVerified.end() && status.mState == VariantState::Present)
                        status = it->second;
                }
            }

            std::function<fs::path()> mRoot;
            mutable std::mutex mMutex;
            mutable std::vector<VariantStatus> mStatuses;
            std::map<std::string, VariantStatus> mVerified;
        };

        class FaceEditorModule final : public EditorModule
        {
        public:
            void registerUi(EditorRegistrar& registrar) override
            {
                mContext = &registrar.context();
                mModels = std::make_unique<ModelsSource>([this] { return modelsRoot(mContext->userDataPath()); });

                registrar.registerWorkspace({ "face", "Face" });

                // The README is written on first use (once Studio has a user data folder to put it in).
                if (!mContext->userDataPath().empty())
                {
                    std::string error;
                    if (!ensureModelsFolder(modelsRoot(mContext->userDataPath()), error))
                        mContext->log(
                            LogLevel::Warning, "The Audio2Face models folder could not be prepared: " + error);
                }

                SettingDesc audioRootSetting;
                audioRootSetting.property = textField(kAudioRootSetting, "Voice audio folder",
                    "Where the voice lines named in a curve file are looked for (for example an extracted Sound/Vo "
                    "folder), when they are not next to the curve file.");
                audioRootSetting.defaultValue = std::string();
                registrar.registerSetting(audioRootSetting);

                PanelDesc curves;
                curves.id = "face.panel";
                curves.title = "Face Curves";
                curves.workspaceId = "face";
                curves.dock = DockArea::Right;
                curves.source = [this]() -> PropertySource* {
                    FaceDocument* doc = faceOf(mContext->activeAsset());
                    return doc != nullptr ? &doc->properties() : nullptr;
                };
                registrar.registerPanel(curves);

                PanelDesc models;
                models.id = "face.models";
                models.title = "Face Models";
                models.workspaceId = "face";
                models.dock = DockArea::Right;
                models.source = [this]() -> PropertySource* { return mModels.get(); };
                registrar.registerPanel(models);

                AssetTypeDesc type;
                type.formatId = kFormatId;
                type.fileExtension = kFileExtension;
                type.label = "Face curves (opas.facecurves)";
                type.open = [this](const fs::path& path, std::string& error) -> std::unique_ptr<AssetDocument> {
                    return FaceDocument::open(path, audioRoot(), error);
                };
                registrar.registerAssetType(type);

                addCommands(registrar);
            }

            void shutdown() override
            {
                mModels.reset();
                mContext = nullptr;
            }

        private:
            fs::path audioRoot() const
            {
                if (mContext == nullptr)
                    return {};
                auto value = mContext->setting(kAudioRootSetting);
                if (value && std::holds_alternative<std::string>(*value))
                    return fs::path(std::get<std::string>(*value));
                return {};
            }

            static PropertyDesc intField(const char* id, const char* label, int lo, int hi)
            {
                PropertyDesc p;
                p.id = id;
                p.label = label;
                p.type = PropertyType::Int;
                p.min = lo;
                p.max = hi;
                return p;
            }

            void addCommands(EditorRegistrar& registrar)
            {
                CommandDesc newFile;
                newFile.id = "face.newCurves";
                newFile.label = "New Curves File...";
                newFile.menuPath = "Face/New Curves File...";
                newFile.handler = [this](EditorContext& context) { newCurves(context); };
                registrar.registerCommand(newFile);

                CommandDesc setAudio;
                setAudio.id = "face.setAudio";
                setAudio.label = "Set Audio File...";
                setAudio.menuPath = "Face/Set Audio File...";
                setAudio.handler = [this](EditorContext& context) { setAudioFile(context); };
                setAudio.enabledWhen = [this] { return mContext != nullptr && faceOf(mContext->activeAsset()); };
                registrar.registerCommand(setAudio);

                CommandDesc generate;
                generate.id = "face.generate";
                generate.label = "Generate Curves...";
                generate.menuPath = "Face/Generate Curves...";
                generate.handler = [this](EditorContext& context) { generateCurves(context); };
                generate.enabledWhen = [this] { return mContext != nullptr && faceOf(mContext->activeAsset()); };
                registrar.registerCommand(generate);

                CommandDesc verify;
                verify.id = "face.verifyModels";
                verify.label = "Verify Models";
                verify.menuPath = "Face/Verify Models";
                verify.handler = [this](EditorContext& context) { verifyModels(context); };
                registrar.registerCommand(verify);
            }

            void newCurves(EditorContext& context)
            {
                DialogDesc dialog;
                dialog.title = "New Curves File";
                dialog.message = "Makes an .facecurves.json file with every channel at rest for a voice line.";
                dialog.fields = { textField("audio", "Voice audio file", "A .wav, .mp3 or .fuz file."),
                    intField("fps", "Frames per second", kMinFrameRate, kMaxFrameRate) };
                dialog.initial = { std::string(), std::int64_t{ kDefaultFrameRate } };
                dialog.okLabel = "Create";
                auto answer = context.showDialog(dialog);
                if (!answer)
                    return;
                const fs::path audio = std::get<std::string>((*answer)[0]);
                const auto fps = static_cast<std::uint32_t>(std::get<std::int64_t>((*answer)[1]));
                fs::path out = audio;
                out.replace_extension();
                out += kFileExtension;
                std::string error;
                if (!createCurvesFile(audio, out, fps, error))
                {
                    context.log(LogLevel::Error, "New curves file: " + error);
                    return;
                }
                context.log(LogLevel::Info, "Wrote " + out.generic_string());
                if (context.openAsset(out) != nullptr)
                    context.requestRedraw();
            }

            void setAudioFile(EditorContext& context)
            {
                FaceDocument* doc = faceOf(context.activeAsset());
                if (doc == nullptr)
                    return;
                DialogDesc dialog;
                dialog.title = "Set Audio File";
                dialog.fields = { textField("audio", "Voice audio file", "A .wav, .mp3 or .fuz file.") };
                dialog.initial = { doc->audioPath().generic_string() };
                auto answer = context.showDialog(dialog);
                if (!answer)
                    return;
                std::string error;
                if (!doc->setAudio(fs::path(std::get<std::string>((*answer)[0])), error))
                {
                    context.log(LogLevel::Error, "Set audio: " + error);
                    return;
                }
                context.markDirty(doc->assetId());
                context.requestRedraw();
            }

            void generateCurves(EditorContext& context)
            {
                std::vector<std::string> usable;
                for (const auto& status : mModels->statuses())
                    if (status.mId != "audio2emotion"
                        && (status.mState == VariantState::Present || status.mState == VariantState::Verified))
                        usable.push_back(status.mId);
                DialogDesc dialog;
                dialog.title = "Generate Curves";
                dialog.message = usable.empty()
                    ? "No Audio2Face model was found in the models folder (see its README.txt)."
                    : "Settings for the generator.";
                PropertyDesc variant;
                variant.id = "variant";
                variant.label = "Model";
                variant.type = PropertyType::Choice;
                variant.choices = usable.empty() ? std::vector<std::string>{ "none found" } : usable;
                PropertyDesc emotion;
                emotion.id = "emotion";
                emotion.label = "Use the emotion model";
                emotion.type = PropertyType::Bool;
                dialog.fields = { variant, emotion };
                dialog.okLabel = "Generate";
                auto answer = context.showDialog(dialog);
                if (!answer)
                    return;
                context.log(LogLevel::Warning, kGenerateNeedsRuntime);
            }

            void verifyModels(EditorContext& context)
            {
                const fs::path root = modelsRoot(context.userDataPath());
                std::vector<VariantSpec> specs;
                for (const auto& status : mModels->statuses())
                    if (status.mState == VariantState::Present || status.mState == VariantState::Verified)
                        for (const auto& spec : knownVariants())
                            if (spec.mId == status.mId)
                                specs.push_back(spec);
                if (specs.empty())
                {
                    context.log(
                        LogLevel::Info, "Verify Models: no model variant is complete in " + root.generic_string());
                    return;
                }
                ModelsSource* models = mModels.get();
                auto results = std::make_shared<std::vector<VariantStatus>>();
                TaskDesc task;
                task.label = "Verify Audio2Face models";
                task.work = [root, specs, results](TaskControl& control) {
                    for (std::size_t i = 0; i < specs.size(); ++i)
                    {
                        const double base = static_cast<double>(i) / specs.size();
                        const double span = 1.0 / specs.size();
                        results->push_back(verifyVariant(root, specs[i], [&](double f) {
                            control.reportProgress(base + span * f, specs[i].mId);
                            return !control.cancelled();
                        }));
                        if (control.cancelled())
                            return;
                    }
                };
                task.done = [models, results, &context, this](TaskOutcome outcome, const std::string& error) {
                    if (mContext == nullptr)
                        return;
                    if (outcome != TaskOutcome::Finished)
                    {
                        context.log(LogLevel::Warning, "Verify Models did not finish: " + error);
                        return;
                    }
                    for (const auto& status : *results)
                    {
                        models->noteVerified(status);
                        context.log(status.mState == VariantState::Verified ? LogLevel::Info : LogLevel::Error,
                            "Model " + status.mId + ": " + stateText(status.mState) + " " + status.mDetail);
                    }
                    context.requestRedraw();
                };
                context.startTask(std::move(task));
            }

            EditorContext* mContext = nullptr;
            std::unique_ptr<ModelsSource> mModels;
        };

        EditorModule* createModule()
        {
            return new FaceEditorModule();
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
