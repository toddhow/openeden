#include "probes.hpp"

#include "reflectionprobes.hpp"

#include <algorithm>
#include <charconv>
#include <cmath>
#include <initializer_list>
#include <limits>
#include <memory>
#include <new>

namespace Render::Gi
{
    namespace
    {
        constexpr const char* formatName = "openeden.probes";
        constexpr long long formatVersion = 1;
        constexpr float maxSpacing = 100000.f;
        constexpr std::size_t probeNumbers = 27;
        constexpr int maxJsonDepth = 16;
        // A hostile file must not make the reader build a huge tree: the longest legitimate array is a volume's probes.
        constexpr std::size_t maxArrayItems = maxVolumeProbes;
        constexpr std::size_t maxObjectMembers = 16;
        constexpr std::size_t maxFileBytes = 256u * 1024u * 1024u;

        // A problem found while reading; turned into a ProbeError at the top.
        struct Failure
        {
            int line;
            std::string field;
            std::string message;
        };

        // A minimal JSON tree that remembers the line of every value, so errors can say where they are.
        struct Value
        {
            enum class Type
            {
                Null,
                Bool,
                Number,
                String,
                Array,
                Object
            };
            Type type = Type::Null;
            int line = 1;
            // The literal of a number, or the decoded text of a string.
            std::string text;
            std::vector<Value> items;
            // Members of an object, in file order, with the line of each key.
            std::vector<std::pair<std::string, Value>> members;
            std::vector<int> keyLines;
        };

        class Reader
        {
        public:
            explicit Reader(std::string_view text)
                : mText(text)
            {
            }

            Value readDocument()
            {
                Value v = readValue(0);
                skipSpace();
                if (mPos != mText.size())
                    fail("unexpected text after the end of the document");
                return v;
            }

        private:
            [[noreturn]] void fail(const std::string& message)
            {
                throw Failure{ mLine, "", "invalid JSON: " + message };
            }

            void skipSpace()
            {
                while (mPos < mText.size())
                {
                    const char c = mText[mPos];
                    if (c == '\n')
                        ++mLine;
                    else if (c != ' ' && c != '\t' && c != '\r')
                        break;
                    ++mPos;
                }
            }

            bool consume(char c)
            {
                skipSpace();
                if (mPos < mText.size() && mText[mPos] == c)
                {
                    ++mPos;
                    return true;
                }
                return false;
            }

            Value readValue(int depth)
            {
                if (depth > maxJsonDepth)
                    fail("nested too deeply");
                skipSpace();
                if (mPos >= mText.size())
                    fail("unexpected end of the file");
                Value v;
                v.line = mLine;
                const char c = mText[mPos];
                if (c == '{')
                {
                    ++mPos;
                    v.type = Value::Type::Object;
                    if (consume('}'))
                        return v;
                    do
                    {
                        skipSpace();
                        const int keyLine = mLine;
                        if (mPos >= mText.size() || mText[mPos] != '"')
                            fail("expected a string key");
                        std::string key = readString();
                        if (!consume(':'))
                            fail("expected ':' after a key");
                        v.keyLines.push_back(keyLine);
                        if (v.members.size() >= maxObjectMembers)
                            fail("too many keys in an object");
                        v.members.emplace_back(std::move(key), readValue(depth + 1));
                    } while (consume(','));
                    if (!consume('}'))
                        fail("expected ',' or '}'");
                }
                else if (c == '[')
                {
                    ++mPos;
                    v.type = Value::Type::Array;
                    if (consume(']'))
                        return v;
                    do
                    {
                        if (v.items.size() >= maxArrayItems)
                            fail("too many items in an array");
                        v.items.push_back(readValue(depth + 1));
                    } while (consume(','));
                    if (!consume(']'))
                        fail("expected ',' or ']'");
                }
                else if (c == '"')
                {
                    v.type = Value::Type::String;
                    v.text = readString();
                }
                else if (c == '-' || (c >= '0' && c <= '9'))
                {
                    v.type = Value::Type::Number;
                    v.text = readNumber();
                }
                else if (matchWord("true") || matchWord("false"))
                    v.type = Value::Type::Bool;
                else if (matchWord("null"))
                    v.type = Value::Type::Null;
                else
                    fail("unexpected character");
                return v;
            }

