#include "game_skyrimse/quest_script.hpp"

#include "game_skyrimse/pack_core.hpp"
#include "game_skyrimse/survey.hpp"

#include <components/script_papyrus/pexfile.hpp>

#include <algorithm>
#include <cstring>
#include <set>

// VMAD and .pex layouts: see the header (task 0156).

namespace game_skyrimse
{
    namespace
    {
        Content::Error makeError(const std::string& file, std::uint64_t offset, std::uint32_t type, std::string message)
        {
            Content::Error error;
            error.mFile = file;
            error.mOffset = offset;
            error.mRecordType = type;
            error.mMessage = std::move(message);
            return error;
        }

        std::string lower(std::string_view s)
        {
            std::string out(s);
            std::transform(out.begin(), out.end(), out.begin(),
                [](unsigned char c) { return static_cast<char>(c >= 'A' && c <= 'Z' ? c + 32 : c); });
            return out;
        }

        class Cursor
        {
        public:
            explicit Cursor(std::span<const std::byte> d)
                : mData(d)
            {
            }
            bool take(std::size_t n, const std::byte*& p)
            {
                if (mData.size() - mAt < n)
                    return false;
                p = mData.data() + mAt;
                mAt += n;
                return true;
            }
            bool u8(std::uint8_t& v)
            {
                const std::byte* p;
                if (!take(1, p))
                    return false;
                v = static_cast<std::uint8_t>(*p);
                return true;
            }
            bool u16(std::uint16_t& v)
            {
                const std::byte* p;
                if (!take(2, p))
                    return false;
                std::memcpy(&v, p, 2);
                return true;
            }
            bool u32(std::uint32_t& v)
            {
                const std::byte* p;
                if (!take(4, p))
                    return false;
                std::memcpy(&v, p, 4);
                return true;
            }
            bool f32(float& v)
            {
                std::uint32_t bits;
                if (!u32(bits))
                    return false;
                std::memcpy(&v, &bits, 4);
                return true;
            }
            bool wstring(std::string& out)
            {
                std::uint16_t n;
                const std::byte* p;
                if (!u16(n) || !take(n, p))
                    return false;
                out.assign(reinterpret_cast<const char*>(p), n);
                return true;
            }
            bool skip(std::size_t n)
            {
                const std::byte* p;
                return take(n, p);
            }

        private:
            std::span<const std::byte> mData;
            std::size_t mAt = 0;
        };
    }

    bool parseVmadScripts(
        std::span<const std::byte> vmad, const QuestScriptLimits& limits, std::vector<QuestScriptRef>& out)
    {
        out.clear();
        Cursor c(vmad);
        std::uint16_t version = 0, format = 0, count = 0;
        if (!c.u16(version) || !c.u16(format) || !c.u16(count))
            return false;
        (void)version;
        (void)format; // objects are 8 bytes in both formats
        if (count > limits.mMaxScripts)
            return false;
        for (std::uint16_t s = 0; s < count; ++s)
        {
            QuestScriptRef script;
            std::uint8_t status = 0;
            std::uint16_t props = 0;
            out.push_back({});
            if (!c.wstring(script.mName) || !c.u8(status) || !c.u16(props) || props > limits.mMaxProperties)
                return false;
            for (std::uint16_t p = 0; p < props; ++p)
            {
                QuestScriptProperty prop;
                std::uint8_t type = 0, pstatus = 0;
                if (!c.wstring(prop.mName) || !c.u8(type) || !c.u8(pstatus))
                    return false;
                prop.mType = type;
                switch (type)
                {
                    case 0:
                        break;
                    case 1:
                        if (!c.skip(8))
                            return false;
                        break;
                    case 2:
                    {
                        std::string text;
                        if (!c.wstring(text))
                            return false;
                        break;
                    }
                    case 3:
                    {
                        std::uint32_t v;
                        if (!c.u32(v))
                            return false;
                        prop.mScalar = true;
                        prop.mValue = static_cast<float>(static_cast<std::int32_t>(v));
                        break;
                    }
                    case 4:
                        if (!c.f32(prop.mValue))
                            return false;
                        prop.mScalar = true;
                        break;
                    case 5:
                    {
                        std::uint8_t v;
                        if (!c.u8(v))
                            return false;
                        prop.mScalar = true;
                        prop.mValue = v != 0 ? 1.f : 0.f;
                        break;
                    }
                    case 11:
                    case 12:
                    case 13:
                    case 14:
                    case 15:
                    {
                        std::uint32_t n;
                        if (!c.u32(n) || n > limits.mMaxArray)
                            return false;
                        for (std::uint32_t i = 0; i < n; ++i)
                        {
                            bool ok = true;
                            switch (type)
                            {
                                case 11:
                                    ok = c.skip(8);
                                    break;
                                case 12:
                                {
                                    std::string t;
                                    ok = c.wstring(t);
                                    break;
                                }
                                case 13:
                                case 14:
                                    ok = c.skip(4);
                                    break;
                                default:
                                    ok = c.skip(1);
                                    break;
                            }
                            if (!ok)
                                return false;
                        }
                        break;
                    }
                    default:
                        return false; // an unknown type: the rest cannot be aligned
                }
                script.mProperties.push_back(std::move(prop));
            }
            out.back() = std::move(script);
        }
        return true;
    }

