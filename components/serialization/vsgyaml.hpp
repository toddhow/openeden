#ifndef OPENEDEN_COMPONENTS_SERIALIZATION_VSGYAML_H
#define OPENEDEN_COMPONENTS_SERIALIZATION_VSGYAML_H

#include <cstddef>

#include <yaml-cpp/yaml.h>

#include <vsg/maths/vec2.h>
#include <vsg/maths/vec3.h>
#include <vsg/maths/vec4.h>

// VSG vectors as YAML sequences, in the same form as osgyaml.hpp, so one file reads through either.
namespace Serialization
{
    template <class Vec>
    YAML::Node encodeVsgVec(const Vec& rhs)
    {
        YAML::Node node;
        for (std::size_t i = 0; i < Vec::size(); ++i)
            node.push_back(rhs[i]);

        return node;
    }

    template <class Vec>
    bool decodeVsgVec(const YAML::Node& node, Vec& rhs)
    {
        if (!node.IsSequence() || node.size() != Vec::size())
            return false;

        for (std::size_t i = 0; i < Vec::size(); ++i)
            rhs[i] = node[i].as<typename Vec::value_type>();

        return true;
    }
}

namespace YAML
{
    template <>
    struct convert<vsg::vec2>
    {
        static Node encode(const vsg::vec2& rhs) { return Serialization::encodeVsgVec(rhs); }

        static bool decode(const Node& node, vsg::vec2& rhs) { return Serialization::decodeVsgVec(node, rhs); }
    };

    template <>
    struct convert<vsg::vec3>
    {
        static Node encode(const vsg::vec3& rhs) { return Serialization::encodeVsgVec(rhs); }

        static bool decode(const Node& node, vsg::vec3& rhs) { return Serialization::decodeVsgVec(node, rhs); }
    };

    template <>
    struct convert<vsg::vec4>
    {
        static Node encode(const vsg::vec4& rhs) { return Serialization::encodeVsgVec(rhs); }

        static bool decode(const Node& node, vsg::vec4& rhs) { return Serialization::decodeVsgVec(node, rhs); }
    };
}

#endif
