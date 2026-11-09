#include "facecurves.hpp"

// OpenEden facial speech animation, task 0071 stage b. The JSON reader is a small strict parser of its own (no JSON
// library: the format is flat, and this keeps the library dependency-free). Memory is bounded by construction: strings
// by sMaxParsedString, nesting by sMaxNesting, members and generic arrays by small counts, and the integer arrays (the
// only large ones) are read straight into 32-bit vectors that are counted against sMaxTotalValues as they grow.
//
// PACKED BINARY LAYOUT ("OPFC", all integers little endian, version 1):
//   offset  size  field
//   0       4     magic "OPFC"
//   4       1     format version (1)
//   5       1     reserved, must be 0
//   6       2     frameRate (u16)
//   8       4     frameCount (u32)
//   12      8     audio duration seconds (IEEE-754 binary64 bits)
//   20      32    audio SHA-256, raw bytes
//   52      8     seed (i64)
//   60      ...   audio name, generator name, generator version, emotion: each u16 byte length, then the bytes
//                 (an emotion of length 0 means absent)
//           2     channel count (u16)
//           ...   per channel: kind (u8: 0 weight, 1 signed), name length (u8), name bytes, then frameCount values
//                 (weight: u8 each; signed: i16 each)
//   The file ends exactly after the last value.

#include <algorithm>
#include <charconv>
#include <cmath>
#include <cstring>
#include <limits>
#include <set>
#include <string>
#include <utility>

namespace Face
{
    std::string LoadError::format() const
    {
        std::string s = mFile + ":" + std::to_string(mLine) + ": ";
        if (!mPointer.empty())
            s += mPointer + ": ";
        return s + mMessage;
    }

    namespace
    {
        constexpr std::size_t sMaxParsedString = 4096; // parser bound; the format's own cap (256) is a validation rule
        constexpr std::size_t sMaxMembers = 64; // per object
        constexpr std::size_t sMaxGenericItems = 1024; // per array that does not hold integers
        constexpr std::size_t sMaxIssues = 64;
        constexpr std::size_t sMaxChannelName = 64;
        constexpr std::size_t sShaHexChars = 64;
        constexpr double sFrameSlack = 1e-9; // seconds: rounding slack of the duration rule

        // ---- UTF-8 -------------------------------------------------------------------------------------------------
        bool validUtf8(std::string_view s)
        {
            std::size_t i = 0;
            while (i < s.size())
            {
                const unsigned char c = static_cast<unsigned char>(s[i]);
                std::size_t n = 0;
                std::uint32_t cp = 0;
                if (c < 0x80)
                {
                    ++i;
                    continue;
                }
                if (c >= 0xC2 && c <= 0xDF)
                {
                    n = 1;
                    cp = c & 0x1Fu;
                }
                else if (c >= 0xE0 && c <= 0xEF)
                {
                    n = 2;
                    cp = c & 0x0Fu;
                }
                else if (c >= 0xF0 && c <= 0xF4)
                {
                    n = 3;
                    cp = c & 0x07u;
                }
                else
                {
                    return false;
                }
                if (i + n >= s.size())
                    return false; // the continuation bytes are missing
                for (std::size_t k = 1; k <= n; ++k)
                {
                    const unsigned char cc = static_cast<unsigned char>(s[i + k]);
                    if ((cc & 0xC0u) != 0x80u)
                        return false;
                    cp = (cp << 6) | (cc & 0x3Fu);
                }
                if ((n == 2 && cp < 0x800) || (n == 3 && cp < 0x10000) || cp > 0x10FFFF || (cp >= 0xD800 && cp <= 0xDFFF))
                    return false;
                i += n + 1;
            }
            return true;
        }

        void appendUtf8(std::string& out, std::uint32_t cp)
        {
            if (cp < 0x80)
                out.push_back(static_cast<char>(cp));
            else if (cp < 0x800)
            {
                out.push_back(static_cast<char>(0xC0 | (cp >> 6)));
                out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
            }
            else if (cp < 0x10000)
            {
                out.push_back(static_cast<char>(0xE0 | (cp >> 12)));
                out.push_back(static_cast<char>(0x80 | ((cp >> 6) & 0x3F)));
                out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
            }
            else
            {
                out.push_back(static_cast<char>(0xF0 | (cp >> 18)));
                out.push_back(static_cast<char>(0x80 | ((cp >> 12) & 0x3F)));
                out.push_back(static_cast<char>(0x80 | ((cp >> 6) & 0x3F)));
                out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
            }
        }

        // ---- JSON DOM -----------------------------------------------------------------------------------------------
        struct Node
        {
            enum class Type
            {
                Null,
                Bool,
                Int,
                Num,
                Str,
                Arr,
                Obj
            };
            Type mType = Type::Null;
            std::size_t mLine = 1;
            std::int64_t mInt = 0;
            double mNum = 0.0;
            std::string mStr;
            std::vector<Node> mItems; // generic array
            std::vector<std::int32_t> mInts; // array that starts with an integer
            std::vector<std::pair<std::string, Node>> mMembers;
        };

        struct Failure
        {
            LoadError mError;
        };

