#include <components/nif/data.hpp>
#include <components/nif/niffile.hpp>
#include <components/nif/node.hpp>

#include <gtest/gtest.h>

#include <algorithm>
#include <array>
#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <memory>
#include <sstream>
#include <string>
#include <vector>

#include "nifbuilder.hpp"

// Task 0082: Morrowind files must load exactly as they did before the reader got its options. The expected hash below
// was computed by the reader as it was BEFORE `ReadOptions` existed (the test is written against `Reader::parse`'s
// original signature and the fields of the records), then pinned: a change that alters any parsed value of these
// generated files changes the hash.
namespace Nif
{
    namespace
    {
        using Testing::ByteWriter;

        constexpr VFS::Path::NormalizedView path("morrowind-golden");

        void writeMwAvObject(ByteWriter& out, const std::string& name, float x, float y, float z, float scale)
        {
            out.sized(name);
            out.put<std::int32_t>(-1); // extra data
            out.put<std::int32_t>(-1); // controller
            out.put<std::uint16_t>(0x0008); // flags
            out.floats({ x, y, z });
            out.floats({ 0.f, -1.f, 0.f, 1.f, 0.f, 0.f, 0.f, 0.f, 1.f }); // a quarter turn about Z
            out.put(scale);
            out.floats({ 0.f, 0.f, 0.f }); // velocity
            out.put<std::int32_t>(0); // properties
            out.put<std::int32_t>(0); // no bounds
        }

        void writeMwNode(ByteWriter& out, const std::string& name, float x, float y, float z, float scale,
            const std::vector<std::int32_t>& children)
        {
            out.sized("NiNode");
            writeMwAvObject(out, name, x, y, z, scale);
            out.put(static_cast<std::int32_t>(children.size()));
            for (std::int32_t child : children)
                out.put(child);
            out.put<std::int32_t>(0); // effects
        }

        // Records: 0 root "Scene Root" (translated: the reader replaces record 0's transform), 1 "Bip01" child that
        // keeps its transform, 2 a shape, 3 its data, 4 a second child "Bip01" spelled differently.
        std::string makeMorrowindFile(std::uint32_t fileVersion, bool bip01Root)
        {
            ByteWriter out;
            out.raw(fileVersion == 0x04000000 ? "NetImmerse File Format, Version 4.0.0.0\n"
                                              : "NetImmerse File Format, Version 4.0.0.2\n");
            out.put(fileVersion);
            out.put<std::uint32_t>(5);

            writeMwNode(out, bip01Root ? "BIP01" : "Scene Root", 10.f, -20.f, 30.f, 2.f, { 1, 2, 4 });
            writeMwNode(out, "Bip01 Pelvis", 1.5f, 2.5f, -3.5f, 0.5f, {});

            out.sized("NiTriShape");
            writeMwAvObject(out, "Body", 0.f, 1.f, 2.f, 1.f);
            out.put<std::int32_t>(3); // data
            out.put<std::int32_t>(-1); // skin

            out.sized("NiTriShapeData");
            out.put<std::uint16_t>(4); // vertices
            out.put<std::int32_t>(1); // has vertices
            out.floats({ 0, 0, 0, 1, 0, 0, 1, 1, 0, 0, 1, 1 });
            out.put<std::int32_t>(1); // has normals
            out.floats({ 0, 0, 1, 0, 0, 1, 0, 0, 1, 0, 0, 1 });
            out.floats({ 0.5f, 0.5f, 0.5f, 1.f }); // bounding sphere
            out.put<std::int32_t>(0); // no colours
            out.put<std::uint16_t>(1); // one UV set
            out.put<std::int32_t>(1); // has UVs
            out.floats({ 0, 0, 1, 0, 1, 1, 0, 1 });
            out.put<std::uint16_t>(2); // triangles
            out.put<std::uint32_t>(6); // indices
            for (const std::uint16_t index : std::array<std::uint16_t, 6>{ 0, 1, 2, 0, 2, 3 })
                out.put(index);
            out.put<std::uint16_t>(0); // match groups

            writeMwNode(out, "bip01", -4.f, 5.f, 6.f, 3.f, {});

            out.put<std::uint32_t>(1); // roots
            out.put<std::int32_t>(0);
            return out.bytes();
        }

        std::string dumpTransform(const NiTransform& transform)
        {
            std::ostringstream out;
            out << " t=" << transform.mTranslation.x << "," << transform.mTranslation.y << ","
                << transform.mTranslation.z << " s=" << transform.mScale << " r=";
            for (const auto& row : transform.mRotation.mValues)
            {
                for (float value : row)
                    out << value << ",";
            }
            return out.str();
        }

