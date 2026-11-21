#include "neutralsave.hpp"

#include <algorithm>
#include <bit>
#include <cmath>
#include <cstring>
#include <stdexcept>
#include <tuple>

#include <openeden/game/world_model.hpp>
#include <openeden/game/world_source_service.hpp>

// The bytes of a neutral save (task 0177). Layout, all little endian:
//   magic "NWSV", u32 version, cell key of the player, u32 cell count, cells, u32 unlocked count, entries,
//   u32 key count, entries, and in version 2 (task 0202) u32 next created index; in version 3 (task 0263) after it
//   u32 pose count, poses: string game id, u32 placement, u8 bone count, bones: string name, 3 f32 position, 4 f32
//   rotation; in version 4 (task 0286) the pose count is always there (it may be 0) and after the poses u32 moved
//   count (at least 1), moved placements: string game id, u32 placement, origin cell key, destination cell key, 3 f32
//   position, 3 f32 rotation
// cell key: u8 kind; for a kind other than None: string game id, then Interior: u32 cell; Exterior: u32 world, i32 x,
//   i32 y. string: u16 length, bytes.
// cell: key, u32 object count, objects. object: u32 placement, u8 flags, then Moved: 6 floats (position, rotation),
//   Scaled: 1 float, Created (version 2): u32 base, u8 kind, string editor id, string model path, u8 has bounds,
//   [6 floats: min, max]. entry: string game id, u32 id.

namespace MWWorld
{
    namespace
    {
        constexpr char kMagic[4] = { 'N', 'W', 'S', 'V' };
        constexpr std::uint8_t kKnownFlags = NeutralSaveMoved | NeutralSaveScaled | NeutralSaveEnabledChanged
            | NeutralSaveEnabled | NeutralSaveDeleted;
        constexpr std::uint8_t kKnownFlagsV2 = kKnownFlags | NeutralSaveCreated;
        constexpr std::uint8_t kMaxObjectKind = static_cast<std::uint8_t>(openeden::sdk::game::ObjectKind::Other);
        constexpr std::uint32_t kCreatedBase = static_cast<std::uint32_t>(openeden::sdk::game::kCreatedObjectBase);

        /// Why a moved-placement entry (version 4) cannot be written or read; empty when it is fine.
        std::string relocationProblem(const NeutralRelocation& moved)
        {
            if (moved.mGameId.empty() || moved.mGameId.size() > kNeutralSaveMaxString)
                return "a moved placement has no game id or a too long one";
            if (moved.mOrigin.mKind == NeutralSaveCellKey::Kind::None
                || moved.mDestination.mKind == NeutralSaveCellKey::Kind::None)
                return "a moved placement has no cell";
            if (moved.mOrigin.mGameId != moved.mGameId || moved.mDestination.mGameId != moved.mGameId)
                return "a moved placement names a cell of another game";
            if (moved.mOrigin == moved.mDestination)
                return "a moved placement stands in the cell it belongs to";
            for (int i = 0; i < 3; ++i)
                if (!std::isfinite(moved.mPos[i]) || !std::isfinite(moved.mRot[i]))
                    return "a moved placement has a position that is not finite";
            return {};
        }

        /// Why a created entry cannot be written or read; empty when it is fine. The counter is the state's.
        std::string createdProblem(const NeutralSavedObject& o, std::uint32_t nextIndex)
        {
            if ((o.mFlags & NeutralSaveCreated) == 0)
                return o.mCreated.has_value()
                    ? "an object that was not created carries a base description"
                    : (o.mPlacement >= kCreatedBase ? "a placement of the pack is in the range of created ones" : "");
            if (!o.mCreated.has_value())
                return "a created object has no base description";
            if ((o.mFlags & NeutralSaveMoved) == 0)
                return "a created object is saved with no position";
            if ((o.mFlags & NeutralSaveDeleted) != 0)
                return "a created object is saved as deleted";
            if (o.mPlacement < kCreatedBase)
                return "a created object has an id outside the created range";
            if (o.mPlacement - kCreatedBase >= nextIndex)
                return "a created object has an id the counter has not reached";
            if (o.mCreated->mKind > kMaxObjectKind)
                return "a created object has an unknown kind";
            return {};
        }

        class Writer
        {
        public:
            std::vector<std::byte> mBytes;