        class Parser
        {
        public:
            Parser(std::string_view text, std::string_view file)
                : mText(text)
                , mFile(file)
            {
            }

            Node parseDocument()
            {
                skipSpace();
                Node root = parseValue(0);
                skipSpace();
                if (mPos != mText.size())
                    fail("unexpected text after the end of the document");
                return root;
            }

        private:
            std::string_view mText;
            std::string mFile;
            std::size_t mPos = 0;
            std::size_t mLine = 1;
            std::size_t mTotalInts = 0;

            [[noreturn]] void fail(const std::string& message, const std::string& pointer = {})
            {
                throw Failure{ LoadError{ mFile, mLine, pointer, message } };
            }

            bool atEnd() const { return mPos >= mText.size(); }

            void skipSpace()
            {
                while (!atEnd())
                {
                    const char c = mText[mPos];
                    if (c == '\n')
                        ++mLine;
                    else if (c != ' ' && c != '\t' && c != '\r')
                        break;
                    ++mPos;
                }
            }

            Node parseValue(std::size_t depth)
            {
                if (atEnd())
                    fail("unexpected end of the document");
                Node node;
                node.mLine = mLine;
                const char c = mText[mPos];
                if (c == '{')
                    parseObject(node, depth);
                else if (c == '[')
                    parseArray(node, depth);
                else if (c == '"')
                {
                    node.mType = Node::Type::Str;
                    node.mStr = parseString();
                }
                else if (c == '-' || (c >= '0' && c <= '9'))
                    parseNumber(node);
                else if (matchWord("true"))
                {
                    node.mType = Node::Type::Bool;
                    node.mInt = 1;
                }
                else if (matchWord("false"))
                {
                    node.mType = Node::Type::Bool;
                }
                else if (matchWord("null"))
                {
                    node.mType = Node::Type::Null;
                }
                else
                {
                    fail("unexpected character");
                }
                return node;
            }

            bool matchWord(std::string_view word)
            {
                if (mText.substr(mPos, word.size()) != word)
                    return false;
                mPos += word.size();
                return true;
            }

            void parseObject(Node& node, std::size_t depth)
            {
                if (depth + 1 > Limits::sMaxNesting)
                    fail("nesting is deeper than " + std::to_string(Limits::sMaxNesting) + " levels");
                node.mType = Node::Type::Obj;
                ++mPos;
                skipSpace();
                if (!atEnd() && mText[mPos] == '}')
                {
                    ++mPos;
                    return;
                }
                while (true)
                {
                    skipSpace();
                    if (atEnd() || mText[mPos] != '"')
                        fail("expected a member name");
                    const std::size_t keyLine = mLine;
                    std::string key = parseString();
                    for (const auto& m : node.mMembers)
                        if (m.first == key)
                            fail("the key \"" + key + "\" appears twice", "/" + key);
                    skipSpace();
                    if (atEnd() || mText[mPos] != ':')
                        fail("expected ':'");
                    ++mPos;
                    skipSpace();
                    if (node.mMembers.size() >= sMaxMembers)
                        fail("an object has more than " + std::to_string(sMaxMembers) + " members");
                    Node value = parseValue(depth + 1);
                    (void)keyLine;
                    node.mMembers.emplace_back(std::move(key), std::move(value));
                    skipSpace();
                    if (atEnd())
                        fail("unexpected end of the document");
                    if (mText[mPos] == ',')
                    {
                        ++mPos;
                        continue;
                    }
                    if (mText[mPos] == '}')
                    {
                        ++mPos;
                        return;
                    }
                    fail("expected ',' or '}'");
                }
            }

            void parseArray(Node& node, std::size_t depth)
            {
                if (depth + 1 > Limits::sMaxNesting)
                    fail("nesting is deeper than " + std::to_string(Limits::sMaxNesting) + " levels");
                node.mType = Node::Type::Arr;
                ++mPos;
                skipSpace();
                if (!atEnd() && mText[mPos] == ']')
                {
                    ++mPos;
                    return;
                }
                // An array whose first element is a number is an integer array: it is the only large kind and is
                // kept compact; every element must then be an integer.
                const bool integers = !atEnd() && (mText[mPos] == '-' || (mText[mPos] >= '0' && mText[mPos] <= '9'));
                while (true)
                {
                    skipSpace();
                    if (integers)
                    {
                        Node n;
                        n.mLine = mLine;
                        if (atEnd() || !(mText[mPos] == '-' || (mText[mPos] >= '0' && mText[mPos] <= '9')))
                            fail("expected an integer");
                        parseNumber(n);
                        if (n.mType != Node::Type::Int)
                            fail("expected an integer");
                        if (n.mInt < std::numeric_limits<std::int32_t>::min()
                            || n.mInt > std::numeric_limits<std::int32_t>::max())
                            fail("the integer is out of range");
                        if (++mTotalInts > Limits::sMaxTotalValues)
                            fail("the file holds more than " + std::to_string(Limits::sMaxTotalValues) + " values");
                        node.mInts.push_back(static_cast<std::int32_t>(n.mInt));
                    }
                    else
                    {
                        if (node.mItems.size() >= sMaxGenericItems)
                            fail("an array has more than " + std::to_string(sMaxGenericItems) + " elements");
                        node.mItems.push_back(parseValue(depth + 1));
                    }
                    skipSpace();
                    if (atEnd())
                        fail("unexpected end of the document");
                    if (mText[mPos] == ',')
                    {
                        ++mPos;
                        continue;
                    }
                    if (mText[mPos] == ']')
                    {
                        ++mPos;
                        return;
                    }
                    fail("expected ',' or ']'");
                }
            }

