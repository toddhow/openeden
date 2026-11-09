#include "nifpatch.hpp"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <limits>
#include <set>

namespace NifEdit
{
    namespace
    {
        constexpr std::uint32_t sVersionMw4000 = 0x04000000;
        constexpr std::uint32_t sVersionMw4002 = 0x04000002;
        // A name or text longer than this is refused: the format allows 32 bits, but no real model comes near.
        constexpr std::size_t sMaxStringBytes = 16u * 1024u * 1024u;
        constexpr std::size_t sMaxTextKeys = 1u << 20;

        enum class Kind
        {
            String,
            U16,
            U8,
            Float,
            Float3,
            Float9,
            TextKeys,
        };

        struct Site
        {
            std::uint64_t mBegin = 0;
            std::uint64_t mEnd = 0;
            Kind mKind = Kind::String;
        };

        // Record types that start with the NiAVObject fields (name, extra data, controller, flags, transform).
        const std::set<std::string, std::less<>>& avTypes()
        {
            static const std::set<std::string, std::less<>> types{ "NiNode", "NiTriShape", "NiTriStrips",
                "RootCollisionNode", "NiBillboardNode", "AvoidNode", "NiBSAnimationNode", "NiBSParticleNode" };
            return types;
        }

        // Properties whose first field after the NiObjectNET part is a 16-bit flags word.
        const std::set<std::string, std::less<>>& flagsPropertyTypes()
        {
            static const std::set<std::string, std::less<>> types{ "NiAlphaProperty", "NiVertexColorProperty",
                "NiZBufferProperty", "NiSpecularProperty", "NiWireframeProperty", "NiDitherProperty", "NiShadeProperty",
                "NiMaterialProperty" };
            return types;
        }

        bool versionSupported(std::uint32_t version)
        {
            return version == sVersionMw4000 || version == sVersionMw4002;
        }

        std::string context(const IndexedRecord& record, std::string_view field)
        {
            std::string text = "NIF edit: record " + std::to_string(record.mIndex) + " (" + record.mType + ")";
            if (!field.empty())
                text += ", field '" + std::string(field) + "'";
            return text + ": ";
        }

        [[noreturn]] void fail(const IndexedRecord& record, std::string_view field, const std::string& message)
        {
            throw EditError(context(record, field) + message);
        }

        // Reads the record's own bytes in order, never past the end of its range.
        class Cursor
        {
        public:
            Cursor(std::string_view bytes, const IndexedRecord& record, std::string_view field)
                : mBytes(bytes)
                , mRecord(record)
                , mField(field)
                , mPos(record.mBodyBegin)
            {
            }

            std::uint64_t pos() const { return mPos; }

            void skip(std::uint64_t count)
            {
                need(count);
                mPos += count;
            }

            std::uint8_t u8()
            {
                need(1);
                return static_cast<std::uint8_t>(mBytes[mPos++]);
            }

            std::uint32_t u32()
            {
                need(4);
                std::uint32_t value;
                std::memcpy(&value, mBytes.data() + mPos, sizeof(value));
                mPos += 4;
                return value;
            }

            // A string with its 32-bit length in front; returns where the whole of it (length included) lies.
            Site sizedString()
            {
                Site site;
                site.mKind = Kind::String;
                site.mBegin = mPos;
                const std::uint32_t length = u32();
                skip(length);
                site.mEnd = mPos;
                return site;
            }

            Site fixed(std::uint64_t size, Kind kind)
            {
                Site site;
                site.mKind = kind;
                site.mBegin = mPos;
                skip(size);
                site.mEnd = mPos;
                return site;
            }

        private:
            void need(std::uint64_t count) const
            {
                if (count > mRecord.mEnd - mPos)
                    fail(mRecord, mField,
                        "the record is shorter than its type's layout (needs " + std::to_string(count)
                            + " more bytes at offset " + std::to_string(mPos) + ")");
            }

            std::string_view mBytes;
            const IndexedRecord& mRecord;
            std::string_view mField;
            std::uint64_t mPos;
        };

        std::vector<std::string> fieldsOfType(std::string_view type)
        {
            if (avTypes().contains(type))
                return { "name", "flags", "translation", "rotation", "scale" };
            if (type == "NiMaterialProperty")
                return { "name", "flags", "ambient", "diffuse", "specular", "emissive", "glossiness", "alpha" };
            if (type == "NiAlphaProperty")
                return { "name", "flags", "threshold" };
            if (flagsPropertyTypes().contains(type))
                return { "name", "flags" };
            if (type == "NiSourceTexture")
                return { "name", "file" };
            if (type == "NiStringExtraData")
                return { "string" };
            if (type == "NiTextKeyExtraData")
                return { "keys" };
            return {};
        }

