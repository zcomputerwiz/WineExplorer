/* SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright 2026 WineExplorer contributors
 *
 * Black-box probe of the Direct3D 11 shader linker (public API of d3dcompiler_47: D3DCompile with a lib_5_0 target,
 * D3DLoadModule, D3DReflectLibrary, D3DCreateFunctionLinkingGraph, D3DCreateLinker, D3DDisassemble), on a shader library
 * written here. Writes to OUTDIR:
 *   lib.dxbc          the compiled library (our own HLSL, below)
 *   lib-chunks.txt    its container: every chunk with fourcc, size and a hex dump
 *   reflect.txt       what the library reflection says about each function and parameter
 *   <case>.hlsl       FunctionLinkingGraph::GenerateHlsl of each case
 *   <case>.dxbc/.asm  the linked shader and its disassembly, plus its chunk list
 * Build (mingw, with Wine's d3d11shader.h first): x86_64-w64-mingw32-g++ -O2 -static -I DIR_WITH_WINE_D3D11SHADER_H -o flg-probe.exe flg-probe.cpp
 * Run: flg-probe.exe OUTDIR [DIR with lib.dxbc and lib93.dxbc compiled earlier, to run where D3DCompile has no lib targets]
 */
#include <windows.h>
#define D3D_COMPILER_VERSION 47
#include <d3d11shader.h>   /* Wine's header (the linker interfaces; mingw's lacks them), before d3dcompiler.h pulls in mingw's */
#include <d3dcompiler.h>
#include <d3d12shader.h>   /* library reflection: the D3D12 types have the layout and method order of the D3D11 ones */

static const GUID iid_library_reflection = {0x54384f1b, 0x5b3e, 0x4bb7, {0xae, 0x01, 0x60, 0xba, 0x30, 0x97, 0xcb, 0xb6}};
#define ID3D11LibraryReflection ID3D12LibraryReflection
#define ID3D11FunctionReflection ID3D12FunctionReflection
#define ID3D11FunctionParameterReflection ID3D12FunctionParameterReflection
#define D3D11_LIBRARY_DESC D3D12_LIBRARY_DESC
#define D3D11_FUNCTION_DESC D3D12_FUNCTION_DESC
#ifndef D3D_RETURN_PARAMETER_INDEX
#define D3D_RETURN_PARAMETER_INDEX (-1)
#endif
#include <stdio.h>
#include <string.h>

typedef HRESULT (WINAPI *compile_t)(const void *, SIZE_T, const char *, const D3D_SHADER_MACRO *, ID3DInclude *, const char *,
                                    const char *, UINT, UINT, ID3DBlob **, ID3DBlob **);
typedef HRESULT (WINAPI *load_module_t)(const void *, SIZE_T, ID3D11Module **);
typedef HRESULT (WINAPI *create_linker_t)(ID3D11Linker **);
typedef HRESULT (WINAPI *create_flg_t)(UINT, ID3D11FunctionLinkingGraph **);
typedef HRESULT (WINAPI *disassemble_t)(const void *, SIZE_T, UINT, const char *, ID3DBlob **);
typedef HRESULT (WINAPI *reflect_library_t)(const void *, SIZE_T, REFIID, void **);

static compile_t p_compile;
static load_module_t p_load_module;
static create_linker_t p_create_linker;
static create_flg_t p_create_flg;
static disassemble_t p_disassemble;
static reflect_library_t p_reflect_library;
static char outdir[MAX_PATH];

