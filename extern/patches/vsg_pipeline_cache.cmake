# openeden-vsg: gives vsg::Device an optional VkPipelineCache and makes VSG's graphics and compute pipeline creation pass
# it to the driver (upstream v1.1.16 passes VK_NULL_HANDLE, so the driver recompiled every pipeline on every run).
# Run by FetchContent's PATCH_COMMAND inside the VSG source directory: cmake -P vsg_pipeline_cache.cmake
# A plain text replacement instead of `git apply`, because `git apply` silently applies nothing when the source folder
# is inside another git work tree. It fails loudly if VSG's text is not what it expects. VSG is MIT (docs/dependencies.md).
function(openeden_replace file from to)
    file(READ "${file}" text)
    string(FIND "${text}" "${to}" already)
    if(NOT already EQUAL -1)
        return()
    endif()
    string(FIND "${text}" "${from}" at)
    if(at EQUAL -1)
        message(FATAL_ERROR "vsg_pipeline_cache: '${from}' not found in ${file}")
    endif()
    string(REPLACE "${from}" "${to}" text "${text}")
    file(WRITE "${file}" "${text}")
endfunction()

openeden_replace("include/vsg/vk/Device.h"
    "        const uint32_t deviceID = 0;\n"
    "        const uint32_t deviceID = 0;\n\n        // openeden-vsg: optional pipeline cache used by every vkCreate*Pipelines call made through this device.\n        // Set once by the application right after the device exists and before any pipeline is compiled; the Device\n        // destroys it. VK_NULL_HANDLE (the default) keeps upstream behaviour.\n        VkPipelineCache pipelineCache = VK_NULL_HANDLE;\n")
openeden_replace("src/vsg/vk/Device.cpp"
    "    if (_device)\n    {\n        vkDestroyDevice"
    "    if (_device && pipelineCache != VK_NULL_HANDLE)\n    {\n        vkDestroyPipelineCache(_device, pipelineCache, _allocator);\n    }\n\n    if (_device)\n    {\n        vkDestroyDevice")
openeden_replace("src/vsg/state/GraphicsPipeline.cpp"
    "vkCreateGraphicsPipelines(*device, VK_NULL_HANDLE,"
    "vkCreateGraphicsPipelines(*device, device->pipelineCache,")
openeden_replace("src/vsg/state/ComputePipeline.cpp"
    "vkCreateComputePipelines(*device, VK_NULL_HANDLE,"
    "vkCreateComputePipelines(*device, device->pipelineCache,")
