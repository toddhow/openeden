#include "uiinputscript.hpp"

#include <algorithm>
#include <cstdlib>
#include <stdexcept>
#include <fstream>
#include <sstream>

#include <SDL.h>

#include <components/debug/debuglog.hpp>
#include <components/settings/values.hpp>

namespace OMW
{
    namespace
    {
        Uint8 buttonNumber(const std::string& name)
        {
            if (name == "R")
                return SDL_BUTTON_RIGHT;
            if (name == "M")
                return SDL_BUTTON_MIDDLE;
            return SDL_BUTTON_LEFT;
        }

        int controllerButton(const std::string& name)
        {
            if (name == "a")
                return SDL_CONTROLLER_BUTTON_A;
            if (name == "b")
                return SDL_CONTROLLER_BUTTON_B;
            if (name == "dpup")
                return SDL_CONTROLLER_BUTTON_DPAD_UP;
            if (name == "dpdown")
                return SDL_CONTROLLER_BUTTON_DPAD_DOWN;
            if (name == "dpleft")
                return SDL_CONTROLLER_BUTTON_DPAD_LEFT;
            if (name == "dpright")
                return SDL_CONTROLLER_BUTTON_DPAD_RIGHT;
            return SDL_GameControllerGetButtonFromString(name.c_str());
        }

        // "ctrl", "shift", "alt", "gui" joined by + (empty is none): the modifier bits of a pushed key event
        Uint16 modifierBits(const std::string& names)
        {
            Uint16 bits = KMOD_NONE;
            std::istringstream words(names);
            std::string word;
            while (std::getline(words, word, '+'))
            {
                if (word == "ctrl")
                    bits |= KMOD_LCTRL;
                else if (word == "shift")
                    bits |= KMOD_LSHIFT;
                else if (word == "alt")
                    bits |= KMOD_LALT;
                else if (word == "gui")
                    bits |= KMOD_LGUI;
            }
            return bits;
        }

        const std::string& arg(const std::vector<std::string>& arguments, std::size_t index)
        {
            static const std::string empty;
            return index < arguments.size() ? arguments[index] : empty;
        }
    }

    std::vector<UiInputScript::Command> UiInputScript::parse(std::string_view text, std::vector<std::string>* errors)
    {
        std::vector<Command> commands;
        std::istringstream stream{ std::string(text) };
        std::string line;
        int lineNumber = 0;
        while (std::getline(stream, line))
        {
            ++lineNumber;
            if (!line.empty() && line.back() == '\r')
                line.pop_back();
            if (line.empty() || line[0] == '#')
                continue;
            std::istringstream words(line);
            Command command;
            std::string frame;
            if (!(words >> frame >> command.mName))
            {
                if (errors)
                    errors->push_back("line " + std::to_string(lineNumber) + ": expected '<frame> <command>'");
                continue;
            }
            try
            {
                std::size_t used = 0;
                command.mFrame = std::stoull(frame, &used);
                if (used != frame.size())
                    throw std::invalid_argument("frame");
            }
            catch (const std::exception&)
            {
                if (errors)
                    errors->push_back("line " + std::to_string(lineNumber) + ": '" + frame + "' is not a frame number");
                continue;
            }
            std::string word;
            if (command.mName == "text" || command.mName == "log" || command.mName == "clip")
            {
                // The rest of the line, one space after the command word removed.
                std::string rest;
                std::getline(words, rest);
                if (!rest.empty() && rest[0] == ' ')
                    rest.erase(0, 1);
                command.mArguments.push_back(rest);
            }
            else if (command.mName == "edit")
            {
                // edit TEXT START LENGTH: the text is everything before the last two words.
                std::vector<std::string> all;
                while (words >> word)
                    all.push_back(word);
                if (all.size() < 2)
                {
                    if (errors)
                        errors->push_back("line " + std::to_string(lineNumber) + ": edit needs TEXT START LENGTH");
                    continue;
                }
                std::string joined;
                for (std::size_t i = 0; i + 2 < all.size(); ++i)
                    joined += (i ? " " : "") + all[i];
                command.mArguments = { joined, all[all.size() - 2], all[all.size() - 1] };
            }
            else
            {
                while (words >> word)
                    command.mArguments.push_back(word);
            }
            commands.push_back(std::move(command));
        }
        std::stable_sort(commands.begin(), commands.end(),
            [](const Command& a, const Command& b) { return a.mFrame < b.mFrame; });
        return commands;
    }

    std::vector<UiInputScript::Command> UiInputScript::fromEnvironment()
    {
        const char* path = std::getenv("OPENEDEN_RMLUI_INPUT_SCRIPT");
        if (path == nullptr || *path == 0)
            return {};
        std::ifstream file{ std::filesystem::path(path) };
        if (!file)
        {
            Log(Debug::Warning) << "[UI] OPENEDEN_RMLUI_INPUT_SCRIPT: cannot read " << path;
            return {};
        }
        std::stringstream buffer;
        buffer << file.rdbuf();
        std::vector<std::string> errors;
        auto commands = parse(buffer.str(), &errors);
        for (const std::string& error : errors)
            Log(Debug::Warning) << "[UI] OPENEDEN_RMLUI_INPUT_SCRIPT: " << error;
        Log(Debug::Info) << "[UI] input script: " << commands.size() << " commands from " << path;
        return commands;
    }

