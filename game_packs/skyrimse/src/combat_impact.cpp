#include "game_skyrimse/combat_impact.hpp"

#include "exterior_common.hpp"

#include <algorithm>
#include <cctype>

namespace game_skyrimse
{
    using detail::Fields;
    using detail::readLe;
    using detail::SubReader;

    namespace
    {
        std::string lowerCase(std::string_view text)
        {
            std::string out(text);
            for (char& c : out)
                c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
            return out;
        }

        bool validSoundForm(FormId id)
        {
            return id != kNullFormId && id != kUnresolvedFormId;
        }
    }

    // ---- decoders --------------------------------------------------------------------------------------------------

    bool decodeImpact(std::span<const std::byte> bytes, const Content::RawRecord& record, const DecodeContext& context,
        ImpactRecord& out, std::string& why)
    {
        const Fields fields{ context, "IPCT", why };
        out = ImpactRecord();
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
                case Content::fourCC("SNAM"):
                case Content::fourCC("NAM1"):
                {
                    if (data.size() != 4)
                    {
                        fields.odd(type);
                        break;
                    }
                    const FormId id = fields.resolved(readLe<std::uint32_t>(data.data()));
                    (type == Content::fourCC("SNAM") ? out.mSound1 : out.mSound2) = id;
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

    bool decodeImpactSet(std::span<const std::byte> bytes, const Content::RawRecord& record,
        const DecodeContext& context, ImpactSetRecord& out, std::string& why)
    {
        const Fields fields{ context, "IPDS", why };
        out = ImpactSetRecord();
        out.mCommon = fields.common(record);
        SubReader reader(bytes);
        std::uint32_t type = 0;
        std::span<const std::byte> data;
        int status = 0;
        while ((status = reader.next(type, data)) == 1)
        {
            if (type == Content::fourCC("EDID"))
            {
                if (!fields.zstring(type, data, out.mCommon.mEditorId))
                    return false;
            }
            else if (type == Content::fourCC("PNAM"))
            {
                if (data.size() != 8)
                {
                    fields.odd(type);
                    continue;
                }
                if (out.mPairs.size() >= kMaxImpactPairs)
                    continue; // a hostile list: the rest is dropped
                ImpactPair pair;
                pair.mMaterial = fields.resolved(readLe<std::uint32_t>(data.data()));
                pair.mImpact = fields.resolved(readLe<std::uint32_t>(data.data() + 4));
                out.mPairs.push_back(pair);
            }
        }
        if (status < 0)
        {
            why = reader.message();
            return false;
        }
        return true;
    }

    bool decodeMaterial(std::span<const std::byte> bytes, const Content::RawRecord& record,
        const DecodeContext& context, MaterialRecord& out, std::string& why)
    {
        const Fields fields{ context, "MATT", why };
        out = MaterialRecord();
        out.mCommon = fields.common(record);
        SubReader reader(bytes);
        std::uint32_t type = 0;
        std::span<const std::byte> data;
        int status = 0;
        while ((status = reader.next(type, data)) == 1)
            if (type == Content::fourCC("EDID") && !fields.zstring(type, data, out.mCommon.mEditorId))
                return false;
        if (status < 0)
        {
            why = reader.message();
            return false;
        }
        return true;
    }

    bool decodeSoundRedirect(std::span<const std::byte> bytes, const Content::RawRecord& record,
        const DecodeContext& context, SoundRedirectRecord& out, std::string& why)
    {
        const Fields fields{ context, "SOUN", why };
        out = SoundRedirectRecord();
        out.mCommon = fields.common(record);
        SubReader reader(bytes);
        std::uint32_t type = 0;
        std::span<const std::byte> data;
        int status = 0;
        while ((status = reader.next(type, data)) == 1)
        {
            if (type == Content::fourCC("EDID"))
            {
                if (!fields.zstring(type, data, out.mCommon.mEditorId))
                    return false;
            }
            else if (type == Content::fourCC("SDSC"))
            {
                if (data.size() != 4)
                {
                    fields.odd(type);
                    continue;
                }
                out.mDescriptor = fields.resolved(readLe<std::uint32_t>(data.data()));
            }
        }
        if (status < 0)
        {
            why = reader.message();
            return false;
        }
        return true;
    }

    // ---- the library -----------------------------------------------------------------------------------------------

    void SoundLibrary::add(const SoundRecords& records)
    {
        for (const ImpactRecord& r : records.mImpacts)
        {
            if (r.mCommon.deleted())
                mImpacts.erase(r.mCommon.mId);
            else
                mImpacts[r.mCommon.mId] = r;
        }
        for (const ImpactSetRecord& r : records.mImpactSets)
        {
            if (r.mCommon.deleted())
                mSets.erase(r.mCommon.mId);
            else
            {
                mSets[r.mCommon.mId] = r;
                if (!r.mCommon.mEditorId.empty())
                    mSetNames[lowerCase(r.mCommon.mEditorId)] = r.mCommon.mId;
            }
        }
        for (const MaterialRecord& r : records.mMaterials)
        {
            if (r.mCommon.deleted())
                mMaterials.erase(r.mCommon.mId);
            else
            {
                mMaterials[r.mCommon.mId] = r;
                if (!r.mCommon.mEditorId.empty())
                    mMaterialNames[lowerCase(r.mCommon.mEditorId)] = r.mCommon.mId;
            }
        }
        for (const SoundRedirectRecord& r : records.mRedirects)
        {
            if (r.mCommon.deleted())
                mRedirects.erase(r.mCommon.mId);
            else
            {
                mRedirects[r.mCommon.mId] = r.mDescriptor;
                if (!r.mCommon.mEditorId.empty())
                    mSoundNames.emplace(lowerCase(r.mCommon.mEditorId), r.mCommon.mId); // a SNDR of the name wins
            }
        }
        for (const SoundDescriptorRecord& r : records.mSounds)
        {
            if (r.mCommon.deleted())
                mSounds.erase(r.mCommon.mId);
            else
            {
                mSounds[r.mCommon.mId] = r;
                if (!r.mCommon.mEditorId.empty())
                    mSoundNames[lowerCase(r.mCommon.mEditorId)] = r.mCommon.mId;
            }
        }
    }

    const ImpactRecord* SoundLibrary::impact(FormId id) const
    {
        const auto it = mImpacts.find(id);
        return it == mImpacts.end() ? nullptr : &it->second;
    }

    const ImpactSetRecord* SoundLibrary::impactSet(FormId id) const
    {
        const auto it = mSets.find(id);
        return it == mSets.end() ? nullptr : &it->second;
    }

    const SoundDescriptorRecord* SoundLibrary::sound(FormId id) const
    {
        const auto redirect = mRedirects.find(id);
        if (redirect != mRedirects.end())
            id = redirect->second;
        const auto it = mSounds.find(id);
        return it == mSounds.end() ? nullptr : &it->second;
    }

    FormId SoundLibrary::materialByEditorId(std::string_view editorId) const
    {
        const auto it = mMaterialNames.find(lowerCase(editorId));
        return it == mMaterialNames.end() ? kNullFormId : it->second;
    }

    FormId SoundLibrary::soundByEditorId(std::string_view editorId) const
    {
        const auto it = mSoundNames.find(lowerCase(editorId));
        return it == mSoundNames.end() ? kNullFormId : it->second;
    }

    FormId SoundLibrary::impactSetByEditorId(std::string_view editorId) const
    {
        const auto it = mSetNames.find(lowerCase(editorId));
        return it == mSetNames.end() ? kNullFormId : it->second;
    }

    FormId SoundLibrary::impactFor(FormId impactSet, FormId material) const
    {
        const ImpactSetRecord* set = this->impactSet(impactSet);
        if (set == nullptr || material == kNullFormId)
            return kNullFormId;
        for (const ImpactPair& pair : set->mPairs)
            if (pair.mMaterial == material)
                return pair.mImpact;
        return kNullFormId;
    }

    // ---- rules -----------------------------------------------------------------------------------------------------

    bool resolveSoundSet(
        const SoundLibrary& library, FormId sound, const SoundFileProbe& exists, ResolvedSoundSet& out)
    {
        out = ResolvedSoundSet();
        const SoundDescriptorRecord* record = validSoundForm(sound) ? library.sound(sound) : nullptr;
        if (record == nullptr)
            return false;
        for (const std::string& stored : record->mFiles)
        {
            std::string path = normalizeSoundPath(stored);
            if (path.empty() || (exists && !exists(path)))
                continue;
            out.mFiles.push_back(std::move(path));
        }
        if (out.mFiles.empty())
            return false;
        out.mName = record->mCommon.mEditorId;
        out.mVolume = soundGain(record->mAttenuation);
        return true;
    }

    bool resolveSound(const SoundLibrary& library, FormId sound, std::uint32_t counter, const SoundFileProbe& exists,
        ResolvedSound& out)
    {
        out = ResolvedSound();
        ResolvedSoundSet set;
        if (!resolveSoundSet(library, sound, exists, set))
            return false;
        out.mName = std::move(set.mName);
        out.mFile = set.mFiles[counter % set.mFiles.size()];
        out.mVolume = set.mVolume;
        return true;
    }

    FormId targetMaterial(
        const SoundLibrary& library, FormId bodyMaterial, int bodyArmourType, bool armourMaterials)
    {
        if (armourMaterials && (bodyArmourType == 0 || bodyArmourType == 1))
        {
            const FormId armour = library.materialByEditorId(bodyArmourType == 0 ? kArmourMaterialLight
                                                                                 : kArmourMaterialHeavy);
            if (armour != kNullFormId)
                return armour;
        }
        return bodyMaterial;
    }

    ImpactSounds impactSoundFor(
        const SoundLibrary& library, FormId impactSet, FormId material, bool includeSecond)
    {
        ImpactSounds out;
        out.mImpact = library.impactFor(impactSet, material);
        if (out.mImpact == kNullFormId)
            return out;
        const ImpactRecord* impact = library.impact(out.mImpact);
        if (impact == nullptr)
            return out;
        out.mFirst = impact->mSound1;
        if (includeSecond)
            out.mSecond = impact->mSound2;
        return out;
    }
}
