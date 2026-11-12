#include "face_editor_module.hpp"

#include "directml_install.hpp"
#include "face_document.hpp"
#include "generate_run.hpp"
#include "models_folder.hpp"

#include <openeden/editor_module/registrar.hpp>

#include <algorithm>
#include <atomic>
#include <cstdio>
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

        const DirectMlPackage* sPackageOverride = nullptr;

        const DirectMlPackage& activeDirectMlPackage()
        {
            return sPackageOverride != nullptr ? *sPackageOverride : pinnedDirectMlPackage();
        }

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

                CommandDesc installDml;
                installDml.id = "face.installDirectMl";
                installDml.label = "Install DirectML...";
                installDml.menuPath = "Face/Install DirectML...";
                installDml.handler = [this](EditorContext& context) { installDirectMlRuntime(context); };
                registrar.registerCommand(installDml);

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
                FaceDocument* doc = faceOf(context.activeAsset());
                if (doc == nullptr)
                    return;
                std::string why;
                if (!generationAvailable(why))
                {
                    context.log(LogLevel::Error, why);
                    return;
                }
                // v2.3 first: a quarter of v3.0's memory and the default of the task (toddhow, 2026-11-12).
                std::vector<std::string> usable;
                for (const char* id : { "v2.3-mark", "v3.0" })
                    for (const auto& status : mModels->statuses())
                        if (status.mId == id
                            && (status.mState == VariantState::Present || status.mState == VariantState::Verified))
                            usable.push_back(status.mId);
                DialogDesc dialog;
                dialog.title = "Generate Curves";
                dialog.message = usable.empty()
                    ? "No Audio2Face model was found in the models folder (see its README.txt); generating needs a "
                      "model and the ONNX runtime."
                    : "Writes the curves of the channels this file has (replacing them) from the voice audio. One undo "
                      "step.";
                PropertyDesc variant;
                variant.id = "variant";
                variant.label = "Model";
                variant.type = PropertyType::Choice;
                variant.choices = usable.empty() ? std::vector<std::string>{ "none found" } : usable;
                PropertyDesc emotion;
                emotion.id = "emotion";
                emotion.label = "Use the emotion model";
                emotion.tooltip = "Audio2Emotion is not wired yet: the neutral emotion is used.";
                emotion.type = PropertyType::Bool;
                const bool dmlInstalled = directMlInstalled(context.userDataPath(), activeDirectMlPackage());
                PropertyDesc directMl;
                directMl.id = "directml";
                directMl.label = "Use DirectML (the GPU)";
                directMl.tooltip = dmlInstalled
                    ? "Off runs on the CPU. DirectML falls back to the CPU on its own if it cannot be used."
                    : "DirectML is not installed: the CPU is used. Face > Install DirectML... downloads it (optional).";
                directMl.type = PropertyType::Bool;
                dialog.fields = { variant, emotion, directMl };
                dialog.initial = { usable.empty() ? std::string("none found") : usable.front(), false, dmlInstalled };
                dialog.okLabel = "Generate";
                auto answer = context.showDialog(dialog);
                if (!answer)
                    return;
                if (usable.empty())
                {
                    context.log(LogLevel::Warning,
                        "Generate Curves: no Audio2Face model was found in " + modelsRoot(context.userDataPath()).generic_string()
                            + "; generating needs a model (see the README.txt there) and the ONNX runtime.");
                    return;
                }
                auto text = [&](std::size_t i, const std::string& fallback) {
                    return i < answer->size() && std::holds_alternative<std::string>((*answer)[i])
                        ? std::get<std::string>((*answer)[i])
                        : fallback;
                };
                auto flag = [&](std::size_t i, bool fallback) {
                    return i < answer->size() && std::holds_alternative<bool>((*answer)[i])
                        ? std::get<bool>((*answer)[i])
                        : fallback;
                };
                const std::string variantId = text(0, usable.front());
                if (std::find(usable.begin(), usable.end(), variantId) == usable.end())
                    return;

                const WaveformTrack* wave = doc->waveformTrack();
                if (wave == nullptr || wave->waveform().sampleCount == 0)
                {
                    context.log(LogLevel::Error, "Generate Curves: this file has no voice audio loaded.");
                    return;
                }
                const WaveformData wd = wave->waveform();
                auto request = std::make_shared<GenerateRequest>();
                request->mModelsRoot = modelsRoot(context.userDataPath());
                request->mVariantId = variantId;
                request->mUseEmotionModel = flag(1, false);
                request->mAllowDirectMl = flag(2, dmlInstalled);
                if (dmlInstalled)
                    request->mDirectMlDll = directMlDllPath(context.userDataPath(), activeDirectMlPackage());
                request->mAudio.assign(wd.samples, wd.samples + wd.sampleCount);
                request->mAudioRate = wd.sampleRate;
                for (const auto& status : mModels->statuses())
                    if (status.mId == variantId && status.mState == VariantState::Verified)
                        request->mAlreadyVerified = true;
                auto result = std::make_shared<GenerateResult>();
                auto failure = std::make_shared<std::string>();

                ModelsSource* models = mModels.get();
                TaskDesc task;
                task.label = "Generate face curves (" + variantId + ")";
                task.assetId = std::string(doc->assetId());
                task.work = [request, result, failure](TaskControl& control) {
                    std::string error;
                    const bool ok = runGeneration(*request, [&](double f, std::string_view message) {
                        control.reportProgress(f, message);
                        return !control.cancelled();
                    }, *result, error);
                    if (!ok)
                        *failure = error.empty() ? "unknown error" : error;
                };
                task.done = [this, &context, models, request, result, failure](TaskOutcome outcome, const std::string& error) {
                    if (mContext == nullptr)
                        return;
                    if (outcome == TaskOutcome::Cancelled || *failure == "cancelled")
                    {
                        context.log(LogLevel::Info, "Generate Curves: cancelled.");
                        return;
                    }
                    if (outcome == TaskOutcome::Failed || !failure->empty())
                    {
                        context.log(LogLevel::Error,
                            "Generate Curves failed: " + (outcome == TaskOutcome::Failed ? error : *failure));
                        return;
                    }
                    if (result->mVerified.mState == VariantState::Verified)
                        models->noteVerified(result->mVerified);
                    FaceDocument* target = faceOf(context.activeAsset());
                    if (target == nullptr)
                        return;
                    GeneratedMeta meta;
                    meta.mName = "audio2face-onnx";
                    meta.mVersion = result->mVariantId + " ort " + result->mRuntimeVersion + " " + result->mProvider
                        + (result->mRestSubtracted ? " rest-offset-1" : "");
                    meta.mEmotion = "neutral";
                    meta.mSeed = result->mVariantId == "v3.0" ? request->mOptions.mNoiseSeed : 0;
                    std::vector<std::string> written, added, skipped;
                    auto command = target->makeGeneratedCommand(result->mWeights, meta, &written, &added, &skipped);
                    if (!command)
                    {
                        context.log(LogLevel::Warning, "Generate Curves: the generator made no curves.");
                        return;
                    }
                    context.pushCommand(std::move(command));
                    context.markDirty(target->assetId());
                    for (const std::string& warning : result->mWarnings)
                        context.log(LogLevel::Warning, warning);
                    if (!result->mFallbackReason.empty())
                        context.log(LogLevel::Warning, result->mFallbackReason + "; the CPU was used.");
                    std::string skippedText;
                    for (const std::string& name : skipped)
                        skippedText += (skippedText.empty() ? "" : ", ") + name;
                    std::string addedText;
                    for (const std::string& name : added)
                        addedText += (addedText.empty() ? "" : ", ") + name;
                    char timing[240];
                    std::snprintf(timing, sizeof(timing), "%.2f s of compute for %.2f s of audio (%.3f s per second; setup %.2f, network %.2f, solve %.2f), %.2f s in all",
                        result->mComputeSeconds, result->mAudioSeconds,
                        result->mAudioSeconds > 0 ? result->mComputeSeconds / result->mAudioSeconds : 0.0,
                        result->mWeights.mSetupSeconds, result->mWeights.mNetworkSeconds, result->mWeights.mSolveSeconds,
                        result->mTotalSeconds);
                    context.log(LogLevel::Info,
                        "Generated " + std::to_string(written.size()) + " channels and added " + std::to_string(added.size())
                            + " new ones with " + result->mVariantId + " on " + result->mProvider + " (ONNX Runtime "
                            + result->mRuntimeVersion + (result->mRestSubtracted ? ", rest offset subtracted" : "")
                            + "): " + timing + (addedText.empty() ? "" : ". Added: " + addedText)
                            + (skippedText.empty() ? "" : ". Poses that could not be added: " + skippedText));
                    context.requestRedraw();
                };
                if (context.startTask(std::move(task)) == 0)
                    context.log(LogLevel::Error, "Generate Curves: Studio could not start the task.");
            }

            void installDirectMlRuntime(EditorContext& context)
            {
                const fs::path user = context.userDataPath();
                const DirectMlPackage package = activeDirectMlPackage();
                if (user.empty())
                {
                    context.log(LogLevel::Error, "Install DirectML: Studio has no user data folder to put it in.");
                    return;
                }
                const fs::path dll = directMlDllPath(user, package);
                if (directMlInstalled(user, package))
                {
                    context.log(LogLevel::Info, "DirectML " + package.mVersion + " is already installed: " + dll.generic_string());
                    return;
                }
                DialogDesc dialog;
                dialog.title = "Install DirectML";
                dialog.message = directMlConfirmationText(user, package);
                PropertyDesc accept;
                accept.id = "accept";
                accept.label = "I have read the licence above and accept it";
                accept.type = PropertyType::Bool;
                dialog.fields = { accept };
                dialog.initial = { false };
                dialog.okLabel = "Download";
                auto answer = context.showDialog(dialog);
                if (!answer)
                    return;
                if (answer->empty() || !std::holds_alternative<bool>((*answer)[0]) || !std::get<bool>((*answer)[0]))
                {
                    context.log(LogLevel::Info, "Install DirectML: not downloaded (the licence was not accepted).");
                    return;
                }
                auto failure = std::make_shared<std::string>();
                TaskDesc task;
                task.label = "Download DirectML " + package.mVersion;
                task.work = [package, dll, failure](TaskControl& control) {
                    std::string error;
                    const bool ok = installDirectMl(package, dll.parent_path(),
                        [&](double f, std::string_view message) {
                            control.reportProgress(f, message);
                            return !control.cancelled();
                        },
                        error);
                    if (!ok)
                        *failure = error.empty() ? "unknown error" : error;
                };
                task.done = [this, &context, failure, package, dll](TaskOutcome outcome, const std::string& error) {
                    if (mContext == nullptr)
                        return;
                    if (outcome == TaskOutcome::Cancelled || *failure == "cancelled")
                        context.log(LogLevel::Info, "Install DirectML: cancelled; nothing was kept.");
                    else if (outcome == TaskOutcome::Failed || !failure->empty())
                        context.log(LogLevel::Error,
                            "Install DirectML failed: " + (outcome == TaskOutcome::Failed ? error : *failure));
                    else
                        context.log(LogLevel::Info,
                            "DirectML " + package.mVersion + " installed: " + dll.generic_string()
                                + ". Generate Curves uses it when \"Use DirectML\" is ticked.");
                    context.requestRedraw();
                };
                if (context.startTask(std::move(task)) == 0)
                    context.log(LogLevel::Error, "Install DirectML: Studio could not start the task.");
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

    void setDirectMlPackageForTests(const DirectMlPackage* package)
    {
        sPackageOverride = package;
    }
}
