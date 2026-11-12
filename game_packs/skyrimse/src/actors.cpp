#include "game_skyrimse/actors.hpp"

#include "game_skyrimse/pack_core.hpp"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <memory>
#include <set>
#include <sstream>

// Layout status of every subrecord read here (task 0118). VERIFIED: the five installed Skyrim SE masters were checked
// by tools/skyrimse_actors_check (sizes, value ranges and the record type each reference resolves to; aggregates in the
// task's stage log). RECALLED-VERIFY: the meaning of a field from memory of the UESP / TES5Edit documentation, which no
// check of ours settled. Nothing is copied from xEdit or the Creation Kit wiki; upstream OpenMW components/esm4 was not
// needed for any layout here.

namespace game_skyrimse
{
    namespace
    {
        template <class T>
        T readLe(const std::byte* p)
        {
            T value;
            std::memcpy(&value, p, sizeof(T));
            return value;
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

        std::string tag(std::uint32_t type)
        {
            return Content::fourCCToString(type);
        }

        constexpr std::uint32_t sXxxx = Content::fourCC("XXXX");

        class SubReader
        {
        public:
            explicit SubReader(std::span<const std::byte> data)
                : mData(data)
            {
            }

            /// 1: a subrecord, 0: the end, -1: damaged (message()).
            int next(std::uint32_t& type, std::span<const std::byte>& data)
            {
                for (;;)
                {
                    if (mPos == mData.size())
                        return 0;
                    if (mData.size() - mPos < 6)
                        return fail("a subrecord header is cut short at byte " + std::to_string(mPos));
                    type = readLe<std::uint32_t>(mData.data() + mPos);
                    std::uint64_t size = readLe<std::uint16_t>(mData.data() + mPos + 4);
                    mPos += 6;
                    if (mHasOverride)
                    {
                        size = mOverride;
                        mHasOverride = false;
                    }
                    if (size > mData.size() - mPos)
                        return fail("subrecord " + tag(type) + " claims " + std::to_string(size) + " bytes but "
                            + std::to_string(mData.size() - mPos) + " remain in the record");
                    if (type == sXxxx)
                    {
                        if (size != 4)
                            return fail("an XXXX subrecord of " + std::to_string(size) + " bytes, not 4");
                        mOverride = readLe<std::uint32_t>(mData.data() + mPos);
                        mHasOverride = true;
                        mPos += 4;
                        continue;
                    }
                    data = mData.subspan(mPos, static_cast<std::size_t>(size));
                    mPos += static_cast<std::size_t>(size);
                    return 1;
                }
            }

            const std::string& message() const { return mMessage; }

        private:
            int fail(std::string message)
            {
                mMessage = std::move(message);
                return -1;
            }

            std::span<const std::byte> mData;
            std::size_t mPos = 0;
            std::uint64_t mOverride = 0;
            bool mHasOverride = false;
            std::string mMessage;
        };

        class ActorParser final : public Content::Visitor
        {
        public:
            ActorParser(const Content::Plugin& plugin, const FormIdResolver& resolver, const ActorLimits& limits,
                ActorRecords& out)
                : mPlugin(plugin)
                , mResolver(resolver)
                , mLimits(limits)
                , mOut(out)
                , mLocalized(plugin.header().isLocalized())
            {
            }

            bool onRecord(const Content::RawRecord& record) override
            {
                ++mOut.mRecordsWalked;
                switch (record.mType)
                {
                    case Content::fourCC("NPC_"):
                    case Content::fourCC("RACE"):
                    case Content::fourCC("ARMA"):
                    case Content::fourCC("ARMO"):
                    case Content::fourCC("OTFT"):
                    case Content::fourCC("HDPT"):
                    case Content::fourCC("CLAS"):
                        break;
                    default:
                        return true;
                }
                TypeCount& count = mOut.mCounts[record.mType];
                ++count.mSeen;
                if ((record.mFlags & RecordFlags::Deleted) != 0)
                    ++count.mDeleted;
                if (count.mParsed >= mLimits.mMaxRecordsPerType)
                {
                    fail(record, "more than " + std::to_string(mLimits.mMaxRecordsPerType) + " records of one type");
                    return true;
                }

                std::span<const std::byte> bytes = record.mData;
                if (record.isCompressed())
                {
                    if (record.mUncompressedSize > mLimits.mMaxRecordBytes)
                    {
                        fail(record,
                            "the record inflates to " + std::to_string(record.mUncompressedSize)
                                + " bytes, over the limit of " + std::to_string(mLimits.mMaxRecordBytes));
                        return true;
                    }
                    Content::Error error;
                    if (!inflateRecord(record, mPlugin.name(), mInflated, error))
                    {
                        addError(error, record.mType);
                        return true;
                    }
                    bytes = mInflated;
                }
                else if (record.mDataSize > mLimits.mMaxRecordBytes)
                {
                    fail(record,
                        "the record is " + std::to_string(record.mDataSize) + " bytes, over the limit of "
                            + std::to_string(mLimits.mMaxRecordBytes));
                    return true;
                }

                mWhy.clear();
                mCurrentType = record.mType;
                bool ok = false;
                switch (record.mType)
                {
                    case Content::fourCC("NPC_"):
                        ok = readNpc(record, bytes);
                        break;
                    case Content::fourCC("RACE"):
                        ok = readRace(record, bytes);
                        break;
                    case Content::fourCC("ARMA"):
                        ok = readArma(record, bytes);
                        break;
                    case Content::fourCC("ARMO"):
                        ok = readArmo(record, bytes);
                        break;
                    case Content::fourCC("OTFT"):
                        ok = readOutfit(record, bytes);
                        break;
                    case Content::fourCC("HDPT"):
                        ok = readHeadPart(record, bytes);
                        break;
                    default:
                        ok = readClass(record, bytes);
                        break;
                }
                if (ok)
                    ++count.mParsed;
                else
                    fail(record, mWhy);
                return true;
            }

        private:
            void addError(Content::Error error, std::uint32_t type)
            {
                ++mOut.mCounts[type].mFailed;
                if (mOut.mErrors.size() < mLimits.mMaxErrors)
                    mOut.mErrors.push_back(std::move(error));
                else
                    ++mOut.mErrorsDropped;
            }

            void fail(const Content::RawRecord& record, std::string message)
            {
                addError(makeError(mPlugin.name(), record.mOffset, record.mType, std::move(message)), record.mType);
            }

            void oddType(std::uint32_t sub)
            {
                ++mOut.mOddSubrecords;
                ++mOut.mOddBy[tag(mCurrentType) + "." + tag(sub)];
            }

            bool damaged(const SubReader& reader)
            {
                mWhy = reader.message();
                return false;
            }

            FormId resolved(std::uint32_t stored)
            {
                const std::optional<FormId> id = mResolver.resolve(stored);
                if (!id)
                {
                    ++mOut.mUnresolvedFormIds;
                    return kUnresolvedFormId;
                }
                return *id;
            }

            RecordCommon common(const Content::RawRecord& record)
            {
                RecordCommon c;
                c.mStoredId = record.mFormId;
                c.mId = resolved(record.mFormId);
                c.mFlags = record.mFlags;
                c.mOffset = record.mOffset;
                return c;
            }

            // -- field readers: a wrong size is "odd" (counted, decodes to nothing); a hostile value fails the record
            // --

            bool zstring(std::uint32_t sub, std::span<const std::byte> data, std::string& out)
            {
                if (data.size() > mLimits.mMaxStringBytes)
                {
                    mWhy = "subrecord " + tag(sub) + " is " + std::to_string(data.size())
                        + " bytes, over the string limit of " + std::to_string(mLimits.mMaxStringBytes);
                    return false;
                }
                const char* begin = reinterpret_cast<const char*>(data.data());
                const void* nul = data.empty() ? nullptr : std::memchr(begin, '\0', data.size());
                out.assign(begin, nul != nullptr ? static_cast<const char*>(nul) : begin + data.size());
                return true;
            }

            bool lstring(std::uint32_t sub, std::span<const std::byte> data, LString& out)
            {
                if (mLocalized)
                {
                    if (data.size() != 4)
                    {
                        oddType(sub);
                        return true;
                    }
                    out.mPresent = true;
                    out.mLocalized = true;
                    out.mId = readLe<std::uint32_t>(data.data());
                    return true;
                }
                out.mPresent = true;
                return zstring(sub, data, out.mText);
            }

            bool formField(std::uint32_t sub, std::span<const std::byte> data, FormId& out)
            {
                if (data.size() != 4)
                {
                    oddType(sub);
                    return true;
                }
                out = resolved(readLe<std::uint32_t>(data.data()));
                return true;
            }

            /// One 4 byte entry appended to a list (PKID, PNAM, SPLO, MODL, HNAM ...).
            bool formEntry(std::uint32_t sub, std::span<const std::byte> data, std::vector<FormId>& out)
            {
                if (data.size() != 4)
                {
                    oddType(sub);
                    return true;
                }
                if (out.size() + 1 > mLimits.mMaxEntriesPerList)
                    return tooMany(sub);
                out.push_back(resolved(readLe<std::uint32_t>(data.data())));
                return true;
            }

            /// A subrecord holding any number of form ids (KWDA, INAM of OTFT, ...).
            bool formList(std::uint32_t sub, std::span<const std::byte> data, std::vector<FormId>& out)
            {
                if (data.size() % 4 != 0)
                {
                    oddType(sub);
                    return true;
                }
                const std::size_t n = data.size() / 4;
                if (out.size() + n > mLimits.mMaxEntriesPerList)
                    return tooMany(sub);
                for (std::size_t i = 0; i < n; ++i)
                    out.push_back(resolved(readLe<std::uint32_t>(data.data() + i * 4)));
                return true;
            }

            bool tooMany(std::uint32_t sub)
            {
                mWhy = "more than " + std::to_string(mLimits.mMaxEntriesPerList) + " entries in " + tag(sub);
                return false;
            }

            bool finiteFloat(std::uint32_t sub, std::span<const std::byte> data, std::optional<float>& out)
            {
                if (data.size() != 4)
                {
                    oddType(sub);
                    return true;
                }
                const float v = readLe<float>(data.data());
                if (!std::isfinite(v))
                {
                    oddType(sub);
                    return true;
                }
                out = v;
                return true;
            }

            // -- NPC_ --

            bool readNpc(const Content::RawRecord& record, std::span<const std::byte> bytes)
            {
                ActorNpc n;
                n.mCommon = common(record);
                SubReader reader(bytes);
                std::uint32_t type = 0;
                std::span<const std::byte> data;
                int status = 0;
                while ((status = reader.next(type, data)) == 1)
                {
                    bool ok = true;
                    switch (type)
                    {
                        case Content::fourCC("EDID"):
                            ok = zstring(type, data, n.mCommon.mEditorId);
                            break;
                        case Content::fourCC("FULL"):
                            ok = lstring(type, data, n.mName);
                            break;
                        case Content::fourCC("RNAM"):
                            ok = formField(type, data, n.mRace);
                            break;
                        case Content::fourCC("CNAM"):
                            ok = formField(type, data, n.mClass);
                            break;
                        case Content::fourCC("VTCK"):
                            ok = formField(type, data, n.mVoiceType);
                            break;
                        case Content::fourCC("TPLT"): // VERIFIED size 4, an NPC_ or an LVLN
                            ok = formField(type, data, n.mTemplate);
                            break;
                        case Content::fourCC("DOFT"): // VERIFIED: an OTFT
                            ok = formField(type, data, n.mDefaultOutfit);
                            break;
                        case Content::fourCC("SOFT"): // VERIFIED: an OTFT
                            ok = formField(type, data, n.mSleepOutfit);
                            break;
                        case Content::fourCC("WNAM"): // VERIFIED: an ARMO
                            ok = formField(type, data, n.mSkin);
                            break;
                        case Content::fourCC("ANAM"): // VERIFIED: an ARMO; meaning RECALLED-VERIFY
                            ok = formField(type, data, n.mFarAwaySkin);
                            break;
                        case Content::fourCC("INAM"):
                            ok = formField(type, data, n.mDeathItem);
                            break;
                        case Content::fourCC("CRIF"):
                            ok = formField(type, data, n.mCrimeFaction);
                            break;
                        case Content::fourCC("ZNAM"):
                            ok = formField(type, data, n.mCombatStyle);
                            break;
                        case Content::fourCC("FTST"):
                            ok = formField(type, data, n.mHeadTextureSet);
                            break;
                        case Content::fourCC("HCLF"):
                            ok = formField(type, data, n.mHairColor);
                            break;
                        case Content::fourCC("DPLT"):
                            ok = formField(type, data, n.mDefaultPackageList);
                            break;
                        case Content::fourCC("PKID"):
                            ok = formEntry(type, data, n.mPackages);
                            break;
                        case Content::fourCC("PNAM"):
                            ok = formEntry(type, data, n.mHeadParts);
                            break;
                        case Content::fourCC("SPLO"):
                            ok = formEntry(type, data, n.mSpells);
                            break;
                        case Content::fourCC("KWDA"):
                            ok = formList(type, data, n.mKeywords);
                            break;
                        case Content::fourCC("NAM6"):
                            ok = finiteFloat(type, data, n.mHeight);
                            break;
                        case Content::fourCC("NAM7"):
                            ok = finiteFloat(type, data, n.mWeight);
                            break;
                        case Content::fourCC("ACBS"): // VERIFIED size 24 (6,525 of 6,525)
                            if (data.size() >= 20)
                            {
                                n.mHasConfig = true;
                                n.mFlags = readLe<std::uint32_t>(data.data());
                                n.mLevel = readLe<std::uint16_t>(data.data() + 8);
                                n.mCalcMin = readLe<std::uint16_t>(data.data() + 10);
                                n.mCalcMax = readLe<std::uint16_t>(data.data() + 12);
                                n.mTemplateFlags = readLe<std::uint16_t>(data.data() + 18);
                                if (data.size() != 24)
                                    oddType(type);
                            }
                            else
                                oddType(type);
                            break;
                        case Content::fourCC("DNAM"): // VERIFIED size 52; offsets RECALLED-VERIFY
                            if (data.size() >= 42)
                            {
                                n.mHasStats = true;
                                for (std::size_t i = 0; i < 18; ++i)
                                    n.mSkills[i] = std::to_integer<std::uint8_t>(data[i]);
                                n.mHealth = readLe<std::uint16_t>(data.data() + 36);
                                n.mMagicka = readLe<std::uint16_t>(data.data() + 38);
                                n.mStamina = readLe<std::uint16_t>(data.data() + 40);
                                if (data.size() != 52)
                                    oddType(type);
                            }
                            else
                                oddType(type);
                            break;
                        case Content::fourCC("AIDT"): // VERIFIED size 20
                            if (data.size() == 20)
                            {
                                AiData ai;
                                ai.mAggression = std::to_integer<std::uint8_t>(data[0]);
                                ai.mConfidence = std::to_integer<std::uint8_t>(data[1]);
                                ai.mEnergy = std::to_integer<std::uint8_t>(data[2]);
                                ai.mResponsibility = std::to_integer<std::uint8_t>(data[3]);
                                ai.mMood = std::to_integer<std::uint8_t>(data[4]);
                                ai.mAssistance = std::to_integer<std::uint8_t>(data[5]);
                                ai.mAggroBehaviour = std::to_integer<std::uint8_t>(data[6]);
                                ai.mUnknown7 = std::to_integer<std::uint8_t>(data[7]);
                                for (std::size_t i = 0; i < 3; ++i)
                                    ai.mRadii[i] = readLe<std::uint32_t>(data.data() + 8 + i * 4);
                                n.mAi = ai;
                            }
                            else
                                oddType(type);
                            break;
                        case Content::fourCC("SNAM"): // VERIFIED size 8: faction, rank i8, three zero bytes
                            if (data.size() == 8)
                            {
                                if (n.mFactions.size() + 1 > mLimits.mMaxEntriesPerList)
                                {
                                    ok = tooMany(type);
                                    break;
                                }
                                FactionMembership f;
                                f.mFaction = resolved(readLe<std::uint32_t>(data.data()));
                                f.mRank = static_cast<std::int8_t>(std::to_integer<std::uint8_t>(data[4]));
                                n.mFactions.push_back(f);
                            }
                            else
                                oddType(type);
                            break;
                        case Content::fourCC("CNTO"): // VERIFIED size 8: item, count i32
                            if (data.size() == 8)
                            {
                                if (n.mInventory.size() + 1 > mLimits.mMaxEntriesPerList)
                                {
                                    ok = tooMany(type);
                                    break;
                                }
                                InventoryEntry e;
                                e.mItem = resolved(readLe<std::uint32_t>(data.data()));
                                e.mCount = readLe<std::int32_t>(data.data() + 4);
                                n.mInventory.push_back(e);
                            }
                            else
                                oddType(type);
                            break;
                        case Content::fourCC("QNAM"): // VERIFIED size 12, three floats in 0 to 1
                            if (data.size() == 12)
                            {
                                std::array<float, 3> c{};
                                for (std::size_t i = 0; i < 3; ++i)
                                    c[i] = readLe<float>(data.data() + i * 4);
                                if (std::all_of(c.begin(), c.end(), [](float v) { return std::isfinite(v); }))
                                    n.mSkinTone = c;
                                else
                                    oddType(type);
                            }
                            else
                                oddType(type);
                            break;
                        case Content::fourCC("NAM9"): // VERIFIED size 76
                            if (data.size() == 76)
                            {
                                std::array<float, 19> m{};
                                for (std::size_t i = 0; i < 19; ++i)
                                    m[i] = readLe<float>(data.data() + i * 4);
                                if (std::all_of(m.begin(), m.end(), [](float v) { return !std::isnan(v); }))
                                    n.mFaceMorphs = m;
                                else
                                    oddType(type);
                            }
                            else
                                oddType(type);
                            break;
                        case Content::fourCC("NAMA"): // VERIFIED size 16
                            if (data.size() == 16)
                            {
                                std::array<std::uint32_t, 4> p{};
                                for (std::size_t i = 0; i < 4; ++i)
                                    p[i] = readLe<std::uint32_t>(data.data() + i * 4);
                                n.mFaceParts = p;
                            }
                            else
                                oddType(type);
                            break;
                        case Content::fourCC("TINI"): // VERIFIED size 2: one tint layer's index
                            if (data.size() == 2)
                                ++n.mTintLayerCount;
                            else
                                oddType(type);
                            break;
                        default:
                            break;
                    }
                    if (!ok)
                        return false;
                }
                if (status < 0)
                    return damaged(reader);
                mOut.mNpcs.push_back(std::move(n));
                return true;
            }

            // -- RACE --

            bool readRace(const Content::RawRecord& record, std::span<const std::byte> bytes)
            {
                enum class Section
                {
                    Top,
                    Body, // NAM1
                    Behaviour, // NAM3
                    Head, // NAM0
                };
                RaceRecord r;
                r.mCommon = common(record);
                SubReader reader(bytes);
                std::uint32_t type = 0;
                std::span<const std::byte> data;
                int status = 0;
                Section section = Section::Top;
                Sex sex = Sex::Male;
                std::uint32_t index = 0;
                const auto idx = [](Sex s) { return s == Sex::Male ? 0 : 1; };
                while ((status = reader.next(type, data)) == 1)
                {
                    bool ok = true;
                    switch (type)
                    {
                        case Content::fourCC("EDID"):
                            ok = zstring(type, data, r.mCommon.mEditorId);
                            break;
                        case Content::fourCC("FULL"):
                            ok = lstring(type, data, r.mName);
                            break;
                        case Content::fourCC("WNAM"):
                            ok = formField(type, data, r.mSkin);
                            break;
                        case Content::fourCC("KWDA"):
                            ok = formList(type, data, r.mKeywords);
                            break;
                        case Content::fourCC("SPLO"):
                            ok = formEntry(type, data, r.mSpells);
                            break;
                        case Content::fourCC("BOD2"): // VERIFIED size 8 (204 of 204)
                            if (data.size() == 8)
                            {
                                r.mSlotMask = readLe<std::uint32_t>(data.data());
                                r.mArmorType = readLe<std::uint32_t>(data.data() + 4);
                            }
                            else
                                oddType(type);
                            break;
                        case Content::fourCC("DATA"): // VERIFIED size 164 (204 of 204)
                            if (data.size() >= 36)
                            {
                                r.mHasData = true;
                                for (std::size_t i = 0; i < 2; ++i)
                                {
                                    const float h = readLe<float>(data.data() + 16 + i * 4);
                                    const float w = readLe<float>(data.data() + 24 + i * 4);
                                    r.mHeight[i] = std::isfinite(h) ? h : 1.0f;
                                    r.mWeight[i] = std::isfinite(w) ? w : 1.0f;
                                }
                                r.mFlags = readLe<std::uint32_t>(data.data() + 32);
                                if (data.size() != 164)
                                    oddType(type);
                            }
                            else
                                oddType(type);
                            break;
                        case Content::fourCC("VTCK"): // VERIFIED size 8: male, female
                            if (data.size() == 8)
                            {
                                r.mVoiceTypes[0] = resolved(readLe<std::uint32_t>(data.data()));
                                r.mVoiceTypes[1] = resolved(readLe<std::uint32_t>(data.data() + 4));
                            }
                            else
                                oddType(type);
                            break;
                        case Content::fourCC("DNAM"): // VERIFIED size 8 where present (61 of 204): male, female
                            if (data.size() == 8)
                            {
                                r.mDecapitateArmor[0] = resolved(readLe<std::uint32_t>(data.data()));
                                r.mDecapitateArmor[1] = resolved(readLe<std::uint32_t>(data.data() + 4));
                            }
                            else
                                oddType(type);
                            break;
                        case Content::fourCC("DFTM"):
                            ok = formField(type, data, r.mDefaultFaceTexture[0]);
                            break;
                        case Content::fourCC("DFTF"):
                            ok = formField(type, data, r.mDefaultFaceTexture[1]);
                            break;
                        case Content::fourCC("AHCM"):
                            ok = formEntry(type, data, r.mAvailableHairColors[0]);
                            break;
                        case Content::fourCC("AHCF"):
                            ok = formEntry(type, data, r.mAvailableHairColors[1]);
                            break;
                        case Content::fourCC("FTSM"):
                            ok = formEntry(type, data, r.mFaceTextureSets[0]);
                            break;
                        case Content::fourCC("FTSF"):
                            ok = formEntry(type, data, r.mFaceTextureSets[1]);
                            break;
                        case Content::fourCC("MNAM"): // an empty marker: the male section of whatever follows
                            sex = Sex::Male;
                            index = 0;
                            break;
                        case Content::fourCC("FNAM"): // an empty marker: the female section
                            sex = Sex::Female;
                            index = 0;
                            break;
                        case Content::fourCC("NAM1"): // empty: the body data (morph files) section starts
                            section = Section::Body;
                            break;
                        case Content::fourCC("NAM3"): // empty: the behaviour graph section starts
                            section = Section::Behaviour;
                            break;
                        case Content::fourCC("NAM0"): // empty: a head data section starts (one per sex)
                            section = Section::Head;
                            break;
                        case Content::fourCC("INDX"):
                            if (data.size() == 4)
                                index = readLe<std::uint32_t>(data.data());
                            else
                                oddType(type);
                            break;
                        case Content::fourCC("ANAM"): // the skeleton model, before the body data section (VERIFIED)
                            if (section == Section::Top)
                                ok = zstring(type, data, r.mSkeleton[idx(sex)]);
                            break;
                        case Content::fourCC("MODL"):
                            if (section == Section::Body || section == Section::Behaviour)
                            {
                                if ((section == Section::Body ? r.mBodyModels.size() : r.mBehaviourGraphs.size())
                                    >= mLimits.mMaxEntriesPerList)
                                {
                                    ok = tooMany(type);
                                    break;
                                }
                                RaceModel m;
                                m.mSex = sex;
                                m.mIndex = index;
                                ok = zstring(type, data, m.mPath);
                                if (ok)
                                    (section == Section::Body ? r.mBodyModels : r.mBehaviourGraphs)
                                        .push_back(std::move(m));
                            }
                            break;
                        case Content::fourCC("HEAD"): // VERIFIED: an HDPT (282 of 282)
                            if (section == Section::Head && data.size() == 4)
                            {
                                if (r.mHeadParts.size() >= mLimits.mMaxEntriesPerList)
                                {
                                    ok = tooMany(type);
                                    break;
                                }
                                RaceHeadPart h;
                                h.mSex = sex;
                                h.mIndex = index;
                                h.mHeadPart = resolved(readLe<std::uint32_t>(data.data()));
                                r.mHeadParts.push_back(h);
                            }
                            else if (data.size() != 4)
                                oddType(type);
                            break;
                        default:
                            break;
                    }
                    if (!ok)
                        return false;
                }
                if (status < 0)
                    return damaged(reader);
                mOut.mRaces.push_back(std::move(r));
                return true;
            }

            // -- ARMA, ARMO --

            bool readArma(const Content::RawRecord& record, std::span<const std::byte> bytes)
            {
                ArmatureRecord a;
                a.mCommon = common(record);
                SubReader reader(bytes);
                std::uint32_t type = 0;
                std::span<const std::byte> data;
                int status = 0;
                while ((status = reader.next(type, data)) == 1)
                {
                    bool ok = true;
                    switch (type)
                    {
                        case Content::fourCC("EDID"):
                            ok = zstring(type, data, a.mCommon.mEditorId);
                            break;
                        case Content::fourCC("BOD2"): // VERIFIED size 8
                            if (data.size() == 8)
                            {
                                a.mSlotMask = readLe<std::uint32_t>(data.data());
                                a.mArmorType = readLe<std::uint32_t>(data.data() + 4);
                            }
                            else
                                oddType(type);
                            break;
                        case Content::fourCC("BODT"): // VERIFIED size 12: slots, general flags + padding, armor type
                            if (data.size() == 12)
                            {
                                a.mSlotMask = readLe<std::uint32_t>(data.data());
                                a.mArmorType = readLe<std::uint32_t>(data.data() + 8);
                            }
                            else
                                oddType(type);
                            break;
                        case Content::fourCC("RNAM"):
                            ok = formField(type, data, a.mRace);
                            break;
                        case Content::fourCC("MODL"):
                            ok = formEntry(type, data, a.mAdditionalRaces);
                            break;
                        case Content::fourCC("MOD2"):
                            ok = zstring(type, data, a.mMaleModel);
                            break;
                        case Content::fourCC("MOD3"):
                            ok = zstring(type, data, a.mFemaleModel);
                            break;
                        case Content::fourCC("MOD4"):
                            ok = zstring(type, data, a.mMaleFirstPerson);
                            break;
                        case Content::fourCC("MOD5"):
                            ok = zstring(type, data, a.mFemaleFirstPerson);
                            break;
                        case Content::fourCC("DNAM"): // VERIFIED size 12
                            if (data.size() == 12)
                            {
                                a.mHasDnam = true;
                                a.mMalePriority = std::to_integer<std::uint8_t>(data[0]);
                                a.mFemalePriority = std::to_integer<std::uint8_t>(data[1]);
                                a.mMaleWeightSlider = std::to_integer<std::uint8_t>(data[2]);
                                a.mFemaleWeightSlider = std::to_integer<std::uint8_t>(data[3]);
                                const float adjust = readLe<float>(data.data() + 8);
                                a.mWeaponAdjust = std::isfinite(adjust) ? adjust : 0.0f;
                            }
                            else
                                oddType(type);
                            break;
                        default:
                            break;
                    }
                    if (!ok)
                        return false;
                }
                if (status < 0)
                    return damaged(reader);
                mOut.mArmatures.push_back(std::move(a));
                return true;
            }

            bool readArmo(const Content::RawRecord& record, std::span<const std::byte> bytes)
            {
                ArmorExtra a;
                a.mCommon = common(record);
                SubReader reader(bytes);
                std::uint32_t type = 0;
                std::span<const std::byte> data;
                int status = 0;
                while ((status = reader.next(type, data)) == 1)
                {
                    bool ok = true;
                    switch (type)
                    {
                        case Content::fourCC("EDID"):
                            ok = zstring(type, data, a.mCommon.mEditorId);
                            break;
                        case Content::fourCC("BOD2"): // VERIFIED size 8
                            if (data.size() == 8)
                            {
                                a.mSlotMask = readLe<std::uint32_t>(data.data());
                                a.mArmorType = readLe<std::uint32_t>(data.data() + 4);
                            }
                            else
                                oddType(type);
                            break;
                        case Content::fourCC("BODT"): // VERIFIED sizes 8 and 12 (10 records)
                            if (data.size() == 8 || data.size() == 12)
                            {
                                a.mSlotMask = readLe<std::uint32_t>(data.data());
                                a.mArmorType = readLe<std::uint32_t>(data.data() + (data.size() == 12 ? 8 : 4));
                            }
                            else
                                oddType(type);
                            break;
                        case Content::fourCC("RNAM"):
                            ok = formField(type, data, a.mRace);
                            break;
                        case Content::fourCC("MODL"):
                            ok = formEntry(type, data, a.mArmatures);
                            break;
                        case Content::fourCC("TNAM"):
                            ok = formField(type, data, a.mTemplate);
                            break;
                        case Content::fourCC("DNAM"):
                            if (data.size() == 4)
                                a.mRating = readLe<std::uint32_t>(data.data());
                            else
                                oddType(type);
                            break;
                        default:
                            break;
                    }
                    if (!ok)
                        return false;
                }
                if (status < 0)
                    return damaged(reader);
                mOut.mArmors.push_back(std::move(a));
                return true;
            }

            // -- OTFT, HDPT, CLAS --

            bool readOutfit(const Content::RawRecord& record, std::span<const std::byte> bytes)
            {
                OutfitRecord o;
                o.mCommon = common(record);
                SubReader reader(bytes);
                std::uint32_t type = 0;
                std::span<const std::byte> data;
                int status = 0;
                while ((status = reader.next(type, data)) == 1)
                {
                    bool ok = true;
                    if (type == Content::fourCC("EDID"))
                        ok = zstring(type, data, o.mCommon.mEditorId);
                    else if (type == Content::fourCC("INAM"))
                        ok = formList(type, data, o.mItems);
                    if (!ok)
                        return false;
                }
                if (status < 0)
                    return damaged(reader);
                mOut.mOutfits.push_back(std::move(o));
                return true;
            }

            bool readHeadPart(const Content::RawRecord& record, std::span<const std::byte> bytes)
            {
                HeadPartRecord h;
                h.mCommon = common(record);
                SubReader reader(bytes);
                std::uint32_t type = 0;
                std::span<const std::byte> data;
                int status = 0;
                while ((status = reader.next(type, data)) == 1)
                {
                    bool ok = true;
                    switch (type)
                    {
                        case Content::fourCC("EDID"):
                            ok = zstring(type, data, h.mCommon.mEditorId);
                            break;
                        case Content::fourCC("FULL"):
                            ok = lstring(type, data, h.mName);
                            break;
                        case Content::fourCC("DATA"): // VERIFIED size 1
                            if (data.size() == 1)
                                h.mFlags = std::to_integer<std::uint8_t>(data[0]);
                            else
                                oddType(type);
                            break;
                        case Content::fourCC("PNAM"): // VERIFIED size 4, 0 to 6
                            if (data.size() == 4)
                                h.mType = readLe<std::uint32_t>(data.data());
                            else
                                oddType(type);
                            break;
                        case Content::fourCC("MODL"):
                            ok = zstring(type, data, h.mModel);
                            break;
                        case Content::fourCC("HNAM"):
                            ok = formEntry(type, data, h.mExtraParts);
                            break;
                        case Content::fourCC("TNAM"):
                            ok = formField(type, data, h.mTextureSet);
                            break;
                        case Content::fourCC("CNAM"):
                            ok = formField(type, data, h.mColor);
                            break;
                        case Content::fourCC("RNAM"):
                            ok = formField(type, data, h.mValidRaces);
                            break;
                        default:
                            break;
                    }
                    if (!ok)
                        return false;
                }
                if (status < 0)
                    return damaged(reader);
                mOut.mHeadParts.push_back(std::move(h));
                return true;
            }

            bool readClass(const Content::RawRecord& record, std::span<const std::byte> bytes)
            {
                ClassRecord c;
                c.mCommon = common(record);
                SubReader reader(bytes);
                std::uint32_t type = 0;
                std::span<const std::byte> data;
                int status = 0;
                while ((status = reader.next(type, data)) == 1)
                {
                    bool ok = true;
                    switch (type)
                    {
                        case Content::fourCC("EDID"):
                            ok = zstring(type, data, c.mCommon.mEditorId);
                            break;
                        case Content::fourCC("FULL"):
                            ok = lstring(type, data, c.mName);
                            break;
                        case Content::fourCC("DATA"): // VERIFIED size 36
                            if (data.size() == 36)
                            {
                                c.mHasData = true;
                                c.mTrainedSkill = std::to_integer<std::uint8_t>(data[4]);
                                c.mMaxTrainingLevel = std::to_integer<std::uint8_t>(data[5]);
                                for (std::size_t i = 0; i < 18; ++i)
                                    c.mSkillWeights[i] = std::to_integer<std::uint8_t>(data[6 + i]);
                                const float bleed = readLe<float>(data.data() + 24);
                                c.mBleedoutDefault = std::isfinite(bleed) ? bleed : 0.0f;
                                c.mVoicePoints = readLe<std::uint32_t>(data.data() + 28);
                                c.mHealthWeight = std::to_integer<std::uint8_t>(data[32]);
                                c.mMagickaWeight = std::to_integer<std::uint8_t>(data[33]);
                                c.mStaminaWeight = std::to_integer<std::uint8_t>(data[34]);
                            }
                            else
                                oddType(type);
                            break;
                        default:
                            break;
                    }
                    if (!ok)
                        return false;
                }
                if (status < 0)
                    return damaged(reader);
                mOut.mClasses.push_back(std::move(c));
                return true;
            }

            const Content::Plugin& mPlugin;
            const FormIdResolver& mResolver;
            const ActorLimits& mLimits;
            ActorRecords& mOut;
            bool mLocalized = false;
            std::vector<std::byte> mInflated;
            std::string mWhy;
            std::uint32_t mCurrentType = 0;
        };
    }