            void parseNumber(Node& node)
            {
                const std::size_t begin = mPos;
                std::size_t p = mPos;
                auto digits = [&]() {
                    const std::size_t from = p;
                    while (p < mText.size() && mText[p] >= '0' && mText[p] <= '9')
                        ++p;
                    return p - from;
                };
                if (p < mText.size() && mText[p] == '-')
                    ++p;
                if (p < mText.size() && mText[p] == '0')
                    ++p;
                else if (digits() == 0)
                    fail("not a number");
                bool integral = true;
                if (p < mText.size() && mText[p] == '.')
                {
                    integral = false;
                    ++p;
                    if (digits() == 0)
                        fail("not a number");
                }
                if (p < mText.size() && (mText[p] == 'e' || mText[p] == 'E'))
                {
                    integral = false;
                    ++p;
                    if (p < mText.size() && (mText[p] == '+' || mText[p] == '-'))
                        ++p;
                    if (digits() == 0)
                        fail("not a number");
                }
                const char* first = mText.data() + begin;
                const char* last = mText.data() + p;
                if (integral)
                {
                    std::int64_t v = 0;
                    const auto r = std::from_chars(first, last, v);
                    if (r.ec != std::errc() || r.ptr != last)
                        fail("the integer is out of range");
                    node.mType = Node::Type::Int;
                    node.mInt = v;
                }
                else
                {
                    double v = 0.0;
                    const auto r = std::from_chars(first, last, v);
                    if (r.ec != std::errc() || r.ptr != last || !std::isfinite(v))
                        fail("the number is out of range");
                    node.mType = Node::Type::Num;
                    node.mNum = v == 0.0 ? 0.0 : v;
                }
                mPos = p;
            }

            std::uint32_t hex4()
            {
                if (mPos + 4 > mText.size())
                    fail("bad \\u escape");
                std::uint32_t v = 0;
                for (int i = 0; i < 4; ++i)
                {
                    const char c = mText[mPos++];
                    v <<= 4;
                    if (c >= '0' && c <= '9')
                        v |= static_cast<std::uint32_t>(c - '0');
                    else if (c >= 'a' && c <= 'f')
                        v |= static_cast<std::uint32_t>(c - 'a' + 10);
                    else if (c >= 'A' && c <= 'F')
                        v |= static_cast<std::uint32_t>(c - 'A' + 10);
                    else
                        fail("bad \\u escape");
                }
                return v;
            }

            std::string parseString()
            {
                ++mPos; // opening quote
                std::string out;
                const std::size_t start = mPos;
                while (true)
                {
                    if (atEnd())
                        fail("unterminated string");
                    const unsigned char c = static_cast<unsigned char>(mText[mPos]);
                    if (c == '"')
                    {
                        ++mPos;
                        break;
                    }
                    if (c < 0x20)
                        fail("a control character in a string");
                    if (c == '\\')
                    {
                        ++mPos;
                        if (atEnd())
                            fail("unterminated string");
                        const char e = mText[mPos++];
                        switch (e)
                        {
                            case '"':
                                out.push_back('"');
                                break;
                            case '\\':
                                out.push_back('\\');
                                break;
                            case '/':
                                out.push_back('/');
                                break;
                            case 'b':
                                out.push_back('\b');
                                break;
                            case 'f':
                                out.push_back('\f');
                                break;
                            case 'n':
                                out.push_back('\n');
                                break;
                            case 'r':
                                out.push_back('\r');
                                break;
                            case 't':
                                out.push_back('\t');
                                break;
                            case 'u':
                            {
                                std::uint32_t cp = hex4();
                                if (cp >= 0xD800 && cp <= 0xDBFF)
                                {
                                    if (mPos + 2 > mText.size() || mText[mPos] != '\\' || mText[mPos + 1] != 'u')
                                        fail("a lone surrogate in a \\u escape");
                                    mPos += 2;
                                    const std::uint32_t lo = hex4();
                                    if (lo < 0xDC00 || lo > 0xDFFF)
                                        fail("a lone surrogate in a \\u escape");
                                    cp = 0x10000 + ((cp - 0xD800) << 10) + (lo - 0xDC00);
                                }
                                else if (cp >= 0xDC00 && cp <= 0xDFFF)
                                {
                                    fail("a lone surrogate in a \\u escape");
                                }
                                appendUtf8(out, cp);
                                break;
                            }
                            default:
                                fail("an unknown escape in a string");
                        }
                    }
                    else
                    {
                        out.push_back(static_cast<char>(c));
                        ++mPos;
                    }
                    if (out.size() > sMaxParsedString)
                        fail("a string is longer than " + std::to_string(sMaxParsedString) + " bytes");
                }
                (void)start;
                if (!validUtf8(out))
                    fail("a string is not valid UTF-8");
                return out;
            }
        };