            bool matchWord(std::string_view word)
            {
                if (mText.substr(mPos, word.size()) == word)
                {
                    mPos += word.size();
                    return true;
                }
                return false;
            }

            std::string readNumber()
            {
                const std::size_t start = mPos;
                if (mText[mPos] == '-')
                    ++mPos;
                const auto digits = [this] {
                    std::size_t n = 0;
                    while (mPos < mText.size() && mText[mPos] >= '0' && mText[mPos] <= '9')
                    {
                        ++mPos;
                        ++n;
                    }
                    return n;
                };
                if (digits() == 0)
                    fail("a number needs digits");
                {
                    // JSON has no leading zeros: "007" is an error, "0" and "0.5" are fine.
                    const std::size_t first = start + (mText[start] == '-' ? 1 : 0);
                    if (mText[first] == '0' && mPos - first > 1)
                        fail("a number has a leading zero");
                }

                if (mPos < mText.size() && mText[mPos] == '.')
                {
                    ++mPos;
                    if (digits() == 0)
                        fail("a number needs digits after '.'");
                }
                if (mPos < mText.size() && (mText[mPos] == 'e' || mText[mPos] == 'E'))
                {
                    ++mPos;
                    if (mPos < mText.size() && (mText[mPos] == '+' || mText[mPos] == '-'))
                        ++mPos;
                    if (digits() == 0)
                        fail("a number needs digits in its exponent");
                }
                return std::string(mText.substr(start, mPos - start));
            }

            static void appendUtf8(std::string& out, unsigned cp)
            {
                if (cp < 0x80)
                    out += static_cast<char>(cp);
                else if (cp < 0x800)
                {
                    out += static_cast<char>(0xC0 | (cp >> 6));
                    out += static_cast<char>(0x80 | (cp & 0x3F));
                }
                else if (cp < 0x10000)
                {
                    out += static_cast<char>(0xE0 | (cp >> 12));
                    out += static_cast<char>(0x80 | ((cp >> 6) & 0x3F));
                    out += static_cast<char>(0x80 | (cp & 0x3F));
                }
                else
                {
                    out += static_cast<char>(0xF0 | (cp >> 18));
                    out += static_cast<char>(0x80 | ((cp >> 12) & 0x3F));
                    out += static_cast<char>(0x80 | ((cp >> 6) & 0x3F));
                    out += static_cast<char>(0x80 | (cp & 0x3F));
                }
            }

            unsigned readHex4()
            {
                if (mPos + 4 > mText.size())
                    fail("a \\u escape needs four hex digits");
                unsigned cp = 0;
                for (int i = 0; i < 4; ++i)
                {
                    const char c = mText[mPos++];
                    cp <<= 4;
                    if (c >= '0' && c <= '9')
                        cp |= static_cast<unsigned>(c - '0');
                    else if (c >= 'a' && c <= 'f')
                        cp |= static_cast<unsigned>(c - 'a' + 10);
                    else if (c >= 'A' && c <= 'F')
                        cp |= static_cast<unsigned>(c - 'A' + 10);
                    else
                        fail("a \\u escape needs four hex digits");
                }
                return cp;
            }

            std::string readString()
            {
                ++mPos; // the opening quote
                std::string out;
                while (true)
                {
                    if (mPos >= mText.size())
                        fail("a string is not closed");
                    const char c = mText[mPos++];
                    if (c == '"')
                        return out;
                    if (static_cast<unsigned char>(c) < 0x20)
                        fail("a control character in a string");
                    if (c != '\\')
                    {
                        out += c;
                        continue;
                    }
                    if (mPos >= mText.size())
                        fail("a string is not closed");
                    const char e = mText[mPos++];
                    switch (e)
                    {
                        case '"':
                            out += '"';
                            break;
                        case '\\':
                            out += '\\';
                            break;
                        case '/':
                            out += '/';
                            break;
                        case 'b':
                            out += '\b';
                            break;
                        case 'f':
                            out += '\f';
                            break;
                        case 'n':
                            out += '\n';
                            break;
                        case 'r':
                            out += '\r';
                            break;
                        case 't':
                            out += '\t';
                            break;
                        case 'u':
                        {
                            unsigned cp = readHex4();
                            if (cp >= 0xD800 && cp < 0xDC00 && mText.substr(mPos, 2) == "\\u")
                            {
                                mPos += 2;
                                const unsigned low = readHex4();
                                if (low >= 0xDC00 && low < 0xE000)
                                    cp = 0x10000 + ((cp - 0xD800) << 10) + (low - 0xDC00);
                                else
                                    fail("a broken surrogate pair");
                            }
                            appendUtf8(out, cp);
                            break;
                        }
                        default:
                            fail("an unknown escape in a string");
                    }
                }
            }

