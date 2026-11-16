#include "game_skyrimse/lockbooks.hpp"

#include "game_skyrimse/pack_core.hpp"
#include "game_skyrimse/survey.hpp"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstring>

// Rules and layouts: see the header (task 0203). Every tuning number is RECALLED-VERIFY.

namespace game_skyrimse::lockbooks
{
    namespace
    {
        std::uint64_t splitmix(std::uint64_t state)
        {
            state += 0x9E3779B97F4A7C15ull;
            std::uint64_t z = state;
            z = (z ^ (z >> 30)) * 0xBF58476D1CE4E5B9ull;
            z = (z ^ (z >> 27)) * 0x94D049BB133111EBull;
            return z ^ (z >> 31);
        }

        std::uint32_t le32(const std::byte* p)
        {
            std::uint32_t v;
            std::memcpy(&v, p, 4);
            return v;
        }

        float lef(const std::byte* p)
        {
            float v;
            std::memcpy(&v, p, 4);
            return v;
        }

        Content::Error makeError(const std::string& file, std::uint64_t offset, std::uint32_t type, std::string message)
        {
            Content::Error error;
            error.mFile = file;
            error.mOffset = offset;
            error.mRecordType = type;
            error.mMessage = std::move(message);
            return error;
        }

        constexpr std::uint64_t kSweetSalt = 0x4C6F636B50696B21ull; // "LockPik!"
        constexpr float kSweetRange = 85.f;
        constexpr float kAngleLimit = 90.f;
        constexpr float kAtLimit = 1e-4f; // progress this close to the turn limit counts as at it
        constexpr float kSkillMax = 100.f;
    }

    // ---- locks ---------------------------------------------------------------------------------------------------

    LockTier tierOf(std::int32_t level)
    {
        if (level < 0 || level > 255)
            return LockTier::RequiresKey;
        if (level == 255)
            return LockTier::RequiresKey;
        if (level < 25)
            return LockTier::Novice;
        if (level < 50)
            return LockTier::Apprentice;
        if (level < 75)
            return LockTier::Adept;
        if (level < 100)
            return LockTier::Expert;
        return LockTier::Master;
    }

    bool pickable(std::int32_t level)
    {
        return tierOf(level) != LockTier::RequiresKey;
    }

    std::string_view tierName(LockTier tier)
    {
        switch (tier)
        {
            case LockTier::Novice:
                return "novice";
            case LockTier::Apprentice:
                return "apprentice";
            case LockTier::Adept:
                return "adept";
            case LockTier::Expert:
                return "expert";
            case LockTier::Master:
                return "master";
            case LockTier::RequiresKey:
                break;
        }
        return "requires key";
    }

    float windowDegrees(LockTier tier, float skill, const PickSettings& settings)
    {
        if (tier == LockTier::RequiresKey)
            return 0.f;
        const float s = std::isfinite(skill) ? std::clamp(skill, 0.f, kSkillMax) : 0.f;
        const float width = settings.mWindow[static_cast<std::size_t>(tier)] + s * settings.mWindowPerSkill;
        return std::max(width, settings.mWindowMin);
    }

    float sweetSpotOf(std::uint64_t placement)
    {
        const std::uint64_t z = splitmix(placement ^ kSweetSalt);
        const float u = static_cast<float>(z >> 48) / 65536.f; // [0, 1)
        return -kSweetRange + 2.f * kSweetRange * u;
    }

    float unlockExperience(LockTier tier, const PickSettings& settings)
    {
        if (tier == LockTier::RequiresKey)
            return 0.f;
        return settings.mXpPerTier * static_cast<float>(static_cast<int>(tier) + 1);
    }

    bool Attempt::begin(std::uint64_t placement, std::int32_t level, float skill, const PickSettings& settings)
    {
        mSettings = settings;
        mPlacement = placement;
        mLevel = level;
        mTier = tierOf(level);
        mProgress = 0.f;
        mStrain = 0.f;
        mSweet = sweetSpotOf(placement);
        mWindow = windowDegrees(mTier, skill, settings);
        if (mTier == LockTier::RequiresKey)
        {
            mState = State::Refused;
            return false;
        }
        mState = State::Picking;
        return true;
    }

    float Attempt::turnLimit(float angle) const
    {
        const float diff = std::fabs(angle - mSweet);
        if (diff <= mWindow)
            return 1.f;
        return std::max(0.f, 1.f - (diff - mWindow) / mSettings.mFalloff);
    }

