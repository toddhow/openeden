#include "skyrimlip.hpp"

#include <algorithm>
#include <cctype>
#include <iterator>
#include <map>
#include <mutex>

#include <components/debug/debuglog.hpp>
#include <components/vfs/pathutil.hpp>

namespace MWRender::SkyrimLip
{
    namespace
    {
        std::uint32_t findMorph(const TriMorph::File& file, std::string_view name)
        {
            for (std::uint32_t i = 0; i < file.mMorphs.size(); ++i)
            {
                const std::string& n = file.mMorphs[i].mName;
                if (n.size() == name.size() && std::equal(n.begin(), n.end(), name.begin(), [](char a, char b) {
                        return std::tolower(static_cast<unsigned char>(a))
                            == std::tolower(static_cast<unsigned char>(b));
                    }))
                    return i;
            }
            return ~0u;
        }
    }

    vsg::ref_ptr<HeadMorph> HeadMorph::make(
        std::shared_ptr<const TriMorph::File> file, vsg::ref_ptr<vsg::vec3Array> positions)
    {
        if (!file || !positions || positions->size() != file->mVertices || file->mVertices == 0)
            return {};
        bool any = false;
        for (const std::string_view name : Lip::kVisemeNames)
            any = any || findMorph(*file, name) != ~0u;
        if (!any)
            return {};
        return vsg::ref_ptr<HeadMorph>(new HeadMorph(std::move(file), std::move(positions)));
    }

    HeadMorph::HeadMorph(std::shared_ptr<const TriMorph::File> file, vsg::ref_ptr<vsg::vec3Array> positions)
        : mFile(std::move(file))
        , mPositions(std::move(positions))
    {
        const std::size_t n = mPositions->size();
        mBase = std::make_unique<std::array<float, 3>[]>(n);
        for (std::size_t i = 0; i < n; ++i)
        {
            const vsg::vec3& p = (*mPositions)[static_cast<std::uint32_t>(i)];
            mBase[i] = { p.x, p.y, p.z };
        }
        for (std::size_t k = 0; k < mMorphOf.size(); ++k)
            mMorphOf[k] = findMorph(*mFile, Lip::kVisemeNames[k]);
        mPositions->properties.dataVariance = vsg::DYNAMIC_DATA;
    }

    bool HeadMorph::setWeights(const TriMorph::MouthWeights& weights)
    {
        return setVisemes(TriMorph::toVisemes(weights));
    }

    bool HeadMorph::setVisemes(const TriMorph::VisemeWeights& weights)
    {
        TriMorph::VisemeWeights q;
        for (std::size_t k = 0; k < q.size(); ++k)
            q[k] = mMorphOf[k] == ~0u ? 0.f : TriMorph::quantise(weights[k]);
        if (q == mLast)
            return false;
        TriMorph::Term terms[16];
        std::size_t count = 0;
        for (std::size_t k = 0; k < q.size(); ++k)
            if (q[k] != 0.f)
                terms[count++] = { mMorphOf[k], q[k] };
        const std::size_t n = mPositions->size();
        static_assert(sizeof(std::array<float, 3>) == sizeof(vsg::vec3));
        auto* out = reinterpret_cast<std::array<float, 3>*>(mPositions->data());
        if (!TriMorph::apply(*mFile, std::span<const std::array<float, 3>>(mBase.get(), n),
                std::span<const TriMorph::Term>(terms, count), std::span<std::array<float, 3>>(out, n)))
            return false;
        mPositions->dirty();
        mLast = q;
        ++mUploads;
        return true;
    }

    std::shared_ptr<const TriMorph::File> headFileFor(const VFS::Manager& vfs, std::uint32_t vertexCount)
    {
        static std::mutex sMutex;
        static std::map<std::string, std::shared_ptr<const TriMorph::File>> sFiles; // null: unreadable, remembered
        std::lock_guard lock(sMutex);
        for (const std::string_view path : sHeadTriFiles)
        {
            auto found = sFiles.find(std::string(path));
            if (found == sFiles.end())
            {
                std::shared_ptr<const TriMorph::File> file;
                try
                {
                    const Files::IStreamPtr stream = vfs.find(VFS::Path::Normalized(path));
                    if (stream)
                    {
                        const std::string bytes(
                            (std::istreambuf_iterator<char>(*stream)), std::istreambuf_iterator<char>());
                        TriMorph::Result r = TriMorph::read(bytes, path, Lip::kVisemeNames);
                        if (r.mOk)
                            file = std::make_shared<const TriMorph::File>(std::move(r.mFile));
                        else
                            Log(Debug::Warning) << "Skyrim lip sync: " << r.mError;
                    }
                }
                catch (const std::exception& e)
                {
                    Log(Debug::Warning) << "Skyrim lip sync: " << path << ": " << e.what();
                }
                found = sFiles.emplace(std::string(path), std::move(file)).first;
            }
            if (found->second && found->second->mVertices == vertexCount)
                return found->second;
        }
        return {};
    }
}
