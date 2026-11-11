#ifndef OPENEDEN_TESTS_LUAUI_HARNESS
#define OPENEDEN_TESTS_LUAUI_HARNESS

#include <filesystem>
#include <map>
#include <memory>
#include <string>
#include <vector>

#include <MyGUI_DataManager.h>
#include <MyGUI_IRenderTarget.h>
#include <MyGUI_IVertexBuffer.h>
#include <MyGUI_RenderManager.h>
#include <MyGUI_Gui.h>
#include <MyGUI_ITexture.h>
#include <MyGUI_LogManager.h>

#include <components/lua/luastate.hpp>
#include <components/lua_ui/element.hpp>
#include <components/lua_ui/resources.hpp>

namespace LuaUiTest
{
    /*
     * Test-only helpers for running the REAL components/lua_ui code without a window.
     *
     * Ownership: Harness owns the MyGUI platform objects, the Gui and the Lua state; widgets belong to MyGUI.
     * Threads: single threaded, main test thread only.
     *
     * MyGUI ships a DummyPlatform (MyGUI.DummyPlatform.lib), but its render manager answers every texture request with
     * a null pointer, which the font code cannot use, and the library is not part of every MyGUI package. The render
     * manager and the data manager here implement MyGUI's own interfaces directly: textures that keep their pixels
     * in memory, vertex buffers that draw nothing, and files read from a list of folders.
     */

    // A texture that keeps its pixels in a vector so that TrueType fonts can be rendered into it.
    class StubTexture final : public MyGUI::ITexture
    {
    public:
        explicit StubTexture(std::string name, int width = 0, int height = 0)
            : mName(std::move(name))
            , mWidth(width)
            , mHeight(height)
        {
        }
        const std::string& getName() const override { return mName; }
        void createManual(int width, int height, MyGUI::TextureUsage usage, MyGUI::PixelFormat format) override;
        void loadFromFile(const std::string&) override {}
        void saveToFile(const std::string&) override {}
        void setShader(const std::string&) override {}
        void destroy() override { mPixels.clear(); }
        void* lock(MyGUI::TextureUsage) override;
        void unlock() override { mLocked = false; }
        bool isLocked() const override { return mLocked; }
        int getWidth() const override { return mWidth; }
        int getHeight() const override { return mHeight; }
        MyGUI::PixelFormat getFormat() const override { return mFormat; }
        MyGUI::TextureUsage getUsage() const override { return mUsage; }
        size_t getNumElemBytes() const override { return mFormat.getBytesPerPixel(); }

    private:
        std::string mName;
        int mWidth;
        int mHeight;
        MyGUI::PixelFormat mFormat{ MyGUI::PixelFormat::R8G8B8A8 };
        MyGUI::TextureUsage mUsage{};
        std::vector<unsigned char> mPixels;
        bool mLocked = false;
    };

    class StubVertexBuffer final : public MyGUI::IVertexBuffer
    {
    public:
        void setVertexCount(size_t count) override { mVertices.resize(count); }
        size_t getVertexCount() const override { return mVertices.size(); }
        MyGUI::Vertex* lock() override { return mVertices.data(); }
        void unlock() override {}

    private:
        std::vector<MyGUI::Vertex> mVertices;
    };

    class StubRenderManager final : public MyGUI::RenderManager, public MyGUI::IRenderTarget
    {
    public:
        ~StubRenderManager() override;
        MyGUI::IVertexBuffer* createVertexBuffer() override { return new StubVertexBuffer; }
        void destroyVertexBuffer(MyGUI::IVertexBuffer* buffer) override { delete buffer; }
        const MyGUI::IntSize& getViewSize() const override { return mViewSize; }
        MyGUI::VertexColourType getVertexFormat() const override { return MyGUI::VertexColourType::ColourARGB; }
        void setViewSize(int width, int height) override;
        void registerShader(const std::string&, const std::string&, const std::string&) override {}
        void begin() override {}
        void end() override {}
        void doRender(MyGUI::IVertexBuffer*, MyGUI::ITexture*, size_t) override {}
        const MyGUI::RenderTargetInfo& getInfo() const override { return mInfo; }
        MyGUI::ITexture* createTexture(const std::string& name) override;
        void destroyTexture(MyGUI::ITexture* texture) override;
        MyGUI::ITexture* getTexture(const std::string& name) override;
        bool isFormatSupported(MyGUI::PixelFormat, MyGUI::TextureUsage) override { return true; }
        // Size reported for a texture file that is asked for by name (the engine reads it from the VFS).
        void setTextureSize(const std::string& name, int width, int height) { mSizes[name] = { width, height }; }
        static constexpr int sDefaultTextureSize = 16;

    private:
        MyGUI::IntSize mViewSize;
        MyGUI::RenderTargetInfo mInfo;
        std::map<std::string, std::unique_ptr<StubTexture>> mTextures;
        std::map<std::string, std::pair<int, int>> mSizes;
    };

    class FolderDataManager final : public MyGUI::DataManager
    {
    public:
        explicit FolderDataManager(std::vector<std::filesystem::path> folders)
            : mFolders(std::move(folders))
        {
        }
        MyGUI::IDataStream* getData(const std::string& name) const override;
        void freeData(MyGUI::IDataStream* data) override;
        bool isDataExist(const std::string& name) const override { return !find(name).empty(); }
        const MyGUI::VectorString& getDataListNames(const std::string&) const override { return mEmpty; }
        std::string getDataPath(const std::string& name) const override { return find(name).string(); }

    private:
        std::filesystem::path find(const std::string& name) const;
        std::vector<std::filesystem::path> mFolders;
        MyGUI::VectorString mEmpty;
    };

    struct Rect
    {
        int left, top, width, height;
    };

    // Brings up MyGUI, the Lua UI widget factories, the skins/layers of files/data/mygui and a TrueType font named
    // "Default" (the same DejaVu file the game ships), and a Lua state with the packages a layout script needs.
    class Harness
    {
    public:
        explicit Harness(int viewWidth = 1024, int viewHeight = 768);
        ~Harness();
        Harness(const Harness&) = delete;
        Harness& operator=(const Harness&) = delete;

        LuaUtil::LuaState& lua() { return mLua; }
        StubRenderManager& renderManager() { return *mRender; }
        sol::table uiPackage() { return mUi; }
        sol::table utilPackage() { return mUtil; }

        // Runs a Lua chunk with `ui` and `util` as the arguments and returns what it returns.
        sol::object run(const std::string& source, const std::string& chunkName);

        // Creates the element for a layout table (as `ui.create` would, without the delayed action queue).
        std::shared_ptr<LuaUi::Element> createElement(const sol::table& layout);
        // As `ui.update` would: marks the element for update and applies it.
        void updateElement(LuaUi::Element& element);

    private:
        void shutdownAll();
        std::unique_ptr<StubRenderManager> mRender;
        std::unique_ptr<FolderDataManager> mData;
        std::unique_ptr<MyGUI::LogManager> mLog;
        std::unique_ptr<MyGUI::Gui> mGui;
        LuaUtil::LuaState mLua;
        LuaUi::ResourceManager mResources;
        sol::table mUi;
        sol::table mUtil;
    };

    // One line per widget, indented by depth: type, rectangle, own visibility, alpha, and for text widgets the text
    // extent. Children made from a layout template are marked "template". With `sortSiblings` the subtrees of
    // siblings are listed in text order instead of creation order (for layouts built by iterating a Lua hash table).
    std::string recordGeometry(LuaUi::WidgetExtension* root, bool sortSiblings = false);
}

#endif