    bool ArmatureRecord::appliesTo(FormId race) const
    {
        if (race == kNullFormId || race == kUnresolvedFormId)
            return false;
        if (mRace == race)
            return true;
        return std::find(mAdditionalRaces.begin(), mAdditionalRaces.end(), race) != mAdditionalRaces.end();
    }

    bool parseActorRecords(
        const Content::Plugin& plugin, const LoadOrder& order, const ActorLimits& limits, ActorRecords& out)
    {
        out = ActorRecords();
        out.mName = plugin.name();
        if (!isSkyrimSeHeader(plugin.header()))
        {
            out.mWalkError = makeError(plugin.name(), 0, 0, "this plugin is not a Skyrim Special Edition one");
            return false;
        }
        const FormIdResolver resolver(plugin.header(), plugin.name(), order);
        ActorParser parser(plugin, resolver, limits, out);
        const std::optional<Content::Error> error = plugin.walk(parser);
        if (error)
        {
            out.mWalkError = *error;
            return false;
        }
        return true;
    }

    bool parseActorRecordsBytes(const std::string& bytes, const std::string& name, const LoadOrder& order,
        const ActorLimits& limits, ActorRecords& out, Content::Error& error)
    {
        const std::unique_ptr<Content::Plugin> plugin
            = Content::Plugin::open(std::make_unique<std::istringstream>(bytes, std::ios::binary), name, error);
        if (plugin == nullptr)
            return false;
        if (!parseActorRecords(*plugin, order, limits, out))
        {
            if (out.mWalkError)
                error = *out.mWalkError;
            return false;
        }
        return true;
    }