    namespace
    {
        class QuestScriptParser final : public Content::Visitor
        {
        public:
            QuestScriptParser(const Content::Plugin& plugin, const FormIdResolver& resolver,
                const QuestScriptLimits& limits, QuestScriptRecords& out)
                : mPlugin(plugin)
                , mResolver(resolver)
                , mLimits(limits)
                , mOut(out)
            {
            }

            bool onRecord(const Content::RawRecord& record) override
            {
                if (record.mType != Content::fourCC("QUST"))
                    return true;
                ++mOut.mQuestsSeen;
                std::span<const std::byte> bytes = record.mData;
                if (record.isCompressed())
                {
                    if (record.mUncompressedSize > mLimits.mMaxRecordBytes)
                    {
                        fail(record, "the record inflates past the limit");
                        return true;
                    }
                    Content::Error error;
                    if (!inflateRecord(record, mPlugin.name(), mInflated, error))
                    {
                        addError(std::move(error));
                        return true;
                    }
                    bytes = mInflated;
                }
                else if (record.mDataSize > mLimits.mMaxRecordBytes)
                {
                    fail(record, "the record is over the size limit");
                    return true;
                }
                QuestScriptEntry entry;
                const std::optional<FormId> id = mResolver.resolve(record.mFormId);
                if (!id)
                {
                    ++mOut.mUnresolvedFormIds;
                    return true;
                }
                entry.mQuest = *id;
                entry.mDeleted = (record.mFlags & RecordFlags::Deleted) != 0;
                bool framed = survey::forEachSubrecord(bytes, [&](const survey::SubrecordView& sub) {
                    if (sub.mType != Content::fourCC("VMAD") || !entry.mScripts.empty())
                        return;
                    if (!parseVmadScripts(sub.mData, mLimits, entry.mScripts))
                        entry.mOk = false;
                });
                if (!framed)
                    entry.mOk = false;
                if (!entry.mOk)
                    ++mOut.mDamaged;
                if (!entry.mScripts.empty())
                    ++mOut.mWithScripts;
                mOut.mQuests.push_back(std::move(entry));
                return true;
            }

        private:
            void addError(Content::Error error)
            {
                if (mOut.mErrors.size() < mLimits.mMaxErrors)
                    mOut.mErrors.push_back(std::move(error));
                else
                    ++mOut.mErrorsDropped;
            }
            void fail(const Content::RawRecord& record, std::string message)
            {
                addError(makeError(mPlugin.name(), record.mOffset, record.mType, std::move(message)));
            }

            const Content::Plugin& mPlugin;
            const FormIdResolver& mResolver;
            const QuestScriptLimits& mLimits;
            QuestScriptRecords& mOut;
            std::vector<std::byte> mInflated;
        };
    }

    bool parseQuestScripts(
        const Content::Plugin& plugin, const LoadOrder& order, const QuestScriptLimits& limits, QuestScriptRecords& out)
    {
        out = QuestScriptRecords();
        out.mName = plugin.name();
        if (!isSkyrimSeHeader(plugin.header()))
        {
            out.mWalkError = makeError(plugin.name(), 0, 0, "this plugin is not a Skyrim Special Edition one");
            return false;
        }
        const FormIdResolver resolver(plugin.header(), plugin.name(), order);
        QuestScriptParser parser(plugin, resolver, limits, out);
        const std::optional<Content::Error> error = plugin.walk(parser);
        if (error)
        {
            out.mWalkError = *error;
            return false;
        }
        return true;
    }

