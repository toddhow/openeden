#include "overlay.hpp"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <mutex>
#include <stdexcept>
#include <string>
#include <vector>

#include <SDL_clipboard.h>
#include <SDL_keyboard.h>

#include <vsg/io/Options.h>
#include <vsg/nodes/Node.h>

#include <RmlUi/Core.h>

#include <components/debug/debuglog.hpp>
#include <components/vfs/manager.hpp>
#include <components/vsgutil/compilecontext.hpp>

#include "clipboard.hpp"
#include "controllerfocus.hpp"
#include "debugpanel.hpp"
#include "fileinterface.hpp"
#include "fontengine.hpp"
#include "ime.hpp"
#include "rmlprotocol.hpp"
#include "inputmap.hpp"
#include "renderinterface.hpp"
#include "systeminterface.hpp"
#include "themehost.hpp"
#include "themesheet.hpp"
#include "uiscale.hpp"

namespace UiRml
{
    namespace
    {
        constexpr const char* sFontFamily = "Morrowind";
        constexpr const char* sFallbackTrueTypeFont = "fonts/DejaVuLGCSansMono.ttf";
        constexpr const char* sContextName = "openeden-main";

        // The most events held between two frames; further ones are dropped and counted (a frame that long is already
        // a frozen game).
        constexpr std::size_t sMaxQueuedEvents = 512;
        // Pixels one wheel tick scrolls a controller-driven list (RmlUi's own wheel step is a few lines).
        constexpr float sStickScrollPixelsPerTick = 24.f;

        std::atomic<bool> sExists{ false };

        // The system clipboard through SDL; only ClipboardBridge's main-thread calls reach it.
        class SdlClipboard final : public ClipboardBackend
        {
        public:
            std::string get() override
            {
                char* text = SDL_GetClipboardText();
                std::string result = text != nullptr ? text : "";
                if (text != nullptr)
                    SDL_free(text);
                return result;
            }
            void set(const std::string& text) override { SDL_SetClipboardText(text.c_str()); }
        };

        struct QueuedEvent
        {
            enum class Kind : std::uint8_t
            {
                Move,
                Button,
                Wheel,
                Key,
                Text,
                Edit,
                ControllerButton,
                Blur,
                FocusEntry,
            };
            Kind mKind = Kind::Move;
            bool mDown = false;
            int mA = 0;
            int mB = 0;
            int mMods = 0;
            float mX = 0.f;
            float mY = 0.f;
            std::string mText;
        };
    }

    struct Overlay::Impl
    {
        SdlClipboard mClipboardBackend;
        ClipboardBridge mClipboard{ mClipboardBackend };
        SystemInterface mSystem{ &mClipboard };
        VfsFileInterface mFiles;
        // Task 0163: the theme of the documents; serves `ui/base.rcss` through mFiles.
        ThemeHost mTheme;
        RenderInterface mRender;
        TextInputTracker mTracker;
        std::unique_ptr<BitmapFontEngine> mFonts;
        Rml::Context* mContext = nullptr;
        std::unique_ptr<DebugPanel> mPanel;
        // Task 0162: what the Lua UI library wrote, applied on this (the GUI) thread at the start of each frame.
        std::shared_ptr<Ui::CommandQueue> mUiQueue = std::make_shared<Ui::CommandQueue>();
        std::unique_ptr<RmlProtocol> mProtocol;
        std::atomic<std::uint32_t> mWidth;
        std::atomic<std::uint32_t> mHeight;
        std::uint32_t mContextWidth = 0;
        std::uint32_t mContextHeight = 0;
        bool mInitialised = false;

        // The event queue: the main thread appends, the frame thread takes the lot.
        std::mutex mQueueMutex;
        std::vector<QueuedEvent> mQueue;
        std::vector<QueuedEvent> mWork;
        std::size_t mDropped = 0;

