#ifndef OPENMW_LUAUI_RESOURCES
#define OPENMW_LUAUI_RESOURCES

#include <memory>
#include <unordered_map>
#include <vector>

#include <vsg/maths/vec2.h>

#include <components/vfs/pathutil.hpp>

namespace VFS
{
    class Manager;
}

namespace LuaUi
{
    struct TextureData
    {
        VFS::Path::Normalized mPath;
        vsg::vec2 mOffset;
        vsg::vec2 mSize;
    };

    // will have more/different data when automated atlasing is supported
    using TextureResource = TextureData;

    class ResourceManager
    {
    public:
        std::shared_ptr<TextureResource> registerTexture(TextureData data)
        {
            TextureResources& list = mTextures[data.mPath];
            list.push_back(std::make_shared<TextureResource>(std::move(data)));
            return list.back();
        }

        void clear() { mTextures.clear(); }

    private:
        using TextureResources = std::vector<std::shared_ptr<TextureResource>>;
        std::unordered_map<VFS::Path::Normalized, TextureResources, VFS::Path::Hash> mTextures;
    };
}

#endif // OPENMW_LUAUI_LAYERS
