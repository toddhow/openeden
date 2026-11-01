#ifndef CREATIVEMOTION_EDITOR_CONVERT_H
#define CREATIVEMOTION_EDITOR_CONVERT_H

#include <openeden/editor_module/types.hpp>

#include <opas/math/Vec3.hpp>

namespace creativemotion_editor
{
    /// CreativeMotion's Vec3 to the Editor Module API's. Both are meters with Z up, so only the type changes: this is
    /// the module's one conversion point. Meters become world units once, in Eden Studio's overlay (task 0032, Units).
    inline openeden::editor::Vec3 toEditor(const opas::math::Vec3& v)
    {
        return { v.x, v.y, v.z };
    }
}

#endif
