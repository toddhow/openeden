#include "luauiharness.hpp"

#include <algorithm>
#include <cstdio>
#include <fstream>
#include <sstream>
#include <stdexcept>

#include <MyGUI_DataFileStream.h>
#include <MyGUI_FactoryManager.h>
#include <MyGUI_LayerManager.h>
#include <MyGUI_LogManager.h>
#include <MyGUI_ResourceManager.h>
#include <MyGUI_TextBox.h>

#include <components/lua/utilpackage.hpp>
#include <components/lua_ui/alignment.hpp>
#include <components/lua_ui/content.hpp>
#include <components/lua_ui/util.hpp>
#include <components/myguiplatform/scalinglayer.hpp>

namespace LuaUiTest
{
    namespace
    {
        const std::filesystem::path& sourceRoot()
        {
            static const std::filesystem::path root{ OPENMW_PROJECT_SOURCE_DIR };
            return root;
        }

        std::string readFile(const std::filesystem::path& path)
        {
            std::ifstream stream(path, std::ios::binary);
            if (!stream)
                throw std::runtime_error("Cannot open " + path.string());
            std::ostringstream out;
            out << stream.rdbuf();
            return out.str();
        }
    }

    void StubTexture::createManual(int width, int height, MyGUI::TextureUsage usage, MyGUI::PixelFormat format)
    {
        mWidth = width;
        mHeight = height;
        mUsage = usage;
        mFormat = format;
        mPixels.assign(static_cast<size_t>(width) * static_cast<size_t>(height) * mFormat.getBytesPerPixel(), 0);
    }

    void* StubTexture::lock(MyGUI::TextureUsage)
    {
        if (mPixels.empty())
            mPixels.assign(static_cast<size_t>(mWidth) * static_cast<size_t>(mHeight) * mFormat.getBytesPerPixel(), 0);
        mLocked = true;
        return mPixels.data();
    }

    StubRenderManager::~StubRenderManager() = default;

    void StubRenderManager::setViewSize(int width, int height)
    {
        mViewSize.set(width, height);
        onResizeView(mViewSize);
    }

    MyGUI::ITexture* StubRenderManager::createTexture(const std::string& name)
    {
        auto texture = std::make_unique<StubTexture>(name);
        MyGUI::ITexture* result = texture.get();
        mTextures[name] = std::move(texture);
        return result;
    }

    void StubRenderManager::destroyTexture(MyGUI::ITexture* texture)
    {
        if (texture != nullptr)
            mTextures.erase(texture->getName());
    }

    MyGUI::ITexture* StubRenderManager::getTexture(const std::string& name)
    {
        auto found = mTextures.find(name);
        if (found != mTextures.end())
            return found->second.get();
        // A texture file the engine would load from the VFS: report a size for it.
        auto size = mSizes.find(name);
        const int width = size != mSizes.end() ? size->second.first : sDefaultTextureSize;
        const int height = size != mSizes.end() ? size->second.second : sDefaultTextureSize;
        auto texture = std::make_unique<StubTexture>(name, width, height);
        MyGUI::ITexture* result = texture.get();
        mTextures[name] = std::move(texture);
        return result;
    }

    std::filesystem::path FolderDataManager::find(const std::string& name) const
    {
        for (const auto& folder : mFolders)
        {
            std::filesystem::path candidate = folder / name;
            if (std::filesystem::is_regular_file(candidate))
                return candidate;
        }
        return {};
    }

    MyGUI::IDataStream* FolderDataManager::getData(const std::string& name) const
    {
        const std::filesystem::path path = find(name);
        if (path.empty())
            return nullptr;
        auto stream = std::make_unique<std::ifstream>(path, std::ios::binary);
        if (!stream->is_open())
            return nullptr;
        return new MyGUI::DataFileStream(std::move(stream));
    }

    void FolderDataManager::freeData(MyGUI::IDataStream* data)
    {
        delete data;
    }

