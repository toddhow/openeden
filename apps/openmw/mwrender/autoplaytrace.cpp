#include "autoplaytrace.hpp"

#include <cstdlib>
#include <iomanip>
#include <sstream>
#include <string>

#include <vsg/lighting/Light.h>

#include <components/animation/autoplay.hpp>
#include <components/debug/debuglog.hpp>
#include <components/misc/strings/lower.hpp>

namespace MWRender
{
    namespace
    {
        const std::string& tracedText()
        {
            static const std::string text = [] {
                const char* value = std::getenv("OPENEDEN_AUTOPLAY_TRACE");
                return value == nullptr ? std::string() : Misc::StringUtils::lowerCase(value);
            }();
            return text;
        }

        void writeGroups(std::ostringstream& line, const Anim::AutoPlay& clock)
        {
            for (const Anim::AutoPlay::Group& group : clock.groups)
            {
                line << "[g" << group.phaseGroup << " t=" << group.timer << " n=" << group.controllers.size();
                for (const auto& [controller, target] : group.controllers)
                {
                    if (const auto* light = dynamic_cast<const vsg::Light*>(target))
                        line << " light=(" << light->color.r << ',' << light->color.g << ',' << light->color.b << ')';
                }
                line << ']';
            }
        }
    }

    bool autoPlayTraced(std::string_view recordId, std::string_view name)
    {
        const std::string& text = tracedText();
        if (text.empty())
            return false;
        return Misc::StringUtils::lowerCase(recordId).find(text) != std::string::npos
            || Misc::StringUtils::lowerCase(name).find(text) != std::string::npos;
    }

    void traceAutoPlay(std::string_view who, unsigned frame, float dt, const Anim::AutoPlay& own,
        std::span<const Anim::AutoPlay> parts, const ListSizes& lists)
    {
        std::ostringstream line;
        line << std::setprecision(9) << "AUTOPLAYTRACE " << who << " f=" << frame << " dt=" << dt
             << " static=" << lists.mStatic << " weapon=" << lists.mWeapon << " face=" << lists.mFace << " own=";
        writeGroups(line, own);
        line << " parts=";
        for (std::size_t part = 0; part < parts.size(); ++part)
        {
            if (parts[part].groups.empty())
                continue;
            line << '{' << part << ':';
            writeGroups(line, parts[part]);
            line << '}';
        }
        Log(Debug::Info) << line.str();
    }

    void traceAutoPlayEvent(std::string_view who, std::string_view event)
    {
        Log(Debug::Info) << "AUTOPLAYTRACE " << who << " event=" << event;
    }
}