            void u8(std::uint8_t v) { mBytes.push_back(static_cast<std::byte>(v)); }
            void u16(std::uint16_t v)
            {
                u8(static_cast<std::uint8_t>(v));
                u8(static_cast<std::uint8_t>(v >> 8));
            }
            void u32(std::uint32_t v)
            {
                for (int i = 0; i < 4; ++i)
                    u8(static_cast<std::uint8_t>(v >> (8 * i)));
            }
            void i32(std::int32_t v) { u32(static_cast<std::uint32_t>(v)); }
            void f32(float v) { u32(std::bit_cast<std::uint32_t>(v)); }
            void string(const std::string& s)
            {
                if (s.size() > kNeutralSaveMaxString)
                    throw std::length_error(
                        "a neutral save string is longer than " + std::to_string(kNeutralSaveMaxString));
                u16(static_cast<std::uint16_t>(s.size()));
                for (char c : s)
                    u8(static_cast<std::uint8_t>(c));
            }
            void count(std::size_t n, std::size_t cap, const char* what)
            {
                if (n > cap)
                    throw std::length_error(std::string("a neutral save holds too many ") + what);
                u32(static_cast<std::uint32_t>(n));
            }
            void key(const NeutralSaveCellKey& k)
            {
                u8(static_cast<std::uint8_t>(k.mKind));
                if (k.mKind == NeutralSaveCellKey::Kind::None)
                    return;
                string(k.mGameId);
                if (k.mKind == NeutralSaveCellKey::Kind::Interior)
                    u32(k.mCell);
                else
                {
                    u32(k.mWorld);
                    i32(k.mX);
                    i32(k.mY);
                }
            }
        };

        class Reader
        {
        public:
            explicit Reader(std::span<const std::byte> bytes)
                : mBytes(bytes)
            {
            }

            std::string mError;

            bool fail(const std::string& what)
            {
                if (mError.empty())
                    mError = what + " (at byte " + std::to_string(mPos) + ")";
                return false;
            }
            std::size_t left() const { return mBytes.size() - mPos; }
            bool atEnd() const { return mPos == mBytes.size(); }

            bool u8(std::uint8_t& v)
            {
                if (left() < 1)
                    return fail("the save ends early");
                v = static_cast<std::uint8_t>(mBytes[mPos++]);
                return true;
            }
            bool u16(std::uint16_t& v)
            {
                if (left() < 2)
                    return fail("the save ends early");
                v = static_cast<std::uint16_t>(
                    static_cast<std::uint8_t>(mBytes[mPos]) | static_cast<std::uint8_t>(mBytes[mPos + 1]) << 8);
                mPos += 2;
                return true;
            }
            bool u32(std::uint32_t& v)
            {
                if (left() < 4)
                    return fail("the save ends early");
                v = 0;
                for (int i = 0; i < 4; ++i)
                    v |= static_cast<std::uint32_t>(static_cast<std::uint8_t>(mBytes[mPos + i])) << (8 * i);
                mPos += 4;
                return true;
            }
            bool i32(std::int32_t& v)
            {
                std::uint32_t u = 0;
                if (!u32(u))
                    return false;
                v = static_cast<std::int32_t>(u);
                return true;
            }
            bool f32(float& v)
            {
                std::uint32_t u = 0;
                if (!u32(u))
                    return false;
                v = std::bit_cast<float>(u);
                if (!std::isfinite(v))
                    return fail("a number in the save is not finite");
                return true;
            }
            bool string(std::string& s)
            {
                std::uint16_t n = 0;
                if (!u16(n))
                    return false;
                if (n > kNeutralSaveMaxString)
                    return fail("a string in the save is too long");
                if (left() < n)
                    return fail("the save ends inside a string");
                s.assign(reinterpret_cast<const char*>(mBytes.data() + mPos), n);
                mPos += n;
                return true;
            }
            /// A count, bounded by the cap and by the bytes that could still hold that many items of `minItemBytes`.
            bool count(std::size_t& n, std::size_t cap, std::size_t minItemBytes, const char* what)
            {
                std::uint32_t v = 0;
                if (!u32(v))
                    return false;
                if (v > cap)
                    return fail(std::string("too many ") + what);
                if (static_cast<std::size_t>(v) * minItemBytes > left())
                    return fail(std::string("the save cannot hold the ") + what + " it announces");
                n = v;
                return true;
            }
            bool key(NeutralSaveCellKey& k, bool allowNone)
            {
                std::uint8_t kind = 0;
                if (!u8(kind))
                    return false;
                if (kind > static_cast<std::uint8_t>(NeutralSaveCellKey::Kind::Exterior))
                    return fail("an unknown cell kind");
                k = {};
                k.mKind = static_cast<NeutralSaveCellKey::Kind>(kind);
                if (k.mKind == NeutralSaveCellKey::Kind::None)
                    return allowNone || fail("a cell with no kind");
                if (!string(k.mGameId))
                    return false;
                if (k.mGameId.empty())
                    return fail("a cell with no game id");
                if (k.mKind == NeutralSaveCellKey::Kind::Interior)
                    return u32(k.mCell);
                return u32(k.mWorld) && i32(k.mX) && i32(k.mY);
            }
            bool entries(std::vector<std::pair<std::string, std::uint32_t>>& out, const char* what)
            {
                std::size_t n = 0;
                if (!count(n, kNeutralSaveMaxEntries, 6, what))
                    return false;
                out.reserve(n);
                for (std::size_t i = 0; i < n; ++i)
                {
                    std::pair<std::string, std::uint32_t> e;
                    if (!string(e.first) || !u32(e.second))
                        return false;
                    if (!out.empty() && !(out.back() < e))
                        return fail(std::string("the ") + what + " are not in order");
                    out.push_back(std::move(e));
                }
                return true;
            }

