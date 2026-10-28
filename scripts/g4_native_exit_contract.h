/* SPDX-License-Identifier: GPL-2.0-or-later */
/* Narrow shared contract/decision logic: native Win32 caller and host tests. */
#ifndef G4_NATIVE_EXIT_CONTRACT_H
#define G4_NATIVE_EXIT_CONTRACT_H
#include <stddef.h>
#include <stdint.h>
#include <string.h>
#include <wchar.h>

#define WEX_EXIT_IMAGE L"Z:\\home\\debian\\wex-g4\\chromium-headful-r1-20261023-stage\\vendor\\extracted\\chrome-win\\chrome.exe"
#ifndef WEX_EXIT_OUTPUT
#define WEX_EXIT_OUTPUT L"Z:\\home\\debian\\wex-g4\\chromium-headful-native-exit-r1-20261023"
#endif
#define WEX_EXIT_CHILD L"Z:\\home\\debian\\wex-g4\\native-exit-calibration-r1-20261023-stage\\original\\exit-child.exe"
#define WEX_EXIT_CAL_OUTPUT L"Z:\\home\\debian\\wex-g4\\native-exit-calibration-r1-20261023"
#define WEX_EXIT_MANIFEST "8fc037ca464bc22952c9f9d63447cf0feb7dcab07e20489cb2d3a4ef3e1a8ba7"

struct wex_launch {
    const wchar_t *image, *events, *nonce;
    const wchar_t *child_args[16]; /* widened for combined navigation-phase diagnostic args */
    size_t count;
    uint32_t wait_ms;
    int calibration;
};

static inline int wex_nonce(const wchar_t *s) {
    size_t i;
    if (!s || wcslen(s)!=32) return 0;
    for (i=0;i<32;i++) if (!((s[i]>=L'0' && s[i]<=L'9') || (s[i]>=L'a' && s[i]<=L'f'))) return 0;
    return 1;
}

static inline int wex_url(const wchar_t *url,const wchar_t *nonce) {
    const wchar_t *p; unsigned port=0; size_t digits=0; wchar_t tail[128];
    if (!url || !wex_nonce(nonce) || wcsncmp(url,L"http://127.0.0.1:",17)) return 0;
    p=url+17;
    if (*p<L'1' || *p>L'9') return 0;
    while (*p>=L'0' && *p<=L'9') { if (++digits>5) return 0; port=port*10+(unsigned)(*p++-L'0'); }
    if (!port || port>65535) return 0;
    if (swprintf(tail,128,L"/%ls/index.html#nonce=%ls",nonce,nonce)<0) return 0;
    return !wcscmp(p,tail);
}

/* Fixed ordered schema, never shell/arbitrary executable or flags passthrough.
 * Runtime bytes/manifest must ALSO be independently pinned by future runner.
 */
static inline int wex_validate(int argc,wchar_t **argv,struct wex_launch *out) {
    struct wex_launch v;
    memset(&v,0,sizeof(v));
    if (argc==14 && !wcscmp(argv[1],L"--chromium") &&
        !wcscmp(argv[2],L"--image") && !wcscmp(argv[3],WEX_EXIT_IMAGE) &&
        !wcscmp(argv[4],L"--profile") && !wcscmp(argv[5],WEX_EXIT_OUTPUT L"\\profile") &&
        !wcscmp(argv[6],L"--log") && !wcscmp(argv[7],WEX_EXIT_OUTPUT L"\\chromium.log") &&
        !wcscmp(argv[8],L"--events") && !wcscmp(argv[9],WEX_EXIT_OUTPUT L"\\native-exit.jsonl") &&
        !wcscmp(argv[10],L"--nonce") && wex_nonce(argv[11]) &&
        !wcscmp(argv[12],L"--url") && wex_url(argv[13],argv[11])) {
        /* App/profile/log strings are constructed, not accepted as extra flags. */
        static wchar_t app[256];
        if (swprintf(app,256,L"--app=%ls",argv[13])<0) return 0;
        v.image=argv[3];v.events=argv[9];v.nonce=argv[11];v.wait_ms=45000;
        v.child_args[0]=v.image;v.child_args[1]=app;
        v.child_args[2]=L"--user-data-dir=" WEX_EXIT_OUTPUT L"\\profile";
        v.child_args[3]=L"--no-first-run";v.child_args[4]=L"--no-default-browser-check";
        v.child_args[5]=L"--enable-logging";
        v.child_args[6]=L"--log-file=" WEX_EXIT_OUTPUT L"\\chromium.log";
        v.child_args[7]=L"--v=1";v.count=8;
#ifdef WEX_NAVPHASE
        /* WO-20261028-CHROMIUM-NAVIGATION-PHASE: combined bounded diagnostic on the SAME run, so we
         * get evidence either way. (a) release-safe scoped VLOG for browser navigation/network phases;
         * (b) fixed INTEGER NetLog duration 40s, Default capture only, 8MiB cap (no sensitive/TLS);
         * (c) best-effort startup tracing 35s that flushes to file on expiry (Windows has no streaming).
         * No remote debug port / global sampler. Order MUST mirror child_args() in
         * check-g4-chromium-native-exit.py exactly, or the binding() command-line check rejects it. */
        v.child_args[8]=L"--vmodule=*/content/browser/*=2,*/content/browser/renderer_host/*=2,*/content/browser/loader/*=2,*/services/network/*=2";
        v.child_args[9]=L"--log-net-log=" WEX_EXIT_OUTPUT L"\\netlog.json";
        v.child_args[10]=L"--net-log-max-size-mb=8";
        v.child_args[11]=L"--net-log-duration=40";
        v.child_args[12]=L"--trace-startup";
        v.child_args[13]=L"--trace-startup-duration=35";
        v.child_args[14]=L"--trace-startup-file=" WEX_EXIT_OUTPUT L"\\trace.json";v.count=15;
#endif
    } else if (argc==8 && !wcscmp(argv[1],L"--calibration") &&
        !wcscmp(argv[2],L"--image") && !wcscmp(argv[3],WEX_EXIT_CHILD) &&
        !wcscmp(argv[4],L"--events") && !wcscmp(argv[5],WEX_EXIT_CAL_OUTPUT L"\\native-exit.jsonl") &&
        !wcscmp(argv[6],L"--nonce") && wex_nonce(argv[7])) {
        v.image=argv[3];v.events=argv[5];v.nonce=argv[7];v.wait_ms=5000;v.calibration=1;
        v.child_args[0]=v.image;v.child_args[1]=L"--exit-status";
        v.child_args[2]=L"3221225599"; /* 0xc000007f: original fixture value, NOT a diagnosis */
        v.child_args[3]=L"--nonce";v.child_args[4]=v.nonce;v.count=5;
    } else return 0;
    *out=v;return 1;
}

