#ifndef OPENEDEN_TEST_SUITE_NIFEDIT_NIFEDITBUILDER_H
#define OPENEDEN_TEST_SUITE_NIFEDIT_NIFEDITBUILDER_H

// Generated Morrowind NIF files for the NIF editing tests (task 0046). Every byte is written here, so no game asset
// enters the repository, and the builder records where each editable field lies: the tests use those offsets as an
// source of truth that does not depend on the library under test.

#include <array>
#include <cstdint>
#include <cstring>
#include <initializer_list>
#include <map>
#include <string>
#include <tuple>
#include <utility>
#include <vector>

namespace NifEdit::Testing
{
    struct Built
    {
        std::string mBytes;
        // "<record>:<field>" -> [begin, end) in mBytes
        std::map<std::string, std::pair<std::size_t, std::size_t>> mFields;
        // record index -> [begin, end) in mBytes (the type name included)
        std::vector<std::pair<std::size_t, std::size_t>> mRecords;
        std::size_t mHeaderEnd = 0;
        std::size_t mFooterBegin = 0;

        std::pair<std::size_t, std::size_t> field(int record, const std::string& name) const
        {
            return mFields.at(std::to_string(record) + ":" + name);
        }
    };

    struct TextKeyData
    {
        float mTime;
        std::string mText;
    };

    class MwBuilder
    {
    public:
        explicit MwBuilder(std::uint32_t version = 0x04000002)
            : mVersion(version)
        {
        }

        // Record numbers of the file make() builds:
        //  0 NiNode "Scene Root" (children 1, 2)      1 NiTriShape "Body" (data 3, properties 4, 5)
        //  2 NiNode "Child"                            3 NiTriShapeData
        //  4 NiMaterialProperty "Mat"                  5 NiAlphaProperty "Alpha"
        //  6 NiSourceTexture "Tex" (external)          7 NiStringExtraData
        //  8 NiTextKeyExtraData                        9 NiVertexColorProperty "Vc"
        // 10 NiZBufferProperty "Z"                    11 NiSpecularProperty "Spec"
        // 12 NiWireframeProperty "Wire"               13 NiDitherProperty "Dither"
        // 14 NiShadeProperty "Shade"
        static Built make(std::uint32_t version = 0x04000002) { return MwBuilder(version).build(); }

        Built build()
        {
            avObject(0, "NiNode", "Scene Root", { 1.f, 2.f, 3.f }, 1.5f, {});
            list({ 1, 2 });
            list({}); // effects

            avObject(1, "NiTriShape", "Body", { 10.f, 20.f, 30.f }, 2.f, { 4, 5 });
            i32(3);
            i32(-1);

            avObject(2, "NiNode", "Child", { -1.f, -2.f, -3.f }, 0.5f, {});
            list({});
            list({});

            triShapeData();

            property(4, "NiMaterialProperty", "Mat");
            u16(4, "flags", 0x0001);
            colour(4, "ambient", { 0.1f, 0.2f, 0.3f });
            colour(4, "diffuse", { 0.4f, 0.5f, 0.6f });
            colour(4, "specular", { 0.7f, 0.8f, 0.9f });
            colour(4, "emissive", { 0.f, 0.f, 0.f });
            f32(4, "glossiness", 10.f);
            f32(4, "alpha", 1.f);

            property(5, "NiAlphaProperty", "Alpha");
            u16(5, "flags", 0x12ED);
            u8(5, "threshold", 128);

            property(6, "NiSourceTexture", "Tex");
            u8(6, "external", 1); // external flag, one byte
            str(6, "file", "textures\\tex_a.tga");
            u32(5); // pixel layout
            u32(2); // mip maps
            u32(3); // alpha
            put<std::uint8_t>(1); // is static (one byte)

            extraHeader(7, "NiStringExtraData", 18); // 4 + the 14 characters below
            str(7, "string", "NiOptimizeKeep");

            extraHeader(8, "NiTextKeyExtraData", 0); // 0 in real files
            textKeys(8, { { 0.f, "idle: Start" }, { 1.5f, "idle: Stop" }, { 2.f, "sound: Foot" } });

            property(9, "NiVertexColorProperty", "Vc");
            u16(9, "flags", 0x0003);
            u32(1); // vertex mode
            u32(2); // lighting mode
            simpleProperty(10, "NiZBufferProperty", "Z", 0x0007);
            simpleProperty(11, "NiSpecularProperty", "Spec", 0x0001);
            simpleProperty(12, "NiWireframeProperty", "Wire", 0x0000);
            simpleProperty(13, "NiDitherProperty", "Dither", 0x0001);
            simpleProperty(14, "NiShadeProperty", "Shade", 0x0001);

            return finish({ 0 });
        }

    private:
        struct Rec
        {
            std::string mBytes;
            std::map<std::string, std::pair<std::size_t, std::size_t>> mFields; // relative to the record start
        };

        template <class T>
        void put(const T& value)
        {
            mCurrent().mBytes.append(reinterpret_cast<const char*>(&value), sizeof(T));
        }

        Rec& mCurrent() { return mRecords.back(); }

        void begin(int index, const std::string& type)
        {
            (void)index;
            mRecords.emplace_back();
            put(static_cast<std::uint32_t>(type.size()));
            mCurrent().mBytes += type;
        }

        void mark(int index, const std::string& name, std::size_t from)
        {
            mCurrent().mFields[std::to_string(index) + ":" + name] = { from, mCurrent().mBytes.size() };
        }