        private:
            std::span<const std::byte> mBytes;
            std::size_t mPos = 0;
        };

        void writeEntries(
            Writer& w, const std::vector<std::pair<std::string, std::uint32_t>>& entries, const char* what)
        {
            w.count(entries.size(), kNeutralSaveMaxEntries, what);
            for (const auto& [game, id] : entries)
            {
                w.string(game);
                w.u32(id);
            }
        }
    }

    bool operator<(const NeutralSaveCellKey& a, const NeutralSaveCellKey& b)
    {
        const auto ta = std::tie(a.mKind, a.mGameId, a.mCell, a.mWorld, a.mX, a.mY);
        const auto tb = std::tie(b.mKind, b.mGameId, b.mCell, b.mWorld, b.mX, b.mY);
        return ta < tb;
    }

    void canonicaliseNeutralSave(NeutralSaveState& state)
    {
        for (NeutralSavedCell& cell : state.mCells)
        {
            std::stable_sort(cell.mObjects.begin(), cell.mObjects.end(),
                [](const NeutralSavedObject& a, const NeutralSavedObject& b) { return a.mPlacement < b.mPlacement; });
            cell.mObjects.erase(std::unique(cell.mObjects.begin(), cell.mObjects.end(),
                                    [](const NeutralSavedObject& a, const NeutralSavedObject& b) {
                                        return a.mPlacement == b.mPlacement;
                                    }),
                cell.mObjects.end());
        }
        std::stable_sort(state.mCells.begin(), state.mCells.end(),
            [](const NeutralSavedCell& a, const NeutralSavedCell& b) { return a.mKey < b.mKey; });
        state.mCells.erase(std::unique(state.mCells.begin(), state.mCells.end(),
                               [](const NeutralSavedCell& a, const NeutralSavedCell& b) { return a.mKey == b.mKey; }),
            state.mCells.end());
        std::stable_sort(state.mCorpsePoses.begin(), state.mCorpsePoses.end(),
            [](const NeutralCorpsePose& a, const NeutralCorpsePose& b) {
                return std::tie(a.mGameId, a.mPlacement) < std::tie(b.mGameId, b.mPlacement);
            });
        state.mCorpsePoses.erase(std::unique(state.mCorpsePoses.begin(), state.mCorpsePoses.end(),
                                     [](const NeutralCorpsePose& a, const NeutralCorpsePose& b) {
                                         return a.mGameId == b.mGameId && a.mPlacement == b.mPlacement;
                                     }),
            state.mCorpsePoses.end());
        std::stable_sort(state.mRelocations.begin(), state.mRelocations.end(),
            [](const NeutralRelocation& a, const NeutralRelocation& b) {
                return std::tie(a.mGameId, a.mPlacement) < std::tie(b.mGameId, b.mPlacement);
            });
        state.mRelocations.erase(std::unique(state.mRelocations.begin(), state.mRelocations.end(),
                                     [](const NeutralRelocation& a, const NeutralRelocation& b) {
                                         return a.mGameId == b.mGameId && a.mPlacement == b.mPlacement;
                                     }),
            state.mRelocations.end());
        for (auto* entries : { &state.mUnlocked, &state.mKeys })
        {
            std::sort(entries->begin(), entries->end());
            entries->erase(std::unique(entries->begin(), entries->end()), entries->end());
        }
    }