        // ---- DOM -> model ---------------------------------------------------------------------------------------------
        const Node* findMember(const Node& obj, std::string_view key)
        {
            for (const auto& m : obj.mMembers)
                if (m.first == key)
                    return &m.second;
            return nullptr;
        }

        std::size_t lineOfPointer(const Node& root, std::string_view pointer)
        {
            const Node* node = &root;
            std::size_t pos = 0;
            while (pos < pointer.size())
            {
                ++pos; // the '/'
                const std::size_t end = std::min(pointer.find('/', pos), pointer.size());
                const std::string_view token = pointer.substr(pos, end - pos);
                pos = end;
                if (node->mType == Node::Type::Obj)
                {
                    const Node* next = findMember(*node, token);
                    if (!next)
                        break;
                    node = next;
                }
                else if (node->mType == Node::Type::Arr && !node->mItems.empty())
                {
                    std::size_t index = 0;
                    const auto r = std::from_chars(token.data(), token.data() + token.size(), index);
                    if (r.ec != std::errc() || index >= node->mItems.size())
                        break;
                    node = &node->mItems[index];
                }
                else
                {
                    break; // an integer array: the array's own line
                }
            }
            return node->mLine;
        }

        class Mapper
        {
        public:
            Mapper(const Node& root, std::string_view file)
                : mRoot(root)
                , mFile(file)
            {
            }

            Curves map()
            {
                if (mRoot.mType != Node::Type::Obj)
                    fail("", "the document must be an object", mRoot);
                static const char* const sKeys[] = { "format", "version", "audio", "generator", "frameRate",
                    "frameCount", "channels" };
                checkKeys(mRoot, "", sKeys, 7, 7);

                const Node& format = *findMember(mRoot, "format");
                if (str(format, "/format") != sFormatName)
                    fail("/format", "the format must be \"" + std::string(sFormatName) + "\"", format);
                const Node& version = *findMember(mRoot, "version");
                if (integer(version, "/version") != sFormatVersion)
                    fail("/version", "unsupported version (this reader reads version 1)", version);

                Curves c;
                const Node& audio = *findMember(mRoot, "audio");
                expectObject(audio, "/audio");
                static const char* const sAudioKeys[] = { "name", "durationSeconds", "sha256" };
                checkKeys(audio, "/audio", sAudioKeys, 3, 3);
                c.mAudioName = str(*findMember(audio, "name"), "/audio/name");
                const Node& dur = *findMember(audio, "durationSeconds");
                if (dur.mType == Node::Type::Int)
                    c.mAudioDurationSeconds = static_cast<double>(dur.mInt);
                else if (dur.mType == Node::Type::Num)
                    c.mAudioDurationSeconds = dur.mNum;
                else
                    fail("/audio/durationSeconds", "must be a number", dur);
                c.mAudioSha256 = str(*findMember(audio, "sha256"), "/audio/sha256");

                const Node& gen = *findMember(mRoot, "generator");
                expectObject(gen, "/generator");
                static const char* const sGenKeys[] = { "name", "version", "seed", "emotion" };
                checkKeys(gen, "/generator", sGenKeys, 4, 3);
                c.mGeneratorName = str(*findMember(gen, "name"), "/generator/name");
                c.mGeneratorVersion = str(*findMember(gen, "version"), "/generator/version");
                c.mSeed = integer(*findMember(gen, "seed"), "/generator/seed");
                if (const Node* emotion = findMember(gen, "emotion"))
                {
                    c.mEmotion = str(*emotion, "/generator/emotion");
                    if (c.mEmotion.empty())
                        fail("/generator/emotion", "an emotion, when present, must not be empty", *emotion);
                }

                c.mFrameRate = u32(*findMember(mRoot, "frameRate"), "/frameRate");
                c.mFrameCount = u32(*findMember(mRoot, "frameCount"), "/frameCount");

                const Node& channels = *findMember(mRoot, "channels");
                if (channels.mType != Node::Type::Arr || !channels.mInts.empty())
                    fail("/channels", "must be an array of channel objects", channels);
                if (channels.mItems.size() > Limits::sMaxChannels)
                    fail("/channels", "more than " + std::to_string(Limits::sMaxChannels) + " channels", channels);
                for (std::size_t i = 0; i < channels.mItems.size(); ++i)
                {
                    const std::string at = "/channels/" + std::to_string(i);
                    const Node& n = channels.mItems[i];
                    expectObject(n, at);
                    static const char* const sChannelKeys[] = { "name", "kind", "values" };
                    checkKeys(n, at, sChannelKeys, 3, 3);
                    Channel ch;
                    ch.mName = str(*findMember(n, "name"), at + "/name");
                    const Node& kind = *findMember(n, "kind");
                    const std::string& kindName = str(kind, at + "/kind");
                    if (kindName == "weight")
                        ch.mKind = ChannelKind::Weight;
                    else if (kindName == "signed")
                        ch.mKind = ChannelKind::Signed;
                    else
                        fail(at + "/kind", "the kind must be \"weight\" or \"signed\"", kind);
                    const Node& values = *findMember(n, "values");
                    if (values.mType != Node::Type::Arr || !values.mItems.empty())
                        fail(at + "/values", "must be an array of integers", values);
                    ch.mValues.reserve(values.mInts.size());
                    for (std::size_t k = 0; k < values.mInts.size(); ++k)
                    {
                        const std::int32_t v = values.mInts[k];
                        if (v < std::numeric_limits<std::int16_t>::min() || v > std::numeric_limits<std::int16_t>::max())
                            fail(at + "/values/" + std::to_string(k), "the value is out of range", values);
                        ch.mValues.push_back(static_cast<std::int16_t>(v));
                    }
                    c.mChannels.push_back(std::move(ch));
                }
                return c;
            }

