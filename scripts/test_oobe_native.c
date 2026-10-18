/*
 * Production native test seam for OOBE components parser, controller and worker lifecycle.
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include <windows.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>
#include <assert.h>

#include "../src/programs/oobe/oobe_catalog.h"

static LONG released_blocks;
static void tracked_free(void *block)
{
    if (block) InterlockedIncrement(&released_blocks);
    free(block);
}
#define free tracked_free
#include "../src/programs/oobe/oobe_worker.h"
#undef free

static int g_tests_run = 0;
static int g_tests_passed = 0;

#define TEST_ASSERT(cond, msg) do { \
    g_tests_run++; \
    if (!(cond)) { \
        fprintf(stderr, "FAIL: %s (line %d): %s\n", __FUNCTION__, __LINE__, msg); \
        return 0; \
    } else { \
        g_tests_passed++; \
    } \
} while(0)

/* Test 1: Six rows parsed in order with correct metadata and disabled flags */
static int test_catalog_six_rows(void)
{
    const char *catalog =
        "vcrun2022\tVisual C++ 2015-2022 runtime\tMicrosoft Corporation\tVisual C++ runtime\t~14 MB\t0\t0\t1\tUpstream mutable URL divergence\n"
        "corefonts\tMicrosoft TrueType Core Fonts\tMicrosoft Corporation\tOriginal core fonts\t~4 MB\t0\t0\t1\tRetired upstream by vendor\n"
        "tahoma\tMicrosoft Tahoma font\tMicrosoft Corporation\tTahoma fonts\t~410 KB\t0\t0\t1\tIE language pack payload terms require extraction\n"
        "d3dcompiler_47\tDirectX Shader Compiler\tMicrosoft Corporation\tShader compiler\t~4.5 MB\t0\t0\t1\tRedistributable package lacks a standalone vendor terms URL\n"
        "xact\tMicrosoft XACT audio engine\tMicrosoft Corporation\tAudio engine\t~95 MB\t0\t0\t1\tVendor terms are embedded inside redistributable\n"
        "physx\tNVIDIA PhysX System Software\tNVIDIA Corporation\tPhysics engine\t~27 MB\t0\t0\t1\tMarked broken upstream in winetricks\n";

    OobeContext ctx;
    memset(&ctx, 0, sizeof(ctx));
    ParseCatalogBuffer(catalog, &ctx);

    TEST_ASSERT(ctx.component_count == 6, "Expected exactly 6 parsed components");
    TEST_ASSERT(strcmp(ctx.components[0].verb, "vcrun2022") == 0, "Component 0 verb mismatch");
    TEST_ASSERT(ctx.components[0].disabled == true, "Component 0 must be disabled");
    TEST_ASSERT(strstr(ctx.components[0].disabled_reason, "Upstream mutable URL") != NULL, "Component 0 reason mismatch");

    TEST_ASSERT(strcmp(ctx.components[1].verb, "corefonts") == 0, "Component 1 verb mismatch");
    TEST_ASSERT(ctx.components[1].disabled == true, "Component 1 must be disabled");
    TEST_ASSERT(strstr(ctx.components[1].disabled_reason, "Retired upstream") != NULL, "Component 1 reason mismatch");

    TEST_ASSERT(strcmp(ctx.components[2].verb, "tahoma") == 0, "Component 2 verb mismatch");
    TEST_ASSERT(ctx.components[2].disabled == true, "Component 2 must be disabled");

    TEST_ASSERT(strcmp(ctx.components[3].verb, "d3dcompiler_47") == 0, "Component 3 verb mismatch");
    TEST_ASSERT(ctx.components[3].disabled == true, "Component 3 must be disabled");

    TEST_ASSERT(strcmp(ctx.components[4].verb, "xact") == 0, "Component 4 verb mismatch");
    TEST_ASSERT(ctx.components[4].disabled == true, "Component 4 must be disabled");

    TEST_ASSERT(strcmp(ctx.components[5].verb, "physx") == 0, "Component 5 verb mismatch");
    TEST_ASSERT(ctx.components[5].disabled == true, "Component 5 must be disabled");

    return 1;
}

