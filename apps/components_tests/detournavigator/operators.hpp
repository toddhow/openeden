#ifndef OPENMW_TEST_SUITE_DETOURNAVIGATOR_OPERATORS_H
#define OPENMW_TEST_SUITE_DETOURNAVIGATOR_OPERATORS_H

#include <components/misc/vsgmath.hpp>
#include <components/detournavigator/debug.hpp>

#include <deque>
#include <iomanip>
#include <limits>

#include <gtest/gtest.h>

namespace
{
    template <class T>
    struct Wrapper
    {
        const T& mValue;
    };

    template <class Range>
    inline testing::Message& writeRange(testing::Message& message, const Range& range, std::size_t newLine)
    {
        message << "{";
        std::size_t i = 0;
        for (const auto& v : range)
        {
            if (i++ % newLine == 0)
                message << "\n";
            message << Wrapper<typename std::decay<decltype(v)>::type>{ v } << ", ";
        }
        return message << "\n}";
    }
}

namespace testing
{
    template <>
    inline testing::Message& Message::operator<<(const vsg::vec3& value)
    {
        return (*this) << std::setprecision(std::numeric_limits<float>::max_exponent10) << "Vec3fEq(" << value.x
                       << ", " << value.y << ", " << value.z << ')';
    }

    template <>
    inline testing::Message& Message::operator<<(const vsg::ivec2& value)
    {
        return (*this) << "{" << value.x << ", " << value.y << '}';
    }

    template <>
    inline testing::Message& Message::operator<<(const Wrapper<vsg::vec3>& value)
    {
        return (*this) << value.mValue;
    }

    template <>
    inline testing::Message& Message::operator<<(const Wrapper<vsg::ivec2>& value)
    {
        return (*this) << value.mValue;
    }

    template <>
    inline testing::Message& Message::operator<<(const Wrapper<float>& value)
    {
        return (*this) << std::setprecision(std::numeric_limits<float>::max_exponent10) << value.mValue;
    }

    template <>
    inline testing::Message& Message::operator<<(const Wrapper<int>& value)
    {
        return (*this) << value.mValue;
    }

    template <>
    inline testing::Message& Message::operator<<(const std::deque<vsg::vec3>& value)
    {
        return writeRange(*this, value, 1);
    }

    template <>
    inline testing::Message& Message::operator<<(const std::vector<vsg::vec3>& value)
    {
        return writeRange(*this, value, 1);
    }

    template <>
    inline testing::Message& Message::operator<<(const std::vector<vsg::ivec2>& value)
    {
        return writeRange(*this, value, 1);
    }

    template <>
    inline testing::Message& Message::operator<<(const std::vector<float>& value)
    {
        return writeRange(*this, value, 3);
    }

    template <>
    inline testing::Message& Message::operator<<(const std::vector<int>& value)
    {
        return writeRange(*this, value, 3);
    }
}

#endif
