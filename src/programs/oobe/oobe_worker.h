/* SPDX-License-Identifier: GPL-2.0-or-later
 * Shared production lifetime primitives for the native OOBE workers. */
#ifndef OOBE_WORKER_H
#define OOBE_WORKER_H

static inline bool OobeAcceptTerms(OobeComponentItem *comp, const char *digest,
                                  bool retrieved, bool agreed)
{
    size_t i;
    if (!comp || !retrieved || !agreed || !digest || strlen(digest) != 64)
        return false;
    for (i = 0; i < 64; ++i)
        if (!((digest[i] >= '0' && digest[i] <= '9') ||
              (digest[i] >= 'a' && digest[i] <= 'f'))) return false;
    memcpy(comp->sha256, digest, 65);
    memcpy(comp->accepted_sha256, digest, 65);
    comp->accepted = true;
    return true;
}

typedef struct {
    LONG ref_count;
    HWND hwnd;
    OobeComponentItem *comp;
    char candidate_sha256[65];
    WCHAR *terms_text;
    DWORD err;
    HANDLE hThread;
    LONG cancelled;
    LONG generation;
} TermsFetchContext;

static inline void TermsFetchContext_Release(TermsFetchContext *ctx)
{
    if (ctx && InterlockedDecrement(&ctx->ref_count) == 0) {
        if (ctx->hThread) CloseHandle(ctx->hThread);
        free(ctx->terms_text);
        free(ctx);
    }
}

typedef struct {
    LONG ref_count;
    HWND hwnd;
    HANDLE hThread;
    LONG cancelled;
    int current_idx;
    HANDLE hPromptEvent;
    int prompt_result;
    char prompt_msg[512];
    char status_msg[256];
    LONG generation;
} InstallState;

static inline void InstallState_Release(InstallState *state)
{
    if (state && InterlockedDecrement(&state->ref_count) == 0) {
        if (state->hThread) CloseHandle(state->hThread);
        if (state->hPromptEvent) CloseHandle(state->hPromptEvent);
        free(state);
    }
}

/* Review-only download worker state (embedded terms of disabled components).
 * One context per review dialog; all coordinator traffic (REVIEW_INFO,
 * START_REVIEW, QUERY_REVIEW, CANCEL_REVIEW) runs on the worker thread. The
 * dialog keeps one reference, the worker another; closing the dialog sets
 * cancelled and wakes hGoEvent, so the worker exits with bounded socket calls. */
typedef struct {
    LONG ref_count;
    HWND hwnd;                 /* NULL after the dialog is destroyed */
    OobeComponentItem *comp;   /* borrowed; owned by the caller */
    char op_id[96];            /* coordinator review operation id (max 73 chars) */
    ULONG64 total_bytes;       /* REVIEW_INFO: all packages combined */
    ULONG64 done_bytes;        /* latest QUERY_REVIEW progress */
    DWORD package_count;
    BOOL cached;               /* REVIEW_INFO: terms already in the local cache */
    char err_text[256];        /* human-readable last failure, for display */
    BOOL terms_ok;             /* verified terms are displayed in IDC_REV_TERMS */
    char terms_sha256[65];     /* fingerprint of the displayed terms */
    TermsFetchContext *terms;  /* dialog-owned reference to the cached-terms fetch */
    HANDLE hGoEvent;           /* auto-reset: download authorization / close wake-up */
    HANDLE hThread;
    DWORD err;                 /* latest worker phase outcome (Win32 error) */
    LONG cancelled;            /* dialog closed: the worker must terminate */
    LONG stopped;              /* "stop download" clicked: abort this attempt only */
    LONG generation;
} ReviewDownloadContext;

static inline BOOL ReviewContextStopRequested(ReviewDownloadContext *ctx)
{
    return ctx && (InterlockedCompareExchange(&ctx->cancelled, 0, 0) ||
                   InterlockedCompareExchange(&ctx->stopped, 0, 0));
}

static inline void ReviewDownloadContext_Release(ReviewDownloadContext *ctx)
{
    if (ctx && InterlockedDecrement(&ctx->ref_count) == 0) {
        if (ctx->hThread) CloseHandle(ctx->hThread);
        if (ctx->hGoEvent) CloseHandle(ctx->hGoEvent);
        free(ctx);
    }
}

/* Q3 shared controller seam for the embedded-review dialog's cached-terms
 * completion (OobeReviewProc WM_USER+101). The decision lives here so both the
 * production proc and the native test dispatch real completions through one
 * code path. It never touches window state and never records acceptance: it
 * only reports what UI transitions a live, in-generation completion authorizes.
 * A stale-generation tag (a superseded fetch) or a closed dialog (cancelled)
 * yields applied==FALSE, so the caller must not mutate the window, release the
 * live fetch, or accept anything. */
typedef struct {
    BOOL applied;        /* FALSE => stale generation or dialog closed: ignore entirely */
    BOOL fetch_ok;       /* terms fetched successfully (err==SUCCESS && text present) */
    BOOL disabled_row;   /* review-only component: no install-accept control at all */
    BOOL agree_enabled;  /* IDC_REV_AGREE enabled (install-eligible, fetch ok) */
    BOOL agree_checked;  /* BST state for IDC_REV_AGREE — always FALSE after the Q3 fix */
    BOOL ok_enabled;     /* IDOK enabled */
} ReviewTermsResult;

static inline void OobeReviewTermsCompletion(ReviewDownloadContext *ctx,
                                             TermsFetchContext *tctx,
                                             LONG msg_generation,
                                             ReviewTermsResult *out)
{
    out->applied = FALSE;
    out->fetch_ok = FALSE;
    out->disabled_row = FALSE;
    out->agree_enabled = FALSE;
    out->agree_checked = FALSE;
    out->ok_enabled = FALSE;
    if (!ctx || !tctx) return;
    if (tctx->generation != msg_generation) return;   /* stale completion: superseded fetch */
    if (InterlockedCompareExchange(&ctx->cancelled, 0, 0)) return;  /* dialog closed */
    out->applied = TRUE;
    out->fetch_ok = (tctx->err == ERROR_SUCCESS && tctx->terms_text != NULL);
    if (!out->fetch_ok) return;
    out->disabled_row = (ctx->comp != NULL && ctx->comp->disabled);
    if (out->disabled_row) return;  /* review-only: no accept control */
    out->agree_enabled = TRUE;
    out->agree_checked = FALSE;     /* NEVER pre-checked, even on a same-digest reopen */
    out->ok_enabled = FALSE;
}
#endif
