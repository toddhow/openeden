#include "game_skyrimse/character_service.hpp"

#include <algorithm>
#include <cstring>

namespace game_skyrimse::character
{
    namespace
    {
        using openeden::sdk::BindingAccess;
        using openeden::sdk::BindingResult;
        using openeden::sdk::BindingStatus;
        using openeden::sdk::BindingType;
        using openeden::sdk::BindingValue;

        constexpr std::uint32_t kChunkVersion = 1;
        constexpr std::uint32_t kMaxBlobBytes = 1024;

        class Fn final : public openeden::sdk::BindingHandler
        {
        public:
            using Body = std::function<BindingResult(std::span<const BindingValue>)>;
            explicit Fn(Body body)
                : mBody(std::move(body))
            {
            }
            BindingResult call(std::span<const BindingValue> args) override { return mBody(args); }

        private:
            Body mBody;
        };

        class Chunk final : public openeden::sdk::SaveChunkHandler
        {
        public:
            explicit Chunk(CharacterService& service)
                : mService(service)
            {
            }
            std::string_view chunkName() const override { return "character"; }
            std::uint32_t chunkVersion() const override { return kChunkVersion; }
            void write(openeden::sdk::ChunkWriter& writer) override { mService.writeChunk(writer); }
            bool read(openeden::sdk::ChunkReader& reader, std::uint32_t version) override
            {
                return mService.readChunk(reader, version);
            }

        private:
            CharacterService& mService;
        };

        BindingResult okResult() { return BindingResult(); }

        BindingResult boolResult(bool v)
        {
            BindingResult r;
            r.value.type = BindingType::Bool;
            r.value.boolean = v;
            return r;
        }

        BindingResult intResult(std::int64_t v)
        {
            BindingResult r;
            r.value.type = BindingType::Int32;
            r.value.integer = static_cast<std::int32_t>(std::clamp<std::int64_t>(v, INT32_MIN, INT32_MAX));
            return r;
        }
    }

    struct CharacterService::Impl
    {
        std::vector<std::unique_ptr<Fn>> mHandlers;
        std::vector<openeden::sdk::FunctionDescriptor> mFunctions;
        std::unique_ptr<Chunk> mChunk;
        std::string mText; // the text of the last answer: a binding result views it until the next call
        std::uint64_t mBump = 0;
        bool mWarnedNoRaces = false;
    };

    CharacterService::CharacterService(Hooks hooks)
        : mImpl(std::make_unique<Impl>())
        , mHooks(std::move(hooks))
    {
        mImpl->mChunk = std::make_unique<Chunk>(*this);
    }

    CharacterService::~CharacterService() = default;

    openeden::sdk::SaveChunkHandler& CharacterService::chunk()
    {
        return *mImpl->mChunk;
    }

    const Choice& CharacterService::current() const
    {
        if (!mDone && mCreator != nullptr)
            return mCreator->choice();
        return mFinal;
    }

    std::uint64_t CharacterService::revision() const
    {
        return mImpl->mBump + (mCreator != nullptr ? mCreator->revision() : 0);
    }

    void CharacterService::changed()
    {
        if (mHooks.mApplyBody && mCreator != nullptr && !mDone)
            mHooks.mApplyBody(mCreator->choice());
    }

    bool CharacterService::poll()
    {
        if (mCreator != nullptr)
            return true;
        if (!mHooks.mAccess)
            return false;
        Access access;
        if (!mHooks.mAccess(access) || access.mIndex == nullptr)
            return false;
        auto creator = std::make_unique<Creator>(*access.mIndex, access.mText);
        if (!creator->ok())
        {
            if (!mImpl->mWarnedNoRaces && mHooks.mLog)
                mHooks.mLog("CHARACTER no playable race was found: the creation is off");
            mImpl->mWarnedNoRaces = true;
            return false;
        }
        mAccess = std::move(access);
        mCreator = std::move(creator);
        if (mHooks.mLog)
            mHooks.mLog("CHARACTER ready: " + std::to_string(mCreator->races().size()) + " playable races");
        if (mHavePending)
        {
            std::string why;
            if (mCreator->load(mPending, why))
            {
                mFinal = mCreator->choice();
                if (mHooks.mApplyBody)
                    mHooks.mApplyBody(mFinal);
                const RaceOption* race = mCreator->race();
                if (race != nullptr && mHooks.mRace)
                    mHooks.mRace(*race, false);
                if (mHooks.mLog)
                    mHooks.mLog("CHARACTER restored: " + mFinal.mName);
            }
            else
            {
                mDone = false;
                if (mHooks.mLog)
                    mHooks.mLog("CHARACTER the saved choice was refused: " + why);
                changed();
            }
            mHavePending = false;
        }
        else if (!mDone)
            changed();
        ++mImpl->mBump;
        return true;
    }

    bool CharacterService::chooseRace(std::size_t index)
    {
        if (!poll() || mDone)
            return false;
        if (!mCreator->selectRace(index))
            return false;
        changed();
        return true;
    }