        // What the frame thread publishes for the main thread's routing decisions.
        std::atomic<bool> mHasDocuments{ false };
        std::atomic<bool> mOverDocument{ false };
        std::atomic<bool> mTextFocus{ false };
        std::atomic<bool> mNavCaptured{ false };
        std::atomic<bool> mHasNavDocument{ false };
        std::atomic<int> mCaretX{ 0 };
        std::atomic<int> mCaretY{ 0 };
        std::atomic<int> mCaretWidth{ 0 };
        std::atomic<int> mCaretHeight{ 0 };

        // Theme changes asked for by any thread, applied by the frame thread.
        std::mutex mThemeMutex;
        ThemeRequest mThemeWanted;
        bool mThemeDirty = false;
        float mThemeReloadSeconds = 0.f;
        double mLastThemePoll = -1.0;
        // Task 0162: what the overlay's frame costs on the GUI thread, summed; logged at the end of the run.
        struct FrameTimes
        {
            std::uint64_t mFrames = 0;
            double mDrain = 0.0;  // applying the scripts' commands
            double mUpdate = 0.0; // panel, Context::Update, publish
            double mRender = 0.0; // Context::Render and recording the draw list
        } mTimes;

        std::atomic<float> mRequestedScale;
        float mAppliedRatio = 1.f;
        std::atomic<int> mStickValue{ 0 };
        double mLastFrameTime = -1.0;
        float mStickRemainder = 0.f;

        // Main thread only: SDL's text input, started by us.
        bool mStartedTextInput = false;
        bool mTextInputWasActive = false;

        Impl(vsg::ref_ptr<vsgUtil::CompileContext> compile, vsg::ref_ptr<const vsg::Options> shaderOptions,
            const VFS::Manager& vfs, const OverlayConfig& config, std::uint32_t width, std::uint32_t height)
            : mFiles(vfs)
            , mTheme([this](const std::string& path, std::string& out) {
                std::vector<char> bytes;
                if (!mFiles.readAll(path, bytes))
                    return false;
                out.assign(bytes.begin(), bytes.end());
                return true;
            })
            , mRender(std::move(compile), std::move(shaderOptions), mFiles)
            , mWidth(width)
            , mHeight(height)
            , mRequestedScale(config.mGuiScale)
        {
            mThemeReloadSeconds = config.mThemeReloadSeconds;
            mTheme.setRequest({ config.mTheme.empty() ? std::string(kDefaultThemeName) : config.mTheme,
                parseNameList(config.mThemeVariants) });
            mFiles.setGenerator([this](const std::string& path, std::vector<char>& out) { return mTheme.generate(path, out); });
            mFiles.setGuard([](const std::string& path, const std::vector<char>& bytes) { return styleSheetAllowed(path, bytes); });
            mQueue.reserve(sMaxQueuedEvents);
            mWork.reserve(sMaxQueuedEvents);
            mRender.setExtent(width, height);
            Rml::SetSystemInterface(&mSystem);
            Rml::SetFileInterface(&mFiles);
            Rml::SetRenderInterface(&mRender);
            if (!Rml::Initialise())
                throw std::runtime_error("RmlUi could not be initialised");
            mInitialised = true;

            // The default (FreeType) engine RmlUi made stays as the fallback for TrueType; ours draws the bitmap fonts.
            mFonts = std::make_unique<BitmapFontEngine>(
                Rml::GetFontEngineInterface(),
                [this](const std::string& path, std::vector<char>& out) { return mFiles.readAll(path, out); },
                config.mEncoding, &mRender);
            Rml::SetFontEngineInterface(mFonts.get());

            loadFonts(config);

            mContextWidth = width;
            mContextHeight = height;
            mContext = Rml::CreateContext(sContextName, Rml::Vector2i(int(width), int(height)), nullptr, &mTracker);
            if (mContext == nullptr)
                throw std::runtime_error("RmlUi could not create its context");
            mAppliedRatio = dpRatioFor(config.mGuiScale);
            mContext->SetDensityIndependentPixelRatio(mAppliedRatio);
            mTheme.refresh();
            logTheme("loaded");
            {
                RmlProtocol::Options protocolOptions;
                protocolOptions.mFontFamily = sFontFamily;
                std::vector<char> probe;
                protocolOptions.mLoadBaseSheet = mFiles.readAll("ui/base.rcss", probe);
                // Task 0163: scripts' documents are loaded from this VFS file, so a theme change reloads their sheets.
                if (mFiles.readAll("ui/document.rml", probe))
                    protocolOptions.mDocumentFile = "ui/document.rml";
                protocolOptions.mEventSink = [queue = mUiQueue](Ui::Event&& event) { queue->pushEvent(std::move(event)); };
                mProtocol = std::make_unique<RmlProtocol>(*mContext, std::move(protocolOptions));
            }
            if (!config.mDebugPanel)
            {
            }
            else if (Rml::ElementDocument* document = mContext->LoadDocument(kDebugPanelDocument))
            {
                document->Show();
                mPanel = std::make_unique<DebugPanel>(document);
            }
            else
                Log(Debug::Error) << "[UI] the debug panel document could not be loaded";
            mHasDocuments = mContext->GetNumDocuments() > 0;
        }