    // ---- ActorIndex ----------------------------------------------------------------------------------------------

    namespace
    {
        template <class T, class Map>
        void put(Map& map, const std::vector<T>& records)
        {
            for (const T& r : records)
            {
                if (r.mCommon.mId == kNullFormId || r.mCommon.mId == kUnresolvedFormId)
                    continue;
                if (r.mCommon.deleted())
                    map.erase(r.mCommon.mId);
                else
                    map[r.mCommon.mId] = r;
            }
        }
    }

    void ActorIndex::add(const ActorRecords& records)
    {
        put(mNpcs, records.mNpcs);
        put(mRaces, records.mRaces);
        put(mArmatures, records.mArmatures);
        put(mArmors, records.mArmors);
        put(mOutfits, records.mOutfits);
        put(mHeadParts, records.mHeadParts);
        put(mClasses, records.mClasses);
    }

    void ActorIndex::addLeveledLists(const std::vector<LeveledList>& lists)
    {
        for (const LeveledList& l : lists)
        {
            if (l.mCommon.mId == kNullFormId || l.mCommon.mId == kUnresolvedFormId)
                continue;
            if (l.mCommon.deleted())
                mLeveled.erase(l.mCommon.mId);
            else
                mLeveled[l.mCommon.mId] = l;
        }
    }

