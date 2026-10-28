#include "opas/anim/AnimGraph.hpp"

#include "opas/format/FileReader.hpp"
#include "opas/format/FileWriter.hpp"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <limits>
#include <map>
#include <set>
#include <utility>

namespace opas::anim {
namespace {

// Rules: docs/formats/animgraph.md.

using format::detail::FileReader;
using format::detail::Json;
using format::detail::jsonString;
using format::detail::number;

constexpr std::string_view kFormatName = "opas.animgraph";
constexpr std::string_view kAnyState = "*";
constexpr std::size_t kNoFile = std::numeric_limits<std::size_t>::max();

// --- Where a value came from ----------------------------------------------------

// The file (index into the merged list), line and JSON pointer that set a
// value; a graph built in code has kNoFile, line 0 and canonical pointers.
struct Origin {
    std::size_t file = kNoFile;
    std::size_t line = 0;
    std::string pointer;
};

// A rule broken, where it was set and why.
struct Located {
    Origin origin;
    std::string message;
};

template <typename T>
struct Field {
    std::optional<T> value;
    Origin origin;
};

// The value of a field validation has already required; a default-made value
// if it is somehow absent, so a broken invariant cannot read an empty optional.
template <typename T>
const T& get(const Field<T>& field)
{
    static const T kEmpty{};
    return field.value ? *field.value : kEmpty;
}

enum class ValueKind : std::uint8_t { Unknown, Number, Bool }; // Unknown: built in code

struct TypedValue {
    double value = 0.0;
    ValueKind kind = ValueKind::Unknown;
    friend bool operator==(const TypedValue&, const TypedValue&) = default;
};

struct ConditionDraft {
    GraphCondition condition;
    ValueKind valueKind = ValueKind::Unknown;
    Origin origin;      // the condition object
    Origin valueOrigin; // its "value" (or the object)
    Origin opOrigin;    // its "op" (or the object)
    Origin paramOrigin; // its "parameter" (or the object)
};

bool sameValue(const std::vector<ConditionDraft>& a, const std::vector<ConditionDraft>& b)
{
    if (a.size() != b.size()) {
        return false;
    }
    for (std::size_t i = 0; i < a.size(); ++i) {
        if (!(a[i].condition == b[i].condition) || a[i].valueKind != b[i].valueKind) {
            return false;
        }
    }
    return true;
}

struct BlendDraft {
    Blend1D blend;
    Origin parameterOrigin;
};

bool sameValue(const BlendDraft& a, const BlendDraft& b)
{
    return a.blend == b.blend;
}

template <typename T>
bool sameValue(const T& a, const T& b)
{
    return a == b;
}

struct ParameterDraft {
    std::string id;
    Origin idOrigin;
    Field<ParameterType> type;
    Field<TypedValue> defaultValue;
};

struct StateDraft {
    std::string id;
    Origin idOrigin;
    Field<std::string> clip;
    Field<BlendDraft> blend;
    Field<bool> loop;
    Field<double> speed;
};

struct TransitionDraft {
    std::string id;
    Origin idOrigin;
    Field<std::string> from;
    Field<std::string> to;
    Field<int> priority;
    Field<std::vector<ConditionDraft>> conditions;
    Field<double> durationS;
    Field<TransitionMode> mode;
};

struct GraphDraft {
    Field<std::string> id;
    Field<std::string> initialState;
    Origin root; // the first file's document
    std::vector<ParameterDraft> parameters;
    std::vector<StateDraft> states;
    std::vector<TransitionDraft> transitions;
};

// Names must be non-empty and free of control characters (U+0000-U+001F,
// U+007F), as in the other formats.
bool isValidName(std::string_view name)
{
    if (name.empty()) {
        return false;
    }
    return std::none_of(name.begin(), name.end(), [](char c) {
        const auto u = static_cast<unsigned char>(c);
        return u < 0x20 || u == 0x7f;
    });
}

std::optional<CompareOp> opFromText(std::string_view text)
{
    if (text == "<") {
        return CompareOp::Less;
    }
    if (text == "<=") {
        return CompareOp::LessEqual;
    }
    if (text == ">") {
        return CompareOp::Greater;
    }
    if (text == ">=") {
        return CompareOp::GreaterEqual;
    }
    if (text == "==") {
        return CompareOp::Equal;
    }
    if (text == "!=") {
        return CompareOp::NotEqual;
    }
    return std::nullopt;
}

std::string_view opText(CompareOp op)
{
    switch (op) {
    case CompareOp::Less:
        return "<";
    case CompareOp::LessEqual:
        return "<=";
    case CompareOp::Greater:
        return ">";
    case CompareOp::GreaterEqual:
        return ">=";
    case CompareOp::Equal:
        return "==";
    case CompareOp::NotEqual:
        return "!=";
    }
    return "==";
}

// --- Reading one file ------------------------------------------------------------

class GraphFileReader : public FileReader {
public:
    using FileReader::FileReader;

    std::size_t fileIndex = 0;

    Origin originOf(const Json& value, std::string pointer) const
    {
        const std::size_t start = value.start_pos();
        return Origin{fileIndex, start == std::string::npos ? 1 : lineAt(start),
                      std::move(pointer)};
    }

    bool readName(const Json& value, const std::string& pointer, std::string& out)
    {
        if (!value.is_string()) {
            return fail(value, pointer, "must be a string");
        }
        out = value.get<std::string>();
        if (!isValidName(out)) {
            return fail(value, pointer, "must be non-empty, without control characters");
        }
        return true;
    }

    template <typename T, typename Read>
    bool optionalField(const Json& object, const std::string& pointer, std::string_view key,
                       Field<T>& out, Read read)
    {
        const auto it = object.find(key);
        if (it == object.end()) {
            return true;
        }
        const std::string at = pointer + "/" + std::string(key);
        T value{};
        if (!read(*it, at, value)) {
            return false;
        }
        out.value = std::move(value);
        out.origin = originOf(*it, at);
        return true;
    }