static const char lib_source[] =
    "Texture2D tex : register(t0);\n"
    "SamplerState smp : register(s0);\n"
    "cbuffer Params : register(b0) { float4 tint; float scale; };\n"
    "export float4 SampleTex(float2 uv) { return tex.Sample(smp, uv); }\n"
    "export float4 Tint(float4 c) { return c * tint * scale; }\n"
    "export float4 Mul(float4 a, float4 b) { return a * b; }\n"
    "export float4 Add(float4 a, float4 b) { return a + b; }\n"
    "export float Luma(float4 c) { return dot(c.rgb, float3(0.299, 0.587, 0.114)); }\n"
    "export float4 Lerp3(float4 a, float4 b, float t) { return lerp(a, b, t); }\n"
    "export void Split(float4 c, out float3 rgb, out float a) { rgb = c.rgb; a = c.a; }\n"
    "export float4 Join(float3 rgb, float a) { return float4(rgb, a); }\n"
    "export float4 Transform(float4 p, float4x4 m) { return mul(p, m); }\n";

static FILE *open_out(const char *name, const char *mode)
{
    char path[MAX_PATH];

    snprintf(path, sizeof(path), "%s\\%s", outdir, name);
    return fopen(path, mode);
}

static void write_blob(const char *name, const void *data, SIZE_T size)
{
    FILE *f = open_out(name, "wb");

    if (!f) return;
    fwrite(data, 1, size, f);
    fclose(f);
}

static void dump_chunks(const char *name, const void *data, SIZE_T size, int hex)
{
    const BYTE *b = (const BYTE *)data;
    const UINT *u = (const UINT *)data;
    FILE *f = open_out(name, "w");
    UINT i, count;

    if (!f) return;
    if (size < 32 || memcmp(b, "DXBC", 4)) { fprintf(f, "not a DXBC container\n"); fclose(f); return; }
    count = u[7];
    fprintf(f, "container size %u (header %u), version %u, %u chunks\n", (UINT)size, u[6], u[5], count);
    for (i = 0; i < count; i++)
    {
        UINT off = u[8 + i], csize = *(const UINT *)(b + off + 4), k;

        fprintf(f, "chunk %u at %#x: '%.4s' size %u\n", i, off, (const char *)(b + off), csize);
        if (!hex) continue;
        for (k = 0; k < csize; k += 16)
        {
            UINT m;

            fprintf(f, "  %04x:", k);
            for (m = k; m < k + 16 && m < csize; m += 4) fprintf(f, " %08x", *(const UINT *)(b + off + 8 + m));
            fprintf(f, "  ");
            for (m = k; m < k + 16 && m < csize; m++)
            {
                BYTE c = b[off + 8 + m];
                fputc(c >= 32 && c < 127 ? c : '.', f);
            }
            fputc('\n', f);
        }
    }
    fclose(f);
}

static void reflect(const void *data, SIZE_T size)
{
    ID3D11LibraryReflection *lib = NULL;
    D3D11_LIBRARY_DESC desc;
    FILE *f = open_out("reflect.txt", "w");
    HRESULT hr;
    UINT i;
    int p;

    if (!f) return;
    hr = p_reflect_library(data, size, iid_library_reflection, (void **)&lib);
    fprintf(f, "D3DReflectLibrary: hr %08lx\n", hr);
    if (FAILED(hr)) { fclose(f); return; }
    lib->GetDesc(&desc);
    fprintf(f, "creator '%s' flags %#x functions %u\n", desc.Creator, desc.Flags, desc.FunctionCount);
    for (i = 0; i < desc.FunctionCount; i++)
    {
        ID3D11FunctionReflection *fn = lib->GetFunctionByIndex(i);
        D3D11_FUNCTION_DESC fd;

        if (FAILED(fn->GetDesc(&fd))) { fprintf(f, "function %u: GetDesc failed\n", i); continue; }
        fprintf(f, "function %u '%s': version %#x creator '%s' flags %#x cbuffers %u resources %u instructions %u temps %u "
                "arrays %u feature level %#x has return %d parameters %d min feature %#x\n", i, fd.Name, fd.Version, fd.Creator,
                fd.Flags, fd.ConstantBuffers, fd.BoundResources, fd.InstructionCount, fd.TempRegisterCount, fd.TempArrayCount,
                fd.MinFeatureLevel, fd.HasReturn, fd.FunctionParameterCount, fd.MinFeatureLevel);
        for (p = -1; p < fd.FunctionParameterCount; p++)
        {
            ID3D11FunctionParameterReflection *pr = fn->GetFunctionParameter(p);
            D3D12_PARAMETER_DESC pd;

            if (FAILED(pr->GetDesc(&pd))) continue;
            fprintf(f, "  param %d '%s' semantic '%s' type %d class %d rows %u cols %u interp %d flags %#x first in reg %u comp %u "
                    "first out reg %u comp %u\n", p, pd.Name ? pd.Name : "", pd.SemanticName ? pd.SemanticName : "", pd.Type, pd.Class,
                    pd.Rows, pd.Columns, pd.InterpolationMode, pd.Flags, pd.FirstInRegister, pd.FirstInComponent,
                    pd.FirstOutRegister, pd.FirstOutComponent);
        }
        for (UINT r = 0; r < fd.BoundResources; r++)
        {
            D3D12_SHADER_INPUT_BIND_DESC bd;

            if (SUCCEEDED(fn->GetResourceBindingDesc(r, &bd)))
                fprintf(f, "  resource '%s' type %d point %u count %u flags %#x\n", bd.Name, bd.Type, bd.BindPoint, bd.BindCount,
                        bd.uFlags);
        }
    }
    lib->Release();
    fclose(f);
}

