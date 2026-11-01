#pragma once

// The Editor Module API, part 3: the module itself and its library's entry point (docs/tasks/0031).

#include <openeden/editor_module/registrar.hpp>
#include <openeden/editor_module/types.hpp>

namespace openeden::editor
{
    /// Implemented by each editor module. Owned by Eden Studio from creation until the module is unloaded.
    class EditorModule
    {
    public:
        virtual ~EditorModule() = default;
        /// Describes every panel, command, gizmo, asset type, section and track. Called once.
        virtual void registerUi(EditorRegistrar& registrar) = 0;
        /// Called before the module is destroyed, while its registrations are still present.
        virtual void shutdown() = 0;
    };

    struct EditorModuleDescriptor
    {
        openeden::sdk::Version apiVersion;
        const char* id;
        EditorModule* (*create)();
        void (*destroy)(EditorModule*);
    };

    inline constexpr const char* kEditorModuleEntryPoint = "openeden_editor_module_descriptor";
}

#if defined(_WIN32)
#define OPENEDEN_EDITOR_MODULE_EXPORT __declspec(dllexport)
#else
#define OPENEDEN_EDITOR_MODULE_EXPORT __attribute__((visibility("default")))
#endif

/// Defines the library's exported entry point for a module class with a default constructor.
#define OPENEDEN_EDITOR_MODULE(ModuleId, ModuleClass)                                                                  \
    extern "C" OPENEDEN_EDITOR_MODULE_EXPORT const openeden::editor::EditorModuleDescriptor*                           \
    openeden_editor_module_descriptor()                                                                                \
    {                                                                                                                  \
        static const openeden::editor::EditorModuleDescriptor descriptor{ openeden::editor::kApiVersion, ModuleId,     \
            []() -> openeden::editor::EditorModule* { return new ModuleClass(); },                                     \
            [](openeden::editor::EditorModule* module) { delete module; } };                                           \
        return &descriptor;                                                                                            \
    }
