#ifndef OPENEDEN_TEST_SUITE_RESOURCE_COLLADADOCUMENTS_H
#define OPENEDEN_TEST_SUITE_RESOURCE_COLLADADOCUMENTS_H

// Builders for small COLLADA 1.4.1 documents used by the assimp and .dae animation tests. The layout follows what
// Blender's COLLADA exporter writes: matrix node transforms, skins with <vertex_weights> and matrix animations.

#include <cstddef>
#include <sstream>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace ColladaTesting
{
    struct Position
    {
        float mX;
        float mY;
        float mZ;
    };

    /// One vertex of a skinned mesh: its position and its (joint name, weight) pairs, in file order.
    struct SkinnedVertex
    {
        Position mPosition;
        std::vector<std::pair<std::string, float>> mWeights;
    };

    inline constexpr std::string_view identityMatrix = "1 0 0 0 0 1 0 0 0 0 1 0 0 0 0 1";

    /// A row-major COLLADA matrix with the given translation and no rotation.
    inline std::string makeTranslationMatrix(float x, float y, float z)
    {
        std::ostringstream stream;
        stream << "1 0 0 " << x << " 0 1 0 " << y << " 0 0 1 " << z << " 0 0 0 1";
        return stream.str();
    }

    /// A <geometry> with the given positions and triangles (indices into positions).
    inline std::string makeGeometry(
        std::string_view id, const std::vector<Position>& positions, const std::vector<unsigned>& triangles)
    {
        std::ostringstream stream;
        stream << "<geometry id=\"" << id << "\" name=\"" << id << "\"><mesh>";
        stream << "<source id=\"" << id << "-positions\"><float_array id=\"" << id << "-positions-array\" count=\""
               << positions.size() * 3 << "\">";
        for (const Position& position : positions)
            stream << position.mX << ' ' << position.mY << ' ' << position.mZ << ' ';
        stream << "</float_array><technique_common><accessor source=\"#" << id << "-positions-array\" count=\""
               << positions.size()
               << "\" stride=\"3\"><param name=\"X\" type=\"float\"/><param name=\"Y\" type=\"float\"/>"
                  "<param name=\"Z\" type=\"float\"/></accessor></technique_common></source>";
        stream << "<vertices id=\"" << id << "-vertices\"><input semantic=\"POSITION\" source=\"#" << id
               << "-positions\"/></vertices>";
        stream << "<triangles count=\"" << triangles.size() / 3 << "\"><input semantic=\"VERTEX\" source=\"#" << id
               << "-vertices\" offset=\"0\"/><p>";
        for (std::size_t i = 0; i < triangles.size(); ++i)
            stream << (i == 0 ? "" : " ") << triangles[i];
        stream << "</p></triangles></mesh></geometry>";
        return stream.str();
    }

    /// A closed axis-aligned box from (0, 0, 0) to (1, 1, 1).
    inline std::string makeUnitBoxGeometry(std::string_view id)
    {
        std::vector<Position> positions;
        for (int i = 0; i < 8; ++i)
            positions.push_back({ (i & 1) ? 1.f : 0.f, (i & 2) ? 1.f : 0.f, (i & 4) ? 1.f : 0.f });
        return makeGeometry(id, positions,
            { 0, 2, 3, 0, 3, 1, 4, 5, 7, 4, 7, 6, 0, 1, 5, 0, 5, 4, 2, 6, 7, 2, 7, 3, 0, 4, 6, 0, 6, 2, 1, 3, 7, 1, 7,
                5 });
    }

    /// A <controller> skinning geometry `meshId` to `joints` (their sids). Bind shape and inverse bind matrices are
    /// identity. `vcountPrefix` is written before the first <vcount> entry, to reproduce exporters that start the
    /// list with whitespace.
    inline std::string makeSkinController(std::string_view id, std::string_view meshId,
        const std::vector<std::string>& joints, const std::vector<SkinnedVertex>& vertices,
        std::string_view vcountPrefix)
    {
        std::vector<float> weights;
        std::ostringstream vcount;
        std::ostringstream v;
        vcount << vcountPrefix;
        for (std::size_t i = 0; i < vertices.size(); ++i)
        {
            vcount << (i == 0 ? "" : " ") << vertices[i].mWeights.size();
            for (const auto& [joint, weight] : vertices[i].mWeights)
            {
                std::size_t jointIndex = 0;
                while (jointIndex < joints.size() && joints[jointIndex] != joint)
                    ++jointIndex;
                if (v.tellp() > 0)
                    v << ' ';
                v << jointIndex << ' ' << weights.size();
                weights.push_back(weight);
            }
        }

        std::ostringstream stream;
        stream << "<controller id=\"" << id << "\" name=\"" << id << "\"><skin source=\"#" << meshId << "\">";
        stream << "<bind_shape_matrix>" << identityMatrix << "</bind_shape_matrix>";
        stream << "<source id=\"" << id << "-joints\"><Name_array id=\"" << id << "-joints-array\" count=\""
               << joints.size() << "\">";
        for (std::size_t i = 0; i < joints.size(); ++i)
            stream << (i == 0 ? "" : " ") << joints[i];
        stream << "</Name_array><technique_common><accessor source=\"#" << id << "-joints-array\" count=\""
               << joints.size()
               << "\" stride=\"1\"><param name=\"JOINT\" type=\"name\"/></accessor></technique_common></source>";
        stream << "<source id=\"" << id << "-bind_poses\"><float_array id=\"" << id << "-bind_poses-array\" count=\""
               << joints.size() * 16 << "\">";
        for (std::size_t i = 0; i < joints.size(); ++i)
            stream << (i == 0 ? "" : " ") << identityMatrix;
        stream << "</float_array><technique_common><accessor source=\"#" << id << "-bind_poses-array\" count=\""
               << joints.size()
               << "\" stride=\"16\"><param name=\"TRANSFORM\" type=\"float4x4\"/></accessor></technique_common>"
                  "</source>";
        stream << "<source id=\"" << id << "-weights\"><float_array id=\"" << id << "-weights-array\" count=\""
               << weights.size() << "\">";
        for (std::size_t i = 0; i < weights.size(); ++i)
            stream << (i == 0 ? "" : " ") << weights[i];
        stream << "</float_array><technique_common><accessor source=\"#" << id << "-weights-array\" count=\""
               << weights.size()
               << "\" stride=\"1\"><param name=\"WEIGHT\" type=\"float\"/></accessor></technique_common></source>";
        stream << "<joints><input semantic=\"JOINT\" source=\"#" << id
               << "-joints\"/><input semantic=\"INV_BIND_MATRIX\" source=\"#" << id << "-bind_poses\"/></joints>";
        stream << "<vertex_weights count=\"" << vertices.size() << "\"><input semantic=\"JOINT\" source=\"#" << id
               << "-joints\" offset=\"0\"/><input semantic=\"WEIGHT\" source=\"#" << id << "-weights\" offset=\"1\"/>";
        stream << "<vcount>" << vcount.str() << "</vcount><v>" << v.str() << "</v></vertex_weights>";
        stream << "</skin></controller>";
        return stream.str();
    }

    /// A <node>. `content` holds its transform elements, instances and child nodes. A joint gets its name as sid.
    inline std::string makeNode(
        std::string_view id, std::string_view name, std::string_view content, bool joint = false)
    {
        std::ostringstream stream;
        stream << "<node id=\"" << id << "\" name=\"" << name << "\"";
        if (joint)
            stream << " sid=\"" << name << "\" type=\"JOINT\"";
        else
            stream << " type=\"NODE\"";
        stream << ">" << content << "</node>";
        return stream.str();
    }

    inline std::string makeMatrix(std::string_view matrix)
    {
        return "<matrix sid=\"transform\">" + std::string(matrix) + "</matrix>";
    }

    inline std::string makeInstanceGeometry(std::string_view geometryId)
    {
        return "<instance_geometry url=\"#" + std::string(geometryId) + "\"/>";
    }

    inline std::string makeInstanceController(std::string_view controllerId, std::string_view skeletonNodeId)
    {
        return "<instance_controller url=\"#" + std::string(controllerId) + "\"><skeleton>#"
            + std::string(skeletonNodeId) + "</skeleton></instance_controller>";
    }

    /// An <animation> of the matrix of node `targetNodeId` (`<channel target="targetNodeId/transform"/>`), with one
    /// row-major 4x4 matrix per sampler time, as Blender exports.
    inline std::string makeMatrixAnimation(std::string_view id, std::string_view targetNodeId,
        const std::vector<float>& times, const std::vector<std::string>& matrices)
    {
        std::ostringstream stream;
        stream << "<animation id=\"" << id << "\" name=\"" << id << "\">";
        stream << "<source id=\"" << id << "-input\"><float_array id=\"" << id << "-input-array\" count=\""
               << times.size() << "\">";
        for (std::size_t i = 0; i < times.size(); ++i)
            stream << (i == 0 ? "" : " ") << times[i];
        stream << "</float_array><technique_common><accessor source=\"#" << id << "-input-array\" count=\""
               << times.size()
               << "\" stride=\"1\"><param name=\"TIME\" type=\"float\"/></accessor></technique_common></source>";
        stream << "<source id=\"" << id << "-output\"><float_array id=\"" << id << "-output-array\" count=\""
               << matrices.size() * 16 << "\">";
        for (std::size_t i = 0; i < matrices.size(); ++i)
            stream << (i == 0 ? "" : " ") << matrices[i];
        stream << "</float_array><technique_common><accessor source=\"#" << id << "-output-array\" count=\""
               << matrices.size()
               << "\" stride=\"16\"><param name=\"TRANSFORM\" type=\"float4x4\"/></accessor></technique_common>"
                  "</source>";
        stream << "<source id=\"" << id << "-interpolation\"><Name_array id=\"" << id
               << "-interpolation-array\" count=\"" << times.size() << "\">";
        for (std::size_t i = 0; i < times.size(); ++i)
            stream << (i == 0 ? "" : " ") << "LINEAR";
        stream << "</Name_array><technique_common><accessor source=\"#" << id << "-interpolation-array\" count=\""
               << times.size()
               << "\" stride=\"1\"><param name=\"INTERPOLATION\" type=\"name\"/></accessor></technique_common>"
                  "</source>";
        stream << "<sampler id=\"" << id << "-sampler\"><input semantic=\"INPUT\" source=\"#" << id
               << "-input\"/><input semantic=\"OUTPUT\" source=\"#" << id
               << "-output\"/><input semantic=\"INTERPOLATION\" source=\"#" << id << "-interpolation\"/></sampler>";
        stream << "<channel source=\"#" << id << "-sampler\" target=\"" << targetNodeId << "/transform\"/>";
        stream << "</animation>";
        return stream.str();
    }

    /// A Blender 2.8+ style container <animation> holding nested animations (e.g. from makeMatrixAnimation).
    inline std::string makeAnimationContainer(std::string_view id, std::string_view nestedAnimations)
    {
        return "<animation id=\"" + std::string(id) + "\" name=\"" + std::string(id) + "\">"
            + std::string(nestedAnimations) + "</animation>";
    }

    /// One <animation> with a single time <source> shared by one matrix sampler and channel per target node.
    /// `matrices[i]` holds the matrices (one per time) of node `targetNodeIds[i]`.
    inline std::string makeSharedTimeMatrixAnimation(std::string_view id, const std::vector<std::string>& targetNodeIds,
        const std::vector<float>& times, const std::vector<std::vector<std::string>>& matrices)
    {
        std::ostringstream stream;
        stream << "<animation id=\"" << id << "\" name=\"" << id << "\">";
        stream << "<source id=\"" << id << "-input\"><float_array id=\"" << id << "-input-array\" count=\""
               << times.size() << "\">";
        for (std::size_t i = 0; i < times.size(); ++i)
            stream << (i == 0 ? "" : " ") << times[i];
        stream << "</float_array><technique_common><accessor source=\"#" << id << "-input-array\" count=\""
               << times.size()
               << "\" stride=\"1\"><param name=\"TIME\" type=\"float\"/></accessor></technique_common></source>";
        stream << "<source id=\"" << id << "-interpolation\"><Name_array id=\"" << id
               << "-interpolation-array\" count=\"" << times.size() << "\">";
        for (std::size_t i = 0; i < times.size(); ++i)
            stream << (i == 0 ? "" : " ") << "LINEAR";
        stream << "</Name_array><technique_common><accessor source=\"#" << id << "-interpolation-array\" count=\""
               << times.size()
               << "\" stride=\"1\"><param name=\"INTERPOLATION\" type=\"name\"/></accessor></technique_common>"
                  "</source>";
        for (std::size_t target = 0; target < targetNodeIds.size(); ++target)
        {
            const std::string outputId = std::string(id) + "-output" + std::to_string(target);
            stream << "<source id=\"" << outputId << "\"><float_array id=\"" << outputId << "-array\" count=\""
                   << matrices[target].size() * 16 << "\">";
            for (std::size_t i = 0; i < matrices[target].size(); ++i)
                stream << (i == 0 ? "" : " ") << matrices[target][i];
            stream << "</float_array><technique_common><accessor source=\"#" << outputId << "-array\" count=\""
                   << matrices[target].size()
                   << "\" stride=\"16\"><param name=\"TRANSFORM\" type=\"float4x4\"/></accessor></technique_common>"
                      "</source>";
        }
        for (std::size_t target = 0; target < targetNodeIds.size(); ++target)
        {
            const std::string samplerId = std::string(id) + "-sampler" + std::to_string(target);
            stream << "<sampler id=\"" << samplerId << "\"><input semantic=\"INPUT\" source=\"#" << id
                   << "-input\"/><input semantic=\"OUTPUT\" source=\"#" << id << "-output" << target
                   << "\"/><input semantic=\"INTERPOLATION\" source=\"#" << id << "-interpolation\"/></sampler>";
        }
        for (std::size_t target = 0; target < targetNodeIds.size(); ++target)
            stream << "<channel source=\"#" << id << "-sampler" << target << "\" target=\"" << targetNodeIds[target]
                   << "/transform\"/>";
        stream << "</animation>";
        return stream.str();
    }

    /// An <animation_clip> starting at `start` that instances the given animations.
    inline std::string makeAnimationClip(
        std::string_view id, float start, float end, const std::vector<std::string>& animationIds)
    {
        std::ostringstream stream;
        stream << "<animation_clip id=\"" << id << "\" name=\"" << id << "\" start=\"" << start << "\" end=\"" << end
               << "\">";
        for (const std::string& animationId : animationIds)
            stream << "<instance_animation url=\"#" << animationId << "\"/>";
        stream << "</animation_clip>";
        return stream.str();
    }

    /// The parts of a document; empty libraries are left out.
    struct DocumentParts
    {
        std::string mAnimations;
        std::string mAnimationClips;
        std::string mControllers;
        std::string mGeometries;
        std::string mNodes;
    };

    /// A complete COLLADA 1.4.1 document in meters with Z up.
    inline std::string makeColladaDocument(const DocumentParts& parts)
    {
        std::ostringstream stream;
        stream << "<?xml version=\"1.0\" encoding=\"utf-8\"?>\n"
                  "<COLLADA xmlns=\"http://www.collada.org/2005/11/COLLADASchema\" version=\"1.4.1\">"
                  "<asset><contributor><authoring_tool>components-tests</authoring_tool></contributor>"
                  "<created>2026-10-29T00:00:00</created><modified>2026-10-29T00:00:00</modified>"
                  "<unit meter=\"1.0\" name=\"meter\"/><up_axis>Z_UP</up_axis></asset>";
        if (!parts.mAnimations.empty())
            stream << "<library_animations>" << parts.mAnimations << "</library_animations>";
        if (!parts.mAnimationClips.empty())
            stream << "<library_animation_clips>" << parts.mAnimationClips << "</library_animation_clips>";
        if (!parts.mControllers.empty())
            stream << "<library_controllers>" << parts.mControllers << "</library_controllers>";
        if (!parts.mGeometries.empty())
            stream << "<library_geometries>" << parts.mGeometries << "</library_geometries>";
        stream << "<library_visual_scenes><visual_scene id=\"Scene\" name=\"Scene\">" << parts.mNodes
               << "</visual_scene></library_visual_scenes>"
                  "<scene><instance_visual_scene url=\"#Scene\"/></scene></COLLADA>\n";
        return stream.str();
    }
}

#endif