    std::vector<std::byte> encodeNeutralSave(NeutralSaveState state)
    {
        canonicaliseNeutralSave(state);
        std::size_t created = 0;
        for (const NeutralSavedCell& cell : state.mCells)
            for (const NeutralSavedObject& o : cell.mObjects)
            {
                if (const std::string problem = createdProblem(o, state.mNextCreatedIndex); !problem.empty())
                    throw std::invalid_argument("a neutral save cannot be written: " + problem);
                if ((o.mFlags & NeutralSaveCreated) != 0)
                    ++created;
            }
        if (created > kNeutralSaveMaxCreated)
            throw std::length_error("a neutral save holds too many created objects");
        // Version 1 bytes unless something was ever created: every other save is what it always was
        const bool poses = !state.mCorpsePoses.empty();
        if (state.mCorpsePoses.size() > kNeutralSaveMaxPoses)
            throw std::length_error("a neutral save holds too many corpse poses");
        for (const NeutralCorpsePose& pose : state.mCorpsePoses)
        {
            if (pose.mGameId.empty() || pose.mBones.empty() || pose.mBones.size() > kNeutralSaveMaxPoseBones)
                throw std::invalid_argument(
                    "a neutral save cannot be written: a corpse pose has no game id or a bad bone count");
            for (const NeutralCorpseBone& bone : pose.mBones)
                if (bone.mName.empty() || bone.mName.size() > kNeutralSaveMaxPoseName)
                    throw std::invalid_argument(
                        "a neutral save cannot be written: a corpse pose bone has an empty or too long name");
        }
        if (state.mRelocations.size() > kNeutralSaveMaxRelocations)
            throw std::length_error("a neutral save holds too many moved placements");
        for (const NeutralRelocation& moved : state.mRelocations)
            if (const std::string problem = relocationProblem(moved); !problem.empty())
                throw std::invalid_argument("a neutral save cannot be written: " + problem);
        const bool relocations = !state.mRelocations.empty();
        const bool version2 = created != 0 || state.mNextCreatedIndex != 0 || poses || relocations;
        Writer w;
        for (char c : kMagic)
            w.u8(static_cast<std::uint8_t>(c));
        w.u32(relocations  ? kNeutralSaveVersionRelocations
                : poses    ? kNeutralSaveVersionPoses
                : version2 ? kNeutralSaveVersionCreated
                           : kNeutralSaveVersion);
        w.key(state.mPlayerCell);
        w.count(state.mCells.size(), kNeutralSaveMaxCells, "cells");
        for (const NeutralSavedCell& cell : state.mCells)
        {
            w.key(cell.mKey);
            w.count(cell.mObjects.size(), kNeutralSaveMaxObjects, "objects");
            for (const NeutralSavedObject& o : cell.mObjects)
            {
                w.u32(o.mPlacement);
                w.u8(o.mFlags);
                if ((o.mFlags & NeutralSaveMoved) != 0)
                {
                    for (float f : o.mPos)
                        w.f32(f);
                    for (float f : o.mRot)
                        w.f32(f);
                }
                if ((o.mFlags & NeutralSaveScaled) != 0)
                    w.f32(o.mScale);
                if ((o.mFlags & NeutralSaveCreated) != 0)
                {
                    const NeutralCreatedInfo& info = *o.mCreated;
                    w.u32(info.mBase);
                    w.u8(info.mKind);
                    w.string(info.mEditorId);
                    w.string(info.mModel);
                    w.u8(info.mHasBounds ? 1 : 0);
                    if (info.mHasBounds)
                    {
                        for (float f : info.mBoundsMin)
                            w.f32(f);
                        for (float f : info.mBoundsMax)
                            w.f32(f);
                    }
                }
            }
        }
        writeEntries(w, state.mUnlocked, "opened locks");
        writeEntries(w, state.mKeys, "keys");
        if (version2)
            w.u32(state.mNextCreatedIndex);
        if (poses || relocations)
        {
            w.u32(static_cast<std::uint32_t>(state.mCorpsePoses.size()));
            for (const NeutralCorpsePose& pose : state.mCorpsePoses)
            {
                w.string(pose.mGameId);
                w.u32(pose.mPlacement);
                w.u8(static_cast<std::uint8_t>(pose.mBones.size()));
                for (const NeutralCorpseBone& bone : pose.mBones)
                {
                    w.string(bone.mName);
                    for (float f : bone.mPos)
                        w.f32(f);
                    for (float f : bone.mRot)
                        w.f32(f);
                }
            }
        }
        if (relocations)
        {
            w.u32(static_cast<std::uint32_t>(state.mRelocations.size()));
            for (const NeutralRelocation& moved : state.mRelocations)
            {
                w.string(moved.mGameId);
                w.u32(moved.mPlacement);
                w.key(moved.mOrigin);
                w.key(moved.mDestination);
                for (float f : moved.mPos)
                    w.f32(f);
                for (float f : moved.mRot)
                    w.f32(f);
            }
        }
        if (w.mBytes.size() > kNeutralSaveMaxBytes)
            throw std::length_error("a neutral save is larger than " + std::to_string(kNeutralSaveMaxBytes) + " bytes");
        return std::move(w.mBytes);
    }

