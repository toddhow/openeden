#include "game_skyrimse/character.hpp"

#include "game_skyrimse/progression.hpp"

#include <algorithm>
#include <cctype>
#include <cstring>
#include <set>

namespace game_skyrimse::character
{
    namespace
    {
        constexpr std::uint8_t kSaveVersion = 1;

        constexpr std::array<Part, kPartCount> kParts{ Part::Hair, Part::Beard, Part::Eyes };

        // The text of a record's LString: the function resolves localised text (the table that holds a FULL is
        // .STRINGS, a DESC .DLSTRINGS; the other tables are tried when the first has nothing), a plain text is kept.
        std::string textOf(const TextFn& text, const LString& s, std::uint32_t plugin, StringTableKind first)
        {
            if (!s.mPresent)
                return std::string();
            if (!s.mLocalized)
                return s.mText;
            if (!text)
                return std::string();
            std::string out = text(s, plugin, first);
            if (!out.empty())
                return out;
            for (const StringTableKind kind :
                { StringTableKind::Strings, StringTableKind::DlStrings, StringTableKind::IlStrings })
            {
                if (kind == first)
                    continue;
                out = text(s, plugin, kind);
                if (!out.empty())
                    return out;
            }
            return std::string();
        }

        std::string lowerAscii(std::string s)
        {
            for (char& c : s)
                c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
            return s;
        }

        // true when the race is in the part's valid-race list
        bool validForRace(const ActorIndex& index, const HeadPartRecord& head, FormId race)
        {
            if (head.mValidRaces == kNullFormId)
                return false;
            const ActorFormList* list = index.formList(head.mValidRaces);
            if (list == nullptr)
                return false;
            return std::find(list->mForms.begin(), list->mForms.end(), race) != list->mForms.end();
        }

        bool playableRaceWithSkeleton(const RaceRecord& race)
        {
            return race.playable() && !race.mSkeleton[0].empty() && !race.mSkeleton[1].empty();
        }

        std::string nameOfPart(const TextFn& text, const HeadPartRecord& head)
        {
            std::string name = textOf(text, head.mName, head.mPlugin, StringTableKind::Strings);
            return name.empty() ? head.mCommon.mEditorId : name;
        }

        struct Writer
        {
            std::vector<std::uint8_t> mBytes;
            void u8(std::uint8_t v) { mBytes.push_back(v); }
            void u32(std::uint32_t v)
            {
                for (int i = 0; i < 4; ++i)
                    mBytes.push_back(static_cast<std::uint8_t>((v >> (8 * i)) & 0xFF));
            }
        };

        struct Reader
        {
            std::span<const std::uint8_t> mBytes;
            std::size_t mPos = 0;
            bool u8(std::uint8_t& v)
            {
                if (mPos + 1 > mBytes.size())
                    return false;
                v = mBytes[mPos++];
                return true;
            }
            bool u32(std::uint32_t& v)
            {
                if (mPos + 4 > mBytes.size())
                    return false;
                v = 0;
                for (int i = 0; i < 4; ++i)
                    v |= static_cast<std::uint32_t>(mBytes[mPos + i]) << (8 * i);
                mPos += 4;
                return true;
            }
            std::size_t remaining() const { return mBytes.size() - mPos; }
        };
    }

    std::uint32_t headPartType(Part part)
    {
        switch (part)
        {
            case Part::Hair:
                return 3;
            case Part::Beard:
                return 4;
            case Part::Eyes:
                return 2;
        }
        return 0;
    }

    std::string_view partName(Part part)
    {
        switch (part)
        {
            case Part::Hair:
                return "hair";
            case Part::Beard:
                return "beard";
            case Part::Eyes:
                return "eyes";
        }
        return {};
    }

    bool partFromName(std::string_view name, Part& out)
    {
        for (const Part part : kParts)
            if (partName(part) == name)
            {
                out = part;
                return true;
            }
        return false;
    }

    std::vector<RaceOption> playableRaces(const ActorIndex& index, const TextFn& text)
    {
        std::vector<RaceOption> out;
        for (const FormId id : index.raceIds())
        {
            const RaceRecord* race = index.race(id);
            if (race == nullptr || !playableRaceWithSkeleton(*race))
                continue;
            RaceOption option;
            option.mId = id;
            option.mEditorId = race->mCommon.mEditorId;
            option.mName = textOf(text, race->mName, race->mPlugin, StringTableKind::Strings);
            if (option.mName.empty())
                option.mName = option.mEditorId;
            option.mDescription = textOf(text, race->mDescription, race->mPlugin, StringTableKind::DlStrings);
            option.mBoosts = race->mSkillBoosts;
            option.mAbilities = race->mSpells;
            out.push_back(std::move(option));
        }
        std::stable_sort(out.begin(), out.end(), [](const RaceOption& a, const RaceOption& b) {
            const std::string la = lowerAscii(a.mName), lb = lowerAscii(b.mName);
            if (la != lb)
                return la < lb;
            return a.mId < b.mId;
        });
        if (out.size() > kMaxRaces)
            out.resize(kMaxRaces);
        return out;
    }

