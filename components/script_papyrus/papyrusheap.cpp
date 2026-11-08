#include "papyrusvm.hpp"

#include <bit>
#include <stdexcept>

// Names, strings, arrays, structs and the garbage collector of the Papyrus runtime (task 0093 stage c).
//
// A string is an id into mStrings: the strings the scripts hold as constants are interned once and never freed, the
// strings the scripts make (strcat, casts, natives) are "dynamic" and freed by a mark and sweep collection. An array or
// a struct is an id into mHeap. The collection runs only at the start of an allocation, when the heap would pass its
// cap, and finds its roots in the instances' variables and the values of every queued and running stack, so a value
// that an instruction has read but not stored yet is never lost (an allocating instruction allocates once, after it has
// read).

namespace ScriptPapyrus::Vm
{
    Val Val::makeInt(std::int32_t value)
    {
        return { Kind::Int, std::bit_cast<std::uint32_t>(value), 0 };
    }

    Val Val::makeFloat(float value)
    {
        return { Kind::Float, std::bit_cast<std::uint32_t>(value), 0 };
    }

    Val Val::makeBool(bool value)
    {
        return { Kind::Bool, value ? 1u : 0u, 0 };
    }

    std::int32_t Val::asInt() const
    {
        return std::bit_cast<std::int32_t>(a);
    }

    float Val::asFloat() const
    {
        return std::bit_cast<float>(a);
    }

    namespace
    {
        constexpr std::uint64_t kObjectOverhead = 32;

        char lowerChar(char c)
        {
            return c >= 'A' && c <= 'Z' ? static_cast<char>(c - 'A' + 'a') : c;
        }

        Val zeroOfKind(TypeKind kind)
        {
            switch (kind)
            {
                case TypeKind::Int:
                    return Val::makeInt(0);
                case TypeKind::Float:
                    return Val::makeFloat(0.0f);
                case TypeKind::Bool:
                    return Val::makeBool(false);
                case TypeKind::String:
                    return Val::makeString(0);
                default:
                    return Val{};
            }
        }
    }

    // ---- names
    // -------------------------------------------------------------------------------------------------------

    // The permanent tables (names and permanent strings) are bounded as a whole: a script that names a new state in a
    // loop, or a host that sends a new event name every frame, must not grow them without end (review finding 3).
    // Each entry counts its text twice, as key and as copy.
    std::string Runtime::nameTableFull(std::string_view text) const
    {
        if (mNames.mShown.size() >= mOptions.maxNames)
        {
            return "the table of names is full (" + std::to_string(mNames.mShown.size()) + " names, cap "
                + std::to_string(mOptions.maxNames) + ")";
        }
        if (mPermanentBytes + 2 * text.size() > mOptions.maxInternedBytes)
        {
            return "the permanent tables are full (" + std::to_string(mPermanentBytes) + " bytes, cap "
                + std::to_string(mOptions.maxInternedBytes) + ")";
        }
        return {};
    }

    std::string Runtime::stringTableFull(std::string_view text) const
    {
        if (mInterned.size() >= mOptions.maxInternedStrings)
        {
            return "the table of permanent strings is full (" + std::to_string(mInterned.size()) + " strings, cap "
                + std::to_string(mOptions.maxInternedStrings) + ")";
        }
        if (mPermanentBytes + 2 * text.size() > mOptions.maxInternedBytes)
        {
            return "the permanent tables are full (" + std::to_string(mPermanentBytes) + " bytes, cap "
                + std::to_string(mOptions.maxInternedBytes) + ")";
        }
        return {};
    }

    std::uint32_t Runtime::nameId(std::string_view text)
    {
        mNameScratch.assign(text);
        for (char& c : mNameScratch)
        {
            c = lowerChar(c);
        }
        const auto found = mNames.mIds.find(std::string_view(mNameScratch));
        if (found != mNames.mIds.end())
        {
            return found->second;
        }
        if (const std::string full = nameTableFull(text); !full.empty())
        {
            throw std::length_error(full);
        }
        const std::uint32_t id = static_cast<std::uint32_t>(mNames.mShown.size());
        mPermanentBytes += 2 * text.size();
        mNames.mIds.emplace(mNameScratch, id);
        mNames.mShown.emplace_back(text);
        mNames.mIsEvent.push_back(mNameScratch.size() > 2 && mNameScratch[0] == 'o' && mNameScratch[1] == 'n' ? 1 : 0);
        return id;
    }

