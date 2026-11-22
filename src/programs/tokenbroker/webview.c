/* SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright 2026 WineExplorer contributors
 *
 * The sign-in window of the Microsoft account provider (docs/WAM_BROKER_DESIGN.md, "The sign-in window"): a WebView2
 * control, owned by the calling program's window, that opens the authorize URL. Every navigation goes to
 * signin_redirect(); the one to the redirect URI is cancelled there, never loaded. Closing the window cancels the
 * sign-in; after SIGNIN_TIMEOUT_MS it fails.
 *
 * WebView2 comes from the runtime that Edge and Copilot install, through its documented loader entry point
 * CreateCoreWebView2EnvironmentWithOptions in WebView2Loader.dll. Nothing of WebView2 ships with WineExplorer: the
 * loader is taken from HKLM\Software\Wine\WebAuthentication "WebView2Loader" (a full path), else from beside this
 * program; never from the calling program's folder (how the image gets one is still open). Without one the request
 * fails with a message that says so.
 *
 * The few interfaces used are called through their vtables by index; the interface IDs and method positions are
 * those of Microsoft's WebView2 SDK interface description (WebView2.idl, SDK 1.0.4258.31, BSD-3-Clause; nothing of
 * it is copied here, only these facts).
 */

#include "broker.h"

#include <objbase.h>

#include "wine/debug.h"

WINE_DEFAULT_DEBUG_CHANNEL(tokenbroker);

#define SIGNIN_TIMEOUT_MS   (10 * 60 * 1000)
#define TIMER_TIMEOUT       1
#define WM_SIGNIN_DONE      (WM_APP + 1)

/* interface IDs */
static const GUID IID_EnvironmentCompletedHandler =
    { 0x4e8a3389, 0xc9d8, 0x4bd2, { 0xb6, 0xb5, 0x12, 0x4f, 0xee, 0x6c, 0xc1, 0x4d } };
static const GUID IID_ControllerCompletedHandler =
    { 0x6c4819f3, 0xc9b7, 0x4260, { 0x81, 0x27, 0xc9, 0xf5, 0xbd, 0xe7, 0xf6, 0x8c } };
static const GUID IID_NavigationStartingHandler =
    { 0x9adbe429, 0xf36d, 0x432b, { 0x9d, 0xdc, 0xf8, 0x88, 0x1f, 0xbd, 0x76, 0xe3 } };
static const GUID IID_LaunchingExternalUriSchemeHandler =
    { 0x74f712e0, 0x8165, 0x43a9, { 0xa1, 0x3f, 0x0c, 0xce, 0x59, 0x7e, 0x75, 0xdf } };
static const GUID IID_ICoreWebView2_18 =
    { 0x7a626017, 0x28be, 0x49b2, { 0xb8, 0x65, 0x3b, 0xa2, 0xb3, 0x52, 0x2d, 0x90 } };

/* method positions in the vtables (IUnknown's three first) */
#define ENV_CREATE_CONTROLLER           3   /* ICoreWebView2Environment::CreateCoreWebView2Controller */
#define CONTROLLER_PUT_ISVISIBLE        4
#define CONTROLLER_PUT_BOUNDS           6
#define CONTROLLER_CLOSE                24
#define CONTROLLER_GET_COREWEBVIEW2     25
#define WEBVIEW_NAVIGATE                5
#define WEBVIEW_ADD_NAVIGATIONSTARTING  7
#define WEBVIEW18_ADD_LAUNCHINGEXTERNAL 117
#define NAVARGS_GET_URI                 3   /* ICoreWebView2NavigationStartingEventArgs */
#define NAVARGS_PUT_CANCEL              8
#define EXTARGS_GET_URI                 3   /* ICoreWebView2LaunchingExternalUriSchemeEventArgs */
#define EXTARGS_PUT_CANCEL              7

#define VTBL( obj ) (*(void ***)(obj))
#define RELEASE( obj ) ((ULONG (STDMETHODCALLTYPE *)( void * ))VTBL( obj )[2])( obj )

typedef HRESULT (STDAPICALLTYPE *create_environment_fn)( const WCHAR *browser_folder, const WCHAR *user_data_folder,
                                                         void *options, void *handler );

struct webview
{
    struct signin *signin;
    HWND hwnd;
    void *controller;
    void *webview;
    BOOL finished;
    HRESULT hr;
};

