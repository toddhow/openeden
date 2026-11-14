// Task 0163 (decision 0015, UI system U3): the theme system. The validator, the token lists, the assembly of the
// served base sheet, the theme host and the file interface hooks. Pure parts run on generated text; the shipped files
// are read from the source tree.
#include <components/ui_rml/fileinterface.hpp>
#include <components/ui_rml/themehost.hpp>
#include <components/ui_rml/themesheet.hpp>

#include "../vfs/vfstestutil.hpp"

#include <gtest/gtest.h>

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <map>
#include <sstream>
#include <string>

namespace
{
    using namespace UiRml;
    using VFSTest::ScratchDir;
    using VFSTest::writeFile;

    bool has(const std::vector<std::string>& list, const std::string& value)
    {
        return std::find(list.begin(), list.end(), value) != list.end();
    }

    bool anyMessageHas(const std::vector<StyleDiagnostic>& list, const std::string& text)
    {
        return std::any_of(list.begin(), list.end(),
            [&](const StyleDiagnostic& d) { return d.mMessage.find(text) != std::string::npos; });
    }

    // A theme sheet that declares every required token.
    std::string fullTheme(const std::string& colour = "#112233")
    {
        std::string text = "body {\n";
        for (const std::string& token : requiredTokens())
            text += "    " + token + ": " + (token.starts_with("--color") ? colour : std::string("4dp")) + ";\n";
        text += "}\n";
        return text;
    }

    ReadFile readerOf(const std::map<std::string, std::string>& files)
    {
        return [&files](const std::string& path, std::string& out) {
            const auto it = files.find(path);
            if (it == files.end())
                return false;
            out = it->second;
            return true;
        };
    }

    std::map<std::string, std::string> basicFiles()
    {
        return { { "ui/themes/neutral/theme.rcss", fullTheme("#111111") },
            { "ui/themes/other/theme.rcss", fullTheme("#222222") },
            { "ui/variants/big.rcss", "body { --font-size-md: 30dp; }\n" }, { "ui/base.rcss", "body { color: var(--color-text); }\n" } };
    }

    std::string readFileText(const std::filesystem::path& path)
    {
        std::ifstream in(path, std::ios::binary);
        return { std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>() };
    }

    std::filesystem::path sourceFile(const std::string& relative)
    {
        return std::filesystem::path{ OPENMW_PROJECT_SOURCE_DIR } / "files" / "data" / relative;
    }

    // ---- the validator ----------------------------------------------------------------------------------------

    TEST(UiThemeValidator, a_plain_sheet_has_no_problems)
    {
        EXPECT_TRUE(validateStyleSheet("body { color: #fff; padding: 4dp 8dp; }\n.btn:hover { background-color: red; }\n", "a.rcss")
                        .empty());
    }

    TEST(UiThemeValidator, each_forbidden_property_is_refused_with_file_and_line)
    {
        for (const char* property : { "box-shadow: 1px 1px 2px #000", "filter: blur(2px)", "backdrop-filter: blur(2px)",
                 "mask-image: url(a.png)" })
        {
            const std::string text = std::string("div {\n  color: red;\n  ") + property + ";\n}\n";
            const auto problems = validateStyleSheet(text, "mod/x.rcss");
            ASSERT_FALSE(problems.empty()) << property;
            EXPECT_EQ(problems[0].mFile, "mod/x.rcss") << property;
            EXPECT_EQ(problems[0].mLine, 3) << property;
            EXPECT_EQ(problems[0].mColumn, 3) << property;
            EXPECT_EQ(problems[0].text().rfind("mod/x.rcss:3:3: ", 0), 0u) << problems[0].text();
        }
    }

    TEST(UiThemeValidator, gradients_and_shaders_are_refused_wherever_they_appear)
    {
        for (const char* value : { "decorator: linear-gradient(horizontal, #fff, #000)",
                 "background-image: radial-gradient(circle, #fff, #000)", "decorator: conic-gradient(#fff, #000)",
                 "decorator: repeating-linear-gradient(#fff, #000 4px)", "decorator: shader(my-shader)",
                 "--looks: linear-gradient(#fff, #000)" })
        {
            const auto problems = validateStyleSheet(std::string("a { ") + value + "; }", "g.rcss");
            EXPECT_FALSE(problems.empty()) << value;
        }
    }