    bool decodeNeutralSave(std::span<const std::byte> bytes, NeutralSaveState& out, std::string& error)
    {
        out = {};
        error.clear();
        Reader r(bytes);
        std::uint32_t version = 0;
        const auto fail = [&]() {
            out = {};
            error = r.mError.empty() ? "malformed neutral save" : r.mError;
            if (version == kNeutralSaveVersionRelocations)
                error = "neutral save version " + std::to_string(version) + ": " + error;
            return false;
        };
        if (bytes.size() > kNeutralSaveMaxBytes)
        {
            r.fail("the neutral save is too large");
            return fail();
        }
        NeutralSaveState state;
        for (char expected : kMagic)
        {
            std::uint8_t c = 0;
            if (!r.u8(c))
                return fail();
            if (c != static_cast<std::uint8_t>(expected))
            {
                r.fail("not a neutral save (wrong magic)");
                return fail();
            }
        }
        if (!r.u32(version))
            return fail();
        if (version != kNeutralSaveVersion && version != kNeutralSaveVersionCreated
            && version != kNeutralSaveVersionPoses && version != kNeutralSaveVersionRelocations)
        {
            r.fail("the neutral save has version " + std::to_string(version) + ", this build reads versions "
                + std::to_string(kNeutralSaveVersion) + " to " + std::to_string(kNeutralSaveVersionRelocations));
            return fail();
        }
        const bool version4 = version == kNeutralSaveVersionRelocations;
        const bool version3 = version == kNeutralSaveVersionPoses || version4;
        const bool version2 = version == kNeutralSaveVersionCreated || version3;
        std::size_t createdCount = 0;
        if (!r.key(state.mPlayerCell, true))
            return fail();
        std::size_t cells = 0;
        if (!r.count(cells, kNeutralSaveMaxCells, 5, "cells"))
            return fail();
        state.mCells.reserve(cells);
        for (std::size_t i = 0; i < cells; ++i)
        {
            NeutralSavedCell cell;
            if (!r.key(cell.mKey, false))
                return fail();
            if (!state.mCells.empty() && !(state.mCells.back().mKey < cell.mKey))
            {
                r.fail("the cells are not in order");
                return fail();
            }
            std::size_t objects = 0;
            if (!r.count(objects, kNeutralSaveMaxObjects, 5, "objects"))
                return fail();
            cell.mObjects.reserve(objects);
            for (std::size_t j = 0; j < objects; ++j)
            {
                NeutralSavedObject o;
                if (!r.u32(o.mPlacement) || !r.u8(o.mFlags))
                    return fail();
                if ((o.mFlags & ~(version2 ? kKnownFlagsV2 : kKnownFlags)) != 0
                    || ((o.mFlags & NeutralSaveEnabled) != 0 && (o.mFlags & NeutralSaveEnabledChanged) == 0))
                {
                    r.fail("an object has flags that cannot go together");
                    return fail();
                }
                if ((o.mFlags & NeutralSaveMoved) != 0)
                {
                    for (float& f : o.mPos)
                        if (!r.f32(f))
                            return fail();
                    for (float& f : o.mRot)
                        if (!r.f32(f))
                            return fail();
                }
                if ((o.mFlags & NeutralSaveScaled) != 0 && !r.f32(o.mScale))
                    return fail();
                if ((o.mFlags & NeutralSaveCreated) != 0)
                {
                    NeutralCreatedInfo info;
                    std::uint8_t hasBounds = 0;
                    if (!r.u32(info.mBase) || !r.u8(info.mKind) || !r.string(info.mEditorId) || !r.string(info.mModel)
                        || !r.u8(hasBounds))
                        return fail();
                    if (hasBounds > 1)
                    {
                        r.fail("a created object has a bounds flag that is not 0 or 1");
                        return fail();
                    }
                    info.mHasBounds = hasBounds == 1;
                    if (info.mHasBounds)
                    {
                        for (float& f : info.mBoundsMin)
                            if (!r.f32(f))
                                return fail();
                        for (float& f : info.mBoundsMax)
                            if (!r.f32(f))
                                return fail();
                    }
                    if (++createdCount > kNeutralSaveMaxCreated)
                    {
                        r.fail("too many created objects");
                        return fail();
                    }
                    o.mCreated = std::move(info);
                }
                if (!cell.mObjects.empty() && cell.mObjects.back().mPlacement >= o.mPlacement)
                {
                    r.fail("the objects of a cell are not in order");
                    return fail();
                }
                cell.mObjects.push_back(o);
            }
            state.mCells.push_back(std::move(cell));
        }
        if (!r.entries(state.mUnlocked, "opened locks") || !r.entries(state.mKeys, "keys"))
            return fail();
        if (version2)
        {
            if (!r.u32(state.mNextCreatedIndex))
                return fail();
            if (state.mNextCreatedIndex > openeden::sdk::game::kMaxCreatedObjects + 1u)
            {
                r.fail("the counter of created objects is past its range");
                return fail();
            }
            for (const NeutralSavedCell& cell : state.mCells)
                for (const NeutralSavedObject& o : cell.mObjects)
                    if (const std::string problem = createdProblem(o, state.mNextCreatedIndex); !problem.empty())
                    {
                        r.fail(problem);
                        return fail();
                    }
            if (!version3 && createdCount == 0 && state.mNextCreatedIndex == 0)
            {
                r.fail("a version 2 save with nothing created and no counter is not canonical");
                return fail();
            }
        }
        if (version3)
        {
            std::size_t poses = 0;
            if (!r.count(poses, kNeutralSaveMaxPoses, 11, "corpse poses"))
                return fail();
            if (poses == 0 && !version4)
            {
                r.fail("a version 3 save with no corpse pose is not canonical");
                return fail();
            }
            state.mCorpsePoses.reserve(poses);
            for (std::size_t i = 0; i < poses; ++i)
            {
                NeutralCorpsePose pose;
                std::uint8_t bones = 0;
                if (!r.string(pose.mGameId) || !r.u32(pose.mPlacement) || !r.u8(bones))
                    return fail();
                if (pose.mGameId.empty() || bones == 0 || bones > kNeutralSaveMaxPoseBones)
                {
                    r.fail("a corpse pose has no game id or a bad bone count");
                    return fail();
                }
                if (!state.mCorpsePoses.empty()
                    && !(std::tie(state.mCorpsePoses.back().mGameId, state.mCorpsePoses.back().mPlacement)
                        < std::tie(pose.mGameId, pose.mPlacement)))
                {
                    r.fail("the corpse poses are not in order");
                    return fail();
                }
                pose.mBones.reserve(bones);
                for (std::uint8_t b = 0; b < bones; ++b)
                {
                    NeutralCorpseBone bone;
                    if (!r.string(bone.mName))
                        return fail();
                    if (bone.mName.empty() || bone.mName.size() > kNeutralSaveMaxPoseName)
                    {
                        r.fail("a corpse pose bone has an empty or too long name");
                        return fail();
                    }
                    for (float& f : bone.mPos)
                        if (!r.f32(f))
                            return fail();
                    for (float& f : bone.mRot)
                        if (!r.f32(f))
                            return fail();
                    pose.mBones.push_back(std::move(bone));
                }
                state.mCorpsePoses.push_back(std::move(pose));
            }
        }
        if (version4)
        {
            std::size_t moves = 0;
            if (!r.count(moves, kNeutralSaveMaxRelocations, 40, "moved placements"))
                return fail();
            if (moves == 0)
            {
                r.fail("a version 4 save with no moved placement is not canonical");
                return fail();
            }
            state.mRelocations.reserve(moves);
            for (std::size_t i = 0; i < moves; ++i)
            {
                NeutralRelocation moved;
                if (!r.string(moved.mGameId) || !r.u32(moved.mPlacement) || !r.key(moved.mOrigin, false)
                    || !r.key(moved.mDestination, false))
                    return fail();
                for (float& f : moved.mPos)
                    if (!r.f32(f))
                        return fail();
                for (float& f : moved.mRot)
                    if (!r.f32(f))
                        return fail();
                if (const std::string problem = relocationProblem(moved); !problem.empty())
                {
                    r.fail(problem);
                    return fail();
                }
                if (!state.mRelocations.empty()
                    && !(std::tie(state.mRelocations.back().mGameId, state.mRelocations.back().mPlacement)
                        < std::tie(moved.mGameId, moved.mPlacement)))
                {
                    r.fail("the moved placements are not in order");
                    return fail();
                }
                state.mRelocations.push_back(std::move(moved));
            }
        }
        if (!r.atEnd())
        {
            r.fail("there are bytes after the end of the neutral save");
            return fail();
        }
        out = std::move(state);
        return true;
    }