    FormId defaultPart(const ActorIndex& index, FormId raceId, bool female, Part part)
    {
        const RaceRecord* race = index.race(raceId);
        if (race == nullptr)
            return kNullFormId;
        const Sex sex = female ? Sex::Female : Sex::Male;
        const RaceHeadPart* best = nullptr;
        for (const RaceHeadPart& h : race->mHeadParts)
        {
            if (h.mSex != sex)
                continue;
            const HeadPartRecord* head = index.headPart(h.mHeadPart);
            if (head == nullptr || head->mType != headPartType(part))
                continue;
            if (best == nullptr || h.mIndex < best->mIndex)
                best = &h;
        }
        return best != nullptr ? best->mHeadPart : kNullFormId;
    }

    std::vector<PartOption> partOptions(
        const ActorIndex& index, const TextFn& text, FormId race, bool female, Part part)
    {
        std::vector<PartOption> out;
        if (index.race(race) == nullptr)
            return out;
        struct Candidate
        {
            const HeadPartRecord* mHead;
            FormId mId;
        };
        std::vector<Candidate> candidates;
        const FormId theDefault = defaultPart(index, race, female, part);
        for (const FormId id : index.headPartIds())
        {
            const HeadPartRecord* head = index.headPart(id);
            if (head == nullptr || head->mType != headPartType(part))
                continue;
            const bool isDefault = id == theDefault;
            if (!isDefault)
            {
                if (!head->playable() || head->extraPart() || !(female ? head->female() : head->male()))
                    continue;
                if (!validForRace(index, *head, race))
                    continue;
            }
            candidates.push_back({ head, id });
        }
        std::stable_sort(candidates.begin(), candidates.end(), [](const Candidate& a, const Candidate& b) {
            if (a.mHead->mCommon.mEditorId != b.mHead->mCommon.mEditorId)
                return a.mHead->mCommon.mEditorId < b.mHead->mCommon.mEditorId;
            return a.mId < b.mId;
        });
        if (part != Part::Eyes)
            out.push_back({ kNullFormId, "None" });
        for (const Candidate& c : candidates)
        {
            if (out.size() >= kMaxOptionsPerPart)
                break;
            out.push_back({ c.mId, nameOfPart(text, *c.mHead) });
        }
        return out;
    }

    Choice defaultChoice(const ActorIndex& index, FormId race, bool female)
    {
        Choice c;
        c.mRace = race;
        c.mFemale = female;
        for (const Part part : kParts)
            c.mParts[static_cast<std::size_t>(part)] = defaultPart(index, race, female, part);
        return c;
    }

    bool validChoice(const ActorIndex& index, const Choice& choice, std::string& why)
    {
        const RaceRecord* race = index.race(choice.mRace);
        if (race == nullptr)
        {
            why = "the race is not a RACE record";
            return false;
        }
        if (!race->playable())
        {
            why = "the race is not playable";
            return false;
        }
        if (race->skeleton(choice.mFemale ? Sex::Female : Sex::Male).empty())
        {
            why = "the race has no skeleton for the sex";
            return false;
        }
        if (choice.mName.size() > kMaxNameBytes)
        {
            why = "the name is too long";
            return false;
        }
        for (const Part part : kParts)
        {
            const FormId id = choice.mParts[static_cast<std::size_t>(part)];
            const auto options = partOptions(index, TextFn(), choice.mRace, choice.mFemale, part);
            const bool found
                = std::any_of(options.begin(), options.end(), [&](const PartOption& o) { return o.mId == id; });
            if (!found)
            {
                why = std::string(partName(part)) + " is not an option of this race and sex";
                return false;
            }
        }
        why.clear();
        return true;
    }

    std::string sanitiseName(std::string_view name)
    {
        std::string out;
        for (const char c : name)
        {
            const unsigned char u = static_cast<unsigned char>(c);
            if (u >= 0x20 && u < 0x7F)
                out.push_back(c);
        }
        const std::size_t first = out.find_first_not_of(' ');
        if (first == std::string::npos)
            return std::string();
        const std::size_t last = out.find_last_not_of(' ');
        out = out.substr(first, last - first + 1);
        if (out.size() > kMaxNameBytes)
            out.resize(kMaxNameBytes);
        while (!out.empty() && out.back() == ' ')
            out.pop_back();
        return out;
    }

    void applyChoice(const Choice& choice, PlayerBodyRequest& request)
    {
        request.mRace = choice.mRace;
        request.mFemale = choice.mFemale;
        request.mHeadChoice.clear();
        for (const Part part : kParts)
            request.mHeadChoice.emplace_back(headPartType(part), choice.mParts[static_cast<std::size_t>(part)]);
    }

    std::vector<std::uint8_t> encodeChoice(const Choice& choice)
    {
        Writer w;
        w.u8(kSaveVersion);
        w.u32(choice.mRace);
        w.u8(choice.mFemale ? 1 : 0);
        for (const Part part : kParts)
            w.u32(choice.mParts[static_cast<std::size_t>(part)]);
        w.u32(static_cast<std::uint32_t>(choice.mName.size()));
        w.mBytes.insert(w.mBytes.end(), choice.mName.begin(), choice.mName.end());
        return w.mBytes;
    }