    std::vector<FormId> ActorIndex::npcIds() const
    {
        std::vector<FormId> ids;
        ids.reserve(mNpcs.size());
        for (const auto& [id, npc] : mNpcs)
            ids.push_back(id);
        std::sort(ids.begin(), ids.end());
        return ids;
    }

    const LeveledEntry* ActorIndex::pickLeveled(FormId list, std::uint16_t level, std::uint32_t pick) const
    {
        const LeveledList* l = leveledList(list);
        if (l == nullptr)
            return nullptr;
        std::uint16_t best = 0;
        bool any = false;
        for (const LeveledEntry& e : l->mEntries)
        {
            if (e.mLevel > level)
                continue;
            any = true;
            best = std::max(best, e.mLevel);
        }
        if (!any)
            return nullptr;
        const bool allBelow = (l->mFlags & LeveledFlags::CalcFromAllLevelsLessThanPlayer) != 0;
        std::vector<const LeveledEntry*> eligible;
        for (const LeveledEntry& e : l->mEntries)
            if (e.mLevel <= level && (allBelow || e.mLevel == best))
                eligible.push_back(&e);
        return eligible.empty() ? nullptr : eligible[pick % eligible.size()];
    }

    ActorIndex::ResolveStatus ActorIndex::resolve(
        FormId npcId, std::uint16_t level, std::uint32_t pick, Resolved& out) const
    {
        out = Resolved();
        const ActorNpc* start = npc(npcId);
        if (start == nullptr)
            return ResolveStatus::UnknownNpc;

        // The holder of each category, starting at the NPC itself. A category moves to the template while the holder's
        // own flag for it is set.
        struct Category
        {
            std::uint16_t mFlag;
            FormId* mFrom;
        };
        out.mTraitsFrom = out.mStatsFrom = out.mFactionsFrom = out.mSpellsFrom = out.mAiFrom = out.mPackagesFrom
            = out.mBaseDataFrom = out.mInventoryFrom = out.mKeywordsFrom = npcId;
        std::array<Category, 9> categories{ {
            { TemplateFlags::Traits, &out.mTraitsFrom },
            { TemplateFlags::Stats, &out.mStatsFrom },
            { TemplateFlags::Factions, &out.mFactionsFrom },
            { TemplateFlags::SpellList, &out.mSpellsFrom },
            { TemplateFlags::AiData, &out.mAiFrom },
            { TemplateFlags::AiPackages, &out.mPackagesFrom },
            { TemplateFlags::BaseData, &out.mBaseDataFrom },
            { TemplateFlags::Inventory, &out.mInventoryFrom },
            { TemplateFlags::Keywords, &out.mKeywordsFrom },
        } };

        std::set<FormId> seen;
        FormId cur = npcId;
        for (std::uint32_t depth = 0;; ++depth)
        {
            if (!seen.insert(cur).second)
                return ResolveStatus::Cycle;
            if (depth > kMaxTemplateDepth)
                return ResolveStatus::TooDeep;
            const ActorNpc* n = npc(cur);
            if (n == nullptr)
                return ResolveStatus::BrokenLink;
            if (n->mTemplate == kNullFormId || n->mTemplate == kUnresolvedFormId)
                break;
            // The template: an NPC_ or a leveled NPC list (followed through nested lists).
            FormId next = n->mTemplate;
            for (std::uint32_t hops = 0; npc(next) == nullptr; ++hops)
            {
                if (hops > kMaxTemplateDepth)
                    return ResolveStatus::TooDeep;
                const LeveledEntry* e = pickLeveled(next, level, pick);
                if (e == nullptr)
                    return ResolveStatus::BrokenLink;
                next = e->mReference;
            }
            bool moved = false;
            for (Category& c : categories)
                if (*c.mFrom == cur && (n->mTemplateFlags & c.mFlag) != 0)
                {
                    *c.mFrom = next;
                    moved = true;
                }
            if (!moved)
                break;
            cur = next;
            out.mDepth = depth + 1;
        }

        const auto from = [&](FormId id) { return npc(id); };
        const ActorNpc* traits = from(out.mTraitsFrom);
        const ActorNpc* stats = from(out.mStatsFrom);
        const ActorNpc* base = from(out.mBaseDataFrom);
        const ActorNpc* inventory = from(out.mInventoryFrom);
        if (traits == nullptr || stats == nullptr || base == nullptr || inventory == nullptr)
            return ResolveStatus::BrokenLink;
        out.mRace = traits->mRace;
        out.mSkin = traits->mSkin;
        out.mHeadParts = traits->mHeadParts;
        out.mClass = stats->mClass;
        out.mName = base->mName;
        out.mDefaultOutfit = inventory->mDefaultOutfit;
        out.mInventory = inventory->mInventory;
        if (const ActorNpc* n = from(out.mFactionsFrom))
            out.mFactions = n->mFactions;
        if (const ActorNpc* n = from(out.mPackagesFrom))
            out.mPackages = n->mPackages;
        return ResolveStatus::Ok;
    }