            std::string_view mText;
            std::size_t mPos = 0;
            int mLine = 1;
        };

        // ---- schema ----

        const Value* find(const Value& object, const char* key)
        {
            for (const auto& [name, value] : object.members)
                if (name == key)
                    return &value;
            return nullptr;
        }

        // The object must hold exactly `keys`, each once.
        void checkKeys(const Value& object, const std::string& path, std::initializer_list<const char*> keys)
        {
            const auto describe = [&](const std::string& key) { return path.empty() ? key : path + "." + key; };
            for (std::size_t i = 0; i < object.members.size(); ++i)
            {
                const std::string& name = object.members[i].first;
                const bool known = std::any_of(keys.begin(), keys.end(), [&](const char* k) { return name == k; });
                if (!known)
                    throw Failure{ object.keyLines[i], describe(name), "unknown key '" + name + "'" };
                for (std::size_t j = 0; j < i; ++j)
                    if (object.members[j].first == name)
                        throw Failure{ object.keyLines[i], describe(name), "key '" + name + "' appears twice" };
            }
            for (const char* key : keys)
                if (find(object, key) == nullptr)
                    throw Failure{ object.line, describe(key), std::string("missing key '") + key + "'" };
        }

        void expectObject(const Value& v, const std::string& field)
        {
            if (v.type != Value::Type::Object)
                throw Failure{ v.line, field, "expected an object" };
        }

        void expectArray(const Value& v, const std::string& field)
        {
            if (v.type != Value::Type::Array)
                throw Failure{ v.line, field, "expected an array" };
        }

        float readFloat(const Value& v, const std::string& field)
        {
            if (v.type != Value::Type::Number)
                throw Failure{ v.line, field, "expected a number" };
            float out = 0.f;
            const auto [ptr, ec] = std::from_chars(v.text.data(), v.text.data() + v.text.size(), out);
            if (ec == std::errc::result_out_of_range || !std::isfinite(out))
                throw Failure{ v.line, field, "the number is not a finite 32-bit float (too large or too small)" };
            if (ec != std::errc() || ptr != v.text.data() + v.text.size())
                throw Failure{ v.line, field, "cannot read the number" };
            return out;
        }

        long long readInteger(const Value& v, const std::string& field, long long low, long long high)
        {
            if (v.type != Value::Type::Number)
                throw Failure{ v.line, field, "expected an integer" };
            if (v.text.find_first_of(".eE") != std::string::npos)
                throw Failure{ v.line, field, "expected an integer, not a fraction or an exponent" };
            long long out = 0;
            const auto [ptr, ec] = std::from_chars(v.text.data(), v.text.data() + v.text.size(), out);
            if (ec != std::errc() || ptr != v.text.data() + v.text.size() || out < low || out > high)
                throw Failure{ v.line, field,
                    "expected an integer from " + std::to_string(low) + " to " + std::to_string(high) };
            return out;
        }

        vsg::vec3 readVec3(const Value& v, const std::string& field)
        {
            expectArray(v, field);
            if (v.items.size() != 3)
                throw Failure{ v.line, field, "expected three numbers" };
            return vsg::vec3(readFloat(v.items[0], field + "[0]"), readFloat(v.items[1], field + "[1]"),
                readFloat(v.items[2], field + "[2]"));
        }