    TEST(UiThemeValidator, the_straight_gradients_are_drawn_from_vertex_colours_and_stay_allowed)
    {
        EXPECT_TRUE(validateStyleSheet("a { decorator: horizontal-gradient(#fff #000); }\nb { decorator: vertical-gradient(#fff #000); }", "s.rcss")
                        .empty());
    }

    TEST(UiThemeValidator, filter_functions_in_any_value_are_refused)
    {
        EXPECT_FALSE(validateStyleSheet("a { decorator: blur(3px); }", "f.rcss").empty());
        EXPECT_FALSE(validateStyleSheet("a { x: drop-shadow(1px 1px 2px #000); }", "f.rcss").empty());
    }

    TEST(UiThemeValidator, a_radius_is_fine_alone_and_refused_next_to_clipping_overflow)
    {
        EXPECT_TRUE(validateStyleSheet(".a { border-radius: 4dp; }\n", "r.rcss").empty());
        EXPECT_TRUE(validateStyleSheet(".a { border-radius: 4dp; overflow: visible; }\n", "r.rcss").empty());
        for (const char* overflow : { "overflow: hidden", "overflow-y: scroll", "overflow-x: auto" })
        {
            // either order of the two declarations
            const auto first = validateStyleSheet(std::string(".a {\n border-radius: 4dp;\n ") + overflow + ";\n}\n", "r.rcss");
            ASSERT_FALSE(first.empty()) << overflow;
            EXPECT_EQ(first[0].mLine, 2) << overflow;
            const auto second = validateStyleSheet(std::string(".a { ") + overflow + "; border-top-left-radius: var(--r); }\n", "r.rcss");
            EXPECT_FALSE(second.empty()) << overflow;
        }
        // another rule's overflow does not count
        EXPECT_TRUE(validateStyleSheet(".a { border-radius: 4dp; }\n.b { overflow: hidden; }\n", "r.rcss").empty());
    }

    TEST(UiThemeValidator, comments_and_strings_are_not_looked_into)
    {
        const std::string text = "/* box-shadow: 1px; filter: blur(2px) */\na { font-family: \"filter: blur(2px)\"; }\n";
        EXPECT_TRUE(validateStyleSheet(text, "c.rcss").empty());
    }

    TEST(UiThemeValidator, names_are_matched_case_insensitively_and_lines_count_from_the_first_line)
    {
        const auto problems = validateStyleSheet("a {\n  BOX-SHADOW: 1px 1px #000;\n}", "u.rcss", 10);
        ASSERT_EQ(problems.size(), 1u);
        EXPECT_EQ(problems[0].mLine, 11);
    }

    TEST(UiThemeValidator, every_problem_in_a_sheet_is_reported)
    {
        const auto problems = validateStyleSheet("a { filter: blur(1px); }\nb { box-shadow: 1px 1px #000; }\n", "m.rcss");
        EXPECT_GE(problems.size(), 2u);
    }

    TEST(UiThemeValidator, the_style_blocks_of_a_document_are_checked_with_the_documents_lines)
    {
        const std::string rml = "<rml>\n<head>\n<style>\nbody { color: red; }\ndiv { box-shadow: 1px 1px #000; }\n</style>\n</head>\n<body/>\n</rml>\n";
        const auto problems = validateMarkupStyles(rml, "d.rml");
        ASSERT_EQ(problems.size(), 1u);
        EXPECT_EQ(problems[0].mFile, "d.rml");
        EXPECT_EQ(problems[0].mLine, 5);
        EXPECT_TRUE(validateMarkupStyles("<rml><body>box-shadow: 1px</body></rml>", "d.rml").empty());
    }

    // ---- tokens and names ------------------------------------------------------------------------------------

    TEST(UiThemeTokens, defined_and_used_tokens_are_listed_in_first_use_order)
    {
        const std::string text = "body { --a: 1dp; --b: var(--a); }\n.x { color: var(--c, red); width: var(--a); --a: 2dp; }";
        EXPECT_EQ(tokensDefined(text), (std::vector<std::string>{ "--a", "--b" }));
        EXPECT_EQ(tokensUsed(text), (std::vector<std::string>{ "--a", "--c" }));
        EXPECT_TRUE(tokensDefined("/* --x: 1 */ a { }").empty());
    }