/* the completion and event handlers: one object kind, an Invoke of two pointer-sized arguments each */
struct handler
{
    const void *vtbl;
    LONG ref;
    const GUID *iid;
    struct webview *ctx;
};

static HRESULT STDMETHODCALLTYPE handler_QueryInterface( struct handler *h, REFIID iid, void **out )
{
    if (IsEqualGUID( iid, &IID_IUnknown ) || IsEqualGUID( iid, h->iid ))
    {
        InterlockedIncrement( &h->ref );
        *out = h;
        return S_OK;
    }
    *out = NULL;
    return E_NOINTERFACE;
}

static ULONG STDMETHODCALLTYPE handler_AddRef( struct handler *h )
{
    return InterlockedIncrement( &h->ref );
}

static ULONG STDMETHODCALLTYPE handler_Release( struct handler *h )
{
    ULONG ref = InterlockedDecrement( &h->ref );
    if (!ref) free( h );
    return ref;
}

static void finish( struct webview *ctx )
{
    if (ctx->finished) return;
    ctx->finished = TRUE;
    PostMessageW( ctx->hwnd, WM_SIGNIN_DONE, 0, 0 );
}

static void fail( struct webview *ctx, const WCHAR *code, const WCHAR *message )
{
    struct signin *signin = ctx->signin;

    if (signin_transition( signin, SIGNIN_FAILED ))
    {
        free( signin->error_code );
        free( signin->error );
        signin->error_code = strdupW( code );
        signin->error = strdupW( message );
    }
    finish( ctx );
}

/* a navigation, or a launch of another scheme: the redirect is cancelled and ends the window */
static void navigation( struct webview *ctx, void *args, int get_uri, int put_cancel )
{
    WCHAR *uri = NULL;

    if (FAILED(((HRESULT (STDMETHODCALLTYPE *)( void *, WCHAR ** ))VTBL( args )[get_uri])( args, &uri )) || !uri)
        return;
    /* only the length is logged: the redirect carries the code */
    TRACE( "navigation to a URI of %u characters\n", (unsigned int)wcslen( uri ) );
    if (signin_redirect( ctx->signin, uri ))
    {
        ((HRESULT (STDMETHODCALLTYPE *)( void *, BOOL ))VTBL( args )[put_cancel])( args, TRUE );
        finish( ctx );
    }
    SecureZeroMemory( uri, wcslen( uri ) * sizeof(WCHAR) );
    CoTaskMemFree( uri );
}