    std::optional<NeutralSavedObject> diffNeutralObject(
        std::uint32_t placement, const ESM::CellRef& original, bool originallyEnabled, const NeutralObjectNow& now)
    {
        NeutralSavedObject saved;
        saved.mPlacement = placement;
        if (now.mDeleted)
        {
            saved.mFlags = NeutralSaveDeleted;
            return saved;
        }
        bool moved = false;
        for (int i = 0; i < 3; ++i)
        {
            moved
                = moved || now.mPosition.pos[i] != original.mPos.pos[i] || now.mPosition.rot[i] != original.mPos.rot[i];
            saved.mPos[i] = now.mPosition.pos[i];
            saved.mRot[i] = now.mPosition.rot[i];
        }
        if (moved)
            saved.mFlags |= NeutralSaveMoved;
        else
        {
            std::fill(std::begin(saved.mPos), std::end(saved.mPos), 0.f);
            std::fill(std::begin(saved.mRot), std::end(saved.mRot), 0.f);
        }
        if (now.mScale != original.mScale)
        {
            saved.mFlags |= NeutralSaveScaled;
            saved.mScale = now.mScale;
        }
        if (now.mEnabled != originallyEnabled)
        {
            saved.mFlags |= NeutralSaveEnabledChanged;
            if (now.mEnabled)
                saved.mFlags |= NeutralSaveEnabled;
        }
        if (saved.mFlags == 0)
            return std::nullopt;
        return saved;
    }

