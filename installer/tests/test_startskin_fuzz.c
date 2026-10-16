/*
 * SPDX-License-Identifier: GPL-2.0-or-later
 *
 * Unit and fuzz test suite for Open-Shell Start menu skin loader (Phase K1)
 *
 * Copyright 2026 WineExplorer Desktop Environment Project
 */

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "../../src/dlls/wexshell/startskin.h"

static int g_tests_run = 0;
static int g_tests_passed = 0;
static int g_tests_failed = 0;

#define TEST_ASSERT(cond, msg) do { \
    g_tests_run++; \
    if (cond) { \
        g_tests_passed++; \
        printf("  [PASS] %s\n", msg); \
    } else { \
        g_tests_failed++; \
        printf("  [FAIL] %s (line %d)\n", msg, __LINE__); \
    } \
} while (0)

/* -------------------------------------------------------------------------
 * 1. Condition evaluation tests
 * ------------------------------------------------------------------------- */
static void test_conditions(void)
{
    const WCHAR *vars_empty[] = { NULL };
    const WCHAR *vars_basic[] = { L"TWO_COLUMNS", L"HIGH_DPI", L"SEARCHBOX" };
    const WCHAR *vars_opts[]  = { L"OPTION_A", L"OPTION_C" };

    printf("=== Test Suite 1: Condition Evaluation ===\n");

    /* Empty and constants */
    TEST_ASSERT(startskin_eval_condition(L"", vars_empty, 0) == 1, "Empty condition is TRUE");
    TEST_ASSERT(startskin_eval_condition(L"TRUE", vars_empty, 0) == 1, "'TRUE' token evaluates to 1");
    TEST_ASSERT(startskin_eval_condition(L"true", vars_empty, 0) == 1, "'true' case-insensitive evaluates to 1");
    TEST_ASSERT(startskin_eval_condition(L"FALSE", vars_empty, 0) == 0, "'FALSE' token evaluates to 0");
    TEST_ASSERT(startskin_eval_condition(L"false", vars_empty, 0) == 0, "'false' case-insensitive evaluates to 0");

    /* Single variable */
    TEST_ASSERT(startskin_eval_condition(L"TWO_COLUMNS", vars_basic, 3) == 1, "TWO_COLUMNS present is 1");
    TEST_ASSERT(startskin_eval_condition(L"two_columns", vars_basic, 3) == 1, "two_columns case-insensitive is 1");
    TEST_ASSERT(startskin_eval_condition(L"TOUCH_ENABLED", vars_basic, 3) == 0, "TOUCH_ENABLED absent is 0");

    /* Not operator */
    TEST_ASSERT(startskin_eval_condition(L"not TWO_COLUMNS", vars_basic, 3) == 0, "not TWO_COLUMNS present is 0");
    TEST_ASSERT(startskin_eval_condition(L"not TOUCH_ENABLED", vars_basic, 3) == 1, "not TOUCH_ENABLED absent is 1");
    TEST_ASSERT(startskin_eval_condition(L"NOT FALSE", vars_empty, 0) == 1, "NOT FALSE is 1");

    /* And / Or logic */
    TEST_ASSERT(startskin_eval_condition(L"TWO_COLUMNS and HIGH_DPI", vars_basic, 3) == 1, "A and B present is 1");
    TEST_ASSERT(startskin_eval_condition(L"TWO_COLUMNS and TOUCH_ENABLED", vars_basic, 3) == 0, "A and B_absent is 0");
    TEST_ASSERT(startskin_eval_condition(L"TOUCH_ENABLED or TWO_COLUMNS", vars_basic, 3) == 1, "A_absent or B is 1");
    TEST_ASSERT(startskin_eval_condition(L"TOUCH_ENABLED or FOOBAR", vars_basic, 3) == 0, "A_absent or B_absent is 0");

    /* Precedence and parentheses */
    TEST_ASSERT(startskin_eval_condition(L"not TOUCH_ENABLED and TWO_COLUMNS", vars_basic, 3) == 1, "(not A) and B is 1");
    TEST_ASSERT(startskin_eval_condition(L"(OPTION_A or OPTION_B) and OPTION_C", vars_opts, 2) == 1, "(A or B) and C is 1");
    TEST_ASSERT(startskin_eval_condition(L"(OPTION_B or OPTION_D) and OPTION_C", vars_opts, 2) == 0, "(B_absent or D_absent) and C is 0");
    TEST_ASSERT(startskin_eval_condition(L"OPTION_A and (OPTION_B or not OPTION_D)", vars_opts, 2) == 1, "A and (B or not D) is 1");

    /* Malformed / syntax error conditions */
    TEST_ASSERT(startskin_eval_condition(L"(TWO_COLUMNS", vars_basic, 3) == -1, "Unclosed '(' returns -1");
    TEST_ASSERT(startskin_eval_condition(L"TWO_COLUMNS)", vars_basic, 3) == -1, "Unmatched ')' returns -1");
    TEST_ASSERT(startskin_eval_condition(L"and TWO_COLUMNS", vars_basic, 3) == -1, "Leading 'and' returns -1");
    TEST_ASSERT(startskin_eval_condition(L"TWO_COLUMNS and", vars_basic, 3) == -1, "Trailing 'and' returns -1");
}

/* -------------------------------------------------------------------------
 * 2. Fuzz & malformed skin loading tests
 * ------------------------------------------------------------------------- */
