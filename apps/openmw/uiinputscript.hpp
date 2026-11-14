#ifndef OPENMW_UIINPUTSCRIPT_H
#define OPENMW_UIINPUTSCRIPT_H

#include <cstdint>
#include <filesystem>
#include <string>
#include <string_view>
#include <vector>

struct SDL_Window;

namespace OMW
{
    /// A test aid (task 0111): `OPENEDEN_RMLUI_INPUT_SCRIPT=<file>` makes the engine push synthetic SDL events at
    /// chosen frames, so that the real input path (SDL event, InputWrapper, UiInputFilter, overlay) can be driven in a
    /// quiet-window game without a real mouse, keyboard or controller. Active only with `[GUI] rmlui` on. One command
    /// per line, `<frame> <command> <arguments>`; lines starting with # are comments. The frame counts frames of a
    /// running game (not loading screens or the main menu before a game):
    ///
    ///   mouse X Y | down L|R|M X Y | up L|R|M X Y | wheel DY
    ///   key NAME down|up [ctrl+shift+alt+gui] (SDL key name, for example Return, Escape, A; modifiers optional)
    ///   clip UTF8 (sets the system clipboard) | cliplog (writes it to the log)
    ///   text UTF8 | edit UTF8 START LENGTH
    ///   cbutton NAME down|up (SDL controller button name: a, b, back, x, dpup, ...) | caxis righty VALUE
    ///   resize WIDTH HEIGHT | scale FACTOR | shot | log TEXT
    ///
    /// `scale` and `shot` (a screenshot, as the F12 key) are done through the callbacks given to frame().
    class UiInputScript
    {
    public:
        struct Command
        {
            std::uint64_t mFrame = 0;
            std::string mName;
            std::vector<std::string> mArguments;
        };

        /// Parses `text`; a malformed line is skipped and named in `errors` (if given). Pure.
        static std::vector<Command> parse(std::string_view text, std::vector<std::string>* errors = nullptr);

        UiInputScript(SDL_Window* window, std::vector<Command> commands);

        /// Reads the file named by the environment variable, or returns an empty script when it is not set or
        /// unreadable.
        static std::vector<Command> fromEnvironment();

        bool empty() const { return mCommands.empty(); }

        /// Pushes the events of every command due at or before `current` that has not run. `setScale` receives the
        /// factor of a `scale` command, `shoot` is called for a `shot`.
        template <class SetScale, class Shoot>
        void frame(std::uint64_t current, SetScale&& setScale, Shoot&& shoot)
        {
            while (mNext < mCommands.size() && mCommands[mNext].mFrame <= current)
            {
                const Command& command = mCommands[mNext++];
                if (command.mName == "scale" && !command.mArguments.empty())
                    setScale(std::stof(command.mArguments[0]));
                else if (command.mName == "shot")
                    shoot();
                else
                    run(command);
            }
        }

    private:
        void run(const Command& command);

        SDL_Window* mWindow;
        std::vector<Command> mCommands;
        std::size_t mNext = 0;
    };
}

#endif
