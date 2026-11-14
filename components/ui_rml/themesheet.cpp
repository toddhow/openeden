#include "themesheet.hpp"

#include <algorithm>
#include <cctype>

namespace UiRml
{
    namespace
    {
        std::string lower(std::string_view text)
        {
            std::string result(text);
            for (char& c : result)
                c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
            return result;
        }

        bool isSpace(char c)
        {
            return c == ' ' || c == '\t' || c == '\r' || c == '\n' || c == '\f';
        }

        bool isNameChar(char c)
        {
            return std::isalnum(static_cast<unsigned char>(c)) != 0 || c == '-' || c == '_';
        }

        // The text with comments and the insides of quoted strings blanked out (same length, newlines kept), so that
        // nothing in them is mistaken for a declaration.
        std::string blankOut(std::string_view text)
        {
            std::string clean(text);
            std::size_t i = 0;
            while (i < clean.size())
            {
                if (clean[i] == '/' && i + 1 < clean.size() && clean[i + 1] == '*')
                {
                    std::size_t end = clean.find("*/", i + 2);
                    end = end == std::string::npos ? clean.size() : end + 2;
                    for (std::size_t k = i; k < end; ++k)
                        if (clean[k] != '\n')
                            clean[k] = ' ';
                    i = end;
                }
                else if (clean[i] == '"' || clean[i] == '\'')
                {
                    const char quote = clean[i];
                    std::size_t k = i + 1;
                    while (k < clean.size() && clean[k] != quote && clean[k] != '\n')
                    {
                        if (clean[k] == '\\' && k + 1 < clean.size())
                            clean[k++] = ' ';
                        clean[k++] = ' ';
                    }
                    i = k < clean.size() && clean[k] == quote ? k + 1 : k;
                }
                else
                    ++i;
            }
            return clean;
        }

        struct Statement
        {
            enum class Kind
            {
                Prelude, // text before a `{`
                Declaration, // `name: value` inside a block
                BlockEnd,
            };
            Kind mKind = Kind::Prelude;
            std::string mText;
            int mLine = 0;
            int mColumn = 0;
        };

        // Splits blanked-out RCSS into preludes, declarations and block ends, with the position of the first
        // character of each. Statements outside any block that end in `;` (at-rules such as `@import`) are ignored.
        std::vector<Statement> statements(const std::string& clean, int firstLine, int firstColumn)
        {
            std::vector<Statement> result;
            int depth = 0;
            int line = firstLine;
            int column = firstColumn;
            std::string current;
            int startLine = 0;
            int startColumn = 0;
            auto flush = [&](Statement::Kind kind) {
                if (!current.empty())
                {
                    while (!current.empty() && isSpace(current.back()))
                        current.pop_back();
                }
                if (!current.empty())
                    result.push_back({ kind, current, startLine, startColumn });
                current.clear();
            };
            for (const char c : clean)
            {
                if (c == '{')
                {
                    flush(Statement::Kind::Prelude);
                    ++depth;
                }
                else if (c == '}')
                {
                    if (depth > 0)
                        flush(Statement::Kind::Declaration);
                    else
                        current.clear();
                    if (depth > 0)
                    {
                        --depth;
                        result.push_back({ Statement::Kind::BlockEnd, {}, line, column });
                    }
                }
                else if (c == ';')
                {
                    if (depth > 0)
                        flush(Statement::Kind::Declaration);
                    else
                        current.clear();
                }
                else if (!(current.empty() && isSpace(c)))
                {
                    if (current.empty())
                    {
                        startLine = line;
                        startColumn = column;
                    }
                    current.push_back(c);
                }
                if (c == '\n')
                {
                    ++line;
                    column = 1;
                }
                else
                    ++column;
            }
            return result;
        }

        bool splitDeclaration(const std::string& text, std::string& name, std::string& value)
        {
            const std::size_t colon = text.find(':');
            if (colon == std::string::npos)
                return false;
            name = lower(text.substr(0, colon));
            while (!name.empty() && isSpace(name.back()))
                name.pop_back();
            value = lower(text.substr(colon + 1));
            return !name.empty();
        }

