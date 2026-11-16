// Task 0203: the BOOK records and their texts on the five Skyrim SE masters (aggregates only: lines SKYRIMSE-BOOKS).
// Settles the DATA layout and checks that the text renderer copes with every DESC. Skipped without
// OPENEDEN_SKYRIMSE_DATA (the game's Data folder; "Skyrim - Interface.bsa" holds the string tables).
#include <game_skyrimse/lockbooks.hpp>
#include <game_skyrimse/pack_core.hpp>

#include <gtest/gtest.h>

#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <filesystem>
#include <iostream>
#include <map>
#include <string>
#include <vector>

namespace
{
    namespace fs = std::filesystem;
    using namespace game_skyrimse;
    using namespace game_skyrimse::lockbooks;

    TEST(SkyrimBooksRealData, theMastersBooksDecodeAndTheirTextRenders)
    {
        const char* env = std::getenv("OPENEDEN_SKYRIMSE_DATA");
        if (env == nullptr || *env == '\0' || !fs::is_directory(env))
            GTEST_SKIP() << "OPENEDEN_SKYRIMSE_DATA is not set";
        const fs::path data(env);
        const std::vector<std::string> masters
            = { "Skyrim.esm", "Update.esm", "Dawnguard.esm", "HearthFires.esm", "Dragonborn.esm" };
        for (const std::string& n : masters)
            if (!fs::exists(data / n))
                GTEST_SKIP() << "the five official masters are not all in the Data folder";
        if (!fs::exists(data / "Skyrim - Interface.bsa"))
            GTEST_SKIP() << "Skyrim - Interface.bsa is missing";

        LoadOrder order;
        std::string err;
        for (const std::string& n : masters)
            ASSERT_TRUE(order.add(n, false, err)) << err;

        std::uint64_t books = 0, odd = 0, failed = 0, both = 0, skill = 0, spell = 0, plain = 0, cannotTake = 0;
        std::map<int, int> perSlot;
        std::uint64_t texts = 0, noText = 0, pages = 0, maxPages = 0, maxPageChars = 0, pictures = 0, pagebreaks = 0;
        std::map<std::string, std::uint64_t> tags;
        for (const std::string& n : masters)
        {
            Content::Error error;
            const auto plugin = Content::Plugin::openFile(data / n, error);
            ASSERT_NE(plugin, nullptr) << error.describe();
            BookRecords records;
            ASSERT_TRUE(parseBookRecords(*plugin, order, BookLimits(), records));
            odd += records.mOdd;
            failed += records.mFailed;
            both += records.mBothFlags;

            StringTable table;
            std::string bytes;
            std::string archiveError;
            const std::string tablePath
                = stringTablePath(fs::path(n).stem().string(), "english", StringTableKind::DlStrings);
            const bool haveTable = records.mLocalized
                && readArchiveEntry(data / "Skyrim - Interface.bsa", tablePath, bytes, archiveError)
                && StringTable::parse({ reinterpret_cast<const std::byte*>(bytes.data()), bytes.size() },
                    StringTableKind::DlStrings, n, table, error);
            for (const BookRecord& b : records.mBooks)
            {
                ++books;
                EXPECT_TRUE(b.mDecoded) << n;
                EXPECT_EQ(b.mFlags & ~(BookFlags::TeachesSkill | BookFlags::CannotBeTaken | BookFlags::TeachesSpell), 0)
                    << "an unknown flag";
                if ((b.mFlags & BookFlags::CannotBeTaken) != 0)
                    ++cannotTake;
                switch (b.kind())
                {
                    case BookKind::Skill:
                        ++skill;
                        EXPECT_GE(b.skillSlot(), 0) << "a skill book teaches an actor value from 6 to 23";
                        ++perSlot[b.skillSlot()];
                        break;
                    case BookKind::Spell:
                        ++spell;
                        EXPECT_NE(b.mTeaches, 0u);
                        break;
                    case BookKind::Plain:
                        ++plain;
                        break;
                }
                LString desc = b.mDesc;
                if (desc.mLocalized && haveTable)
                    (void)lookupLString(desc, table);
                if (desc.mText.empty())
                {
                    ++noText;
                    continue;
                }
                ++texts;
                for (std::size_t i = 0; i < desc.mText.size(); ++i)
                    if (desc.mText[i] == '<')
                    {
                        std::size_t j = i + 1;
                        if (j < desc.mText.size() && desc.mText[j] == '/')
                            ++j;
                        std::string name;
                        while (j < desc.mText.size() && std::isalnum(static_cast<unsigned char>(desc.mText[j])) != 0)
                        {
                            const auto c = static_cast<unsigned char>(desc.mText[j++]);
                            name.push_back(static_cast<char>(std::tolower(c)));
                        }
                        ++tags[name];
                    }
                const std::vector<std::string> rendered = renderBookText(desc.mText);
                const char* show = std::getenv("OPENEDEN_BOOKS_SHOW");
                if (show != nullptr && b.mEditorId.rfind(show, 0) == 0)
                {
                    std::string escaped;
                    for (const char c : desc.mText)
                        escaped += c == '\n' ? std::string("\\n") : c == '\r' ? std::string("\\r") : std::string(1, c);
                    std::cout << "SKYRIMSE-BOOKS show " << b.mEditorId << " raw: " << escaped.substr(0, 600) << "\n";
                    for (const std::string& p : rendered)
                        std::cout << "SKYRIMSE-BOOKS page: [" << p.substr(0, 300) << "]\n";
                }
                pages += rendered.size();
                maxPages = std::max<std::uint64_t>(maxPages, rendered.size());
                for (const std::string& p : rendered)
                {
                    maxPageChars = std::max<std::uint64_t>(maxPageChars, p.size());
                    EXPECT_EQ(p.find('<'), std::string::npos) << "a tag was left in a page";
                }
                if (desc.mText.find("[pagebreak]") != std::string::npos)
                    ++pagebreaks;
                if (desc.mText.find("<img") != std::string::npos)
                    ++pictures;
            }
        }
        std::cout << "SKYRIMSE-BOOKS books " << books << " odd " << odd << " failed " << failed << " bothFlags " << both
                  << " skill " << skill << " spell " << spell << " plain " << plain << " cannotBeTaken " << cannotTake
                  << " texts " << texts << " noText " << noText << " pages " << pages << " maxPages " << maxPages
                  << " maxPageChars " << maxPageChars << " withPagebreak " << pagebreaks << " withImg " << pictures
                  << "\n";
        std::cout << "SKYRIMSE-BOOKS tags";
        for (const auto& [name, count] : tags)
            std::cout << " " << (name.empty() ? "(none)" : name) << "=" << count;
        std::cout << "\nSKYRIMSE-BOOKS skillsPerSlot";
        for (const auto& [slot, count] : perSlot)
            std::cout << " " << slot << "=" << count;
        std::cout << "\n";
        EXPECT_EQ(odd, 0u);
        EXPECT_EQ(failed, 0u);
        EXPECT_EQ(perSlot.size(), 18u);
    }
}
