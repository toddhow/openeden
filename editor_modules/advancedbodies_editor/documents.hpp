#ifndef ADVANCEDBODIES_EDITOR_DOCUMENTS_H
#define ADVANCEDBODIES_EDITOR_DOCUMENTS_H

// AdvancedBodies' editor module (task 0069, stage f): the body package panel. Built against the Editor Module API
// (sdk/include/openeden/editor_module), components/body and nlohmann/json only. Read only: no document is ever dirty
// and save() refuses.

#include <openeden/editor_module/module.hpp>
#include <openeden/editor_module/registrar.hpp>
#include <openeden/editor_module/types.hpp>

#include <cstddef>
#include <filesystem>
#include <memory>
#include <string>

namespace advancedbodies_editor
{
    class BodyView;

    inline constexpr const char* kModuleId = "advancedbodies_editor";
    inline constexpr const char* kPackageFormat = "openeden.body"; // a package folder, opened from its body.json
    inline constexpr const char* kPackFormat = "openeden.bodypack"; // a .edenpak: packed package or merged cache pack
    inline constexpr const char* kCacheFormat = "openeden.bodycache"; // the merged cache's `current` pointer
    inline constexpr const char* kPackageSuffix = "body.json";
    inline constexpr const char* kPackSuffix = ".edenpak";
    inline constexpr const char* kCacheSuffix = "current";

    /// The panel's finding rows stop here; a last row says how many more there are.
    inline constexpr std::size_t kMaxFindingRows = 200;

    /// A body source (openBodySource) as an asset. Null with `error` set when it cannot be opened.
    std::unique_ptr<openeden::editor::AssetDocument> openBodyAsset(
        const std::filesystem::path& path, std::string& error);

    /// The view behind a document this module made; null for any other document (another module's).
    const BodyView* viewOf(const openeden::editor::AssetDocument& document);

    /// What the regions gizmo draws (Editor Module API 1.1 gave it filled triangles). The default shows both: the fill
    /// first, then the edges over it.
    enum class RegionDisplay
    {
        FillAndEdges,
        Fill,
        Edges
    };

    /// Where `openmw --bake-bodies` writes the merged body cache's `current` pointer under a user data folder
    /// (<user data>/openeden/bodies/current); an empty path for an empty folder.
    std::filesystem::path mergedCachePath(const std::filesystem::path& userData);

    /// What a document this module made shows; FillAndEdges for another module's document.
    RegionDisplay regionDisplay(const openeden::editor::AssetDocument& document);

    /// Chooses what the regions gizmo draws. False, and nothing changes, for another module's document. Never an undo
    /// step and never dirty: what is viewed is not an edit.
    bool setRegionDisplay(openeden::editor::AssetDocument& document, RegionDisplay display);

    /// The fixed short summary an inspector section shows (the same object for the document's life). Null for a
    /// document this module did not make.
    openeden::editor::PropertySource* summaryOf(openeden::editor::AssetDocument& document);

    /// Shows body `index` of a document this module made (its panel rows and gizmos follow). False, and nothing
    /// changes, for another module's document or an index out of range. Never an undo step: what is viewed is not an
    /// edit (the undo stack is the content document's, and its clean flag is that document's modified state).
    bool selectBody(openeden::editor::AssetDocument& document, std::size_t index);

    /// The module's descriptor, for tests that link the sources in. The library exports the same one under the API's
    /// entry point name (openeden_editor_module_descriptor).
    const openeden::editor::EditorModuleDescriptor* moduleDescriptor();
}

#endif
