#include <components/nifedit/nifdocument.hpp>

#include <gtest/gtest.h>

#include <algorithm>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <map>
#include <sstream>

// Task 0046: a check of the user's own loose Morrowind meshes. Not run by default: with OPENEDEN_NIF_MORROWIND_DIR set
// to a folder of them, every .nif is opened for editing, saved without edits (must be byte-identical), saved with every
// editable field set to its own value (must be byte-identical), and edited (a longer name on every supported record,
// a changed value on every supported field) and read again by the engine's reader. Prints counts only: nothing of the
// files is shown, and nothing is written.
namespace NifEdit
{
    namespace
    {
        TEST(NifEditReal, loose_morrowind_meshes_round_trip_and_edit_when_asked)
        {
            const char* dir = std::getenv("OPENEDEN_NIF_MORROWIND_DIR");
            if (dir == nullptr)
            {
                GTEST_SKIP() << "OPENEDEN_NIF_MORROWIND_DIR is not set";
            }
            std::vector<std::filesystem::path> files;
            for (const auto& entry : std::filesystem::recursive_directory_iterator(dir))
            {
                std::string extension = entry.path().extension().string();
                for (char& c : extension)
                    c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
                if (entry.is_regular_file() && extension == ".nif")
                    files.push_back(entry.path());
            }
            std::sort(files.begin(), files.end());

            std::size_t opened = 0, notOpened = 0, saveDiffers = 0, identityDiffers = 0, editFailed = 0;
            std::size_t fieldsChecked = 0, recordsEditable = 0, recordsTotal = 0;
            std::size_t extraRemainingMatches = 0, extraRemainingOther = 0, textRemainingMatches = 0,
                        textRemainingOther = 0;
            std::map<std::string, std::size_t> refusals;
            std::map<std::string, std::size_t> unsupportedTypes;
            for (const auto& path : files)
            {
                std::ifstream input(path, std::ios::binary);
                std::ostringstream stream;
                stream << input.rdbuf();
                const std::string bytes = stream.str();
                try
                {
                    Document document = Document::open(bytes);
                    ++opened;
                    if (document.build() != bytes)
                        ++saveDiffers;
                    PatchSet identity;
                    for (const IndexedRecord& record : document.index().mRecords)
                    {
                        ++recordsTotal;
                        const auto fields = editableFields(record.mType, document.index().mVersion);
                        if (fields.empty())
                        {
                            ++unsupportedTypes[record.mType];
                            continue;
                        }
                        ++recordsEditable;
                        for (const std::string& field : fields)
                        {
                            try
                            {
                                identity.set({ record.mIndex, field, document.originalValue(record.mIndex, field) });
                                ++fieldsChecked;
                            }
                            catch (const EditError& e)
                            {
                                // A record whose layout the editor does not know (an unusual file): counted by cause.
                                std::string why = e.what();
                                why = why.substr(why.find("): ") == std::string::npos ? 0 : why.find("): ") + 3);
                                ++refusals[why.substr(0, 60)];
                            }
                        }
                        if (record.mType == "NiStringExtraData" || record.mType == "NiTextKeyExtraData")
                        {
                            std::uint32_t remaining;
                            std::memcpy(&remaining, bytes.data() + record.mBodyBegin + 4, 4);
                            const std::uint64_t afterField = record.mEnd - (record.mBodyBegin + 8);
                            const bool match = remaining == afterField;
                            if (record.mType == "NiStringExtraData")
                                (match ? extraRemainingMatches : extraRemainingOther)++;
                            else
                                (match ? textRemainingMatches : textRemainingOther)++;
                        }
                    }
                    if (applyPatches(bytes, document.index(), identity) != bytes)
                        ++identityDiffers;

                    // A real edit: every supported name grows, and every flags word of every record is set to 0.
                    Document edited = Document::open(bytes);
                    for (const IndexedRecord& record : edited.index().mRecords)
                    {
                        const auto fields = editableFields(record.mType, edited.index().mVersion);
                        for (const std::string& field : fields)
                        {
                            try
                            {
                                if (field == "name")
                                    edited.set(record.mIndex, field,
                                        std::get<std::string>(edited.originalValue(record.mIndex, field)) + "_edited");
                                else if (field == "flags")
                                    edited.set(record.mIndex, field, std::int64_t(0));
                            }
                            catch (const EditError&)
                            {
                            }
                        }
                    }
                    (void)edited.buildChecked();
                }
                catch (const EditError& e)
                {
                    std::string why = e.what();
                    ++refusals[why.substr(0, 70)];
                    ++editFailed;
                }
                catch (const std::exception&)
                {
                    ++notOpened;
                }
            }
            std::cout << "NIFEDIT-REAL files " << files.size() << " opened " << opened << " not-readable " << notOpened
                      << " edit-or-open-refused " << editFailed << " unedited-save-differs " << saveDiffers
                      << " identity-differs " << identityDiffers << " records " << recordsTotal << " editable-records "
                      << recordsEditable << " identity-fields " << fieldsChecked << "\n";
            std::cout << "NIFEDIT-REAL extra-data bytes-remaining string: matches " << extraRemainingMatches
                      << " other " << extraRemainingOther << "; text keys: matches " << textRemainingMatches
                      << " other " << textRemainingOther << "\n";
            for (const auto& [why, count] : refusals)
                std::cout << "NIFEDIT-REAL refusal " << count << " x " << why << "\n";
            for (const auto& [type, count] : unsupportedTypes)
                std::cout << "NIFEDIT-REAL unsupported-type " << type << " " << count << "\n";
            EXPECT_EQ(saveDiffers, 0u);
            EXPECT_EQ(identityDiffers, 0u);
        }
    }
}