    TEST(UiThemeTokens, the_required_set_names_the_design_tokens)
    {
        const auto& required = requiredTokens();
        for (const char* token : { "--font-family", "--font-size-md", "--space-4", "--radius-md", "--border-width",
                 "--color-surface", "--color-text", "--color-control-focus", "--scrollbar-width" })
            EXPECT_TRUE(has(required, token)) << token;
        EXPECT_FALSE(std::any_of(required.begin(), required.end(), [](const std::string& t) { return !t.starts_with("--"); }));
    }

    TEST(UiThemeNames, a_name_is_a_safe_path_component)
    {
        EXPECT_TRUE(isValidThemeName("neutral"));
        EXPECT_TRUE(isValidThemeName("high-contrast_2"));
        for (const char* bad : { "", "..", "a/b", "a\\b", "A", "a b", "a.b" })
            EXPECT_FALSE(isValidThemeName(bad)) << bad;
        EXPECT_FALSE(isValidThemeName(std::string(65, 'a')));
    }

    TEST(UiThemeNames, a_list_is_trimmed_deduplicated_and_ordered)
    {
        EXPECT_EQ(parseNameList(" a, b,,a ,c"), (std::vector<std::string>{ "a", "b", "c" }));
        EXPECT_TRUE(parseNameList("").empty());
        EXPECT_TRUE(parseNameList(" , ").empty());
    }

    TEST(UiThemePaths, theme_and_variant_paths)
    {
        EXPECT_EQ(themePath("neutral"), "ui/themes/neutral/theme.rcss");
        EXPECT_EQ(variantPath("large-text"), "ui/variants/large-text.rcss");
    }

    // ---- assembly --------------------------------------------------------------------------------------------

    TEST(UiThemeAssembly, the_layers_are_joined_theme_variants_base_overrides)
    {
        auto files = basicFiles();
        files["ui/overrides.rcss"] = "body { --color-text: #abcdef; }\n";
        const ThemeAssembly a = assembleTheme(readerOf(files), { "neutral", { "big" } });
        EXPECT_TRUE(a.mProblems.empty());
        EXPECT_FALSE(a.mFellBack);
        EXPECT_EQ(a.mThemeUsed, "neutral");
        EXPECT_EQ(a.mVariantsUsed, (std::vector<std::string>{ "big" }));
        EXPECT_EQ(a.mSources,
            (std::vector<std::string>{ "ui/themes/neutral/theme.rcss", "ui/variants/big.rcss", "ui/base.rcss", "ui/overrides.rcss" }));
        const std::size_t theme = a.mText.find("#111111");
        const std::size_t variant = a.mText.find("30dp");
        const std::size_t base = a.mText.find("color: var(--color-text)");
        const std::size_t overrides = a.mText.find("#abcdef");
        ASSERT_NE(theme, std::string::npos);
        ASSERT_NE(variant, std::string::npos);
        ASSERT_NE(base, std::string::npos);
        ASSERT_NE(overrides, std::string::npos);
        EXPECT_LT(theme, variant);
        EXPECT_LT(variant, base);
        EXPECT_LT(base, overrides);
        // each layer is introduced by a comment naming it
        EXPECT_NE(a.mText.find("ui/variants/big.rcss"), std::string::npos);
    }

    TEST(UiThemeAssembly, another_theme_can_be_chosen_and_missing_overrides_are_no_problem)
    {
        const auto files = basicFiles();
        const ThemeAssembly a = assembleTheme(readerOf(files), { "other", {} });
        EXPECT_TRUE(a.mProblems.empty());
        EXPECT_EQ(a.mThemeUsed, "other");
        EXPECT_NE(a.mText.find("#222222"), std::string::npos);
        EXPECT_EQ(a.mText.find("#111111"), std::string::npos);
    }

    TEST(UiThemeAssembly, an_unknown_theme_falls_back_to_neutral_and_says_so)
    {
        const auto files = basicFiles();
        const ThemeAssembly a = assembleTheme(readerOf(files), { "bogus", {} });
        EXPECT_TRUE(a.mFellBack);
        EXPECT_EQ(a.mThemeUsed, "neutral");
        EXPECT_TRUE(anyMessageHas(a.mProblems, "bogus"));
        EXPECT_NE(a.mText.find("#111111"), std::string::npos);
    }