    UiInputScript::UiInputScript(SDL_Window* window, std::vector<Command> commands)
        : mWindow(window)
        , mCommands(std::move(commands))
    {
    }

    void UiInputScript::run(const Command& command)
    {
        const auto& a = command.mArguments;
        SDL_Event event{};
        const Uint32 window = mWindow != nullptr ? SDL_GetWindowID(mWindow) : 0;
        const std::string& name = command.mName;
        if (name == "mouse")
        {
            event.type = SDL_MOUSEMOTION;
            event.motion.windowID = window;
            event.motion.x = std::atoi(arg(a, 0).c_str());
            event.motion.y = std::atoi(arg(a, 1).c_str());
            SDL_PushEvent(&event);
        }
        else if (name == "down" || name == "up")
        {
            const bool down = name == "down";
            event.type = down ? SDL_MOUSEBUTTONDOWN : SDL_MOUSEBUTTONUP;
            event.button.windowID = window;
            event.button.button = buttonNumber(arg(a, 0));
            event.button.state = down ? SDL_PRESSED : SDL_RELEASED;
            event.button.clicks = 1;
            event.button.x = std::atoi(arg(a, 1).c_str());
            event.button.y = std::atoi(arg(a, 2).c_str());
            SDL_PushEvent(&event);
        }
        else if (name == "wheel")
        {
            event.type = SDL_MOUSEWHEEL;
            event.wheel.windowID = window;
            event.wheel.y = std::atoi(arg(a, 0).c_str());
            event.wheel.direction = SDL_MOUSEWHEEL_NORMAL;
            SDL_PushEvent(&event);
        }
        else if (name == "key")
        {
            const SDL_Keycode key = SDL_GetKeyFromName(arg(a, 0).c_str());
            if (key == SDLK_UNKNOWN)
            {
                Log(Debug::Warning) << "[UI] input script: unknown key " << arg(a, 0);
                return;
            }
            const bool down = arg(a, 1) != "up";
            event.type = down ? SDL_KEYDOWN : SDL_KEYUP;
            event.key.windowID = window;
            event.key.state = down ? SDL_PRESSED : SDL_RELEASED;
            event.key.keysym.sym = key;
            event.key.keysym.scancode = SDL_GetScancodeFromKey(key);
            event.key.keysym.mod = modifierBits(arg(a, 2));
            SDL_PushEvent(&event);
        }
        else if (name == "clip")
        {
            // the system clipboard, as another program would have left it
            SDL_SetClipboardText(arg(a, 0).c_str());
        }
        else if (name == "cliplog")
        {
            char* text = SDL_GetClipboardText();
            Log(Debug::Info) << "[UI] input script: clipboard = \"" << (text != nullptr ? text : "") << "\"";
            if (text != nullptr)
                SDL_free(text);
        }
        else if (name == "text")
        {
            event.type = SDL_TEXTINPUT;
            event.text.windowID = window;
            SDL_strlcpy(event.text.text, arg(a, 0).c_str(), sizeof(event.text.text));
            SDL_PushEvent(&event);
        }
        else if (name == "edit")
        {
            event.type = SDL_TEXTEDITING;
            event.edit.windowID = window;
            SDL_strlcpy(event.edit.text, arg(a, 0).c_str(), sizeof(event.edit.text));
            event.edit.start = std::atoi(arg(a, 1).c_str());
            event.edit.length = std::atoi(arg(a, 2).c_str());
            SDL_PushEvent(&event);
        }
        else if (name == "cbutton")
        {
            const int button = controllerButton(arg(a, 0));
            if (button == SDL_CONTROLLER_BUTTON_INVALID)
            {
                Log(Debug::Warning) << "[UI] input script: unknown controller button " << arg(a, 0);
                return;
            }
            const bool down = arg(a, 1) != "up";
            event.type = down ? SDL_CONTROLLERBUTTONDOWN : SDL_CONTROLLERBUTTONUP;
            event.cbutton.button = static_cast<Uint8>(button);
            event.cbutton.state = down ? SDL_PRESSED : SDL_RELEASED;
            SDL_PushEvent(&event);
        }
        else if (name == "caxis")
        {
            event.type = SDL_CONTROLLERAXISMOTION;
            event.caxis.axis = SDL_CONTROLLER_AXIS_RIGHTY;
            event.caxis.value = static_cast<Sint16>(std::atoi(arg(a, 1).c_str()));
            SDL_PushEvent(&event);
        }
        else if (name == "resize")
        {
            if (mWindow != nullptr)
                SDL_SetWindowSize(mWindow, std::atoi(arg(a, 0).c_str()), std::atoi(arg(a, 1).c_str()));
        }
        else if (name == "theme")
        {
            // Task 0163: sets the settings the engine polls, as an edit of settings.cfg would: THEME [VARIANTS].
            Settings::gui().mRmluiTheme.set(arg(a, 0));
            Settings::gui().mRmluiThemeVariants.set(a.size() > 1 ? a[1] : std::string());
        }
        else if (name == "log")
            Log(Debug::Info) << "[UI] input script: " << arg(a, 0);
        else
            Log(Debug::Warning) << "[UI] input script: unknown command " << name;
    }
}