        // Refuses a style sheet (or the style blocks of a document) that uses what the renderer cannot draw; logs why.
        static bool styleSheetAllowed(const std::string& path, const std::vector<char>& bytes)
        {
            const bool sheet = path.ends_with(".rcss");
            if (!sheet && !path.ends_with(".rml"))
                return true;
            const std::string_view text(bytes.data(), bytes.size());
            const auto problems = sheet ? validateStyleSheet(text, path) : validateMarkupStyles(text, path);
            for (const StyleDiagnostic& problem : problems)
                Log(Debug::Error) << "[UI] style refused: " << problem.text();
            return problems.empty();
        }

        void logTheme(const char* what)
        {
            const ThemeAssembly assembly = mTheme.assembly();
            Log(Debug::Info) << "[UI] theme " << what << ": '" << assembly.mThemeUsed << "'"
                             << (assembly.mFellBack ? " (fell back)" : "") << ", " << assembly.mVariantsUsed.size()
                             << " variant(s), " << assembly.mSources.size() << " sheet(s)";
            for (const StyleDiagnostic& problem : assembly.mProblems)
                Log(Debug::Error) << "[UI] theme problem: " << problem.text();
        }

        // Frame thread: takes a theme request, polls the style sheet files, and when anything changed makes every
        // document read its sheets again.
        void applyThemeChanges(double now)
        {
            bool changed = false;
            bool requested = false;
            {
                std::lock_guard lock(mThemeMutex);
                if (mThemeDirty)
                {
                    mTheme.setRequest(mThemeWanted);
                    mThemeDirty = false;
                    requested = true;
                }
            }
            if (requested)
                changed = mTheme.refresh();
            else if (mThemeReloadSeconds > 0.f && (mLastThemePoll < 0.0 || now - mLastThemePoll >= mThemeReloadSeconds))
            {
                mLastThemePoll = now;
                const bool filesChanged = mFiles.watchedFilesChanged();
                const bool themeChanged = mTheme.refresh();
                changed = filesChanged || themeChanged;
                if (changed)
                    Log(Debug::Info) << "[UI] style sheets changed on disk, reloading";
            }
            if (!changed)
                return;
            logTheme(requested ? "changed" : "reloaded");
            Rml::Factory::ClearStyleSheetCache();
            for (int i = 0; i < mContext->GetNumDocuments(); ++i)
                mContext->GetDocument(i)->ReloadStyleSheet();
        }