    TEST(UiThemeAssembly, a_name_that_is_not_a_path_component_is_never_read)
    {
        auto files = basicFiles();
        std::vector<std::string> asked;
        const ReadFile counting = [&](const std::string& path, std::string& out) {
            asked.push_back(path);
            return readerOf(files)(path, out);
        };
        const ThemeAssembly a = assembleTheme(counting, { "../../etc", { "../x" } });
        EXPECT_TRUE(a.mFellBack);
        EXPECT_EQ(a.mThemeUsed, "neutral");
        EXPECT_TRUE(a.mVariantsUsed.empty());
        EXPECT_FALSE(a.mProblems.empty());
        for (const std::string& source : a.mSources)
            EXPECT_EQ(source.find(".."), std::string::npos);
        for (const std::string& path : asked)
            EXPECT_EQ(path.find(".."), std::string::npos) << path;
    }

    TEST(UiThemeAssembly, a_theme_the_validator_refuses_falls_back_with_a_located_problem)
    {
        auto files = basicFiles();
        files["ui/themes/other/theme.rcss"] = fullTheme() + "\n.x {\n  box-shadow: 1px 1px #000;\n}\n";
        const ThemeAssembly a = assembleTheme(readerOf(files), { "other", {} });
        EXPECT_TRUE(a.mFellBack);
        EXPECT_EQ(a.mThemeUsed, "neutral");
        ASSERT_FALSE(a.mProblems.empty());
        const auto located = std::find_if(a.mProblems.begin(), a.mProblems.end(),
            [](const StyleDiagnostic& d) { return d.mFile == "ui/themes/other/theme.rcss" && d.mLine > 1; });
        EXPECT_NE(located, a.mProblems.end());
    }

    TEST(UiThemeAssembly, a_theme_that_lacks_a_required_token_is_refused_naming_it)
    {
        auto files = basicFiles();
        std::string partial = fullTheme();
        const std::size_t at = partial.find("--color-text:");
        ASSERT_NE(at, std::string::npos);
        partial.replace(at, std::string("--color-text:").size(), "--color-texx:");
        files["ui/themes/other/theme.rcss"] = partial;
        const ThemeAssembly a = assembleTheme(readerOf(files), { "other", {} });
        EXPECT_TRUE(a.mFellBack);
        EXPECT_TRUE(anyMessageHas(a.mProblems, "--color-text"));
    }

    TEST(UiThemeAssembly, a_variant_that_is_missing_or_refused_is_skipped_and_named)
    {
        auto files = basicFiles();
        files["ui/variants/bad.rcss"] = "body { filter: blur(2px); }\n";
        const ThemeAssembly a = assembleTheme(readerOf(files), { "neutral", { "nope", "bad", "big" } });
        EXPECT_FALSE(a.mFellBack);
        EXPECT_EQ(a.mVariantsUsed, (std::vector<std::string>{ "big" }));
        EXPECT_TRUE(anyMessageHas(a.mProblems, "nope"));
        const auto badLocated = std::find_if(a.mProblems.begin(), a.mProblems.end(),
            [](const StyleDiagnostic& d) { return d.mFile == "ui/variants/bad.rcss" && d.mLine == 1; });
        EXPECT_NE(badLocated, a.mProblems.end());
    }

    TEST(UiThemeAssembly, refused_overrides_are_skipped_and_the_rest_is_kept)
    {
        auto files = basicFiles();
        files["ui/overrides.rcss"] = "a {\n box-shadow: 1px 1px #000;\n}\n";
        const ThemeAssembly a = assembleTheme(readerOf(files), { "neutral", {} });
        EXPECT_FALSE(a.mFellBack);
        EXPECT_EQ(a.mText.find("box-shadow"), std::string::npos);
        EXPECT_NE(a.mText.find("#111111"), std::string::npos);
        const auto located = std::find_if(a.mProblems.begin(), a.mProblems.end(),
            [](const StyleDiagnostic& d) { return d.mFile == "ui/overrides.rcss" && d.mLine == 2; });
        EXPECT_NE(located, a.mProblems.end());
    }

