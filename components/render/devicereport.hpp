#ifndef OPENEDEN_RENDER_DEVICEREPORT_H
#define OPENEDEN_RENDER_DEVICEREPORT_H

#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include <vsg/core/ref_ptr.h>
#include <vsg/vk/vulkan.h>

namespace vsg
{
    class PhysicalDevice;
    class Surface;
}

namespace Render
{
    /*
     * What the machine's Vulkan can do (task 0023, F0c), as an ordered list of text key and value pairs. The point is to
     * inform the choice of Vulkan floor (research note 0008, F1), so every item research note 0008 names is here, and
     * each one says plainly if the device does not have it:
     *   "present"            the device offers it
     *   "absent"             the device does not
     *   "unknown (needs ...)" it could not be asked (for example a feature query needs Vulkan 1.1)
     * Other items are numbers or names. A missing item never throws.
     */
    class DeviceReport
    {
    public:
        using Entry = std::pair<std::string, std::string>;

        // Appends; keys stay in the order they were added, which is the order they are written in.
        void add(std::string key, std::string value);
        void add(std::string key, const char* value); // without it a string literal would pick the bool overload
        void add(std::string key, bool present);
        // The value of a key, or an empty string_view if there is none.
        std::string_view get(std::string_view key) const;
        const std::vector<Entry>& entries() const { return mEntries; }

        // One line per entry, for the log.
        std::string toLogText() const;
        // The canonical JSON: one object of strings, in entry order, two-space indent, "\n" line ends, a final newline,
        // control characters and quotes escaped, everything else (UTF-8 included) written as it is. parseJson(toJson())
        // gives the same entries and toJson() of those is the same bytes.
        std::string toJson() const;
        // Throws std::runtime_error with the position in the text when the text is not an object of strings.
        static DeviceReport parseJson(const std::string& text);

    private:
        std::vector<Entry> mEntries;
    };

    /*
     * Asks the device. The probe makes its own short-lived VkInstance at the highest Vulkan version the loader offers
     * (the game's own instance may be older than what the questions need), and finds the same physical device as
     * `used` by name, vendor and device id. Surface formats come from `used` and `surface`, because they belong to the
     * game's instance. `usedInstanceApi` is the version the game asked its instance for. `used` and `surface` may be
     * null; the entries that need them then say so.
     */
    DeviceReport collectDeviceReport(
        const vsg::PhysicalDevice* used, const vsg::Surface* surface, uint32_t usedInstanceApi);
}

#endif
