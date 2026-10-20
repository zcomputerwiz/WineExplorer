/* Native Win32 WebView2 diagnostic host. Uses the official SDK C interfaces.
 * The lab launcher supplies a fixed runtime, profile and diagnostic arguments.
 * No .NET, WRL, runtime installer, or global Wine configuration is required. */
#include <windows.h>
#include <shellapi.h>
#include <winternl.h>
#include <stdio.h>
#include <WebView2.h>

static HWND window;
static FILE *log_file;
static ICoreWebView2Controller *controller;
static ICoreWebView2 *webview;
static DWORD started, duration = 20000;
static BOOL closing, script_pending;
static unsigned int samples;
static EventRegistrationToken navigation_token, process_token;
static WCHAR browser_arguments[4096];
static BOOL graphics_diagnostic;

static void log_result(const char *event, HRESULT result, const WCHAR *detail)
{
    fprintf(log_file, "%lu ms %s hr=0x%08lx %ls\n", GetTickCount() - started,
            event, (unsigned long)result, detail ? detail : L"");
    fflush(log_file);
}

typedef struct { const void *vtable; LONG refs; GUID iid; } CALLBACK_BASE;
static HRESULT callback_query(CALLBACK_BASE *self, REFIID iid, void **object)
{
    if (!object) return E_POINTER;
    *object = NULL;
    if (!IsEqualGUID(iid, &IID_IUnknown) && !IsEqualGUID(iid, &self->iid)) return E_NOINTERFACE;
    *object = self; InterlockedIncrement(&self->refs); return S_OK;
}

static HRESULT STDMETHODCALLTYPE options_query(ICoreWebView2EnvironmentOptions *self, REFIID iid, void **out)
{ return callback_query((CALLBACK_BASE *)self, iid, out); }
static ULONG STDMETHODCALLTYPE options_addref(ICoreWebView2EnvironmentOptions *self)
{ return InterlockedIncrement(&((CALLBACK_BASE *)self)->refs); }
static ULONG STDMETHODCALLTYPE options_release(ICoreWebView2EnvironmentOptions *self)
{ return InterlockedDecrement(&((CALLBACK_BASE *)self)->refs); }
static HRESULT copy_string(LPCWSTR text, LPWSTR *value)
{
    size_t bytes;
    if (!value) return E_POINTER;
    bytes = (lstrlenW(text) + 1) * sizeof(WCHAR);
    *value = CoTaskMemAlloc(bytes); if (!*value) return E_OUTOFMEMORY;
    memcpy(*value, text, bytes); return S_OK;
}
static HRESULT STDMETHODCALLTYPE options_args(ICoreWebView2EnvironmentOptions *self, LPWSTR *value)
{ (void)self; return copy_string(browser_arguments, value); }
static HRESULT STDMETHODCALLTYPE options_empty(ICoreWebView2EnvironmentOptions *self, LPWSTR *value)
{ (void)self; if (!value) return E_POINTER; *value = NULL; return S_OK; }
static HRESULT STDMETHODCALLTYPE options_version(ICoreWebView2EnvironmentOptions *self, LPWSTR *value)
{ (void)self; return copy_string(L"89.0.0.0", value); }
static HRESULT STDMETHODCALLTYPE options_put(ICoreWebView2EnvironmentOptions *self, LPCWSTR value)
{ (void)self; (void)value; return E_NOTIMPL; }
static HRESULT STDMETHODCALLTYPE options_sso(ICoreWebView2EnvironmentOptions *self, BOOL *value)
{ (void)self; if (!value) return E_POINTER; *value = FALSE; return S_OK; }
static HRESULT STDMETHODCALLTYPE options_put_sso(ICoreWebView2EnvironmentOptions *self, BOOL value)
{ (void)self; (void)value; return E_NOTIMPL; }
static const ICoreWebView2EnvironmentOptionsVtbl options_vtable = {
    options_query, options_addref, options_release, options_args, options_put,
    options_empty, options_put, options_version, options_put, options_sso, options_put_sso
};
static CALLBACK_BASE options = {&options_vtable, 1, {0}};