/* Test 2: Empty fields in rows handled safely */
static int test_catalog_empty_fields(void)
{
    const char *catalog = "testverb\tTest Title\tPublisher\t\t\t0\t0\t0\t\n";
    OobeContext ctx;
    memset(&ctx, 0, sizeof(ctx));
    ParseCatalogBuffer(catalog, &ctx);

    TEST_ASSERT(ctx.component_count == 1, "Expected 1 parsed component with empty fields");
    TEST_ASSERT(strcmp(ctx.components[0].verb, "testverb") == 0, "Verb mismatch");
    TEST_ASSERT(ctx.components[0].description[0] == '\0', "Description should be empty string");
    TEST_ASSERT(ctx.components[0].size[0] == '\0', "Size should be empty string");
    TEST_ASSERT(ctx.components[0].disabled == false, "Disabled flag should be false");
    TEST_ASSERT(ctx.components[0].disabled_reason[0] == '\0', "Disabled reason should be empty string");

    return 1;
}

/* Test 3: Malformed rows and oversized lines skipped safely */
static int test_catalog_malformed_and_oversized(void)
{
    char oversized[4096];
    memset(oversized, 'A', sizeof(oversized) - 1);
    oversized[sizeof(oversized) - 1] = '\0';

    OobeContext ctx;
    memset(&ctx, 0, sizeof(ctx));

    /* Missing required columns */
    ParseCatalogBuffer("only\ttwo\tcolumns\n", &ctx);
    TEST_ASSERT(ctx.component_count == 0, "Row with too few columns must be skipped");

    /* Empty verb column */
    ParseCatalogBuffer("\tTitle\tPublisher\tDesc\tSize\t0\t0\n", &ctx);
    TEST_ASSERT(ctx.component_count == 0, "Row with empty verb must be skipped");

    /* Oversized single row without newline */
    ParseCatalogBuffer(oversized, &ctx);
    TEST_ASSERT(ctx.component_count == 0, "Oversized row without columns must be skipped safely");

    return 1;
}

/* Test 4: UTF-8 conversion and Win32 failure fail-closed safety */
static int test_utf8_conversion_fail_closed(void)
{
    char out[256];

    /* Valid UTF-8 ASCII-compatible */
    Utf8ToAnsi("Microsoft Corporation", out, sizeof(out));
    TEST_ASSERT(strcmp(out, "Microsoft Corporation") == 0, "Valid ASCII conversion mismatch");

    /* Invalid UTF-8 byte sequences */
    memset(out, 'X', sizeof(out));
    Utf8ToAnsi("\xff\xfe\x80\x81", out, sizeof(out));
    TEST_ASSERT(out[0] == '\0', "Invalid UTF-8 must fail closed and clear buffer");

    /* NULL and edge cases */
    memset(out, 'Y', sizeof(out));
    Utf8ToAnsi(NULL, out, sizeof(out));
    TEST_ASSERT(out[0] == '\0', "NULL input must clear output buffer");

    Utf8ToAnsi("text", NULL, 0); /* Must not crash */
    TEST_ASSERT(1, "NULL output handled without crash");

    return 1;
}

/* Test 5: Candidate and accepted digest separation and rejection on cancel */
static int test_candidate_accepted_digest_separation(void)
{
    OobeComponentItem comp;
    memset(&comp, 0, sizeof(comp));
    strcpy(comp.verb, "corefonts");

    /* Initially not accepted */
    TEST_ASSERT(comp.accepted == false, "Initially accepted must be false");
    TEST_ASSERT(comp.accepted_sha256[0] == '\0', "Initially accepted_sha256 must be empty");

    /* Candidate retrieved over network */
    const char *candidate = "1111222233334444555566667777888899990000aaaabbbbccccddddeeeeffff";

    /* User cancels dialog: candidate must NOT be accepted */
    TEST_ASSERT(comp.accepted == false, "After dialog cancel, accepted must remain false");
    TEST_ASSERT(comp.accepted_sha256[0] == '\0', "After dialog cancel, accepted_sha256 must remain empty");

    /* User accepts terms explicitly: copy candidate to accepted */
    TEST_ASSERT(!OobeAcceptTerms(&comp, candidate, true, false), "Cancel cannot accept terms");
    TEST_ASSERT(!OobeAcceptTerms(&comp, candidate, false, true), "Missing terms cannot be accepted");
    TEST_ASSERT(!OobeAcceptTerms(&comp, "bad", true, true), "Invalid digest cannot be accepted");
    TEST_ASSERT(OobeAcceptTerms(&comp, candidate, true, true), "Explicit acceptance failed");

    TEST_ASSERT(comp.accepted == true, "After accept, accepted must be true");
    TEST_ASSERT(strcmp(comp.accepted_sha256, candidate) == 0, "Accepted SHA mismatch");

    /* If new candidate is fetched but user cancels, accepted SHA does NOT change */
    const char *new_candidate = "ffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffff";
    /* User cancelled dialog with new candidate */
    TEST_ASSERT(!OobeAcceptTerms(&comp, new_candidate, true, false), "Cancelled replacement cannot be accepted");
    TEST_ASSERT(strcmp(comp.accepted_sha256, candidate) == 0, "Previous accepted SHA must not be overwritten on cancel");
    TEST_ASSERT(strcmp(comp.accepted_sha256, new_candidate) != 0, "Cancelled new candidate must not become accepted SHA");

    return 1;
}