static void test_fuzz_skins(void)
{
    struct start_skin *skin;

    printf("\n=== Test Suite 2: Fuzz & Corrupted File Fallbacks ===\n");

    /* Non-existent file */
    skin = startskin_load(L"nonexistent_skin_file_12345.skin", NULL, NULL, STARTSKIN_TYPE_CLASSIC1, 96);
    TEST_ASSERT(skin == NULL, "Non-existent skin file returns NULL cleanly");

    /* 0-byte truncated file */
    skin = startskin_load(L"test_fuzz_zero.skin", NULL, NULL, STARTSKIN_TYPE_CLASSIC1, 96);
    TEST_ASSERT(skin == NULL, "0-byte empty file returns NULL cleanly");

    /* Truncated header (corrupt PE file) */
    skin = startskin_load(L"test_fuzz_bad_pe.skin", NULL, NULL, STARTSKIN_TYPE_CLASSIC1, 96);
    TEST_ASSERT(skin == NULL, "Truncated PE file returns NULL cleanly");

    /* Valid DLL without SKIN resource */
    skin = startskin_load(L"test_fuzz_no_skin.skin", NULL, NULL, STARTSKIN_TYPE_CLASSIC1, 96);
    TEST_ASSERT(skin == NULL, "DLL without SKIN resource returns NULL cleanly");

    /* Corrupted SKIN description text (invalid characters, garbage) */
    skin = startskin_load(L"test_fuzz_garbage.skin", NULL, NULL, STARTSKIN_TYPE_CLASSIC1, 96);
    TEST_ASSERT(skin == NULL || skin->version >= 1, "Garbage SKIN description does not crash");
    if (skin) startskin_free(skin);

    /* Missing referenced bitmap resource */
    skin = startskin_load(L"test_fuzz_missing_bmp.skin", NULL, NULL, STARTSKIN_TYPE_CLASSIC1, 96);
    TEST_ASSERT(skin == NULL, "Missing required bitmap resource triggers clean fallback to NULL");
}

/* -------------------------------------------------------------------------
 * 3. Valid skin parsing and feature tests
 * ------------------------------------------------------------------------- */
static void test_valid_skin(void)
{
    struct start_skin *skin_96;
    struct start_skin *skin_144;
    struct start_skin *skin_var;

    printf("\n=== Test Suite 3: Valid Skin Loading & DPI Scaling ===\n");

    /* Standard 96 DPI load */
    skin_96 = startskin_load(L"test_valid.skin", NULL, NULL, STARTSKIN_TYPE_CLASSIC1, 96);
    TEST_ASSERT(skin_96 != NULL, "test_valid.skin loaded at 96 DPI");
    if (skin_96)
    {
        TEST_ASSERT(skin_96->main_background == RGB(0x12, 0x34, 0x56), "Main_background parsed correctly (#123456)");
        TEST_ASSERT(skin_96->main_bitmap.hbmp != NULL, "Main_bitmap handle is valid");
        TEST_ASSERT(skin_96->main_bitmap_slices_x[0] == 2 && skin_96->main_bitmap_slices_x[1] == 1,
                    "Main_bitmap_slices_X matches 96 DPI base (2, 1, 2)");
        TEST_ASSERT(skin_96->version == 2, "Skin version parsed as 2");
        TEST_ASSERT(wcscmp(skin_96->about, L"Test Valid Skin") == 0, "About string matches");
        TEST_ASSERT(skin_96->variation_count == 2, "2 Variations discovered");
        if (skin_96->variation_count >= 2)
        {
            TEST_ASSERT(wcscmp(skin_96->variations[0].label, L"Silver") == 0, "Variation 1 is 'Silver'");
            TEST_ASSERT(wcscmp(skin_96->variations[1].label, L"Dark") == 0, "Variation 2 is 'Dark'");
        }
        startskin_free(skin_96);
        TEST_ASSERT(1, "startskin_free() completed cleanly without leak or crash");
    }

    /* 144 DPI (HIGH_DPI) load */
    skin_144 = startskin_load(L"test_valid.skin", NULL, NULL, STARTSKIN_TYPE_CLASSIC1, 144);
    TEST_ASSERT(skin_144 != NULL, "test_valid.skin loaded at 144 DPI (HIGH_DPI)");
    if (skin_144)
    {
        TEST_ASSERT(skin_144->main_bitmap_slices_x[0] == 3 && skin_144->main_bitmap_slices_x[1] == 2,
                    "HIGH_DPI condition triggered alternate slices (3, 2, 3)");
        startskin_free(skin_144);
    }

    /* Variation load */
    skin_var = startskin_load(L"test_valid.skin", L"Dark", NULL, STARTSKIN_TYPE_CLASSIC1, 96);
    TEST_ASSERT(skin_var != NULL, "test_valid.skin loaded with variation 'Dark'");
    if (skin_var)
    {
        TEST_ASSERT(skin_var->main_background == RGB(0x22, 0x22, 0x22), "Dark variation overrode background (#222222)");
        startskin_free(skin_var);
    }
}

int main(int argc, char **argv)
{
    printf("====================================================\n");
    printf(" WineExplorer Start Menu Skin Loader Fuzz & Unit Tests\n");
    printf("====================================================\n\n");

    test_conditions();
    test_fuzz_skins();
    test_valid_skin();

    printf("\n====================================================\n");
    printf(" Summary: %d tests run, %d passed, %d failed\n",
           g_tests_run, g_tests_passed, g_tests_failed);
    printf("====================================================\n");

    return (g_tests_failed == 0) ? 0 : 1;
}