static void log_browser_command_line(void)
{
    UINT32 pid;
    HANDLE process;
    PROCESS_BASIC_INFORMATION info;
    PEB peb;
    RTL_USER_PROCESS_PARAMETERS parameters;
    WCHAR *command;
    typedef NTSTATUS (WINAPI *QUERY_PROCESS)(HANDLE, PROCESSINFOCLASS, void *, ULONG, ULONG *);
    QUERY_PROCESS query = (QUERY_PROCESS)(void *)GetProcAddress(GetModuleHandleW(L"ntdll.dll"), "NtQueryInformationProcess");
    if (FAILED(ICoreWebView2_get_BrowserProcessId(webview, &pid))) return;
    process = OpenProcess(PROCESS_QUERY_INFORMATION | PROCESS_VM_READ, FALSE, pid);
    if (!process) return;
    if (query && !query(process, ProcessBasicInformation, &info, sizeof(info), NULL) &&
        ReadProcessMemory(process, info.PebBaseAddress, &peb, sizeof(peb), NULL) &&
        ReadProcessMemory(process, peb.ProcessParameters, &parameters, sizeof(parameters), NULL))
    {
        command = calloc(parameters.CommandLine.Length + sizeof(WCHAR), 1);
        if (command && ReadProcessMemory(process, parameters.CommandLine.Buffer, command, parameters.CommandLine.Length, NULL))
            log_result("browser Win32 command line", S_OK, command);
        else log_result("read browser command line", HRESULT_FROM_WIN32(GetLastError()), NULL);
        free(command);
    }
    CloseHandle(process);
}
/* Static callbacks remain alive until the process exits. Runtime references are
 * still counted, and QueryInterface only exposes the actual callback interface. */