    bool readBool(const Json& value, const std::string& pointer, bool& out)
    {
        if (!value.is_boolean()) {
            return fail(value, pointer, "must be true or false");
        }
        out = value.get<bool>();
        return true;
    }

    bool readNonNegative(const Json& value, const std::string& pointer, double& out)
    {
        if (!readNumber(value, pointer, out)) {
            return false;
        }
        if (out < 0.0) {
            return fail(value, pointer, "must be at least 0");
        }
        return true;
    }

    bool readCondition(const Json& value, const std::string& pointer, ConditionDraft& out)
    {
        if (!value.is_object()) {
            return fail(value, pointer, "must be a condition object");
        }
        out.origin = originOf(value, pointer);
        out.paramOrigin = out.opOrigin = out.valueOrigin = out.origin;
        GraphCondition& c = out.condition;
        if (value.contains("parameter")) {
            if (!onlyKeys(value, pointer, {"parameter", "op", "value"})) {
                return false;
            }
            c.kind = ConditionKind::Parameter;
            const Json* parameter = member(value, pointer, "parameter");
            const Json* op = member(value, pointer, "op");
            const Json* v = member(value, pointer, "value");
            if (parameter == nullptr || op == nullptr || v == nullptr ||
                !readName(*parameter, pointer + "/parameter", c.parameter)) {
                return false;
            }
            out.paramOrigin = originOf(*parameter, pointer + "/parameter");
            out.opOrigin = originOf(*op, pointer + "/op");
            out.valueOrigin = originOf(*v, pointer + "/value");
            const std::optional<CompareOp> parsed =
                op->is_string() ? opFromText(op->get<std::string>()) : std::nullopt;
            if (!parsed) {
                return fail(*op, pointer + "/op", "must be <, <=, >, >=, == or !=");
            }
            c.op = *parsed;
            if (v->is_boolean()) {
                c.value = v->get<bool>() ? 1.0 : 0.0;
                out.valueKind = ValueKind::Bool;
                return true;
            }
            if (!readNumber(*v, pointer + "/value", c.value)) {
                return false;
            }
            out.valueKind = ValueKind::Number;
            return true;
        }
        if (value.contains("event")) {
            c.kind = ConditionKind::Event;
            return onlyKeys(value, pointer, {"event"}) &&
                   readName(value["event"], pointer + "/event", c.event);
        }
        if (value.contains("clipEnd")) {
            c.kind = ConditionKind::ClipEnd;
            if (!onlyKeys(value, pointer, {"clipEnd"})) {
                return false;
            }
            const Json& flag = value["clipEnd"];
            if (!flag.is_boolean() || !flag.get<bool>()) {
                return fail(flag, pointer + "/clipEnd", "must be true");
            }
            return true;
        }
        return fail(value, pointer, "a condition needs \"parameter\", \"event\" or \"clipEnd\"");
    }

    bool readConditions(const Json& value, const std::string& pointer,
                        std::vector<ConditionDraft>& out)
    {
        if (!value.is_array()) {
            return fail(value, pointer, "must be an array");
        }
        out.resize(value.size());
        for (std::size_t i = 0; i < value.size(); ++i) {
            if (!readCondition(value[i], pointer + "/" + std::to_string(i), out[i])) {
                return false;
            }
        }
        return true;
    }

    bool readBlend(const Json& value, const std::string& pointer, BlendDraft& out)
    {
        if (!value.is_object()) {
            return fail(value, pointer, "must be a blend object");
        }
        if (!onlyKeys(value, pointer, {"parameter", "samples"})) {
            return false;
        }
        const Json* parameter = member(value, pointer, "parameter");
        const Json* samples = member(value, pointer, "samples");
        if (parameter == nullptr || samples == nullptr ||
            !readName(*parameter, pointer + "/parameter", out.blend.parameter)) {
            return false;
        }
        out.parameterOrigin = originOf(*parameter, pointer + "/parameter");
        const std::string samplesAt = pointer + "/samples";
        if (!samples->is_array()) {
            return fail(*samples, samplesAt, "must be an array");
        }
        if (samples->size() < 2) {
            return fail(*samples, samplesAt, "a blend needs at least 2 samples");
        }
        out.blend.samples.resize(samples->size());
        for (std::size_t i = 0; i < samples->size(); ++i) {
            const std::string at = samplesAt + "/" + std::to_string(i);
            const Json& s = (*samples)[i];
            BlendSample& sample = out.blend.samples[i];
            if (!s.is_object()) {
                return fail(s, at, "must be a sample object");
            }
            const Json* clip = member(s, at, "clip");
            const Json* where = member(s, at, "at");
            if (!onlyKeys(s, at, {"clip", "at"}) || clip == nullptr || where == nullptr ||
                !readName(*clip, at + "/clip", sample.clip) ||
                !readNumber(*where, at + "/at", sample.at)) {
                return false;
            }
            if (i > 0 && !(sample.at > out.blend.samples[i - 1].at)) {
                return fail(*where, at + "/at", "must be greater than the previous sample's");
            }
        }
        return true;
    }