        Volume readVolume(const Value& v, std::size_t index)
        {
            const std::string path = "volumes[" + std::to_string(index) + "]";
            expectObject(v, path);
            checkKeys(v, path, { "origin", "spacing", "size", "probes" });
            Volume volume;
            volume.origin = readVec3(*find(v, "origin"), path + ".origin");
            volume.spacing = readFloat(*find(v, "spacing"), path + ".spacing");
            if (!(volume.spacing > 0.f) || volume.spacing > maxSpacing)
                throw Failure{ find(v, "spacing")->line, path + ".spacing",
                    "the spacing must be above 0 and at most 100000" };
            const Value& size = *find(v, "size");
            expectArray(size, path + ".size");
            if (size.items.size() != 3)
                throw Failure{ size.line, path + ".size", "expected three integers" };
            std::size_t product = 1;
            for (int a = 0; a < 3; ++a)
            {
                const long long n = readInteger(size.items[a], path + ".size[" + std::to_string(a) + "]", 1,
                    static_cast<long long>(maxVolumeProbes));
                volume.size[a] = static_cast<std::uint32_t>(n);
                product *= static_cast<std::size_t>(n);
                if (product > maxVolumeProbes)
                    throw Failure{ size.line, path + ".size",
                        "a volume holds at most " + std::to_string(maxVolumeProbes) + " probes" };
            }
            const Value& probes = *find(v, "probes");
            expectArray(probes, path + ".probes");
            if (probes.items.size() != product)
                throw Failure{ probes.line, path + ".probes",
                    "expected " + std::to_string(product) + " probes (the product of size), found "
                        + std::to_string(probes.items.size()) };
            volume.probes.resize(product);
            for (std::size_t i = 0; i < product; ++i)
            {
                const std::string probePath = path + ".probes[" + std::to_string(i) + "]";
                const Value& probe = probes.items[i];
                expectArray(probe, probePath);
                if (probe.items.size() != probeNumbers)
                    throw Failure{ probe.line, probePath,
                        "expected 27 numbers (nine coefficients of r, g, b), found "
                            + std::to_string(probe.items.size()) };
                for (std::size_t k = 0; k < probeNumbers; ++k)
                {
                    const float f = readFloat(probe.items[k], probePath + "[" + std::to_string(k) + "]");
                    volume.probes[i].c[k / 3][k % 3] = f;
                }
            }
            return volume;
        }

        ProbeSet readSet(const Value& root)
        {
            expectObject(root, "");
            // The format and version are checked before the other keys, so a file of another kind says so.
            if (const Value* format = find(root, "format"))
            {
                if (format->type != Value::Type::String || format->text != formatName)
                    throw Failure{ format->line, "format", std::string("expected \"") + formatName + "\"" };
            }
            if (const Value* version = find(root, "version"))
            {
                if (version->type != Value::Type::Number || version->text != std::to_string(formatVersion))
                    throw Failure{ version->line, "version", "unsupported version, expected 1" };
            }
            checkKeys(root, "", { "format", "version", "cell", "bake", "volumes" });

            ProbeSet set;
            const Value& cell = *find(root, "cell");
            expectObject(cell, "cell");
            if (cell.members.size() != 1)
                throw Failure{ cell.line, "cell", "expected exactly one of \"interior\" or \"exterior\"" };
            if (const Value* interior = find(cell, "interior"))
            {
                if (interior->type != Value::Type::String)
                    throw Failure{ interior->line, "cell.interior", "expected a string" };
                set.cell.interior = true;
                set.cell.name = interior->text;
            }
            else if (const Value* exterior = find(cell, "exterior"))
            {
                expectArray(*exterior, "cell.exterior");
                if (exterior->items.size() != 2)
                    throw Failure{ exterior->line, "cell.exterior", "expected two integers" };
                constexpr long long low = std::numeric_limits<std::int32_t>::min();
                constexpr long long high = std::numeric_limits<std::int32_t>::max();
                set.cell.interior = false;
                set.cell.x = static_cast<int>(readInteger(exterior->items[0], "cell.exterior[0]", low, high));
                set.cell.y = static_cast<int>(readInteger(exterior->items[1], "cell.exterior[1]", low, high));
            }
            else
                throw Failure{ cell.keyLines[0], "cell", "expected \"interior\" or \"exterior\"" };

            const Value& bake = *find(root, "bake");
            expectObject(bake, "bake");
            checkKeys(bake, "bake", { "rays", "bounces", "seed", "ambient" });
            set.rays = static_cast<std::uint32_t>(readInteger(*find(bake, "rays"), "bake.rays", 1, 65536));
            set.bounces = static_cast<std::uint32_t>(readInteger(*find(bake, "bounces"), "bake.bounces", 0, 8));
            set.seed = static_cast<std::uint32_t>(
                readInteger(*find(bake, "seed"), "bake.seed", 0, std::numeric_limits<std::uint32_t>::max()));
            set.ambient = readVec3(*find(bake, "ambient"), "bake.ambient");

            const Value& volumes = *find(root, "volumes");
            expectArray(volumes, "volumes");
            if (volumes.items.empty() || volumes.items.size() > maxVolumes)
                throw Failure{ volumes.line, "volumes", "expected 1 to 64 volumes" };
            for (std::size_t i = 0; i < volumes.items.size(); ++i)
                set.volumes.push_back(readVolume(volumes.items[i], i));
            return set;
        }