    std::uint32_t Runtime::findNameId(std::string_view text) const
    {
        mNameScratchConst.assign(text);
        for (char& c : mNameScratchConst)
        {
            c = lowerChar(c);
        }
        const auto found = mNames.mIds.find(std::string_view(mNameScratchConst));
        return found == mNames.mIds.end() ? kNoScript : found->second;
    }

    std::string_view Runtime::nameText(std::uint32_t id) const
    {
        return id < mNames.mShown.size() ? std::string_view(mNames.mShown[id]) : std::string_view();
    }

    // ---- strings
    // -----------------------------------------------------------------------------------------------------

    std::uint32_t Runtime::internString(std::string_view text)
    {
        const auto found = mInterned.find(text);
        if (found != mInterned.end())
        {
            return found->second;
        }
        if (const std::string full = stringTableFull(text); !full.empty())
        {
            throw std::length_error(full);
        }
        const std::uint32_t id = static_cast<std::uint32_t>(mStrings.size());
        mPermanentBytes += 2 * text.size();
        StrEntry entry;
        entry.text.assign(text);
        mStrings.push_back(std::move(entry));
        mInterned.emplace(std::string(text), id);
        return id;
    }

    std::string_view Runtime::textOf(const Val& value) const
    {
        return value.kind == Kind::String && value.a < mStrings.size() ? std::string_view(mStrings[value.a].text)
                                                                       : std::string_view();
    }

    bool Runtime::chargeHeap(std::uint64_t bytes)
    {
        if (mHeapBytes + bytes > mOptions.maxHeapBytes)
        {
            collect();
            if (mHeapBytes + bytes > mOptions.maxHeapBytes)
            {
                return fail(ErrorKind::LimitExceeded,
                    "the VM heap is full (" + std::to_string(mHeapBytes) + " bytes held, " + std::to_string(bytes)
                        + " more asked, cap " + std::to_string(mOptions.maxHeapBytes) + ")");
            }
        }
        mHeapBytes += bytes;
        return true;
    }

    bool Runtime::newString(std::string_view text, Val& out)
    {
        if (text.empty())
        {
            out = Val::makeString(0);
            return true;
        }
        if (text.size() > mOptions.maxStringBytes)
        {
            return fail(ErrorKind::LimitExceeded,
                "a string of " + std::to_string(text.size()) + " bytes is over the cap of "
                    + std::to_string(mOptions.maxStringBytes));
        }
        if (!chargeHeap(text.size() + kObjectOverhead))
        {
            return false;
        }
        std::uint32_t id;
        if (!mFreeStrings.empty())
        {
            id = mFreeStrings.back();
            mFreeStrings.pop_back();
        }
        else
        {
            id = static_cast<std::uint32_t>(mStrings.size());
            mStrings.emplace_back();
        }
        StrEntry& entry = mStrings[id];
        entry.text.assign(text);
        entry.dynamic = true;
        entry.live = true;
        entry.mark = false;
        mLiveStrings.push_back(id);
        out = Val::makeString(id);
        return true;
    }

    // ---- arrays and structs
    // ------------------------------------------------------------------------------------------

    namespace
    {
        std::uint32_t takeHeapSlot(std::deque<HeapObj>& heap, std::vector<std::uint32_t>& freeList)
        {
            if (!freeList.empty())
            {
                const std::uint32_t id = freeList.back();
                freeList.pop_back();
                return id;
            }
            heap.emplace_back();
            return static_cast<std::uint32_t>(heap.size() - 1);
        }
    }

    bool Runtime::newArray(TypeKind elem, std::size_t length, Val& out)
    {
        if (length > mOptions.maxArrayLength)
        {
            return fail(ErrorKind::LimitExceeded,
                "an array of " + std::to_string(length) + " elements is over the cap of "
                    + std::to_string(mOptions.maxArrayLength));
        }
        if (!chargeHeap(length * sizeof(Val) + kObjectOverhead))
        {
            return false;
        }
        const std::uint32_t id = takeHeapSlot(mHeap, mFreeHeap);
        HeapObj& object = mHeap[id];
        object.type = HeapObj::Type::Array;
        object.mark = false;
        object.elem = elem;
        object.items.assign(length, zeroOfKind(elem));
        mLiveHeap.push_back(id);
        out = Val::makeArray(id);
        return true;
    }