    void CharacterService::chooseFemale(bool female)
    {
        if (!poll() || mDone)
            return;
        const std::uint64_t before = mCreator->revision();
        mCreator->setFemale(female);
        if (mCreator->revision() != before)
            changed();
    }

    void CharacterService::stepPart(Part part, int delta)
    {
        if (!poll() || mDone)
            return;
        const std::uint64_t before = mCreator->revision();
        mCreator->stepPart(part, delta);
        if (mCreator->revision() != before)
            changed();
    }

    void CharacterService::setName(std::string_view name)
    {
        if (!poll() || mDone)
            return;
        const std::uint64_t before = mCreator->revision();
        mCreator->setName(name);
        if (mCreator->revision() != before)
            changed();
    }

    bool CharacterService::finish()
    {
        if (!poll() || mDone)
            return false;
        if (mCreator->choice().mName.empty())
            mCreator->setName(kDefaultName);
        mFinal = mCreator->choice();
        std::string why;
        if (!validChoice(*mAccess.mIndex, mFinal, why))
        {
            if (mHooks.mLog)
                mHooks.mLog("CHARACTER finish refused: " + why);
            return false;
        }
        mDone = true;
        ++mImpl->mBump;
        if (mHooks.mApplyBody)
            mHooks.mApplyBody(mFinal);
        const RaceOption* race = mCreator->race();
        if (race != nullptr && mHooks.mRace)
            mHooks.mRace(*race, true);
        if (mHooks.mLog)
            mHooks.mLog("CHARACTER finished: " + mFinal.mName + ", race "
                + (race != nullptr ? race->mName : std::string()) + (mFinal.mFemale ? ", female" : ", male"));
        return true;
    }

    void CharacterService::writeChunk(openeden::sdk::ChunkWriter& writer)
    {
        const std::uint8_t done = mDone ? 1 : 0;
        writer.writeValue(done);
        if (!mDone)
            return;
        const std::vector<std::uint8_t> blob = encodeChoice(mFinal);
        writer.writeValue(static_cast<std::uint32_t>(blob.size()));
        writer.write(blob.data(), blob.size());
    }

    bool CharacterService::readChunk(openeden::sdk::ChunkReader& reader, std::uint32_t version)
    {
        if (version != kChunkVersion)
            return false;
        std::uint8_t done = 0;
        if (!reader.readValue(done) || done > 1)
            return false;
        if (done == 0)
        {
            mDone = false;
            mHavePending = false;
            return reader.remaining() == 0;
        }
        std::uint32_t length = 0;
        if (!reader.readValue(length) || length > kMaxBlobBytes || length > reader.remaining())
            return false;
        std::vector<std::uint8_t> blob(length);
        if (length != 0 && !reader.read(blob.data(), length))
            return false;
        Choice choice;
        std::string error;
        if (!decodeChoice(blob, choice, error))
        {
            if (mHooks.mLog)
                mHooks.mLog("CHARACTER the saved choice is damaged: " + error);
            return false;
        }
        mDone = true;
        mFinal = choice;
        mPending = choice;
        mHavePending = true;
        if (mHooks.mApplyBody)
            mHooks.mApplyBody(choice); // the body follows at once; the records are checked when they are read
        if (mCreator != nullptr)
        {
            // a load into a running session: the records are there already
            mCreator.reset();
            (void)poll();
        }
        return true;
    }

