#include "devicereport.hpp"

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <set>
#include <sstream>
#include <stdexcept>

#include <nlohmann/json.hpp>

#include <vsg/vk/PhysicalDevice.h>
#include <vsg/vk/Surface.h>

namespace Render
{
    void DeviceReport::add(std::string key, std::string value)
    {
        mEntries.emplace_back(std::move(key), std::move(value));
    }

    void DeviceReport::add(std::string key, const char* value)
    {
        add(std::move(key), std::string(value));
    }

    void DeviceReport::add(std::string key, bool present)
    {
        add(std::move(key), present ? "present" : "absent");
    }

    std::string_view DeviceReport::get(std::string_view key) const
    {
        for (const Entry& e : mEntries)
            if (e.first == key)
                return e.second;
        return {};
    }

    std::string DeviceReport::toLogText() const
    {
        std::ostringstream out;
        for (const Entry& e : mEntries)
            out << "  " << e.first << " = " << e.second << '\n';
        return out.str();
    }

    namespace
    {
        void appendEscaped(std::string& out, const std::string& s)
        {
            out += '"';
            for (const unsigned char c : s)
            {
                switch (c)
                {
                    case '"':
                        out += "\\\"";
                        break;
                    case '\\':
                        out += "\\\\";
                        break;
                    case '\n':
                        out += "\\n";
                        break;
                    case '\r':
                        out += "\\r";
                        break;
                    case '\t':
                        out += "\\t";
                        break;
                    default:
                        if (c < 0x20)
                        {
                            char buffer[8];
                            std::snprintf(buffer, sizeof(buffer), "\\u%04x", c);
                            out += buffer;
                        }
                        else
                            out += static_cast<char>(c);
                }
            }
            out += '"';
        }
    }

    std::string DeviceReport::toJson() const
    {
        std::string out = "{\n";
        for (std::size_t i = 0; i < mEntries.size(); ++i)
        {
            out += "  ";
            appendEscaped(out, mEntries[i].first);
            out += ": ";
            appendEscaped(out, mEntries[i].second);
            out += i + 1 < mEntries.size() ? ",\n" : "\n";
        }
        out += "}\n";
        return out;
    }

    DeviceReport DeviceReport::parseJson(const std::string& text)
    {
        nlohmann::ordered_json root;
        try
        {
            root = nlohmann::ordered_json::parse(text);
        }
        catch (const nlohmann::json::exception& e)
        {
            throw std::runtime_error(std::string("device report: not valid JSON: ") + e.what());
        }
        if (!root.is_object())
            throw std::runtime_error("device report: the top level must be an object");
        DeviceReport report;
        for (auto it = root.begin(); it != root.end(); ++it)
        {
            if (!it.value().is_string())
                throw std::runtime_error("device report: the value of \"" + it.key() + "\" must be a string");
            report.add(it.key(), it.value().get<std::string>());
        }
        return report;
    }

    namespace
    {
        std::string versionString(uint32_t v)
        {
            return std::to_string(VK_API_VERSION_MAJOR(v)) + "." + std::to_string(VK_API_VERSION_MINOR(v)) + "."
                + std::to_string(VK_API_VERSION_PATCH(v));
        }

        std::string hex(uint32_t v)
        {
            char buffer[16];
            std::snprintf(buffer, sizeof(buffer), "0x%04x", v);
            return buffer;
        }

        // "name,name" for the set bits of `flags`, or "none".
        template <class Table>
        std::string flagNames(uint32_t flags, const Table& table)
        {
            std::string out;
            for (const auto& [bit, name] : table)
                if (flags & bit)
                    out += (out.empty() ? "" : ",") + std::string(name);
            return out.empty() ? "none" : out;
        }

        struct NamedBit
        {
            uint32_t first;
            const char* second;
        };