    std::vector<ActorIndex::WornModel> ActorIndex::wornModels(
        FormId outfitId, FormId race, Sex sex, std::uint16_t level, std::uint32_t pick) const
    {
        std::vector<WornModel> result;
        const OutfitRecord* o = outfit(outfitId);
        if (o == nullptr)
            return result;
        constexpr std::uint32_t kMaxItems = 256;
        constexpr std::uint32_t kMaxDepth = 8;
        std::vector<std::pair<FormId, std::uint32_t>> work; // (form, depth), processed in order
        for (auto it = o->mItems.rbegin(); it != o->mItems.rend(); ++it)
            work.emplace_back(*it, 0);
        std::uint32_t visited = 0;
        while (!work.empty() && visited++ < kMaxItems)
        {
            const auto [id, depth] = work.back();
            work.pop_back();
            if (const ArmorExtra* armor = this->armor(id))
            {
                for (FormId armatureId : armor->mArmatures)
                {
                    const ArmatureRecord* a = armature(armatureId);
                    if (a == nullptr || !a->appliesTo(race) || a->model(sex).empty())
                        continue;
                    WornModel w;
                    w.mArmor = id;
                    w.mArmature = armatureId;
                    w.mSlotMask = a->mSlotMask;
                    w.mModel = a->model(sex);
                    w.mPriority = sex == Sex::Male ? a->mMalePriority : a->mFemalePriority;
                    result.push_back(std::move(w));
                    break; // one addon per armor: the first that fits the race
                }
            }
            else if (depth < kMaxDepth)
            {
                if (const LeveledEntry* e = pickLeveled(id, level, pick))
                    work.emplace_back(e->mReference, depth + 1);
            }
        }
        return result;
    }
}