    bool Runtime::newStruct(std::uint32_t scriptIndex, std::uint32_t structName, Val& out)
    {
        const auto found = mScripts[scriptIndex].mStructs.find(structName);
        if (found == mScripts[scriptIndex].mStructs.end())
        {
            return fail(ErrorKind::TypeMismatch,
                "script '" + mScripts[scriptIndex].mClass + "' has no struct '" + std::string(nameText(structName))
                    + "'");
        }
        const StructInfo& info = found->second;
        if (!chargeHeap(info.memberDefaults.size() * sizeof(Val) + kObjectOverhead))
        {
            return false;
        }
        const std::uint32_t id = takeHeapSlot(mHeap, mFreeHeap);
        HeapObj& object = mHeap[id];
        object.type = HeapObj::Type::Struct;
        object.mark = false;
        object.script = scriptIndex;
        object.structName = structName;
        object.items = info.memberDefaults;
        mLiveHeap.push_back(id);
        out = Val::makeStruct(id);
        return true;
    }

    // ---- garbage collection
    // ------------------------------------------------------------------------------------------

    void Runtime::markVal(const Val& value, std::vector<std::uint32_t>& pending)
    {
        switch (value.kind)
        {
            case Kind::String:
                if (value.a < mStrings.size() && mStrings[value.a].dynamic && mStrings[value.a].live)
                {
                    mStrings[value.a].mark = true;
                }
                break;
            case Kind::Array:
            case Kind::Struct:
                if (value.a < mHeap.size() && mHeap[value.a].type != HeapObj::Type::Free && !mHeap[value.a].mark)
                {
                    mHeap[value.a].mark = true;
                    pending.push_back(value.a);
                }
                break;
            default:
                break;
        }
    }

    void Runtime::gatherRoots(std::vector<std::uint32_t>& pending)
    {
        for (const Instance& instance : mInstances)
        {
            for (const Val& value : instance.vars)
            {
                markVal(value, pending);
            }
        }
        auto markStack = [&](const Stack* stack) {
            for (const Val& value : stack->values)
            {
                markVal(value, pending);
            }
            markVal(stack->result, pending);
        };
        for (std::size_t i = mReadyHead; i < mReady.size(); ++i)
        {
            markStack(mReady[i]);
        }
        for (const Stack* stack : mRunning)
        {
            markStack(stack);
        }
        // Parked stacks (task 0093 stage d): the ones in the heap and the due ones a tick has not run yet.
        for (const WaitEntry& entry : mWaiting)
        {
            markStack(entry.stack);
        }
        for (std::size_t i = mDueHead; i < mDue.size(); ++i)
        {
            if (mDue[i].stack != nullptr)
            {
                markStack(mDue[i].stack);
            }
        }
    }

    // A collection looks at the live dynamic strings and the live arrays and structs only, never at the slots of the
    // ones already freed, so its cost does not depend on the high-water mark of earlier garbage (review finding 5).
    // Marks are false outside a collection: the sweep clears the survivors'.
    void Runtime::collect()
    {
        ++mStats.collections;
        std::vector<std::uint32_t>& pending = mMarkScratch;
        pending.clear();
        gatherRoots(pending);
        while (!pending.empty())
        {
            const std::uint32_t id = pending.back();
            pending.pop_back();
            for (const Val& value : mHeap[id].items)
            {
                markVal(value, pending);
            }
        }

        std::uint64_t bytes = 0;
        std::size_t kept = 0;
        for (const std::uint32_t id : mLiveStrings)
        {
            ++mStats.collectionVisits;
            StrEntry& entry = mStrings[id];
            if (entry.mark)
            {
                entry.mark = false;
                bytes += entry.text.size() + kObjectOverhead;
                mLiveStrings[kept++] = id;
            }
            else
            {
                entry.live = false;
                // A freed 64 KiB string must not keep its buffer.
                if (entry.text.capacity() > 1024)
                {
                    std::string().swap(entry.text);
                }
                else
                {
                    entry.text.clear();
                }
                mFreeStrings.push_back(id);
            }
        }
        mLiveStrings.resize(kept);
        kept = 0;
        for (const std::uint32_t id : mLiveHeap)
        {
            ++mStats.collectionVisits;
            HeapObj& object = mHeap[id];
            if (object.mark)
            {
                object.mark = false;
                bytes += object.items.size() * sizeof(Val) + kObjectOverhead;
                mLiveHeap[kept++] = id;
            }
            else
            {
                object.type = HeapObj::Type::Free;
                if (object.items.capacity() > 256)
                {
                    std::vector<Val>().swap(object.items);
                }
                else
                {
                    object.items.clear();
                }
                mFreeHeap.push_back(id);
            }
        }
        mLiveHeap.resize(kept);
        mHeapBytes = bytes;
    }
}