        void loadFonts(const OverlayConfig& config)
        {
            bool loaded = false;
            if (!config.mBitmapFont.empty())
                loaded = mFonts->addBitmapFont(sFontFamily, "fonts/" + config.mBitmapFont + ".fnt");
            if (loaded)
            {
                Log(Debug::Info) << "[UI] bitmap font " << config.mBitmapFont << " loaded";
                return;
            }
            // MysticCards/DejaVu are TrueType: the engine's own font as the stand-in, as the MyGUI loader does.
            if (!Rml::LoadFontFace(sFallbackTrueTypeFont, sFontFamily, Rml::Style::FontStyle::Normal,
                    Rml::Style::FontWeight::Normal, false))
                Log(Debug::Error) << "[UI] no font could be loaded: the panel will show no text";
            else
                Log(Debug::Warning) << "[UI] bitmap font '" << config.mBitmapFont << "' not found, using "
                                    << sFallbackTrueTypeFont;
        }

        void enqueue(QueuedEvent&& event)
        {
            std::lock_guard lock(mQueueMutex);
            if (mQueue.size() >= sMaxQueuedEvents)
            {
                ++mDropped;
                return;
            }
            mQueue.push_back(std::move(event));
        }

        Rml::Element* focusedControl() const { return UiRml::focusedControl(*mContext); }

        void blurFocus()
        {
            if (Rml::Element* focus = focusedControl())
                focus->Blur();
        }

        void apply(const QueuedEvent& event)
        {
            switch (event.mKind)
            {
                case QueuedEvent::Kind::Move:
                    mContext->ProcessMouseMove(event.mA, event.mB, event.mMods);
                    break;
                case QueuedEvent::Kind::Button:
                    if (event.mA >= 0)
                    {
                        if (event.mDown)
                            mContext->ProcessMouseButtonDown(event.mA, event.mMods);
                        else
                            mContext->ProcessMouseButtonUp(event.mA, event.mMods);
                    }
                    break;
                case QueuedEvent::Kind::Wheel:
                    // SDL: positive y is away from the user; RmlUi: positive is down.
                    mContext->ProcessMouseWheel(Rml::Vector2f(-event.mX, -event.mY), event.mMods);
                    break;
                case QueuedEvent::Kind::Key:
                {
                    const Rml::Input::KeyIdentifier id = keyFromSdl(event.mA);
                    if (id == Rml::Input::KI_UNKNOWN)
                        break;
                    if (event.mDown)
                    {
                        mContext->ProcessKeyDown(id, event.mMods);
                        if (id == Rml::Input::KI_ESCAPE)
                            blurFocus();
                    }
                    else
                        mContext->ProcessKeyUp(id, event.mMods);
                    break;
                }
                case QueuedEvent::Kind::ControllerButton:
                {
                    const Rml::Input::KeyIdentifier id = keyFromControllerButton(event.mA);
                    if (id == Rml::Input::KI_UNKNOWN)
                        break;
                    if (event.mDown)
                    {
                        mContext->ProcessKeyDown(id, 0);
                        if (id == Rml::Input::KI_ESCAPE)
                            blurFocus();
                    }
                    else
                        mContext->ProcessKeyUp(id, 0);
                    break;
                }
                case QueuedEvent::Kind::Blur:
                    blurFocus();
                    break;
                case QueuedEvent::Kind::FocusEntry:
                    UiRml::toggleControllerFocus(*mContext);
                    break;
                case QueuedEvent::Kind::Text:
                    mContext->ProcessTextInput(Rml::String(event.mText));
                    break;
                case QueuedEvent::Kind::Edit:
                    mTracker.edit(event.mText, event.mA, event.mB);
                    break;
            }
        }

        // Scrolls the nearest scrollable ancestor of the focused control by the right stick's ticks.
        void applyStick(double dt)
        {
            const int value = mStickValue.load();
            if (value == 0 || !(dt > 0.0))
            {
                mStickRemainder = 0.f;
                return;
            }
            mStickRemainder += stickWheelTicks(value, static_cast<float>(dt));
            const float pixels = mStickRemainder * sStickScrollPixelsPerTick;
            if (std::abs(pixels) < 1.f)
                return;
            mStickRemainder = 0.f;
            Rml::Element* focus = focusedControl();
            if (focus == nullptr)
                return;
            // The nearest scrollable ancestor of the focused control, else the first scrollable element of its
            // document (a list beside the controls).
            Rml::Element* scrollable = nullptr;
            for (Rml::Element* element = focus; element != nullptr && scrollable == nullptr;
                 element = element->GetParentNode())
            {
                if (isScrollable(*element))
                    scrollable = element;
            }
            if (scrollable == nullptr)
                scrollable = findScrollable(*focus->GetOwnerDocument());
            if (scrollable != nullptr)
                scrollable->SetScrollTop(scrollable->GetScrollTop() + pixels);
        }

