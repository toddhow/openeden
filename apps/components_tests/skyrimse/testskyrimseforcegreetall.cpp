// Task 0273b (review finding 3): the forced greeting publishes EVERY waiting request, each with its own reference.
//
// In plain words: the global script used to be told one base (the first waiting request). If that actor was out of
// range, every other request waited behind it, and the script activated the nearest object of that BASE, which is not
// necessarily the actor whose package asked. Now the pack also lists all waiting requests as
// "reference:base:serial" (separated by ';') and the script serves each one it can reach, by reference.
// `pending_base` and `pending_serial` keep their meaning for the first request.
#include <game_skyrimse/forcegreet.hpp>

#include <gtest/gtest.h>

#include <map>
#include <string>

namespace
{
    using namespace game_skyrimse;

    constexpr FormId kTopic = 0x3E50;

    class Stub final : public openeden::sdk::BindingsService
    {
    public:
        openeden::sdk::DeclareResult declareNamespace(const openeden::sdk::NamespaceDescriptor& ns) override
        {
            for (const auto& f : ns.functions)
                mFunctions[std::string(f.name)] = f;
            openeden::sdk::DeclareResult r;
            r.ok = true;
            return r;
        }
        std::map<std::string, openeden::sdk::FunctionDescriptor> mFunctions;
    };

    TEST(ForceGreetAll, everyWaitingRequestIsListedInReferenceOrder)
    {
        ForceGreet fg;
        ASSERT_TRUE(fg.request(0x200, 0x20, kTopic));
        ASSERT_TRUE(fg.request(0x100, 0x10, kTopic));
        const auto all = fg.pendingRequests();
        ASSERT_EQ(all.size(), 2u);
        EXPECT_EQ(all[0].mReference, 0x100u);
        EXPECT_EQ(all[0].mBase, 0x10u);
        EXPECT_EQ(all[1].mReference, 0x200u);
        EXPECT_NE(all[0].mSerial, all[1].mSerial) << "each request has a serial of its own";
        EXPECT_EQ(all[1].mSerial, 1u) << "the first request made";
        EXPECT_EQ(all[0].mSerial, 2u);
    }

    TEST(ForceGreetAll, anOpenOrFinishedRequestIsNotListedAndOthersStay)
    {
        ForceGreet fg;
        ASSERT_TRUE(fg.request(0x100, 0x10, kTopic));
        ASSERT_TRUE(fg.request(0x200, 0x20, kTopic));
        fg.began(0x100);
        const auto all = fg.pendingRequests();
        ASSERT_EQ(all.size(), 1u);
        EXPECT_EQ(all[0].mReference, 0x200u) << "an open request does not hold the others back";
    }

    TEST(ForceGreetAll, theFirstRequestStillAnswersTheOldFunctions)
    {
        ForceGreet fg;
        ASSERT_TRUE(fg.request(0x100, 0x10, kTopic));
        EXPECT_EQ(fg.pendingBase(), "16");
        EXPECT_EQ(fg.serial(), 1u);
    }

    TEST(ForceGreetAll, theBindingListsReferenceBaseAndSerialOfEveryWaitingRequest)
    {
        ForceGreet fg;
        Stub stub;
        ASSERT_TRUE(fg.declare(stub).ok);
        ASSERT_EQ(stub.mFunctions.count("pending_requests"), 1u);
        const auto text = [&] { return std::string(stub.mFunctions["pending_requests"].handler->call({}).value.text); };
        EXPECT_EQ(text(), "");
        ASSERT_TRUE(fg.request(0x100, 0x10, kTopic));
        ASSERT_TRUE(fg.request(0x200, 0x20, kTopic));
        EXPECT_EQ(text(), "256:16:1;512:32:2");
        EXPECT_EQ(stub.mFunctions["pending_requests"].access, openeden::sdk::BindingAccess::Read);
    }
}