    void Attempt::update(float dt, float angle, bool turning)
    {
        if (mState != State::Picking || !(dt > 0.f) || !std::isfinite(dt))
            return;
        dt = std::min(dt, mSettings.mMaxStep);
        angle = std::isfinite(angle) ? std::clamp(angle, -kAngleLimit, kAngleLimit) : 0.f;
        const float limit = turnLimit(angle);
        if (turning)
        {
            if (mProgress < limit)
                mProgress = std::min(limit, mProgress + dt / mSettings.mTurnSeconds);
            else if (mProgress > limit)
                mProgress = std::max(limit, mProgress - mSettings.mRelaxPerSecond * dt);
            if (limit >= 1.f && mProgress >= 1.f - kAtLimit)
            {
                mProgress = 1.f;
                mState = State::Opened;
                return;
            }
            if (limit < 1.f && mProgress >= limit - kAtLimit)
                mStrain += dt / mSettings.mStrainSeconds;
            if (mStrain >= 1.f - kAtLimit)
            {
                mStrain = 1.f;
                mState = State::Broke;
            }
        }
        else
        {
            mProgress = std::max(0.f, mProgress - mSettings.mRelaxPerSecond * dt);
            mStrain = std::max(0.f, mStrain - mSettings.mStrainRecover * dt);
        }
    }

    void Attempt::cancel()
    {
        if (mState == State::Picking)
            mState = State::Cancelled;
    }

    // ---- books ---------------------------------------------------------------------------------------------------

    std::string_view bookKindName(BookKind kind)
    {
        switch (kind)
        {
            case BookKind::Skill:
                return "skill";
            case BookKind::Spell:
                return "spell";
            case BookKind::Plain:
                break;
        }
        return "book";
    }

    BookKind BookRecord::kind() const
    {
        if ((mFlags & BookFlags::TeachesSkill) != 0)
            return BookKind::Skill;
        if ((mFlags & BookFlags::TeachesSpell) != 0)
            return BookKind::Spell;
        return BookKind::Plain;
    }

    int BookRecord::skillSlot() const
    {
        if (kind() != BookKind::Skill || mTeaches < 6 || mTeaches > 23)
            return -1;
        return static_cast<int>(mTeaches) - 6;
    }

    namespace
    {
        class Parser final : public Content::Visitor
        {
        public:
            Parser(const Content::Plugin& plugin, const FormIdResolver& resolver, const BookLimits& limits,
                BookRecords& out)
                : mPlugin(plugin)
                , mResolver(resolver)
                , mLimits(limits)
                , mOut(out)
                , mLocalized(plugin.header().isLocalized())
            {
            }

            bool onRecord(const Content::RawRecord& record) override
            {
                if (record.mType != Content::fourCC("BOOK"))
                    return true;
                const bool deleted = (record.mFlags & RecordFlags::Deleted) != 0;
                const std::optional<FormId> id = mResolver.resolve(record.mFormId);
                if (!id || *id == kNullFormId)
                {
                    ++mOut.mOdd;
                    return true;
                }
                if (deleted)
                {
                    mOut.mRemoved.push_back(*id);
                    return true;
                }
                std::span<const std::byte> bytes = record.mData;
                if (record.isCompressed())
                {
                    if (record.mUncompressedSize > mLimits.mMaxRecordBytes)
                    {
                        fail(record, "the record inflates past the limit");
                        return true;
                    }
                    Content::Error error;
                    if (!inflateRecord(record, mPlugin.name(), mInflated, error))
                    {
                        addError(std::move(error));
                        return true;
                    }
                    bytes = mInflated;
                }
                else if (record.mDataSize > mLimits.mMaxRecordBytes)
                {
                    fail(record, "the record is over the size limit");
                    return true;
                }
                BookRecord book;
                book.mId = *id;
                std::uint32_t rawTeaches = 0;
                const bool ok = survey::forEachSubrecord(bytes, [&](const survey::SubrecordView& sub) {
                    if (sub.mType == Content::fourCC("EDID"))
                        book.mEditorId = textOf(sub.mData);
                    else if (sub.mType == Content::fourCC("FULL"))
                        book.mName = lstring(sub.mData);
                    else if (sub.mType == Content::fourCC("DESC"))
                        book.mDesc = lstring(sub.mData);
                    else if (sub.mType == Content::fourCC("DATA"))
                    {
                        if (sub.mData.size() != 16)
                        {
                            ++mOut.mOdd;
                            return;
                        }
                        book.mFlags = static_cast<std::uint8_t>(sub.mData[0]);
                        book.mType = static_cast<std::uint8_t>(sub.mData[1]);
                        rawTeaches = le32(sub.mData.data() + 4);
                        book.mValue = static_cast<std::int32_t>(le32(sub.mData.data() + 8));
                        book.mWeight = lef(sub.mData.data() + 12);
                        book.mDecoded = true;
                    }
                });
                if (!ok)
                {
                    fail(record, "a subrecord is cut short or claims more bytes than the record holds");
                    return true;
                }
                if ((book.mFlags & BookFlags::TeachesSkill) != 0 && (book.mFlags & BookFlags::TeachesSpell) != 0)
                    ++mOut.mBothFlags;
                if ((book.mFlags & BookFlags::TeachesSkill) != 0)
                    book.mTeaches = rawTeaches;
                else if ((book.mFlags & BookFlags::TeachesSpell) != 0)
                {
                    const std::optional<FormId> spell = mResolver.resolve(rawTeaches);
                    if (spell)
                        book.mTeaches = *spell;
                    else
                    {
                        ++mOut.mOdd;
                        book.mFlags = static_cast<std::uint8_t>(book.mFlags & ~BookFlags::TeachesSpell);
                    }
                }
                mOut.mBooks.push_back(std::move(book));
                return true;
            }

