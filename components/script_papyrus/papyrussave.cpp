// Task 0093 stage e: script state of the Papyrus runtime, as a versioned blob.
//
// In plain words: serialise() writes every instance (its key, script, state and variables), every event that is queued
// and has not started, and every parked stack (its frames, locals, wake time, creation sequence and latent token), plus
// the strings, arrays and structs they reach. restore() reads such a blob into a runtime that has the same scripts
// loaded, checks all of it before it trusts any of it, builds the new state next to the old one and only then switches.
// A refused blob changes nothing. The runtime never writes a save itself: the game pack keeps the bytes in its own
// chunk. The layout is in papyrussave.hpp.
//
// Nothing in the blob is a pointer or a number that belongs to one run (a string id, a name id, a script index): texts,
// scripts and heap objects are numbered in the order the writer meets them, so a blob that is restored and written
// again is the same bytes. Main thread only.

#include "papyrussave.hpp"

#include <algorithm>
#include <bit>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <limits>
#include <map>
#include <set>
#include <stdexcept>
#include <unordered_map>
#include <unordered_set>

#include "papyruslimits.hpp"
#include "papyrusvm.hpp"

namespace ScriptPapyrus::Vm
{
    using namespace openeden::script;

    namespace
    {
        constexpr std::uint8_t kFlagParked = 1;
        constexpr std::uint8_t kFlagStarted = 2;
        constexpr std::uint8_t kFlagHostWait = 4;
        constexpr std::uint8_t kHeapArray = 1;
        constexpr std::uint8_t kHeapStruct = 2;
        constexpr std::uint8_t kLastKind = static_cast<std::uint8_t>(Kind::Struct);
        constexpr std::uint8_t kLastTypeKind = static_cast<std::uint8_t>(TypeKind::Var);
        constexpr std::uint64_t kObjectOverhead = 32; // as papyrusheap.cpp counts a string, array or struct
        constexpr std::size_t kMinInstanceBytes = 1 + 4 * 6;
        constexpr std::size_t kMinStackBytes = 1 + 8 + 4 * 4 + 4;

        std::string lowerCopy(std::string_view text)
        {
            std::string out(text);
            for (char& c : out)
            {
                if (c >= 'A' && c <= 'Z')
                {
                    c = static_cast<char>(c - 'A' + 'a');
                }
            }
            return out;
        }

        // ---- writing ---------------------------------------------------------------------------------------------

        class Out
        {
        public:
            void u8(std::uint8_t v) { mBytes.push_back(v); }
            void u32(std::uint32_t v)
            {
                for (int i = 0; i < 4; ++i)
                {
                    mBytes.push_back(static_cast<std::uint8_t>((v >> (8 * i)) & 0xFFu));
                }
            }
            void u64(std::uint64_t v)
            {
                for (int i = 0; i < 8; ++i)
                {
                    mBytes.push_back(static_cast<std::uint8_t>((v >> (8 * i)) & 0xFFu));
                }
            }
            void bytes(const std::string& text) { mBytes.insert(mBytes.end(), text.begin(), text.end()); }
            void append(const Out& other) { mBytes.insert(mBytes.end(), other.mBytes.begin(), other.mBytes.end()); }
            std::vector<std::uint8_t> mBytes;
        };

        // ---- reading ---------------------------------------------------------------------------------------------

        /// Why a blob is refused. The message already carries the place in the blob where there is one.
        struct Refusal
        {
            std::string mMessage;
        };

        class Reader
        {
        public:
            Reader(const std::uint8_t* data, std::size_t size, std::size_t start)
                : mData(data)
                , mSize(size)
                , mPos(start)
            {
            }

            [[noreturn]] void fail(const std::string& message) const
            {
                throw Refusal{ "blob offset " + std::to_string(mPos) + ": " + message };
            }
            std::size_t remaining() const { return mSize - mPos; }
            std::size_t pos() const { return mPos; }
            void need(std::size_t n, const char* what) const
            {
                if (remaining() < n)
                {
                    fail(std::string("the blob ends inside ") + what);
                }
            }
            std::uint8_t u8(const char* what)
            {
                need(1, what);
                return mData[mPos++];
            }
            std::uint32_t u32(const char* what)
            {
                need(4, what);
                std::uint32_t v = 0;
                for (int i = 0; i < 4; ++i)
                {
                    v |= static_cast<std::uint32_t>(mData[mPos + static_cast<std::size_t>(i)]) << (8 * i);
                }
                mPos += 4;
                return v;
            }
            std::uint64_t u64(const char* what)
            {
                need(8, what);
                std::uint64_t v = 0;
                for (int i = 0; i < 8; ++i)
                {
                    v |= static_cast<std::uint64_t>(mData[mPos + static_cast<std::size_t>(i)]) << (8 * i);
                }
                mPos += 8;
                return v;
            }
            /// A count that is checked against its cap, and against the bytes that are left, before anything is
            /// reserved for it: each counted item takes at least \a minBytes.
            std::uint32_t count(std::size_t minBytes, std::size_t cap, const char* what)
            {
                const std::size_t at = mPos;
                const std::uint32_t n = u32(what);
                if (n > cap)
                {
                    mPos = at;
                    fail(std::string(what) + ": " + std::to_string(n) + " is over the cap of " + std::to_string(cap));
                }
                if (static_cast<std::uint64_t>(n) * minBytes > remaining())
                {
                    mPos = at;
                    fail(std::string(what) + ": " + std::to_string(n) + " items do not fit in the bytes that are left");
                }
                return n;
            }
            std::string text(std::size_t length, const char* what)
            {
                need(length, what);
                std::string out(reinterpret_cast<const char*>(mData + mPos), length);
                mPos += length;
                return out;
            }

        private:
            const std::uint8_t* mData;
            std::size_t mSize;
            std::size_t mPos;
        };

        // ---- the blob as plain data --------------------------------------------------------------------------------

        struct PVal
        {
            std::uint8_t mKind = 0;
            std::uint32_t mA = 0;
            std::uint32_t mB = 0;
        };
        struct PVar
        {
            std::uint32_t mName = 0;
            std::uint8_t mKind = 0;
        };
        struct PScript
        {
            std::uint32_t mName = 0;
            std::uint64_t mChain = 0;
            std::vector<PVar> mVars;
        };
        struct PHeap
        {
            std::uint8_t mType = 0;
            std::uint8_t mElem = 0;
            std::uint32_t mScript = 0;
            std::uint32_t mName = 0;
            std::vector<PVal> mItems;
        };
        struct PInstance
        {
            AttachKey mKey;
            std::uint32_t mScript = 0;
            std::uint32_t mState = 0;
            std::vector<PVal> mVars;
        };
        struct PFrame
        {
            std::uint32_t mScript = 0;
            std::uint32_t mFunc = 0;
            std::uint32_t mPc = 0;
            PVal mSelf;
            std::uint8_t mRetKind = 0;
            std::uint32_t mRetSlot = 0;
        };
        struct PStack
        {
            bool mParked = false;
            bool mStarted = false;
            bool mHostWait = false;
            std::uint64_t mSeq = 0;
            std::uint32_t mInstance = 0;
            std::uint32_t mEvent = 0;
            std::uint32_t mArgCount = 0;
            double mWake = 0.0;
            std::uint64_t mToken = 0;
            std::uint32_t mPool = 0;
            std::vector<PFrame> mFrames;
            std::vector<PVal> mValues;
        };
        struct Plain
        {
            double mNow = 0.0;
            std::uint64_t mNextSeq = 0;
            std::uint32_t mParkCounter = 0;
            std::uint32_t mInstanceCount = 0;
            std::vector<std::string> mTexts;
            std::vector<std::uint8_t> mTextIsString; ///< A String value uses this text.
            std::vector<PScript> mScripts;
            std::vector<std::uint8_t> mHeapTypes;
            std::vector<PHeap> mHeap;
            std::vector<PInstance> mInstances;
            std::vector<PStack> mStacks;
            std::uint32_t mParkedCount = 0;
            std::uint32_t mReadyCount = 0;
        };