static HRESULT STDMETHODCALLTYPE navigation_starting_Invoke( struct handler *h, void *sender, void *args )
{
    navigation( h->ctx, args, NAVARGS_GET_URI, NAVARGS_PUT_CANCEL );
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE launching_external_Invoke( struct handler *h, void *sender, void *args )
{
    navigation( h->ctx, args, EXTARGS_GET_URI, EXTARGS_PUT_CANCEL );
    return S_OK;
}

static struct handler *handler_create( const void *vtbl, const GUID *iid, struct webview *ctx );

static void resize( struct webview *ctx )
{
    RECT rect;

    if (!ctx->controller) return;
    GetClientRect( ctx->hwnd, &rect );
    ((HRESULT (STDMETHODCALLTYPE *)( void *, RECT ))VTBL( ctx->controller )[CONTROLLER_PUT_BOUNDS])( ctx->controller, rect );
}

static const void *navigation_starting_vtbl[4];
static const void *launching_external_vtbl[4];

static HRESULT STDMETHODCALLTYPE controller_completed_Invoke( struct handler *h, HRESULT result, void *controller )
{
    struct webview *ctx = h->ctx;
    struct handler *nav;
    void *webview18;
    INT64 token;
    HRESULT hr;

    if (ctx->finished) return S_OK;
    if (FAILED(result) || !controller)
    {
        WARN( "WebView2 controller: %#lx\n", result );
        fail( ctx, L"no_webview", L"The WebView2 sign-in window could not be created." );
        return S_OK;
    }
    ((ULONG (STDMETHODCALLTYPE *)( void * ))VTBL( controller )[1])( controller );
    ctx->controller = controller;
    hr = ((HRESULT (STDMETHODCALLTYPE *)( void *, void ** ))VTBL( controller )[CONTROLLER_GET_COREWEBVIEW2])(
            controller, &ctx->webview );
    if (FAILED(hr) || !ctx->webview)
    {
        fail( ctx, L"no_webview", L"The WebView2 sign-in window could not be created." );
        return S_OK;
    }
    resize( ctx );
    ((HRESULT (STDMETHODCALLTYPE *)( void *, BOOL ))VTBL( controller )[CONTROLLER_PUT_ISVISIBLE])( controller, TRUE );

    if ((nav = handler_create( navigation_starting_vtbl, &IID_NavigationStartingHandler, ctx )))
    {
        ((HRESULT (STDMETHODCALLTYPE *)( void *, void *, INT64 * ))VTBL( ctx->webview )[WEBVIEW_ADD_NAVIGATIONSTARTING])(
                ctx->webview, nav, &token );
        handler_Release( nav );
    }
    /* a redirect to a scheme the engine does not load may come as a launch of an external scheme instead */
    if (SUCCEEDED(((HRESULT (STDMETHODCALLTYPE *)( void *, REFIID, void ** ))VTBL( ctx->webview )[0])(
            ctx->webview, &IID_ICoreWebView2_18, &webview18 )))
    {
        if ((nav = handler_create( launching_external_vtbl, &IID_LaunchingExternalUriSchemeHandler, ctx )))
        {
            ((HRESULT (STDMETHODCALLTYPE *)( void *, void *, INT64 * ))VTBL( webview18 )[WEBVIEW18_ADD_LAUNCHINGEXTERNAL])(
                    webview18, nav, &token );
            handler_Release( nav );
        }
        RELEASE( webview18 );
    }
    hr = ((HRESULT (STDMETHODCALLTYPE *)( void *, const WCHAR * ))VTBL( ctx->webview )[WEBVIEW_NAVIGATE])(
            ctx->webview, ctx->signin->flow->authorize_url );
    if (FAILED(hr))
    {
        WARN( "WebView2 Navigate: %#lx\n", hr );
        fail( ctx, L"network", L"The sign-in page could not be opened." );
    }
    return S_OK;
}

static const void *controller_completed_vtbl[4];

static HRESULT STDMETHODCALLTYPE environment_completed_Invoke( struct handler *h, HRESULT result, void *environment )
{
    struct webview *ctx = h->ctx;
    struct handler *next;
    HRESULT hr;

    if (ctx->finished) return S_OK;
    if (FAILED(result) || !environment)
    {
        WARN( "WebView2 environment: %#lx\n", result );
        fail( ctx, L"no_webview", L"No WebView2 runtime could be started for the sign-in window." );
        return S_OK;
    }
    if (!(next = handler_create( controller_completed_vtbl, &IID_ControllerCompletedHandler, ctx )))
    {
        fail( ctx, L"out_of_memory", L"Out of memory." );
        return S_OK;
    }
    hr = ((HRESULT (STDMETHODCALLTYPE *)( void *, HWND, void * ))VTBL( environment )[ENV_CREATE_CONTROLLER])(
            environment, ctx->hwnd, next );
    handler_Release( next );
    if (FAILED(hr))
    {
        WARN( "WebView2 CreateCoreWebView2Controller: %#lx\n", hr );
        fail( ctx, L"no_webview", L"The WebView2 sign-in window could not be created." );
    }
    return S_OK;
}

static const void *environment_completed_vtbl[4] =
    { handler_QueryInterface, handler_AddRef, handler_Release, environment_completed_Invoke };
static const void *controller_completed_vtbl[4] =
    { handler_QueryInterface, handler_AddRef, handler_Release, controller_completed_Invoke };
static const void *navigation_starting_vtbl[4] =
    { handler_QueryInterface, handler_AddRef, handler_Release, navigation_starting_Invoke };
static const void *launching_external_vtbl[4] =
    { handler_QueryInterface, handler_AddRef, handler_Release, launching_external_Invoke };

static struct handler *handler_create( const void *vtbl, const GUID *iid, struct webview *ctx )
{
    struct handler *h = calloc( 1, sizeof(*h) );

    if (!h) return NULL;
    h->vtbl = vtbl;
    h->ref = 1;
    h->iid = iid;
    h->ctx = ctx;
    return h;
}

static LRESULT CALLBACK signin_wndproc( HWND hwnd, UINT msg, WPARAM wparam, LPARAM lparam )
{
    struct webview *ctx = (struct webview *)GetWindowLongPtrW( hwnd, GWLP_USERDATA );

    switch (msg)
    {
    case WM_CREATE:
        SetWindowLongPtrW( hwnd, GWLP_USERDATA, (LONG_PTR)((CREATESTRUCTW *)lparam)->lpCreateParams );
        return 0;
    case WM_SIZE:
        if (ctx) resize( ctx );
        return 0;
    case WM_TIMER:
        if (ctx && wparam == TIMER_TIMEOUT) fail( ctx, L"timeout", L"The sign-in took too long." );
        return 0;
    case WM_CLOSE:
        /* the user closed the window before the sign-in finished */
        if (ctx && !ctx->finished && signin_transition( ctx->signin, SIGNIN_CANCELLED ))
        {
            ctx->signin->error_code = strdupW( L"user_cancel" );
            ctx->signin->error = strdupW( L"The sign-in window was closed." );
            finish( ctx );
        }
        return 0;
    }
    return DefWindowProcW( hwnd, msg, wparam, lparam );
}

/* WebView2Loader.dll: the machine's configured one (HKLM only: a user's setting must not choose code that sees the
 * sign-in), else the one beside this program. Never the calling program's folder. A full path only, and only that
 * file: LoadLibraryExW with LOAD_WITH_ALTERED_SEARCH_PATH resolves its own imports from its folder. */
static HMODULE load_loader( void )
{
    WCHAR path[MAX_PATH], *p;
    DWORD size = sizeof(path);
    HMODULE module;

    if (!RegGetValueW( HKEY_LOCAL_MACHINE, L"Software\\Wine\\WebAuthentication", L"WebView2Loader", RRF_RT_REG_SZ,
                       NULL, path, &size ) &&
        ((path[0] && path[1] == ':' && path[2] == '\\') || (path[0] == '\\' && path[1] == '\\')) &&
        (module = LoadLibraryExW( path, NULL, LOAD_WITH_ALTERED_SEARCH_PATH )))
        return module;
    if (GetModuleFileNameW( NULL, path, MAX_PATH ) && (p = wcsrchr( path, '\\' )) &&
        p + 20 < path + MAX_PATH)
    {
        wcscpy( p + 1, L"WebView2Loader.dll" );
        if ((module = LoadLibraryExW( path, NULL, LOAD_WITH_ALTERED_SEARCH_PATH ))) return module;
    }
    return NULL;
}

/* the browser profile of the sign-in window: %LOCALAPPDATA%\WineExplorer\TokenBroker\WebView2 */
static BOOL user_data_folder( WCHAR *path, DWORD size )
{
    DWORD len = GetEnvironmentVariableW( L"LOCALAPPDATA", path, size );

    if (!len || len + 40 > size) return FALSE;
    wcscat( path, L"\\WineExplorer" );
    CreateDirectoryW( path, NULL );
    wcscat( path, L"\\TokenBroker" );
    CreateDirectoryW( path, NULL );
    wcscat( path, L"\\WebView2" );
    CreateDirectoryW( path, NULL );
    return TRUE;
}

void signin_run_webview( struct signin *signin )
{
    static const WCHAR class_name[] = L"WineTokenBrokerSignIn";
    WNDCLASSW wc = { 0 };
    struct webview ctx = { signin };
    create_environment_fn create_environment;
    struct handler *handler;
    WCHAR folder[MAX_PATH], title[MAX_PATH + 32];
    HWND owner = IsWindow( signin->owner ) ? signin->owner : NULL;
    RECT area, owner_rect;
    HMODULE loader;
    HRESULT hr, com;
    MSG msg;
    int width = 520, height = 680, x, y;

    if (!(loader = load_loader()) ||
        !(create_environment = (void *)GetProcAddress( loader, "CreateCoreWebView2EnvironmentWithOptions" )))
    {
        WARN( "no WebView2 loader\n" );
        if (loader) FreeLibrary( loader );
        if (signin_transition( signin, SIGNIN_FAILED ))
        {
            signin->error_code = strdupW( L"no_webview" );
            signin->error = strdupW( L"The sign-in window needs WebView2: no WebView2Loader.dll was found." );
        }
        return;
    }
    com = CoInitializeEx( NULL, COINIT_APARTMENTTHREADED );

    wc.lpfnWndProc = signin_wndproc;
    wc.hInstance = GetModuleHandleW( NULL );
    wc.hCursor = LoadCursorW( NULL, (const WCHAR *)IDC_ARROW );
    wc.hbrBackground = (HBRUSH)(COLOR_WINDOW + 1);
    wc.lpszClassName = class_name;
    RegisterClassW( &wc );

    /* centred on the calling program's window, else on the screen; the title names the program asking */
    SystemParametersInfoW( SPI_GETWORKAREA, 0, &area, 0 );
    if (owner && GetWindowRect( owner, &owner_rect )) area = owner_rect;
    x = max( 0, area.left + (area.right - area.left - width) / 2 );
    y = max( 0, area.top + (area.bottom - area.top - height) / 2 );
    swprintf( title, ARRAY_SIZE(title), L"Sign in - %s", signin->program_name && *signin->program_name ?
              signin->program_name : L"Microsoft account" );
    ctx.hwnd = CreateWindowExW( WS_EX_DLGMODALFRAME, class_name, title, WS_OVERLAPPEDWINDOW | WS_VISIBLE,
                                x, y, width, height, owner, NULL, wc.hInstance, &ctx );
    if (!ctx.hwnd && owner)
    {
        /* an owner in another process that cannot be used: the window opens unowned (WAM_BROKER_PLAN.md, risks) */
        WARN( "the sign-in window cannot be owned by %p: %lu\n", owner, GetLastError() );
        ctx.hwnd = CreateWindowExW( 0, class_name, title, WS_OVERLAPPEDWINDOW | WS_VISIBLE, x, y, width, height,
                                    NULL, NULL, wc.hInstance, &ctx );
    }
    if (!ctx.hwnd || !signin_transition( signin, SIGNIN_WINDOW_OPEN ))
    {
        if (signin_transition( signin, SIGNIN_FAILED ))
        {
            signin->error_code = strdupW( L"no_window" );
            signin->error = strdupW( L"The sign-in window could not be opened." );
        }
        goto done;
    }
    SetForegroundWindow( ctx.hwnd );
    SetTimer( ctx.hwnd, TIMER_TIMEOUT, SIGNIN_TIMEOUT_MS, NULL );

    if (!user_data_folder( folder, ARRAY_SIZE(folder) )) folder[0] = 0;
    if (!(handler = handler_create( environment_completed_vtbl, &IID_EnvironmentCompletedHandler, &ctx )))
        fail( &ctx, L"out_of_memory", L"Out of memory." );
    else
    {
        /* a fixed runtime named by WebView2's documented variable is passed on explicitly (the installed one else) */
        WCHAR runtime[MAX_PATH];
        DWORD len = GetEnvironmentVariableW( L"WEBVIEW2_BROWSER_EXECUTABLE_FOLDER", runtime, ARRAY_SIZE(runtime) );

        hr = create_environment( len && len < ARRAY_SIZE(runtime) ? runtime : NULL, folder[0] ? folder : NULL, NULL,
                                 handler );
        handler_Release( handler );
        if (FAILED(hr))
        {
            WARN( "CreateCoreWebView2EnvironmentWithOptions: %#lx\n", hr );
            fail( &ctx, L"no_webview", L"No WebView2 runtime was found for the sign-in window." );
        }
    }

    while (GetMessageW( &msg, NULL, 0, 0 ) > 0)
    {
        if (msg.hwnd == ctx.hwnd && msg.message == WM_SIGNIN_DONE) break;
        TranslateMessage( &msg );
        DispatchMessageW( &msg );
    }
    KillTimer( ctx.hwnd, TIMER_TIMEOUT );

done:
    if (ctx.webview) RELEASE( ctx.webview );
    if (ctx.controller)
    {
        ((HRESULT (STDMETHODCALLTYPE *)( void * ))VTBL( ctx.controller )[CONTROLLER_CLOSE])( ctx.controller );
        RELEASE( ctx.controller );
    }
    if (ctx.hwnd)
    {
        SetWindowLongPtrW( ctx.hwnd, GWLP_USERDATA, 0 );
        DestroyWindow( ctx.hwnd );
    }
    /* the window's last messages, so that nothing of it is left for the next sign-in on this thread */
    while (PeekMessageW( &msg, NULL, 0, 0, PM_REMOVE )) DispatchMessageW( &msg );
    if (SUCCEEDED(com)) CoUninitialize();
    /* the loader stays loaded: the runtime may still hold references to it */
}