    TEST(UiThemeAssembly, a_missing_base_sheet_is_a_problem_but_the_tokens_are_still_served)
    {
        auto files = basicFiles();
        files.erase("ui/base.rcss");
        const ThemeAssembly a = assembleTheme(readerOf(files), { "neutral", {} });
        EXPECT_TRUE(anyMessageHas(a.mProblems, "ui/base.rcss"));
        EXPECT_NE(a.mText.find("#111111"), std::string::npos);
    }

    TEST(UiThemeAssembly, with_no_theme_at_all_nothing_is_served_but_the_problems_are_there)
    {
        std::map<std::string, std::string> files;
        const ThemeAssembly a = assembleTheme(readerOf(files), { "neutral", {} });
        EXPECT_TRUE(a.mThemeUsed.empty());
        EXPECT_FALSE(a.mProblems.empty());
    }

    TEST(UiThemeAssembly, assembling_twice_gives_the_same_bytes)
    {
        const auto files = basicFiles();
        EXPECT_EQ(assembleTheme(readerOf(files), { "other", { "big" } }).mText, assembleTheme(readerOf(files), { "other", { "big" } }).mText);
    }

    // a mod's folder that holds the same path wins by VFS order
    TEST(UiThemeAssembly, a_later_data_folder_replaces_a_theme_file_and_adds_overrides)
    {
        ScratchDir shipped;
        ScratchDir mod;
        writeFile(shipped.path() / "ui" / "themes" / "neutral" / "theme.rcss", fullTheme("#111111"));
        writeFile(shipped.path() / "ui" / "base.rcss", "body { color: var(--color-text); }\n");
        writeFile(mod.path() / "ui" / "themes" / "neutral" / "theme.rcss", fullTheme("#99aa00"));
        writeFile(mod.path() / "ui" / "overrides.rcss", "body { --color-text: #00aa99; }\n");
        VFS::Manager vfs;
        vfs.addArchive(std::make_unique<VFS::FileSystemArchive>(shipped.path()));
        vfs.addArchive(std::make_unique<VFS::FileSystemArchive>(mod.path()));
        vfs.buildIndex();
        const ReadFile read = [&vfs](const std::string& path, std::string& out) {
            const Files::IStreamPtr stream = vfs.find(VFS::Path::Normalized(path));
            if (stream == nullptr)
                return false;
            out.assign(std::istreambuf_iterator<char>(*stream), std::istreambuf_iterator<char>());
            return true;
        };
        const ThemeAssembly a = assembleTheme(read, { "neutral", {} });
        EXPECT_TRUE(a.mProblems.empty());
        EXPECT_NE(a.mText.find("#99aa00"), std::string::npos);
        EXPECT_EQ(a.mText.find("#111111"), std::string::npos);
        EXPECT_NE(a.mText.find("#00aa99"), std::string::npos);
    }

    // ---- the shipped files -----------------------------------------------------------------------------------

    TEST(UiThemeShipped, the_base_sheet_reads_only_required_tokens_and_every_shipped_sheet_validates)
    {
        const std::string base = readFileText(sourceFile("ui/base.rcss"));
        ASSERT_FALSE(base.empty());
        const auto used = tokensUsed(base);
        EXPECT_FALSE(used.empty());
        for (const std::string& token : used)
            EXPECT_TRUE(has(requiredTokens(), token)) << token;
        for (const char* file : { "ui/base.rcss", "ui/debugpanel.rcss", "ui/themes/neutral/theme.rcss",
                 "ui/themes/high-contrast/theme.rcss", "ui/variants/large-text.rcss", "ui/variants/small-text.rcss" })
        {
            const auto problems = validateStyleSheet(readFileText(sourceFile(file)), file);
            for (const auto& problem : problems)
                ADD_FAILURE() << problem.text();
        }
        const auto markup = validateMarkupStyles(readFileText(sourceFile("ui/debugpanel.rml")), "ui/debugpanel.rml");
        EXPECT_TRUE(markup.empty());
    }

