#include <components/nifedit/nifdocument.hpp>

#include <gtest/gtest.h>

#include "nifeditsupport.hpp"

// Task 0046 stage c (and the checks of stage e that need no file): the document, its commands and the undo stack.
namespace NifEdit
{
    namespace
    {
        using Testing::Built;
        using Testing::MwBuilder;

        std::unique_ptr<SetFieldCommand> set(
            Document& document, std::uint32_t record, const char* field, PatchValue value, bool mergeable = false)
        {
            return std::make_unique<SetFieldCommand>(document, record, field, std::move(value), mergeable);
        }

        TEST(NifEditDocument, a_set_value_is_read_back_and_the_document_is_dirty_until_saved)
        {
            Document document = Document::open(MwBuilder::make().mBytes);
            EXPECT_FALSE(document.dirty());
            document.set(2, "name", std::string("Edited"));
            EXPECT_TRUE(document.dirty());
            EXPECT_EQ(std::get<std::string>(document.value(2, "name")), "Edited");
            EXPECT_EQ(std::get<std::string>(document.originalValue(2, "name")), "Child");
            const std::string saved = document.buildChecked();
            EXPECT_NE(saved, document.original());
            document.markSaved();
            EXPECT_FALSE(document.dirty());
            EXPECT_EQ(Testing::dumpAll(saved)[2].find("name=[Edited]") != std::string::npos, true);
        }

        TEST(NifEditDocument, setting_a_field_to_the_files_own_value_removes_the_patch)
        {
            Document document = Document::open(MwBuilder::make().mBytes);
            document.set(2, "scale", 3.0);
            EXPECT_FALSE(document.patches().empty());
            document.set(2, "scale", 0.5); // the file holds 0.5
            EXPECT_TRUE(document.patches().empty());
            EXPECT_EQ(document.build(), document.original());
        }

        TEST(NifEditDocument, a_refused_value_changes_nothing)
        {
            Document document = Document::open(MwBuilder::make().mBytes);
            document.set(2, "scale", 3.0);
            const std::uint64_t generation = document.generation();
            EXPECT_THROW(document.set(2, "scale", std::string("no")), EditError);
            EXPECT_THROW(document.set(3, "name", std::string("no")), EditError);
            EXPECT_THROW(document.set(2, "flags", std::int64_t(-5)), EditError);
            EXPECT_EQ(document.generation(), generation);
            EXPECT_EQ(document.patches().patches().size(), 1u);
        }

        TEST(NifEditDocument, a_float_set_from_a_double_reads_back_as_the_stored_float)
        {
            Document document = Document::open(MwBuilder::make().mBytes);
            document.set(4, "alpha", 0.1); // not exact in a float
            const std::string saved = document.buildChecked();
            const auto* material
                = dynamic_cast<const Nif::NiMaterialProperty*>(Testing::parseFile(saved)->mRecords[4].get());
            EXPECT_EQ(material->mAlpha, 0.1f);
        }

        TEST(NifEditUndo, undo_and_redo_restore_the_patch_set_and_the_bytes_exactly)
        {
            Document document = Document::open(MwBuilder::make().mBytes);
            UndoStack stack;
            const std::string original = document.build();

            stack.push(set(document, 2, "name", std::string("One")));
            const std::string afterOne = document.build();
            stack.push(set(document, 2, "scale", 2.0));
            stack.push(set(document, 8, "keys", std::vector<TextKey>{ { 1.f, "x: y" } }));
            const std::string afterThree = document.build();
            EXPECT_EQ(stack.size(), 3u);

            stack.undo();
            stack.undo();
            EXPECT_EQ(document.build(), afterOne);
            stack.undo();
            EXPECT_EQ(document.build(), original);
            EXPECT_TRUE(document.patches().empty());
            EXPECT_FALSE(stack.canUndo());

            stack.redo();
            stack.redo();
            stack.redo();
            EXPECT_EQ(document.build(), afterThree);
            EXPECT_FALSE(stack.canRedo());
        }

        TEST(NifEditUndo, undo_puts_back_an_earlier_patch_not_the_files_value)
        {
            Document document = Document::open(MwBuilder::make().mBytes);
            UndoStack stack;
            stack.push(set(document, 2, "scale", 2.0));
            stack.push(set(document, 2, "scale", 3.0));
            EXPECT_EQ(stack.size(), 2u);
            stack.undo();
            EXPECT_EQ(std::get<double>(document.value(2, "scale")), 2.0);
            stack.undo();
            EXPECT_EQ(std::get<double>(document.value(2, "scale")), 0.5);
        }

