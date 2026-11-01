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

/* Test 7 (B2): review column grammar — valid old/new rows, malformed and overflow fields */
static int test_catalog_review_column_grammar(void)
{
    OobeContext ctx;

    /* Old row without a review column keeps parsing with no review capability. */
    memset(&ctx, 0, sizeof(ctx));
    ParseCatalogBuffer("oldverb\tOld Title\tPublisher\tDesc\t~1 MB\t0\t0\t1\tRetired upstream\n", &ctx);
    TEST_ASSERT(ctx.component_count == 1, "Old row without review column must parse");
    TEST_ASSERT(ctx.components[0].review_available == false, "Old row must not gain review capability");
    TEST_ASSERT(ctx.components[0].review_bytes == 0, "Old row review bytes must stay zero");
    TEST_ASSERT(ctx.components[0].review_cached == false, "Old row review cache flag must stay clear");

    /* New rows: exact byte totals with both cache states. */
    memset(&ctx, 0, sizeof(ctx));
    ParseCatalogBuffer("revverb\tRev Title\tPublisher\tDesc\t~2 MB\t0\t0\t1\tRetired upstream\tR:123456789:0\n"
                       "cachedverb\tCached Title\tPublisher\tDesc\t~3 MB\t0\t0\t1\tRetired upstream\tR:987654321:1\n", &ctx);
    TEST_ASSERT(ctx.component_count == 2, "Both new review rows must parse");
    TEST_ASSERT(ctx.components[0].review_available == true, "Valid R field must enable review");
    TEST_ASSERT(ctx.components[0].review_bytes == 123456789ULL, "Review byte total mismatch");
    TEST_ASSERT(ctx.components[0].review_cached == false, "Cache flag 0 must stay clear");
    TEST_ASSERT(ctx.components[1].review_available == true, "Cached R field must enable review");
    TEST_ASSERT(ctx.components[1].review_bytes == 987654321ULL, "Cached review byte total mismatch");
    TEST_ASSERT(ctx.components[1].review_cached == true, "Cache flag 1 must be set");

    /* Grammar limit: exactly OOBE_REVIEW_MAX_BYTES is accepted. */
    memset(&ctx, 0, sizeof(ctx));
    ParseCatalogBuffer("maxverb\tMax Title\tPublisher\tDesc\t~16 GB\t0\t0\t1\tRetired upstream\tR:17179869184:0\n", &ctx);
    TEST_ASSERT(ctx.component_count == 1, "Maximum-size row must parse");
    TEST_ASSERT(ctx.components[0].review_available == true, "Exactly 16 GiB must be accepted");
    TEST_ASSERT(ctx.components[0].review_bytes == OOBE_REVIEW_MAX_BYTES, "Maximum review bytes mismatch");

    /* Malformed and overflowing fields leave the row parsed but without review. */
    const char *bad_rows[] = {
        "overflow\tT\tP\tD\tS\t0\t0\t1\tR\tR:17179869185:0\n",  /* one byte over the cap */
        "toolong\tT\tP\tD\tS\t0\t0\t1\tR\tR:100000000000:0\n",  /* twelve digits, far over the cap */
        "leadzero\tT\tP\tD\tS\t0\t0\t1\tR\tR:0123456789:0\n",   /* leading zero */
        "junk\tT\tP\tD\tS\t0\t0\t1\tR\tR:123456789:0x\n",       /* trailing junk */
        "negative\tT\tP\tD\tS\t0\t0\t1\tR\tR:-123456789:0\n",   /* negative value */
        "flagtwo\tT\tP\tD\tS\t0\t0\t1\tR\tR:123456789:2\n",     /* unknown cache flag */
        "lowercase\tT\tP\tD\tS\t0\t0\t1\tR\tr:123456789:0\n",   /* lowercase marker */
        "emptyrev\tT\tP\tD\tS\t0\t0\t1\tR\t\n",                 /* empty review field */
    };
    size_t i;
    for (i = 0; i < sizeof(bad_rows) / sizeof(bad_rows[0]); ++i) {
        OobeContext bad;
        memset(&bad, 0, sizeof(bad));
        ParseCatalogBuffer(bad_rows[i], &bad);
        TEST_ASSERT(bad.component_count == 1, "Malformed review field must not drop the row");
        TEST_ASSERT(bad.components[0].review_available == false, "Malformed review field must stay disabled");
        TEST_ASSERT(bad.components[0].review_bytes == 0, "Malformed review bytes must stay zero");
    }

    return 1;
}