    void QuestScriptIndex::add(QuestScriptRecords records)
    {
        for (QuestScriptEntry& e : records.mQuests)
        {
            if (e.mQuest == kNullFormId || e.mQuest == kUnresolvedFormId)
                continue;
            if (e.mDeleted)
                mQuests.erase(e.mQuest);
            else
                mQuests[e.mQuest] = std::move(e);
        }
    }

    const QuestScriptEntry* QuestScriptIndex::find(FormId quest) const
    {
        const auto it = mQuests.find(quest);
        return it == mQuests.end() ? nullptr : &it->second;
    }

    // ---- QuestVariables ----------------------------------------------------------------------------------------

    struct QuestVariables::Impl
    {
        const QuestScriptIndex& mScripts;
        PexLoader mLoader;
        std::map<std::string, std::shared_ptr<const ScriptPapyrus::Pex::File>> mCache; ///< null: missing or refused
        std::size_t mLoaded = 0;
        std::size_t mMissing = 0;

        Impl(const QuestScriptIndex& s, PexLoader l)
            : mScripts(s)
            , mLoader(std::move(l))
        {
        }

        std::shared_ptr<const ScriptPapyrus::Pex::File> file(const std::string& name)
        {
            const std::string key = lower(name);
            const auto it = mCache.find(key);
            if (it != mCache.end())
                return it->second;
            std::shared_ptr<const ScriptPapyrus::Pex::File> result;
            std::vector<std::uint8_t> bytes;
            if (mLoader && mLoader(key, bytes))
            {
                ScriptPapyrus::Pex::ParseResult parsed = ScriptPapyrus::Pex::parse(bytes, key);
                if (parsed.mOk)
                {
                    result = std::make_shared<const ScriptPapyrus::Pex::File>(std::move(parsed.mFile));
                    ++mLoaded;
                }
            }
            if (!result)
                ++mMissing;
            mCache.emplace(key, result);
            return result;
        }

        // the objects of a script, the script itself first, then its parents (at most 8 deep)
        std::vector<std::pair<std::shared_ptr<const ScriptPapyrus::Pex::File>, const ScriptPapyrus::Pex::Object*>>
        chain(const std::string& script)
        {
            std::vector<std::pair<std::shared_ptr<const ScriptPapyrus::Pex::File>, const ScriptPapyrus::Pex::Object*>>
                out;
            std::string name = script;
            for (int depth = 0; depth < 8 && !name.empty(); ++depth)
            {
                const auto f = file(name);
                if (!f)
                    break;
                const ScriptPapyrus::Pex::Object* object = nullptr;
                for (const auto& o : f->mObjects)
                    if (lower(f->str(o.mName)) == lower(name))
                        object = &o;
                if (object == nullptr && !f->mObjects.empty())
                    object = &f->mObjects.front();
                if (object == nullptr)
                    break;
                out.emplace_back(f, object);
                name = std::string(f->str(object->mParent));
            }
            return out;
        }
    };

    namespace
    {
        using namespace ScriptPapyrus::Pex;

        bool isName(const File& f, const Value& v, const std::set<std::string>& names)
        {
            return (v.mType == ValueType::Identifier || v.mType == ValueType::String)
                && names.count(lower(f.str(v.mString))) != 0;
        }

        // Does the function write one of the names (directly), or call a function of the same script chain on self
        // that does? `seen` stops loops.
        bool writes(const File& f, const std::vector<const Function*>& all, const Function& fn,
            const std::set<std::string>& names, std::set<const Function*>& seen)
        {
            if (!seen.insert(&fn).second)
                return false;
            for (const Instruction& in : fn.mInstructions)
            {
                const Value* ops = fn.mOperands.data() + in.mFirstOperand;
                const std::size_t n = std::size_t(in.mFixedCount) + in.mExtraCount;
                if (in.mFirstOperand + n > fn.mOperands.size() || in.mFixedCount == 0)
                    continue;
                switch (in.mOpcode)
                {
                    case Op::Jmp:
                    case Op::JmpT:
                    case Op::JmpF:
                    case Op::Return:
                    case Op::Nop:
                        break;
                    case Op::PropSet:
                        if (isName(f, ops[0], names))
                            return true;
                        break;
                    case Op::PropGet:
                    case Op::CallStatic:
                        if (in.mFixedCount > 2 && isName(f, ops[2], names))
                            return true;
                        if (in.mOpcode == Op::CallStatic)
                            break;
                        break;
                    case Op::CallParent:
                        if (in.mFixedCount > 1 && isName(f, ops[1], names))
                            return true;
                        break;
                    case Op::CallMethod:
                    {
                        if (in.mFixedCount > 2 && isName(f, ops[2], names))
                            return true;
                        if (in.mFixedCount > 1 && lower(f.str(ops[1].mString)) == "self")
                        {
                            const std::string callee = lower(f.str(ops[0].mString));
                            for (const Function* g : all)
                                if (lower(f.str(g->mName)) == callee && writes(f, all, *g, names, seen))
                                    return true;
                        }
                        break;
                    }
                    default:
                        if (isName(f, ops[0], names))
                            return true;
                        break;
                }
            }
            return false;
        }
    }

