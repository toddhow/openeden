#ifndef OPENMW_COMPONENTS_SDLUTIL_SDLINPUTWRAPPER_H
#define OPENMW_COMPONENTS_SDLUTIL_SDLINPUTWRAPPER_H

#include <SDL_events.h>
#include <SDL_version.h>

#include "events.hpp"

namespace SDLUtil
{
    /// \brief A wrapper around SDL's event queue, mostly used for handling input-related events.
    class InputWrapper
    {
    public:
        InputWrapper(SDL_Window* window, void* viewer, bool grab);
        ~InputWrapper();

        void setMouseEventCallback(MouseListener* listen) { mMouseListener = listen; }
        void setSensorEventCallback(SensorListener* listen) { mSensorListener = listen; }
        void setKeyboardEventCallback(KeyListener* listen) { mKeyboardListener = listen; }
        void setWindowEventCallback(WindowListener* listen) { mWindowListener = listen; }
        void setControllerEventCallback(ControllerListener* listen) { mConListener = listen; }

        void capture(bool windowEventsOnly);
        bool isModifierHeld(int mod);
        bool isKeyDown(SDL_Scancode key);

        void setMouseVisible(bool visible);
        void setMouseRelative(bool relative);
        bool getMouseRelative() { return mMouseRelative; }
        void setGrabPointer(bool grab);

        void warpMouse(int x, int y);

        void updateMouseSettings();

    private:
        void handleWindowEvent(const SDL_Event& evt);

        bool _handleWarpMotion(const SDL_MouseMotionEvent& evt);
        void _wrapMousePointer(const SDL_MouseMotionEvent& evt);
        MouseMotionEvent _packageMouseMotion(const SDL_Event& evt);
        void _setWindowScale();

        SDL_Window* mSDLWindow;
        // Was the viewer's osg::ref_ptr. apps/openmw/mwinput/mousemanager.cpp still reads this class's layout, while
        // the object it holds is components/sdlutil/inputwrapper.hpp's, so getMouseRelative() there reads the wrong
        // byte. Dropping this member would change that behaviour; the fix is to include inputwrapper.hpp there, as
        // toddhow's decision (task 0013).
        void* mViewer;

        MouseListener* mMouseListener;
        SensorListener* mSensorListener;
        KeyListener* mKeyboardListener;
        WindowListener* mWindowListener;
        ControllerListener* mConListener;

        Uint16 mWarpX;
        Uint16 mWarpY;
        bool mWarpCompensate;
        bool mWrapPointer;

        bool mAllowGrab;
        bool mWantMouseVisible;
        bool mWantGrab;
        bool mWantRelative;
        bool mGrabPointer;
        bool mMouseRelative;

        bool mFirstMouseMove;

        Sint32 mMouseZ;
        Sint32 mMouseX;
        Sint32 mMouseY;

        bool mWindowHasFocus;
        bool mMouseInWindow;

        Uint16 mScaleX;
        Uint16 mScaleY;
    };

}

#endif