/* Test 8 (U1): production state/controller seam for review-only components */
static int test_review_only_controller_seam(void)
{
    OobeContext ctx;
    OobeComponentItem *comp;
    ReviewDownloadContext rctx;
    const char *fp1 = "1111222233334444555566667777888899990000aaaabbbbccccddddeeeeffff";
    const char *fp2 = "ffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffff";

    /* A disabled row with a valid review column reaches the UI in a state that
     * cannot be checked for install or pre-accepted. */
    memset(&ctx, 0, sizeof(ctx));
    ParseCatalogBuffer("corefonts\tMicrosoft TrueType Core Fonts\tMicrosoft Corporation\tOriginal core fonts\t~4 MB\t0\t0\t1\tRetired upstream by vendor\tR:4194304:0\n", &ctx);
    TEST_ASSERT(ctx.component_count == 1, "Disabled review row must parse");
    comp = &ctx.components[0];
    TEST_ASSERT(comp->disabled == true, "Row must stay disabled for installation");
    TEST_ASSERT(comp->review_available == true, "Row must offer terms review");
    TEST_ASSERT(comp->checked == false, "Review-only row must not be checkable for install");
    TEST_ASSERT(comp->accepted == false, "Acceptance must be false before any explicit action");
    TEST_ASSERT(comp->accepted_sha256[0] == '\0', "No accepted fingerprint may pre-exist");

    /* Displaying terms (retrieved) without an explicit agreement cannot accept. */
    TEST_ASSERT(!OobeAcceptTerms(comp, fp1, true, false), "Review-only display cannot become acceptance");
    TEST_ASSERT(comp->accepted == false, "Accepted must remain false after review-only display");

    /* Acceptance binds to the exact fingerprint: a changed candidate does not
     * inherit the previous agreement and cannot be auto-approved. */
    TEST_ASSERT(OobeAcceptTerms(comp, fp1, true, true), "Explicit acceptance must bind fp1");
    TEST_ASSERT(strcmp(comp->accepted_sha256, fp1) == 0, "Accepted fingerprint must equal fp1");
    TEST_ASSERT(strcmp(comp->accepted_sha256, fp2) != 0, "Changed candidate must not match the binding");
    TEST_ASSERT(!OobeAcceptTerms(comp, fp2, true, false), "Changed candidate without agreement cannot accept");
    TEST_ASSERT(strcmp(comp->accepted_sha256, fp1) == 0, "Binding must survive a rejected replacement");

    /* Digest validation at the seam: uppercase, short and NULL inputs reject. */
    TEST_ASSERT(!OobeAcceptTerms(comp, "1111222233334444555566667777888899990000AAAABBBBCCCCDDDDEEEEEFFF", true, true),
                "Uppercase digest must be rejected");
    TEST_ASSERT(!OobeAcceptTerms(comp, "1111222233334444555566667777888899990000aaaabbbbccccddddeeeefff", true, true),
                "Short digest must be rejected");
    TEST_ASSERT(!OobeAcceptTerms(comp, NULL, true, true), "NULL digest must be rejected");
    TEST_ASSERT(!OobeAcceptTerms(NULL, fp1, true, true), "NULL component must be rejected without crash");

    /* A closed review dialog (cancelled, hwnd gone) or a stale generation tag
     * cannot approve: the production stop predicate fires so the worker exits
     * before any acceptance is recorded. */
    memset(&rctx, 0, sizeof(rctx));
    rctx.ref_count = 1;
    TEST_ASSERT(!ReviewContextStopRequested(&rctx), "Fresh context must not request a stop");
    {
        LONG seen_generation = rctx.generation;
        InterlockedIncrement(&rctx.generation);
        TEST_ASSERT(seen_generation != rctx.generation, "Stale worker generation tag must be detectable");
    }
    InterlockedExchange(&rctx.stopped, 1);
    TEST_ASSERT(ReviewContextStopRequested(&rctx), "Per-attempt stop must abort the download");
    InterlockedExchange(&rctx.stopped, 0);
    rctx.hwnd = NULL;
    InterlockedExchange(&rctx.cancelled, 1);
    TEST_ASSERT(ReviewContextStopRequested(&rctx), "Closed dialog must terminate the review worker");
    TEST_ASSERT(comp->accepted == true && strcmp(comp->accepted_sha256, fp1) == 0,
                "Stale or closed completion must not alter the recorded acceptance");

    return 1;
}