    NeutralSavedObject saveCreatedNeutralObject(
        std::uint32_t placement, const NeutralCreatedInfo& info, const NeutralObjectNow& now)
    {
        NeutralSavedObject saved;
        saved.mPlacement = placement;
        saved.mFlags = NeutralSaveCreated | NeutralSaveMoved;
        for (int i = 0; i < 3; ++i)
        {
            saved.mPos[i] = now.mPosition.pos[i];
            saved.mRot[i] = now.mPosition.rot[i];
        }
        if (now.mScale != 1.f)
        {
            saved.mFlags |= NeutralSaveScaled;
            saved.mScale = now.mScale;
        }
        if (!now.mEnabled)
            saved.mFlags |= NeutralSaveEnabledChanged;
        saved.mCreated = info;
        return saved;
    }

    void applyNeutralSavedObject(const NeutralSavedObject& saved, NeutralObjectNow& now)
    {
        if ((saved.mFlags & NeutralSaveDeleted) != 0)
        {
            now.mDeleted = true;
            return;
        }
        if ((saved.mFlags & NeutralSaveMoved) != 0)
        {
            for (int i = 0; i < 3; ++i)
            {
                now.mPosition.pos[i] = saved.mPos[i];
                now.mPosition.rot[i] = saved.mRot[i];
            }
        }
        if ((saved.mFlags & NeutralSaveScaled) != 0)
            now.mScale = saved.mScale;
        if ((saved.mFlags & NeutralSaveEnabledChanged) != 0)
            now.mEnabled = (saved.mFlags & NeutralSaveEnabled) != 0;
    }
}
