#include "game_skyrimse/shouts.hpp"

#include "exterior_common.hpp"

#include <algorithm>
#include <cmath>

namespace game_skyrimse
{
    using detail::Fields;
    using detail::readLe;
    using detail::SubReader;

    namespace
    {
        constexpr std::size_t kSnamBytes = 12;

        std::string lowerOf(std::string_view text)
        {
            std::string out(text);
            for (char& c : out)
                if (c >= 'A' && c <= 'Z')
                    c = static_cast<char>(c + 32);
            return out;
        }

        template <class T>
        void putSorted(std::vector<T>& list, const T& record)
        {
            const auto at = std::lower_bound(list.begin(), list.end(), record.mCommon.mId,
                [](const T& r, FormId id) { return r.mCommon.mId < id; });
            if (at != list.end() && at->mCommon.mId == record.mCommon.mId)
                *at = record;
            else
                list.insert(at, record);
        }

        template <class T>
        void removeSorted(std::vector<T>& list, FormId id)
        {
            const auto at = std::lower_bound(
                list.begin(), list.end(), id, [](const T& r, FormId wanted) { return r.mCommon.mId < wanted; });
            if (at != list.end() && at->mCommon.mId == id)
                list.erase(at);
        }

        template <class T>
        const T* findSorted(const std::vector<T>& list, FormId id)
        {
            const auto at = std::lower_bound(
                list.begin(), list.end(), id, [](const T& r, FormId wanted) { return r.mCommon.mId < wanted; });
            return at != list.end() && at->mCommon.mId == id ? &*at : nullptr;
        }
    }

    bool decodeShout(std::span<const std::byte> bytes, const Content::RawRecord& record, const DecodeContext& context,
        ShoutRecord& out, std::string& why)
    {
        const Fields fields{ context, "SHOU", why };
        out = ShoutRecord();
        out.mCommon = fields.common(record);
        SubReader reader(bytes);
        std::uint32_t type = 0;
        std::span<const std::byte> data;
        int status = 0;
        while ((status = reader.next(type, data)) == 1)
        {
            switch (type)
            {
                case Content::fourCC("EDID"):
                    if (!fields.zstring(type, data, out.mCommon.mEditorId))
                        return false;
                    break;
                case Content::fourCC("FULL"):
                    if (!fields.lstring(type, data, out.mName))
                        return false;
                    break;
                case Content::fourCC("MDOB"):
                    fields.form(type, data, out.mMenuObject);
                    break;
                case Content::fourCC("SNAM"):
                {
                    if (data.size() != kSnamBytes)
                    {
                        fields.odd(type);
                        break;
                    }
                    if (out.mWordCount >= kShoutWordCount)
                    {
                        why = "SNAM: more than " + std::to_string(kShoutWordCount) + " words";
                        return false;
                    }
                    const float recovery = readLe<float>(data.data() + 8);
                    if (!std::isfinite(recovery) || recovery < 0.f)
                    {
                        why = "SNAM: the recovery time is not a finite number of seconds from 0";
                        return false;
                    }
                    ShoutWordSlot& slot = out.mWords[out.mWordCount++];
                    slot.mWord = fields.resolved(readLe<std::uint32_t>(data.data()));
                    slot.mSpell = fields.resolved(readLe<std::uint32_t>(data.data() + 4));
                    slot.mRecovery = recovery;
                    out.mHasData = true;
                    break;
                }
                default:
                    break;
            }
        }
        if (status < 0)
        {
            why = reader.message();
            return false;
        }
        return true;
    }

    bool decodeWord(std::span<const std::byte> bytes, const Content::RawRecord& record, const DecodeContext& context,
        WordRecord& out, std::string& why)
    {
        const Fields fields{ context, "WOOP", why };
        out = WordRecord();
        out.mCommon = fields.common(record);
        SubReader reader(bytes);
        std::uint32_t type = 0;
        std::span<const std::byte> data;
        int status = 0;
        while ((status = reader.next(type, data)) == 1)
        {
            switch (type)
            {
                case Content::fourCC("EDID"):
                    if (!fields.zstring(type, data, out.mCommon.mEditorId))
                        return false;
                    break;
                case Content::fourCC("FULL"):
                    if (!fields.lstring(type, data, out.mName))
                        return false;
                    break;
                default:
                    break;
            }
        }
        if (status < 0)
        {
            why = reader.message();
            return false;
        }
        return true;
    }

    void ShoutIndex::add(const std::vector<ShoutRecord>& shouts, const std::vector<WordRecord>& words)
    {
        for (const ShoutRecord& s : shouts)
        {
            if (s.mCommon.deleted())
                removeSorted(mShouts, s.mCommon.mId);
            else
                putSorted(mShouts, s);
        }
        for (const WordRecord& w : words)
        {
            if (w.mCommon.deleted())
                removeSorted(mWords, w.mCommon.mId);
            else
                putSorted(mWords, w);
        }
    }

    const ShoutRecord* ShoutIndex::shout(FormId id) const
    {
        return findSorted(mShouts, id);
    }

