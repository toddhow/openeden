#ifndef GAME_MWWORLD_CUSTOMDATA_H
#define GAME_MWWORLD_CUSTOMDATA_H

#include <memory>
#include <typeinfo>

namespace MWWorld
{
    /// \brief Base class for the MW-class-specific part of RefData
    class CustomData
    {
    public:
        virtual ~CustomData() {}

        virtual std::unique_ptr<CustomData> clone() const = 0;

        /// The data as a T, for the class that owns it. Throws std::logic_error ("bad cast ...") when it is not
        /// one. Task 0098 move 8: this replaces the twelve typed accessors (asNpcCustomData and so on), so the
        /// core no longer names the data types of the classes in the game pack. An exact type match (the normal
        /// case, the owning class's own data) is a type_info comparison; anything else falls back to dynamic_cast.
        template <class T>
        T& as()
        {
            if (typeid(*this) == typeid(T))
                return static_cast<T&>(*this);
            if (T* result = dynamic_cast<T*>(this))
                return *result;
            throwBadCast(typeid(*this), typeid(T));
        }

        template <class T>
        const T& as() const
        {
            if (typeid(*this) == typeid(T))
                return static_cast<const T&>(*this);
            if (const T* result = dynamic_cast<const T*>(this))
                return *result;
            throwBadCast(typeid(*this), typeid(T));
        }

    private:
        [[noreturn]] static void throwBadCast(const std::type_info& from, const std::type_info& to);
    };

    template <class T>
    struct TypedCustomData : CustomData
    {
        std::unique_ptr<CustomData> clone() const final { return std::make_unique<T>(*static_cast<const T*>(this)); }
    };
}

#endif