    bool decodeChoice(std::span<const std::uint8_t> bytes, Choice& out, std::string& error)
    {
        Reader r{ bytes };
        std::uint8_t version = 0;
        if (!r.u8(version))
        {
            error = "the blob is empty";
            return false;
        }
        if (version != kSaveVersion)
        {
            error = "the blob has version " + std::to_string(version) + ", this build reads "
                + std::to_string(kSaveVersion);
            return false;
        }
        Choice c;
        std::uint8_t female = 0;
        std::uint32_t length = 0;
        if (!r.u32(c.mRace) || !r.u8(female) || female > 1)
        {
            error = "the blob is damaged (race or sex)";
            return false;
        }
        c.mFemale = female != 0;
        for (const Part part : kParts)
            if (!r.u32(c.mParts[static_cast<std::size_t>(part)]))
            {
                error = "the blob is damaged (parts)";
                return false;
            }
        if (!r.u32(length) || length > kMaxNameBytes || length > r.remaining())
        {
            error = "the blob is damaged (name length)";
            return false;
        }
        c.mName.assign(reinterpret_cast<const char*>(bytes.data() + r.mPos), length);
        r.mPos += length;
        if (r.remaining() != 0)
        {
            error = "the blob has trailing bytes";
            return false;
        }
        out = std::move(c);
        error.clear();
        return true;
    }

    std::string describeBoosts(const std::vector<RaceSkillBoost>& boosts)
    {
        std::string out;
        for (const RaceSkillBoost& b : boosts)
        {
            if (b.mActorValue < progression::kFirstSkillValue
                || b.mActorValue >= progression::kFirstSkillValue + progression::kSkillCount)
                continue;
            const std::string_view name = progression::skillName(b.mActorValue - progression::kFirstSkillValue);
            if (name.empty())
                continue;
            if (!out.empty())
                out += ", ";
            out += std::string(name) + " +" + std::to_string(b.mBoost);
        }
        return out;
    }

    // ---- Creator -------------------------------------------------------------------------------------------------

    Creator::Creator(const ActorIndex& index, TextFn text)
        : mIndex(index)
        , mText(std::move(text))
        , mRaces(playableRaces(index, mText))
    {
        if (mRaces.empty())
            return;
        for (std::size_t i = 0; i < mRaces.size(); ++i)
            if (mRaces[i].mId == kPlayerRace)
                mRaceIndex = i;
        mChoice = defaultChoice(mIndex, mRaces[mRaceIndex].mId, false);
        rebuildOptions();
    }

    void Creator::rebuildOptions()
    {
        for (const Part part : kParts)
            mOptions[static_cast<std::size_t>(part)] = partOptions(mIndex, mText, mChoice.mRace, mChoice.mFemale, part);
    }

    std::size_t Creator::partIndex(Part part) const
    {
        const auto& options = mOptions[static_cast<std::size_t>(part)];
        const FormId id = mChoice.mParts[static_cast<std::size_t>(part)];
        for (std::size_t i = 0; i < options.size(); ++i)
            if (options[i].mId == id)
                return i;
        return 0;
    }

    bool Creator::selectRace(std::size_t index)
    {
        if (index >= mRaces.size())
            return false;
        mRaceIndex = index;
        const std::string name = mChoice.mName;
        mChoice = defaultChoice(mIndex, mRaces[index].mId, mChoice.mFemale);
        mChoice.mName = name;
        rebuildOptions();
        ++mRevision;
        return true;
    }

    void Creator::setFemale(bool female)
    {
        if (mRaces.empty() || female == mChoice.mFemale)
            return;
        const std::string name = mChoice.mName;
        mChoice = defaultChoice(mIndex, mChoice.mRace, female);
        mChoice.mName = name;
        rebuildOptions();
        ++mRevision;
    }

    void Creator::stepPart(Part part, int delta)
    {
        const auto& options = mOptions[static_cast<std::size_t>(part)];
        if (options.empty())
            return;
        const long long n = static_cast<long long>(options.size());
        const long long step = static_cast<long long>(delta) % n;
        if (step == 0)
            return;
        const long long next = ((static_cast<long long>(partIndex(part)) + step) % n + n) % n;
        mChoice.mParts[static_cast<std::size_t>(part)] = options[static_cast<std::size_t>(next)].mId;
        ++mRevision;
    }

    void Creator::setName(std::string_view name)
    {
        std::string clean = sanitiseName(name);
        if (clean == mChoice.mName)
            return;
        mChoice.mName = std::move(clean);
        ++mRevision;
    }

    bool Creator::load(const Choice& choice, std::string& why)
    {
        if (!validChoice(mIndex, choice, why))
            return false;
        std::size_t found = mRaces.size();
        for (std::size_t i = 0; i < mRaces.size(); ++i)
            if (mRaces[i].mId == choice.mRace)
                found = i;
        if (found == mRaces.size())
        {
            why = "the race is not among the playable races";
            return false;
        }
        mRaceIndex = found;
        mChoice = choice;
        rebuildOptions();
        ++mRevision;
        return true;
    }
}
