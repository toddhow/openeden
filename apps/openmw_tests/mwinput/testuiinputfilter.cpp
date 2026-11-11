// Task 0111: the filter between SDL's event dispatch and the input managers. With no overlay set it must be invisible:
// every call reaches the manager once, with the same arguments, and the wheel total is untouched ("off is unchanged"
// for the input path). With an overlay set, the GUI mode checks need the game's window manager and are covered by the
// in-game checks (tools/rmlui_input_check) instead.
#include "apps/openmw/mwinput/uiinputfilter.hpp"

#include <gtest/gtest.h>

#include <string>
#include <vector>

namespace
{
    using namespace MWInput;

    struct Calls
    {
        std::vector<std::string> mCalls;
        SDLUtil::MouseMotionEvent mLastMotion{};
        SDL_TextEditingEvent mLastEdit{};
    };

    class FakeMouse final : public SDLUtil::MouseListener
    {
    public:
        explicit FakeMouse(Calls& log) : mLog(log) {}
        void mouseMoved(const SDLUtil::MouseMotionEvent& arg) override
        {
            mLog.mCalls.push_back("moved");
            mLog.mLastMotion = arg;
        }
        void mousePressed(const SDL_MouseButtonEvent&, Uint8 id) override { mLog.mCalls.push_back("pressed" + std::to_string(id)); }
        void mouseReleased(const SDL_MouseButtonEvent&, Uint8 id) override { mLog.mCalls.push_back("released" + std::to_string(id)); }
        void mouseWheelMoved(const SDL_MouseWheelEvent& arg) override { mLog.mCalls.push_back("wheel" + std::to_string(arg.y)); }

    private:
        Calls& mLog;
    };

    class FakeKeys final : public SDLUtil::KeyListener
    {
    public:
        explicit FakeKeys(Calls& log) : mLog(log) {}
        void textInput(const SDL_TextInputEvent& arg) override { mLog.mCalls.push_back(std::string("text:") + arg.text); }
        void textEditing(const SDL_TextEditingEvent& arg) override
        {
            mLog.mCalls.push_back(std::string("edit:") + arg.text);
            mLog.mLastEdit = arg;
        }
        void keyPressed(const SDL_KeyboardEvent& arg) override { mLog.mCalls.push_back("keydown" + std::to_string(arg.keysym.scancode)); }
        void keyReleased(const SDL_KeyboardEvent& arg) override { mLog.mCalls.push_back("keyup" + std::to_string(arg.keysym.scancode)); }

    private:
        Calls& mLog;
    };

    class FakeController final : public SDLUtil::ControllerListener
    {
    public:
        explicit FakeController(Calls& log) : mLog(log) {}
        void buttonPressed(int device, const SDL_ControllerButtonEvent& evt) override
        {
            mLog.mCalls.push_back("cdown" + std::to_string(device) + ":" + std::to_string(evt.button));
        }
        void buttonReleased(int device, const SDL_ControllerButtonEvent& evt) override
        {
            mLog.mCalls.push_back("cup" + std::to_string(device) + ":" + std::to_string(evt.button));
        }
        void axisMoved(int device, const SDL_ControllerAxisEvent& arg) override
        {
            mLog.mCalls.push_back("axis" + std::to_string(device) + ":" + std::to_string(arg.axis) + ":" + std::to_string(arg.value));
        }
        void controllerAdded(int, const SDL_ControllerDeviceEvent&) override { mLog.mCalls.push_back("added"); }
        void controllerRemoved(const SDL_ControllerDeviceEvent&) override { mLog.mCalls.push_back("removed"); }
        void touchpadMoved(int, const SDLUtil::TouchEvent&) override { mLog.mCalls.push_back("tmoved"); }
        void touchpadPressed(int, const SDLUtil::TouchEvent&) override { mLog.mCalls.push_back("tpressed"); }
        void touchpadReleased(int, const SDLUtil::TouchEvent&) override { mLog.mCalls.push_back("treleased"); }

    private:
        Calls& mLog;
    };

    TEST(UiInputFilterOff, every_call_reaches_its_manager_once_with_the_same_arguments)
    {
        Calls log;
        FakeMouse mouse(log);
        FakeKeys keys(log);
        FakeController controller(log);
        UiInputFilter filter(mouse, keys, controller);
        ASSERT_EQ(filter.target(), nullptr);

        SDLUtil::MouseMotionEvent motion{};
        motion.type = SDL_MOUSEWHEEL;
        motion.x = 10;
        motion.y = 20;
        motion.z = 360;
        motion.zrel = 120;
        filter.mouseMoved(motion);
        EXPECT_EQ(log.mLastMotion.z, 360);
        EXPECT_EQ(log.mLastMotion.zrel, 120);
        EXPECT_EQ(log.mLastMotion.x, 10);
        EXPECT_EQ(log.mLastMotion.y, 20);

        SDL_MouseButtonEvent button{};
        filter.mousePressed(button, SDL_BUTTON_LEFT);
        filter.mouseReleased(button, SDL_BUTTON_LEFT);
        SDL_MouseWheelEvent wheel{};
        wheel.y = 1;
        filter.mouseWheelMoved(wheel);

        SDL_TextInputEvent text{};
        SDL_strlcpy(text.text, "a", sizeof(text.text));
        filter.textInput(text);
        SDL_TextEditingEvent edit{};
        SDL_strlcpy(edit.text, "ni", sizeof(edit.text));
        edit.start = 1;
        edit.length = 1;
        filter.textEditing(edit);
        EXPECT_EQ(log.mLastEdit.start, 1);
        EXPECT_EQ(log.mLastEdit.length, 1);

        SDL_KeyboardEvent key{};
        key.keysym.scancode = SDL_SCANCODE_W;
        filter.keyPressed(key);
        filter.keyReleased(key);

        SDL_ControllerButtonEvent cbutton{};
        cbutton.button = SDL_CONTROLLER_BUTTON_A;
        filter.buttonPressed(1, cbutton);
        filter.buttonReleased(1, cbutton);
        SDL_ControllerAxisEvent axis{};
        axis.axis = SDL_CONTROLLER_AXIS_RIGHTY;
        axis.value = 1234;
        filter.axisMoved(1, axis);
        SDL_ControllerDeviceEvent device{};
        filter.controllerAdded(1, device);
        filter.controllerRemoved(device);

        const std::vector<std::string> expected = { "moved", "pressed1", "released1", "wheel1", "text:a", "edit:ni",
            "keydown" + std::to_string(SDL_SCANCODE_W), "keyup" + std::to_string(SDL_SCANCODE_W),
            "cdown1:" + std::to_string(SDL_CONTROLLER_BUTTON_A), "cup1:" + std::to_string(SDL_CONTROLLER_BUTTON_A),
            "axis1:" + std::to_string(SDL_CONTROLLER_AXIS_RIGHTY) + ":1234", "added", "removed" };
        EXPECT_EQ(log.mCalls, expected);
    }

    TEST(UiInputFilterOff, setting_the_target_to_null_again_keeps_it_off)
    {
        Calls log;
        FakeMouse mouse(log);
        FakeKeys keys(log);
        FakeController controller(log);
        UiInputFilter filter(mouse, keys, controller);
        filter.setTarget(nullptr);
        SDL_KeyboardEvent key{};
        key.keysym.scancode = SDL_SCANCODE_A;
        filter.keyPressed(key);
        EXPECT_EQ(log.mCalls.size(), 1u);
    }
}
