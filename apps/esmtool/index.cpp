#include "index.hpp"

#include <iomanip>
#include <iostream>

#include <components/contentsource/plugin.hpp>

namespace EsmTool
{
    namespace
    {
        const char* gameName(Content::GameGuess game)
        {
            switch (game)
            {
                case Content::GameGuess::Morrowind:
                    return "morrowind";
                case Content::GameGuess::SkyrimSE:
                    return "skyrimse";
                case Content::GameGuess::Fallout4:
                    return "fallout4";
                case Content::GameGuess::FalloutNV:
                    return "falloutnv";
                case Content::GameGuess::Unknown:
                    break;
            }
            return "unknown";
        }
    }

    int index(const Arguments& info)
    {
        Content::Error error;
        const std::unique_ptr<Content::Plugin> plugin = Content::Plugin::openFile(info.filename, error);
        if (plugin == nullptr)
        {
            std::cout << "ERROR: " << error.describe() << std::endl;
            return 1;
        }
        const Content::Header& h = plugin->header();
        std::cout << "family " << (h.mFamily == Content::Family::Tes3 ? "tes3" : "tes4") << '\n';
        std::cout << "game " << gameName(h.mGame) << '\n';
        std::cout << "version " << h.mVersion << '\n';
        std::cout << "formVersion " << h.mFormVersion << '\n';
        std::cout << "recordHeaderSize " << h.mRecordHeaderSize << '\n';
        std::cout << "flags 0x" << std::hex << h.mFlags << std::dec << " master " << h.isMaster() << " light "
                  << h.isLight() << " localized " << h.isLocalized() << '\n';
        std::cout << "declaredRecords " << h.mDeclaredRecords << '\n';
        std::cout << "masters " << h.mMasters.size() << '\n';
        std::cout << "fileSize " << plugin->fileSize() << '\n';

        Content::Summary summary;
        const std::optional<Content::Error> walkError = Content::summarise(*plugin, summary);
        std::cout << "records " << summary.mRecords << " groups " << summary.mGroups << " compressed "
                  << summary.mCompressed << " deleted " << summary.mDeleted << " maxGroupDepth "
                  << summary.mMaxGroupDepth << '\n';
        for (const auto& [type, count] : summary.mGroupsByType)
        {
            std::cout << "groupType " << type << ' ' << count << '\n';
        }
        for (const auto& [type, count] : summary.mRecordsByType)
        {
            std::cout << "type " << Content::fourCCToString(type) << ' ' << count << '\n';
        }
        if (walkError.has_value())
        {
            std::cout << "ERROR: " << walkError->describe() << std::endl;
            return 1;
        }
        return 0;
    }
}