        private:
            const Node& mRoot;
            std::string mFile;

            [[noreturn]] void fail(const std::string& pointer, const std::string& message, const Node& at)
            {
                throw Failure{ LoadError{ mFile, at.mLine, pointer, message } };
            }

            void expectObject(const Node& n, const std::string& pointer)
            {
                if (n.mType != Node::Type::Obj)
                    fail(pointer, "must be an object", n);
            }

            void checkKeys(const Node& obj, const std::string& at, const char* const* keys, std::size_t count,
                std::size_t required)
            {
                for (const auto& m : obj.mMembers)
                {
                    bool known = false;
                    for (std::size_t i = 0; i < count; ++i)
                        known = known || m.first == keys[i];
                    if (!known)
                        fail(at + "/" + m.first, "unknown key \"" + m.first + "\"", m.second);
                }
                for (std::size_t i = 0; i < required; ++i)
                    if (!findMember(obj, keys[i]))
                        fail(at + "/" + keys[i], std::string("missing key \"") + keys[i] + "\"", obj);
            }

            const std::string& str(const Node& n, const std::string& pointer)
            {
                if (n.mType != Node::Type::Str)
                    fail(pointer, "must be a string", n);
                return n.mStr;
            }

            std::int64_t integer(const Node& n, const std::string& pointer)
            {
                if (n.mType != Node::Type::Int)
                    fail(pointer, "must be an integer", n);
                return n.mInt;
            }

            std::uint32_t u32(const Node& n, const std::string& pointer)
            {
                const std::int64_t v = integer(n, pointer);
                if (v < 0 || v > std::numeric_limits<std::uint32_t>::max())
                    fail(pointer, "the integer is out of range", n);
                return static_cast<std::uint32_t>(v);
            }
        };

        // ---- Validation helpers -------------------------------------------------------------------------------------
        bool validLowerHex(std::string_view s)
        {
            if (s.size() != sShaHexChars)
                return false;
            for (const char c : s)
                if (!((c >= '0' && c <= '9') || (c >= 'a' && c <= 'f')))
                    return false;
            return true;
        }

        bool durationFitsFrames(double duration, std::uint32_t rate, std::uint32_t count)
        {
            if (!std::isfinite(duration) || rate == 0)
                return false;
            const double frames = static_cast<double>(count) / static_cast<double>(rate);
            return std::fabs(frames - duration) <= 1.0 / static_cast<double>(rate) + sFrameSlack;
        }

        // ---- Writer -------------------------------------------------------------------------------------------------
        void appendJsonString(std::string& out, std::string_view s)
        {
            static const char* const sHex = "0123456789abcdef";
            out.push_back('"');
            for (const char ch : s)
            {
                const unsigned char c = static_cast<unsigned char>(ch);
                switch (c)
                {
                    case '"':
                        out += "\\\"";
                        break;
                    case '\\':
                        out += "\\\\";
                        break;
                    case '\n':
                        out += "\\n";
                        break;
                    case '\t':
                        out += "\\t";
                        break;
                    case '\r':
                        out += "\\r";
                        break;
                    default:
                        if (c < 0x20)
                        {
                            out += "\\u00";
                            out.push_back(sHex[c >> 4]);
                            out.push_back(sHex[c & 0xF]);
                        }
                        else
                        {
                            out.push_back(ch);
                        }
                }
            }
            out.push_back('"');
        }

        std::string numberText(double v)
        {
            if (v == 0.0)
                return "0";
            char buffer[64];
            const auto r = std::to_chars(buffer, buffer + sizeof(buffer), v);
            return std::string(buffer, r.ptr);
        }

        // ---- Binary helpers -----------------------------------------------------------------------------------------
        void put16(std::vector<std::uint8_t>& out, std::uint32_t v)
        {
            out.push_back(static_cast<std::uint8_t>(v));
            out.push_back(static_cast<std::uint8_t>(v >> 8));
        }

        void put32(std::vector<std::uint8_t>& out, std::uint32_t v)
        {
            for (int i = 0; i < 4; ++i)
                out.push_back(static_cast<std::uint8_t>(v >> (8 * i)));
        }