        bool isRadiusName(const std::string& name)
        {
            return name.starts_with("border-") && name.ends_with("-radius");
        }

        bool isOverflowName(const std::string& name)
        {
            return name == "overflow" || name == "overflow-x" || name == "overflow-y";
        }

        std::string firstWord(const std::string& value)
        {
            std::size_t begin = 0;
            while (begin < value.size() && isSpace(value[begin]))
                ++begin;
            std::size_t end = begin;
            while (end < value.size() && !isSpace(value[end]) && value[end] != '!')
                ++end;
            return value.substr(begin, end - begin);
        }

        const char* forbiddenFunction(const std::string& text)
        {
            for (const char* needle :
                { "linear-gradient(", "radial-gradient(", "conic-gradient(", "shader(", "blur(", "drop-shadow(" })
                if (text.find(needle) != std::string::npos)
                    return needle;
            return nullptr;
        }

        std::vector<StyleDiagnostic> validate(
            std::string_view text, std::string_view fileName, int firstLine, int firstColumn)
        {
            std::vector<StyleDiagnostic> problems;
            auto report = [&](const Statement& at, std::string message) {
                problems.push_back({ std::string(fileName), at.mLine, at.mColumn, std::move(message) });
            };
            struct Block
            {
                bool mHasRadius = false;
                bool mClips = false;
                Statement mRadius;
            };
            std::vector<Block> blocks;
            for (const Statement& s : statements(blankOut(text), firstLine, firstColumn))
            {
                switch (s.mKind)
                {
                    case Statement::Kind::Prelude:
                    {
                        const std::string prelude = lower(s.mText);
                        if (prelude.find("linear-gradient") != std::string::npos
                            || prelude.find("radial-gradient") != std::string::npos
                            || prelude.find("conic-gradient") != std::string::npos
                            || (prelude.starts_with("@decorator") && prelude.find("shader") != std::string::npos))
                            report(s,
                                "gradients and shaders are not drawn by the renderer (no layers or shaders; decision "
                                "0015)");
                        blocks.emplace_back();
                        break;
                    }
                    case Statement::Kind::BlockEnd:
                        if (!blocks.empty())
                        {
                            const Block& block = blocks.back();
                            if (block.mHasRadius && block.mClips)
                                report(block.mRadius,
                                    "a rounded corner with a clipping overflow needs a clip mask, which the renderer "
                                    "lacks");
                            blocks.pop_back();
                        }
                        break;
                    case Statement::Kind::Declaration:
                    {
                        std::string name;
                        std::string value;
                        if (!splitDeclaration(s.mText, name, value))
                            break;
                        if (name == "box-shadow" || name == "filter" || name == "backdrop-filter"
                            || name == "mask-image")
                        {
                            report(s,
                                "'" + name
                                    + "' needs layers, filters or masks, which the renderer lacks (decision 0015)");
                            break;
                        }
                        if (const char* function = forbiddenFunction(value))
                        {
                            report(s,
                                "'" + std::string(function) + ")' in '" + name
                                    + "' needs gradients, shaders or filters, which the renderer lacks (decision "
                                      "0015)");
                            break;
                        }
                        if (!blocks.empty())
                        {
                            if (isRadiusName(name) && !blocks.back().mHasRadius)
                            {
                                blocks.back().mHasRadius = true;
                                blocks.back().mRadius = s;
                            }
                            else if (isOverflowName(name))
                            {
                                const std::string word = firstWord(value);
                                if (word == "hidden" || word == "scroll" || word == "auto")
                                    blocks.back().mClips = true;
                            }
                        }
                        break;
                    }
                }
            }
            return problems;
        }

        std::vector<std::string> pushUnique(std::vector<std::string> list, const std::string& value)
        {
            if (std::find(list.begin(), list.end(), value) == list.end())
                list.push_back(value);
            return list;
        }
    }

    std::string StyleDiagnostic::text() const
    {
        if (mLine <= 0)
            return mFile + ": " + mMessage;
        return mFile + ":" + std::to_string(mLine) + ":" + std::to_string(mColumn) + ": " + mMessage;
    }