        /// What parse() needs to know about the runtime that reads the blob.
        struct ParseLimits
        {
            std::size_t mMaxStringBytes = 0;
            std::size_t mMaxArrayLength = 0;
            std::size_t mMaxInstances = 0;
            std::size_t mMaxStacks = 0;
            std::uint64_t mMaxHeapBytes = 0;
            std::size_t mMaxFrames = 0;
        };

        Plain parse(const SaveBlob& blob, const ParseLimits& limits)
        {
            if (blob.bytes.empty())
            {
                throw Refusal{ "the blob is empty" };
            }
            if (blob.formatVersion != Save::kFormatVersion)
            {
                throw Refusal{ "the blob has format version " + std::to_string(blob.formatVersion)
                    + "; this runtime reads version " + std::to_string(Save::kFormatVersion) };
            }
            if (blob.bytes.size() > Save::kMaxBlobBytes)
            {
                throw Refusal{ "the blob is " + std::to_string(blob.bytes.size()) + " bytes, over the cap of "
                    + std::to_string(Save::kMaxBlobBytes) };
            }
            Reader header(blob.bytes.data(), blob.bytes.size(), 0);
            if (blob.bytes.size() < Save::kHeaderBytes)
            {
                header.fail("the blob is shorter than its header");
            }
            if (header.u32("the header") != Save::kMagic)
            {
                throw Refusal{ "blob offset 0: this is not a Papyrus script state (wrong magic)" };
            }
            if (header.u32("the header") != Save::kFormatVersion)
            {
                throw Refusal{ "blob offset 4: the header names another format version than the blob's version field" };
            }
            const std::uint64_t bodyBytes = header.u64("the header");
            const std::uint64_t checksum = header.u64("the header");
            if (bodyBytes != blob.bytes.size() - Save::kHeaderBytes)
            {
                throw Refusal{ "blob offset 8: the body length " + std::to_string(bodyBytes) + " does not match the "
                    + std::to_string(blob.bytes.size() - Save::kHeaderBytes) + " bytes that follow the header" };
            }
            if (Save::fnv1a(blob.bytes.data() + Save::kHeaderBytes, static_cast<std::size_t>(bodyBytes)) != checksum)
            {
                throw Refusal{ "blob offset 16: the checksum does not match the body (the blob is damaged)" };
            }

            Reader in(blob.bytes.data(), blob.bytes.size(), Save::kHeaderBytes);
            Plain plain;
            plain.mNow = std::bit_cast<double>(in.u64("the script time"));
            if (!std::isfinite(plain.mNow) || plain.mNow < 0.0)
            {
                in.fail("the script time is not a finite number of seconds at or above zero");
            }
            plain.mNextSeq = in.u64("the next sequence");
            plain.mParkCounter = in.u32("the park counter");
            plain.mInstanceCount = in.u32("the instance count");
            if (plain.mInstanceCount > limits.mMaxInstances
                || static_cast<std::uint64_t>(plain.mInstanceCount) * kMinInstanceBytes > in.remaining())
            {
                in.fail("the instance count " + std::to_string(plain.mInstanceCount) + " is not possible here");
            }

            // texts
            const std::uint32_t textCount = in.count(4, Save::kMaxTexts, "the text count");
            plain.mTexts.reserve(textCount);
            for (std::uint32_t i = 0; i < textCount; ++i)
            {
                const std::uint32_t length = in.u32("a text length");
                if (length > Limits::kMaxStringBytes)
                {
                    in.fail("a text of " + std::to_string(length) + " bytes is over the cap of "
                        + std::to_string(Limits::kMaxStringBytes));
                }
                plain.mTexts.push_back(in.text(length, "a text"));
            }
            plain.mTextIsString.assign(plain.mTexts.size(), 0);
            auto textRef = [&](const char* what) {
                const std::uint32_t index = in.u32(what);
                if (index >= plain.mTexts.size())
                {
                    in.fail(std::string(what) + ": text " + std::to_string(index) + " does not exist");
                }
                return index;
            };

            // scripts
            const std::uint32_t scriptCount = in.count(16, Limits::kMaxScripts, "the script count");
            plain.mScripts.reserve(scriptCount);
            for (std::uint32_t i = 0; i < scriptCount; ++i)
            {
                PScript script;
                script.mName = textRef("a script name");
                script.mChain = in.u64("a script digest");
                const std::uint32_t vars = in.count(5, Limits::kMaxMembersPerObject * 64, "a script's variable count");
                script.mVars.reserve(vars);
                for (std::uint32_t v = 0; v < vars; ++v)
                {
                    PVar var;
                    var.mName = textRef("a variable name");
                    var.mKind = in.u8("a variable type");
                    if (var.mKind > kLastTypeKind)
                    {
                        in.fail("a variable has type kind " + std::to_string(var.mKind));
                    }
                    script.mVars.push_back(var);
                }
                plain.mScripts.push_back(std::move(script));
            }
            auto scriptRef = [&](const char* what) {
                const std::uint32_t index = in.u32(what);
                if (index >= plain.mScripts.size())
                {
                    in.fail(std::string(what) + ": script " + std::to_string(index) + " does not exist");
                }
                return index;
            };

            // heap: the types first, so that a value can be checked against the object it names
            const std::uint32_t heapCount = in.count(1 + 5, Save::kMaxTotalValues, "the heap object count");
            // Every object takes at least kObjectOverhead bytes of the VM heap: refuse a count the heap cap cannot hold
            // before one entry is reserved for it.
            if (static_cast<std::uint64_t>(heapCount) * kObjectOverhead > limits.mMaxHeapBytes)
            {
                in.fail("the heap object count " + std::to_string(heapCount) + " needs at least "
                    + std::to_string(static_cast<std::uint64_t>(heapCount) * kObjectOverhead)
                    + " bytes of VM heap; this runtime's heap is capped at " + std::to_string(limits.mMaxHeapBytes));
            }
            plain.mHeapTypes.reserve(heapCount);
            for (std::uint32_t i = 0; i < heapCount; ++i)
            {
                const std::uint8_t type = in.u8("a heap object type");
                if (type != kHeapArray && type != kHeapStruct)
                {
                    in.fail("heap object " + std::to_string(i) + " has type " + std::to_string(type));
                }
                plain.mHeapTypes.push_back(type);
            }
            std::size_t totalValues = 0;
            auto readVal = [&](const char* what) {
                if (++totalValues > Save::kMaxTotalValues)
                {
                    in.fail("the blob holds more than " + std::to_string(Save::kMaxTotalValues) + " values");
                }
                PVal v;
                v.mKind = in.u8(what);
                if (v.mKind > kLastKind)
                {
                    in.fail(std::string(what) + ": value kind " + std::to_string(v.mKind) + " does not exist");
                }
                switch (static_cast<Kind>(v.mKind))
                {
                    case Kind::None:
                        break;
                    case Kind::Int:
                    case Kind::Float:
                        v.mA = in.u32(what);
                        break;
                    case Kind::Bool:
                        v.mA = in.u32(what);
                        if (v.mA > 1)
                        {
                            in.fail(std::string(what) + ": a Bool holds " + std::to_string(v.mA));
                        }
                        break;
                    case Kind::String:
                    {
                        v.mA = textRef(what);
                        if (plain.mTexts[v.mA].size() > limits.mMaxStringBytes)
                        {
                            in.fail(std::string(what) + ": a string of " + std::to_string(plain.mTexts[v.mA].size())
                                + " bytes is over the cap of " + std::to_string(limits.mMaxStringBytes));
                        }
                        plain.mTextIsString[v.mA] = 1;
                        break;
                    }
                    case Kind::Form:
                        v.mA = in.u32(what);
                        v.mB = textRef(what);
                        break;
                    case Kind::Instance:
                        v.mA = in.u32(what);
                        if (v.mA < 1 || v.mA > plain.mInstanceCount)
                        {
                            in.fail(std::string(what) + ": instance " + std::to_string(v.mA) + " does not exist");
                        }
                        break;
                    case Kind::Array:
                    case Kind::Struct:
                    {
                        v.mA = in.u32(what);
                        const std::uint8_t wanted
                            = v.mKind == static_cast<std::uint8_t>(Kind::Array) ? kHeapArray : kHeapStruct;
                        if (v.mA >= plain.mHeapTypes.size() || plain.mHeapTypes[v.mA] != wanted)
                        {
                            in.fail(std::string(what) + ": heap object " + std::to_string(v.mA)
                                + " does not exist or is not of the kind the value says");
                        }
                        break;
                    }
                }
                return v;
            };
            auto readVals = [&](std::vector<PVal>& out, std::size_t cap, const char* what) {
                const std::uint32_t n = in.count(1, cap, what);
                out.reserve(n);
                for (std::uint32_t i = 0; i < n; ++i)
                {
                    out.push_back(readVal(what));
                }
            };

            plain.mHeap.reserve(heapCount);
            for (std::uint32_t i = 0; i < heapCount; ++i)
            {
                PHeap object;
                object.mType = plain.mHeapTypes[i];
                if (object.mType == kHeapArray)
                {
                    object.mElem = in.u8("an array element kind");
                    if (object.mElem > kLastTypeKind)
                    {
                        in.fail("an array has element kind " + std::to_string(object.mElem));
                    }
                    readVals(object.mItems, limits.mMaxArrayLength, "an array's length");
                }
                else
                {
                    object.mScript = scriptRef("a struct's script");
                    object.mName = textRef("a struct's name");
                    readVals(object.mItems, Limits::kMaxStructMembers, "a struct's member count");
                }
                plain.mHeap.push_back(std::move(object));
            }

            // instances
            plain.mInstances.reserve(plain.mInstanceCount);
            for (std::uint32_t i = 0; i < plain.mInstanceCount; ++i)
            {
                PInstance instance;
                const std::uint8_t kind = in.u8("an attach kind");
                if (kind > static_cast<std::uint8_t>(AttachKind::ActiveEffect))
                {
                    in.fail("instance " + std::to_string(i + 1) + " has attach kind " + std::to_string(kind));
                }
                instance.mKey.kind = static_cast<AttachKind>(kind);
                instance.mKey.formId = in.u32("a form id");
                instance.mKey.aliasIndex = in.u32("an alias index");
                instance.mKey.effectKey = in.u32("an effect key");
                instance.mScript = scriptRef("an instance's script");
                instance.mState = textRef("an instance's state");
                readVals(instance.mVars, plain.mScripts[instance.mScript].mVars.size(), "an instance's variable count");
                if (instance.mVars.size() != plain.mScripts[instance.mScript].mVars.size())
                {
                    in.fail("instance " + std::to_string(i + 1) + " holds " + std::to_string(instance.mVars.size())
                        + " variables but its script lists "
                        + std::to_string(plain.mScripts[instance.mScript].mVars.size()));
                }
                plain.mInstances.push_back(std::move(instance));
            }

            // stacks
            const std::uint32_t stackCount = in.count(kMinStackBytes, limits.mMaxStacks, "the stack count");
            plain.mStacks.reserve(stackCount);
            std::uint64_t lastSeq = 0;
            std::set<std::uint32_t> pools;
            for (std::uint32_t i = 0; i < stackCount; ++i)
            {
                PStack stack;
                const std::uint8_t flags = in.u8("a stack's flags");
                if ((flags & ~(kFlagParked | kFlagStarted | kFlagHostWait)) != 0)
                {
                    in.fail("stack " + std::to_string(i) + " has flags " + std::to_string(flags));
                }
                stack.mParked = (flags & kFlagParked) != 0;
                stack.mStarted = (flags & kFlagStarted) != 0;
                stack.mHostWait = (flags & kFlagHostWait) != 0;
                if (stack.mStarted != stack.mParked || (stack.mHostWait && !stack.mParked))
                {
                    in.fail("stack " + std::to_string(i)
                        + " is inconsistent: a stack that has started is parked, and only a parked stack waits for "
                          "the host");
                }
                stack.mSeq = in.u64("a stack's sequence");
                if (i > 0 && stack.mSeq <= lastSeq)
                {
                    in.fail("the stacks are not in strictly increasing creation order");
                }
                if (stack.mSeq >= plain.mNextSeq)
                {
                    in.fail("stack " + std::to_string(i) + " has a sequence at or above the next one");
                }
                lastSeq = stack.mSeq;
                stack.mInstance = in.u32("a stack's instance");
                if (stack.mInstance < 1 || stack.mInstance > plain.mInstanceCount)
                {
                    in.fail("stack " + std::to_string(i) + " belongs to instance " + std::to_string(stack.mInstance)
                        + ", which does not exist");
                }
                stack.mEvent = textRef("a stack's event name");
                stack.mArgCount = in.u32("a stack's argument count");
                if (stack.mArgCount > kMaxCallArgs)
                {
                    in.fail("a stack has " + std::to_string(stack.mArgCount) + " arguments");
                }
                if (stack.mParked)
                {
                    stack.mWake = std::bit_cast<double>(in.u64("a wake time"));
                    // A wait for the host may be unbounded (+infinity); a stack the host has completed is due before
                    // every timer (-infinity); a timed wait is neither.
                    const bool badWake = std::isnan(stack.mWake)
                        || (stack.mHostWait ? stack.mWake == -std::numeric_limits<double>::infinity()
                                            : stack.mWake == std::numeric_limits<double>::infinity());
                    if (badWake)
                    {
                        in.fail("a wake time does not fit the kind of wait the stack is in");
                    }
                    stack.mToken = in.u64("a latent token");
                    stack.mPool = in.u32("a pool index");
                    if (stack.mPool >= Save::kMaxPoolSlots)
                    {
                        in.fail("a pool index " + std::to_string(stack.mPool) + " is over the cap");
                    }
                    if (stack.mToken
                        != ((static_cast<std::uint64_t>(stack.mPool) + 1) << 32 | (stack.mToken & 0xFFFFFFFFu)))
                    {
                        in.fail("a latent token does not name the pool slot of its stack");
                    }
                    if (!pools.insert(stack.mPool).second)
                    {
                        in.fail("two parked stacks share pool slot " + std::to_string(stack.mPool));
                    }
                    ++plain.mParkedCount;
                }
                else
                {
                    ++plain.mReadyCount;
                }
                const std::uint32_t frames = in.count(
                    1 + 4 + 4 + 4 + 1 + 1 + 4, std::min(Save::kMaxFrames, limits.mMaxFrames), "a stack's frame count");
                stack.mFrames.reserve(frames);
                for (std::uint32_t f = 0; f < frames; ++f)
                {
                    PFrame frame;
                    frame.mScript = scriptRef("a frame's script");
                    frame.mFunc = in.u32("a frame's function");
                    frame.mPc = in.u32("a frame's position");
                    frame.mSelf = readVal("a frame's self");
                    frame.mRetKind = in.u8("a frame's result destination");
                    frame.mRetSlot = in.u32("a frame's result slot");
                    stack.mFrames.push_back(frame);
                }
                readVals(stack.mValues, Save::kMaxStackValues, "a stack's value count");
                if (stack.mParked && stack.mFrames.empty())
                {
                    in.fail("a parked stack has no frames");
                }
                if (!stack.mParked && (!stack.mFrames.empty() || stack.mValues.size() != stack.mArgCount))
                {
                    in.fail("a queued stack must have no frames and exactly its arguments as values");
                }
                plain.mStacks.push_back(std::move(stack));
            }
            if (in.remaining() != 0)
            {
                in.fail(std::to_string(in.remaining()) + " bytes follow the last stack");
            }
            if (plain.mReadyCount > Save::kMaxQueuedStacks)
            {
                throw Refusal{ "the blob queues " + std::to_string(plain.mReadyCount) + " events, over the cap of "
                    + std::to_string(Save::kMaxQueuedStacks) };
            }
            return plain;
        }