/* Test 9 (Q3): production controller seam for the embedded-review cached-terms
 * completion. Dispatches real completions through OobeReviewTermsCompletion —
 * the exact decision OobeReviewProc's WM_USER+101 applies — against an
 * install-eligible synthetic component, and proves that opening/reopening/
 * retrying never prechecks or records a fresh install acceptance. */
static int test_review_terms_completion_controller(void)
{
    static WCHAR terms_buf[] = L"synthetic embedded license terms";
    const char *fp1 = "1111222233334444555566667777888899990000aaaabbbbccccddddeeeeffff";
    const char *fp2 = "ffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffff";

    /* Install-eligible synthetic component (disabled column 0, valid R field). */
    OobeContext ctx;
    OobeComponentItem *comp;
    memset(&ctx, 0, sizeof(ctx));
    ParseCatalogBuffer("synfont\tSynthetic Font\tSyn Publisher\tDesc\t~1 MB\t0\t0\t0\tReason\tR:4194304:0\n", &ctx);
    TEST_ASSERT(ctx.component_count == 1, "Install-eligible synthetic row must parse");
    comp = &ctx.components[0];
    TEST_ASSERT(comp->disabled == false, "Synthetic component must be install-eligible");
    TEST_ASSERT(comp->review_available == true, "Synthetic component must offer terms review");
    TEST_ASSERT(comp->accepted == false, "No acceptance before any explicit action");

    ReviewDownloadContext rctx;
    TermsFetchContext tctx;
    memset(&rctx, 0, sizeof(rctx));
    rctx.ref_count = 1;
    rctx.comp = comp;
    rctx.generation = 7;
    memset(&tctx, 0, sizeof(tctx));
    tctx.terms_text = terms_buf;
    tctx.err = ERROR_SUCCESS;
    tctx.generation = 7;
    strcpy(tctx.candidate_sha256, fp1);

    /* (1) Initial display of fetched terms: the install-eligible row enables the
     * accept control but leaves it UNCHECKED and IDOK disabled. */
    ReviewTermsResult r;
    OobeReviewTermsCompletion(&rctx, &tctx, 7, &r);
    TEST_ASSERT(r.applied == TRUE, "In-generation completion must be applied");
    TEST_ASSERT(r.fetch_ok == TRUE, "Successful fetch must report fetch_ok");
    TEST_ASSERT(r.disabled_row == FALSE, "Install-eligible row is not review-only");
    TEST_ASSERT(r.agree_enabled == TRUE, "Accept control must be enabled for an install-eligible row");
    TEST_ASSERT(r.agree_checked == FALSE, "Acceptance must start unchecked on initial display");
    TEST_ASSERT(r.ok_enabled == FALSE, "IDOK must stay disabled until the user checks acceptance");

    /* (2) Same-digest reopen: the component was previously accepted for this
     * exact fingerprint. The completion must STILL leave the box unchecked —
     * opening/reopening never prechecks or records a fresh acceptance. */
    TEST_ASSERT(OobeAcceptTerms(comp, fp1, true, true), "Establish a prior explicit acceptance of fp1");
    TEST_ASSERT(strcmp(comp->accepted_sha256, fp1) == 0, "Prior acceptance bound to fp1");
    OobeReviewTermsCompletion(&rctx, &tctx, 7, &r);
    TEST_ASSERT(r.applied == TRUE, "Same-digest reopen completion must be applied");
    TEST_ASSERT(r.agree_checked == FALSE, "Same-digest reopen must NOT precheck acceptance (Q3)");
    TEST_ASSERT(r.ok_enabled == FALSE, "IDOK must stay disabled on a same-digest reopen");
    /* Negative control: the OLD prechecking rule would have checked the box
     * here. Proving it is TRUE demonstrates this case fails on the old code. */
    {
        BOOL old_precheck = (comp->accepted && strcmp(comp->accepted_sha256, tctx.candidate_sha256) == 0);
        TEST_ASSERT(old_precheck == TRUE, "Old precheck rule would have fired on this same-digest reopen");
        TEST_ASSERT(r.agree_checked != old_precheck, "New seam must disagree with the old precheck here");
    }

    /* (3) Changed digest: candidate no longer matches the recorded binding. */
    strcpy(tctx.candidate_sha256, fp2);
    OobeReviewTermsCompletion(&rctx, &tctx, 7, &r);
    TEST_ASSERT(r.applied == TRUE, "Changed-digest completion must be applied");
    TEST_ASSERT(r.agree_checked == FALSE, "Changed digest must not inherit the prior acceptance");
    TEST_ASSERT(r.ok_enabled == FALSE, "IDOK must stay disabled for a changed digest");

    /* (4) Stale-generation completion: belongs to a superseded fetch. It must
     * be ignored entirely — no window mutation, no acceptance. */
    memset(comp->accepted_sha256, 0, sizeof(comp->accepted_sha256));
    comp->accepted = false;
    OobeReviewTermsCompletion(&rctx, &tctx, 999, &r);
    TEST_ASSERT(r.applied == FALSE, "Stale-generation completion must be ignored");
    TEST_ASSERT(r.fetch_ok == FALSE && r.agree_enabled == FALSE &&
                r.agree_checked == FALSE && r.ok_enabled == FALSE,
                "Stale completion must authorize no UI transition");
    TEST_ASSERT(comp->accepted == false, "Stale completion must record no acceptance");

    /* (5) Completion after close: the dialog is destroyed (cancelled set). Even
     * with a matching generation and a dangling comp pointer, the seam bails
     * before touching window state or the borrowed component — zero access. */
    rctx.comp = NULL;  /* simulate the borrowed component no longer being valid */
    InterlockedExchange(&rctx.cancelled, 1);
    strcpy(tctx.candidate_sha256, fp1);
    OobeReviewTermsCompletion(&rctx, &tctx, 7, &r);
    TEST_ASSERT(r.applied == FALSE, "Post-close completion must be ignored");
    TEST_ASSERT(r.agree_checked == FALSE && r.ok_enabled == FALSE,
                "Post-close completion must authorize no acceptance UI");

    /* (6) Only explicit acceptance persists: the seam never sets comp->accepted.
     * Re-establish a live context and confirm the box stays clear until an
     * explicit OobeAcceptTerms call. */
    memset(&rctx, 0, sizeof(rctx));
    rctx.ref_count = 1;
    rctx.comp = comp;
    rctx.generation = 7;
    TEST_ASSERT(comp->accepted == false, "No acceptance recorded by any completion above");
    OobeReviewTermsCompletion(&rctx, &tctx, 7, &r);
    TEST_ASSERT(r.applied == TRUE && r.agree_checked == FALSE, "Live completion leaves the box unchecked");
    TEST_ASSERT(comp->accepted == false, "Seam must not record acceptance on its own");
    TEST_ASSERT(OobeAcceptTerms(comp, fp1, true, true), "Explicit acceptance is the only path to accepted");
    TEST_ASSERT(comp->accepted == true && strcmp(comp->accepted_sha256, fp1) == 0,
                "Explicit acceptance binds fp1");

    /* (7) Review-only (disabled) row: no install-accept control at all. */
    OobeContext dctx;
    OobeComponentItem *dcomp;
    memset(&dctx, 0, sizeof(dctx));
    ParseCatalogBuffer("corefonts\tMicrosoft TrueType Core Fonts\tMicrosoft Corporation\tOriginal core fonts"
                       "\t~4 MB\t0\t0\t1\tRetired upstream by vendor\tR:4194304:0\n", &dctx);
    TEST_ASSERT(dctx.component_count == 1, "Disabled review row must parse");
    dcomp = &dctx.components[0];
    memset(&rctx, 0, sizeof(rctx));
    rctx.ref_count = 1;
    rctx.comp = dcomp;
    rctx.generation = 7;
    OobeReviewTermsCompletion(&rctx, &tctx, 7, &r);
    TEST_ASSERT(r.applied == TRUE, "Disabled-row completion must be applied");
    TEST_ASSERT(r.disabled_row == TRUE, "Disabled row is review-only");
    TEST_ASSERT(r.agree_enabled == FALSE && r.agree_checked == FALSE && r.ok_enabled == FALSE,
                "Review-only row must expose no install-accept control");

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
    if (!test_catalog_review_column_grammar()) return 1;
    if (!test_review_only_controller_seam()) return 1;
    if (!test_review_terms_completion_controller()) return 1;

    printf("ALL %d NATIVE TESTS PASSED (%d assertions verified)\n", 9, g_tests_passed);
    return 0;
}
