#ifndef COMPONENTS_LUA_CONFIGURATION_H
#define COMPONENTS_LUA_CONFIGURATION_H

#include <functional>
#include <map>
#include <optional>
#include <string>

#include <components/esm3/refnum.hpp>
#include <components/esmbridge/luascripts.hpp>
#include <components/vfs/pathutil.hpp>

namespace ESM
{
    class ESMReader;
    class ESMWriter;
}

namespace LuaUtil
{
    using ScriptIdsWithInitializationData = std::map<int, std::string_view>;

    class ScriptsConfiguration
    {
    public:
        // Answers a script's requirement: an empty string if it is met, else why not (what the session has). Added in
        // task 0097 stage C. With no function given every script that has a requirement is refused.
        using RequirementCheck = std::function<std::string(const ESM::LuaScriptCfg::Requirement&)>;

        // A script whose requirement is not met is dropped here, after duplicates and merges are settled, as if its mod
        // were absent: it starts nowhere, `findId` does not know it and a saved script of it finds no mapping.
        void init(ESM::LuaScriptsCfg, bool, const RequirementCheck& check = {});

        size_t size() const { return mScripts.size(); }
        const ESM::LuaScriptCfg& operator[](size_t id) const { return mScripts[id]; }

        std::optional<int> findId(VFS::Path::NormalizedView path) const;
        std::optional<int> mapId(int savedId) const;

        bool isCustomScript(int id) const { return mScripts[id].mFlags & ESM::LuaScriptCfg::sCustom; }

        ScriptIdsWithInitializationData getMenuConf() const { return getConfByFlag(ESM::LuaScriptCfg::sMenu); }
        ScriptIdsWithInitializationData getGlobalConf() const { return getConfByFlag(ESM::LuaScriptCfg::sGlobal); }
        ScriptIdsWithInitializationData getPlayerConf() const { return getConfByFlag(ESM::LuaScriptCfg::sPlayer); }
        ScriptIdsWithInitializationData getLoadConf() const { return getConfByFlag(ESM::LuaScriptCfg::sLoad); }
        ScriptIdsWithInitializationData getLocalConf(
            uint32_t type, const ESM::RefId& recordId, ESM::RefNum refnum) const;

        void read(ESM::ESMReader&);
        void write(ESM::ESMWriter&) const;

    private:
        ScriptIdsWithInitializationData getConfByFlag(ESM::LuaScriptCfg::Flags flag) const;

        std::vector<ESM::LuaScriptCfg> mScripts;
        std::map<VFS::Path::Normalized, int, std::less<>> mPathToIndex;

        struct DetailedConf
        {
            int mScriptId;
            bool mAttach;
            std::string_view mInitializationData;
        };
        std::map<uint32_t, std::vector<int>> mScriptsPerType;
        std::map<ESM::RefId, std::vector<DetailedConf>, std::less<>> mScriptsPerRecordId;
        std::map<ESM::RefNum, std::vector<DetailedConf>> mScriptsPerRefNum;
        std::map<int, int> mScriptIdMapping;
    };

    // Parse ESM::LuaScriptsCfg from text and add to `cfg`.
    void parseOMWScripts(ESM::LuaScriptsCfg& cfg, std::string_view data);

    std::string scriptCfgToString(const ESM::LuaScriptCfg& script);

}

#endif // COMPONENTS_LUA_CONFIGURATION_H