        void put64(std::vector<std::uint8_t>& out, std::uint64_t v)
        {
            for (int i = 0; i < 8; ++i)
                out.push_back(static_cast<std::uint8_t>(v >> (8 * i)));
        }

        void putString16(std::vector<std::uint8_t>& out, const std::string& s)
        {
            put16(out, static_cast<std::uint32_t>(s.size()));
            out.insert(out.end(), s.begin(), s.end());
        }

        int hexValue(char c) { return c <= '9' ? c - '0' : c - 'a' + 10; }

        class ByteReader
        {
        public:
            ByteReader(const std::vector<std::uint8_t>& bytes, std::string_view file)
                : mBytes(bytes)
                , mFile(file)
            {
            }

            std::size_t remaining() const { return mBytes.size() - mPos; }
            std::size_t position() const { return mPos; }

            [[noreturn]] void fail(const std::string& message) const
            {
                throw Failure{ LoadError{ mFile, 1, "", message + " (at byte " + std::to_string(mPos) + ")" } };
            }

            void need(std::size_t n) const
            {
                if (remaining() < n)
                    fail("the file ends early");
            }

            std::uint32_t get(std::size_t bytes)
            {
                need(bytes);
                std::uint64_t v = 0;
                for (std::size_t i = 0; i < bytes; ++i)
                    v |= static_cast<std::uint64_t>(mBytes[mPos + i]) << (8 * i);
                mPos += bytes;
                return static_cast<std::uint32_t>(v);
            }

            std::uint64_t get64()
            {
                need(8);
                std::uint64_t v = 0;
                for (std::size_t i = 0; i < 8; ++i)
                    v |= static_cast<std::uint64_t>(mBytes[mPos + i]) << (8 * i);
                mPos += 8;
                return v;
            }

            std::string getString(std::size_t length)
            {
                need(length);
                std::string s(reinterpret_cast<const char*>(mBytes.data() + mPos), length);
                mPos += length;
                return s;
            }

            const std::uint8_t* take(std::size_t n)
            {
                need(n);
                const std::uint8_t* p = mBytes.data() + mPos;
                mPos += n;
                return p;
            }

        private:
            const std::vector<std::uint8_t>& mBytes;
            std::string mFile;
            std::size_t mPos = 0;
        };
    }

