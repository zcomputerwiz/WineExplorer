// SPDX-License-Identifier: GPL-2.0-or-later
// Test-process-only IAT instrumentation; no production hooks or DLL changes.
#define WEX_G4_HELPERS_ONLY
#include "test_g4_correctness_probe.cpp"
#undef WEX_G4_HELPERS_ONLY
#include <initializer_list>

struct RegistrationHook
{
    decltype(&RegisterWaitForSingleObject) original = NULL;
    ULONG_PTR *slot = NULL, saved = 0;
    DWORD thread = 0;
    HANDLE entered = NULL, resume = NULL, callback_entered = NULL, callback_resume = NULL;
    WAITORTIMERCALLBACK callback = NULL;
    void *context = NULL;
    volatile LONG registrations = 0, callbacks = 0, timed_out = 0;
    bool reject = false, hold = false;
};
static RegistrationHook *active_hook;

static VOID CALLBACK instrumented_callback(void *context, BOOLEAN timed_out)
{
    RegistrationHook *hook = (RegistrationHook *)context;
    LONG index = InterlockedIncrement(&hook->callbacks);
    hook->callback(hook->context,timed_out);
    if (index == 1 && hook->hold)
    {
        SetEvent(hook->callback_entered);
        if (WaitForSingleObject(hook->callback_resume,10000) != WAIT_OBJECT_0)
            InterlockedExchange(&hook->timed_out,1);
    }
}

static BOOL WINAPI instrumented_register(PHANDLE wait, HANDLE object, WAITORTIMERCALLBACK callback,
        void *context, ULONG timeout, ULONG flags)
{
    RegistrationHook *hook = active_hook;
    if (GetCurrentThreadId() != hook->thread)
        return hook->original(wait,object,callback,context,timeout,flags);
    InterlockedIncrement(&hook->registrations);
    if (hook->reject)
    {
        SetLastError(ERROR_NOT_ENOUGH_MEMORY);
        return FALSE;
    }
    hook->callback = callback;
    hook->context = context;
    SetEvent(hook->entered);
    if (hook->hold && WaitForSingleObject(hook->resume,10000) != WAIT_OBJECT_0)
    {
        InterlockedExchange(&hook->timed_out,1);
        SetLastError(ERROR_TIMEOUT);
        return FALSE;
    }
    return hook->original(wait,object,instrumented_callback,hook,timeout,flags);
}

static bool install_hook(RegistrationHook *hook)
{
    BYTE *base = (BYTE *)GetModuleHandleA("d3d11.dll");
    if (!base) return false;
    IMAGE_NT_HEADERS *nt = (IMAGE_NT_HEADERS *)(base + ((IMAGE_DOS_HEADER *)base)->e_lfanew);
    DWORD imports = nt->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_IMPORT].VirtualAddress;
    if (!imports) return false;
    for (IMAGE_IMPORT_DESCRIPTOR *desc = (IMAGE_IMPORT_DESCRIPTOR *)(base + imports); desc->Name; ++desc)
    {
        if (!desc->OriginalFirstThunk) continue;
        IMAGE_THUNK_DATA *names = (IMAGE_THUNK_DATA *)(base + desc->OriginalFirstThunk);
        IMAGE_THUNK_DATA *iat = (IMAGE_THUNK_DATA *)(base + desc->FirstThunk);
        for (; names->u1.AddressOfData; ++names, ++iat)
        {
            if (IMAGE_SNAP_BY_ORDINAL(names->u1.Ordinal)) continue;
            IMAGE_IMPORT_BY_NAME *name = (IMAGE_IMPORT_BY_NAME *)(base + names->u1.AddressOfData);
            if (strcmp((const char *)name->Name,"RegisterWaitForSingleObject")) continue;
            DWORD old;
            hook->slot = &iat->u1.Function;
            if (!VirtualProtect(hook->slot,sizeof(*hook->slot),PAGE_READWRITE,&old)) return false;
            hook->saved = *hook->slot;
            hook->original = (decltype(hook->original))(UINT_PTR)hook->saved;
            active_hook = hook;
            *hook->slot = (ULONG_PTR)instrumented_register;
            DWORD ignored;
            return VirtualProtect(hook->slot,sizeof(*hook->slot),old,&ignored) != FALSE;
        }
    }
    return false; // fail closed if this Wine DLL no longer uses the expected seam
}

static bool uninstall_hook(RegistrationHook *hook)
{
    if (!hook->slot || !hook->saved) return false;
    DWORD old, ignored;
    if (!VirtualProtect(hook->slot,sizeof(*hook->slot),PAGE_READWRITE,&old)) return false;
    *hook->slot = hook->saved;
    bool restored = VirtualProtect(hook->slot,sizeof(*hook->slot),old,&ignored) != FALSE;
    active_hook = NULL;
    return restored;
}

struct RegisterWork { ID3D11Fence *fence; HANDLE event; HRESULT hr; RegistrationHook *hook; };
static DWORD WINAPI register_worker(void *data)
{
    RegisterWork *work = (RegisterWork *)data;
    work->hook->thread = GetCurrentThreadId();
    work->hr = work->fence->SetEventOnCompletion(1,work->event);
    return 0;
}