    std::vector<StyleDiagnostic> validateStyleSheet(std::string_view text, std::string_view fileName, int firstLine)
    {
        return validate(text, fileName, firstLine, 1);
    }

    std::vector<StyleDiagnostic> validateMarkupStyles(std::string_view rml, std::string_view fileName)
    {
        std::vector<StyleDiagnostic> problems;
        const std::string lowered = lower(rml);
        std::size_t at = 0;
        while ((at = lowered.find("<style", at)) != std::string::npos)
        {
            const std::size_t open = lowered.find('>', at);
            if (open == std::string::npos)
                break;
            const std::size_t close = lowered.find("</style", open);
            const std::size_t end = close == std::string::npos ? lowered.size() : close;
            int line = 1;
            int column = 1;
            for (std::size_t i = 0; i <= open; ++i)
            {
                if (rml[i] == '\n')
                {
                    ++line;
                    column = 1;
                }
                else
                    ++column;
            }
            const auto found = validate(rml.substr(open + 1, end - open - 1), fileName, line, column);
            problems.insert(problems.end(), found.begin(), found.end());
            at = end;
        }
        return problems;
    }

    std::vector<std::string> tokensDefined(std::string_view text)
    {
        std::vector<std::string> result;
        for (const Statement& s : statements(blankOut(text), 1, 1))
        {
            std::string name;
            std::string value;
            if (s.mKind == Statement::Kind::Declaration && splitDeclaration(s.mText, name, value)
                && name.starts_with("--"))
                result = pushUnique(std::move(result), name);
        }
        return result;
    }

    std::vector<std::string> tokensUsed(std::string_view text)
    {
        std::vector<std::string> result;
        const std::string clean = lower(blankOut(text));
        std::size_t at = 0;
        while ((at = clean.find("var(", at)) != std::string::npos)
        {
            at += 4;
            while (at < clean.size() && isSpace(clean[at]))
                ++at;
            std::size_t end = at;
            while (end < clean.size() && isNameChar(clean[end]))
                ++end;
            if (end > at + 2 && clean.compare(at, 2, "--") == 0)
                result = pushUnique(std::move(result), clean.substr(at, end - at));
            at = end;
        }
        return result;
    }

    const std::vector<std::string>& requiredTokens()
    {
        static const std::vector<std::string> sTokens = { "--font-family", "--font-size-sm", "--font-size-md",
            "--font-size-lg", "--space-1", "--space-2", "--space-3", "--space-4", "--space-5", "--space-6", "--space-7",
            "--space-8", "--radius-sm", "--radius-md", "--border-width", "--control-height", "--scrollbar-width",
            "--color-surface", "--color-surface-sunken", "--color-text", "--color-text-strong", "--color-text-muted",
            "--color-text-ok", "--color-text-info", "--color-control", "--color-control-hover", "--color-control-focus",
            "--color-control-text-focus", "--color-input", "--color-input-focus", "--color-input-text",
            "--color-border", "--color-focus-border", "--color-scrollbar-track", "--color-scrollbar-thumb" };
        return sTokens;
    }