    bool readParameter(const Json& value, const std::string& pointer, ParameterDraft& out)
    {
        if (!value.is_object()) {
            return fail(value, pointer, "must be a parameter object");
        }
        if (!onlyKeys(value, pointer, {"id", "type", "default"}) ||
            !readId(value, pointer, out.id, out.idOrigin)) {
            return false;
        }
        const auto type = [this](const Json& v, const std::string& p, ParameterType& o) {
            const std::string text = v.is_string() ? v.get<std::string>() : std::string{};
            if (text == "number") {
                o = ParameterType::Number;
                return true;
            }
            if (text == "bool") {
                o = ParameterType::Bool;
                return true;
            }
            return fail(v, p, "must be \"number\" or \"bool\"");
        };
        const auto defaultValue = [this](const Json& v, const std::string& p, TypedValue& o) {
            if (v.is_boolean()) {
                o = TypedValue{v.get<bool>() ? 1.0 : 0.0, ValueKind::Bool};
                return true;
            }
            o.kind = ValueKind::Number;
            return readNumber(v, p, o.value);
        };
        return optionalField(value, pointer, "type", out.type, type) &&
               optionalField(value, pointer, "default", out.defaultValue, defaultValue);
    }

    bool readState(const Json& value, const std::string& pointer, StateDraft& out)
    {
        if (!value.is_object()) {
            return fail(value, pointer, "must be a state object");
        }
        if (!onlyKeys(value, pointer, {"id", "clip", "blend", "loop", "speed"}) ||
            !readId(value, pointer, out.id, out.idOrigin)) {
            return false;
        }
        if (out.id == kAnyState) {
            return fail(value["id"], pointer + "/id", "\"*\" is reserved for any state");
        }
        if (value.contains("clip") && value.contains("blend")) {
            return fail(value["blend"], pointer + "/blend",
                        "a state has either \"clip\" or \"blend\", not both");
        }
        const auto name = [this](const Json& v, const std::string& p, std::string& o) {
            return readName(v, p, o);
        };
        const auto blend = [this](const Json& v, const std::string& p, BlendDraft& o) {
            return readBlend(v, p, o);
        };
        const auto flag = [this](const Json& v, const std::string& p, bool& o) {
            return readBool(v, p, o);
        };
        const auto speed = [this](const Json& v, const std::string& p, double& o) {
            return readNonNegative(v, p, o);
        };
        return optionalField(value, pointer, "clip", out.clip, name) &&
               optionalField(value, pointer, "blend", out.blend, blend) &&
               optionalField(value, pointer, "loop", out.loop, flag) &&
               optionalField(value, pointer, "speed", out.speed, speed);
    }

    bool readTransition(const Json& value, const std::string& pointer, TransitionDraft& out)
    {
        if (!value.is_object()) {
            return fail(value, pointer, "must be a transition object");
        }
        if (!onlyKeys(value, pointer,
                      {"id", "from", "to", "priority", "conditions", "durationS", "mode"}) ||
            !readId(value, pointer, out.id, out.idOrigin)) {
            return false;
        }
        const auto name = [this](const Json& v, const std::string& p, std::string& o) {
            return readName(v, p, o);
        };
        const auto integer = [this](const Json& v, const std::string& p, int& o) {
            return readInt(v, p, o);
        };
        const auto conditions = [this](const Json& v, const std::string& p,
                                       std::vector<ConditionDraft>& o) {
            return readConditions(v, p, o);
        };
        const auto duration = [this](const Json& v, const std::string& p, double& o) {
            return readNonNegative(v, p, o);
        };
        const auto mode = [this](const Json& v, const std::string& p, TransitionMode& o) {
            const std::string text = v.is_string() ? v.get<std::string>() : std::string{};
            if (text == "inertialize") {
                o = TransitionMode::Inertialize;
                return true;
            }
            if (text == "crossfade") {
                o = TransitionMode::Crossfade;
                return true;
            }
            return fail(v, p, "must be \"inertialize\" or \"crossfade\"");
        };
        return optionalField(value, pointer, "from", out.from, name) &&
               optionalField(value, pointer, "to", out.to, name) &&
               optionalField(value, pointer, "priority", out.priority, integer) &&
               optionalField(value, pointer, "conditions", out.conditions, conditions) &&
               optionalField(value, pointer, "durationS", out.durationS, duration) &&
               optionalField(value, pointer, "mode", out.mode, mode);
    }

    template <typename Draft, typename Read>
    bool readEntries(const Json& root, std::string_view key, std::vector<Draft>& out, Read read)
    {
        const auto it = root.find(key);
        if (it == root.end()) {
            return true;
        }
        const std::string pointer = "/" + std::string(key);
        if (!it->is_array()) {
            return fail(*it, pointer, "must be an array");
        }
        out.resize(it->size());
        for (std::size_t i = 0; i < it->size(); ++i) {
            if (!read((*it)[i], pointer + "/" + std::to_string(i), out[i])) {
                return false;
            }
        }
        return true;
    }

    bool readDocument(const Json& root, GraphDraft& out)
    {
        if (!readHeader(root, kFormatName,
                        {"format", "version", "id", "initialState", "parameters", "states",
                         "transitions"})) {
            return false;
        }
        out.root = originOf(root, "");
        const Json* id = member(root, "", "id");
        std::string graphId;
        if (id == nullptr || !readName(*id, "/id", graphId)) {
            return false;
        }
        out.id = Field<std::string>{graphId, originOf(*id, "/id")};
        const auto name = [this](const Json& v, const std::string& p, std::string& o) {
            return readName(v, p, o);
        };
        return optionalField(root, "", "initialState", out.initialState, name) &&
               readEntries(root, "parameters", out.parameters,
                           [this](const Json& v, const std::string& p, ParameterDraft& o) {
                               return readParameter(v, p, o);
                           }) &&
               readEntries(root, "states", out.states,
                           [this](const Json& v, const std::string& p, StateDraft& o) {
                               return readState(v, p, o);
                           }) &&
               readEntries(root, "transitions", out.transitions,
                           [this](const Json& v, const std::string& p, TransitionDraft& o) {
                               return readTransition(v, p, o);
                           });
    }

private:
    bool readId(const Json& object, const std::string& pointer, std::string& id, Origin& origin)
    {
        const Json* value = member(object, pointer, "id");
        if (value == nullptr || !readName(*value, pointer + "/id", id)) {
            return false;
        }
        origin = originOf(*value, pointer + "/id");
        return true;
    }
};

// --- Merging ---------------------------------------------------------------------

class Merger {
public:
    explicit Merger(std::span<const std::string> fileNames) : fileNames_(fileNames) {}