        private:
            std::string textOf(std::span<const std::byte> data) const
            {
                std::size_t n = 0;
                while (n < data.size() && n < mLimits.mMaxTextBytes && data[n] != std::byte(0))
                    ++n;
                return std::string(reinterpret_cast<const char*>(data.data()), n);
            }

            LString lstring(std::span<const std::byte> data) const
            {
                LString s;
                s.mPresent = true;
                if (mLocalized)
                {
                    if (data.size() == 4)
                    {
                        s.mLocalized = true;
                        s.mId = le32(data.data());
                    }
                    else
                        s.mPresent = false;
                    return s;
                }
                s.mText = textOf(data);
                return s;
            }

            void addError(Content::Error error)
            {
                ++mOut.mFailed;
                if (mOut.mErrors.size() < mLimits.mMaxErrors)
                    mOut.mErrors.push_back(std::move(error));
            }

            void fail(const Content::RawRecord& record, std::string message)
            {
                addError(makeError(mPlugin.name(), record.mOffset, record.mType, std::move(message)));
            }

            const Content::Plugin& mPlugin;
            const FormIdResolver& mResolver;
            const BookLimits& mLimits;
            BookRecords& mOut;
            bool mLocalized;
            std::vector<std::byte> mInflated;
        };
    }

    bool parseBookRecords(
        const Content::Plugin& plugin, const LoadOrder& order, const BookLimits& limits, BookRecords& out)
    {
        out = BookRecords();
        out.mName = plugin.name();
        out.mLocalized = plugin.header().isLocalized();
        if (!isSkyrimSeHeader(plugin.header()))
        {
            out.mWalkError = makeError(plugin.name(), 0, 0, "this plugin is not a Skyrim Special Edition one");
            return false;
        }
        const FormIdResolver resolver(plugin.header(), plugin.name(), order);
        Parser parser(plugin, resolver, limits, out);
        const std::optional<Content::Error> error = plugin.walk(parser);
        if (error)
        {
            out.mWalkError = *error;
            return false;
        }
        return true;
    }

    void BookIndex::add(BookRecords&& records, std::uint32_t pluginIndex)
    {
        mOdd += records.mOdd;
        for (const FormId id : records.mRemoved)
            mBooks.erase(id);
        for (BookRecord& book : records.mBooks)
        {
            book.mPlugin = pluginIndex;
            mBooks[book.mId] = std::move(book);
        }
    }

    const BookRecord* BookIndex::book(FormId id) const
    {
        const auto it = mBooks.find(id);
        return it == mBooks.end() ? nullptr : &it->second;
    }

    // ---- text ----------------------------------------------------------------------------------------------------

    namespace
    {
        // Windows-1252 0x80 to 0x9F as code points (0 = undefined in the code page).
        constexpr std::uint32_t kCp1252[32] = { 0x20AC, 0, 0x201A, 0x0192, 0x201E, 0x2026, 0x2020, 0x2021, 0x02C6,
            0x2030, 0x0160, 0x2039, 0x0152, 0, 0x017D, 0, 0, 0x2018, 0x2019, 0x201C, 0x201D, 0x2022, 0x2013, 0x2014,
            0x02DC, 0x2122, 0x0161, 0x203A, 0x0153, 0, 0x017E, 0x0178 };

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