static char prebuilt[MAX_PATH];   /* optional: a folder with lib.dxbc and lib93.dxbc compiled on Windows (to run under Wine) */
typedef HRESULT (WINAPI *create_blob_t)(SIZE_T, ID3DBlob **);
static create_blob_t p_create_blob;

/* compile the library, or read the one compiled earlier (Wine's compiler has no library targets) */
static HRESULT compile_library(const char *src, SIZE_T len, const char *target, const char *file, ID3DBlob **lib, ID3DBlob **errors)
{
    char path[MAX_PATH];
    FILE *f;
    long size;

    if (!*prebuilt) return p_compile(src, len, file, NULL, NULL, NULL, target, 0, 0, lib, errors);
    snprintf(path, sizeof(path), "%s\\%s", prebuilt, !strcmp(target, "lib_5_0") ? "lib.dxbc" : "lib93.dxbc");
    if (!(f = fopen(path, "rb"))) return E_FAIL;
    fseek(f, 0, SEEK_END); size = ftell(f); fseek(f, 0, SEEK_SET);
    if (FAILED(p_create_blob(size, lib))) { fclose(f); return E_OUTOFMEMORY; }
    fread((*lib)->GetBufferPointer(), 1, size, f);
    fclose(f);
    return S_OK;
}
static D3D11_PARAMETER_DESC param(const char *name, const char *semantic, D3D_SHADER_VARIABLE_TYPE type, UINT cols, UINT reg)
{
    D3D11_PARAMETER_DESC d;

    memset(&d, 0, sizeof(d));
    d.Name = name;
    d.SemanticName = semantic;
    d.Type = type;
    d.Class = cols == 1 ? D3D_SVC_SCALAR : D3D_SVC_VECTOR;
    d.Rows = 1;
    d.Columns = cols;
    d.InterpolationMode = !strcmp(semantic, "SV_POSITION") ? D3D_INTERPOLATION_LINEAR_NOPERSPECTIVE : D3D_INTERPOLATION_LINEAR;
    d.Flags = D3D_PF_IN;
    d.FirstInRegister = reg;
    d.FirstInComponent = 0;
    d.FirstOutRegister = 0;
    d.FirstOutComponent = 0;
    return d;
}

static void print_errors(FILE *f, const char *what, HRESULT hr, ID3DBlob *errors)
{
    fprintf(f, "%s: hr %08lx\n", what, hr);
    if (errors) { fprintf(f, "%.*s\n", (int)errors->GetBufferSize(), (const char *)errors->GetBufferPointer()); errors->Release(); }
}