        const NamedBit subgroupStages[] = { { VK_SHADER_STAGE_VERTEX_BIT, "vertex" },
            { VK_SHADER_STAGE_TESSELLATION_CONTROL_BIT, "tess_control" },
            { VK_SHADER_STAGE_TESSELLATION_EVALUATION_BIT, "tess_eval" }, { VK_SHADER_STAGE_GEOMETRY_BIT, "geometry" },
            { VK_SHADER_STAGE_FRAGMENT_BIT, "fragment" }, { VK_SHADER_STAGE_COMPUTE_BIT, "compute" } };
        const NamedBit subgroupOps[] = { { VK_SUBGROUP_FEATURE_BASIC_BIT, "basic" },
            { VK_SUBGROUP_FEATURE_VOTE_BIT, "vote" }, { VK_SUBGROUP_FEATURE_ARITHMETIC_BIT, "arithmetic" },
            { VK_SUBGROUP_FEATURE_BALLOT_BIT, "ballot" }, { VK_SUBGROUP_FEATURE_SHUFFLE_BIT, "shuffle" },
            { VK_SUBGROUP_FEATURE_SHUFFLE_RELATIVE_BIT, "shuffle_relative" },
            { VK_SUBGROUP_FEATURE_CLUSTERED_BIT, "clustered" }, { VK_SUBGROUP_FEATURE_QUAD_BIT, "quad" } };

        const char* colorSpaceName(VkColorSpaceKHR cs)
        {
            switch (cs)
            {
                case VK_COLOR_SPACE_SRGB_NONLINEAR_KHR:
                    return "srgb_nonlinear";
                case VK_COLOR_SPACE_DISPLAY_P3_NONLINEAR_EXT:
                    return "display_p3_nonlinear";
                case VK_COLOR_SPACE_EXTENDED_SRGB_LINEAR_EXT:
                    return "extended_srgb_linear";
                case VK_COLOR_SPACE_EXTENDED_SRGB_NONLINEAR_EXT:
                    return "extended_srgb_nonlinear";
                case VK_COLOR_SPACE_DISPLAY_P3_LINEAR_EXT:
                    return "display_p3_linear";
                case VK_COLOR_SPACE_BT709_LINEAR_EXT:
                    return "bt709_linear";
                case VK_COLOR_SPACE_BT709_NONLINEAR_EXT:
                    return "bt709_nonlinear";
                case VK_COLOR_SPACE_BT2020_LINEAR_EXT:
                    return "bt2020_linear";
                case VK_COLOR_SPACE_HDR10_ST2084_EXT:
                    return "hdr10_st2084";
                case VK_COLOR_SPACE_HDR10_HLG_EXT:
                    return "hdr10_hlg";
                case VK_COLOR_SPACE_PASS_THROUGH_EXT:
                    return "pass_through";
                default:
                    return "other";
            }
        }

        bool hasExtension(const std::vector<VkExtensionProperties>& list, const char* name)
        {
            return std::any_of(list.begin(), list.end(),
                [name](const VkExtensionProperties& p) { return std::strcmp(p.extensionName, name) == 0; });
        }

        // A Vulkan structure with every member zero but sType. (Brace initialisation with only sType trips
        // -Wmissing-field-initializers, which is an error on Linux.)
        template <class T>
        T makeStruct(VkStructureType type)
        {
            T s{};
            s.sType = type;
            return s;
        }

        template <class Fn>
        Fn instanceProc(VkInstance instance, const char* name)
        {
            return reinterpret_cast<Fn>(vkGetInstanceProcAddr(instance, name));
        }

        // An extension that a later Vulkan version made part of the core.
        std::string extensionStatus(
            const std::vector<VkExtensionProperties>& list, const char* name, uint32_t deviceApi, uint32_t coreSince)
        {
            if (coreSince && deviceApi >= coreSince)
                return "present (core in Vulkan " + std::to_string(VK_API_VERSION_MAJOR(coreSince)) + "."
                    + std::to_string(VK_API_VERSION_MINOR(coreSince)) + ")";
            return hasExtension(list, name) ? "present (extension)" : "absent";
        }
    }