        const IndexedRecord& recordOf(const FileIndex& index, std::uint32_t number)
        {
            if (number >= index.mRecords.size())
                throw EditError("NIF edit: record " + std::to_string(number) + " does not exist (the file has "
                    + std::to_string(index.mRecords.size()) + ")");
            return index.mRecords[number];
        }

        Site locate(std::string_view bytes, const FileIndex& index, const IndexedRecord& record, std::string_view field)
        {
            if (!versionSupported(index.mVersion))
                fail(record, field, "not editable: only Morrowind's 4.0.0.0 and 4.0.0.2 files can be edited");
            const std::vector<std::string> fields = fieldsOfType(record.mType);
            if (fields.empty())
                fail(record, field, "not editable: record type " + record.mType + " is not supported");
            if (std::find(fields.begin(), fields.end(), field) == fields.end())
                fail(record, field, "not editable: " + record.mType + " has no editable field of that name");
            if (record.mEnd > bytes.size())
                fail(record, field, "the record lies outside the file");

            Cursor cursor(bytes, record, field);

            if (record.mType == "NiStringExtraData")
            {
                cursor.skip(8); // next extra data, record size
                return cursor.sizedString();
            }
            if (record.mType == "NiTextKeyExtraData")
            {
                cursor.skip(8);
                Site site;
                site.mKind = Kind::TextKeys;
                site.mBegin = cursor.pos();
                site.mEnd = record.mEnd;
                return site;
            }

            // NiObjectNET: name, extra data reference, controller reference.
            const Site name = cursor.sizedString();
            if (field == "name")
                return name;
            cursor.skip(8);

            if (record.mType == "NiSourceTexture")
            {
                // External flag (one byte, as the reader reads it); only an external texture has a file name.
                if (cursor.u8() == 0)
                    fail(record, field, "not editable: the texture's pixel data is inside the file");
                return cursor.sizedString();
            }

            if (avTypes().contains(record.mType))
            {
                const Site flags = cursor.fixed(2, Kind::U16);
                if (field == "flags")
                    return flags;
                const Site translation = cursor.fixed(12, Kind::Float3);
                if (field == "translation")
                    return translation;
                const Site rotation = cursor.fixed(36, Kind::Float9);
                if (field == "rotation")
                    return rotation;
                return cursor.fixed(4, Kind::Float); // scale
            }

            // A property: the flags word first.
            const Site flags = cursor.fixed(2, Kind::U16);
            if (field == "flags")
                return flags;
            if (record.mType == "NiAlphaProperty")
                return cursor.fixed(1, Kind::U8); // threshold
            // NiMaterialProperty
            for (const char* colour : { "ambient", "diffuse", "specular", "emissive" })
            {
                const Site site = cursor.fixed(12, Kind::Float3);
                if (field == colour)
                    return site;
            }
            const Site glossiness = cursor.fixed(4, Kind::Float);
            if (field == "glossiness")
                return glossiness;
            return cursor.fixed(4, Kind::Float); // alpha
        }