static bool interleaving_case(bool reject)
{
    ID3D11Device *base = NULL;
    ID3D11DeviceContext *base_context = NULL;
    IDCompositionDevice *composition = NULL;
    ID3D11Device5 *device = NULL;
    ID3D11DeviceContext4 *context = NULL;
    ID3D11Fence *producer = NULL, *observer = NULL;
    HANDLE shared = NULL, event = NULL, produced = NULL, worker = NULL;
    RegistrationHook hook;
    RegisterWork work = {NULL,NULL,E_FAIL,&hook};
    bool ok = false, installed = false;
    DWORD before = 0, after = 0;
    if (!create_d3d_and_dcomp(NULL,&base,&base_context,&composition,NULL)
            || FAILED(base->QueryInterface(__uuidof(ID3D11Device5),(void **)&device))
            || FAILED(base_context->QueryInterface(__uuidof(ID3D11DeviceContext4),(void **)&context))
            || FAILED(device->CreateFence(0,D3D11_FENCE_FLAG_SHARED,__uuidof(ID3D11Fence),(void **)&producer))
            || FAILED(producer->CreateSharedHandle(NULL,GENERIC_ALL,NULL,&shared))
            || FAILED(device->OpenSharedFence(shared,__uuidof(ID3D11Fence),(void **)&observer))) goto done;
    event = CreateEventA(NULL,FALSE,FALSE,NULL);
    produced = CreateEventA(NULL,FALSE,FALSE,NULL);
    hook.entered = CreateEventA(NULL,FALSE,FALSE,NULL);
    hook.resume = CreateEventA(NULL,TRUE,FALSE,NULL);
    hook.callback_entered = CreateEventA(NULL,FALSE,FALSE,NULL);
    hook.callback_resume = CreateEventA(NULL,TRUE,FALSE,NULL);
    if (!event || !produced || !hook.entered || !hook.resume || !hook.callback_entered || !hook.callback_resume) goto done;
    if (!(installed = install_hook(&hook))) goto done;
    if (reject)
    {
        hook.thread = GetCurrentThreadId();
        hook.reject = true;
        for (unsigned int i = 0; i < 65; ++i)
        {
            if (observer->SetEventOnCompletion(1,event) != HRESULT_FROM_WIN32(ERROR_NOT_ENOUGH_MEMORY)
                    || WaitForSingleObject(event,0) != WAIT_TIMEOUT || !live_handle_count(&after)) goto done;
            if (!i) before = after; // persistent observer event is legitimately retained
            if (after != before) goto done;
        }
        hook.reject = false;
        // Let retry use the unmodified registration path, not a second wrapper.
        hook.thread = 0;
        if (FAILED(observer->SetEventOnCompletion(1,event)) || FAILED(context->Signal(producer,1))
                || WaitForSingleObject(event,3000) != WAIT_OBJECT_0 || observer->GetCompletedValue() != 1) goto done;
        printf("REGISTER_FAILURE calls=%ld handles_before=%lu handles_after=%lu recovered=1\n",
                hook.registrations,(unsigned long)before,(unsigned long)after);
        ok = hook.registrations == 65 && !hook.callbacks;
    }
    else
    {
        hook.hold = true;
        work.fence = observer; work.event = event;
        worker = CreateThread(NULL,0,register_worker,&work,0,NULL);
        if (!worker || WaitForSingleObject(hook.entered,10000) != WAIT_OBJECT_0) goto done;
        // Observer is published, but the real RegisterWait has NOT been called.
        // The independent producer waiter is not intercepted (different thread).
        if (FAILED(producer->SetEventOnCompletion(1,produced)) || FAILED(context->Signal(producer,1))
                || WaitForSingleObject(produced,3000) != WAIT_OBJECT_0 || producer->GetCompletedValue() != 1) goto done;
        SetEvent(hook.resume);
        if (WaitForSingleObject(worker,10000) != WAIT_OBJECT_0 || FAILED(work.hr)
                || WaitForSingleObject(event,3000) != WAIT_OBJECT_0
                || WaitForSingleObject(hook.callback_entered,3000) != WAIT_OBJECT_0) goto done;
        // Signal2 while callback1 has processed its waiters but cannot return.
        // Concurrent callback delivery is allowed; do not assume serialization.
        if (FAILED(observer->SetEventOnCompletion(2,event)) || FAILED(producer->SetEventOnCompletion(2,produced))
                || FAILED(context->Signal(producer,2)) || WaitForSingleObject(produced,3000) != WAIT_OBJECT_0) goto done;
        SetEvent(hook.callback_resume);
        if (WaitForSingleObject(event,3000) != WAIT_OBJECT_0 || observer->GetCompletedValue() != 2) goto done;
        ok = hook.registrations == 1 && hook.callbacks >= 2 && !hook.timed_out;
        printf("REGISTER_INTERLEAVING registrations=%ld callbacks=%ld timeout=%ld final=2\n",
                hook.registrations,hook.callbacks,hook.timed_out);
    }
done:
    if (hook.resume) SetEvent(hook.resume);
    if (hook.callback_resume) SetEvent(hook.callback_resume);
    if (worker)
    {
        // Keep stack/COM state alive until the worker leaves; outer runner is the
        // deadlock watchdog. Never TerminateThread while it holds the fence CS.
        WaitForSingleObject(worker,INFINITE);
        CloseHandle(worker);
    }
    release(observer); release(producer); // synchronously drain registered callbacks
    if (hook.saved) ok = uninstall_hook(&hook) && ok;
    ok = ok && !hook.timed_out;
    for (HANDLE handle : {shared,event,produced,hook.entered,hook.resume,hook.callback_entered,hook.callback_resume})
        if (handle) CloseHandle(handle);
    release(context); release(device); release(composition); release(base_context); release(base);
    return ok;
}

int main()
{
    bool registration = interleaving_case(false), failure = interleaving_case(true);
    printf("fence_signal_during_registration_and_callback_tail=%s\n",registration ? "PASS" : "FAIL");
    printf("fence_registration_failure_rollback=%s\n",failure ? "PASS" : "FAIL");
    return registration && failure ? 0 : 1;
}
