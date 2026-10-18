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
#endif