        // ---- writer ----

        void appendFloat(std::string& out, float f)
        {
            char buffer[64];
            const auto result = std::to_chars(buffer, buffer + sizeof(buffer), f);
            out.append(buffer, result.ptr);
        }

        void appendVec3(std::string& out, const vsg::vec3& v)
        {
            out += '[';
            appendFloat(out, v.x);
            out += ", ";
            appendFloat(out, v.y);
            out += ", ";
            appendFloat(out, v.z);
            out += ']';
        }

        void appendString(std::string& out, std::string_view s)
        {
            out += '"';
            for (const char c : s)
            {
                if (c == '"')
                    out += "\\\"";
                else if (c == '\\')
                    out += "\\\\";
                else if (static_cast<unsigned char>(c) < 0x20)
                {
                    constexpr const char* hex = "0123456789abcdef";
                    out += "\\u00";
                    out += hex[(static_cast<unsigned char>(c) >> 4) & 0xF];
                    out += hex[static_cast<unsigned char>(c) & 0xF];
                }
                else
                    out += c;
            }
            out += '"';
        }

        char lowerAscii(char c)
        {
            return (c >= 'A' && c <= 'Z') ? static_cast<char>(c - 'A' + 'a') : c;
        }
    }

    std::string ProbeError::describe() const
    {
        return file + ":" + std::to_string(line) + ": " + field + ": " + message;
    }

    std::optional<ProbeSet> parseProbes(std::string_view json, std::string_view fileName, ProbeError& error)
    {
        if (json.size() > maxFileBytes)
        {
            error = ProbeError{ std::string(fileName), 1, "", "the file is too large" };
            return std::nullopt;
        }
        try
        {
            Reader reader(json);
            const Value root = reader.readDocument();
            return readSet(root);
        }
        catch (const Failure& f)
        {
            error.file = std::string(fileName);
            error.line = f.line;
            error.field = f.field;
            error.message = f.message;
            return std::nullopt;
        }
        catch (const std::bad_alloc&)
        {
            error = ProbeError{ std::string(fileName), 1, "", "out of memory while reading the file" };
            return std::nullopt;
        }
    }

    std::string validate(const ProbeSet& set)
    {
        if (set.rays < 1 || set.rays > 65536)
            return "bake.rays must be 1 to 65536";
        if (set.bounces > 8)
            return "bake.bounces must be 0 to 8";
        const auto finite3
            = [](const vsg::vec3& v) { return std::isfinite(v.x) && std::isfinite(v.y) && std::isfinite(v.z); };
        if (!finite3(set.ambient))
            return "bake.ambient must be finite";
        if (set.volumes.empty() || set.volumes.size() > maxVolumes)
            return "a probe set holds 1 to 64 volumes";
        for (std::size_t i = 0; i < set.volumes.size(); ++i)
        {
            const Volume& v = set.volumes[i];
            const std::string path = "volumes[" + std::to_string(i) + "]";
            if (!finite3(v.origin))
                return path + ".origin must be finite";
            if (!(v.spacing > 0.f) || !std::isfinite(v.spacing) || v.spacing > maxSpacing)
                return path + ".spacing must be above 0 and at most 100000";
            std::size_t product = 1;
            for (const std::uint32_t n : v.size)
            {
                if (n < 1)
                    return path + ".size must be at least 1 on every axis";
                product *= n;
                if (product > maxVolumeProbes)
                    return path + " holds more than the maximum number of probes";
            }
            if (v.probes.size() != product)
                return path + ".probes does not match size";
            for (std::size_t p = 0; p < v.probes.size(); ++p)
                for (const vsg::vec3& c : v.probes[p].c)
                    if (!finite3(c))
                        return path + ".probes[" + std::to_string(p) + "] must be finite";
        }
        return {};
    }