    std::vector<Located> warnings;

    template <typename T>
    void field(Field<T>& into, Field<T>& from, std::string_view kind, const std::string& id,
               std::string_view name)
    {
        if (!from.value) {
            return;
        }
        if (into.value && !sameValue(*into.value, *from.value)) {
            conflict(into.origin, from.origin, kind, id, name);
        }
        into = std::move(from);
    }

    void conflict(const Origin& earlier, const Origin& later, std::string_view kind,
                  const std::string& id, std::string_view name)
    {
        std::string what = std::string(kind);
        if (!id.empty()) {
            what += " \"" + id + "\"";
        }
        warnings.push_back(Located{later, "conflict: " + what + " field \"" + std::string(name) +
                                              "\" set in " + fileName(earlier) + " (line " +
                                              std::to_string(earlier.line) +
                                              ") is overridden here"});
    }

    std::string fileName(const Origin& o) const
    {
        return o.file < fileNames_.size() ? fileNames_[o.file] : std::string("the graph");
    }

private:
    std::span<const std::string> fileNames_;
};

template <typename Draft, typename MergeFields>
void mergeEntries(std::vector<Draft>& into, std::vector<Draft>& from, MergeFields mergeFields)
{
    for (Draft& entry : from) {
        const auto found = std::find_if(into.begin(), into.end(),
                                        [&entry](const Draft& d) { return d.id == entry.id; });
        if (found == into.end()) {
            into.push_back(std::move(entry));
        } else {
            mergeFields(*found, entry);
        }
    }
}

void merge(GraphDraft& into, GraphDraft& from, Merger& m)
{
    m.field(into.initialState, from.initialState, "graph", "", "initialState");
    mergeEntries(into.parameters, from.parameters, [&m](ParameterDraft& a, ParameterDraft& b) {
        m.field(a.type, b.type, "parameter", a.id, "type");
        m.field(a.defaultValue, b.defaultValue, "parameter", a.id, "default");
    });
    mergeEntries(into.states, from.states, [&m](StateDraft& a, StateDraft& b) {
        // clip and blend are alternatives: giving one replaces the other.
        if (b.clip.value && a.blend.value) {
            m.conflict(a.blend.origin, b.clip.origin, "state", a.id, "blend");
            a.blend = {};
        }
        if (b.blend.value && a.clip.value) {
            m.conflict(a.clip.origin, b.blend.origin, "state", a.id, "clip");
            a.clip = {};
        }
        m.field(a.clip, b.clip, "state", a.id, "clip");
        m.field(a.blend, b.blend, "state", a.id, "blend");
        m.field(a.loop, b.loop, "state", a.id, "loop");
        m.field(a.speed, b.speed, "state", a.id, "speed");
    });
    mergeEntries(into.transitions, from.transitions, [&m](TransitionDraft& a, TransitionDraft& b) {
        m.field(a.from, b.from, "transition", a.id, "from");
        m.field(a.to, b.to, "transition", a.id, "to");
        m.field(a.priority, b.priority, "transition", a.id, "priority");
        m.field(a.conditions, b.conditions, "transition", a.id, "conditions");
        m.field(a.durationS, b.durationS, "transition", a.id, "durationS");
        m.field(a.mode, b.mode, "transition", a.id, "mode");
    });
}

// --- Validation after merging -----------------------------------------------------

Origin missingAt(const Origin& entity, std::string_view field)
{
    // The entity's id origin points at ".../id"; the missing field is a sibling.
    Origin o = entity;
    const std::size_t slash = o.pointer.rfind('/');
    o.pointer = (slash == std::string::npos ? std::string{} : o.pointer.substr(0, slash)) + "/" +
                std::string(field);
    return o;
}

class Validator {
public:
    explicit Validator(const GraphDraft& d) : d_(d)
    {
        for (std::size_t i = 0; i < d.parameters.size(); ++i) {
            parameterIndex_[d.parameters[i].id] = i;
        }
        for (std::size_t i = 0; i < d.states.size(); ++i) {
            stateIndex_[d.states[i].id] = i;
        }
    }

    std::optional<Located> errors() const
    {
        if (!d_.initialState.value) {
            Origin o = d_.root;
            o.pointer = "/initialState";
            return Located{o, "missing required field"};
        }
        if (d_.states.empty()) {
            Origin o = d_.root;
            o.pointer = "/states";
            return Located{o, "a graph needs at least one state"};
        }
        if (!stateIndex_.contains(get(d_.initialState))) {
            return Located{d_.initialState.origin, "no state \"" + get(d_.initialState) + "\""};
        }
        for (const ParameterDraft& p : d_.parameters) {
            if (!p.type.value) {
                return Located{missingAt(p.idOrigin, "type"), "missing required field"};
            }
            if (p.defaultValue.value) {
                const ValueKind kind = p.defaultValue.value->kind;
                const bool isBool = get(p.type) == ParameterType::Bool;
                if ((isBool && kind == ValueKind::Number) || (!isBool && kind == ValueKind::Bool) ||
                    (isBool && kind == ValueKind::Unknown && p.defaultValue.value->value != 0.0 &&
                     p.defaultValue.value->value != 1.0)) {
                    return Located{p.defaultValue.origin, "does not match the parameter's type"};
                }
            }
        }
        for (const StateDraft& s : d_.states) {
            if (auto problem = stateErrors(s)) {
                return problem;
            }
        }
        for (const TransitionDraft& t : d_.transitions) {
            if (auto problem = transitionErrors(t)) {
                return problem;
            }
        }
        return std::nullopt;
    }