        bool iequals(std::string_view a, std::string_view b)
        {
            if (a.size() != b.size())
                return false;
            for (std::size_t i = 0; i < a.size(); ++i)
                if (std::tolower(static_cast<unsigned char>(a[i])) != std::tolower(static_cast<unsigned char>(b[i])))
                    return false;
            return true;
        }

        constexpr std::size_t kMaxTagBytes = 4096;
        constexpr std::size_t kMaxEntityBytes = 12;

        // Builds paragraphs and pages from the markup.
        class TextBuilder
        {
        public:
            explicit TextBuilder(std::size_t pageChars)
                : mPageChars(pageChars == 0 ? kDefaultPageChars : pageChars)
            {
            }

            void text(std::string_view utf8)
            {
                for (const char c : utf8)
                    put(c);
            }
            void put(char c)
            {
                if (c == ' ')
                {
                    flushNewlines();
                    if (!mCur.empty() && mCur.back() != ' ' && mCur.back() != '\n')
                        mPendingSpace = true;
                    return;
                }
                flushNewlines();
                if (mPendingSpace)
                {
                    mCur.push_back(' ');
                    mPendingSpace = false;
                }
                mCur.push_back(c);
            }
            void rawNewline() { ++mNewlines; }
            void lineBreak()
            {
                flushNewlines();
                mPendingSpace = false;
                if (mCur.empty())
                    return;
                if (mCur.back() == '\n')
                    flushParagraph(); // a second break in a row is an empty line (the real books use <br><br>)
                else
                    mCur.push_back('\n');
            }
            void paragraphBreak()
            {
                mNewlines = 0;
                mPendingSpace = false;
                flushParagraph();
            }
            void pageBreak()
            {
                paragraphBreak();
                flushPage();
            }
            std::vector<std::string> finish()
            {
                paragraphBreak();
                flushPage();
                return std::move(mPages);
            }

        private:
            void flushNewlines()
            {
                if (mNewlines == 0)
                    return;
                const std::size_t n = mNewlines;
                mNewlines = 0;
                if (mCur.empty())
                    return;
                if (n >= 2)
                {
                    mPendingSpace = false;
                    flushParagraph();
                }
                else
                {
                    mPendingSpace = false;
                    if (mCur.back() != '\n')
                        mCur.push_back('\n');
                }
            }

            void flushParagraph()
            {
                while (!mCur.empty() && (mCur.back() == ' ' || mCur.back() == '\n'))
                    mCur.pop_back();
                if (mCur.empty())
                    return;
                addParagraph(mCur);
                mCur.clear();
            }

            void addParagraph(const std::string& para)
            {
                if (para.size() <= mPageChars)
                {
                    if (!mPage.empty() && mPage.size() + 2 + para.size() > mPageChars)
                        flushPage();
                    if (!mPage.empty())
                        mPage += "\n\n";
                    mPage += para;
                    return;
                }
                flushPage();
                std::size_t pos = 0;
                while (para.size() - pos > mPageChars)
                {
                    std::size_t cut = mPageChars;
                    // the last space or newline inside the limit, else a hard cut that keeps UTF-8 whole
                    std::size_t space = std::string::npos;
                    for (std::size_t i = pos + mPageChars; i > pos; --i)
                        if (para[i] == ' ' || para[i] == '\n')
                        {
                            space = i - pos;
                            break;
                        }
                    if (space != std::string::npos && space > 0)
                        cut = space;
                    else
                        while (cut > 1 && (static_cast<unsigned char>(para[pos + cut]) & 0xC0) == 0x80)
                            --cut;
                    std::string piece = para.substr(pos, cut);
                    while (!piece.empty() && (piece.back() == ' ' || piece.back() == '\n'))
                        piece.pop_back();
                    if (!piece.empty())
                        mPages.push_back(std::move(piece));
                    pos += cut;
                    while (pos < para.size() && (para[pos] == ' ' || para[pos] == '\n'))
                        ++pos;
                }
                if (pos < para.size())
                    mPage = para.substr(pos);
            }

            void flushPage()
            {
                if (!mPage.empty())
                    mPages.push_back(std::move(mPage));
                mPage.clear();
            }