        PatchValue decode(std::string_view bytes, const IndexedRecord& record, std::string_view field, const Site& site)
        {
            const char* data = bytes.data() + site.mBegin;
            const std::uint64_t size = site.mEnd - site.mBegin;
            switch (site.mKind)
            {
                case Kind::String:
                    return std::string(data + 4, size - 4);
                case Kind::U16:
                {
                    std::uint16_t value;
                    std::memcpy(&value, data, sizeof(value));
                    return static_cast<std::int64_t>(value);
                }
                case Kind::U8:
                    return static_cast<std::int64_t>(static_cast<unsigned char>(*data));
                case Kind::Float:
                {
                    float value;
                    std::memcpy(&value, data, sizeof(value));
                    return static_cast<double>(value);
                }
                case Kind::Float3:
                {
                    std::array<float, 3> value;
                    std::memcpy(value.data(), data, sizeof(value));
                    return value;
                }
                case Kind::Float9:
                {
                    std::array<float, 9> value;
                    std::memcpy(value.data(), data, sizeof(value));
                    return value;
                }
                case Kind::TextKeys:
                {
                    std::vector<TextKey> keys;
                    std::uint64_t pos = 0;
                    auto need = [&](std::uint64_t count) {
                        if (count > size - pos)
                            fail(record, field, "the text keys are shorter than their count says");
                    };
                    need(4);
                    std::uint32_t count;
                    std::memcpy(&count, data, sizeof(count));
                    pos = 4;
                    if (count > sMaxTextKeys || static_cast<std::uint64_t>(count) * 8 > size - pos)
                        fail(record, field, "the text key count " + std::to_string(count) + " does not fit the record");
                    keys.reserve(count);
                    for (std::uint32_t i = 0; i < count; ++i)
                    {
                        TextKey key;
                        need(8);
                        std::memcpy(&key.mTime, data + pos, sizeof(float));
                        std::uint32_t length;
                        std::memcpy(&length, data + pos + 4, sizeof(length));
                        pos += 8;
                        need(length);
                        key.mText.assign(data + pos, length);
                        pos += length;
                        keys.push_back(std::move(key));
                    }
                    if (pos != size)
                        fail(record, field,
                            "not editable: " + std::to_string(size - pos)
                                + " bytes follow the text keys that this editor does not know");
                    return keys;
                }
            }
            fail(record, field, "internal error: unknown field kind");
        }

        template <class T>
        void append(std::string& out, const T& value)
        {
            out.append(reinterpret_cast<const char*>(&value), sizeof(T));
        }

        void checkString(const IndexedRecord& record, std::string_view field, const std::string& text)
        {
            if (text.size() > sMaxStringBytes)
                fail(record, field, "the text is longer than " + std::to_string(sMaxStringBytes) + " bytes");
            if (text.find('\0') != std::string::npos)
                fail(record, field, "the text contains a zero byte, which the reader would cut the string at");
        }

        void checkFloat(const IndexedRecord& record, std::string_view field, double value)
        {
            if (!std::isfinite(value) || std::fabs(value) > std::numeric_limits<float>::max())
                fail(record, field, "a number is not finite or does not fit a 32-bit float");
        }

        std::string encode(const IndexedRecord& record, std::string_view field, Kind kind, const PatchValue& value)
        {
            std::string out;
            switch (kind)
            {
                case Kind::String:
                {
                    const auto* text = std::get_if<std::string>(&value);
                    if (text == nullptr)
                        fail(record, field, "the value must be text");
                    checkString(record, field, *text);
                    append(out, static_cast<std::uint32_t>(text->size()));
                    out += *text;
                    return out;
                }
                case Kind::U16:
                case Kind::U8:
                {
                    const auto* number = std::get_if<std::int64_t>(&value);
                    if (number == nullptr)
                        fail(record, field, "the value must be an integer");
                    const std::int64_t limit = kind == Kind::U16 ? 0xFFFF : 0xFF;
                    if (*number < 0 || *number > limit)
                        fail(record, field,
                            "the integer " + std::to_string(*number) + " is outside 0.." + std::to_string(limit));
                    if (kind == Kind::U16)
                        append(out, static_cast<std::uint16_t>(*number));
                    else
                        append(out, static_cast<std::uint8_t>(*number));
                    return out;
                }
                case Kind::Float:
                {
                    const auto* number = std::get_if<double>(&value);
                    if (number == nullptr)
                        fail(record, field, "the value must be a number");
                    checkFloat(record, field, *number);
                    append(out, static_cast<float>(*number));
                    return out;
                }
                case Kind::Float3:
                {
                    const auto* vector = std::get_if<std::array<float, 3>>(&value);
                    if (vector == nullptr)
                        fail(record, field, "the value must be three numbers");
                    for (float item : *vector)
                        checkFloat(record, field, item);
                    out.append(reinterpret_cast<const char*>(vector->data()), sizeof(*vector));
                    return out;
                }
                case Kind::Float9:
                {
                    const auto* matrix = std::get_if<std::array<float, 9>>(&value);
                    if (matrix == nullptr)
                        fail(record, field, "the value must be nine numbers");
                    for (float item : *matrix)
                        checkFloat(record, field, item);
                    out.append(reinterpret_cast<const char*>(matrix->data()), sizeof(*matrix));
                    return out;
                }
                case Kind::TextKeys:
                {
                    const auto* keys = std::get_if<std::vector<TextKey>>(&value);
                    if (keys == nullptr)
                        fail(record, field, "the value must be a list of text keys");
                    if (keys->size() > sMaxTextKeys)
                        fail(record, field, "too many text keys");
                    append(out, static_cast<std::uint32_t>(keys->size()));
                    for (const TextKey& key : *keys)
                    {
                        checkFloat(record, field, key.mTime);
                        checkString(record, field, key.mText);
                        append(out, key.mTime);
                        append(out, static_cast<std::uint32_t>(key.mText.size()));
                        out += key.mText;
                    }
                    return out;
                }
            }
            fail(record, field, "internal error: unknown field kind");
        }

