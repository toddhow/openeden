// The library's entry point (the Editor Module API's kEditorModuleEntryPoint). Only the shared library has it: the tests
// link the other sources into eden-studio-tests, which already has another module's entry point.

#include "face_editor_module.hpp"

extern "C" OPENEDEN_EDITOR_MODULE_EXPORT const openeden::editor::EditorModuleDescriptor*
openeden_editor_module_descriptor()
{
    return face_editor::moduleDescriptor();
}