static inline int wex_put(wchar_t *out,size_t cap,size_t *used,wchar_t c) {
    if (*used+1>=cap) return 0;
    out[(*used)++]=c;out[*used]=0;return 1;
}

/* Windows command-line quoting: double slashes before quote/trailing quote.
 * Always quote each argument, including argv[0]; lpApplicationName is separate.
 */
static inline int wex_quote(wchar_t *out,size_t cap,size_t *used,const wchar_t *arg) {
    size_t slashes=0,i;
    if (!wex_put(out,cap,used,L'"')) return 0;
    for (;;) {
        if (*arg==L'\\') { slashes++;arg++;continue; }
        if (*arg==L'"' || !*arg) {
            for (i=0;i<slashes*2;i++) if (!wex_put(out,cap,used,L'\\')) return 0;
            slashes=0;
            if (!*arg) break;
            if (!wex_put(out,cap,used,L'\\')) return 0;
        } else {
            for (i=0;i<slashes;i++) if (!wex_put(out,cap,used,L'\\')) return 0;
            slashes=0;
        }
        if (!wex_put(out,cap,used,*arg++)) return 0;
    }
    return wex_put(out,cap,used,L'"');
}

static inline int wex_command(wchar_t *out,size_t cap,const struct wex_launch *v) {
    size_t i,used=0;
    if (!cap) return 0;
    out[0]=0;
    for (i=0;i<v->count;i++) {
        if (i && !wex_put(out,cap,&used,L' ')) return 0;
        if (!wex_quote(out,cap,&used,v->child_args[i])) return 0;
    }
    return 1;
}

static inline void wex_status_hex(uint32_t value,char out[11]) {
    static const char digits[]="0123456789abcdef";unsigned i;
    out[0]='0';out[1]='x';
    for (i=0;i<8;i++) out[2+i]=digits[(value>>(28-i*4))&15];
    out[10]=0;
}

/* Minimal shared API sequence; concrete Win32 functions live in the one helper.
 * poll returns WAIT_TIMEOUT=258 only when not signaled. Exit259 is accepted ONLY
 * after signaled wait, not inferred from GetExitCodeProcess alone.
 */
struct wex_ops {
    void *ctx;
    int (*create)(void*),(*close_thread)(void*),(*identity)(void*),(*bound)(void*);
    uint32_t (*poll)(void*),(*wait)(void*);
    int (*query_exit)(void*),(*exited)(void*),(*timeout)(void*),(*failure)(void*,const char*);
};
static inline int wex_observe(struct wex_ops *o) {
    uint32_t wait;
    if (!o->create(o->ctx)) return o->failure(o->ctx,"create");
    if (!o->close_thread(o->ctx)) return o->failure(o->ctx,"close_thread");
    if (o->poll(o->ctx)!=258) return o->failure(o->ctx,"identity_race_before");
    if (!o->identity(o->ctx)) return o->failure(o->ctx,"identity_query");
    if (o->poll(o->ctx)!=258) return o->failure(o->ctx,"identity_race_after");
    if (!o->bound(o->ctx)) return o->failure(o->ctx,"binding_flush");
    wait=o->wait(o->ctx);
    if (wait==258) { if (!o->timeout(o->ctx)) return o->failure(o->ctx,"timeout_flush"); return 1; }
    if (wait!=0) return o->failure(o->ctx,"wait");
    if (!o->query_exit(o->ctx)) return o->failure(o->ctx,"exit_query");
    if (!o->exited(o->ctx)) return o->failure(o->ctx,"exit_flush");
    return 0; /* helper observation success, NOT child status/product acceptance */
}
#endif