    std::string writeCanonical(const ProbeSet& set)
    {
        std::string out;
        out += "{\n  \"format\": \"openeden.probes\",\n  \"version\": 1,\n  \"cell\": {\n    ";
        if (set.cell.interior)
        {
            out += "\"interior\": ";
            appendString(out, set.cell.name);
        }
        else
            out += "\"exterior\": [" + std::to_string(set.cell.x) + ", " + std::to_string(set.cell.y) + "]";
        out += "\n  },\n  \"bake\": {\n";
        out += "    \"rays\": " + std::to_string(set.rays) + ",\n";
        out += "    \"bounces\": " + std::to_string(set.bounces) + ",\n";
        out += "    \"seed\": " + std::to_string(set.seed) + ",\n";
        out += "    \"ambient\": ";
        appendVec3(out, set.ambient);
        out += "\n  },\n  \"volumes\": [\n";
        for (std::size_t i = 0; i < set.volumes.size(); ++i)
        {
            const Volume& v = set.volumes[i];
            out += "    {\n      \"origin\": ";
            appendVec3(out, v.origin);
            out += ",\n      \"spacing\": ";
            appendFloat(out, v.spacing);
            out += ",\n      \"size\": [" + std::to_string(v.size[0]) + ", " + std::to_string(v.size[1]) + ", "
                + std::to_string(v.size[2]) + "],\n      \"probes\": [\n";
            for (std::size_t p = 0; p < v.probes.size(); ++p)
            {
                out += "        [";
                for (std::size_t k = 0; k < probeNumbers; ++k)
                {
                    if (k != 0)
                        out += ", ";
                    appendFloat(out, v.probes[p].c[k / 3][k % 3]);
                }
                out += p + 1 < v.probes.size() ? "],\n" : "]\n";
            }
            out += "      ]\n    }";
            out += i + 1 < set.volumes.size() ? ",\n" : "\n";
        }
        out += "  ]\n}\n";
        return out;
    }