        static bool isScrollable(Rml::Element& element)
        {
            return element.GetScrollHeight() > element.GetClientHeight() + 1.f;
        }

        // Depth first, in document order; the document itself is not offered (it has the whole content).
        static Rml::Element* findScrollable(Rml::Element& parent)
        {
            for (int i = 0; i < parent.GetNumChildren(); ++i)
            {
                Rml::Element* child = parent.GetChild(i);
                if (isScrollable(*child))
                    return child;
                if (Rml::Element* found = findScrollable(*child))
                    return found;
            }
            return nullptr;
        }

        void publish()
        {
            mHasDocuments.store(mContext->GetNumDocuments() > 0, std::memory_order_relaxed);
            mHasNavDocument.store(hasNavCaptureDocument(*mContext), std::memory_order_relaxed);
            mOverDocument.store(mContext->IsMouseInteracting(), std::memory_order_relaxed);

            bool textFocus = false;
            bool navCaptured = false;
            if (Rml::Element* focus = focusedControl())
            {
                textFocus = isTextEntry(focus->GetTagName(), focus->GetAttribute<Rml::String>("type", ""));
                if (Rml::ElementDocument* document = focus->GetOwnerDocument())
                    navCaptured = document->HasAttribute("nav-capture");
            }
            mTextFocus.store(textFocus, std::memory_order_relaxed);
            mNavCaptured.store(navCaptured, std::memory_order_relaxed);

            Rml::Rectanglef box;
            if (textFocus && mTracker.context() != nullptr && mTracker.context()->GetBoundingBox(box))
            {
                mCaretX.store(static_cast<int>(box.Left()), std::memory_order_relaxed);
                mCaretY.store(static_cast<int>(box.Top()), std::memory_order_relaxed);
                mCaretWidth.store(static_cast<int>(box.Width()), std::memory_order_relaxed);
                mCaretHeight.store(static_cast<int>(box.Height()), std::memory_order_relaxed);
            }
        }

        void frame(vsg::CommandBuffer& commandBuffer)
        {
            const std::uint32_t width = mWidth.load();
            const std::uint32_t height = mHeight.load();
            if (width != mContextWidth || height != mContextHeight)
            {
                mContext->SetDimensions(Rml::Vector2i(int(width), int(height)));
                mContextWidth = width;
                mContextHeight = height;
            }
            const float ratio = dpRatioFor(mRequestedScale.load());
            if (ratio != mAppliedRatio)
            {
                mContext->SetDensityIndependentPixelRatio(ratio);
                mAppliedRatio = ratio;
            }
            mRender.setExtent(width, height);
            mRender.beginFrame();

            const double now = Rml::GetSystemInterface()->GetElapsedTime();
            const double dt = mLastFrameTime < 0.0 ? 0.0 : now - mLastFrameTime;
            mLastFrameTime = now;

            {
                std::lock_guard lock(mQueueMutex);
                mWork.swap(mQueue);
                mQueue.clear();
                if (mDropped != 0)
                {
                    Log(Debug::Warning) << "[UI] " << mDropped << " input events were dropped (queue full)";
                    mDropped = 0;
                }
            }
            applyThemeChanges(now);
            for (const QueuedEvent& event : mWork)
                apply(event);
            mWork.clear();
            applyStick(dt);

            using Clock = std::chrono::steady_clock;
            const auto t0 = Clock::now();
            mUiQueue->drain(*mProtocol);
            const auto t1 = Clock::now();
            if (mPanel)
                mPanel->frame(now);
            mContext->Update();
            publish();
            const auto t2 = Clock::now();
            mContext->Render();
            mRender.draw(commandBuffer);
            const auto t3 = Clock::now();
            ++mTimes.mFrames;
            mTimes.mDrain += std::chrono::duration<double, std::micro>(t1 - t0).count();
            mTimes.mUpdate += std::chrono::duration<double, std::micro>(t2 - t1).count();
            mTimes.mRender += std::chrono::duration<double, std::micro>(t3 - t2).count();
        }