    Harness::Harness(int viewWidth, int viewHeight)
        : mLua(nullptr, nullptr)
    {
        try
        {
            mLog = std::make_unique<MyGUI::LogManager>();
            mRender = std::make_unique<StubRenderManager>();
            mData = std::make_unique<FolderDataManager>(std::vector<std::filesystem::path>{
                sourceRoot() / "apps" / "components_tests" / "lua_ui" / "data", sourceRoot() / "files" / "data" / "mygui",
                sourceRoot() / "files" / "data" / "fonts" });

            mGui = std::make_unique<MyGUI::Gui>();
            mGui->initialise({});
            mRender->setViewSize(viewWidth, viewHeight);

            MyGUI::FactoryManager::getInstance().registerFactory<MyGUIPlatform::ScalingLayer>("Layer");
            LuaUi::registerAllWidgets();
            auto& resources = MyGUI::ResourceManager::getInstance();
            resources.load("openmw_layers.xml");
            resources.load("openmw_lua.xml");
            resources.load("testfont.xml");

            mLua.addInternalLibSearchPath(sourceRoot() / "components" / "lua_ui");
            mLua.addInternalLibSearchPath(sourceRoot() / "components" / "lua");
            sol::state_view lua = mLua.unsafeState();

            mUtil = LuaUtil::initUtilPackage(lua);
            mUi = sol::table(lua, sol::create);
            mUi["content"] = LuaUi::loadContentConstructor(&mLua);
            sol::table types(lua, sol::create);
            for (const auto& [typeName, widgetName] : LuaUi::widgetTypeToName())
                types[widgetName] = typeName;
            mUi["TYPE"] = types;
            sol::table alignment(lua, sol::create);
            alignment["Start"] = static_cast<int>(LuaUi::Alignment::Start);
            alignment["Center"] = static_cast<int>(LuaUi::Alignment::Center);
            alignment["End"] = static_cast<int>(LuaUi::Alignment::End);
            mUi["ALIGNMENT"] = alignment;
            mUi["texture"] = [this](const sol::table& options) {
                LuaUi::TextureData data;
                sol::object path = LuaUtil::getFieldOrNil(options, "path");
                if (path.is<std::string>())
                    data.mPath = VFS::Path::Normalized(path.as<std::string>());
                if (data.mPath.empty())
                    throw std::logic_error("Invalid texture path");
                sol::object offset = LuaUtil::getFieldOrNil(options, "offset");
                if (offset.is<vsg::vec2>())
                    data.mOffset = offset.as<vsg::vec2>();
                sol::object size = LuaUtil::getFieldOrNil(options, "size");
                if (size.is<vsg::vec2>())
                    data.mSize = size.as<vsg::vec2>();
                return mResources.registerTexture(std::move(data));
            };
            mUi["_getDefaultFontSize"] = []() { return 16; };
            mUi["_getMenuTransparency"] = []() { return 1.0f; };

            // Stand-ins for the parts of openmw.core that the MWUI constants read: the header and normal text colours
            sol::table core(lua, sol::create);
            core["getGMST"] = [](const std::string& name) -> std::string {
                if (name == "FontColor_color_header")
                    return "223,201,159";
                return "202,165,96";
            };

            sol::table preset(lua, sol::create);
            preset["openmw.ui"] = mUi;
            preset["openmw.util"] = mUtil;
            preset["openmw.core"] = core;
            auto readSource = [](const std::string& name) -> std::string {
                std::string path = name;
                for (char& c : path)
                    if (c == '.')
                        c = '/';
                return readFile(sourceRoot() / "files" / "data" / (path + ".lua"));
            };
            sol::load_result makeRequire = lua.load(R"(
                local readSource, loaded = ...
                return function(name)
                    local p = loaded[name]
                    if p == nil then
                        p = assert(load(readSource(name), "@" .. name))(name)
                        loaded[name] = p
                    end
                    return p
                end
            )");
            lua["require"] = static_cast<sol::protected_function>(makeRequire)(readSource, preset).get<sol::function>();
        }
        catch (...)
        {
            shutdownAll();
            throw;
        }
    }

    void Harness::shutdownAll()
    {
        LuaUi::clearGameInterface();
        LuaUi::clearMenuInterface();
        mResources.clear();
        if (mGui)
            mGui->shutdown();
        mGui.reset();
        mRender.reset();
        mData.reset();
        mLog.reset();
    }

    Harness::~Harness()
    {
        shutdownAll();
    }

    sol::object Harness::run(const std::string& source, const std::string& chunkName)
    {
        sol::state_view lua = mLua.unsafeState();
        sol::load_result loaded = lua.load(source, chunkName, sol::load_mode::text);
        if (!loaded.valid())
            throw std::runtime_error(std::string("Lua error: ") + loaded.get<sol::error>().what());
        sol::protected_function fn = loaded;
        sol::protected_function_result result = fn(mUi, mUtil);
        if (!result.valid())
            throw std::runtime_error(std::string("Lua error: ") + result.get<sol::error>().what());
        return result;
    }

    std::shared_ptr<LuaUi::Element> Harness::createElement(const sol::table& layout)
    {
        std::shared_ptr<LuaUi::Element> element = LuaUi::Element::make(layout, false);
        element->create();
        return element;
    }

    void Harness::updateElement(LuaUi::Element& element)
    {
        element.mState = LuaUi::Element::Update;
        element.update();
    }

    namespace
    {
        // The text of a widget and its subtree, one line per widget, indented by depth. With sorting, the blocks of
        // sibling subtrees are put in text order, for layouts whose children come from an unordered Lua table.
        std::string widgetBlock(LuaUi::WidgetExtension* ext, const std::string& marker, size_t depth, bool sortSiblings)
        {
            MyGUI::Widget* widget = ext->widget();
            const MyGUI::IntCoord coord = widget->getCoord();
            char alpha[32];
            std::snprintf(alpha, sizeof(alpha), "%.4f", static_cast<double>(widget->getAlpha()));
            std::ostringstream out;
            out << std::string(depth * 2, ' ') << marker << widget->getTypeName() << " rect=" << coord.left << ','
                << coord.top << ',' << coord.width << ',' << coord.height
                << " visible=" << (widget->getVisible() ? 1 : 0) << " alpha=" << alpha;
            if (auto* text = dynamic_cast<MyGUI::TextBox*>(widget))
            {
                const MyGUI::IntSize extent = text->getTextSize();
                out << " text=" << extent.width << 'x' << extent.height;
            }
            out << '\n';

            std::vector<std::string> blocks;
            for (LuaUi::WidgetExtension* child : ext->children())
                blocks.push_back(widgetBlock(child, "", depth + 1, sortSiblings));
            std::vector<std::string> templateBlocks;
            for (LuaUi::WidgetExtension* child : ext->templateChildren())
                templateBlocks.push_back(widgetBlock(child, "template ", depth + 1, sortSiblings));
            if (sortSiblings)
            {
                std::sort(blocks.begin(), blocks.end());
                std::sort(templateBlocks.begin(), templateBlocks.end());
            }
            for (const std::string& block : blocks)
                out << block;
            for (const std::string& block : templateBlocks)
                out << block;
            return out.str();
        }
    }

    std::string recordGeometry(LuaUi::WidgetExtension* root, bool sortSiblings)
    {
        return widgetBlock(root, "", 0, sortSiblings);
    }
}
