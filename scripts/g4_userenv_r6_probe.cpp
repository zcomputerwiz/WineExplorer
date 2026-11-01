// SPDX-License-Identifier: GPL-2.0-or-later
// Original bounded R6 registration metadata sanity probe. No profile/token/process changes.
// Hardened lifecycle: the SAME prefixed nonce moniker is used for both SID derivation and
// registration; we fail-before-mutation (a non-absent identity aborts with no register/delete);
// fresh ownership is acquired only on a fresh S_OK registration (never on a duplicate); genuine
// per-step HRESULTs are recorded; and a finally-style cleanup unregisters only while this run
// still holds ownership.
#define _WIN32_WINNT 0x0602
#include <windows.h>
#include <sddl.h>
#include <stdio.h>
#include <string.h>
#include <wchar.h>

static unsigned checks, failures;
static void check(bool value) { ++checks;if (!value) ++failures; }
static long hr32(HRESULT h){return (long)(int)h;}
static bool escaped(const wchar_t *s,char *out,size_t cap) {
    char utf8[2048];int n=WideCharToMultiByte(CP_UTF8,WC_ERR_INVALID_CHARS,s,-1,utf8,sizeof(utf8),NULL,NULL);
    size_t used=0;if (!n)return false;
    for(int i=0;i<n-1;++i) {
        unsigned char c=utf8[i];if(c<32||used+3>=cap)return false;
        if(c=='"'||c=='\\')out[used++]='\\';
        out[used++]=c;
    }
    out[used]=0;return true;
}

typedef HRESULT (WINAPI *derive_t)(PCWSTR,PSID *);
typedef HRESULT (WINAPI *register_t)(PSID,LPCWSTR,LPCWSTR);
typedef HRESULT (WINAPI *unregister_t)(PSID);
typedef HRESULT (WINAPI *lookup_t)(PSID,LPWSTR *);
typedef void (WINAPI *freemem_t)(void *);

