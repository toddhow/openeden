#include "overlay.hpp"

#include <atomic>
#include <stdexcept>

#include <vsg/io/Options.h>
#include <vsg/nodes/Node.h>

#include <RmlUi/Core.h>

#include <components/debug/debuglog.hpp>
#include <components/vfs/manager.hpp>
#include <components/vsgutil/compilecontext.hpp>

#include "debugpanel.hpp"
#include "fileinterface.hpp"
#include "fontengine.hpp"
#include "renderinterface.hpp"
#include "systeminterface.hpp"

namespace UiRml
{
    namespace
    {
        constexpr const char* sFontFamily = "Morrowind";
        constexpr const char* sFallbackTrueTypeFont = "fonts/DejaVuLGCSansMono.ttf";
        constexpr const char* sContextName = "openeden-main";

        std::atomic<bool> sExists{ false };
    }

    struct Overlay::Impl
    {
        SystemInterface mSystem;
        VfsFileInterface mFiles;
        RenderInterface mRender;
        std::unique_ptr<BitmapFontEngine> mFonts;
        Rml::Context* mContext = nullptr;
        std::unique_ptr<DebugPanel> mPanel;
        std::atomic<std::uint32_t> mWidth;
        std::atomic<std::uint32_t> mHeight;
        std::uint32_t mContextWidth = 0;
        std::uint32_t mContextHeight = 0;
        bool mInitialised = false;

        Impl(vsg::ref_ptr<vsgUtil::CompileContext> compile, vsg::ref_ptr<const vsg::Options> shaderOptions,
            const VFS::Manager& vfs, const OverlayConfig& config, std::uint32_t width, std::uint32_t height)
            : mFiles(vfs)
            , mRender(std::move(compile), std::move(shaderOptions), mFiles)
            , mWidth(width)
            , mHeight(height)
        {
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
            mContext = Rml::CreateContext(sContextName, Rml::Vector2i(int(width), int(height)));
            if (mContext == nullptr)
                throw std::runtime_error("RmlUi could not create its context");
            if (Rml::ElementDocument* document = mContext->LoadDocumentFromMemory(debugPanelMarkup(sFontFamily)))
            {
                document->Show();
                mPanel = std::make_unique<DebugPanel>(document);
            }
            else
                Log(Debug::Error) << "[UI] the debug panel document could not be loaded";
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
            mRender.setExtent(width, height);
            mRender.beginFrame();
            if (mPanel)
                mPanel->frame(Rml::GetSystemInterface()->GetElapsedTime());
            mContext->Update();
            mContext->Render();
            mRender.draw(commandBuffer);
        }

        ~Impl()
        {
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
}
