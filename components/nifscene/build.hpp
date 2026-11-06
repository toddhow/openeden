#ifndef OPENEDEN_COMPONENTS_NIFSCENE_BUILD_HPP
#define OPENEDEN_COMPONENTS_NIFSCENE_BUILD_HPP

#include <components/files/istreamptr.hpp>
#include <components/nif/niffile.hpp>
#include <components/vfs/pathutil.hpp>

#include "scene.hpp"

namespace ToUTF8
{
    class StatelessUtf8Encoder;
}

namespace NifScene
{
    struct SceneOptions
    {
        /// How readScene reads the file. The scene reader accepts Skyrim SE as well as Morrowind and does not apply
        /// Morrowind's rule that the first NiNode loses its transform: that rule belongs to the Morrowind adapter.
        Nif::ReadOptions mRead = [] {
            Nif::ReadOptions options;
            options.mAllowedVersions = Nif::VersionSet::SkyrimSE;
            options.mMorrowindRootRule = false;
            return options;
        }();
        /// Keep influences whose weight is exactly zero (the packed format pads every vertex to four).
        bool mKeepZeroWeights = false;
    };

    /// The scene of a parsed file. Throws Nif::Exception, naming the file, for content it cannot represent (a triangle
    /// index outside the vertices, a bone index outside the skin).
    Scene buildScene(const Nif::NIFFile& file, const SceneOptions& options);

    /// Parses a stream and builds its scene.
    Scene readScene(Files::IStreamPtr&& stream, VFS::Path::NormalizedView path, const SceneOptions& options,
        const ToUTF8::StatelessUtf8Encoder* encoder = nullptr);
}

#endif