/* one case: build the graph, generate HLSL, link, disassemble */
static void link_case(const char *name, ID3D11Module *module, int bind, int which)
{
    ID3D11ModuleInstance *lib_instance = NULL, *flg_instance = NULL;
    ID3D11FunctionLinkingGraph *flg = NULL;
    ID3D11LinkingNode *in = NULL, *out = NULL, *n1 = NULL, *n2 = NULL, *n3 = NULL;
    ID3D11Linker *linker = NULL;
    ID3DBlob *code = NULL, *errors = NULL, *hlsl = NULL, *text = NULL;
    char fname[MAX_PATH];
    FILE *f;
    HRESULT hr;

    snprintf(fname, sizeof(fname), "%s.txt", name);
    if (!(f = open_out(fname, "w"))) return;

    hr = module->CreateInstance("", &lib_instance);
    fprintf(f, "CreateInstance(\"\"): hr %08lx\n", hr);
    if (bind && lib_instance)
    {
        fprintf(f, "BindResource(0,0,1) %08lx\n", lib_instance->BindResource(0, 0, 1));
        fprintf(f, "BindSampler(0,0,1) %08lx\n", lib_instance->BindSampler(0, 0, 1));
        fprintf(f, "BindConstantBuffer(0,0,0) %08lx\n", lib_instance->BindConstantBuffer(0, 0, 0));
    }

    p_create_flg(0, &flg);
    if (which == 0)
    {
        /* pixel shader: SV_POSITION, TEXCOORD0 (uv), COLOR0 -> SampleTex(uv) -> Tint -> Mul(color) -> SV_TARGET */
        D3D11_PARAMETER_DESC ins[3] = {param("pos", "SV_POSITION", D3D_SVT_FLOAT, 4, 0), param("uv", "TEXCOORD0", D3D_SVT_FLOAT, 2, 1),
                                       param("color", "COLOR0", D3D_SVT_FLOAT, 4, 2)};
        D3D11_PARAMETER_DESC outs[1] = {param("target", "SV_TARGET", D3D_SVT_FLOAT, 4, 0)};

        outs[0].Flags = D3D_PF_OUT;
        outs[0].InterpolationMode = D3D_INTERPOLATION_UNDEFINED;   /* the linker refuses an interpolation mode on SV_TARGET */
        hr = flg->SetInputSignature(ins, 3, &in);                    fprintf(f, "SetInputSignature %08lx\n", hr);
        hr = flg->CallFunction("", module, "SampleTex", &n1);        fprintf(f, "CallFunction SampleTex %08lx\n", hr);
        hr = flg->PassValue(in, 1, n1, 0);                           fprintf(f, "PassValue in.1 -> SampleTex.0 %08lx\n", hr);
        hr = flg->CallFunction("", module, "Tint", &n2);             fprintf(f, "CallFunction Tint %08lx\n", hr);
        hr = flg->PassValue(n1, D3D_RETURN_PARAMETER_INDEX, n2, 0);  fprintf(f, "PassValue SampleTex.ret -> Tint.0 %08lx\n", hr);
        hr = flg->CallFunction("", module, "Mul", &n3);              fprintf(f, "CallFunction Mul %08lx\n", hr);
        hr = flg->PassValue(n2, D3D_RETURN_PARAMETER_INDEX, n3, 0);  fprintf(f, "PassValue Tint.ret -> Mul.0 %08lx\n", hr);
        hr = flg->PassValue(in, 2, n3, 1);                           fprintf(f, "PassValue in.2 -> Mul.1 %08lx\n", hr);
        hr = flg->SetOutputSignature(outs, 1, &out);                 fprintf(f, "SetOutputSignature %08lx\n", hr);
        hr = flg->PassValue(n3, D3D_RETURN_PARAMETER_INDEX, out, 0); fprintf(f, "PassValue Mul.ret -> out.0 %08lx\n", hr);
    }
    else if (which == 1)
    {
        /* pixel shader with swizzles, a scalar and out parameters: Split(color) -> Join(rgb, Luma(color)) with .bgr */
        D3D11_PARAMETER_DESC ins[2] = {param("pos", "SV_POSITION", D3D_SVT_FLOAT, 4, 0), param("color", "COLOR0", D3D_SVT_FLOAT, 4, 1)};
        D3D11_PARAMETER_DESC outs[1] = {param("target", "SV_TARGET", D3D_SVT_FLOAT, 4, 0)};

        outs[0].Flags = D3D_PF_OUT;
        outs[0].InterpolationMode = D3D_INTERPOLATION_UNDEFINED;   /* the linker refuses an interpolation mode on SV_TARGET */
        hr = flg->SetInputSignature(ins, 2, &in);                    fprintf(f, "SetInputSignature %08lx\n", hr);
        hr = flg->CallFunction("", module, "Split", &n1);            fprintf(f, "CallFunction Split %08lx\n", hr);
        hr = flg->PassValue(in, 1, n1, 0);                           fprintf(f, "PassValue in.1 -> Split.0 %08lx\n", hr);
        hr = flg->CallFunction("", module, "Luma", &n2);             fprintf(f, "CallFunction Luma %08lx\n", hr);
        hr = flg->PassValueWithSwizzle(in, 1, "rgba", n2, 0, "bgra"); fprintf(f, "PassValueWithSwizzle in.1.rgba -> Luma.0.bgra %08lx\n", hr);
        hr = flg->CallFunction("", module, "Join", &n3);             fprintf(f, "CallFunction Join %08lx\n", hr);
        hr = flg->PassValue(n1, 1, n3, 0);                           fprintf(f, "PassValue Split.1 (out rgb) -> Join.0 %08lx\n", hr);
        hr = flg->PassValue(n2, D3D_RETURN_PARAMETER_INDEX, n3, 1);  fprintf(f, "PassValue Luma.ret -> Join.1 %08lx\n", hr);
        hr = flg->SetOutputSignature(outs, 1, &out);                 fprintf(f, "SetOutputSignature %08lx\n", hr);
        hr = flg->PassValue(n3, D3D_RETURN_PARAMETER_INDEX, out, 0); fprintf(f, "PassValue Join.ret -> out.0 %08lx\n", hr);
    }
    else
    {
        /* vertex shader: POSITION -> Transform(p, ?) is not wired (matrix needs a cbuffer); use Add(pos, pos) -> SV_POSITION, and
         * pass TEXCOORD0 straight through */
        D3D11_PARAMETER_DESC ins[2] = {param("pos", "POSITION", D3D_SVT_FLOAT, 4, 0), param("uv", "TEXCOORD0", D3D_SVT_FLOAT, 2, 1)};
        D3D11_PARAMETER_DESC outs[2] = {param("opos", "SV_POSITION", D3D_SVT_FLOAT, 4, 0), param("ouv", "TEXCOORD0", D3D_SVT_FLOAT, 2, 1)};

        outs[0].Flags = outs[1].Flags = D3D_PF_OUT;
        hr = flg->SetInputSignature(ins, 2, &in);                    fprintf(f, "SetInputSignature %08lx\n", hr);
        hr = flg->CallFunction("", module, "Add", &n1);              fprintf(f, "CallFunction Add %08lx\n", hr);
        hr = flg->PassValue(in, 0, n1, 0);                           fprintf(f, "PassValue in.0 -> Add.0 %08lx\n", hr);
        hr = flg->PassValue(in, 0, n1, 1);                           fprintf(f, "PassValue in.0 -> Add.1 %08lx\n", hr);
        hr = flg->SetOutputSignature(outs, 2, &out);                 fprintf(f, "SetOutputSignature %08lx\n", hr);
        hr = flg->PassValue(n1, D3D_RETURN_PARAMETER_INDEX, out, 0); fprintf(f, "PassValue Add.ret -> out.0 %08lx\n", hr);
        hr = flg->PassValue(in, 1, out, 1);                          fprintf(f, "PassValue in.1 -> out.1 %08lx\n", hr);
    }

    errors = NULL;
    hr = flg->GenerateHlsl(0, &hlsl);
    fprintf(f, "GenerateHlsl: hr %08lx\n", hr);
    if (hlsl)
    {
        snprintf(fname, sizeof(fname), "%s.hlsl", name);
        write_blob(fname, hlsl->GetBufferPointer(), hlsl->GetBufferSize());
        hlsl->Release();
    }
    hr = flg->CreateModuleInstance(&flg_instance, &errors);
    print_errors(f, "CreateModuleInstance", hr, errors);

    p_create_linker(&linker);
    hr = linker->UseLibrary(lib_instance);
    fprintf(f, "UseLibrary %08lx\n", hr);
    errors = NULL;
    hr = linker->Link(flg_instance, "main", which == 2 ? "vs_5_0" : "ps_5_0", 0, &code, &errors);
    print_errors(f, "Link", hr, errors);
    if (code)
    {
        snprintf(fname, sizeof(fname), "%s.dxbc", name);
        write_blob(fname, code->GetBufferPointer(), code->GetBufferSize());
        snprintf(fname, sizeof(fname), "%s-chunks.txt", name);
        dump_chunks(fname, code->GetBufferPointer(), code->GetBufferSize(), 0);
        if (SUCCEEDED(p_disassemble(code->GetBufferPointer(), code->GetBufferSize(), 0, NULL, &text)) && text)
        {
            snprintf(fname, sizeof(fname), "%s.asm", name);
            write_blob(fname, text->GetBufferPointer(), text->GetBufferSize() - 1);
            text->Release();
        }
        code->Release();
    }
    fclose(f);
}

