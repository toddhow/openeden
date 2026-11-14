#ifndef OPENEDEN_COMPONENTS_UI_RML_THEMESHEET_H
#define OPENEDEN_COMPONENTS_UI_RML_THEMESHEET_H

#include <functional>
#include <string>
#include <string_view>
#include <vector>

// The theme system of the new UI (task 0163, decision 0015 point 2). Everything here is pure: no RmlUi, no VFS, no
// thread. A theme is a style sheet of design tokens (RCSS custom properties on `body`); the style sheet every document
// links, `ui/base.rcss`, is served as the chosen theme's tokens, the chosen variants, the real base sheet and the
// optional user overrides, joined in that order.
namespace UiRml
{
    /// One problem in a style sheet: where it is and what it is. `mLine` and `mColumn` count from 1.
    struct StyleDiagnostic
    {
        std::string mFile;
        int mLine = 0;
        int mColumn = 0;
        std::string mMessage;

        /// "file:line:column: message"
        std::string text() const;
        bool operator==(const StyleDiagnostic&) const = default;
    };

    /// Refuses what the render interface cannot draw (decision 0015, note 0045 14.9 decision 3): the properties
    /// `box-shadow`, `filter`, `backdrop-filter` and `mask-image`, any gradient or `shader(` value (also in `decorator`,
    /// `background-image` and custom properties), the filter functions `blur(` and `drop-shadow(`, and a
    /// `border-radius` (or corner radius) in a rule whose `overflow` is not `visible` (rounded clipping needs a clip
    /// mask). Comments and quoted strings are skipped. Lines count from `firstLine`.
    std::vector<StyleDiagnostic> validateStyleSheet(std::string_view text, std::string_view fileName, int firstLine = 1);

    /// The same for the `<style>` blocks of an RML document; lines are the document's own.
    std::vector<StyleDiagnostic> validateMarkupStyles(std::string_view rml, std::string_view fileName);

    /// Names (with the leading `--`) of the custom properties a sheet declares, in order of first declaration.
    std::vector<std::string> tokensDefined(std::string_view text);
    /// Names of the custom properties a sheet reads with `var(`, in order of first use.
    std::vector<std::string> tokensUsed(std::string_view text);
    /// Every token a theme must declare (the base sheet reads only these).
    const std::vector<std::string>& requiredTokens();

    /// A theme or variant name is a path component: lower-case letters, digits, `-` and `_`, 1 to 64 characters.
    bool isValidThemeName(std::string_view name);
    /// "a, b,,c" gives {a, b, c}; spaces trimmed, empty names dropped, duplicates removed (first wins).
    std::vector<std::string> parseNameList(std::string_view list);

    inline constexpr const char* kDefaultThemeName = "neutral";
    /// `ui/base.rcss`: the one path documents link and the file interface generates.
    inline constexpr const char* kBaseSheetPath = "ui/base.rcss";
    inline constexpr const char* kOverridesPath = "ui/overrides.rcss";
    std::string themePath(std::string_view name);   // ui/themes/<name>/theme.rcss
    std::string variantPath(std::string_view name); // ui/variants/<name>.rcss

    struct ThemeRequest
    {
        std::string mName = kDefaultThemeName;
        std::vector<std::string> mVariants;
        bool operator==(const ThemeRequest&) const = default;
    };

    /// Reads a file by its virtual path; false when it does not exist. The theme code never touches the disk itself.
    using ReadFile = std::function<bool(const std::string& path, std::string& out)>;

    struct ThemeAssembly
    {
        /// The served `ui/base.rcss`.
        std::string mText;
        /// The theme that was used (the requested one, or `neutral` after a fall back, or empty when even that failed).
        std::string mThemeUsed;
        std::vector<std::string> mVariantsUsed;
        /// The virtual paths that went into mText, in order.
        std::vector<std::string> mSources;
        /// Everything that was refused or missing, located.
        std::vector<StyleDiagnostic> mProblems;
        bool mFellBack = false;
    };

    /// Joins theme, variants, `ui/base.rcss` and `ui/overrides.rcss` (missing overrides are not a problem). A layer
    /// that is missing, refused by the validator or (for a theme) does not declare every required token is skipped and
    /// named in mProblems; a theme that fails falls back to `neutral`. Later data wins by the reader's own path rules.
    ThemeAssembly assembleTheme(const ReadFile& read, const ThemeRequest& request);
}

#endif