    bool isValidThemeName(std::string_view name)
    {
        if (name.empty() || name.size() > 64)
            return false;
        return std::all_of(name.begin(), name.end(),
            [](char c) { return (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '-' || c == '_'; });
    }

    std::vector<std::string> parseNameList(std::string_view list)
    {
        std::vector<std::string> result;
        std::size_t begin = 0;
        while (begin <= list.size())
        {
            std::size_t end = list.find(',', begin);
            if (end == std::string_view::npos)
                end = list.size();
            std::string_view item = list.substr(begin, end - begin);
            while (!item.empty() && isSpace(item.front()))
                item.remove_prefix(1);
            while (!item.empty() && isSpace(item.back()))
                item.remove_suffix(1);
            if (!item.empty())
                result = pushUnique(std::move(result), std::string(item));
            begin = end + 1;
        }
        return result;
    }

    std::string themePath(std::string_view name)
    {
        return "ui/themes/" + std::string(name) + "/theme.rcss";
    }

    std::string variantPath(std::string_view name)
    {
        return "ui/variants/" + std::string(name) + ".rcss";
    }

    namespace
    {
        enum class Layer
        {
            Missing,
            Refused,
            Ok,
        };

        Layer readLayer(
            const ReadFile& read, const std::string& path, std::string& text, std::vector<StyleDiagnostic>& problems)
        {
            if (!read(path, text))
                return Layer::Missing;
            const auto found = validateStyleSheet(text, path);
            if (found.empty())
                return Layer::Ok;
            problems.insert(problems.end(), found.begin(), found.end());
            return Layer::Refused;
        }

        std::string banner(const std::string& path)
        {
            return "/* ==== " + path + " ==== */\n";
        }

        // Reads one theme sheet; true when it is usable. A refusal is added to `problems`.
        bool readTheme(
            const ReadFile& read, const std::string& name, std::string& text, std::vector<StyleDiagnostic>& problems)
        {
            if (!isValidThemeName(name))
            {
                problems.push_back({ "theme request", 0, 0, "'" + name + "' is not a valid theme name" });
                return false;
            }
            const std::string path = themePath(name);
            switch (readLayer(read, path, text, problems))
            {
                case Layer::Missing:
                    problems.push_back({ path, 0, 0, "theme '" + name + "' not found" });
                    return false;
                case Layer::Refused:
                    return false;
                case Layer::Ok:
                    break;
            }
            const std::vector<std::string> defined = tokensDefined(text);
            bool complete = true;
            for (const std::string& token : requiredTokens())
                if (std::find(defined.begin(), defined.end(), token) == defined.end())
                {
                    problems.push_back(
                        { path, 0, 0, "theme '" + name + "' does not declare the required token " + token });
                    complete = false;
                }
            return complete;
        }
    }

    ThemeAssembly assembleTheme(const ReadFile& read, const ThemeRequest& request)
    {
        ThemeAssembly result;
        std::string text;
        std::string themeText;
        std::string used = request.mName;
        if (!readTheme(read, request.mName, themeText, result.mProblems))
        {
            result.mFellBack = true;
            used = kDefaultThemeName;
            if (request.mName == kDefaultThemeName || !readTheme(read, used, themeText, result.mProblems))
            {
                used.clear();
                themeText.clear();
            }
        }
        if (!used.empty())
        {
            result.mThemeUsed = used;
            result.mSources.push_back(themePath(used));
            result.mText += banner(themePath(used)) + themeText + "\n";
        }
        for (const std::string& variant : request.mVariants)
        {
            if (!isValidThemeName(variant))
            {
                result.mProblems.push_back({ "theme request", 0, 0, "'" + variant + "' is not a valid variant name" });
                continue;
            }
            const std::string path = variantPath(variant);
            switch (readLayer(read, path, text, result.mProblems))
            {
                case Layer::Missing:
                    result.mProblems.push_back({ path, 0, 0, "variant '" + variant + "' not found" });
                    break;
                case Layer::Refused:
                    break;
                case Layer::Ok:
                    result.mVariantsUsed.push_back(variant);
                    result.mSources.push_back(path);
                    result.mText += banner(path) + text + "\n";
                    break;
            }
        }
        switch (readLayer(read, kBaseSheetPath, text, result.mProblems))
        {
            case Layer::Missing:
                result.mProblems.push_back(
                    { kBaseSheetPath, 0, 0, std::string("style sheet ") + kBaseSheetPath + " not found" });
                break;
            case Layer::Refused:
                break;
            case Layer::Ok:
                result.mSources.push_back(kBaseSheetPath);
                result.mText += banner(kBaseSheetPath) + text + "\n";
                break;
        }
        if (readLayer(read, kOverridesPath, text, result.mProblems) == Layer::Ok)
        {
            result.mSources.push_back(kOverridesPath);
            result.mText += banner(kOverridesPath) + text + "\n";
        }
        return result;
    }
}