/* The shape of the graphs that the lifted compositor builds (measured with wintrace on Windows): a lib_4_0_level_9_3 library, an
 * input signature of four float4 (SV_POSITION, COLOR0, TEXCOORD0, TEXCOORD1) with interpolation mode 0 and register 0 everywhere,
 * PassValueWithSwizzle "xy" of a texture coordinate into a sampling function, a final function taking the samples and the colour, one
 * SV_Target output, linked as ps_4_0_level_9_3. Here with the functions of this program's library. */
static void link_case93(const char *source)
{
    static const char extra[] =
        "Texture2D tex1 : register(t1);\n"
        "SamplerState smp1 : register(s1);\n"
        "export float4 SampleTex1(float2 uv) { return tex1.Sample(smp1, uv); }\n"
        "export float4 Combine(float4 color, float4 a, float4 b) { return color * a * b.a; }\n";
    char full[4096];
    ID3DBlob *lib = NULL, *errors = NULL, *text = NULL, *code = NULL;
    ID3D11Module *module = NULL;
    ID3D11ModuleInstance *inst = NULL, *flg_instance = NULL;
    ID3D11FunctionLinkingGraph *flg = NULL;
    ID3D11LinkingNode *in = NULL, *out = NULL, *s0 = NULL, *s1 = NULL, *c = NULL;
    ID3D11Linker *linker = NULL;
    D3D11_PARAMETER_DESC ins[4] = {param("position", "SV_POSITION", D3D_SVT_FLOAT, 4, 0), param("color", "COLOR0", D3D_SVT_FLOAT, 4, 0),
                                   param("uv0", "TEXCOORD0", D3D_SVT_FLOAT, 4, 0), param("uv1", "TEXCOORD1", D3D_SVT_FLOAT, 4, 0)};
    D3D11_PARAMETER_DESC outs[1] = {param("output", "SV_Target", D3D_SVT_FLOAT, 4, 0)};
    FILE *f = open_out("ps93.txt", "w");
    HRESULT hr;
    int i;

    if (!f) return;
    for (i = 0; i < 4; i++) ins[i].InterpolationMode = D3D_INTERPOLATION_UNDEFINED;
    outs[0].InterpolationMode = D3D_INTERPOLATION_UNDEFINED;
    outs[0].Flags = D3D_PF_OUT;
    snprintf(full, sizeof(full), "%s%s", source, extra);
    hr = compile_library(full, strlen(full), "lib_4_0_level_9_3", "lib93.hlsl", &lib, &errors);
    print_errors(f, "D3DCompile lib_4_0_level_9_3", hr, errors);
    if (FAILED(hr)) { fclose(f); return; }
    write_blob("lib93.dxbc", lib->GetBufferPointer(), lib->GetBufferSize());
    dump_chunks("lib93-chunks.txt", lib->GetBufferPointer(), lib->GetBufferSize(), 1);
    if (SUCCEEDED(p_disassemble(lib->GetBufferPointer(), lib->GetBufferSize(), 0, NULL, &text)) && text)
    {
        write_blob("lib93.asm", text->GetBufferPointer(), text->GetBufferSize() - 1);
        text->Release();
    }
    hr = p_load_module(lib->GetBufferPointer(), lib->GetBufferSize(), &module);
    fprintf(f, "D3DLoadModule %08lx\n", hr);
    if (FAILED(hr)) { fclose(f); return; }
    module->CreateInstance("", &inst);
    /* the library's t0/s0 to t0/s0, t1/s1 to t1/s1, cb0 to cb0 */
    fprintf(f, "BindResource(0,0,2) %08lx BindSampler(0,0,2) %08lx BindConstantBuffer(0,0,0) %08lx\n", inst->BindResource(0, 0, 2),
            inst->BindSampler(0, 0, 2), inst->BindConstantBuffer(0, 0, 0));

    p_create_flg(0, &flg);
    hr = flg->SetInputSignature(ins, 4, &in);                                   fprintf(f, "SetInputSignature %08lx\n", hr);
    hr = flg->CallFunction("", module, "SampleTex", &s0);                       fprintf(f, "CallFunction SampleTex %08lx\n", hr);
    hr = flg->PassValueWithSwizzle(in, 2, "xy", s0, 0, "xy");                    fprintf(f, "PassValueWithSwizzle in.2.xy -> 0.xy %08lx\n", hr);
    hr = flg->CallFunction("", module, "SampleTex1", &s1);                      fprintf(f, "CallFunction SampleTex1 %08lx\n", hr);
    hr = flg->PassValueWithSwizzle(in, 3, "xy", s1, 0, "xy");                    fprintf(f, "PassValueWithSwizzle in.3.xy -> 0.xy %08lx\n", hr);
    hr = flg->CallFunction("", module, "Combine", &c);                          fprintf(f, "CallFunction Combine %08lx\n", hr);
    hr = flg->PassValue(in, 1, c, 0);                                           fprintf(f, "PassValue in.1 -> Combine.0 %08lx\n", hr);
    hr = flg->PassValue(s0, D3D_RETURN_PARAMETER_INDEX, c, 1);                  fprintf(f, "PassValue s0.ret -> Combine.1 %08lx\n", hr);
    hr = flg->PassValue(s1, D3D_RETURN_PARAMETER_INDEX, c, 2);                  fprintf(f, "PassValue s1.ret -> Combine.2 %08lx\n", hr);
    hr = flg->SetOutputSignature(outs, 1, &out);                                fprintf(f, "SetOutputSignature %08lx\n", hr);
    hr = flg->PassValue(c, D3D_RETURN_PARAMETER_INDEX, out, 0);                 fprintf(f, "PassValue Combine.ret -> out.0 %08lx\n", hr);
    errors = NULL;
    hr = flg->CreateModuleInstance(&flg_instance, &errors);
    print_errors(f, "CreateModuleInstance", hr, errors);
    p_create_linker(&linker);
    fprintf(f, "UseLibrary %08lx\n", linker->UseLibrary(inst));
    errors = NULL;
    hr = linker->Link(flg_instance, "main", "ps_4_0_level_9_3", 0, &code, &errors);
    print_errors(f, "Link ps_4_0_level_9_3", hr, errors);
    if (code)
    {
        write_blob("ps93.dxbc", code->GetBufferPointer(), code->GetBufferSize());
        dump_chunks("ps93-chunks.txt", code->GetBufferPointer(), code->GetBufferSize(), 1);
        if (SUCCEEDED(p_disassemble(code->GetBufferPointer(), code->GetBufferSize(), 0, NULL, &text)) && text)
        {
            write_blob("ps93.asm", text->GetBufferPointer(), text->GetBufferSize() - 1);
            text->Release();
        }
    }
    fclose(f);
}