        void i32(std::int32_t value) { put(value); }
        void u32(std::uint32_t value) { put(value); }

        void u16(int index, const std::string& name, std::uint16_t value)
        {
            const std::size_t from = mCurrent().mBytes.size();
            put(value);
            mark(index, name, from);
        }

        void u8(int index, const std::string& name, std::uint8_t value)
        {
            const std::size_t from = mCurrent().mBytes.size();
            put(value);
            mark(index, name, from);
        }

        void f32(int index, const std::string& name, float value)
        {
            const std::size_t from = mCurrent().mBytes.size();
            put(value);
            mark(index, name, from);
        }

        void colour(int index, const std::string& name, const std::array<float, 3>& value)
        {
            const std::size_t from = mCurrent().mBytes.size();
            for (float item : value)
                put(item);
            mark(index, name, from);
        }

        void str(int index, const std::string& name, const std::string& text)
        {
            const std::size_t from = mCurrent().mBytes.size();
            put(static_cast<std::uint32_t>(text.size()));
            mCurrent().mBytes += text;
            mark(index, name, from);
        }

        void list(const std::vector<std::int32_t>& refs)
        {
            put(static_cast<std::uint32_t>(refs.size()));
            for (std::int32_t ref : refs)
                put(ref);
        }

        // type name + name + extra + controller
        void property(int index, const std::string& type, const std::string& name)
        {
            begin(index, type);
            str(index, "name", name);
            i32(-1);
            i32(-1);
        }

        void simpleProperty(int index, const std::string& type, const std::string& name, std::uint16_t flags)
        {
            property(index, type, name);
            u16(index, "flags", flags);
        }

        void avObject(int index, const std::string& type, const std::string& name, const std::array<float, 3>& t,
            float scale, const std::vector<std::int32_t>& properties)
        {
            begin(index, type);
            str(index, "name", name);
            i32(-1); // extra data
            i32(-1); // controller
            u16(index, "flags", 0x000C);
            colour(index, "translation", t);
            const std::size_t from = mCurrent().mBytes.size();
            // a rotation about Z by a quarter turn
            for (float item : std::initializer_list<float>{ 0.f, -1.f, 0.f, 1.f, 0.f, 0.f, 0.f, 0.f, 1.f })
                put(item);
            mark(index, "rotation", from);
            f32(index, "scale", scale);
            for (float item : std::initializer_list<float>{ 0.f, 0.f, 0.f }) // velocity
                put(item);
            list(properties.empty() ? std::vector<std::int32_t>{} : properties);
            i32(0); // no bounding volume
        }

        void triShapeData()
        {
            begin(3, "NiTriShapeData");
            put<std::uint16_t>(4);
            i32(1);
            for (float item : std::initializer_list<float>{ 0, 0, 0, 1, 0, 0, 1, 1, 0, 0, 1, 1 })
                put(static_cast<float>(item));
            i32(1);
            for (float item : std::initializer_list<float>{ 0, 0, 1, 0, 0, 1, 0, 0, 1, 0, 0, 1 })
                put(static_cast<float>(item));
            for (float item : std::initializer_list<float>{ 0.5f, 0.5f, 0.5f, 1.f })
                put(item);
            i32(0);
            put<std::uint16_t>(1);
            i32(1);
            for (float item : std::initializer_list<float>{ 0, 0, 1, 0, 1, 1, 0, 1 })
                put(static_cast<float>(item));
            put<std::uint16_t>(2);
            u32(6);
            for (std::uint16_t item : std::initializer_list<std::uint16_t>{ 0, 1, 2, 0, 2, 3 })
                put(item);
            put<std::uint16_t>(0);
        }

        void extraHeader(int index, const std::string& type, std::uint32_t bytesRemaining)
        {
            begin(index, type);
            i32(-1); // next extra data
            u32(bytesRemaining);
        }

        void textKeys(int index, const std::vector<TextKeyData>& keys)
        {
            const std::size_t from = mCurrent().mBytes.size();
            put(static_cast<std::uint32_t>(keys.size()));
            for (const TextKeyData& key : keys)
            {
                put(key.mTime);
                put(static_cast<std::uint32_t>(key.mText.size()));
                mCurrent().mBytes += key.mText;
            }
            mark(index, "keys", from);
        }

        Built finish(const std::vector<std::int32_t>& roots)
        {
            Built built;
            std::string& out = built.mBytes;
            out = mVersion == 0x04000000 ? "NetImmerse File Format, Version 4.0.0.0\n"
                                         : "NetImmerse File Format, Version 4.0.0.2\n";
            out.append(reinterpret_cast<const char*>(&mVersion), 4);
            const std::uint32_t count = static_cast<std::uint32_t>(mRecords.size());
            out.append(reinterpret_cast<const char*>(&count), 4);
            built.mHeaderEnd = out.size();
            for (std::uint32_t i = 0; i < count; ++i)
            {
                const std::size_t base = out.size();
                for (const auto& [name, range] : mRecords[i].mFields)
                    built.mFields[name] = { base + range.first, base + range.second };
                out += mRecords[i].mBytes;
                built.mRecords.emplace_back(base, out.size());
            }
            built.mFooterBegin = out.size();
            const std::uint32_t rootCount = static_cast<std::uint32_t>(roots.size());
            out.append(reinterpret_cast<const char*>(&rootCount), 4);
            for (std::int32_t root : roots)
                out.append(reinterpret_cast<const char*>(&root), 4);
            return built;
        }

        std::uint32_t mVersion;
        std::vector<Rec> mRecords;
    };
}

#endif