#define WV_CALLBACK(name, type, invoke) \
    static HRESULT STDMETHODCALLTYPE name##_query(type *self, REFIID iid, void **out) \
    { return callback_query((CALLBACK_BASE *)self, iid, out); } \
    static ULONG STDMETHODCALLTYPE name##_addref(type *self) \
    { return InterlockedIncrement(&((CALLBACK_BASE *)self)->refs); } \
    static ULONG STDMETHODCALLTYPE name##_release(type *self) \
    { return InterlockedDecrement(&((CALLBACK_BASE *)self)->refs); } \
    static const type##Vtbl name##_vtable = {name##_query, name##_addref, name##_release, invoke}; \
    static CALLBACK_BASE name = {&name##_vtable, 1, {0}}

static void resize_webview(void)
{
    RECT bounds;
    if (!controller) return;
    GetClientRect(window, &bounds);
    log_result("controller bounds", ICoreWebView2Controller_put_Bounds(controller, bounds), NULL);
}

static HRESULT STDMETHODCALLTYPE script_done(ICoreWebView2ExecuteScriptCompletedHandler *self,
                                              HRESULT error, LPCWSTR json)
{
    (void)self; script_pending = FALSE;
    log_result("sample", error, json); samples++;
    return S_OK;
}
WV_CALLBACK(script, ICoreWebView2ExecuteScriptCompletedHandler, script_done);

static HRESULT STDMETHODCALLTYPE graphics_done(ICoreWebView2ExecuteScriptCompletedHandler *self,
                                                HRESULT error, LPCWSTR json)
{
    (void)self;
    log_result("graphics capabilities", error, json);
    return S_OK;
}
WV_CALLBACK(graphics, ICoreWebView2ExecuteScriptCompletedHandler, graphics_done);

static HRESULT STDMETHODCALLTYPE navigation_done(ICoreWebView2NavigationCompletedEventHandler *self,
                                                  ICoreWebView2 *sender,
                                                  ICoreWebView2NavigationCompletedEventArgs *args)
{
    BOOL success = FALSE;
    COREWEBVIEW2_WEB_ERROR_STATUS status = COREWEBVIEW2_WEB_ERROR_STATUS_UNKNOWN;
    WCHAR detail[100];
    (void)self; (void)sender;
    ICoreWebView2NavigationCompletedEventArgs_get_IsSuccess(args, &success);
    ICoreWebView2NavigationCompletedEventArgs_get_WebErrorStatus(args, &status);
    swprintf(detail, 100, L"success=%d web_error=%d", success, status);
    log_result("navigation completed", S_OK, detail);
    if (success && graphics_diagnostic)
        log_result("request graphics capabilities", ICoreWebView2_ExecuteScript(sender,
            L"(()=>{const inspect=(kind)=>{const c=document.createElement('canvas');c.width=c.height=4;"
            L"const g=c.getContext(kind,{failIfMajorPerformanceCaveat:true,preserveDrawingBuffer:true});"
            L"if(!g)return {available:false};const e=g.getExtension('WEBGL_debug_renderer_info');"
            L"const r={available:true,version:g.getParameter(g.VERSION),renderer:g.getParameter(g.RENDERER),"
            L"unmasked:e?g.getParameter(e.UNMASKED_RENDERER_WEBGL):null,extensions:g.getSupportedExtensions()};"
            L"g.clearColor(1,0,0,1);g.clear(g.COLOR_BUFFER_BIT);const p=new Uint8Array(4);"
            L"g.readPixels(0,0,1,1,g.RGBA,g.UNSIGNED_BYTE,p);r.defaultPixel=Array.from(p);r.defaultError=g.getError();"
            L"const t=g.createTexture();g.bindTexture(g.TEXTURE_2D,t);"
            L"g.texImage2D(g.TEXTURE_2D,0,g.RGBA,4,4,0,g.RGBA,g.UNSIGNED_BYTE,null);r.textureError=g.getError();"
            L"const f=g.createFramebuffer();g.bindFramebuffer(g.FRAMEBUFFER,f);"
            L"g.framebufferTexture2D(g.FRAMEBUFFER,g.COLOR_ATTACHMENT0,g.TEXTURE_2D,t,0);"
            L"r.framebufferStatus=g.checkFramebufferStatus(g.FRAMEBUFFER);g.clear(g.COLOR_BUFFER_BIT);"
            L"g.readPixels(0,0,1,1,g.RGBA,g.UNSIGNED_BYTE,p);r.texturePixel=Array.from(p);r.readError=g.getError();"
            L"r.lost=g.isContextLost();g.deleteFramebuffer(f);g.deleteTexture(t);return r;};"
            L"return {webgl:inspect('webgl'),webgl2:inspect('webgl2')};})()",
            (ICoreWebView2ExecuteScriptCompletedHandler *)&graphics), NULL);
    return S_OK;
}
WV_CALLBACK(navigation, ICoreWebView2NavigationCompletedEventHandler, navigation_done);

static HRESULT STDMETHODCALLTYPE process_failed(ICoreWebView2ProcessFailedEventHandler *self,
                                                ICoreWebView2 *sender,
                                                ICoreWebView2ProcessFailedEventArgs *args)
{
    COREWEBVIEW2_PROCESS_FAILED_KIND kind;
    WCHAR detail[80];
    (void)self; (void)sender;
    HRESULT result = ICoreWebView2ProcessFailedEventArgs_get_ProcessFailedKind(args, &kind);
    swprintf(detail, 80, L"kind=%d", SUCCEEDED(result) ? (int)kind : -1);
    log_result("process failed", result, detail);
    return S_OK;
}
WV_CALLBACK(process, ICoreWebView2ProcessFailedEventHandler, process_failed);

static const WCHAR page[] =
    L"<!doctype html><meta charset='utf-8'><title>WebView2 Wine probe</title>"
    L"<style>body{font:20px sans-serif;background:#e8f5e9;padding:24px}"
    L"#box{width:40px;height:40px;background:#1565c0}</style>"
    L"<h1>WebView2 rendering probe</h1><p id='counter'>Starting</p><div id='box'></div>"
    L"<p><input placeholder='Test typing here'><button onclick='this.textContent=\"Clicked\"'>Click me</button></p>"
    L"<p>Scroll, resize, type, and watch the counter and moving square.</p>"
    L"<div style='height:900px'>Scroll test</div><script>"
    L"window.framesCount=0;window.timerTicks=0;"
    L"function tick(){framesCount++;box.style.transform='translateX('+framesCount%240+'px)';requestAnimationFrame(tick)}"
    L"requestAnimationFrame(tick);setInterval(()=>{timerTicks++;"
    L"counter.textContent='Animation frames: '+framesCount+'; timer ticks: '+timerTicks;"
    L"document.body.style.background=timerTicks%2?'#e8f5e9':'#fff3e0'},1000);</script>";

static HRESULT STDMETHODCALLTYPE controller_done(ICoreWebView2CreateCoreWebView2ControllerCompletedHandler *self,
                                                  HRESULT error, ICoreWebView2Controller *result)
{
    (void)self;
    log_result("controller completed", error, NULL);
    if (closing || FAILED(error) || !result) return S_OK;
    controller = result; ICoreWebView2Controller_AddRef(controller);
    error = ICoreWebView2Controller_get_CoreWebView2(controller, &webview);
    log_result("get webview", error, NULL);
    if (FAILED(error) || !webview) return S_OK;
    log_browser_command_line();
    resize_webview();
    log_result("controller visible", ICoreWebView2Controller_put_IsVisible(controller, TRUE), NULL);
    log_result("navigation handler", ICoreWebView2_add_NavigationCompleted(webview,
               (ICoreWebView2NavigationCompletedEventHandler *)&navigation, &navigation_token), NULL);
    log_result("process handler", ICoreWebView2_add_ProcessFailed(webview,
               (ICoreWebView2ProcessFailedEventHandler *)&process, &process_token), NULL);
    log_result("navigate local page", ICoreWebView2_NavigateToString(webview, page), NULL);
    return S_OK;
}
WV_CALLBACK(created_controller, ICoreWebView2CreateCoreWebView2ControllerCompletedHandler, controller_done);

static HRESULT STDMETHODCALLTYPE environment_done(ICoreWebView2CreateCoreWebView2EnvironmentCompletedHandler *self,
                                                   HRESULT error, ICoreWebView2Environment *environment)
{
    WCHAR *version = NULL;
    (void)self;
    log_result("environment completed", error, NULL);
    if (closing || FAILED(error) || !environment) return S_OK;
    error = ICoreWebView2Environment_get_BrowserVersionString(environment, &version);
    log_result("runtime version", error, version); CoTaskMemFree(version);
    log_result("create controller", ICoreWebView2Environment_CreateCoreWebView2Controller(environment,
               window, (ICoreWebView2CreateCoreWebView2ControllerCompletedHandler *)&created_controller), NULL);
    return S_OK;
}
WV_CALLBACK(created_environment, ICoreWebView2CreateCoreWebView2EnvironmentCompletedHandler, environment_done);

static LRESULT CALLBACK window_proc(HWND hwnd, UINT message, WPARAM wp, LPARAM lp)
{
    switch (message)
    {
    case WM_SIZE: resize_webview(); break;
    case WM_TIMER:
        if (GetTickCount() - started >= duration) { DestroyWindow(hwnd); return 0; }
        if (webview && !script_pending)
        {
            HRESULT result;
            script_pending = TRUE;
            result = ICoreWebView2_ExecuteScript(webview,
                L"({frames:window.framesCount,ticks:window.timerTicks,visibility:document.visibilityState,"
                L"dpr:devicePixelRatio,paints:performance.getEntriesByType('paint').length,"
                L"text:document.getElementById('counter')?.textContent,color:getComputedStyle(document.body).backgroundColor})",
                (ICoreWebView2ExecuteScriptCompletedHandler *)&script);
            if (FAILED(result)) { script_pending = FALSE; log_result("execute script", result, NULL); }
        }
        return 0;
    case WM_DESTROY:
        closing = TRUE; KillTimer(hwnd, 1);
        if (controller) { ICoreWebView2Controller_Close(controller); ICoreWebView2Controller_Release(controller); }
        if (webview) ICoreWebView2_Release(webview);
        controller = NULL; webview = NULL;
        log_result("shutdown", S_OK, samples ? L"samples received; inspect frame progression" : L"NO SAMPLES RECEIVED");
        PostQuitMessage(0); return 0;
    }
    return DefWindowProcW(hwnd, message, wp, lp);
}

typedef HRESULT (WINAPI *CREATE_ENVIRONMENT)(PCWSTR, PCWSTR, ICoreWebView2EnvironmentOptions *,
                                           ICoreWebView2CreateCoreWebView2EnvironmentCompletedHandler *);

int WINAPI wWinMain(HINSTANCE instance, HINSTANCE previous, WCHAR *command, int show)
{
    WCHAR runtime[4096], profile[4096], arguments[4096], log_path[4096] = {0}, seconds[32];
    WNDCLASSW cls = {0}; MSG message;
    HMODULE loader; CREATE_ENVIRONMENT create;
    HRESULT error;
    (void)previous; (void)command; (void)show;
    started = GetTickCount();
    graphics_diagnostic = GetEnvironmentVariableW(L"WEX_WEBVIEW2_GRAPHICS_DIAGNOSTIC", seconds, 32)
            && !lstrcmpW(seconds, L"1");
    /* Only hide a console created by THIS diagnostic host, never an inherited
     * user terminal. Child logging must not obscure the page being inspected. */
    if (!GetConsoleWindow() && AllocConsole()) ShowWindow(GetConsoleWindow(), SW_HIDE);
    GetEnvironmentVariableW(L"WEX_WEBVIEW2_LOG", log_path, 4096);
    if (!log_path[0]) lstrcpyW(log_path, L"webview2-probe.log");
    log_file = _wfopen(log_path, L"w"); if (!log_file) return 2;
    if (GetEnvironmentVariableW(L"WEX_WEBVIEW2_SECONDS", seconds, 32))
    { unsigned long value = wcstoul(seconds, NULL, 10); if (value >= 5 && value <= 300) duration = value * 1000; }
    runtime[0] = profile[0] = arguments[0] = 0;
    GetEnvironmentVariableW(L"WEBVIEW2_BROWSER_EXECUTABLE_FOLDER", runtime, 4096);
    GetEnvironmentVariableW(L"WEX_WEBVIEW2_PROFILE", profile, 4096);
    GetEnvironmentVariableW(L"WEBVIEW2_ADDITIONAL_BROWSER_ARGUMENTS", arguments, 4096);
    lstrcpynW(browser_arguments, arguments, 4096);
    log_result("runtime folder", S_OK, runtime); log_result("profile", S_OK, profile);
    log_result("requested browser arguments", S_OK, arguments);
    error = CoInitializeEx(NULL, COINIT_APARTMENTTHREADED);
    log_result("CoInitializeEx", error, NULL); if (FAILED(error)) return 2;
    CLSIDFromString(L"{4e8a3389-c9d8-4bd2-b6b5-124fee6cc14d}", &created_environment.iid);
    CLSIDFromString(L"{6c4819f3-c9b7-4260-8127-c9f5bde7f68c}", &created_controller.iid);
    CLSIDFromString(L"{49511172-cc67-4bca-9923-137112f4c4cc}", &script.iid);
    graphics.iid = script.iid;
    CLSIDFromString(L"{d33a35bf-1c49-4f98-93ab-006e0533fe1c}", &navigation.iid);
    CLSIDFromString(L"{79e0aea4-990b-42d9-aa1d-0fcc2e5bc7f1}", &process.iid);
    options.iid = IID_ICoreWebView2EnvironmentOptions;
    cls.lpfnWndProc = window_proc; cls.hInstance = instance;
    cls.lpszClassName = L"WineExplorerWebView2Probe";
    cls.hCursor = LoadCursorW(NULL, IDC_ARROW); cls.hbrBackground = (HBRUSH)(COLOR_WINDOW + 1);
    RegisterClassW(&cls);
    window = CreateWindowW(cls.lpszClassName, L"WebView2 Wine diagnostic probe", WS_OVERLAPPEDWINDOW,
                           25, 25, 740, 520, NULL, NULL, instance, NULL);
    if (!window) { log_result("create window", HRESULT_FROM_WIN32(GetLastError()), NULL); return 2; }
    ShowWindow(window, SW_SHOW); UpdateWindow(window); SetTimer(window, 1, 1000, NULL);
    loader = LoadLibraryW(L"WebView2Loader.dll");
    create = loader ? (CREATE_ENVIRONMENT)(void *)GetProcAddress(loader, "CreateCoreWebView2EnvironmentWithOptions") : NULL;
    if (!create) log_result("load WebView2Loader", HRESULT_FROM_WIN32(GetLastError()), NULL);
    else log_result("create environment", create(runtime[0] ? runtime : NULL, profile[0] ? profile : NULL,
                    (ICoreWebView2EnvironmentOptions *)&options,
                    (ICoreWebView2CreateCoreWebView2EnvironmentCompletedHandler *)&created_environment), NULL);
    while (GetMessageW(&message, NULL, 0, 0) > 0) { TranslateMessage(&message); DispatchMessageW(&message); }
    CoUninitialize(); if (loader) FreeLibrary(loader); fclose(log_file);
    /* Zero means diagnostics were collected, not that rendering passed.
     * The analyzer checks sustained progression rather than a few startup frames. */
    return samples ? 0 : 1;
}