    const WordRecord* ShoutIndex::word(FormId id) const
    {
        return findSorted(mWords, id);
    }

    const ShoutRecord* ShoutIndex::shoutByEditorId(std::string_view editorId) const
    {
        const std::string wanted = lowerOf(editorId);
        for (const ShoutRecord& s : mShouts)
            if (lowerOf(s.mCommon.mEditorId) == wanted)
                return &s;
        return nullptr;
    }

    const WordRecord* ShoutIndex::wordByEditorId(std::string_view editorId) const
    {
        const std::string wanted = lowerOf(editorId);
        for (const WordRecord& w : mWords)
            if (lowerOf(w.mCommon.mEditorId) == wanted)
                return &w;
        return nullptr;
    }

    const ShoutRecord* ShoutIndex::shoutOfWord(FormId word) const
    {
        if (word == kNullFormId)
            return nullptr;
        for (const ShoutRecord& s : mShouts)
            for (std::size_t i = 0; i < s.mWordCount; ++i)
                if (s.mWords[i].mWord == word)
                    return &s;
        return nullptr;
    }
}

namespace game_skyrimse::shouts
{
    std::size_t wordsReached(float held, std::size_t available, const Settings& settings)
    {
        available = std::min(available, kShoutWordCount);
        if (available == 0 || !std::isfinite(held) || held < 0.f)
            return 0;
        const float minimum = std::isfinite(settings.mMinimumHold) && settings.mMinimumHold >= 0.f
            ? settings.mMinimumHold
            : Settings().mMinimumHold;
        if (held < minimum)
            return 0;
        if (!std::isfinite(settings.mWordTime) || settings.mWordTime <= 0.f)
            return 1;
        const float further = std::floor(held / settings.mWordTime);
        const std::size_t n = 1 + static_cast<std::size_t>(std::min(further, static_cast<float>(kShoutWordCount)));
        return std::min(n, available);
    }

    float timeToWord(std::size_t n, const Settings& settings)
    {
        if (n <= 1 || !std::isfinite(settings.mWordTime) || settings.mWordTime <= 0.f)
            return 0.f;
        return static_cast<float>(n - 1) * settings.mWordTime;
    }

    namespace
    {
        bool contains(const std::vector<FormId>& sorted, FormId id)
        {
            return std::binary_search(sorted.begin(), sorted.end(), id);
        }

        bool insertSorted(std::vector<FormId>& sorted, FormId id)
        {
            const auto at = std::lower_bound(sorted.begin(), sorted.end(), id);
            if (at != sorted.end() && *at == id)
                return false;
            sorted.insert(at, id);
            return true;
        }

        // Reads a count and that many ascending, non-zero ids; an empty string is success.
        std::string readIds(openeden::sdk::ChunkReader& reader, std::vector<FormId>& out, const char* what)
        {
            std::uint32_t count = 0;
            if (!reader.readValue(count))
                return std::string("the shouts chunk is cut short before the ") + what + " count";
            if (count > kMaxWords || static_cast<std::uint64_t>(count) * 4 > reader.remaining())
                return std::string("the shouts chunk has a ") + what + " count that does not fit";
            out.reserve(count);
            for (std::uint32_t i = 0; i < count; ++i)
            {
                std::uint32_t id = 0;
                if (!reader.readValue(id))
                    return std::string("the shouts chunk is cut short in the ") + what + " ids";
                if (id == 0 || (!out.empty() && id <= out.back()))
                    return std::string("the shouts chunk has ") + what + " ids that are not ascending";
                out.push_back(id);
            }
            return {};
        }
    }

    bool ShoutState::learn(FormId word)
    {
        if (word == kNullFormId || mLearned.size() >= kMaxWords || !insertSorted(mLearned, word))
            return false;
        ++mRevision;
        return true;
    }

    bool ShoutState::unlock(FormId word)
    {
        if (!contains(mLearned, word) || contains(mUnlocked, word) || mSouls == 0 || mUnlocked.size() >= kMaxWords)
            return false;
        --mSouls;
        (void)insertSorted(mUnlocked, word);
        ++mRevision;
        return true;
    }

    bool ShoutState::forceUnlock(FormId word)
    {
        if (word == kNullFormId || contains(mUnlocked, word) || mUnlocked.size() >= kMaxWords)
            return false;
        (void)learn(word);
        if (!contains(mLearned, word))
            return false; // the learned list is full
        (void)insertSorted(mUnlocked, word);
        ++mRevision;
        return true;
    }

    bool ShoutState::learned(FormId word) const
    {
        return contains(mLearned, word);
    }

    bool ShoutState::unlocked(FormId word) const
    {
        return contains(mUnlocked, word);
    }

    void ShoutState::addSouls(std::uint32_t count)
    {
        const std::uint64_t sum = static_cast<std::uint64_t>(mSouls) + count;
        const std::uint32_t next = static_cast<std::uint32_t>(std::min<std::uint64_t>(sum, kMaxSouls));
        if (next == mSouls)
            return;
        mSouls = next;
        ++mRevision;
    }

    void ShoutState::equip(FormId shout)
    {
        if (shout == mEquipped)
            return;
        mEquipped = shout;
        ++mRevision;
    }