        ~Impl()
        {
            if (mTimes.mFrames > 0)
                Log(Debug::Info) << "[UI] overlay frame cost over " << mTimes.mFrames
                                 << " frames (mean microseconds): drain " << mTimes.mDrain / mTimes.mFrames << ", update "
                                 << mTimes.mUpdate / mTimes.mFrames << ", render " << mTimes.mRender / mTimes.mFrames;
            mClipboard.pump();
            if (mStartedTextInput && !mTextInputWasActive)
                SDL_StopTextInput();
            // The panel's listeners come off the document before RmlUi releases it.
            mPanel.reset();
            mProtocol.reset();
            if (mInitialised)
            {
                // Releases the contexts, documents and fonts, through the interfaces above (all still alive).
                Rml::Shutdown();
            }
        }
    };

    Overlay::Overlay(vsg::ref_ptr<vsgUtil::CompileContext> compile, vsg::ref_ptr<const vsg::Options> shaderOptions,
        const VFS::Manager& vfs, const OverlayConfig& config, std::uint32_t width, std::uint32_t height)
    {
        if (sExists.exchange(true))
            throw std::logic_error("only one UiRml::Overlay can exist at a time");
        try
        {
            mImpl = std::make_unique<Impl>(std::move(compile), std::move(shaderOptions), vfs, config, width, height);
        }
        catch (...)
        {
            sExists = false;
            throw;
        }
    }

    Overlay::~Overlay()
    {
        mImpl.reset();
        sExists = false;
    }

    std::shared_ptr<Ui::CommandQueue> Overlay::commandQueue() const
    {
        return mImpl->mUiQueue;
    }

    vsg::ref_ptr<vsg::Node> Overlay::node()
    {
        return mImpl->mRender.node(
            [impl = mImpl.get()](vsg::CommandBuffer& commandBuffer) { impl->frame(commandBuffer); });
    }

    void Overlay::setExtent(std::uint32_t width, std::uint32_t height)
    {
        mImpl->mWidth = width;
        mImpl->mHeight = height;
    }

    void Overlay::setGuiScale(float factor)
    {
        mImpl->mRequestedScale = factor;
    }

    void Overlay::setTheme(const std::string& name, const std::string& variants)
    {
        ThemeRequest request{ name.empty() ? std::string(kDefaultThemeName) : name, parseNameList(variants) };
        std::lock_guard lock(mImpl->mThemeMutex);
        if (!mImpl->mThemeDirty && request == mImpl->mTheme.request())
            return;
        mImpl->mThemeWanted = std::move(request);
        mImpl->mThemeDirty = true;
    }

    UiState Overlay::state() const
    {
        UiState state;
        state.mHasDocuments = mImpl->mHasDocuments.load(std::memory_order_relaxed);
        state.mOverDocument = mImpl->mOverDocument.load(std::memory_order_relaxed);
        state.mTextFocus = mImpl->mTextFocus.load(std::memory_order_relaxed);
        state.mNavCaptured = mImpl->mNavCaptured.load(std::memory_order_relaxed);
        state.mHasNavDocument = mImpl->mHasNavDocument.load(std::memory_order_relaxed);
        return state;
    }

    void Overlay::mouseMoved(int x, int y, int sdlMods)
    {
        QueuedEvent event;
        event.mKind = QueuedEvent::Kind::Move;
        event.mA = x;
        event.mB = y;
        event.mMods = modifiersFromSdl(static_cast<std::uint16_t>(sdlMods));
        mImpl->enqueue(std::move(event));
    }