/* Test 6: Deterministic worker reference-counting and lifetime verification */
static HANDLE worker_gate;
static DWORD WINAPI delayed_terms_worker(void *arg)
{
    TermsFetchContext *ctx = arg;
    WaitForSingleObject(worker_gate, INFINITE);
    TermsFetchContext_Release(ctx);
    return 0;
}

static int test_worker_lifetime_reference_counting(void)
{
    TermsFetchContext *ctx = calloc(1, sizeof(*ctx));
    InstallState *state;
    HANDLE waiter;
    LONG before = released_blocks;
    TEST_ASSERT(ctx != NULL, "Allocate production context");
    ctx->ref_count = 2;
    ctx->terms_text = malloc(64);
    TEST_ASSERT(ctx->terms_text != NULL, "Allocate production text");
    wcscpy(ctx->terms_text, L"active text");
    worker_gate = CreateEventA(NULL, TRUE, FALSE, NULL);
    TEST_ASSERT(worker_gate != NULL, "Create delayed-worker gate");
    ctx->hThread = CreateThread(NULL, 0, delayed_terms_worker, ctx, 0, NULL);
    TEST_ASSERT(ctx->hThread != NULL, "Start actual worker");
    TEST_ASSERT(DuplicateHandle(GetCurrentProcess(), ctx->hThread,
                               GetCurrentProcess(), &waiter, 0, FALSE,
                               DUPLICATE_SAME_ACCESS), "Keep independent join handle");
    InterlockedExchange(&ctx->cancelled, 1);
    TermsFetchContext_Release(ctx);
    TEST_ASSERT(ctx->ref_count == 1, "Worker retains context after UI closes");
    TEST_ASSERT(released_blocks == before, "No premature production free");
    TEST_ASSERT(wcscmp(ctx->terms_text, L"active text") == 0, "Worker text still alive");
    SetEvent(worker_gate);
    TEST_ASSERT(WaitForSingleObject(waiter, 5000) == WAIT_OBJECT_0, "Late worker finishes");
    TEST_ASSERT(released_blocks == before + 2, "Production context and text freed exactly once");
    CloseHandle(waiter);
    CloseHandle(worker_gate);

    state = calloc(1, sizeof(*state));
    TEST_ASSERT(state != NULL, "Allocate production install state");
    state->ref_count = 2;
    state->hPromptEvent = CreateEventA(NULL, TRUE, FALSE, NULL);
    /* Real production releases when CreateThread fails: drop both references. */
    InstallState_Release(state);
    TEST_ASSERT(released_blocks == before + 2, "Reserved UI reference remains");
    InstallState_Release(state);
    TEST_ASSERT(released_blocks == before + 3, "Failed worker state freed exactly once");

    return 1;
}

int main(int argc, char **argv)
{
    (void)argc;
    (void)argv;
    printf("=== Running OOBE Native Production Code Tests ===\n");

    if (!test_catalog_six_rows()) return 1;
    if (!test_catalog_empty_fields()) return 1;
    if (!test_catalog_malformed_and_oversized()) return 1;
    if (!test_utf8_conversion_fail_closed()) return 1;
    if (!test_candidate_accepted_digest_separation()) return 1;
    if (!test_worker_lifetime_reference_counting()) return 1;

    printf("ALL %d NATIVE TESTS PASSED (%d assertions verified)\n", 6, g_tests_passed);
    return 0;
}