    // Only for a graph without errors.
    std::vector<Located> warnings() const
    {
        std::vector<Located> out;
        // Reachability from initialState; "*" transitions reach their target
        // from any other state.
        std::vector<bool> reached(d_.states.size(), false);
        std::vector<std::size_t> queue{stateIndex_.at(get(d_.initialState))};
        reached[queue.front()] = true;
        while (!queue.empty()) {
            const std::size_t s = queue.back();
            queue.pop_back();
            for (const TransitionDraft& t : d_.transitions) {
                const std::size_t to = stateIndex_.at(get(t.to));
                const bool fromHere =
                    get(t.from) == kAnyState ? to != s : stateIndex_.at(get(t.from)) == s;
                if (fromHere && !reached[to]) {
                    reached[to] = true;
                    queue.push_back(to);
                }
            }
        }
        for (std::size_t i = 0; i < d_.states.size(); ++i) {
            if (!reached[i]) {
                out.push_back(Located{d_.states[i].idOrigin,
                                      "state \"" + d_.states[i].id +
                                          "\" cannot be reached from the initial state"});
            }
        }
        for (const TransitionDraft& t : d_.transitions) {
            if (auto reason = neverMet(t)) {
                out.push_back(
                    Located{t.conditions.value ? t.conditions.origin : t.idOrigin,
                            "transition \"" + t.id + "\" can never be taken: " + *reason});
            }
        }
        return out;
    }

    AnimGraph graph() const
    {
        AnimGraph g;
        g.id = d_.id.value.value_or("");
        g.initialState = get(d_.initialState);
        for (const ParameterDraft& p : d_.parameters) {
            g.parameters.push_back(GraphParameter{
                p.id, get(p.type), p.defaultValue.value ? p.defaultValue.value->value : 0.0});
        }
        for (const StateDraft& s : d_.states) {
            GraphState state;
            state.id = s.id;
            state.clip = s.clip.value.value_or("");
            if (s.blend.value) {
                state.blend = s.blend.value->blend;
            }
            state.loop = s.loop.value.value_or(true);
            state.speed = s.speed.value.value_or(1.0);
            g.states.push_back(std::move(state));
        }
        for (const TransitionDraft& t : d_.transitions) {
            GraphTransition transition;
            transition.id = t.id;
            transition.from = get(t.from);
            transition.to = get(t.to);
            transition.priority = t.priority.value.value_or(0);
            if (t.conditions.value) {
                for (const ConditionDraft& c : get(t.conditions)) {
                    transition.conditions.push_back(c.condition);
                }
            }
            transition.durationS = get(t.durationS);
            transition.mode = t.mode.value.value_or(TransitionMode::Inertialize);
            g.transitions.push_back(std::move(transition));
        }
        return g;
    }

private:
    std::optional<Located> stateErrors(const StateDraft& s) const
    {
        if (!s.clip.value && !s.blend.value) {
            return Located{missingAt(s.idOrigin, "clip"), "a state needs \"clip\" or \"blend\""};
        }
        if (s.clip.value && s.blend.value) {
            return Located{s.blend.origin, "a state has either \"clip\" or \"blend\", not both"};
        }
        if (s.blend.value) {
            const BlendDraft& b = get(s.blend);
            const auto p = parameterIndex_.find(b.blend.parameter);
            if (p == parameterIndex_.end()) {
                return Located{b.parameterOrigin, "no parameter \"" + b.blend.parameter + "\""};
            }
            if (get(d_.parameters[p->second].type) != ParameterType::Number) {
                return Located{b.parameterOrigin, "a blend parameter must be a number"};
            }
        }
        return std::nullopt;
    }

    std::optional<Located> transitionErrors(const TransitionDraft& t) const
    {
        if (!t.from.value) {
            return Located{missingAt(t.idOrigin, "from"), "missing required field"};
        }
        if (!t.to.value) {
            return Located{missingAt(t.idOrigin, "to"), "missing required field"};
        }
        if (!t.durationS.value) {
            return Located{missingAt(t.idOrigin, "durationS"), "missing required field"};
        }
        if (get(t.from) != kAnyState && !stateIndex_.contains(get(t.from))) {
            return Located{t.from.origin, "no state \"" + get(t.from) + "\""};
        }
        if (!stateIndex_.contains(get(t.to))) {
            return Located{t.to.origin, "no state \"" + get(t.to) + "\""};
        }
        if (!t.conditions.value) {
            return std::nullopt;
        }
        for (const ConditionDraft& c : get(t.conditions)) {
            if (c.condition.kind != ConditionKind::Parameter) {
                continue;
            }
            const auto p = parameterIndex_.find(c.condition.parameter);
            if (p == parameterIndex_.end()) {
                return Located{c.paramOrigin, "no parameter \"" + c.condition.parameter + "\""};
            }
            const bool isBool = get(d_.parameters[p->second].type) == ParameterType::Bool;
            if (isBool && c.condition.op != CompareOp::Equal &&
                c.condition.op != CompareOp::NotEqual) {
                return Located{c.opOrigin, "a bool parameter is compared with == or != only"};
            }
            const bool wrongKind = (isBool && c.valueKind == ValueKind::Number) ||
                                   (!isBool && c.valueKind == ValueKind::Bool) ||
                                   (isBool && c.valueKind == ValueKind::Unknown &&
                                    c.condition.value != 0.0 && c.condition.value != 1.0);
            if (wrongKind) {
                return Located{c.valueOrigin, "does not match the parameter's type"};
            }
        }
        return std::nullopt;
    }

