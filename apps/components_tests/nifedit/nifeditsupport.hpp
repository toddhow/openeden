#ifndef OPENEDEN_TEST_SUITE_NIFEDIT_NIFEDITSUPPORT_H
#define OPENEDEN_TEST_SUITE_NIFEDIT_NIFEDITSUPPORT_H

#include <components/nif/base.hpp>
#include <components/nif/data.hpp>
#include <components/nif/extra.hpp>
#include <components/nif/niffile.hpp>
#include <components/nif/node.hpp>
#include <components/nif/property.hpp>
#include <components/nif/texture.hpp>
#include <components/vfs/pathutil.hpp>

#include <memory>
#include <sstream>
#include <string>
#include <vector>

#include "nifeditbuilder.hpp"

namespace NifEdit::Testing
{
    inline std::unique_ptr<Nif::NIFFile> parseFile(const std::string& bytes)
    {
        constexpr VFS::Path::NormalizedView path("nifedit-test");
        auto file = std::make_unique<Nif::NIFFile>(path);
        Nif::Reader reader(*file, nullptr);
        reader.parse(std::make_unique<std::istringstream>(bytes), Nif::ReadOptions{});
        return file;
    }

    // What the engine's reader makes of one record, as text. Only the record types the editor knows have fields in it.
    inline std::string dumpRecord(const Nif::NIFFile& file, std::size_t index)
    {
        const Nif::Record* record = file.mRecords.at(index).get();
        std::ostringstream out;
        out.precision(9);
        out << index << " " << record->mRecordName;
        if (const auto* net = dynamic_cast<const Nif::NiObjectNET*>(record))
            out << " name=[" << net->mName << "]";
        if (const auto* av = dynamic_cast<const Nif::NiAVObject*>(record))
        {
            out << " flags=" << av->mFlags << " t=" << av->mTransform.mTranslation.x << ","
                << av->mTransform.mTranslation.y << "," << av->mTransform.mTranslation.z
                << " s=" << av->mTransform.mScale << " r=";
            for (const auto& row : av->mTransform.mRotation.mValues)
                for (float value : row)
                    out << value << ",";
        }
        if (const auto* material = dynamic_cast<const Nif::NiMaterialProperty*>(record))
        {
            out << " flags=" << material->mFlags;
            for (const vsg::vec3& colour :
                { material->mAmbient, material->mDiffuse, material->mSpecular, material->mEmissive })
                out << " c=" << colour.x << "," << colour.y << "," << colour.z;
            out << " gloss=" << material->mGlossiness << " alpha=" << material->mAlpha;
        }
        if (const auto* alpha = dynamic_cast<const Nif::NiAlphaProperty*>(record))
            out << " flags=" << alpha->mFlags << " threshold=" << static_cast<int>(alpha->mThreshold);
        if (const auto* texture = dynamic_cast<const Nif::NiSourceTexture*>(record))
            out << " file=[" << texture->mFile << "]";
        if (const auto* text = dynamic_cast<const Nif::NiStringExtraData*>(record))
            out << " string=[" << text->mData << "]";
        if (const auto* keys = dynamic_cast<const Nif::NiTextKeyExtraData*>(record))
        {
            out << " keys=";
            for (const auto& key : keys->mList)
                out << key.mTime << "=[" << key.mText << "];";
        }
        if (const auto* color = dynamic_cast<const Nif::NiVertexColorProperty*>(record))
            out << " flags=" << color->mFlags;
        if (const auto* z = dynamic_cast<const Nif::NiZBufferProperty*>(record))
            out << " flags=" << z->mFlags;
        return out.str();
    }

    inline std::vector<std::string> dumpAll(const std::string& bytes)
    {
        const auto file = parseFile(bytes);
        std::vector<std::string> lines;
        for (std::size_t i = 0; i < file->mRecords.size(); ++i)
            lines.push_back(dumpRecord(*file, i));
        return lines;
    }
}

#endif