        TEST(NifEditUndo, a_dragged_value_merges_into_one_step)
        {
            Document document = Document::open(MwBuilder::make().mBytes);
            UndoStack stack;
            for (double value : { 1.0, 1.5, 2.0, 2.5, 3.0 })
                stack.push(set(document, 2, "scale", value, true));
            EXPECT_EQ(stack.size(), 1u);
            EXPECT_EQ(std::get<double>(document.value(2, "scale")), 3.0);
            stack.undo();
            EXPECT_EQ(std::get<double>(document.value(2, "scale")), 0.5);
            EXPECT_TRUE(document.patches().empty());
            stack.redo();
            EXPECT_EQ(std::get<double>(document.value(2, "scale")), 3.0);
        }

        TEST(NifEditUndo, merging_stops_at_another_field_or_a_non_mergeable_step)
        {
            Document document = Document::open(MwBuilder::make().mBytes);
            UndoStack stack;
            stack.push(set(document, 2, "scale", 1.0, true));
            stack.push(set(document, 2, "scale", 2.0, true));
            stack.push(set(document, 1, "scale", 5.0, true)); // another record
            stack.push(set(document, 1, "scale", 6.0, false)); // not mergeable
            stack.push(set(document, 1, "scale", 7.0, true)); // the top is not mergeable
            EXPECT_EQ(stack.size(), 4u);
        }

        TEST(NifEditUndo, pushing_after_an_undo_drops_the_redo_branch)
        {
            Document document = Document::open(MwBuilder::make().mBytes);
            UndoStack stack;
            stack.push(set(document, 2, "scale", 1.0));
            stack.push(set(document, 2, "scale", 2.0));
            stack.undo();
            stack.push(set(document, 2, "flags", std::int64_t(5)));
            EXPECT_FALSE(stack.canRedo());
            EXPECT_EQ(stack.size(), 2u);
            EXPECT_EQ(std::get<double>(document.value(2, "scale")), 1.0);
        }

        TEST(NifEditUndo, a_refused_command_is_not_pushed)
        {
            Document document = Document::open(MwBuilder::make().mBytes);
            UndoStack stack;
            EXPECT_THROW(stack.push(set(document, 3, "name", std::string("x"))), EditError);
            EXPECT_EQ(stack.size(), 0u);
            EXPECT_FALSE(document.dirty());
        }

        TEST(NifEditUndo, the_clean_state_follows_the_save)
        {
            Document document = Document::open(MwBuilder::make().mBytes);
            UndoStack stack;
            EXPECT_TRUE(stack.isClean());
            stack.push(set(document, 2, "scale", 1.0));
            EXPECT_FALSE(stack.isClean());
            stack.setClean();
            EXPECT_TRUE(stack.isClean());
            stack.undo();
            EXPECT_FALSE(stack.isClean());
            stack.redo();
            EXPECT_TRUE(stack.isClean());
            // A merge into the saved step makes it differ from the saved file.
            stack.push(set(document, 2, "scale", 1.5, true));
            stack.setClean();
            stack.push(set(document, 2, "scale", 1.75, true));
            EXPECT_FALSE(stack.isClean());
            // A new step after undoing past the saved state can never be clean again by undo and redo.
            stack.undo();
            stack.undo();
            stack.push(set(document, 2, "flags", std::int64_t(1)));
            stack.undo();
            stack.undo();
            EXPECT_FALSE(stack.isClean());
        }

        TEST(NifEditDocument, an_edit_that_changes_the_size_of_a_record_keeps_every_other_record)
        {
            const Built built = MwBuilder::make();
            Document document = Document::open(built.mBytes);
            const std::vector<std::string> before = Testing::dumpAll(built.mBytes);
            document.set(0, "name", std::string(500, 'n'));
            document.set(6, "file", std::string(""));
            document.set(8, "keys", std::vector<TextKey>{});
            const std::vector<std::string> after = Testing::dumpAll(document.buildChecked());
            ASSERT_EQ(after.size(), before.size());
            for (std::size_t i = 0; i < before.size(); ++i)
            {
                if (i != 0 && i != 6 && i != 8)
                    EXPECT_EQ(after[i], before[i]) << "record " << i;
            }
        }

        TEST(NifEditDocument, saving_twice_gives_identical_bytes)
        {
            Document document = Document::open(MwBuilder::make().mBytes);
            document.set(2, "name", std::string("Twice"));
            document.set(4, "diffuse", std::array<float, 3>{ 1.f, 0.f, 0.f });
            EXPECT_EQ(document.buildChecked(), document.buildChecked());
        }
    }
}