        // A text dump of the parsed records: type, index, name, transform, links, geometry arrays.
        std::string dump(const NIFFile& file)
        {
            std::ostringstream out;
            out << std::setprecision(9);
            out << "version " << file.mVersion << " user " << file.mUserVersion << " beth " << file.mBethVersion
                << "\n";
            for (const auto& record : file.mRecords)
            {
                out << record->mRecordIndex << " " << record->mRecordName;
                if (const auto* node = dynamic_cast<const NiAVObject*>(record.get()))
                {
                    out << " name=" << node->mName << " flags=" << node->mFlags << dumpTransform(node->mTransform);
                    if (const auto* group = dynamic_cast<const NiNode*>(record.get()))
                    {
                        out << " children=";
                        for (const auto& child : group->mChildren)
                            out << (child.empty() ? -1 : static_cast<int>(child->mRecordIndex)) << ",";
                    }
                    if (const auto* geometry = dynamic_cast<const NiGeometry*>(record.get()))
                        out << " data="
                            << (geometry->mData.empty() ? -1 : static_cast<int>(geometry->mData->mRecordIndex));
                }
                if (const auto* data = dynamic_cast<const NiGeometryData*>(record.get()))
                {
                    out << " vertices=";
                    for (const auto& v : data->mVertices)
                        out << v.x << "," << v.y << "," << v.z << ";";
                    out << " normals=";
                    for (const auto& v : data->mNormals)
                        out << v.x << "," << v.y << "," << v.z << ";";
                    out << " uvs=";
                    for (const auto& set : data->mUVList)
                    {
                        for (const auto& v : set)
                            out << v.x << "," << v.y << ";";
                        out << "|";
                    }
                }
                if (const auto* shape = dynamic_cast<const NiTriShapeData*>(record.get()))
                {
                    out << " triangles=";
                    for (unsigned short index : shape->mTriangles)
                        out << index << ",";
                }
                out << "\n";
            }
            out << "roots=";
            for (const Record* root : file.mRoots)
                out << (root == nullptr ? -1 : static_cast<int>(root->mRecordIndex)) << ",";
            out << "\n";
            return out.str();
        }

        std::uint64_t fnv1a(const std::string& text)
        {
            std::uint64_t hash = 14695981039346656037ull;
            for (unsigned char c : text)
            {
                hash ^= c;
                hash *= 1099511628211ull;
            }
            return hash;
        }

        std::string parseAndDump(const std::string& bytes)
        {
            NIFFile file(path);
            Reader reader(file, nullptr);
            reader.parse(std::make_unique<std::istringstream>(bytes));
            return dump(file);
        }

        TEST(NifMorrowindUnchanged, a_generated_4_0_0_2_file_parses_to_the_pinned_dump)
        {
            const std::string text = parseAndDump(makeMorrowindFile(0x04000002, false));
            // The root's own transform is replaced by the identity (a Morrowind rule of NiNode::read); the second
            // NiNode named "bip01" keeps its transform.
            EXPECT_NE(text.find("0 NiNode name=Scene Root"), std::string::npos);
            EXPECT_NE(text.find(" name=Scene Root flags=8 t=0,0,0 s=1 "), std::string::npos) << text;
            EXPECT_NE(text.find(" name=bip01 flags=8 t=-4,5,6 s=3 "), std::string::npos) << text;
            EXPECT_EQ(fnv1a(text), 10830023638193184693ull) << text;
        }

        TEST(NifMorrowindUnchanged, a_root_called_bip01_keeps_its_transform)
        {
            const std::string text = parseAndDump(makeMorrowindFile(0x04000002, true));
            EXPECT_NE(text.find(" name=BIP01 flags=8 t=10,-20,30 s=2 "), std::string::npos) << text;
            EXPECT_EQ(fnv1a(text), 3036533786913774926ull) << text;
        }

        TEST(NifMorrowindUnchanged, a_generated_4_0_0_0_file_parses_to_the_pinned_dump)
        {
            const std::string text = parseAndDump(makeMorrowindFile(0x04000000, false));
            EXPECT_EQ(fnv1a(text), 9734782150469507659ull) << text;
        }

        // Not run by default: with OPENEDEN_NIF_MORROWIND_DIR set to a folder of the user's own loose Morrowind
        // meshes, parses every .nif in it with the Morrowind default options and writes one line per file (relative
        // path, hash of the record dump, or the error) to the file named by OPENEDEN_NIF_DUMP_OUT, plus a total hash.
        // Run before and after a reader change; the two outputs must be identical. Nothing of the files is printed.
        TEST(NifMorrowindUnchanged, real_files_dump_hashes_when_asked)
        {
            const char* dir = std::getenv("OPENEDEN_NIF_MORROWIND_DIR");
            const char* out = std::getenv("OPENEDEN_NIF_DUMP_OUT");
            if (dir == nullptr || out == nullptr)
                GTEST_SKIP() << "OPENEDEN_NIF_MORROWIND_DIR and OPENEDEN_NIF_DUMP_OUT are not set";
            std::vector<std::filesystem::path> files;
            for (const auto& entry : std::filesystem::recursive_directory_iterator(dir))
            {
                if (!entry.is_regular_file())
                    continue;
                std::string extension = entry.path().extension().string();
                for (char& c : extension)
                    c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
                if (extension == ".nif")
                    files.push_back(entry.path());
            }
            std::sort(files.begin(), files.end());
            std::ofstream report(out);
            std::string all;
            std::size_t failed = 0;
            for (const auto& file : files)
            {
                std::string line = std::filesystem::relative(file, dir).generic_string();
                std::string result;
                try
                {
                    std::ifstream input(file, std::ios::binary);
                    std::ostringstream bytes;
                    bytes << input.rdbuf();
                    result = std::to_string(fnv1a(parseAndDump(bytes.str())));
                }
                catch (const std::exception& e)
                {
                    ++failed;
                    result = std::string("error: ") + e.what();
                }
                line += " " + result + '\n';
                report << line;
                all += line;
            }
            report << "total " << files.size() << " files, " << failed << " errors, hash " << fnv1a(all) << '\n';
        }
    }
}
