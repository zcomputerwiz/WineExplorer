/* SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright 2026 WineExplorer contributors
 * Small native/COM probes, compiled for both Windows architectures.
 */
#define COBJMACROS
#include <windows.h>
#include <mshtml.h>
#include <d3d11.h>
#include <stdio.h>
#include <string.h>

static int html(void)
{
    /* Public COM class identifier; MinGW's uuid archive omits this CLSID. */
    static const CLSID html_document = {0x25336920, 0x03f9, 0x11cf,
                                        {0x8f, 0xd0, 0x00, 0xaa, 0x00, 0x68, 0x6f, 0x13}};
    IHTMLDocument2 *doc = NULL;
    IHTMLElement *body = NULL;
    SAFEARRAY *array;
    VARIANT *value;
    BSTR text = NULL;
    HRESULT hr;
    int result = 1;
    CoInitialize(NULL);
    hr = CoCreateInstance(&html_document, NULL, CLSCTX_INPROC_SERVER,
                          &IID_IHTMLDocument2, (void **)&doc);
    if (FAILED(hr)) goto done;
    array = SafeArrayCreateVector(VT_VARIANT, 0, 1);
    if (!array) goto done;
    SafeArrayAccessData(array, (void **)&value);
    V_VT(value) = VT_BSTR;
    V_BSTR(value) = SysAllocString(L"<html><body>Open runtime smoke test</body></html>");
    SafeArrayUnaccessData(array);
    hr = IHTMLDocument2_write(doc, array);
    SafeArrayDestroy(array);
    if (FAILED(hr)) goto done;
    IHTMLDocument2_close(doc);
    hr = IHTMLDocument2_get_body(doc, &body);
    if (FAILED(hr) || !body) goto done;
    hr = IHTMLElement_get_innerText(body, &text);
    if (SUCCEEDED(hr) && text && wcsstr(text, L"Open runtime smoke test")) result = 0;
done:
    printf("MSHTML %u-bit: %s (HRESULT=%08lx)\n", (unsigned)(sizeof(void *) * 8),
           result ? "FAIL" : "PASS", (unsigned long)hr);
    SysFreeString(text);
    if (body) IHTMLElement_Release(body);
    if (doc) IHTMLDocument2_Release(doc);
    CoUninitialize();
    return result;
}

static int graphics(void)
{
    ID3D11Device *device = NULL;
    ID3D11DeviceContext *context = NULL;
    D3D_FEATURE_LEVEL level;
    HRESULT hr = D3D11CreateDevice(NULL, D3D_DRIVER_TYPE_HARDWARE, NULL, 0,
                                  NULL, 0, D3D11_SDK_VERSION, &device, &level, &context);
    printf("D3D11 %u-bit: %s (HRESULT=%08lx, feature=%x)\n", (unsigned)(sizeof(void *) * 8),
           FAILED(hr) ? "FAIL" : "PASS", (unsigned long)hr, SUCCEEDED(hr) ? level : 0);
    if (context) ID3D11DeviceContext_Release(context);
    if (device) ID3D11Device_Release(device);
    return FAILED(hr) ? 1 : 0;
}

int main(int argc, char **argv)
{
    if (argc == 2 && !strcmp(argv[1], "html")) return html();
    if (argc == 2 && !strcmp(argv[1], "d3d11")) return graphics();
    printf("Native %u-bit process: PASS\n", (unsigned)(sizeof(void *) * 8));
    return 0;
}