        // ---- the variable layout of a script ------------------------------------------------------------------------

        struct Slot
        {
            std::uint32_t mName = 0; ///< Lower-cased name id.
            std::uint8_t mKind = 0; ///< TypeKind.
            std::uint8_t mElem = 0; ///< An array's element TypeKind.
        };

        /// The slots of an instance of \a scriptIndex, in the order of Instance::vars: parents first.
        std::vector<Slot> layoutOf(const std::vector<Script>& scripts, std::uint32_t scriptIndex)
        {
            const Script& script = scripts[scriptIndex];
            std::size_t slots = script.totalVars();
            for (const std::uint32_t owner : script.mChain)
            {
                slots = std::max<std::size_t>(slots, scripts[owner].mVarBase + scripts[owner].mVars.size());
            }
            std::vector<Slot> layout(slots);
            for (const std::uint32_t owner : script.mChain)
            {
                const Script& chainScript = scripts[owner];
                for (std::size_t i = 0; i < chainScript.mVars.size(); ++i)
                {
                    layout[chainScript.mVarBase + i]
                        = Slot{ chainScript.mVars[i].name, static_cast<std::uint8_t>(chainScript.mVars[i].type.kind),
                              static_cast<std::uint8_t>(chainScript.mVars[i].type.elem) };
                }
            }
            return layout;
        }