    QuestVariables::QuestVariables(const QuestScriptIndex& scripts, PexLoader loader)
        : mImpl(std::make_unique<Impl>(scripts, std::move(loader)))
    {
    }

    QuestVariables::~QuestVariables() = default;

    std::size_t QuestVariables::scriptsLoaded() const
    {
        return mImpl->mLoaded;
    }

    std::size_t QuestVariables::scriptsMissing() const
    {
        return mImpl->mMissing;
    }

    QuestVariableValue QuestVariables::value(FormId quest, std::string_view variable) const
    {
        QuestVariableValue none;
        const QuestScriptEntry* entry = mImpl->mScripts.find(quest);
        if (entry == nullptr || entry->mScripts.empty() || variable.empty())
            return none;
        const std::string wanted = lower(variable);
        for (const QuestScriptRef& script : entry->mScripts)
        {
            const auto objects = mImpl->chain(script.mName);
            for (const auto& [file, object] : objects)
            {
                const File& f = *file;
                // by variable name, or by property name (an auto property is its backing variable)
                std::string propertyName;
                std::string variableName;
                const Variable* var = nullptr;
                for (const Variable& v : object->mVariables)
                    if (lower(f.str(v.mName)) == wanted)
                    {
                        var = &v;
                        variableName = wanted;
                    }
                const Property* prop = nullptr;
                for (const Property& p : object->mProperties)
                {
                    if (lower(f.str(p.mName)) == wanted
                        || ((p.mFlags & Property::kAuto) != 0 && lower(f.str(p.mAutoVariable)) == wanted))
                    {
                        prop = &p;
                        propertyName = lower(f.str(p.mName));
                        if ((p.mFlags & Property::kAuto) != 0)
                            variableName = lower(f.str(p.mAutoVariable));
                        break;
                    }
                }
                if (prop != nullptr && (prop->mFlags & Property::kAuto) != 0)
                {
                    var = nullptr;
                    for (const Variable& v : object->mVariables)
                        if (lower(f.str(v.mName)) == variableName)
                            var = &v;
                }
                if (var == nullptr && prop == nullptr)
                    continue;
                if (prop != nullptr && (prop->mFlags & Property::kAuto) == 0)
                    return none; // a property with functions: its value is code, not data

                // did OnInit (or what it calls on self) write it?
                std::set<std::string> names;
                if (!variableName.empty())
                    names.insert(variableName);
                if (!propertyName.empty())
                    names.insert(propertyName);
                std::vector<const Function*> all;
                for (const auto& [ff, oo] : objects)
                    if (ff.get() == &f)
                        for (const State& s : oo->mStates)
                            for (const Function& fn : s.mFunctions)
                                all.push_back(&fn);
                for (const Function* fn : all)
                {
                    if (lower(f.str(fn->mName)) != "oninit")
                        continue;
                    std::set<const Function*> seen;
                    if (writes(f, all, *fn, names, seen))
                        return none;
                }

                // a VMAD value for the property wins over the declared initial one
                const std::string key = !propertyName.empty() ? propertyName : wanted;
                for (const QuestScriptProperty& p : script.mProperties)
                    if (lower(p.mName) == key)
                        return p.mScalar ? QuestVariableValue{ true, p.mValue } : none;
                if (var == nullptr)
                    return none;
                const std::string type = lower(f.str(var->mType));
                if (type != "int" && type != "float" && type != "bool")
                    return none;
                QuestVariableValue r;
                r.mKnown = true;
                switch (var->mInitial.mType)
                {
                    case ValueType::Integer:
                    case ValueType::Bool:
                        r.mValue = static_cast<float>(var->mInitial.mInteger);
                        break;
                    case ValueType::Float:
                        r.mValue = var->mInitial.mFloat;
                        break;
                    default:
                        r.mValue = 0.f;
                        break;
                }
                return r;
            }
        }
        return none;
    }
}