    TEST(UiThemeShipped, every_shipped_theme_declares_every_required_token_and_assembles_cleanly)
    {
        for (const char* name : { "neutral", "high-contrast" })
        {
            const std::string theme = readFileText(sourceFile(std::string("ui/themes/") + name + "/theme.rcss"));
            const auto defined = tokensDefined(theme);
            for (const std::string& token : requiredTokens())
                EXPECT_TRUE(has(defined, token)) << name << " lacks " << token;
        }
        const ReadFile read = [](const std::string& path, std::string& out) {
            out = readFileText(sourceFile(path));
            return !out.empty();
        };
        for (const char* name : { "neutral", "high-contrast" })
            for (const char* variant : { "", "large-text", "small-text" })
            {
                ThemeRequest request{ name, {} };
                if (*variant != 0)
                    request.mVariants.push_back(variant);
                const ThemeAssembly a = assembleTheme(read, request);
                EXPECT_TRUE(a.mProblems.empty()) << name << " " << variant;
                EXPECT_FALSE(a.mFellBack) << name << " " << variant;
            }
    }

    TEST(UiThemeShipped, the_variants_only_declare_required_tokens)
    {
        for (const char* file : { "ui/variants/large-text.rcss", "ui/variants/small-text.rcss" })
            for (const std::string& token : tokensDefined(readFileText(sourceFile(file))))
                EXPECT_TRUE(has(requiredTokens(), token)) << file << " " << token;
    }

    // ---- the theme host --------------------------------------------------------------------------------------

    TEST(UiThemeHost, the_first_refresh_reports_a_change_and_an_equal_one_does_not)
    {
        auto files = basicFiles();
        ThemeHost host(readerOf(files));
        EXPECT_TRUE(host.refresh());
        EXPECT_FALSE(host.refresh());
        EXPECT_EQ(host.assembly().mThemeUsed, "neutral");
    }

    TEST(UiThemeHost, a_changed_file_is_found_by_the_next_refresh_and_served)
    {
        auto files = basicFiles();
        ThemeHost host(readerOf(files));
        host.refresh();
        files["ui/themes/neutral/theme.rcss"] = fullTheme("#ababab");
        EXPECT_TRUE(host.refresh());
        std::vector<char> out;
        ASSERT_TRUE(host.generate("ui/base.rcss", out));
        EXPECT_NE(std::string(out.begin(), out.end()).find("#ababab"), std::string::npos);
        EXPECT_FALSE(host.refresh());
    }

    TEST(UiThemeHost, a_new_request_is_applied_at_the_next_refresh)
    {
        auto files = basicFiles();
        ThemeHost host(readerOf(files));
        host.refresh();
        host.setRequest({ "other", { "big" } });
        EXPECT_EQ(host.request().mName, "other");
        EXPECT_TRUE(host.refresh());
        EXPECT_EQ(host.assembly().mThemeUsed, "other");
        host.setRequest({ "other", { "big" } });
        EXPECT_FALSE(host.refresh());
    }

    TEST(UiThemeHost, only_the_base_sheet_path_is_generated_and_spelling_does_not_matter)
    {
        auto files = basicFiles();
        ThemeHost host(readerOf(files));
        std::vector<char> out;
        EXPECT_FALSE(host.generate("ui/debugpanel.rcss", out));
        EXPECT_FALSE(host.generate("ui/themes/neutral/theme.rcss", out));
        // nothing was assembled yet: the first request assembles
        EXPECT_TRUE(host.generate("UI\\Base.RCSS", out));
        EXPECT_FALSE(out.empty());
    }

    // ---- the file interface hooks ----------------------------------------------------------------------------

    std::unique_ptr<VFS::Manager> managerOf(const std::filesystem::path& folder)
    {
        auto manager = std::make_unique<VFS::Manager>();
        manager->addArchive(std::make_unique<VFS::FileSystemArchive>(folder));
        manager->buildIndex();
        return manager;
    }

    TEST(UiThemeFileInterface, a_generator_serves_a_file_the_vfs_does_not_have)
    {
        ScratchDir dir;
        writeFile(dir.path() / "other.txt", "x");
        const auto vfs = managerOf(dir.path());
        VfsFileInterface files(*vfs);
        files.setGenerator([](const std::string& path, std::vector<char>& out) {
            if (path != "ui/base.rcss")
                return false;
            out = { 'a', 'b', 'c' };
            return true;
        });
        const Rml::FileHandle file = files.Open("UI/Base.rcss");
        ASSERT_NE(file, 0u);
        EXPECT_EQ(files.Length(file), 3u);
        files.Close(file);
        EXPECT_EQ(files.Open("ui/other.rcss"), 0u);
        const Rml::FileHandle other = files.Open("other.txt");
        EXPECT_NE(other, 0u);
        files.Close(other);
    }