    // Why a transition's conditions can never all hold, if they cannot.
    std::optional<std::string> neverMet(const TransitionDraft& t) const
    {
        if (!t.conditions.value) {
            return std::nullopt;
        }
        // Per number parameter, the interval allowed so far.
        struct Range {
            double low = -std::numeric_limits<double>::infinity();
            double high = std::numeric_limits<double>::infinity();
            bool lowOpen = false;
            bool highOpen = false;
        };
        std::map<std::string, Range> ranges;
        std::map<std::string, std::set<double>> boolsAllowed;
        std::map<std::string, std::set<double>> excluded; // by != on number parameters
        bool clipEnd = false;
        for (const ConditionDraft& cd : get(t.conditions)) {
            const GraphCondition& c = cd.condition;
            if (c.kind == ConditionKind::ClipEnd) {
                clipEnd = true;
                continue;
            }
            if (c.kind != ConditionKind::Parameter) {
                continue;
            }
            const bool isBool =
                get(d_.parameters[parameterIndex_.at(c.parameter)].type) == ParameterType::Bool;
            if (isBool) {
                auto [it, inserted] =
                    boolsAllowed.try_emplace(c.parameter, std::set<double>{0.0, 1.0});
                if (c.op == CompareOp::Equal) {
                    const bool had = it->second.contains(c.value);
                    it->second.clear();
                    if (had) {
                        it->second.insert(c.value);
                    }
                } else {
                    it->second.erase(c.value);
                }
                if (it->second.empty()) {
                    return "\"" + c.parameter + "\" must be both true and false";
                }
                continue;
            }
            Range& r = ranges[c.parameter];
            const auto raiseLow = [&r](double v, bool open) {
                if (v > r.low || (v == r.low && open)) {
                    r.low = v;
                    r.lowOpen = open;
                }
            };
            const auto lowerHigh = [&r](double v, bool open) {
                if (v < r.high || (v == r.high && open)) {
                    r.high = v;
                    r.highOpen = open;
                }
            };
            switch (c.op) {
            case CompareOp::Less:
                lowerHigh(c.value, true);
                break;
            case CompareOp::LessEqual:
                lowerHigh(c.value, false);
                break;
            case CompareOp::Greater:
                raiseLow(c.value, true);
                break;
            case CompareOp::GreaterEqual:
                raiseLow(c.value, false);
                break;
            case CompareOp::Equal:
                raiseLow(c.value, false);
                lowerHigh(c.value, false);
                break;
            case CompareOp::NotEqual:
                excluded[c.parameter].insert(c.value); // empties only a single-value range
                break;
            }
            if (r.low > r.high || (r.low == r.high && (r.lowOpen || r.highOpen))) {
                return "no value of \"" + c.parameter + "\" satisfies all its comparisons";
            }
        }
        for (const auto& [name, r] : ranges) {
            const auto ex = excluded.find(name);
            if (r.low == r.high && ex != excluded.end() && ex->second.contains(r.low)) {
                return "no value of \"" + name + "\" satisfies all its comparisons";
            }
        }
        if (clipEnd) {
            const auto ends = [](const StateDraft& s) { return !s.loop.value.value_or(true); };
            if (get(t.from) != kAnyState) {
                if (!ends(d_.states[stateIndex_.at(get(t.from))])) {
                    return std::string("clipEnd on a looping state");
                }
            } else {
                const bool any =
                    std::any_of(d_.states.begin(), d_.states.end(),
                                [&](const StateDraft& s) { return s.id != get(t.to) && ends(s); });
                if (!any) {
                    return std::string("clipEnd, but every state it can leave loops");
                }
            }
        }
        return std::nullopt;
    }