        /// Whether a value of the blob may sit where the declared type is. None fits everywhere (a variable nothing was
        /// stored in yet); a number fits any number type, as the interpreter converts between them; an array fits an
        /// array variable of the same element kind (a Var[] takes any).
        bool fitsPlain(std::uint8_t kind, std::uint8_t elem, const PVal& value, const Plain& plain)
        {
            if (static_cast<Kind>(value.mKind) == Kind::None)
            {
                return true;
            }
            const Kind valueKind = static_cast<Kind>(value.mKind);
            switch (static_cast<TypeKind>(kind))
            {
                case TypeKind::Int:
                case TypeKind::Float:
                case TypeKind::Bool:
                    return valueKind == Kind::Int || valueKind == Kind::Float || valueKind == Kind::Bool;
                case TypeKind::String:
                    return valueKind == Kind::String;
                case TypeKind::Object:
                    return valueKind == Kind::Form || valueKind == Kind::Instance;
                case TypeKind::Array:
                {
                    if (valueKind != Kind::Array)
                    {
                        return false;
                    }
                    const TypeKind declared = static_cast<TypeKind>(elem);
                    return declared == TypeKind::Var || declared == TypeKind::None
                        || static_cast<TypeKind>(plain.mHeap[value.mA].mElem) == declared;
                }
                case TypeKind::Struct:
                    return valueKind == Kind::Struct;
                case TypeKind::Var:
                case TypeKind::None:
                    break;
            }
            return true;
        }
    }

    // ---- digests
    // ------------------------------------------------------------------------------------------------------

    std::uint64_t Runtime::chainDigest(std::uint32_t scriptIndex) const
    {
        // The script's own bytes, then its parents' in order (by name, through the scripts that are loaded). It does
        // not look at the prepare state, so a script written before it was prepared and restored after gives the same
        // number (review 7, finding M5). A parent that is not loaded ends the chain with a marker.
        const Script* script = &mScripts[scriptIndex];
        std::uint64_t hash = Save::fnvMix(Save::kFnvBasis, script->mDigest);
        for (std::size_t depth = 0; !script->mParent.empty() && depth < 256; ++depth)
        {
            const std::uint32_t id = findNameId(script->mParent);
            const std::uint32_t parent = id == kNoScript ? kNoScript : scriptByClassId(id);
            if (parent == kNoScript)
            {
                hash = Save::fnvMix(hash, 0);
                break;
            }
            script = &mScripts[parent];
            hash = Save::fnvMix(hash, script->mDigest);
        }
        return hash;
    }

    // ---- serialise
    // ------------------------------------------------------------------------------------------------------

    SaveBlob Runtime::serialise() const noexcept
    {
        try
        {
            return serialiseImpl();
        }
        catch (...)
        {
            return {};
        }
    }

