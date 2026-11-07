#include "game_services.hpp"

#include <algorithm>

namespace openeden::extension
{
    bool RecordServiceImpl::registerRecordType(const openeden::sdk::RecordTypeInfo& info)
    {
        if (info.typeId == 0 || info.sizeBytes == 0 || info.name.empty())
            return false;

        const auto [it, inserted] = mTypes.try_emplace(info.typeId);
        if (!inserted)
            return false;

        Entry& entry = it->second;
        entry.name = std::string(info.name);
        entry.info = info;
        entry.info.name = entry.name; // the node does not move, so the view stays valid
        return true;
    }

    const openeden::sdk::RecordTypeInfo* RecordServiceImpl::findRecordType(std::uint32_t typeId) const
    {
        const auto it = mTypes.find(typeId);
        return it == mTypes.end() ? nullptr : &it->second.info;
    }

    void RecordServiceImpl::clear() noexcept
    {
        mTypes.clear();
    }

    void ContentServiceImpl::registerLoader(openeden::sdk::ContentLoader& loader)
    {
        mLoaders.push_back(&loader);
    }

    openeden::sdk::ContentLoader* ContentServiceImpl::findLoader(const openeden::sdk::ContentHeader& header) const
    {
        for (openeden::sdk::ContentLoader* loader : mLoaders)
        {
            if (loader->probe(header))
                return loader;
        }
        return nullptr;
    }

    bool ScriptHostServiceImpl::declareLanguage(std::string_view name)
    {
        if (name.empty())
            return false;
        return mLanguages.emplace(name).second;
    }

    bool ScriptHostServiceImpl::isLanguageDeclared(std::string_view name) const
    {
        return mLanguages.find(name) != mLanguages.end();
    }

    bool UiServiceImpl::declareWindow(std::string_view id, std::string_view title)
    {
        if (id.empty())
            return false;
        return mWindows.try_emplace(std::string(id), std::string(title)).second;
    }

    bool UiServiceImpl::isWindowDeclared(std::string_view id) const
    {
        return mWindows.find(id) != mWindows.end();
    }
} // namespace openeden::extension