    const GraphDraft& d_;
    std::map<std::string, std::size_t> parameterIndex_;
    std::map<std::string, std::size_t> stateIndex_;
};

// A draft of a graph built in code, with canonical pointers, for
// validateAnimGraph. Ids are checked here, since no reader saw them.
std::optional<Located> draftFromGraph(const AnimGraph& g, GraphDraft& d)
{
    const auto at = [](std::string pointer) { return Origin{kNoFile, 0, std::move(pointer)}; };
    if (!isValidName(g.id)) {
        return Located{at("/id"), "must be non-empty, without control characters"};
    }
    d.id = {g.id, at("/id")};
    d.root = at("");
    if (!g.initialState.empty()) {
        d.initialState = {g.initialState, at("/initialState")};
    }
    std::set<std::string> ids;
    for (std::size_t i = 0; i < g.parameters.size(); ++i) {
        const std::string p = "/parameters/" + std::to_string(i);
        const GraphParameter& gp = g.parameters[i];
        if (!isValidName(gp.id)) {
            return Located{at(p + "/id"), "must be non-empty, without control characters"};
        }
        if (!ids.insert(gp.id).second) {
            return Located{at(p + "/id"), "duplicate parameter \"" + gp.id + "\""};
        }
        if (!std::isfinite(gp.defaultValue)) {
            return Located{at(p + "/default"), "must be finite"};
        }
        d.parameters.push_back(
            ParameterDraft{gp.id,
                           at(p + "/id"),
                           {gp.type, at(p + "/type")},
                           {TypedValue{gp.defaultValue, ValueKind::Unknown}, at(p + "/default")}});
    }
    ids.clear();
    for (std::size_t i = 0; i < g.states.size(); ++i) {
        const std::string p = "/states/" + std::to_string(i);
        const GraphState& gs = g.states[i];
        if (!isValidName(gs.id) || gs.id == kAnyState) {
            return Located{at(p + "/id"),
                           "must be non-empty, without control characters, not \"*\""};
        }
        if (!ids.insert(gs.id).second) {
            return Located{at(p + "/id"), "duplicate state \"" + gs.id + "\""};
        }
        if (!(std::isfinite(gs.speed) && gs.speed >= 0.0)) {
            return Located{at(p + "/speed"), "must be finite and at least 0"};
        }
        StateDraft s{
            gs.id, at(p + "/id"), {}, {}, {gs.loop, at(p + "/loop")}, {gs.speed, at(p + "/speed")}};
        if (!gs.clip.empty()) {
            if (!isValidName(gs.clip)) {
                return Located{at(p + "/clip"), "must be without control characters"};
            }
            s.clip = {gs.clip, at(p + "/clip")};
        }
        if (gs.blend) {
            const std::string b = p + "/blend";
            if (gs.blend->samples.size() < 2) {
                return Located{at(b + "/samples"), "a blend needs at least 2 samples"};
            }
            for (std::size_t k = 0; k < gs.blend->samples.size(); ++k) {
                const BlendSample& sample = gs.blend->samples[k];
                const std::string sp = b + "/samples/" + std::to_string(k);
                if (!isValidName(sample.clip)) {
                    return Located{at(sp + "/clip"),
                                   "must be non-empty, without control characters"};
                }
                if (!std::isfinite(sample.at) ||
                    (k > 0 && !(sample.at > gs.blend->samples[k - 1].at))) {
                    return Located{at(sp + "/at"),
                                   "must be finite and greater than the previous sample's"};
                }
            }
            s.blend = {BlendDraft{*gs.blend, at(b + "/parameter")}, at(b)};
        }
        d.states.push_back(std::move(s));
    }
    ids.clear();
    for (std::size_t i = 0; i < g.transitions.size(); ++i) {
        const std::string p = "/transitions/" + std::to_string(i);
        const GraphTransition& gt = g.transitions[i];
        if (!isValidName(gt.id)) {
            return Located{at(p + "/id"), "must be non-empty, without control characters"};
        }
        if (!ids.insert(gt.id).second) {
            return Located{at(p + "/id"), "duplicate transition \"" + gt.id + "\""};
        }
        if (!(std::isfinite(gt.durationS) && gt.durationS >= 0.0)) {
            return Located{at(p + "/durationS"), "must be finite and at least 0"};
        }
        TransitionDraft t{gt.id,
                          at(p + "/id"),
                          {},
                          {},
                          {gt.priority, at(p + "/priority")},
                          {},
                          {gt.durationS, at(p + "/durationS")},
                          {gt.mode, at(p + "/mode")}};
        if (!gt.from.empty()) {
            t.from = {gt.from, at(p + "/from")};
        }
        if (!gt.to.empty()) {
            t.to = {gt.to, at(p + "/to")};
        }
        std::vector<ConditionDraft> conditions;
        for (std::size_t k = 0; k < gt.conditions.size(); ++k) {
            const std::string cp = p + "/conditions/" + std::to_string(k);
            const GraphCondition& c = gt.conditions[k];
            if (c.kind == ConditionKind::Parameter && !std::isfinite(c.value)) {
                return Located{at(cp + "/value"), "must be finite"};
            }
            if (c.kind == ConditionKind::Event && !isValidName(c.event)) {
                return Located{at(cp + "/event"), "must be non-empty, without control characters"};
            }
            conditions.push_back(ConditionDraft{c, ValueKind::Unknown, at(cp), at(cp + "/value"),
                                                at(cp + "/op"), at(cp + "/parameter")});
        }
        t.conditions = {std::move(conditions), at(p + "/conditions")};
        d.transitions.push_back(std::move(t));
    }
    return std::nullopt;
}

format::LoadError toLoadError(const Located& l, std::span<const std::string> fileNames)
{
    return {l.origin.file < fileNames.size() ? fileNames[l.origin.file] : std::string{},
            l.origin.line, l.origin.pointer, l.message};
}

// --- Writing ----------------------------------------------------------------------

std::string writeCondition(const GraphCondition& c, const std::vector<GraphParameter>& parameters)
{
    switch (c.kind) {
    case ConditionKind::Event:
        return "{\"event\": " + jsonString(c.event) + "}";
    case ConditionKind::ClipEnd:
        return "{\"clipEnd\": true}";
    case ConditionKind::Parameter:
        break;
    }
    const auto p = std::find_if(parameters.begin(), parameters.end(),
                                [&c](const GraphParameter& g) { return g.id == c.parameter; });
    const bool isBool = p != parameters.end() && p->type == ParameterType::Bool;
    const std::string value = isBool ? (c.value != 0.0 ? "true" : "false") : number(c.value);
    return "{\"parameter\": " + jsonString(c.parameter) + ", \"op\": " + jsonString(opText(c.op)) +
           ", \"value\": " + value + "}";
}

} // namespace

namespace {

AnimGraphLoadResult loadUnguarded(std::span<const AnimGraphFile> files)
{
    AnimGraphLoadResult result;
    if (files.empty()) {
        result.error = format::LoadError{"", 0, "", "no graph files"};
        return result;
    }
    std::vector<std::string> fileNames;
    for (const AnimGraphFile& f : files) {
        fileNames.emplace_back(f.fileName);
    }
    Merger merger(fileNames);
    GraphDraft merged;
    for (std::size_t i = 0; i < files.size(); ++i) {
        GraphDraft draft;
        const std::optional<format::LoadError> error =
            format::detail::parseAndRead<GraphFileReader>(
                files[i].text, files[i].fileName,
                [&draft, i](GraphFileReader& r, const Json& root) {
                    r.fileIndex = i;
                    if (!r.readDocument(root, draft)) {
                        return false;
                    }
                    // Ids are unique within one file.
                    const auto unique = [&r, &root](const auto& entries, std::string_view kind) {
                        std::set<std::string> seen;
                        for (const auto& e : entries) {
                            if (!seen.insert(e.id).second) {
                                return r.failAt(root, e.idOrigin.pointer,
                                                "duplicate " + std::string(kind) + " \"" + e.id +
                                                    "\"");
                            }
                        }
                        return true;
                    };
                    return unique(draft.parameters, "parameter") && unique(draft.states, "state") &&
                           unique(draft.transitions, "transition");
                });
        if (error) {
            result.error = *error;
            return result;
        }
        if (i == 0) {
            merged = std::move(draft);
            continue;
        }
        if (draft.id.value != merged.id.value) {
            result.error =
                toLoadError(Located{draft.id.origin, "every merged file must have the graph id \"" +
                                                         get(merged.id) + "\""},
                            fileNames);
            return result;
        }
        merge(merged, draft, merger);
    }
    const Validator validator(merged);
    if (const std::optional<Located> problem = validator.errors()) {
        result.error = toLoadError(*problem, fileNames);
        return result;
    }
    for (const Located& w : merger.warnings) {
        result.warnings.push_back(toLoadError(w, fileNames));
    }
    for (const Located& w : validator.warnings()) {
        result.warnings.push_back(toLoadError(w, fileNames));
    }
    result.value = validator.graph();
    return result;
}

} // namespace

AnimGraphLoadResult loadAnimGraph(std::span<const AnimGraphFile> files)
{
    // Parsing catches its own exceptions; merging and validation run after
    // it, so an allocation failure there is caught here (loaders never throw).
    try {
        return loadUnguarded(files);
    } catch (const std::exception& e) {
        AnimGraphLoadResult result;
        result.error =
            format::LoadError{files.empty() ? std::string{} : std::string(files[0].fileName), 0, "",
                              std::string("internal error: ") + e.what()};
        return result;
    }
}

AnimGraphLoadResult loadAnimGraphFiles(std::span<const std::filesystem::path> paths)
{
    try {
        std::vector<std::string> texts;
        std::vector<std::string> names;
        for (const std::filesystem::path& path : paths) {
            format::LoadError error;
            const std::optional<std::string> text = format::detail::readTextFile(path, error);
            if (!text) {
                AnimGraphLoadResult result;
                result.error = std::move(error);
                return result;
            }
            texts.push_back(*text);
            names.push_back(format::detail::utf8FileName(path));
        }
        std::vector<AnimGraphFile> files;
        files.reserve(texts.size());
        for (std::size_t i = 0; i < texts.size(); ++i) {
            files.push_back(AnimGraphFile{texts[i], names[i]});
        }
        return loadAnimGraph(files);
    } catch (const std::exception& e) {
        AnimGraphLoadResult result;
        result.error = format::LoadError{"", 0, "", std::string("internal error: ") + e.what()};
        return result;
    }
}

std::string writeAnimGraph(const AnimGraph& graph)
{
    std::string out = "{\n  \"format\": \"opas.animgraph\",\n  \"version\": 1,\n";
    out += "  \"id\": " + jsonString(graph.id) + ",\n";
    out += "  \"initialState\": " + jsonString(graph.initialState) + ",\n";
    out +=
        "  \"parameters\": " +
        format::detail::arrayBlock(graph.parameters,
                                   [](const GraphParameter& p) {
                                       const bool isBool = p.type == ParameterType::Bool;
                                       return "    {\"id\": " + jsonString(p.id) + ", \"type\": " +
                                              (isBool ? "\"bool\"" : "\"number\"") +
                                              ", \"default\": " +
                                              (isBool ? (p.defaultValue != 0.0 ? "true" : "false")
                                                      : number(p.defaultValue)) +
                                              "}";
                                   }) +
        ",\n";
    out += "  \"states\": " +
           format::detail::arrayBlock(
               graph.states,
               [](const GraphState& s) {
                   std::string line = "    {\"id\": " + jsonString(s.id) + ", ";
                   if (s.blend) {
                       line += "\"blend\": {\"parameter\": " + jsonString(s.blend->parameter) +
                               ", \"samples\": [";
                       for (std::size_t k = 0; k < s.blend->samples.size(); ++k) {
                           const BlendSample& b = s.blend->samples[k];
                           line += (k > 0 ? ", " : "") + std::string("{\"clip\": ") +
                                   jsonString(b.clip) + ", \"at\": " + number(b.at) + "}";
                       }
                       line += "]}";
                   } else {
                       line += "\"clip\": " + jsonString(s.clip);
                   }
                   return line + ", \"loop\": " + (s.loop ? "true" : "false") +
                          ", \"speed\": " + number(s.speed) + "}";
               }) +
           ",\n";
    out +=
        "  \"transitions\": " +
        format::detail::arrayBlock(
            graph.transitions,
            [&graph](const GraphTransition& t) {
                std::string line =
                    "    {\"id\": " + jsonString(t.id) + ", \"from\": " + jsonString(t.from) +
                    ", \"to\": " + jsonString(t.to) +
                    ", \"priority\": " + std::to_string(t.priority) + ", \"conditions\": [";
                for (std::size_t k = 0; k < t.conditions.size(); ++k) {
                    line += (k > 0 ? ", " : "") + writeCondition(t.conditions[k], graph.parameters);
                }
                return line + "], \"durationS\": " + number(t.durationS) + ", \"mode\": " +
                       (t.mode == TransitionMode::Crossfade ? "\"crossfade\"" : "\"inertialize\"") +
                       "}";
            }) +
        "\n}\n";
    return out;
}

std::string validateAnimGraph(const AnimGraph& graph)
{
    GraphDraft draft;
    if (const std::optional<Located> problem = draftFromGraph(graph, draft)) {
        return problem->origin.pointer + ": " + problem->message;
    }
    const Validator validator(draft);
    if (const std::optional<Located> problem = validator.errors()) {
        return problem->origin.pointer + ": " + problem->message;
    }
    return {};
}

} // namespace opas::anim