    void Overlay::mouseButton(int sdlButton, bool down, int sdlMods)
    {
        QueuedEvent event;
        event.mKind = QueuedEvent::Kind::Button;
        event.mA = buttonFromSdl(static_cast<std::uint8_t>(sdlButton));
        event.mDown = down;
        event.mMods = modifiersFromSdl(static_cast<std::uint16_t>(sdlMods));
        mImpl->enqueue(std::move(event));
    }

    void Overlay::mouseWheel(float x, float y, int sdlMods)
    {
        QueuedEvent event;
        event.mKind = QueuedEvent::Kind::Wheel;
        event.mX = x;
        event.mY = y;
        event.mMods = modifiersFromSdl(static_cast<std::uint16_t>(sdlMods));
        mImpl->enqueue(std::move(event));
    }

    void Overlay::key(int sdlKeycode, int sdlMods, bool down)
    {
        // The paste is applied on the GUI thread a frame later, but the system clipboard is read here, on the main
        // thread, before the key is queued (SDL's clipboard belongs to the main thread).
        if (down && isPasteChord(sdlKeycode, static_cast<std::uint16_t>(sdlMods)))
            mImpl->mClipboard.refresh();
        QueuedEvent event;
        event.mKind = QueuedEvent::Kind::Key;
        event.mA = sdlKeycode;
        event.mDown = down;
        event.mMods = modifiersFromSdl(static_cast<std::uint16_t>(sdlMods));
        mImpl->enqueue(std::move(event));
    }

    void Overlay::text(std::string_view utf8)
    {
        QueuedEvent event;
        event.mKind = QueuedEvent::Kind::Text;
        event.mText = std::string(utf8);
        mImpl->enqueue(std::move(event));
    }

    void Overlay::textEditing(std::string_view utf8, int start, int length)
    {
        QueuedEvent event;
        event.mKind = QueuedEvent::Kind::Edit;
        event.mText = std::string(utf8);
        event.mA = start;
        event.mB = length;
        mImpl->enqueue(std::move(event));
    }

    void Overlay::controllerButton(int sdlButton, bool down)
    {
        QueuedEvent event;
        event.mKind = QueuedEvent::Kind::ControllerButton;
        event.mA = sdlButton;
        event.mDown = down;
        mImpl->enqueue(std::move(event));
    }

    void Overlay::controllerAxis(int, int value)
    {
        mImpl->mStickValue = value;
    }

    void Overlay::blur()
    {
        QueuedEvent event;
        event.mKind = QueuedEvent::Kind::Blur;
        mImpl->enqueue(std::move(event));
    }

    void Overlay::focusEntry()
    {
        QueuedEvent event;
        event.mKind = QueuedEvent::Kind::FocusEntry;
        mImpl->enqueue(std::move(event));
    }

    void Overlay::syncTextInput()
    {
        Impl& impl = *mImpl;
        // text the overlay copied goes to the system clipboard here, on the main thread
        impl.mClipboard.pump();
        const bool want = impl.mTextFocus.load(std::memory_order_relaxed);
        if (want)
        {
            if (!impl.mStartedTextInput)
            {
                impl.mTextInputWasActive = SDL_IsTextInputActive() == SDL_TRUE;
                impl.mStartedTextInput = true;
            }
            // MyGUI may have switched text input off meanwhile; keep it on while a field has the focus.
            if (SDL_IsTextInputActive() != SDL_TRUE)
                SDL_StartTextInput();
            SDL_Rect rect{ impl.mCaretX.load(), impl.mCaretY.load(), impl.mCaretWidth.load(), impl.mCaretHeight.load() };
            SDL_SetTextInputRect(&rect);
        }
        else if (impl.mStartedTextInput)
        {
            // Leave it as we found it: if the game had it on (the console), it stays on.
            if (!impl.mTextInputWasActive)
                SDL_StopTextInput();
            impl.mStartedTextInput = false;
        }
    }
}