int main(int argc, char **argv)
{
    HMODULE d3dc = LoadLibraryA("d3dcompiler_47.dll");
    ID3DBlob *lib = NULL, *errors = NULL, *text = NULL;
    ID3D11Module *module = NULL;
    HRESULT hr;
    FILE *f;

    setvbuf(stdout, NULL, _IONBF, 0);
    if (argc < 2 || !d3dc) { printf("usage: flg-probe OUTDIR (needs d3dcompiler_47.dll)\n"); return 2; }
    snprintf(outdir, sizeof(outdir), "%s", argv[1]);
    if (argc > 2) snprintf(prebuilt, sizeof(prebuilt), "%s", argv[2]);
    p_create_blob = (create_blob_t)GetProcAddress(d3dc, "D3DCreateBlob");
    p_compile = (compile_t)GetProcAddress(d3dc, "D3DCompile");
    p_load_module = (load_module_t)GetProcAddress(d3dc, "D3DLoadModule");
    p_create_linker = (create_linker_t)GetProcAddress(d3dc, "D3DCreateLinker");
    p_create_flg = (create_flg_t)GetProcAddress(d3dc, "D3DCreateFunctionLinkingGraph");
    p_disassemble = (disassemble_t)GetProcAddress(d3dc, "D3DDisassemble");
    p_reflect_library = (reflect_library_t)GetProcAddress(d3dc, "D3DReflectLibrary");

    hr = compile_library(lib_source, sizeof(lib_source) - 1, "lib_5_0", "lib.hlsl", &lib, &errors);
    printf("D3DCompile lib_5_0: hr %08lx\n", hr);
    if (errors) { printf("%.*s\n", (int)errors->GetBufferSize(), (const char *)errors->GetBufferPointer()); errors->Release(); }
    if (FAILED(hr)) return 1;
    write_blob("lib.dxbc", lib->GetBufferPointer(), lib->GetBufferSize());
    dump_chunks("lib-chunks.txt", lib->GetBufferPointer(), lib->GetBufferSize(), 1);
    if (SUCCEEDED(p_disassemble(lib->GetBufferPointer(), lib->GetBufferSize(), 0, NULL, &text)) && text)
    {
        write_blob("lib.asm", text->GetBufferPointer(), text->GetBufferSize() - 1);
        text->Release();
    }
    if (!*prebuilt) reflect(lib->GetBufferPointer(), lib->GetBufferSize());   /* Wine has no D3DReflectLibrary */

    hr = p_load_module(lib->GetBufferPointer(), lib->GetBufferSize(), &module);
    printf("D3DLoadModule: hr %08lx\n", hr);
    if (FAILED(hr)) return 1;
    link_case93(lib_source);
    link_case("ps-sample-bound", module, 1, 0);
    link_case("ps-sample-unbound", module, 0, 0);
    link_case("ps-swizzle", module, 1, 1);
    link_case("vs-add", module, 1, 2);
    if ((f = open_out("done.txt", "w"))) { fprintf(f, "done\n"); fclose(f); }
    printf("done\n");
    return 0;
}