    TEST(UiThemeFileInterface, a_generated_file_wins_over_one_of_the_same_name_in_the_vfs_and_skips_the_guard)
    {
        ScratchDir dir;
        writeFile(dir.path() / "ui" / "base.rcss", "from the vfs");
        const auto vfs = managerOf(dir.path());
        VfsFileInterface files(*vfs);
        int guarded = 0;
        files.setGenerator([](const std::string& path, std::vector<char>& out) {
            if (path != "ui/base.rcss")
                return false;
            out = { 'g' };
            return true;
        });
        files.setGuard([&](const std::string&, const std::vector<char>&) {
            ++guarded;
            return true;
        });
        const Rml::FileHandle file = files.Open("ui/base.rcss");
        ASSERT_NE(file, 0u);
        EXPECT_EQ(files.Length(file), 1u);
        files.Close(file);
        EXPECT_EQ(guarded, 0);
    }

    TEST(UiThemeFileInterface, the_guard_can_refuse_style_sheets_and_documents_only)
    {
        ScratchDir dir;
        writeFile(dir.path() / "ui" / "bad.rcss", "a { box-shadow: 1px; }");
        writeFile(dir.path() / "ui" / "doc.rml", "<rml/>");
        writeFile(dir.path() / "ui" / "pic.txt", "text");
        const auto vfs = managerOf(dir.path());
        VfsFileInterface files(*vfs);
        std::vector<std::string> asked;
        files.setGuard([&](const std::string& path, const std::vector<char>&) {
            asked.push_back(path);
            return path != "ui/bad.rcss";
        });
        EXPECT_EQ(files.Open("ui/bad.rcss"), 0u);
        const Rml::FileHandle doc = files.Open("ui/doc.rml");
        EXPECT_NE(doc, 0u);
        files.Close(doc);
        const Rml::FileHandle pic = files.Open("ui/pic.txt");
        EXPECT_NE(pic, 0u);
        files.Close(pic);
        EXPECT_EQ(asked, (std::vector<std::string>{ "ui/bad.rcss", "ui/doc.rml" }));
        EXPECT_EQ(files.openCount(), 0u);
    }

    TEST(UiThemeFileInterface, a_changed_style_sheet_is_noticed_once)
    {
        ScratchDir dir;
        writeFile(dir.path() / "ui" / "a.rcss", "a { color: red; }");
        writeFile(dir.path() / "ui" / "b.txt", "one");
        const auto vfs = managerOf(dir.path());
        VfsFileInterface files(*vfs);
        EXPECT_FALSE(files.watchedFilesChanged()); // nothing opened yet
        const Rml::FileHandle file = files.Open("ui/a.rcss");
        files.Close(file);
        const Rml::FileHandle txt = files.Open("ui/b.txt");
        files.Close(txt);
        EXPECT_FALSE(files.watchedFilesChanged());
        writeFile(dir.path() / "ui" / "b.txt", "two");
        EXPECT_FALSE(files.watchedFilesChanged()); // only style sheets are watched
        writeFile(dir.path() / "ui" / "a.rcss", "a { color: blue; }");
        EXPECT_TRUE(files.watchedFilesChanged());
        EXPECT_FALSE(files.watchedFilesChanged());
    }

    TEST(UiThemeFileInterface, generated_files_are_not_watched)
    {
        ScratchDir dir;
        writeFile(dir.path() / "x.txt", "x");
        const auto vfs = managerOf(dir.path());
        VfsFileInterface files(*vfs);
        files.setGenerator([](const std::string& path, std::vector<char>& out) {
            if (path != "ui/base.rcss")
                return false;
            out = { 'g' };
            return true;
        });
        files.Close(files.Open("ui/base.rcss"));
        EXPECT_FALSE(files.watchedFilesChanged());
    }
}