    std::string cellKey(const CellId& cell)
    {
        if (!cell.interior)
            return "exterior/" + std::to_string(cell.x) + "_" + std::to_string(cell.y);
        std::string lower;
        lower.reserve(cell.name.size());
        for (const char c : cell.name)
            lower += lowerAscii(c);
        // FNV-1a, 32 bit, of the lower-cased name as it is written (before it is sanitised).
        std::uint32_t hash = 2166136261u;
        for (const char c : lower)
        {
            hash ^= static_cast<std::uint8_t>(c);
            hash *= 16777619u;
        }
        std::string safe;
        for (const char c : lower)
        {
            const bool keep = (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9');
            if (keep)
                safe += c;
            else if (safe.empty() || safe.back() != '_')
                safe += '_';
        }
        while (!safe.empty() && safe.back() == '_')
            safe.pop_back();
        std::size_t start = 0;
        while (start < safe.size() && safe[start] == '_')
            ++start;
        safe.erase(0, start);
        if (safe.empty())
            safe = "cell";
        if (safe.size() > 48)
            safe.resize(48);
        constexpr const char* hex = "0123456789abcdef";
        std::string suffix = "-";
        for (int shift = 28; shift >= 0; shift -= 4)
            suffix += hex[(hash >> shift) & 0xF];
        return "interior/" + safe + suffix;
    }

    std::string probeFilePath(const CellId& cell)
    {
        return "openeden/probes/" + cellKey(cell) + ".json";
    }

    // ---- Reflection probes (stage d): the `openeden.reflections` file, beside the light probe file ----

    namespace
    {
        constexpr const char* reflectionFormatName = "openeden.reflections";
        constexpr long long maxReflectionSamples = 256;

        void readCell(const Value& cell, CellId& out)
        {
            expectObject(cell, "cell");
            if (cell.members.size() != 1)
                throw Failure{ cell.line, "cell", "expected exactly one of \"interior\" or \"exterior\"" };
            if (const Value* interior = find(cell, "interior"))
            {
                if (interior->type != Value::Type::String)
                    throw Failure{ interior->line, "cell.interior", "expected a string" };
                out.interior = true;
                out.name = interior->text;
            }
            else if (const Value* exterior = find(cell, "exterior"))
            {
                expectArray(*exterior, "cell.exterior");
                if (exterior->items.size() != 2)
                    throw Failure{ exterior->line, "cell.exterior", "expected two integers" };
                constexpr long long low = std::numeric_limits<std::int32_t>::min();
                constexpr long long high = std::numeric_limits<std::int32_t>::max();
                out.interior = false;
                out.x = static_cast<int>(readInteger(exterior->items[0], "cell.exterior[0]", low, high));
                out.y = static_cast<int>(readInteger(exterior->items[1], "cell.exterior[1]", low, high));
            }
            else
                throw Failure{ cell.keyLines[0], "cell", "expected \"interior\" or \"exterior\"" };
        }

        ReflectionSet readReflectionSet(const Value& root)
        {
            expectObject(root, "");
            if (const Value* format = find(root, "format"))
            {
                if (format->type != Value::Type::String || format->text != reflectionFormatName)
                    throw Failure{ format->line, "format", std::string("expected \"") + reflectionFormatName + "\"" };
            }
            if (const Value* version = find(root, "version"))
            {
                if (version->type != Value::Type::Number || version->text != std::to_string(formatVersion))
                    throw Failure{ version->line, "version", "unsupported version, expected 1" };
            }
            checkKeys(root, "", { "format", "version", "cell", "bake", "resolution", "probes" });

            ReflectionSet set;
            readCell(*find(root, "cell"), set.cell);

            const Value& bake = *find(root, "bake");
            expectObject(bake, "bake");
            checkKeys(bake, "bake", { "samples", "bounces", "seed", "ambient" });
            set.samples = static_cast<std::uint32_t>(
                readInteger(*find(bake, "samples"), "bake.samples", 1, maxReflectionSamples));
            set.bounces = static_cast<std::uint32_t>(readInteger(*find(bake, "bounces"), "bake.bounces", 0, 8));
            set.seed = static_cast<std::uint32_t>(
                readInteger(*find(bake, "seed"), "bake.seed", 0, std::numeric_limits<std::uint32_t>::max()));
            set.ambient = readVec3(*find(bake, "ambient"), "bake.ambient");

            const Value& resolution = *find(root, "resolution");
            set.resolution = static_cast<std::uint32_t>(
                readInteger(resolution, "resolution", 1, std::numeric_limits<std::int32_t>::max()));
            if (!validReflectionResolution(set.resolution))
                throw Failure{ resolution.line, "resolution", "the resolution must be 8, 16, 32 or 64" };

            const Value& probes = *find(root, "probes");
            expectArray(probes, "probes");
            if (probes.items.empty() || probes.items.size() > maxReflectionProbes)
                throw Failure{ probes.line, "probes", "expected 1 to 16 probes" };
            for (std::size_t p = 0; p < probes.items.size(); ++p)
            {
                const std::string path = "probes[" + std::to_string(p) + "]";
                const Value& probe = probes.items[p];
                expectObject(probe, path);
                checkKeys(probe, path, { "position", "texels" });
                ReflectionProbe out;
                out.position = readVec3(*find(probe, "position"), path + ".position");
                const Value& rows = *find(probe, "texels");
                expectArray(rows, path + ".texels");
                if (rows.items.size() != set.resolution)
                    throw Failure{ rows.line, path + ".texels",
                        "expected " + std::to_string(set.resolution) + " rows, found "
                            + std::to_string(rows.items.size()) };
                out.texels.reserve(static_cast<std::size_t>(set.resolution) * set.resolution);
                for (std::size_t r = 0; r < rows.items.size(); ++r)
                {
                    const std::string rowPath = path + ".texels[" + std::to_string(r) + "]";
                    const Value& row = rows.items[r];
                    expectArray(row, rowPath);
                    if (row.items.size() != 3u * set.resolution)
                        throw Failure{ row.line, rowPath,
                            "expected " + std::to_string(3u * set.resolution) + " numbers (r, g, b for each of "
                                + std::to_string(set.resolution) + " texels), found "
                                + std::to_string(row.items.size()) };
                    for (std::size_t t = 0; t < set.resolution; ++t)
                    {
                        const auto number = [&](std::size_t k) {
                            return readFloat(row.items[3 * t + k], rowPath + "[" + std::to_string(3 * t + k) + "]");
                        };
                        const float cr = number(0);
                        const float cg = number(1);
                        const float cb = number(2);
                        out.texels.emplace_back(cr, cg, cb);
                    }
                }
                set.probes.push_back(std::move(out));
            }
            return set;
        }
    }

    std::optional<ReflectionSet> parseReflections(std::string_view json, std::string_view fileName, ProbeError& error)
    {
        // 16 probes of 64 by 64 texels come to about 3 MB of text; anything far above is not a reflection file.
        constexpr std::size_t maxReflectionFileBytes = 16u * 1024u * 1024u;
        if (json.size() > maxReflectionFileBytes)
        {
            error = ProbeError{ std::string(fileName), 1, "", "the file is too large" };
            return std::nullopt;
        }
        try
        {
            Reader reader(json);
            const Value root = reader.readDocument();
            return readReflectionSet(root);
        }
        catch (const Failure& f)
        {
            error.file = std::string(fileName);
            error.line = f.line;
            error.field = f.field;
            error.message = f.message;
            return std::nullopt;
        }
        catch (const std::bad_alloc&)
        {
            error = ProbeError{ std::string(fileName), 1, "", "out of memory while reading the file" };
            return std::nullopt;
        }
    }

    std::string validate(const ReflectionSet& set)
    {
        const auto finite3
            = [](const vsg::vec3& v) { return std::isfinite(v.x) && std::isfinite(v.y) && std::isfinite(v.z); };
        if (set.samples < 1 || set.samples > maxReflectionSamples)
            return "bake.samples must be 1 to 256";
        if (set.bounces > 8)
            return "bake.bounces must be 0 to 8";
        if (!finite3(set.ambient))
            return "bake.ambient must be finite";
        if (!validReflectionResolution(set.resolution))
            return "resolution must be 8, 16, 32 or 64";
        if (set.probes.empty() || set.probes.size() > maxReflectionProbes)
            return "a reflection set holds 1 to 16 probes";
        const std::size_t texels = static_cast<std::size_t>(set.resolution) * set.resolution;
        for (std::size_t p = 0; p < set.probes.size(); ++p)
        {
            const ReflectionProbe& probe = set.probes[p];
            const std::string path = "probes[" + std::to_string(p) + "]";
            if (!finite3(probe.position))
                return path + ".position must be finite";
            if (probe.texels.size() != texels)
                return path + ".texels does not match the resolution";
            for (std::size_t t = 0; t < texels; ++t)
                if (!finite3(probe.texels[t]))
                    return path + ".texels[" + std::to_string(t) + "] must be finite";
        }
        return {};
    }

    std::string writeCanonical(const ReflectionSet& set)
    {
        std::string out;
        out += "{\n  \"format\": \"openeden.reflections\",\n  \"version\": 1,\n  \"cell\": {\n    ";
        if (set.cell.interior)
        {
            out += "\"interior\": ";
            appendString(out, set.cell.name);
        }
        else
            out += "\"exterior\": [" + std::to_string(set.cell.x) + ", " + std::to_string(set.cell.y) + "]";
        out += "\n  },\n  \"bake\": {\n";
        out += "    \"samples\": " + std::to_string(set.samples) + ",\n";
        out += "    \"bounces\": " + std::to_string(set.bounces) + ",\n";
        out += "    \"seed\": " + std::to_string(set.seed) + ",\n";
        out += "    \"ambient\": ";
        appendVec3(out, set.ambient);
        out += "\n  },\n  \"resolution\": " + std::to_string(set.resolution) + ",\n  \"probes\": [\n";
        for (std::size_t p = 0; p < set.probes.size(); ++p)
        {
            const ReflectionProbe& probe = set.probes[p];
            out += "    {\n      \"position\": ";
            appendVec3(out, probe.position);
            out += ",\n      \"texels\": [\n";
            for (std::uint32_t y = 0; y < set.resolution; ++y)
            {
                out += "        [";
                for (std::uint32_t x = 0; x < set.resolution; ++x)
                {
                    const vsg::vec3& t = probe.texels[static_cast<std::size_t>(y) * set.resolution + x];
                    if (x != 0)
                        out += ", ";
                    appendFloat(out, t.x);
                    out += ", ";
                    appendFloat(out, t.y);
                    out += ", ";
                    appendFloat(out, t.z);
                }
                out += y + 1 < set.resolution ? "],\n" : "]\n";
            }
            out += "      ]\n    }";
            out += p + 1 < set.probes.size() ? ",\n" : "\n";
        }
        out += "  ]\n}\n";
        return out;
    }

    std::string reflectionFilePath(const CellId& cell)
    {
        return "openeden/probes/" + cellKey(cell) + ".reflections.json";
    }
}