    openeden::sdk::DeclareResult CharacterService::declare(openeden::sdk::BindingsService& service)
    {
        Impl& m = *mImpl;
        CharacterService* self = this;
        static const BindingType noParams[] = { BindingType::Bool };
        static const BindingType intParam[] = { BindingType::Int32 };
        static const BindingType boolParam[] = { BindingType::Bool };
        static const BindingType strParam[] = { BindingType::String };
        static const BindingType strIntParams[] = { BindingType::String, BindingType::Int32 };
        (void)noParams;

        const auto add = [&](std::string_view name, std::span<const BindingType> params, bool hasResult,
                             BindingType result, BindingAccess access, Fn::Body body) {
            m.mHandlers.push_back(std::make_unique<Fn>(std::move(body)));
            openeden::sdk::FunctionDescriptor f;
            f.name = name;
            f.params = params;
            f.hasResult = hasResult;
            f.result = result;
            f.access = access;
            f.handler = m.mHandlers.back().get();
            m.mFunctions.push_back(f);
        };
        const auto textResult = [&m](std::string text) {
            m.mText = std::move(text);
            BindingResult r;
            r.value.type = BindingType::String;
            r.value.text = m.mText;
            return r;
        };
        const auto raceAt = [self](const BindingValue& v) -> const RaceOption* {
            if (!self->poll() || v.integer < 0)
                return nullptr;
            const auto& races = self->mCreator->races();
            return static_cast<std::size_t>(v.integer) < races.size() ? &races[static_cast<std::size_t>(v.integer)]
                                                                      : nullptr;
        };
        const auto partOf = [](const BindingValue& v, Part& part) { return partFromName(v.text, part); };
        constexpr BindingAccess kRead = BindingAccess::Read;
        constexpr BindingAccess kMutate = BindingAccess::Mutate;

        add("ready", {}, true, BindingType::Bool, kRead,
            [self](std::span<const BindingValue>) { return boolResult(self->poll()); });
        add("needed", {}, true, BindingType::Bool, kRead,
            [self](std::span<const BindingValue>) { return boolResult(self->poll() && !self->mDone); });
        add("revision", {}, true, BindingType::Int32, kRead, [self](std::span<const BindingValue>) {
            (void)self->poll();
            return intResult(static_cast<std::int64_t>(self->revision()));
        });
        add("race_count", {}, true, BindingType::Int32, kRead, [self](std::span<const BindingValue>) {
            return intResult(self->poll() ? static_cast<std::int64_t>(self->mCreator->races().size()) : 0);
        });
        add("race_name", intParam, true, BindingType::String, kRead,
            [raceAt, textResult](std::span<const BindingValue> a) {
                const RaceOption* race = raceAt(a[0]);
                return textResult(race != nullptr ? race->mName : std::string());
            });
        add("race_desc", intParam, true, BindingType::String, kRead,
            [raceAt, textResult](std::span<const BindingValue> a) {
                const RaceOption* race = raceAt(a[0]);
                return textResult(race != nullptr ? race->mDescription : std::string());
            });
        add("race_skills", intParam, true, BindingType::String, kRead,
            [raceAt, textResult](std::span<const BindingValue> a) {
                const RaceOption* race = raceAt(a[0]);
                return textResult(race != nullptr ? describeBoosts(race->mBoosts) : std::string());
            });
        add("race_abilities", intParam, true, BindingType::Int32, kRead, [raceAt](std::span<const BindingValue> a) {
            const RaceOption* race = raceAt(a[0]);
            return intResult(race != nullptr ? static_cast<std::int64_t>(race->mAbilities.size()) : 0);
        });
        add("race_index", {}, true, BindingType::Int32, kRead, [self](std::span<const BindingValue>) {
            return intResult(self->poll() ? static_cast<std::int64_t>(self->mCreator->raceIndex()) : 0);
        });
        add("female", {}, true, BindingType::Bool, kRead, [self](std::span<const BindingValue>) {
            return boolResult(self->poll() && self->current().mFemale);
        });
        add("name", {}, true, BindingType::String, kRead, [self, textResult](std::span<const BindingValue>) {
            return textResult(self->poll() ? self->current().mName : std::string());
        });
        add("part_count", strParam, true, BindingType::Int32, kRead, [self, partOf](std::span<const BindingValue> a) {
            Part part;
            if (!self->poll() || !partOf(a[0], part))
                return intResult(0);
            return intResult(static_cast<std::int64_t>(self->mCreator->options(part).size()));
        });
        add("part_name", strParam, true, BindingType::String, kRead,
            [self, partOf, textResult](std::span<const BindingValue> a) {
                Part part;
                if (!self->poll() || !partOf(a[0], part))
                    return textResult(std::string());
                const auto& options = self->mCreator->options(part);
                const std::size_t i = self->mCreator->partIndex(part);
                return textResult(i < options.size() ? options[i].mName : std::string());
            });
        add("part_index", strParam, true, BindingType::Int32, kRead, [self, partOf](std::span<const BindingValue> a) {
            Part part;
            if (!self->poll() || !partOf(a[0], part))
                return intResult(0);
            return intResult(static_cast<std::int64_t>(self->mCreator->partIndex(part)));
        });

        add("choose_race", intParam, false, BindingType::Bool, kMutate, [self](std::span<const BindingValue> a) {
            if (a[0].integer >= 0)
                (void)self->chooseRace(static_cast<std::size_t>(a[0].integer));
            return okResult();
        });
        add("choose_female", boolParam, false, BindingType::Bool, kMutate, [self](std::span<const BindingValue> a) {
            self->chooseFemale(a[0].boolean);
            return okResult();
        });
        add("step_part", strIntParams, false, BindingType::Bool, kMutate,
            [self, partOf](std::span<const BindingValue> a) {
                Part part;
                if (partOf(a[0], part))
                    self->stepPart(part, a[1].integer);
                return okResult();
            });
        add("set_name", strParam, false, BindingType::Bool, kMutate, [self](std::span<const BindingValue> a) {
            self->setName(a[0].text);
            return okResult();
        });
        add("finish", {}, false, BindingType::Bool, kMutate, [self](std::span<const BindingValue>) {
            (void)self->finish();
            return okResult();
        });

        openeden::sdk::NamespaceDescriptor ns;
        ns.name = kNamespace;
        ns.version = { 1, 0 };
        ns.functions = m.mFunctions;
        return service.declareNamespace(ns);
    }
}