    std::size_t ShoutState::availableWords(const ShoutRecord& shout) const
    {
        std::size_t n = 0;
        while (n < shout.mWordCount && n < kShoutWordCount && contains(mUnlocked, shout.mWords[n].mWord))
            ++n;
        return n;
    }

    void ShoutState::startCooldown(float seconds)
    {
        if (!std::isfinite(seconds) || !(seconds > 0.f))
            return;
        mCooldown = seconds;
        mCooldownTotal = seconds;
        ++mRevision;
    }

    void ShoutState::tick(float seconds)
    {
        if (!std::isfinite(seconds) || !(seconds > 0.f) || mCooldown <= 0.f)
            return;
        mCooldown = std::max(0.f, mCooldown - seconds);
        if (mCooldown <= 0.f)
            ++mRevision;
    }

    void ShoutState::write(openeden::sdk::ChunkWriter& writer) const
    {
        writer.writeValue(static_cast<std::uint32_t>(mLearned.size()));
        for (const FormId id : mLearned)
            writer.writeValue(static_cast<std::uint32_t>(id));
        writer.writeValue(static_cast<std::uint32_t>(mUnlocked.size()));
        for (const FormId id : mUnlocked)
            writer.writeValue(static_cast<std::uint32_t>(id));
        writer.writeValue(mSouls);
        writer.writeValue(static_cast<std::uint32_t>(mEquipped));
        writer.writeValue(mCooldown);
        writer.writeValue(mCooldownTotal);
    }

    bool ShoutState::read(openeden::sdk::ChunkReader& reader, std::uint32_t version, std::string& error)
    {
        if (version != ShoutChunk::kVersion)
        {
            error = "the shouts chunk has version " + std::to_string(version);
            return false;
        }
        std::vector<FormId> learned, unlocked;
        error = readIds(reader, learned, "learned");
        if (error.empty())
            error = readIds(reader, unlocked, "unlocked");
        if (!error.empty())
            return false;
        for (const FormId id : unlocked)
            if (!contains(learned, id))
            {
                error = "the shouts chunk unlocks a word that is not learned";
                return false;
            }
        std::uint32_t souls = 0, equipped = 0;
        float cooldown = 0.f, total = 0.f;
        if (reader.remaining() != 16 || !reader.readValue(souls) || !reader.readValue(equipped)
            || !reader.readValue(cooldown) || !reader.readValue(total))
        {
            error = "the shouts chunk's tail is not 16 bytes";
            return false;
        }
        if (souls > kMaxSouls)
        {
            error = "the shouts chunk has " + std::to_string(souls) + " souls";
            return false;
        }
        if (!std::isfinite(cooldown) || !std::isfinite(total) || cooldown < 0.f || total < 0.f || cooldown > total)
        {
            error = "the shouts chunk's cooldown is not finite or not within its total";
            return false;
        }
        mLearned = std::move(learned);
        mUnlocked = std::move(unlocked);
        mSouls = souls;
        mEquipped = equipped;
        mCooldown = cooldown;
        mCooldownTotal = total;
        ++mRevision;
        return true;
    }

    bool ShoutChunk::read(openeden::sdk::ChunkReader& reader, std::uint32_t version)
    {
        std::string error;
        if (mState.read(reader, version, error))
            return true;
        if (mSink)
            mSink(error);
        return false;
    }

    std::string prettyShoutName(std::string_view editorId)
    {
        std::string base(editorId);
        const auto stripDigits = [&] {
            std::size_t n = base.size();
            while (n > 0 && base[n - 1] >= '0' && base[n - 1] <= '9')
                --n;
            if (n > 0)
                base.resize(n);
        };
        stripDigits();
        constexpr std::string_view kSuffix = "Shout";
        if (base.size() > kSuffix.size() && base.compare(base.size() - kSuffix.size(), kSuffix.size(), kSuffix) == 0)
            base.resize(base.size() - kSuffix.size());
        stripDigits();
        std::string out;
        for (std::size_t i = 0; i < base.size(); ++i)
        {
            const char c = base[i];
            if (i > 0 && c >= 'A' && c <= 'Z' && !(base[i - 1] >= 'A' && base[i - 1] <= 'Z'))
                out.push_back(' ');
            out.push_back(c);
        }
        return out;
    }

    // ---- task 0253: announcements (not saved)

    void ShoutState::announce(std::string text)
    {
        if (text.empty())
            return;
        if (text.size() > kMaxNoticeBytes)
            text.resize(kMaxNoticeBytes);
        if (mNotices.size() < kMaxNotices)
            mNotices.resize(kMaxNotices);
        mNotices[static_cast<std::size_t>(mNoticeCount % kMaxNotices)] = std::move(text);
        ++mNoticeCount;
    }

    std::string ShoutState::noticeAt(std::uint64_t number) const
    {
        if (number < 1 || number > mNoticeCount || mNoticeCount - number >= kMaxNotices)
            return {};
        return mNotices[static_cast<std::size_t>((number - 1) % kMaxNotices)];
    }
}