            std::size_t mPageChars;
            std::string mCur;
            std::string mPage;
            std::vector<std::string> mPages;
            std::size_t mNewlines = 0;
            bool mPendingSpace = false;
        };

        // The text of an entity at raw[pos] == '&'; returns the bytes it covers (0: not an entity).
        std::size_t entity(std::string_view raw, std::size_t pos, std::string& out)
        {
            const std::size_t limit = std::min(raw.size(), pos + kMaxEntityBytes);
            std::size_t end = pos + 1;
            while (end < limit && raw[end] != ';')
                ++end;
            if (end >= limit || raw[end] != ';')
                return 0;
            const std::string_view name = raw.substr(pos + 1, end - pos - 1);
            if (name == "nbsp")
                out = " ";
            else if (name == "amp")
                out = "&";
            else if (name == "lt")
                out = "<";
            else if (name == "gt")
                out = ">";
            else if (name == "quot")
                out = "\"";
            else if (name == "apos")
                out = "'";
            else if (name.size() >= 2 && name[0] == '#')
            {
                const bool hex = name[1] == 'x' || name[1] == 'X';
                std::uint64_t value = 0;
                const std::size_t first = hex ? 2 : 1;
                if (first >= name.size())
                    return 0;
                for (std::size_t i = first; i < name.size(); ++i)
                {
                    const char c = name[i];
                    int digit;
                    if (c >= '0' && c <= '9')
                        digit = c - '0';
                    else if (hex && c >= 'a' && c <= 'f')
                        digit = c - 'a' + 10;
                    else if (hex && c >= 'A' && c <= 'F')
                        digit = c - 'A' + 10;
                    else
                        return 0;
                    value = value * (hex ? 16 : 10) + static_cast<std::uint64_t>(digit);
                    if (value > 0x10FFFF)
                        return 0;
                }
                if (value == 0 || (value >= 0xD800 && value <= 0xDFFF))
                    return 0;
                out.clear();
                appendUtf8(out, static_cast<std::uint32_t>(value));
            }
            else
                return 0;
            return end - pos + 1;
        }
    }

    std::vector<std::string> renderBookText(std::string_view raw, std::size_t pageChars)
    {
        try
        {
            if (raw.size() > kMaxBookTextBytes)
                raw = raw.substr(0, kMaxBookTextBytes);
            TextBuilder b(pageChars);
            std::string scratch;
            std::size_t i = 0;
            while (i < raw.size())
            {
                const char c = raw[i];
                const unsigned char u = static_cast<unsigned char>(c);
                if (c == '<')
                {
                    const std::size_t limit = std::min(raw.size(), i + kMaxTagBytes);
                    std::size_t end = i + 1;
                    while (end < limit && raw[end] != '>')
                        ++end;
                    if (end >= limit || raw[end] != '>')
                        break; // an unterminated tag drops the rest
                    std::size_t n = i + 1;
                    if (n < end && raw[n] == '/')
                        ++n;
                    std::size_t nameEnd = n;
                    while (nameEnd < end && std::isalnum(static_cast<unsigned char>(raw[nameEnd])) != 0)
                        ++nameEnd;
                    const std::string_view name = raw.substr(n, nameEnd - n);
                    if (iequals(name, "p") || iequals(name, "div"))
                        b.paragraphBreak();
                    else if (iequals(name, "br"))
                        b.lineBreak();
                    else if (iequals(name, "img"))
                        b.text("[picture]");
                    i = end + 1;
                    continue;
                }
                if (c == '[' && raw.size() - i >= 11 && iequals(raw.substr(i, 11), "[pagebreak]"))
                {
                    b.pageBreak();
                    i += 11;
                    continue;
                }
                if (c == '&')
                {
                    const std::size_t used = entity(raw, i, scratch);
                    if (used != 0)
                    {
                        b.text(scratch);
                        i += used;
                        continue;
                    }
                    b.put('&');
                    ++i;
                    continue;
                }
                ++i;
                if (c == '\r')
                    continue;
                if (c == '\n')
                {
                    b.rawNewline();
                    continue;
                }
                if (c == '\t')
                {
                    b.put(' ');
                    continue;
                }
                if (u < 0x20)
                    continue;
                if (u < 0x80)
                {
                    b.put(c);
                    continue;
                }
                scratch.clear();
                appendUtf8(scratch, u < 0xA0 ? (kCp1252[u - 0x80] != 0 ? kCp1252[u - 0x80] : 0xFFFD) : u);
                b.text(scratch);
            }
            return b.finish();
        }
        catch (...)
        {
            return {};
        }
    }