    SaveBlob Runtime::serialiseImpl() const
    {
        SaveBlob blob;
        // A stack that is running, or a tick that is half-way, has state nobody can name: there is no blob then.
        if (mInNative || !mRunning.empty() || mDueHead < mDue.size())
        {
            return blob;
        }

        std::vector<std::string> texts;
        std::unordered_map<std::string, std::uint32_t> textIndex;
        auto text = [&](std::string_view value) -> std::uint32_t {
            const auto found = textIndex.find(std::string(value));
            if (found != textIndex.end())
            {
                return found->second;
            }
            const std::uint32_t id = static_cast<std::uint32_t>(texts.size());
            texts.emplace_back(value);
            textIndex.emplace(texts.back(), id);
            return id;
        };
        std::vector<std::uint32_t> scriptOrder;
        std::unordered_map<std::uint32_t, std::uint32_t> scriptIndex;
        auto scriptRef = [&](std::uint32_t script) -> std::uint32_t {
            const auto found = scriptIndex.find(script);
            if (found != scriptIndex.end())
            {
                return found->second;
            }
            const std::uint32_t id = static_cast<std::uint32_t>(scriptOrder.size());
            scriptOrder.push_back(script);
            scriptIndex.emplace(script, id);
            return id;
        };
        std::vector<std::uint32_t> heapOrder;
        std::unordered_map<std::uint32_t, std::uint32_t> heapIndex;
        auto heapRef = [&](std::uint32_t id) -> std::uint32_t {
            const auto found = heapIndex.find(id);
            if (found != heapIndex.end())
            {
                return found->second;
            }
            const std::uint32_t index = static_cast<std::uint32_t>(heapOrder.size());
            heapOrder.push_back(id);
            heapIndex.emplace(id, index);
            return index;
        };
        std::uint64_t invalidReferences = 0; // references to an instance or heap object that does not exist
        auto writeVal = [&](Out& out, const Val& v) {
            switch (v.kind)
            {
                case Kind::Int:
                case Kind::Float:
                case Kind::Bool:
                    out.u8(static_cast<std::uint8_t>(v.kind));
                    out.u32(v.a);
                    return;
                case Kind::String:
                    out.u8(static_cast<std::uint8_t>(v.kind));
                    out.u32(text(v.a < mStrings.size() ? std::string_view(mStrings[v.a].text) : std::string_view()));
                    return;
                case Kind::Form:
                    out.u8(static_cast<std::uint8_t>(v.kind));
                    out.u32(v.a);
                    out.u32(text(lowerCopy(nameText(v.b))));
                    return;
                case Kind::Instance:
                    if (v.a >= 1 && v.a <= mInstances.size())
                    {
                        out.u8(static_cast<std::uint8_t>(v.kind));
                        out.u32(v.a);
                        return;
                    }
                    ++invalidReferences;
                    break;
                case Kind::Array:
                case Kind::Struct:
                    if (v.a < mHeap.size()
                        && mHeap[v.a].type == (v.kind == Kind::Array ? HeapObj::Type::Array : HeapObj::Type::Struct))
                    {
                        out.u8(static_cast<std::uint8_t>(v.kind));
                        out.u32(heapRef(v.a));
                        return;
                    }
                    ++invalidReferences;
                    break;
                case Kind::None:
                    break;
            }
            out.u8(static_cast<std::uint8_t>(Kind::None));
        };

        // Instances first (they number the scripts and heap objects they meet), then the stacks, then the heap.
        Out instances;
        for (const Instance& instance : mInstances)
        {
            if (instance.vars.size() != layoutOf(mScripts, instance.script).size())
            {
                return blob; // an instance that does not fit its script would write a blob that refuses itself
            }
            instances.u8(static_cast<std::uint8_t>(instance.key.kind));
            instances.u32(instance.key.formId);
            instances.u32(instance.key.aliasIndex);
            instances.u32(instance.key.effectKey);
            instances.u32(scriptRef(instance.script));
            instances.u32(
                text(instance.stateName < mStrings.size() ? std::string_view(mStrings[instance.stateName].text)
                                                          : std::string_view()));
            instances.u32(static_cast<std::uint32_t>(instance.vars.size()));
            for (const Val& v : instance.vars)
            {
                writeVal(instances, v);
            }
        }

        struct Listed
        {
            const Stack* mStack;
            bool mParked;
        };
        std::vector<Listed> listed;
        listed.reserve(mWaiting.size() + (mReady.size() - mReadyHead));
        for (std::size_t i = mReadyHead; i < mReady.size(); ++i)
        {
            listed.push_back({ mReady[i], false });
        }
        for (const WaitEntry& entry : mWaiting)
        {
            listed.push_back({ entry.stack, true });
        }
        std::sort(listed.begin(), listed.end(),
            [](const Listed& a, const Listed& b) { return a.mStack->seq < b.mStack->seq; });

        Out stacks;
        for (const Listed& item : listed)
        {
            const Stack& stack = *item.mStack;
            std::uint8_t flags = 0;
            if (item.mParked)
            {
                flags |= kFlagParked | kFlagStarted;
                if (stack.hostWait)
                {
                    flags |= kFlagHostWait;
                }
            }
            stacks.u8(flags);
            stacks.u64(stack.seq);
            stacks.u32(stack.instance);
            stacks.u32(text(lowerCopy(nameText(stack.eventName))));
            stacks.u32(stack.argCount);
            if (item.mParked)
            {
                stacks.u64(std::bit_cast<std::uint64_t>(stack.wake));
                stacks.u64(stack.parkToken);
                stacks.u32(stack.poolIndex);
            }
            stacks.u32(static_cast<std::uint32_t>(stack.frames.size()));
            for (const Frame& frame : stack.frames)
            {
                stacks.u32(scriptRef(frame.script));
                stacks.u32(frame.func);
                stacks.u32(frame.pc);
                writeVal(stacks, frame.self);
                const OpKind kind = frame.retDest.kind;
                const bool writable = kind == OpKind::Local || kind == OpKind::Var || kind == OpKind::StateVar
                    || kind == OpKind::Discard;
                stacks.u8(static_cast<std::uint8_t>(writable ? kind : OpKind::None));
                stacks.u32(writable ? frame.retDest.n : 0);
            }
            stacks.u32(static_cast<std::uint32_t>(stack.values.size()));
            for (const Val& v : stack.values)
            {
                writeVal(stacks, v);
            }
        }

        Out heap;
        Out heapTypes;
        for (std::size_t i = 0; i < heapOrder.size(); ++i)
        {
            const HeapObj& object = mHeap[heapOrder[i]];
            if (object.type == HeapObj::Type::Array)
            {
                heapTypes.u8(kHeapArray);
                heap.u8(static_cast<std::uint8_t>(object.elem));
            }
            else
            {
                heapTypes.u8(kHeapStruct);
                heap.u32(scriptRef(object.script));
                heap.u32(text(lowerCopy(nameText(object.structName))));
            }
            heap.u32(static_cast<std::uint32_t>(object.items.size()));
            for (const Val& v : object.items)
            {
                writeVal(heap, v);
            }
        }

        Out scripts;
        for (std::size_t i = 0; i < scriptOrder.size(); ++i)
        {
            const std::uint32_t index = scriptOrder[i];
            const std::vector<Slot> layout = layoutOf(mScripts, index);
            scripts.u32(text(lowerCopy(mScripts[index].mClass)));
            scripts.u64(chainDigest(index));
            scripts.u32(static_cast<std::uint32_t>(layout.size()));
            for (const Slot& slot : layout)
            {
                scripts.u32(text(lowerCopy(nameText(slot.mName))));
                scripts.u8(slot.mKind);
            }
        }

        Out textOut;
        textOut.u32(static_cast<std::uint32_t>(texts.size()));
        for (const std::string& value : texts)
        {
            textOut.u32(static_cast<std::uint32_t>(value.size()));
            textOut.bytes(value);
        }

        Out body;
        body.u64(std::bit_cast<std::uint64_t>(mNow));
        body.u64(mNextSeq);
        body.u32(mParkCounter);
        body.u32(static_cast<std::uint32_t>(mInstances.size()));
        body.append(textOut);
        body.u32(static_cast<std::uint32_t>(scriptOrder.size()));
        body.append(scripts);
        body.u32(static_cast<std::uint32_t>(heapOrder.size()));
        body.append(heapTypes);
        body.append(heap);
        body.append(instances);
        body.u32(static_cast<std::uint32_t>(listed.size()));
        body.append(stacks);

        Out whole;
        whole.u32(Save::kMagic);
        whole.u32(Save::kFormatVersion);
        whole.u64(body.mBytes.size());
        whole.u64(Save::fnv1a(body.mBytes.data(), body.mBytes.size()));
        whole.append(body);
        if (whole.mBytes.size() > Save::kMaxBlobBytes)
        {
            return blob;
        }
        blob.formatVersion = Save::kFormatVersion;
        blob.bytes = std::move(whole.mBytes);
        if (invalidReferences != 0)
        {
            // serialise() is const for the caller's sake; the log is the world's, not this object's state.
            try
            {
                const_cast<Runtime*>(this)->worldLog(Severity::Warning,
                    "serialise: " + std::to_string(invalidReferences)
                        + " reference(s) to an instance or heap object that no longer exists were written as None");
            }
            catch (...)
            {
            }
        }
        return blob;
    }

    // ---- restore
    // --------------------------------------------------------------------------------------------------------

    RestoreResult Runtime::restore(const SaveBlob& blob) noexcept
    {
        try
        {
            return restoreImpl(blob);
        }
        catch (const std::length_error& error)
        {
            RestoreResult result;
            try
            {
                result.error.message = std::string("restore refused: [limit exceeded] ") + error.what();
            }
            catch (...)
            {
            }
            return result;
        }
        catch (...)
        {
            RestoreResult result;
            try
            {
                result.error.message = "restore refused: out of memory or an internal error";
            }
            catch (...)
            {
            }
            return result;
        }
    }