    bool isValidChannelName(std::string_view name)
    {
        if (name.empty() || name.size() > sMaxChannelName)
            return false;
        for (const char c : name)
            if (!((c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '_' || c == '.'))
                return false;
        return true;
    }

    std::vector<ValidationIssue> validate(const Curves& c)
    {
        std::vector<ValidationIssue> issues;
        auto add = [&](std::string pointer, std::string message) {
            if (issues.size() < sMaxIssues)
                issues.push_back({ std::move(pointer), std::move(message) });
        };
        auto checkString = [&](const std::string& s, const std::string& pointer, bool mayBeEmpty) {
            if (s.empty() && !mayBeEmpty)
                add(pointer, "must not be empty");
            else if (s.size() > Limits::sMaxStringBytes)
                add(pointer, "longer than " + std::to_string(Limits::sMaxStringBytes) + " bytes");
            else if (!validUtf8(s))
                add(pointer, "not valid UTF-8");
        };

        checkString(c.mAudioName, "/audio/name", false);
        if (!validLowerHex(c.mAudioSha256))
            add("/audio/sha256", "must be 64 lowercase hexadecimal characters");
        checkString(c.mGeneratorName, "/generator/name", false);
        checkString(c.mGeneratorVersion, "/generator/version", false);
        checkString(c.mEmotion, "/generator/emotion", true);

        const bool rateOk = c.mFrameRate >= 1 && c.mFrameRate <= Limits::sMaxFrameRate;
        const bool countOk = c.mFrameCount >= 1 && c.mFrameCount <= Limits::sMaxFrameCount;
        if (!rateOk)
            add("/frameRate", "must be 1 to " + std::to_string(Limits::sMaxFrameRate));
        if (!countOk)
            add("/frameCount", "must be 1 to " + std::to_string(Limits::sMaxFrameCount));
        if (!std::isfinite(c.mAudioDurationSeconds) || c.mAudioDurationSeconds < 0.0)
            add("/audio/durationSeconds", "must be a finite number of seconds, zero or more");
        else if (rateOk && countOk && !durationFitsFrames(c.mAudioDurationSeconds, c.mFrameRate, c.mFrameCount))
            add("/audio/durationSeconds", "differs from frameCount / frameRate by more than one frame");

        if (c.mChannels.empty())
            add("/channels", "there must be at least one channel");
        if (c.mChannels.size() > Limits::sMaxChannels)
        {
            add("/channels", "more than " + std::to_string(Limits::sMaxChannels) + " channels");
            return issues;
        }
        if (countOk && c.mChannels.size() * static_cast<std::size_t>(c.mFrameCount) > Limits::sMaxTotalValues)
        {
            add("/channels", "channels x frames is more than " + std::to_string(Limits::sMaxTotalValues) + " values");
            return issues;
        }
        std::set<std::string_view> seen;
        for (std::size_t i = 0; i < c.mChannels.size(); ++i)
        {
            const Channel& ch = c.mChannels[i];
            const std::string at = "/channels/" + std::to_string(i);
            if (!isValidChannelName(ch.mName))
                add(at + "/name", "a channel name is 1 to 64 characters of a-z, 0-9, '_' and '.'");
            else if (!seen.insert(ch.mName).second)
                add(at + "/name", "the channel name \"" + ch.mName + "\" is used twice");
            if (countOk && ch.mValues.size() != c.mFrameCount)
            {
                add(at + "/values", "holds " + std::to_string(ch.mValues.size()) + " values, frameCount is "
                        + std::to_string(c.mFrameCount));
                continue;
            }
            const int low = ch.mKind == ChannelKind::Signed ? -Limits::sMaxWeight : 0;
            for (std::size_t k = 0; k < ch.mValues.size(); ++k)
            {
                if (ch.mValues[k] < low || ch.mValues[k] > Limits::sMaxWeight)
                {
                    add(at + "/values/" + std::to_string(k),
                        "the value " + std::to_string(ch.mValues[k]) + " is outside " + std::to_string(low) + " to "
                            + std::to_string(Limits::sMaxWeight));
                    break;
                }
            }
        }
        return issues;
    }

    std::optional<Curves> readJson(std::string_view text, std::string_view fileName, LoadError& error)
    {
        try
        {
            if (text.size() > Limits::sMaxFileBytes)
                throw Failure{ LoadError{ std::string(fileName), 1, "",
                    "the file is larger than " + std::to_string(Limits::sMaxFileBytes) + " bytes" } };
            Parser parser(text, fileName);
            const Node root = parser.parseDocument();
            Curves curves = Mapper(root, fileName).map();
            const auto issues = validate(curves);
            if (!issues.empty())
                throw Failure{ LoadError{ std::string(fileName), lineOfPointer(root, issues.front().mPointer),
                    issues.front().mPointer, issues.front().mMessage } };
            return curves;
        }
        catch (const Failure& f)
        {
            error = f.mError;
        }
        catch (const std::exception& e)
        {
            error = LoadError{ std::string(fileName), 1, "", std::string("internal error: ") + e.what() };
        }
        return std::nullopt;
    }

    std::optional<std::string> writeJson(const Curves& c)
    {
        if (!validate(c).empty())
            return std::nullopt;
        std::string out;
        out += "{\n  \"format\": \"opas.facecurves\",\n  \"version\": 1,\n";
        out += "  \"audio\": {\"name\": ";
        appendJsonString(out, c.mAudioName);
        out += ", \"durationSeconds\": " + numberText(c.mAudioDurationSeconds) + ", \"sha256\": ";
        appendJsonString(out, c.mAudioSha256);
        out += "},\n  \"generator\": {\"name\": ";
        appendJsonString(out, c.mGeneratorName);
        out += ", \"version\": ";
        appendJsonString(out, c.mGeneratorVersion);
        out += ", \"seed\": " + std::to_string(c.mSeed);
        if (!c.mEmotion.empty())
        {
            out += ", \"emotion\": ";
            appendJsonString(out, c.mEmotion);
        }
        out += "},\n  \"frameRate\": " + std::to_string(c.mFrameRate) + ",\n  \"frameCount\": "
            + std::to_string(c.mFrameCount) + ",\n  \"channels\": [\n";
        for (std::size_t i = 0; i < c.mChannels.size(); ++i)
        {
            const Channel& ch = c.mChannels[i];
            out += "    {\"name\": ";
            appendJsonString(out, ch.mName);
            out += ch.mKind == ChannelKind::Weight ? ", \"kind\": \"weight\", \"values\": ["
                                                   : ", \"kind\": \"signed\", \"values\": [";
            for (std::size_t k = 0; k < ch.mValues.size(); ++k)
            {
                if (k != 0)
                    out += ", ";
                out += std::to_string(ch.mValues[k]);
            }
            out += i + 1 == c.mChannels.size() ? "]}\n" : "]},\n";
        }
        out += "  ]\n}\n";
        return out;
    }

    std::optional<std::vector<std::uint8_t>> writeBinary(const Curves& c)
    {
        if (!validate(c).empty())
            return std::nullopt;
        std::vector<std::uint8_t> out;
        out.reserve(128 + c.mChannels.size() * (c.mFrameCount * 2 + 70));
        out.insert(out.end(), { 'O', 'P', 'F', 'C', 1, 0 });
        put16(out, c.mFrameRate);
        put32(out, c.mFrameCount);
        std::uint64_t bits = 0;
        const double duration = c.mAudioDurationSeconds == 0.0 ? 0.0 : c.mAudioDurationSeconds;
        std::memcpy(&bits, &duration, sizeof(bits));
        put64(out, bits);
        for (std::size_t i = 0; i < 32; ++i)
            out.push_back(static_cast<std::uint8_t>(
                (hexValue(c.mAudioSha256[2 * i]) << 4) | hexValue(c.mAudioSha256[2 * i + 1])));
        put64(out, static_cast<std::uint64_t>(c.mSeed));
        putString16(out, c.mAudioName);
        putString16(out, c.mGeneratorName);
        putString16(out, c.mGeneratorVersion);
        putString16(out, c.mEmotion);
        put16(out, static_cast<std::uint32_t>(c.mChannels.size()));
        for (const Channel& ch : c.mChannels)
        {
            out.push_back(ch.mKind == ChannelKind::Weight ? 0 : 1);
            out.push_back(static_cast<std::uint8_t>(ch.mName.size()));
            out.insert(out.end(), ch.mName.begin(), ch.mName.end());
            for (const std::int16_t v : ch.mValues)
            {
                if (ch.mKind == ChannelKind::Weight)
                    out.push_back(static_cast<std::uint8_t>(v));
                else
                    put16(out, static_cast<std::uint16_t>(v));
            }
        }
        return out;
    }

    std::optional<Curves> readBinary(const std::vector<std::uint8_t>& bytes, std::string_view fileName, LoadError& error)
    {
        try
        {
            if (bytes.size() > Limits::sMaxFileBytes)
                throw Failure{ LoadError{ std::string(fileName), 1, "",
                    "the file is larger than " + std::to_string(Limits::sMaxFileBytes) + " bytes" } };
            ByteReader in(bytes, fileName);
            if (in.getString(4) != "OPFC")
                in.fail("not an opas.facecurves binary file (bad magic)");
            if (in.get(1) != 1)
                in.fail("unsupported binary version (this reader reads version 1)");
            if (in.get(1) != 0)
                in.fail("the reserved byte must be 0");
            Curves c;
            c.mFrameRate = in.get(2);
            c.mFrameCount = in.get(4);
            const std::uint64_t bits = in.get64();
            std::memcpy(&c.mAudioDurationSeconds, &bits, sizeof(bits));
            static const char* const sHex = "0123456789abcdef";
            const std::uint8_t* sha = in.take(32);
            for (std::size_t i = 0; i < 32; ++i)
            {
                c.mAudioSha256.push_back(sHex[sha[i] >> 4]);
                c.mAudioSha256.push_back(sHex[sha[i] & 0xF]);
            }
            c.mSeed = static_cast<std::int64_t>(in.get64());
            c.mAudioName = in.getString(in.get(2));
            c.mGeneratorName = in.getString(in.get(2));
            c.mGeneratorVersion = in.getString(in.get(2));
            c.mEmotion = in.getString(in.get(2));
            const std::size_t channelCount = in.get(2);
            if (channelCount > Limits::sMaxChannels)
                in.fail("more than " + std::to_string(Limits::sMaxChannels) + " channels");
            if (c.mFrameCount > Limits::sMaxFrameCount)
                in.fail("frameCount is above " + std::to_string(Limits::sMaxFrameCount));
            // Every channel needs at least its header and one byte per frame: check before allocating anything.
            if (channelCount * (static_cast<std::size_t>(c.mFrameCount) + 3) > in.remaining())
                in.fail("the file is too short for its channels and frames");
            if (channelCount * static_cast<std::size_t>(c.mFrameCount) > Limits::sMaxTotalValues)
                in.fail("channels x frames is more than " + std::to_string(Limits::sMaxTotalValues) + " values");
            for (std::size_t i = 0; i < channelCount; ++i)
            {
                Channel ch;
                const std::uint32_t kind = in.get(1);
                if (kind > 1)
                    in.fail("an unknown channel kind");
                ch.mKind = kind == 0 ? ChannelKind::Weight : ChannelKind::Signed;
                ch.mName = in.getString(in.get(1));
                const std::size_t width = kind == 0 ? 1 : 2;
                in.need(width * c.mFrameCount);
                ch.mValues.reserve(c.mFrameCount);
                for (std::uint32_t k = 0; k < c.mFrameCount; ++k)
                {
                    const std::uint32_t raw = in.get(width);
                    ch.mValues.push_back(
                        kind == 0 ? static_cast<std::int16_t>(raw) : static_cast<std::int16_t>(static_cast<std::uint16_t>(raw)));
                }
                c.mChannels.push_back(std::move(ch));
            }
            if (in.remaining() != 0)
                in.fail("unexpected bytes after the last channel");
            const auto issues = validate(c);
            if (!issues.empty())
                throw Failure{ LoadError{ std::string(fileName), 1, issues.front().mPointer, issues.front().mMessage } };
            return c;
        }
        catch (const Failure& f)
        {
            error = f.mError;
        }
        catch (const std::exception& e)
        {
            error = LoadError{ std::string(fileName), 1, "", std::string("internal error: ") + e.what() };
        }
        return std::nullopt;
    }

    bool audioMatches(const Curves& c, std::string_view sha256, double durationSeconds)
    {
        if (c.mAudioSha256.empty() || c.mAudioSha256 != sha256 || !std::isfinite(durationSeconds) || c.mFrameRate == 0)
            return false;
        return std::fabs(durationSeconds - c.mAudioDurationSeconds) <= 1.0 / static_cast<double>(c.mFrameRate) + sFrameSlack;
    }
}