    // ---- state and the chunk -------------------------------------------------------------------------------------

    bool LockBooksModel::markPicked(std::uint64_t placement)
    {
        if (mPicked.count(placement) != 0 || mPicked.size() >= kMaxLocks)
            return false;
        mPicked.insert(placement);
        ++mRevision;
        return true;
    }

    bool LockBooksModel::markRead(FormId book)
    {
        if (book == kNullFormId || mRead.count(book) != 0 || mRead.size() >= kMaxBooks)
            return false;
        mRead.insert(book);
        ++mRevision;
        return true;
    }

    bool LockBooksModel::learnSpell(FormId spell)
    {
        if (spell == kNullFormId || knowsSpell(spell) || mSpells.size() >= kMaxSpells)
            return false;
        mSpells.push_back(spell);
        ++mRevision;
        return true;
    }

    bool LockBooksModel::knowsSpell(FormId spell) const
    {
        return std::find(mSpells.begin(), mSpells.end(), spell) != mSpells.end();
    }

    void LockBooksModel::clear()
    {
        mPicked.clear();
        mRead.clear();
        mSpells.clear();
        mBroken = 0;
        mRevision = 0;
    }

    void LockBooksModel::write(openeden::sdk::ChunkWriter& writer) const
    {
        writer.writeValue(static_cast<std::uint32_t>(mPicked.size()));
        for (const std::uint64_t p : mPicked)
            writer.writeValue(p);
        writer.writeValue(static_cast<std::uint32_t>(mRead.size()));
        for (const FormId b : mRead)
            writer.writeValue(static_cast<std::uint32_t>(b));
        writer.writeValue(static_cast<std::uint32_t>(mSpells.size()));
        for (const FormId s : mSpells)
            writer.writeValue(static_cast<std::uint32_t>(s));
        writer.writeValue(static_cast<std::uint32_t>(mBroken));
    }

    bool LockBooksModel::read(openeden::sdk::ChunkReader& reader, std::string& error)
    {
        std::set<std::uint64_t> picked;
        std::set<FormId> readBooks;
        std::vector<FormId> spells;
        std::uint32_t broken = 0;
        const auto refuse = [&](const char* why) {
            error = why;
            return false;
        };
        std::uint32_t n = 0;
        if (!reader.readValue(n) || n > kMaxLocks || static_cast<std::uint64_t>(n) * 8 > reader.remaining())
            return refuse("the lock list is damaged");
        std::uint64_t previous = 0;
        for (std::uint32_t i = 0; i < n; ++i)
        {
            std::uint64_t id = 0;
            if (!reader.readValue(id) || (i > 0 && id <= previous))
                return refuse("a picked lock is damaged or out of order");
            previous = id;
            picked.insert(id);
        }
        if (!reader.readValue(n) || n > kMaxBooks || static_cast<std::uint64_t>(n) * 4 > reader.remaining())
            return refuse("the book list is damaged");
        FormId previousBook = 0;
        for (std::uint32_t i = 0; i < n; ++i)
        {
            std::uint32_t id = 0;
            if (!reader.readValue(id) || id == 0 || (i > 0 && id <= previousBook))
                return refuse("a book is damaged or out of order");
            previousBook = id;
            readBooks.insert(id);
        }
        if (!reader.readValue(n) || n > kMaxSpells || static_cast<std::uint64_t>(n) * 4 > reader.remaining())
            return refuse("the spell list is damaged");
        for (std::uint32_t i = 0; i < n; ++i)
        {
            std::uint32_t id = 0;
            if (!reader.readValue(id) || id == 0 || std::find(spells.begin(), spells.end(), id) != spells.end())
                return refuse("a spell is damaged or repeated");
            spells.push_back(id);
        }
        if (!reader.readValue(broken) || reader.remaining() != 0)
            return refuse("the chunk ends wrongly");
        mPicked = std::move(picked);
        mRead = std::move(readBooks);
        mSpells = std::move(spells);
        mBroken = broken;
        ++mRevision;
        return true;
    }

    bool LockBooksChunk::read(openeden::sdk::ChunkReader& reader, std::uint32_t version)
    {
        mRefusal.clear();
        std::string why;
        if (version != kVersion)
            why = "the lockbooks chunk has version " + std::to_string(version);
        else if (mModel.read(reader, why))
            return true;
        mRefusal = why;
        if (mSink)
            mSink(why);
        return false;
    }
}