    RestoreResult Runtime::restoreImpl(const SaveBlob& blob)
    {
        RestoreResult result;
        if (mInNative || !mRunning.empty())
        {
            result.error.message
                = "restore refused: restore() cannot be used from inside a native, a world callback or a running stack";
            return result;
        }

        std::vector<std::string> warnings;
        struct Guard
        {
            Runtime& mRuntime;
            std::uint64_t mHeapBefore;
            bool mCommitted = false;
            // The permanent name table as it was before the blob added anything: a refused blob takes its names back
            // (review 7, finding M2). Set once the scripts are found and prepared, whose names stay.
            bool mNamesSaved = false;
            std::size_t mNamesBefore = 0;
            std::uint64_t mPermanentBefore = 0;
            void saveNames()
            {
                mNamesSaved = true;
                mNamesBefore = mRuntime.mNames.mShown.size();
                mPermanentBefore = mRuntime.mPermanentBytes;
            }
            ~Guard()
            {
                mRuntime.mRestoring = false;
                if (!mCommitted && mNamesSaved)
                {
                    while (mRuntime.mNames.mShown.size() > mNamesBefore)
                    {
                        mRuntime.mNames.mIds.erase(lowerCopy(mRuntime.mNames.mShown.back()));
                        mRuntime.mNames.mShown.pop_back();
                        mRuntime.mNames.mIsEvent.pop_back();
                    }
                    mRuntime.mPermanentBytes = mPermanentBefore;
                    mRuntime.mStats.names = mRuntime.mNames.mShown.size();
                }
                if (!mCommitted && mRuntime.mHeapBytes != mHeapBefore)
                {
                    // What was built beside the old state is garbage now.
                    try
                    {
                        mRuntime.collect();
                    }
                    catch (...)
                    {
                    }
                }
            }
        } guard{ *this, mHeapBytes };

        try
        {
            ParseLimits limits;
            limits.mMaxStringBytes = mOptions.maxStringBytes;
            limits.mMaxArrayLength = mOptions.maxArrayLength;
            limits.mMaxInstances = mOptions.maxInstances;
            limits.mMaxStacks = static_cast<std::size_t>(mOptions.maxSuspendedStacks) + Save::kMaxQueuedStacks;
            limits.mMaxHeapBytes = mOptions.maxHeapBytes;
            limits.mMaxFrames = mOptions.defaultBudget.maxCallDepth;
            Plain plain = parse(blob, limits);
            const std::vector<std::string>& texts = plain.mTexts;

            if (plain.mParkedCount > mOptions.maxSuspendedStacks)
            {
                throw Refusal{ "the blob parks " + std::to_string(plain.mParkedCount) + " stacks; this runtime allows "
                    + std::to_string(mOptions.maxSuspendedStacks) };
            }

            // ---- which of the blob's scripts are loaded here, and unchanged
            struct Found
            {
                std::uint32_t mIndex = kNoScript;
                bool mChanged = false;
            };
            std::vector<Found> found(plain.mScripts.size());
            for (std::size_t i = 0; i < plain.mScripts.size(); ++i)
            {
                const std::uint32_t id = findNameId(texts[plain.mScripts[i].mName]);
                std::uint32_t index = id == kNoScript ? kNoScript : scriptByClassId(id);
                if (index != kNoScript && !prepare(index))
                {
                    index = kNoScript;
                }
                found[i].mIndex = index;
                found[i].mChanged = index != kNoScript && chainDigest(index) != plain.mScripts[i].mChain;
            }

            guard.saveNames();

            // ---- the keys, and the instances' scripts
            const openeden::script::PapyrusAttachKeyCheck* keyCheck
                = dynamic_cast<const openeden::script::PapyrusAttachKeyCheck*>(&mWorld);
            result.attachKeysChecked = keyCheck != nullptr || plain.mInstances.empty();
            if (keyCheck == nullptr && !plain.mInstances.empty() && mLoggedOnce.insert("restore-attach-keys").second)
            {
                worldLog(Severity::Warning,
                    "restore: the game pack's world offers no PapyrusAttachKeyCheck, so the attach keys of the blob "
                    "were "
                    "NOT validated against the game");
            }
            {
                std::set<AttachKey> keys;
                for (std::size_t i = 0; i < plain.mInstances.size(); ++i)
                {
                    const PInstance& instance = plain.mInstances[i];
                    const std::string label = "instance " + std::to_string(i + 1) + " (form " +
                        [&] {
                            char buffer[16];
                            std::snprintf(buffer, sizeof buffer, "%X", instance.mKey.formId);
                            return std::string(buffer);
                        }()
                        + ", alias " + std::to_string(instance.mKey.aliasIndex) + ")";
                    if (found[instance.mScript].mIndex == kNoScript)
                    {
                        throw Refusal{ label + ": the script '" + texts[plain.mScripts[instance.mScript].mName]
                            + "' is not loaded (or cannot be prepared); load every script before restore()" };
                    }
                    if (!keys.insert(instance.mKey).second)
                    {
                        throw Refusal{ label + ": its attach key is used by an earlier instance" };
                    }
                    bool exists = true;
                    if (keyCheck != nullptr)
                    {
                        WorldScope scope(*this);
                        try
                        {
                            exists = keyCheck->attachKeyExists(instance.mKey);
                        }
                        catch (...)
                        {
                            exists = false;
                        }
                    }
                    if (!exists)
                    {
                        throw Refusal{ label + ": the game no longer has what its attach key names" };
                    }
                }
            }

            // ---- the heap must fit under this runtime's cap, counted before anything is built
            {
                std::uint64_t bytes = 0;
                for (const PHeap& object : plain.mHeap)
                {
                    bytes += object.mItems.size() * sizeof(Val) + kObjectOverhead;
                }
                for (std::size_t i = 0; i < texts.size(); ++i)
                {
                    if (plain.mTextIsString[i] != 0 && !texts[i].empty()
                        && !mInterned.contains(std::string_view(texts[i])))
                    {
                        bytes += texts[i].size() + kObjectOverhead;
                    }
                }
                if (bytes > mOptions.maxHeapBytes)
                {
                    throw Refusal{ "the blob's strings, arrays and structs need " + std::to_string(bytes)
                        + " bytes of VM heap; this runtime's heap is capped at "
                        + std::to_string(mOptions.maxHeapBytes) };
                }
            }

            // ---- build the new state beside the old one
            mRestoring = true;
            auto pending = [&]() {
                std::string message = mErrMsg.empty() ? std::string("the VM could not allocate") : mErrMsg;
                mErrKind = ErrorKind::None;
                mErrMsg.clear();
                mErrLoc.clear();
                mErrFile.clear();
                return Refusal{ message };
            };

            std::vector<Val> strVals(texts.size());
            std::vector<std::uint8_t> strDone(texts.size(), 0);
            auto strVal = [&](std::uint32_t index) {
                if (strDone[index] == 0)
                {
                    const std::string& value = texts[index];
                    Val made;
                    if (value.empty())
                    {
                        made = Val::makeString(0);
                    }
                    else if (const auto interned = mInterned.find(std::string_view(value)); interned != mInterned.end())
                    {
                        made = Val::makeString(interned->second);
                    }
                    else if (!newString(value, made))
                    {
                        throw pending();
                    }
                    strVals[index] = made;
                    strDone[index] = 1;
                }
                return strVals[index];
            };
            std::vector<Val> heapVals(plain.mHeap.size());
            auto convert = [&](const PVal& v) -> Val {
                switch (static_cast<Kind>(v.mKind))
                {
                    case Kind::None:
                        return Val{};
                    case Kind::Int:
                    case Kind::Float:
                    case Kind::Bool:
                        return Val{ static_cast<Kind>(v.mKind), v.mA, 0 };
                    case Kind::String:
                        return strVal(v.mA);
                    case Kind::Form:
                        return Val::makeForm(v.mA, nameId(texts[v.mB]));
                    case Kind::Instance:
                        return Val::makeInstance(v.mA);
                    case Kind::Array:
                    case Kind::Struct:
                        return heapVals[v.mA];
                }
                return Val{};
            };

            // The objects first (empty), so that a value can name any of them; then their contents.
            std::vector<std::uint8_t> heapLive(plain.mHeap.size(), 0);
            std::set<std::string> changedStructs;
            std::uint32_t changedStructObjects = 0;
            for (std::size_t i = 0; i < plain.mHeap.size(); ++i)
            {
                const PHeap& object = plain.mHeap[i];
                if (object.mType == kHeapArray)
                {
                    if (!newArray(static_cast<TypeKind>(object.mElem), object.mItems.size(), heapVals[i]))
                    {
                        throw pending();
                    }
                    heapLive[i] = 1;
                    continue;
                }
                const Found& owner = found[object.mScript];
                const std::string& className = texts[plain.mScripts[object.mScript].mName];
                if (owner.mIndex == kNoScript || owner.mChanged)
                {
                    changedStructs.insert(className);
                    ++changedStructObjects;
                    continue; // the handle becomes None
                }
                const std::uint32_t structName = findNameId(texts[object.mName]);
                const auto info = structName == kNoScript ? mScripts[owner.mIndex].mStructs.end()
                                                          : mScripts[owner.mIndex].mStructs.find(structName);
                if (info == mScripts[owner.mIndex].mStructs.end()
                    || info->second.memberTypes.size() != object.mItems.size())
                {
                    changedStructs.insert(className);
                    ++changedStructObjects;
                    continue;
                }
                if (!newStruct(owner.mIndex, structName, heapVals[i]))
                {
                    throw pending();
                }
                heapLive[i] = 1;
            }
            for (std::size_t i = 0; i < plain.mHeap.size(); ++i)
            {
                if (heapLive[i] == 0)
                {
                    continue;
                }
                const PHeap& object = plain.mHeap[i];
                std::vector<Val> items(object.mItems.size());
                for (std::size_t k = 0; k < items.size(); ++k)
                {
                    std::uint8_t declared = static_cast<std::uint8_t>(TypeKind::Var);
                    std::uint8_t declaredElem = static_cast<std::uint8_t>(TypeKind::Var);
                    if (object.mType == kHeapArray)
                    {
                        declared = object.mElem;
                    }
                    else
                    {
                        const std::uint32_t structName = findNameId(texts[object.mName]);
                        const auto& info = mScripts[found[object.mScript].mIndex].mStructs.find(structName)->second;
                        declared = static_cast<std::uint8_t>(info.memberTypes[k].kind);
                        declaredElem = static_cast<std::uint8_t>(info.memberTypes[k].elem);
                    }
                    if (!fitsPlain(declared, declaredElem, object.mItems[k], plain))
                    {
                        throw Refusal{ "heap object " + std::to_string(i) + ", item " + std::to_string(k)
                            + ": the value does not fit the declared type" };
                    }
                    items[k] = convert(object.mItems[k]);
                }
                mHeap[heapVals[i].a].items = std::move(items);
            }

            // ---- instances
            std::vector<Instance> newInstances;
            newInstances.reserve(plain.mInstances.size());
            std::map<AttachKey, std::uint32_t> newByKey;
            std::set<std::string> changedScripts;
            for (std::size_t i = 0; i < plain.mInstances.size(); ++i)
            {
                const PInstance& saved = plain.mInstances[i];
                const Found& owner = found[saved.mScript];
                const PScript& savedScript = plain.mScripts[saved.mScript];
                const Script& script = mScripts[owner.mIndex];
                Instance instance;
                instance.key = saved.mKey;
                instance.script = owner.mIndex;
                // The state's name is interned after every check has passed (see "the last things that can fail").
                instance.stateName = 0;
                instance.stateId = 0;
                const std::vector<Slot> layout = layoutOf(mScripts, owner.mIndex);
                instance.vars.resize(layout.size());
                for (const std::uint32_t chainOwner : script.mChain)
                {
                    const Script& chainScript = mScripts[chainOwner];
                    for (std::size_t v = 0; v < chainScript.mVars.size(); ++v)
                    {
                        instance.vars[chainScript.mVarBase + v] = chainScript.mVars[v].initial;
                    }
                }
                if (!owner.mChanged)
                {
                    if (saved.mVars.size() != layout.size())
                    {
                        throw Refusal{ "instance " + std::to_string(i + 1) + " holds "
                            + std::to_string(saved.mVars.size()) + " variables but its script has "
                            + std::to_string(layout.size()) };
                    }
                    for (std::size_t v = 0; v < layout.size(); ++v)
                    {
                        if (!fitsPlain(layout[v].mKind, layout[v].mElem, saved.mVars[v], plain))
                        {
                            throw Refusal{ "instance " + std::to_string(i + 1) + ", variable '"
                                + std::string(nameText(layout[v].mName))
                                + "': the value does not fit its declared type" };
                        }
                        instance.vars[v] = convert(saved.mVars[v]);
                    }
                }
                else
                {
                    changedScripts.insert(script.mClass);
                    // The script changed: a variable keeps its value when the new script has one of the same name and
                    // type kind.
                    std::vector<std::uint8_t> taken(savedScript.mVars.size(), 0);
                    for (std::size_t v = 0; v < layout.size(); ++v)
                    {
                        const std::string wanted = lowerCopy(nameText(layout[v].mName));
                        for (std::size_t s = 0; s < savedScript.mVars.size(); ++s)
                        {
                            if (taken[s] != 0 || savedScript.mVars[s].mKind != layout[v].mKind
                                || lowerCopy(texts[savedScript.mVars[s].mName]) != wanted)
                            {
                                continue;
                            }
                            if (fitsPlain(layout[v].mKind, layout[v].mElem, saved.mVars[s], plain))
                            {
                                instance.vars[v] = convert(saved.mVars[s]);
                                taken[s] = 1;
                            }
                            break;
                        }
                    }
                }
                newByKey.emplace(instance.key, static_cast<std::uint32_t>(newInstances.size() + 1));
                newInstances.push_back(std::move(instance));
            }

            // ---- stacks
            struct Built
            {
                Stack mStack;
                bool mParked = false;
                std::uint32_t mPool = 0;
            };
            std::vector<Built> built;
            built.reserve(plain.mStacks.size());
            std::uint32_t droppedStacks = 0;
            std::uint32_t droppedHostWaits = 0;
            std::uint32_t parkedKept = 0;
            std::uint32_t readyKept = 0;
            for (std::size_t n = 0; n < plain.mStacks.size(); ++n)
            {
                const PStack& saved = plain.mStacks[n];
                const std::string label = "stack " + std::to_string(n);
                Built item;
                Stack& stack = item.mStack;
                stack.instance = saved.mInstance;
                stack.eventName = nameId(texts[saved.mEvent]);
                stack.argCount = saved.mArgCount;
                stack.started = saved.mStarted;
                stack.seq = saved.mSeq;
                if (!saved.mParked)
                {
                    stack.values.reserve(saved.mValues.size());
                    for (const PVal& v : saved.mValues)
                    {
                        stack.values.push_back(convert(v));
                    }
                    item.mParked = false;
                    ++readyKept;
                    built.push_back(std::move(item));
                    continue;
                }
                // A parked stack runs the code of its script: it is dropped when that code is not what it was saved
                // against (the instance's own script, or any script of a frame).
                bool usable = !found[plain.mInstances[saved.mInstance - 1].mScript].mChanged;
                for (const PFrame& frame : saved.mFrames)
                {
                    usable = usable && found[frame.mScript].mIndex != kNoScript && !found[frame.mScript].mChanged;
                }
                if (!usable)
                {
                    ++droppedStacks;
                    droppedHostWaits += saved.mHostWait ? 1 : 0;
                    continue;
                }
                std::uint32_t base = 0;
                std::uint32_t previousSize = 0;
                stack.frames.reserve(saved.mFrames.size());
                for (std::size_t f = 0; f < saved.mFrames.size(); ++f)
                {
                    const PFrame& frame = saved.mFrames[f];
                    const Script& owner = mScripts[found[frame.mScript].mIndex];
                    const std::string where = label + ", frame " + std::to_string(f);
                    if (frame.mFunc >= owner.mFuncs.size() || owner.mFuncs[frame.mFunc].native)
                    {
                        throw Refusal{ where + ": function " + std::to_string(frame.mFunc) + " does not exist in "
                            + owner.mClass + " or is native" };
                    }
                    const CFunc& function = owner.mFuncs[frame.mFunc];
                    if (frame.mPc > function.code.size())
                    {
                        throw Refusal{ where + ": the position " + std::to_string(frame.mPc) + " is past the end of "
                            + owner.mClass + "." + std::string(nameText(function.name)) };
                    }
                    if (f == 0)
                    {
                        const std::uint32_t instanceScript
                            = found[plain.mInstances[saved.mInstance - 1].mScript].mIndex;
                        const std::vector<std::uint32_t>& chain = mScripts[instanceScript].mChain;
                        if (std::find(chain.begin(), chain.end(), found[frame.mScript].mIndex) == chain.end())
                        {
                            throw Refusal{ where + ": the first frame of the stack runs " + owner.mClass
                                + ", which is neither the script of instance " + std::to_string(saved.mInstance)
                                + " nor one of its parents" };
                        }
                        if (!function.global
                            && (frame.mSelf.mKind != static_cast<std::uint8_t>(Kind::Instance)
                                || frame.mSelf.mA != saved.mInstance))
                        {
                            throw Refusal{ where + ": self of the first frame is not the instance of the stack" };
                        }
                    }
                    Frame made;
                    made.script = found[frame.mScript].mIndex;
                    made.func = frame.mFunc;
                    made.pc = frame.mPc;
                    made.base = base;
                    made.self = convert(frame.mSelf);
                    if (made.self.kind != Kind::None && made.self.kind != Kind::Instance
                        && made.self.kind != Kind::Form)
                    {
                        throw Refusal{ where + ": self is not an object" };
                    }
                    const OpKind dest = static_cast<OpKind>(frame.mRetKind);
                    if (f == 0)
                    {
                        if (dest != OpKind::None || frame.mRetSlot != 0)
                        {
                            throw Refusal{ where + ": the first frame has nowhere to return to" };
                        }
                    }
                    else
                    {
                        const bool allowed = dest == OpKind::None || dest == OpKind::Local || dest == OpKind::Var
                            || dest == OpKind::StateVar || dest == OpKind::Discard;
                        if (!allowed || (dest == OpKind::Local && frame.mRetSlot >= previousSize))
                        {
                            throw Refusal{ where + ": the result destination is not a slot of the calling frame" };
                        }
                        made.retDest.kind = dest;
                        made.retDest.n = frame.mRetSlot;
                    }
                    base += function.frameSize;
                    previousSize = function.frameSize;
                    stack.frames.push_back(made);
                }
                if (base != saved.mValues.size())
                {
                    throw Refusal{ label + ": " + std::to_string(saved.mValues.size()) + " values do not match the "
                        + std::to_string(base) + " slots of its frames" };
                }
                stack.values.reserve(saved.mValues.size());
                for (std::size_t f = 0; f < stack.frames.size(); ++f)
                {
                    const CFunc& function = mScripts[stack.frames[f].script].mFuncs[stack.frames[f].func];
                    for (std::size_t slot = 0; slot < function.frameSize; ++slot)
                    {
                        const PVal& plainValue = saved.mValues[stack.frames[f].base + slot];
                        if (slot < function.slotTypes.size()
                            && !fitsPlain(static_cast<std::uint8_t>(function.slotTypes[slot].kind),
                                static_cast<std::uint8_t>(function.slotTypes[slot].elem), plainValue, plain))
                        {
                            throw Refusal{ label + ", frame " + std::to_string(f) + ", slot " + std::to_string(slot)
                                + ": the value does not fit the declared type" };
                        }
                        stack.values.push_back(convert(plainValue));
                    }
                }
                stack.wake = saved.mWake;
                stack.hostWait = saved.mHostWait;
                stack.parkToken = saved.mToken;
                item.mParked = true;
                item.mPool = saved.mPool;
                ++parkedKept;
                built.push_back(std::move(item));
            }

            // ---- room: the pool slots the parked stacks sit in (their tokens name them), the lists
            std::size_t poolSize = std::max<std::size_t>(mStackPool.size(), parkedKept + readyKept);
            for (const Built& item : built)
            {
                if (item.mParked)
                {
                    poolSize = std::max<std::size_t>(poolSize, static_cast<std::size_t>(item.mPool) + 1);
                }
            }
            mFreeStacks.reserve(poolSize);
            while (mStackPool.size() < poolSize)
            {
                auto fresh = std::make_unique<Stack>();
                fresh->poolIndex = static_cast<std::uint32_t>(mStackPool.size());
                mStackPool.push_back(std::move(fresh));
                mFreeStacks.push_back(mStackPool.back().get());
            }
            std::vector<std::uint8_t> used(poolSize, 0);
            for (const Built& item : built)
            {
                if (item.mParked)
                {
                    used[item.mPool] = 1;
                }
            }
            std::vector<std::uint32_t> slotOf(built.size(), 0);
            {
                std::size_t nextFree = 0;
                for (std::size_t i = 0; i < built.size(); ++i)
                {
                    if (built[i].mParked)
                    {
                        slotOf[i] = built[i].mPool;
                        continue;
                    }
                    while (used[nextFree] != 0)
                    {
                        ++nextFree;
                    }
                    used[nextFree] = 1;
                    slotOf[i] = static_cast<std::uint32_t>(nextFree);
                }
            }
            std::vector<Stack*> newReady;
            newReady.reserve(readyKept);
            mWaiting.reserve(parkedKept);

            // ---- the last things that can fail: the state names of the instances (permanent strings, past every
            // check above) and the text of the warnings. After this point nothing allocates a name or a string.
            for (std::size_t i = 0; i < newInstances.size(); ++i)
            {
                const std::string& stateText = texts[plain.mInstances[i].mState];
                newInstances[i].stateName = internString(stateText);
                newInstances[i].stateId = nameId(stateText);
            }
            for (const std::string& name : changedStructs)
            {
                warnings.push_back("restore: structs of the changed script '" + name + "' were not restored");
            }
            for (const std::string& name : changedScripts)
            {
                warnings.push_back("restore: the script '" + name
                    + "' has changed since the save: its variables were matched by name and type, and its parked "
                      "stacks were dropped");
            }
            if (droppedStacks > 0)
            {
                warnings.push_back("restore: " + std::to_string(droppedStacks)
                    + " parked stack(s) were dropped because their scripts changed or are not loaded");
            }

            // ---- switch: nothing below can fail
            mWaiting.clear();
            mDue.clear();
            mDueHead = 0;
            for (const std::unique_ptr<Stack>& slot : mStackPool)
            {
                slot->heapPos = Stack::kNotInHeap;
                slot->frames.clear();
                slot->values.clear();
                slot->result = Val{};
                slot->hostWait = false;
                slot->started = false;
                slot->argCount = 0;
                slot->instance = 0;
                slot->eventName = 0;
                slot->parkToken = 0;
            }
            mFreeStacks.clear();
            for (std::size_t i = poolSize; i-- > 0;)
            {
                if (used[i] == 0)
                {
                    mFreeStacks.push_back(mStackPool[i].get());
                }
            }
            for (std::size_t i = 0; i < built.size(); ++i)
            {
                Stack& slot = *mStackPool[slotOf[i]];
                Stack& from = built[i].mStack;
                slot.instance = from.instance;
                slot.eventName = from.eventName;
                slot.argCount = from.argCount;
                slot.started = from.started;
                slot.seq = from.seq;
                slot.frames = std::move(from.frames);
                slot.values = std::move(from.values);
                if (built[i].mParked)
                {
                    slot.wake = from.wake;
                    slot.hostWait = from.hostWait;
                    slot.parkToken = from.parkToken;
                    mWaiting.push_back(WaitEntry{ slot.wake, slot.seq, &slot });
                }
                else
                {
                    newReady.push_back(&slot);
                }
            }
            mReady.swap(newReady);
            mReadyHead = 0;
            rebuildWaiting();
            mSuspendedCount = parkedKept;
            mInstances.swap(newInstances);
            mByKey.swap(newByKey);
            mNow = plain.mNow;
            mNextSeq = plain.mNextSeq;
            // Never lower: a host may still hold a token made by a park before this restore (review 7, finding M3).
            mParkCounter = std::max(mParkCounter, plain.mParkCounter);
            guard.mCommitted = true;

            // The old state's strings, arrays and structs are garbage now.
            try
            {
                collect();
            }
            catch (...)
            {
            }

            result.droppedParkedStacks = droppedStacks;
            result.droppedHostWaits = droppedHostWaits;
            result.changedStructObjects = changedStructObjects;
            result.changedScripts = static_cast<std::uint32_t>(changedScripts.size());
            result.ok = true;
        }
        catch (const Refusal& refusal)
        {
            result = RestoreResult{};
            result.error.message = "restore refused: " + refusal.mMessage;
            return result;
        }

        for (const std::string& warning : warnings)
        {
            try
            {
                worldLog(Severity::Warning, warning);
            }
            catch (...)
            {
            }
        }
        return result;
    }
}
