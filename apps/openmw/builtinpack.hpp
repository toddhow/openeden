#ifndef OPENEDEN_APPS_OPENMW_BUILTINPACK_H
#define OPENEDEN_APPS_OPENMW_BUILTINPACK_H

#include <components/extension/manifest.hpp>
#include <openeden/extension.hpp>

#include <functional>
#include <memory>

namespace OMW
{
    /// A game pack compiled into the executable (task 0084 leftovers, stage I1b): its manifest and a function that
    /// makes its Extension. The executable fills Arguments with these; the extension host uses the one that game= names
    /// when no such pack is installed on disk.
    struct BuiltInGamePack
    {
        openeden::extension::ExtensionManifest manifest;
        std::function<std::unique_ptr<openeden::sdk::Extension>()> create;
    };
}

#endif
