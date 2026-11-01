#pragma once

// The animation graph (task 0020, stage C3d): states playing a clip or a
// one-dimensional blend, transitions with conditions, and parameters a host
// sets. Format, merging rules, validation and canonical form:
// docs/formats/animgraph.md. GraphInstance.hpp plays a graph.
//
// Ownership: plain values.

#include "opas/anim/AnimationPlayer.hpp"
#include "opas/format/LoadError.hpp"

#include <filesystem>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace opas::anim
{

    enum class ParameterType
    {
        Number,
        Bool
    };

    struct GraphParameter
    {
        std::string id;
        ParameterType type = ParameterType::Number;
        double defaultValue = 0.0; // a bool parameter holds 0 (false) or 1 (true)
        friend bool operator==(const GraphParameter&, const GraphParameter&) = default;
    };

    struct BlendSample
    {
        std::string clip;
        double at = 0.0; // the parameter value where this clip plays alone
        friend bool operator==(const BlendSample&, const BlendSample&) = default;
    };

    struct Blend1D
    {
        std::string parameter; // a number parameter
        std::vector<BlendSample> samples; // at least 2, `at` strictly increasing
        friend bool operator==(const Blend1D&, const Blend1D&) = default;
    };

    struct GraphState
    {
        std::string id;
        std::string clip; // exactly one of clip and blend
        std::optional<Blend1D> blend;
        bool loop = true;
        double speed = 1.0; // clip seconds per second, finite and >= 0
        friend bool operator==(const GraphState&, const GraphState&) = default;
    };

    enum class CompareOp
    {
        Less,
        LessEqual,
        Greater,
        GreaterEqual,
        Equal,
        NotEqual
    };
    enum class ConditionKind
    {
        Parameter,
        Event,
        ClipEnd
    };

    struct GraphCondition
    {
        ConditionKind kind = ConditionKind::Parameter;
        std::string parameter; // Parameter
        CompareOp op = CompareOp::Equal;
        double value = 0.0; // Parameter; a bool comparison uses 0 or 1
        std::string event; // Event
        friend bool operator==(const GraphCondition&, const GraphCondition&) = default;
    };

    struct GraphTransition
    {
        std::string id;
        std::string from; // a state id, or "*" for any state other than `to`
        std::string to;
        int priority = 0; // higher is tried first
        std::vector<GraphCondition> conditions; // all must hold; empty always holds
        double durationS = 0.0;
        TransitionMode mode = TransitionMode::Inertialize;
        friend bool operator==(const GraphTransition&, const GraphTransition&) = default;
    };

    struct AnimGraph
    {
        std::string id;
        std::string initialState;
        std::vector<GraphParameter> parameters;
        std::vector<GraphState> states;
        std::vector<GraphTransition> transitions;
        friend bool operator==(const AnimGraph&, const AnimGraph&) = default;
    };

    // One graph file's text, for merging.
    struct AnimGraphFile
    {
        std::string_view text;
        std::string_view fileName; // only used in errors and warnings
    };

    struct AnimGraphLoadResult
    {
        std::optional<AnimGraph> value;
        format::LoadError error; // meaningful only when value is empty
        std::vector<format::LoadError> warnings; // also filled when the graph loads
    };

    // Reads the files in order and merges them into one graph
    // (docs/formats/animgraph.md, "Merging several files"), then validates it.
    // Errors and warnings name the file, line and JSON pointer that set the
    // offending value. No files is an error. Never throws.
    AnimGraphLoadResult loadAnimGraph(std::span<const AnimGraphFile> files);
    AnimGraphLoadResult loadAnimGraphFiles(std::span<const std::filesystem::path> paths);

    // Canonical text of one graph: loading it gives an equal graph, and writing
    // that again gives the same bytes.
    std::string writeAnimGraph(const AnimGraph& graph);

    // The errors of "After merging" for a graph built in code: "" if valid, else
    // "<pointer>: <message>" with a pointer into the canonical form (for example
    // "/transitions/2/to"). Warnings are not reported here.
    std::string validateAnimGraph(const AnimGraph& graph);

} // namespace opas::anim