    DeviceReport collectDeviceReport(
        const vsg::PhysicalDevice* used, const vsg::Surface* surface, uint32_t usedInstanceApi)
    {
        DeviceReport r;
        r.add("schema", "openeden.device-report/1");

        // The loader and the game's instance.
        uint32_t loaderVersion = VK_API_VERSION_1_0;
        if (auto enumerateVersion = instanceProc<PFN_vkEnumerateInstanceVersion>(nullptr, "vkEnumerateInstanceVersion"))
            enumerateVersion(&loaderVersion);
        r.add("instance.loaderApiVersion", versionString(loaderVersion));
        r.add("instance.requestedByGame", versionString(usedInstanceApi));

        std::vector<VkExtensionProperties> instanceExtensions;
        {
            uint32_t count = 0;
            vkEnumerateInstanceExtensionProperties(nullptr, &count, nullptr);
            instanceExtensions.resize(count);
            if (count)
                vkEnumerateInstanceExtensionProperties(nullptr, &count, instanceExtensions.data());
            instanceExtensions.resize(count);
        }
        r.add("instance.VK_EXT_swapchain_colorspace", hasExtension(instanceExtensions, "VK_EXT_swapchain_colorspace"));
        r.add("instance.VK_EXT_debug_utils", hasExtension(instanceExtensions, "VK_EXT_debug_utils"));

        // The probe instance. Vulkan 1.1 is where vkGetPhysicalDeviceFeatures2 appeared, so a 1.0 loader cannot be
        // asked about most features and the entries say "unknown".
        const uint32_t probeApi
            = loaderVersion >= VK_API_VERSION_1_1 ? std::min(loaderVersion, VK_API_VERSION_1_3) : VK_API_VERSION_1_0;
        VkApplicationInfo appInfo = makeStruct<VkApplicationInfo>(VK_STRUCTURE_TYPE_APPLICATION_INFO);
        appInfo.pApplicationName = "OpenEden device report";
        appInfo.apiVersion = probeApi;
        VkInstanceCreateInfo instanceInfo = makeStruct<VkInstanceCreateInfo>(VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO);
        instanceInfo.pApplicationInfo = &appInfo;
        VkInstance probe = VK_NULL_HANDLE;
        const VkResult created = vkCreateInstance(&instanceInfo, nullptr, &probe);
        if (created != VK_SUCCESS)
        {
            r.add("probe", "failed to create an instance (VkResult " + std::to_string(created) + ")");
            return r;
        }

        uint32_t deviceCount = 0;
        vkEnumeratePhysicalDevices(probe, &deviceCount, nullptr);
        std::vector<VkPhysicalDevice> devices(deviceCount);
        if (deviceCount)
            vkEnumeratePhysicalDevices(probe, &deviceCount, devices.data());
        devices.resize(deviceCount);

        VkPhysicalDevice device = VK_NULL_HANDLE;
        VkPhysicalDeviceProperties props{};
        for (VkPhysicalDevice candidate : devices)
        {
            VkPhysicalDeviceProperties p;
            vkGetPhysicalDeviceProperties(candidate, &p);
            const bool matches = used && std::strcmp(p.deviceName, used->getProperties().deviceName) == 0
                && p.vendorID == used->getProperties().vendorID && p.deviceID == used->getProperties().deviceID;
            if (matches || (!used && device == VK_NULL_HANDLE))
            {
                device = candidate;
                props = p;
                if (matches)
                    break;
            }
        }
        if (device == VK_NULL_HANDLE)
        {
            r.add("device", "absent (no physical device found by the probe)");
            vkDestroyInstance(probe, nullptr);
            return r;
        }

        static const char* const types[] = { "other", "integrated gpu", "discrete gpu", "virtual gpu", "cpu" };
        r.add("device.name", props.deviceName);
        r.add("device.type", props.deviceType < 5 ? types[props.deviceType] : "unknown");
        r.add("device.vendorId", hex(props.vendorID));
        r.add("device.deviceId", hex(props.deviceID));
        r.add("device.apiVersion", versionString(props.apiVersion));
        r.add("device.driverVersion", hex(props.driverVersion));

        const uint32_t api = props.apiVersion;

        // Memory heaps.
        {
            VkPhysicalDeviceMemoryProperties mem;
            vkGetPhysicalDeviceMemoryProperties(device, &mem);
            uint64_t deviceLocal = 0;
            for (uint32_t i = 0; i < mem.memoryHeapCount; ++i)
            {
                const bool local = mem.memoryHeaps[i].flags & VK_MEMORY_HEAP_DEVICE_LOCAL_BIT;
                if (local)
                    deviceLocal += mem.memoryHeaps[i].size;
                r.add("memory.heap" + std::to_string(i),
                    std::to_string(mem.memoryHeaps[i].size / (1024 * 1024)) + " MiB" + (local ? ", device local" : ""));
            }
            r.add("memory.deviceLocalMiB", std::to_string(deviceLocal / (1024 * 1024)));
        }

        // Timestamps.
        {
            uint32_t familyCount = 0;
            vkGetPhysicalDeviceQueueFamilyProperties(device, &familyCount, nullptr);
            std::vector<VkQueueFamilyProperties> families(familyCount);
            if (familyCount)
                vkGetPhysicalDeviceQueueFamilyProperties(device, &familyCount, families.data());
            uint32_t bits = 0;
            for (const auto& f : families)
                if (f.queueFlags & VK_QUEUE_GRAPHICS_BIT)
                {
                    bits = f.timestampValidBits;
                    break;
                }
            r.add("timestamps", bits > 0 && props.limits.timestampPeriod > 0.f);
            r.add("timestamps.validBits", std::to_string(bits));
            r.add("timestamps.periodNs", std::to_string(props.limits.timestampPeriod));
            r.add("timestamps.computeAndGraphics", props.limits.timestampComputeAndGraphics == VK_TRUE);
        }

        std::vector<VkExtensionProperties> deviceExtensions;
        {
            uint32_t count = 0;
            vkEnumerateDeviceExtensionProperties(device, nullptr, &count, nullptr);
            deviceExtensions.resize(count);
            if (count)
                vkEnumerateDeviceExtensionProperties(device, nullptr, &count, deviceExtensions.data());
            deviceExtensions.resize(count);
        }

        // Extensions, with the version that made each one core.
        r.add("ext.create_renderpass2",
            extensionStatus(deviceExtensions, "VK_KHR_create_renderpass2", api, VK_API_VERSION_1_2));
        r.add("ext.depth_stencil_resolve",
            extensionStatus(deviceExtensions, "VK_KHR_depth_stencil_resolve", api, VK_API_VERSION_1_2));
        r.add("ext.multiview", extensionStatus(deviceExtensions, "VK_KHR_multiview", api, VK_API_VERSION_1_1));
        r.add("ext.push_descriptor", extensionStatus(deviceExtensions, "VK_KHR_push_descriptor", api, 0));
        r.add("ext.descriptor_indexing",
            extensionStatus(deviceExtensions, "VK_EXT_descriptor_indexing", api, VK_API_VERSION_1_2));
        r.add("ext.16bit_storage", extensionStatus(deviceExtensions, "VK_KHR_16bit_storage", api, VK_API_VERSION_1_1));
        r.add("ext.shader_float16_int8",
            extensionStatus(deviceExtensions, "VK_KHR_shader_float16_int8", api, VK_API_VERSION_1_2));
        r.add("ext.acceleration_structure", extensionStatus(deviceExtensions, "VK_KHR_acceleration_structure", api, 0));
        r.add("ext.ray_query", extensionStatus(deviceExtensions, "VK_KHR_ray_query", api, 0));
        r.add("ext.hdr_metadata", extensionStatus(deviceExtensions, "VK_EXT_hdr_metadata", api, 0));
        r.add("ext.driver_properties",
            extensionStatus(deviceExtensions, "VK_KHR_driver_properties", api, VK_API_VERSION_1_2));

        // Features and properties that need the "2" queries (Vulkan 1.1).
        const std::string needs11 = "unknown (needs Vulkan 1.1)";
        const std::string needs12 = "unknown (needs Vulkan 1.2)";
        auto getFeatures2 = instanceProc<PFN_vkGetPhysicalDeviceFeatures2>(probe, "vkGetPhysicalDeviceFeatures2");
        auto getProperties2 = instanceProc<PFN_vkGetPhysicalDeviceProperties2>(probe, "vkGetPhysicalDeviceProperties2");
        const bool canAsk
            = probeApi >= VK_API_VERSION_1_1 && api >= VK_API_VERSION_1_1 && getFeatures2 && getProperties2;

        VkPhysicalDeviceFeatures coreFeatures{};
        vkGetPhysicalDeviceFeatures(device, &coreFeatures);
        r.add("feature.shaderInt16", coreFeatures.shaderInt16 == VK_TRUE);

        if (canAsk)
        {
            VkPhysicalDeviceMultiviewFeatures multiview
                = makeStruct<VkPhysicalDeviceMultiviewFeatures>(VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_MULTIVIEW_FEATURES);
            VkPhysicalDevice16BitStorageFeatures storage16 = makeStruct<VkPhysicalDevice16BitStorageFeatures>(
                VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_16BIT_STORAGE_FEATURES);
            VkPhysicalDeviceVulkan12Features v12
                = makeStruct<VkPhysicalDeviceVulkan12Features>(VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_2_FEATURES);
            VkPhysicalDeviceRayQueryFeaturesKHR rayQuery = makeStruct<VkPhysicalDeviceRayQueryFeaturesKHR>(
                VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_RAY_QUERY_FEATURES_KHR);
            VkPhysicalDeviceAccelerationStructureFeaturesKHR accel
                = makeStruct<VkPhysicalDeviceAccelerationStructureFeaturesKHR>(
                    VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_ACCELERATION_STRUCTURE_FEATURES_KHR);
            const bool haveV12 = api >= VK_API_VERSION_1_2;
            const bool haveRay = hasExtension(deviceExtensions, "VK_KHR_ray_query");
            const bool haveAccel = hasExtension(deviceExtensions, "VK_KHR_acceleration_structure");

            VkPhysicalDeviceFeatures2 features2
                = makeStruct<VkPhysicalDeviceFeatures2>(VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2);
            void** tail = &features2.pNext;
            auto chain = [&tail](auto& s) {
                *tail = &s;
                tail = &s.pNext;
            };
            chain(multiview);
            chain(storage16);
            if (haveV12)
                chain(v12);
            if (haveRay)
                chain(rayQuery);
            if (haveAccel)
                chain(accel);
            getFeatures2(device, &features2);

            r.add("feature.multiview", multiview.multiview == VK_TRUE);
            r.add("feature.storageBuffer16BitAccess", storage16.storageBuffer16BitAccess == VK_TRUE);
            r.add(
                "feature.uniformAndStorageBuffer16BitAccess", storage16.uniformAndStorageBuffer16BitAccess == VK_TRUE);
            r.add("feature.storagePushConstant16", storage16.storagePushConstant16 == VK_TRUE);
            r.add("feature.rayQuery", haveRay ? rayQuery.rayQuery == VK_TRUE : false);
            r.add("feature.accelerationStructure", haveAccel ? accel.accelerationStructure == VK_TRUE : false);
            if (haveV12)
            {
                r.add("feature.shaderFloat16", v12.shaderFloat16 == VK_TRUE);
                r.add("feature.shaderInt8", v12.shaderInt8 == VK_TRUE);
                r.add("feature.descriptorIndexing", v12.descriptorIndexing == VK_TRUE);
                r.add("feature.runtimeDescriptorArray", v12.runtimeDescriptorArray == VK_TRUE);
                r.add("feature.descriptorBindingPartiallyBound", v12.descriptorBindingPartiallyBound == VK_TRUE);
                r.add("feature.descriptorBindingVariableDescriptorCount",
                    v12.descriptorBindingVariableDescriptorCount == VK_TRUE);
                r.add("feature.shaderSampledImageArrayNonUniformIndexing",
                    v12.shaderSampledImageArrayNonUniformIndexing == VK_TRUE);
                r.add("feature.hostQueryReset", v12.hostQueryReset == VK_TRUE);
                r.add("feature.timelineSemaphore", v12.timelineSemaphore == VK_TRUE);
                r.add("feature.bufferDeviceAddress", v12.bufferDeviceAddress == VK_TRUE);
                r.add("feature.drawIndirectCount", v12.drawIndirectCount == VK_TRUE);
            }
            else
            {
                for (const char* key : { "feature.shaderFloat16", "feature.shaderInt8", "feature.descriptorIndexing",
                         "feature.runtimeDescriptorArray", "feature.descriptorBindingPartiallyBound",
                         "feature.descriptorBindingVariableDescriptorCount",
                         "feature.shaderSampledImageArrayNonUniformIndexing", "feature.hostQueryReset",
                         "feature.timelineSemaphore", "feature.bufferDeviceAddress", "feature.drawIndirectCount" })
                    r.add(key, needs12);
            }

            VkPhysicalDeviceSubgroupProperties subgroup
                = makeStruct<VkPhysicalDeviceSubgroupProperties>(VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SUBGROUP_PROPERTIES);
            VkPhysicalDeviceDriverProperties driver
                = makeStruct<VkPhysicalDeviceDriverProperties>(VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_DRIVER_PROPERTIES);
            VkPhysicalDeviceProperties2 properties2
                = makeStruct<VkPhysicalDeviceProperties2>(VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PROPERTIES_2);
            properties2.pNext = &subgroup;
            if (haveV12)
                subgroup.pNext = &driver;
            getProperties2(device, &properties2);

            r.add("subgroup.size", std::to_string(subgroup.subgroupSize));
            r.add("subgroup.stages", flagNames(subgroup.supportedStages, subgroupStages));
            r.add("subgroup.operations", flagNames(subgroup.supportedOperations, subgroupOps));
            r.add("subgroup.quadInAllStages", subgroup.quadOperationsInAllStages == VK_TRUE);
            if (haveV12)
            {
                r.add("driver.name", driver.driverName);
                r.add("driver.info", driver.driverInfo);
            }
            else
            {
                r.add("driver.name", needs12);
                r.add("driver.info", needs12);
            }
        }
        else
        {
            for (const char* key : { "feature.multiview", "feature.storageBuffer16BitAccess",
                     "feature.uniformAndStorageBuffer16BitAccess", "feature.storagePushConstant16", "feature.rayQuery",
                     "feature.accelerationStructure", "feature.shaderFloat16", "feature.shaderInt8",
                     "feature.descriptorIndexing", "feature.runtimeDescriptorArray",
                     "feature.descriptorBindingPartiallyBound", "feature.descriptorBindingVariableDescriptorCount",
                     "feature.shaderSampledImageArrayNonUniformIndexing", "feature.hostQueryReset",
                     "feature.timelineSemaphore", "feature.bufferDeviceAddress", "feature.drawIndirectCount",
                     "subgroup.size", "subgroup.stages", "subgroup.operations", "subgroup.quadInAllStages",
                     "driver.name", "driver.info" })
                r.add(key, needs11);
        }

        vkDestroyInstance(probe, nullptr);

        // Surface formats belong to the game's instance, so they are asked of the game's physical device.
        if (used && surface)
        {
            uint32_t count = 0;
            vkGetPhysicalDeviceSurfaceFormatsKHR(used->vk(), surface->vk(), &count, nullptr);
            std::vector<VkSurfaceFormatKHR> formats(count);
            if (count)
                vkGetPhysicalDeviceSurfaceFormatsKHR(used->vk(), surface->vk(), &count, formats.data());
            formats.resize(count);
            std::set<std::string> spaces;
            bool hdr10 = false;
            bool floatFormat = false;
            for (const auto& f : formats)
            {
                spaces.insert(colorSpaceName(f.colorSpace));
                hdr10 = hdr10 || f.colorSpace == VK_COLOR_SPACE_HDR10_ST2084_EXT;
                floatFormat = floatFormat || f.format == VK_FORMAT_R16G16B16A16_SFLOAT;
            }
            std::string joined;
            for (const std::string& s : spaces)
                joined += (joined.empty() ? "" : ",") + s;
            r.add("surface.colorSpaces", joined.empty() ? "none" : joined);
            r.add("surface.hdr10_st2084", hdr10);
            r.add("surface.float16Format", floatFormat);
        }
        else
        {
            r.add("surface.colorSpaces", "unknown (no window surface was given)");
            r.add("surface.hdr10_st2084", "unknown (no window surface was given)");
            r.add("surface.float16Format", "unknown (no window surface was given)");
        }
        return r;
    }
}
