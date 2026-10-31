#ifndef OPENMW_COMPONENTS_MISC_FINITEVALUES_HPP
#define OPENMW_COMPONENTS_MISC_FINITEVALUES_HPP


#include <components/lua/luastate.hpp>
#include <components/misc/vsgmath.hpp>

#include <cmath>
#include <stdexcept>
#include <type_traits>
#include <utility>

namespace Misc
{
    template <class T>
    struct FiniteValue
    {
        T mValue;

        FiniteValue(T v)
        {
            constexpr bool isVecType
                = std::is_same_v<vsg::vec2, T> || std::is_same_v<vsg::vec3, T> || std::is_same_v<vsg::vec4, T>;

            if constexpr (isVecType)
            {
                for (std::size_t i = 0; i < T::size(); ++i)
                    if (!std::isfinite(v[i]))
                        throw std::invalid_argument("Vector must contain finite numbers");
            }
            else
            {
                if (!std::isfinite(v))
                    throw std::invalid_argument("Value must be a finite number");
            }
            mValue = v;
        }

        operator T() const { return mValue; }
    };

    using FiniteDouble = FiniteValue<double>;

    using FiniteFloat = FiniteValue<float>;

    using FiniteVec2f = FiniteValue<vsg::vec2>;

    using FiniteVec3f = FiniteValue<vsg::vec3>;

    using FiniteVec4f = FiniteValue<vsg::vec4>;
}

namespace sol
{
    template <class Handler, class T>
    bool sol_lua_check(
        types<Misc::FiniteValue<T>>, lua_State* state, int index, Handler&& handler, stack::record& tracking)
    {
        bool success = stack::check<T>(state, lua_absindex(state, index), std::forward<Handler>(handler));
        tracking.use(1);
        return success;
    }

    template <class T>
    static Misc::FiniteValue<T> sol_lua_get(
        types<Misc::FiniteValue<T>>, lua_State* state, int index, stack::record& tracking)
    {
        T value = stack::get<T>(state, lua_absindex(state, index));
        tracking.use(1);
        return value;
    }
}

#endif