int wmain(int argc,wchar_t **argv) {
    if(argc!=3||wcscmp(argv[1],L"--nonce")||wcslen(argv[2])!=32)return 2;
    for(unsigned i=0;i<32;++i)if(!((argv[2][i]>='0'&&argv[2][i]<='9')||(argv[2][i]>='a'&&argv[2][i]<='f')))return 2;
    const wchar_t *nonce=argv[2]; // raw nonce, preserved verbatim in the JSON record
    if(wcslen(nonce)>64)return 2;
    wchar_t moniker[96],display[96];
    if(swprintf(moniker,96,L"wex-r6-%ls",nonce)<0)return 2; // prefixed identity name, <=64 WCHARs
    if(wcslen(moniker)>64)return 2;
    if(swprintf(display,96,L"WEX R6 %ls",nonce)<0)return 2;

    HMODULE ue=LoadLibraryW(L"userenv.dll");
    HMODULE kb=LoadLibraryW(L"kernelbase.dll");
    derive_t derive=NULL;register_t register_sid=NULL;unregister_t unregister_sid=NULL;lookup_t lookup_moniker=NULL;freemem_t free_mem=NULL;
    if(ue) {
        FARPROC p=GetProcAddress(ue,"DeriveAppContainerSidFromAppContainerName");
        static_assert(sizeof(p)==sizeof(derive),"Win32 function pointer size");
        memcpy(&derive,&p,sizeof(derive));
    }
    if(kb) {
        FARPROC p;
        p=GetProcAddress(kb,"AppContainerRegisterSid");memcpy(&register_sid,&p,sizeof(register_sid));
        p=GetProcAddress(kb,"AppContainerUnregisterSid");memcpy(&unregister_sid,&p,sizeof(unregister_sid));
        p=GetProcAddress(kb,"AppContainerLookupMoniker");memcpy(&lookup_moniker,&p,sizeof(lookup_moniker));
        p=GetProcAddress(kb,"AppContainerFreeMemory");memcpy(&free_mem,&p,sizeof(free_mem));
    }

    WCHAR ue_path[512]={0},kb_path[512]={0},ue_exp[512]={0},kb_exp[512]={0};
    if(GetSystemDirectoryW(ue_exp,480))wcscat(ue_exp,L"\\userenv.dll");
    if(GetSystemDirectoryW(kb_exp,480))wcscat(kb_exp,L"\\kernelbase.dll");
    bool ue_normal=ue&&GetModuleFileNameW(ue,ue_path,512)&&!lstrcmpiW(ue_path,ue_exp);
    bool kb_normal=kb&&GetModuleFileNameW(kb,kb_path,512)&&!lstrcmpiW(kb_path,kb_exp);

    // 1..7: normal builtin paths + the five export resolutions.
    check(ue_normal);check(kb_normal);
    check(derive!=NULL);check(register_sid!=NULL);check(unregister_sid!=NULL);check(lookup_moniker!=NULL);check(free_mem!=NULL);

    PSID sid=NULL;PSID t=(PSID)0xdeadbeef;
    // 8..13: derive the SAME prefixed moniker used for registration -> a valid AppContainer SID.
    check(derive&&derive(NULL,&t)==E_INVALIDARG&&t==(PSID)0xdeadbeef);
    check(derive&&derive(L"",&t)==E_INVALIDARG&&t==(PSID)0xdeadbeef);
    check(derive&&derive(moniker,NULL)==E_INVALIDARG);
    HRESULT derive_hr=derive?derive(moniker,&sid):E_FAIL;
    bool derived=derive_hr==S_OK&&sid!=NULL&&IsValidSid(sid)!=FALSE;check(derived);
    check(derived&&*GetSidSubAuthorityCount(sid)==8);
    if(derived) {
        SID_IDENTIFIER_AUTHORITY authority={{0,0,0,0,0,15}};
        check(!memcmp(GetSidIdentifierAuthority(sid),&authority,sizeof(authority))&&*GetSidSubAuthority(sid,0)==2);
    } else check(false);

    // Per-step genuine HRESULTs (E_FAIL = step not reached). Ownership: `owned` is the stable
    // "acquired fresh ownership" fact; `held` tracks whether we still hold it at exit.
    HRESULT initial_lookup_hr=E_FAIL,register_hr=E_FAIL,post_register_lookup_hr=E_FAIL,
            duplicate_register_hr=E_FAIL,verify_lookup_hr=E_FAIL,unregister_hr=E_FAIL,final_lookup_hr=E_FAIL;
    bool register_attempted=false,owned=false,held=false,unregister_attempted=false,cleanup_attempted=false,cleanup_succeeded=false;

    // 14: fail-before-mutation precondition -- the identity must be absent before we touch it.
    LPWSTR m=NULL;bool clean_absent=false;
    if(derived&&lookup_moniker) {
        initial_lookup_hr=lookup_moniker(sid,&m);
        clean_absent=(initial_lookup_hr==HRESULT_FROM_WIN32(ERROR_FILE_NOT_FOUND))&&m==NULL;
        if(m&&free_mem)free_mem(m);
    }
    check(derived&&clean_absent);

    bool proceed=derived&&clean_absent; // only ever mutate a verified-absent identity
    // 15: fresh registration is the ONLY mutation; owned iff it returns S_OK.
    if(proceed) {
        register_attempted=true;
        register_hr=register_sid?register_sid(sid,moniker,display):E_FAIL;
        owned=(register_hr==S_OK); // a duplicate can never confer fresh ownership
        held=owned;
        check(register_hr==S_OK);
        proceed=owned;
    }
    bool roundtrip_ok=false,intact_ok=false;
    if(proceed) {
        LPWSTR m2=NULL;post_register_lookup_hr=lookup_moniker?lookup_moniker(sid,&m2):E_FAIL;
        check(post_register_lookup_hr==S_OK&&m2!=NULL);
        roundtrip_ok=m2!=NULL&&!wcscmp(m2,moniker);check(roundtrip_ok);
        if(m2&&free_mem)free_mem(m2);
        proceed=post_register_lookup_hr==S_OK&&roundtrip_ok;
    }
    if(proceed) {
        duplicate_register_hr=register_sid?register_sid(sid,moniker,display):E_FAIL;
        check(duplicate_register_hr==HRESULT_FROM_WIN32(ERROR_ALREADY_EXISTS));
        proceed=duplicate_register_hr==HRESULT_FROM_WIN32(ERROR_ALREADY_EXISTS);
    }
    if(proceed) {
        LPWSTR m3=NULL;verify_lookup_hr=lookup_moniker?lookup_moniker(sid,&m3):E_FAIL;
        intact_ok=verify_lookup_hr==S_OK&&m3!=NULL&&!wcscmp(m3,moniker);check(intact_ok);
        if(m3&&free_mem)free_mem(m3);
        proceed=intact_ok;
    }
    // 20..21: planned teardown releases what we own.
    if(proceed) {
        unregister_attempted=true;
        unregister_hr=unregister_sid?unregister_sid(sid):E_FAIL;
        check(unregister_hr==S_OK);
        if(unregister_hr==S_OK)held=false;
        proceed=unregister_hr==S_OK;
    }
    if(proceed) {
        LPWSTR m4=NULL;final_lookup_hr=lookup_moniker?lookup_moniker(sid,&m4):E_FAIL;
        check(final_lookup_hr==HRESULT_FROM_WIN32(ERROR_FILE_NOT_FOUND));
        if(m4&&free_mem)free_mem(m4);
    }

    // Finally-style cleanup: unregister ONLY if this run still holds it (registered but aborted early).
    if(held) {
        cleanup_attempted=true;
        HRESULT hr=unregister_sid?unregister_sid(sid):E_FAIL;
        cleanup_succeeded=(hr==S_OK);
        if(cleanup_succeeded)held=false;
    }

    char ue_json[4096]={0},kb_json[4096]={0},nonce_json[64]={0};
    if(!escaped(ue_path,ue_json,sizeof(ue_json))||!escaped(kb_path,kb_json,sizeof(kb_json))||!escaped(nonce,nonce_json,sizeof(nonce_json)))return 3;
    bool exports=derive&&register_sid&&unregister_sid&&lookup_moniker&&free_mem;
    bool passed=checks==21&&failures==0;
    printf("{\"v\":2,\"nonce\":\"%s\",\"pid\":%lu,"
           "\"userenv_path\":\"%s\",\"kernelbase_path\":\"%s\",\"paths_normal\":%s,\"exports_found\":%s,"
           "\"checks\":%u,\"failures\":%u,"
           "\"derive_hr\":%ld,\"initial_lookup_hr\":%ld,"
           "\"register_attempted\":%s,\"register_hr\":%ld,\"owned\":%s,"
           "\"post_register_lookup_hr\":%ld,\"duplicate_register_hr\":%ld,\"verify_lookup_hr\":%ld,"
           "\"unregister_attempted\":%s,\"unregister_hr\":%ld,\"final_lookup_hr\":%ld,"
           "\"cleanup_attempted\":%s,\"cleanup_succeeded\":%s,\"passed\":%s}\n",
        nonce_json,GetCurrentProcessId(),ue_json,kb_json,(ue_normal&&kb_normal)?"true":"false",exports?"true":"false",
        checks,failures,hr32(derive_hr),hr32(initial_lookup_hr),
        register_attempted?"true":"false",hr32(register_hr),owned?"true":"false",
        hr32(post_register_lookup_hr),hr32(duplicate_register_hr),hr32(verify_lookup_hr),
        unregister_attempted?"true":"false",hr32(unregister_hr),hr32(final_lookup_hr),
        cleanup_attempted?"true":"false",cleanup_succeeded?"true":"false",passed?"true":"false");
    fflush(stdout);
    if(sid)FreeSid(sid);
    if(ue)FreeLibrary(ue);
    if(kb)FreeLibrary(kb);
    return passed?0:1;
}