        struct Splice
        {
            std::uint64_t mBegin;
            std::uint64_t mEnd;
            std::string mBytes;
        };
    }

    void PatchSet::set(FieldPatch patch)
    {
        for (FieldPatch& existing : mPatches)
        {
            if (existing.mRecord == patch.mRecord && existing.mField == patch.mField)
            {
                existing.mValue = std::move(patch.mValue);
                return;
            }
        }
        mPatches.push_back(std::move(patch));
    }

    bool PatchSet::erase(std::uint32_t record, std::string_view field)
    {
        const auto it = std::find_if(mPatches.begin(), mPatches.end(),
            [&](const FieldPatch& patch) { return patch.mRecord == record && patch.mField == field; });
        if (it == mPatches.end())
            return false;
        mPatches.erase(it);
        return true;
    }

    const FieldPatch* PatchSet::find(std::uint32_t record, std::string_view field) const
    {
        const auto it = std::find_if(mPatches.begin(), mPatches.end(),
            [&](const FieldPatch& patch) { return patch.mRecord == record && patch.mField == field; });
        return it == mPatches.end() ? nullptr : &*it;
    }

    std::vector<std::string> editableFields(std::string_view type, std::uint32_t version)
    {
        if (!versionSupported(version))
            return {};
        return fieldsOfType(type);
    }

    PatchValue readField(std::string_view bytes, const FileIndex& index, std::uint32_t record, std::string_view field)
    {
        const IndexedRecord& entry = recordOf(index, record);
        const Site site = locate(bytes, index, entry, field);
        return decode(bytes, entry, field, site);
    }

    void checkPatch(std::string_view bytes, const FileIndex& index, const FieldPatch& patch)
    {
        const IndexedRecord& entry = recordOf(index, patch.mRecord);
        const Site site = locate(bytes, index, entry, patch.mField);
        (void)encode(entry, patch.mField, site.mKind, patch.mValue);
    }

    std::string applyPatches(std::string_view bytes, const FileIndex& index, const PatchSet& patches)
    {
        if (bytes.size() != index.mSize)
            throw EditError("NIF edit: the bytes are not the file the index was made from");
        std::vector<Splice> splices;
        for (const FieldPatch& patch : patches.patches())
        {
            const IndexedRecord& entry = recordOf(index, patch.mRecord);
            const Site site = locate(bytes, index, entry, patch.mField);
            std::string encoded = encode(entry, patch.mField, site.mKind, patch.mValue);
            // A string extra data record keeps "bytes remaining" (the size of what follows it) in front of the string.
            // Every record of the 7,343 Morrowind meshes checked has it exact, so it follows the string; a record
            // where it is anything else keeps its value, so that a file is never made to disagree with itself.
            if (entry.mType == "NiStringExtraData")
            {
                std::uint32_t remaining;
                std::memcpy(&remaining, bytes.data() + entry.mBodyBegin + 4, sizeof(remaining));
                if (remaining == site.mEnd - site.mBegin)
                {
                    std::string size;
                    append(size, static_cast<std::uint32_t>(encoded.size()));
                    splices.push_back({ entry.mBodyBegin + 4, entry.mBodyBegin + 8, std::move(size) });
                }
            }
            splices.push_back({ site.mBegin, site.mEnd, std::move(encoded) });
        }
        std::sort(splices.begin(), splices.end(), [](const Splice& a, const Splice& b) { return a.mBegin < b.mBegin; });
        for (std::size_t i = 1; i < splices.size(); ++i)
        {
            if (splices[i].mBegin < splices[i - 1].mEnd)
                throw EditError("NIF edit: two patches write the same bytes");
        }

        std::string out;
        out.reserve(bytes.size());
        std::uint64_t pos = 0;
        for (const Splice& splice : splices)
        {
            out.append(bytes.substr(pos, splice.mBegin - pos));
            out += splice.mBytes;
            pos = splice.mEnd;
        }
        out.append(bytes.substr(pos));
        return out;
    }
}
