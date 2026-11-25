// Crit Launcher Native — Win32 window + DirectX 11 renderer + WinHTTP.
#ifdef _WIN32

#include <d3d11.h>
#include <dwmapi.h>
#include <shellapi.h>
#include <shlobj.h>
#include <wincodec.h>
#include <winhttp.h>

#include <cstdio>
#include <thread>

#include "core/win_icons.h"

#include "platform/platform.h"
#include "platform/win_shaders.h"

#ifndef DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2
#define DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2 ((HANDLE)-4)
#endif

namespace crit {

// ================================================================ GPU textures
static ID3D11Device* g_device = nullptr;

void GpuUploadTexture(Texture& tex) {
    if (!g_device || tex.w <= 0 || tex.h <= 0 || tex.rgba.empty()) return;
    if (tex.gpu) {
        ((ID3D11ShaderResourceView*)tex.gpu)->Release();
        tex.gpu = nullptr;
    }
    D3D11_TEXTURE2D_DESC desc{};
    desc.Width = (UINT)tex.w;
    desc.Height = (UINT)tex.h;
    desc.MipLevels = 1;
    desc.ArraySize = 1;
    desc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    desc.SampleDesc.Count = 1;
    desc.Usage = D3D11_USAGE_IMMUTABLE;
    desc.BindFlags = D3D11_BIND_SHADER_RESOURCE;
    D3D11_SUBRESOURCE_DATA init{};
    init.pSysMem = tex.rgba.data();
    init.SysMemPitch = (UINT)tex.w * 4;
    ID3D11Texture2D* texture = nullptr;
    if (FAILED(g_device->CreateTexture2D(&desc, &init, &texture))) return;
    D3D11_SHADER_RESOURCE_VIEW_DESC srv{};
    srv.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    srv.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D;
    srv.Texture2D.MipLevels = 1;
    ID3D11ShaderResourceView* view = nullptr;
    g_device->CreateShaderResourceView(texture, &srv, &view);
    texture->Release();
    tex.gpu = view;
}

void GpuReleaseTexture(Texture& tex) {
    if (tex.gpu) {
        ((ID3D11ShaderResourceView*)tex.gpu)->Release();
        tex.gpu = nullptr;
    }
}

// ================================================================ WinHTTP
static std::wstring Widen(const string& s) { return ToWide(s); }

static DWORD g_proxyConfig = 0;

static HttpResult WhRequest(const std::wstring& method, const string& url, const string& body,
                            const std::vector<std::pair<string, string>>& headers, bool keepBytes) {
    HttpResult result;
    std::wstring wurl = Widen(url);
    URL_COMPONENTS uc{};
    uc.dwStructSize = sizeof(uc);
    uc.dwSchemeLength = (DWORD)-1;
    uc.dwHostNameLength = (DWORD)-1;
    uc.dwUrlPathLength = (DWORD)-1;
    uc.dwExtraInfoLength = (DWORD)-1;
    if (!WinHttpCrackUrl(wurl.c_str(), 0, 0, &uc)) {
        result.error = "bad url";
        return result;
    }
    std::wstring host(uc.lwpsHostName, uc.dwHostNameLength);
    std::wstring path(uc.lwpsUrlPath, uc.dwUrlPathLength);
    if (uc.dwExtraInfoLength) path.append(uc.lwpsExtraInfo, uc.dwExtraInfoLength);
    if (path.empty()) path = L"/";
    bool https = uc.nScheme == INTERNET_SCHEME_HTTPS;
    HINTERNET session = WinHttpOpen(L"CritLauncher/1.8", WINHTTP_ACCESS_TYPE_AUTOMATIC_PROXY,
                                    WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0);
    if (!session) {
        result.error = "winhttp session";
        return result;
    }
    WinHttpSetTimeouts(session, 10000, 10000, 20000, 30000);
    HINTERNET conn = WinHttpConnect(session, host.c_str(), uc.nPort, 0);
    if (!conn) {
        WinHttpCloseHandle(session);
        result.error = "connect failed";
        return result;
    }
    HINTERNET req = WinHttpOpenRequest(conn, method.c_str(), path.c_str(), nullptr, WINHTTP_NO_REFERER,
                                       WINHTTP_DEFAULT_ACCEPT_TYPES, https ? WINHTTP_FLAG_SECURE : 0);
    if (!req) {
        WinHttpCloseHandle(conn);
        WinHttpCloseHandle(session);
        result.error = "request failed";
        return result;
    }
    std::wstring headerBlock;
    bool hasUA = false;
    for (auto& h : headers) {
        if (IEquals(h.first, "User-Agent")) hasUA = true;
        headerBlock += Widen(h.first) + L": " + Widen(h.second) + L"\r\n";
    }
    if (!hasUA) headerBlock += L"User-Agent: CritLauncher/1.8 (Windows)\r\n";
    if (!body.empty() && method != L"GET") headerBlock += L"Content-Type: application/json\r\n";
    BOOL sent = WinHttpSendRequest(req, headerBlock.c_str(), (DWORD)headerBlock.size(),
                                   body.empty() ? WINHTTP_NO_REQUEST_DATA : (LPVOID)body.data(),
                                   (DWORD)body.size(), (DWORD)body.size(), 0);
    if (!sent) {
        result.error = Format("send failed (%lu)", GetLastError());
    } else if (!WinHttpReceiveResponse(req, nullptr)) {
        result.error = Format("receive failed (%lu)", GetLastError());
    } else {
        DWORD status = 0, sz = sizeof(status);
        WinHttpQueryHeaders(req, WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER,
                            WINHTTP_HEADER_NAME_BY_INDEX, &status, &sz, WINHTTP_NO_HEADER_INDEX);
        result.status = (int)status;
        std::vector<uint8_t> data;
        for (;;) {
            DWORD avail = 0;
            if (!WinHttpQueryDataAvailable(req, &avail) || avail == 0) break;
            size_t offset = data.size();
            data.resize(offset + avail);
            DWORD read = 0;
            if (!WinHttpReadData(req, data.data() + offset, avail, &read)) break;
            data.resize(offset + read);
            if (read == 0) break;
            if (data.size() > 64u * 1024 * 1024) break;
        }
        result.ok = status >= 200 && status < 400;
        if (keepBytes) result.bytes = data;
        result.body.assign(data.begin(), data.end());
        if (!result.ok && result.error.empty()) result.error = Format("HTTP %d", result.status);
    }
    WinHttpCloseHandle(req);
    WinHttpCloseHandle(conn);
    WinHttpCloseHandle(session);
    return result;
}

HttpResult HttpGet(const string& url, const std::vector<std::pair<string, string>>& headers) {
    return WhRequest(L"GET", url, {}, headers, false);
}
HttpResult HttpGetBytes(const string& url, const std::vector<std::pair<string, string>>& headers) {
    return WhRequest(L"GET", url, {}, headers, true);
}
HttpResult HttpRequest(const string& method, const string& url, const string& body,
                       const std::vector<std::pair<string, string>>& headers) {
    std::wstring wm = Widen(method);
    return WhRequest(wm, url, body, headers, false);
}

bool HttpDownload(const string& url, const string& path, std::function<void(float)> progress) {
    HttpResult r = HttpGetBytes(url);
    if (!r.ok || r.bytes.empty()) return false;
    if (progress) progress(1.f);
    return WriteFileBytes(path, r.bytes);
}

// ================================================================ D3D11 renderer
struct D3DRenderer {
    HWND hwnd = nullptr;
    IDXGISwapChain* swap = nullptr;
    ID3D11RenderTargetView* rtv = nullptr;
    ID3D11Device* device = nullptr;
    ID3D11DeviceContext* ctx = nullptr;
    ID3D11VertexShader* vs = nullptr;
    ID3D11PixelShader* shapePs = nullptr;
    ID3D11PixelShader* textPs = nullptr;
    ID3D11PixelShader* imagePs = nullptr;
    ID3D11InputLayout* layout = nullptr;
    ID3D11Buffer* cb = nullptr;
    ID3D11BlendState* blend = nullptr;
    ID3D11RasterizerState* raster = nullptr;
    ID3D11SamplerState* sampler = nullptr;
    ID3D11DepthStencilState* depthOff = nullptr;
    ID3D11Texture2D* backbuffer = nullptr;
    ID3D11ShaderResourceView* backbufferSrv = nullptr;
    int width = 0, height = 0;
    float dpi = 1.f;

    bool CompileShaders() {
        HMODULE d3dc = LoadLibraryA("d3dcompiler_47.dll");
        if (!d3dc) d3dc = LoadLibraryA("d3dcompiler.dll");
        if (!d3dc) return false;
        typedef HRESULT(WINAPI * PFN_D3DCompile)(LPCVOID, SIZE_T, LPCSTR, const D3D_SHADER_MACRO*, ID3DInclude*,
                                                 LPCSTR, LPCSTR, UINT, UINT, ID3DBlob**, ID3DBlob**);
        auto compile = (PFN_D3DCompile)GetProcAddress(d3dc, "D3DCompile");
        if (!compile) return false;
        ID3DBlob* err = nullptr;
        ID3DBlob* vsBlob = nullptr;
        if (FAILED(compile(kShaderSource, strlen(kShaderSource), "shaders", nullptr, nullptr, "VSMain", "vs_4_0", 0, 0,
                           &vsBlob, &err))) {
            if (err) {
                LogError("vs: %s", (char*)err->GetBufferPointer());
                err->Release();
            }
            return false;
        }
        ID3DBlob* shapeBlob = nullptr;
        ID3DBlob* textBlob = nullptr;
        ID3DBlob* imageBlob = nullptr;
        if (FAILED(compile(kShaderSource, strlen(kShaderSource), "shaders", nullptr, nullptr, "PSShape", "ps_4_0", 0, 0,
                           &shapeBlob, &err)) ||
            FAILED(compile(kShaderSource, strlen(kShaderSource), "shaders", nullptr, nullptr, "PSText", "ps_4_0", 0, 0,
                           &textBlob, &err)) ||
            FAILED(compile(kShaderSource, strlen(kShaderSource), "shaders", nullptr, nullptr, "PSImage", "ps_4_0", 0, 0,
                           &imageBlob, &err))) {
            if (err) {
                LogError("ps: %s", (char*)err->GetBufferPointer());
                err->Release();
            }
            return false;
        }
        device->CreateVertexShader(vsBlob->GetBufferPointer(), vsBlob->GetBufferSize(), nullptr, &vs);
        device->CreatePixelShader(shapeBlob->GetBufferPointer(), shapeBlob->GetBufferSize(), nullptr, &shapePs);
        device->CreatePixelShader(textBlob->GetBufferPointer(), textBlob->GetBufferSize(), nullptr, &textPs);
        device->CreatePixelShader(imageBlob->GetBufferPointer(), imageBlob->GetBufferSize(), nullptr, &imagePs);
        const D3D11_INPUT_ELEMENT_DESC elems[] = {
            {"POS", 0, DXGI_FORMAT_R32G32_FLOAT, 0, 0, D3D11_INPUT_PER_VERTEX_DATA, 0},
            {"UVC", 0, DXGI_FORMAT_R32G32_FLOAT, 0, 8, D3D11_INPUT_PER_VERTEX_DATA, 0},
            {"COL", 0, DXGI_FORMAT_R32G32B32A32_FLOAT, 0, 16, D3D11_INPUT_PER_VERTEX_DATA, 0},
            {"LOCAL", 0, DXGI_FORMAT_R32G32_FLOAT, 0, 32, D3D11_INPUT_PER_VERTEX_DATA, 0},
            {"EXT", 0, DXGI_FORMAT_R32G32_FLOAT, 0, 40, D3D11_INPUT_PER_VERTEX_DATA, 0},
            {"RP", 0, DXGI_FORMAT_R32G32_FLOAT, 0, 48, D3D11_INPUT_PER_VERTEX_DATA, 0},
        };
        device->CreateInputLayout(elems, 6, vsBlob->GetBufferPointer(), vsBlob->GetBufferSize(), &layout);
        vsBlob->Release();
        shapeBlob->Release();
        textBlob->Release();
        imageBlob->Release();
        return vs && shapePs && textPs && imagePs && layout;
    }

    bool Init(HWND window) {
        hwnd = window;
        static const D3D_FEATURE_LEVEL levels[] = {D3D_FEATURE_LEVEL_11_0, D3D_FEATURE_LEVEL_10_1,
                                                   D3D_FEATURE_LEVEL_10_0, D3D_FEATURE_LEVEL_9_3};
        UINT flags = D3D11_CREATE_DEVICE_BGRA_SUPPORT;
        D3D_FEATURE_LEVEL level;
        HRESULT hr = D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr, flags, levels, 4,
                                       D3D11_SDK_VERSION, &device, &level, &ctx);
        if (FAILED(hr)) {
            hr = D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_WARP, nullptr, flags, levels, 4, D3D11_SDK_VERSION,
                                   &device, &level, &ctx);
        }
        if (FAILED(hr)) {
            LogError("D3D11CreateDevice failed: 0x%08lx", (unsigned long)hr);
            return false;
        }
        g_device = device;
        RECT rc{};
        GetClientRect(hwnd, &rc);
        width = std::max(1L, rc.right - rc.left);
        height = std::max(1L, rc.bottom - rc.top);
        IDXGIDevice* dxgiDevice = nullptr;
        device->QueryInterface(__uuidof(IDXGIDevice), (void**)&dxgiDevice);
        IDXGIAdapter* adapter = nullptr;
        dxgiDevice->GetAdapter(&adapter);
        IDXGIFactory2* factory = nullptr;
        adapter->GetParent(__uuidof(IDXGIFactory2), (void**)&factory);
        DXGI_SWAP_CHAIN_DESC1 scd{};
        scd.BufferCount = 2;
        scd.Width = (UINT)width;
        scd.Height = (UINT)height;
        scd.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
        scd.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
        scd.SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD;
        scd.SampleDesc.Count = 1;
        scd.AlphaMode = DXGI_ALPHA_MODE_IGNORE;
        hr = factory->CreateSwapChainForHwnd(device, hwnd, &scd, nullptr, nullptr, &swap);
        if (FAILED(hr)) {
            // flip model unavailable: fall back to the legacy blt model
            DXGI_SWAP_CHAIN_DESC scd2{};
            scd2.BufferCount = 1;
            scd2.BufferDesc.Width = (UINT)width;
            scd2.BufferDesc.Height = (UINT)height;
            scd2.BufferDesc.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
            scd2.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
            scd2.SwapEffect = DXGI_SWAP_EFFECT_DISCARD;
            scd2.SampleDesc.Count = 1;
            scd2.OutputWindow = hwnd;
            scd2.Windowed = TRUE;
            IDXGIFactory* f1 = nullptr;
            adapter->GetParent(__uuidof(IDXGIFactory), (void**)&f1);
            f1->CreateSwapChain(device, &scd2, &swap);
            f1->Release();
        }
        factory->MakeWindowAssociation(hwnd, DXGI_MWA_NO_ALT_ENTER);
        factory->Release();
        adapter->Release();
        dxgiDevice->Release();
        if (!swap) return false;
        if (!CompileShaders()) return false;
        // states
        D3D11_BLEND_DESC bd{};
        bd.RenderTarget[0].BlendEnable = TRUE;
        bd.RenderTarget[0].SrcBlend = D3D11_BLEND_SRC_ALPHA;
        bd.RenderTarget[0].DestBlend = D3D11_BLEND_INV_SRC_ALPHA;
        bd.RenderTarget[0].BlendOp = D3D11_BLEND_OP_ADD;
        bd.RenderTarget[0].SrcBlendAlpha = D3D11_BLEND_ONE;
        bd.RenderTarget[0].DestBlendAlpha = D3D11_BLEND_INV_SRC_ALPHA;
        bd.RenderTarget[0].BlendOpAlpha = D3D11_BLEND_OP_ADD;
        bd.RenderTarget[0].RenderTargetWriteMask = D3D11_COLOR_WRITE_ENABLE_ALL;
        device->CreateBlendState(&bd, &blend);
        D3D11_RASTERIZER_DESC rd{};
        rd.FillMode = D3D11_FILL_SOLID;
        rd.CullMode = D3D11_CULL_NONE;
        rd.ScissorEnable = TRUE;
        device->CreateRasterizerState(&rd, &raster);
        D3D11_SAMPLER_DESC sd{};
        sd.Filter = D3D11_FILTER_MIN_MAG_MIP_LINEAR;
        sd.AddressU = D3D11_TEXTURE_ADDRESS_CLAMP;
        sd.AddressV = D3D11_TEXTURE_ADDRESS_CLAMP;
        sd.MaxLOD = D3D11_FLOAT32_MAX;
        device->CreateSamplerState(&sd, &sampler);
        D3D11_DEPTH_STENCIL_DESC dsd{};
        dsd.DepthEnable = FALSE;
        device->CreateDepthStencilState(&dsd, &depthOff);
        D3D11_BUFFER_DESC cbd{};
        cbd.Usage = D3D11_USAGE_DYNAMIC;
        cbd.ByteWidth = 16;
        cbd.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
        cbd.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
        device->CreateBuffer(&cbd, nullptr, &cb);
        CreateTargets();
        dpi = (float)GetDpiForWindow(hwnd) / 96.f;
        if (dpi < 1.f) dpi = 1.f;
        return true;
    }

    void CreateTargets() {
        ReleaseTargets();
        if (FAILED(swap->GetBuffer(0, __uuidof(ID3D11Texture2D), (void**)&backbuffer))) return;
        device->CreateRenderTargetView(backbuffer, nullptr, &rtv);
        device->CreateShaderResourceView(backbuffer, nullptr, &backbufferSrv);
    }

    void ReleaseTargets() {
        if (rtv) {
            rtv->Release();
            rtv = nullptr;
        }
        if (backbufferSrv) {
            backbufferSrv->Release();
            backbufferSrv = nullptr;
        }
        if (backbuffer) {
            backbuffer->Release();
            backbuffer = nullptr;
        }
    }

    void Resize(int w, int h) {
        if (!swap || w <= 0 || h <= 0) return;
        width = w;
        height = h;
        ReleaseTargets();
        swap->ResizeBuffers(0, (UINT)w, (UINT)h, DXGI_FORMAT_UNKNOWN, 0);
        CreateTargets();
    }

    void Frame(RendererState& state) {
        Canvas& cv = state.canvas;
        for (auto& tex : state.textures.Items()) {
            if (tex && !tex->gpu) GpuUploadTexture(*tex);
            if (tex && tex->dirty && tex->gpu) {
                GpuReleaseTexture(*tex);
                GpuUploadTexture(*tex);
                tex->dirty = false;
            }
        }
        ID3D11DeviceContext* dc = ctx;
        float clear[4] = {0.043f, 0.047f, 0.055f, 1.f};
        dc->ClearRenderTargetView(rtv, clear);
        dc->OMSetRenderTargets(1, &rtv, nullptr);
        D3D11_VIEWPORT vp{0.f, 0.f, (float)width, (float)height, 0.f, 1.f};
        dc->RSSetViewports(1, &vp);
        float blendFactor[4] = {0, 0, 0, 0};
        dc->OMSetBlendState(blend, blendFactor, 0xffffffff);
        dc->RSSetState(raster);
        dc->OMSetDepthStencilState(depthOff, 0);
        dc->IASetInputLayout(layout);
        dc->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
        dc->VSSetShader(vs, nullptr, 0);
        dc->PSSetSamplers(0, 1, &sampler);
        // per-vertex stride: Vertex is 56 bytes packed
        const UINT stride = 56, offset = 0;
        ID3D11Buffer* vb = nullptr;
        ID3D11Buffer* ib = nullptr;
        D3D11_BUFFER_DESC desc{};
        desc.Usage = D3D11_USAGE_DYNAMIC;
        desc.BindFlags = D3D11_BIND_VERTEX_BUFFER;
        desc.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
        desc.ByteWidth = (UINT)(cv.verts.size() * sizeof(Vertex));
        if (desc.ByteWidth == 0) desc.ByteWidth = 16;
        if (SUCCEEDED(device->CreateBuffer(&desc, nullptr, &vb)) && !cv.verts.empty()) {
            D3D11_MAPPED_SUBRESOURCE map{};
            if (SUCCEEDED(dc->Map(vb, 0, D3D11_MAP_WRITE_DISCARD, 0, &map))) {
                memcpy(map.pData, cv.verts.data(), cv.verts.size() * sizeof(Vertex));
                dc->Unmap(vb, 0);
            }
        }
        desc.BindFlags = D3D11_BIND_INDEX_BUFFER;
        desc.ByteWidth = (UINT)(cv.indices.size() * sizeof(uint32_t));
        if (desc.ByteWidth == 0) desc.ByteWidth = 16;
        if (SUCCEEDED(device->CreateBuffer(&desc, nullptr, &ib)) && !cv.indices.empty()) {
            D3D11_MAPPED_SUBRESOURCE map{};
            if (SUCCEEDED(dc->Map(ib, 0, D3D11_MAP_WRITE_DISCARD, 0, &map))) {
                memcpy(map.pData, cv.indices.data(), cv.indices.size() * sizeof(uint32_t));
                dc->Unmap(ib, 0);
            }
        }
        dc->IASetVertexBuffers(0, 1, &vb, &stride, &offset);
        dc->IASetIndexBuffer(ib, DXGI_FORMAT_R32_UINT, 0);
        ID3D11ShaderResourceView* whiteView = cv.white && cv.white->gpu ? (ID3D11ShaderResourceView*)cv.white->gpu : nullptr;
        for (const Batch& b : cv.batches) {
            if (b.indexCount == 0) continue;
            D3D11_RECT scissor{(LONG)std::floor(b.clip.x), (LONG)std::floor(b.clip.y),
                               (LONG)std::ceil(b.clip.r()), (LONG)std::ceil(b.clip.b())};
            scissor.left = std::max(0L, scissor.left);
            scissor.top = std::max(0L, scissor.top);
            scissor.right = std::min((LONG)width, scissor.right);
            scissor.bottom = std::min((LONG)height, scissor.bottom);
            if (scissor.right <= scissor.left || scissor.bottom <= scissor.top) continue;
            dc->RSSetScissorRects(1, &scissor);
            D3D11_MAPPED_SUBRESOURCE map{};
            if (SUCCEEDED(dc->Map(cb, 0, D3D11_MAP_WRITE_DISCARD, 0, &map))) {
                float params[4] = {(float)b.shapeType, 0, 0, 0};
                memcpy(map.pData, params, sizeof params);
                dc->Unmap(cb, 0);
            }
            dc->PSSetConstantBuffers(0, 1, &cb);
            ID3D11ShaderResourceView* view = whiteView;
            if (b.kind == BatchKind::Sdf) {
                dc->PSSetShader(shapePs, nullptr, 0);
            } else if (b.kind == BatchKind::Text) {
                dc->PSSetShader(textPs, nullptr, 0);
                if (b.texture >= 0) {
                    Texture* t = state.textures.Get(b.texture);
                    if (t && t->gpu) view = (ID3D11ShaderResourceView*)t->gpu;
                }
            } else {
                dc->PSSetShader(imagePs, nullptr, 0);
                if (b.texture >= 0) {
                    Texture* t = state.textures.Get(b.texture);
                    if (t && t->gpu) view = (ID3D11ShaderResourceView*)t->gpu;
                }
            }
            dc->PSSetShaderResources(0, 1, &view);
            dc->DrawIndexed(b.indexCount, b.firstIndex, 0);
        }
        vb->Release();
        ib->Release();
    }

    void Present() {
        swap->Present(1, 0);
    }
};

// ================================================================ Platform impl
static Platform* g_platform = nullptr;
static InputState* g_input = nullptr;
static bool g_dragging = false;
static POINT g_dragStart{};
static HCURSOR g_cursors[4];

class WinPlatform : public Platform {
public:
    HWND hwnd = nullptr;
    D3DRenderer gpu;
    bool quit = false;
    Vec2 client{1080, 700};
    float dpi = 1.f;
    bool trackingLeave = false;

    ~WinPlatform() override { Destroy(); }

    bool Create(const WindowConfig& cfg) override {
        SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
        HINSTANCE inst = GetModuleHandleW(nullptr);
        WNDCLASSEXW wc{};
        wc.cbSize = sizeof(wc);
        wc.style = CS_HREDRAW | CS_VREDRAW;
        wc.lpfnWndProc = WndProc;
        wc.hInstance = inst;
        wc.hCursor = LoadCursor(nullptr, IDC_ARROW);
        wc.hbrBackground = nullptr;
        wc.lpszClassName = L"CritLauncherNative";
        wc.hIcon = LoadIconW(inst, MAKEINTRESOURCEW(1));
        wc.hIconSm = wc.hIcon;
        wc.hIcon = LoadIconW(GetModuleHandleW(nullptr), L"IDI_APP");
        if (!wc.hIcon) wc.hIcon = LoadIconW(inst, MAKEINTRESOURCEW(1));
        if (!wc.hIconSm) wc.hIconSm = wc.hIcon;
        RegisterClassExW(&wc);
        // window placement (restores the last session)
        int x = CW_USEDEFAULT, y = CW_USEDEFAULT;
        RECT desired{0, 0, cfg.width, cfg.height};
        AdjustWindowRectExForDpi(&desired, WS_OVERLAPPEDWINDOW, FALSE, 0, GetDpiForSystem());
        int ww = desired.right - desired.left, wh = desired.bottom - desired.top;
        hwnd = CreateWindowExW(WS_EX_APPWINDOW, L"CritLauncherNative", cfg.title, WS_OVERLAPPEDWINDOW, x, y, ww, wh,
                               nullptr, nullptr, inst, this);
        if (!hwnd) return false;
        g_platform = this;
        // dark title bar on Win10/11 (the launcher draws its own chrome anyway)
        BOOL dark = TRUE;
        DwmSetWindowAttribute(hwnd, 20, &dark, sizeof dark);
        ShowWindow(hwnd, SW_SHOW);
        UpdateWindow(hwnd);
        if (!gpu.Init(hwnd)) {
            MessageBoxW(hwnd, L"DirectX 11 could not be initialized. Please make sure your graphics driver is installed and "
                              L"supports Direct3D 11.",
                        L"Crit Launcher", MB_ICONERROR);
            return false;
        }
        dpi = gpu.dpi;
        g_cursors[0] = LoadCursor(nullptr, IDC_ARROW);
        g_cursors[1] = LoadCursor(nullptr, IDC_HAND);
        g_cursors[2] = LoadCursor(nullptr, IDC_IBEAM);
        g_cursors[3] = LoadCursor(nullptr, IDC_SIZENS);
        Log("window created %dx%d dpi %.2f", (int)client.x, (int)client.y, dpi);
        return true;
    }

    void Destroy() override {
        if (hwnd) {
            DestroyWindow(hwnd);
            hwnd = nullptr;
        }
    }

    bool PumpMessages(RendererState&) override {
        MSG msg;
        while (PeekMessageW(&msg, nullptr, 0, 0, PM_REMOVE)) {
            if (msg.message == WM_QUIT) quit = true;
            TranslateMessage(&msg);
            DispatchMessageW(&msg);
        }
        return !quit;
    }

    void Present() override { gpu.Present(); }

    void Resize(int w, int h) override {
        client = Vec2((float)w, (float)h);
        gpu.Resize(w, h);
    }

    Vec2 ClientSize() const override { return gpu.width > 0 ? Vec2((float)gpu.width, (float)gpu.height) : client; }
    float DpiScale() const override { return dpi; }
    const char* Name() const override { return "d3d11"; }

    void Minimize() override { ShowWindow(hwnd, SW_MINIMIZE); }
    void ToggleMaximize() override { ShowWindow(hwnd, IsZoomed(hwnd) ? SW_RESTORE : SW_MAXIMIZE); }
    bool IsMaximized() const override { return IsZoomed(hwnd) != 0; }
    void Close() override { quit = true; PostMessageW(hwnd, WM_CLOSE, 0, 0); }
    void BeginDrag() override {
        POINT pt;
        GetCursorPos(&pt);
        ReleaseCapture();
        SendMessageW(hwnd, WM_NCLBUTTONDOWN, HTCAPTION, 0);
    }
    void SetCursor(int cursor) override {
        SetCursor(g_cursors[std::clamp(cursor, 0, 3)]);
    }
    void SetIcon(const void* icoData, size_t size) override {
        if (!icoData || !size) return;
        HICON big = nullptr, small = nullptr;
        // window icon from embedded .ico bytes
        UINT off = 0;
        if (size < 6) return;
        const uint8_t* d = (const uint8_t*)icoData;
        UINT count = d[4] | (d[5] << 8);
        for (UINT i = 0; i < count; i++) {
            const uint8_t* e = d + 6 + i * 16;
            UINT entrySize = e[8] | (e[9] << 8) | (e[10] << 16) | (e[11] << 24);
            UINT entryOff = e[12] | (e[13] << 8) | (e[14] << 16) | (e[15] << 24);
            if (entryOff + entrySize > size) continue;
            HICON ic = CreateIconFromResourceEx((PBYTE)(d + entryOff), entrySize, TRUE, 0x00030000, e[0] == 0 ? 256 : e[0],
                                                e[1] == 0 ? 256 : e[1], LR_DEFAULTCOLOR);
            if (!ic) continue;
            if ((e[0] == 0 ? 256 : e[0]) >= 32) {
                if (big) DestroyIcon(big);
                big = ic;
            } else {
                if (small) DestroyIcon(small);
                small = ic;
            }
        }
        if (big) SendMessageW(hwnd, WM_SETICON, ICON_BIG, (LPARAM)big);
        if (small) SendMessageW(hwnd, WM_SETICON, ICON_SMALL, (LPARAM)small);
        (void)off;
    }
    void SetClipboard(const string& utf8) override {
        std::wstring w = ToWide(utf8);
        if (!OpenClipboard(hwnd)) return;
        EmptyClipboard();
        size_t bytes = (w.size() + 1) * sizeof(wchar_t);
        HGLOBAL mem = GlobalAlloc(GMEM_MOVEABLE, bytes);
        if (mem) {
            void* p = GlobalLock(mem);
            memcpy(p, w.c_str(), bytes);
            GlobalUnlock(mem);
            SetClipboardData(CF_UNICODETEXT, mem);
        }
        CloseClipboard();
    }

    void RenderFrame(RendererState& state) { gpu.Frame(state); }

    static LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
        WinPlatform* self = (WinPlatform*)GetWindowLongPtrW(hwnd, GWLP_USERDATA);
        switch (msg) {
            case WM_NCCREATE: {
                auto cs = (CREATESTRUCTW*)lp;
                SetWindowLongPtrW(hwnd, GWLP_USERDATA, (LONG_PTR)cs->lpCreateParams);
                return TRUE;
            }
            case WM_SIZE: {
                if (self && wp != SIZE_MINIMIZED) {
                    self->client = Vec2((float)LOWORD(lp), (float)HIWORD(lp));
                    self->Resize((int)self->client.x, (int)self->client.y);
                }
                return 0;
            }
            case WM_DPICHANGED: {
                if (self) {
                    self->dpi = (float)HIWORD(wp) / 96.f;
                    RECT* rc = (RECT*)lp;
                    SetWindowPos(hwnd, nullptr, rc->left, rc->top, rc->right - rc->left, rc->bottom - rc->top,
                                 SWP_NOZORDER | SWP_NOACTIVATE);
                }
                return 0;
            }
            case WM_MOUSEMOVE: {
                if (g_input) {
                    g_input->mouse = Vec2((float)GET_X_LPARAM(lp), (float)GET_Y_LPARAM(lp));
                    if (!self->trackingLeave) {
                        TRACKMOUSEEVENT tme{sizeof(tme), TME_LEAVE, hwnd, 0};
                        TrackMouseEvent(&tme);
                        self->trackingLeave = true;
                    }
                }
                return 0;
            }
            case WM_MOUSELEAVE: {
                if (g_input) g_input->mouse = Vec2(-10000.f, -10000.f);
                if (self) self->trackingLeave = false;
                return 0;
            }
            case WM_LBUTTONDOWN: {
                if (g_input) {
                    g_input->down = true;
                    g_input->pressed = true;
                    SetCapture(hwnd);
                }
                return 0;
            }
            case WM_LBUTTONUP: {
                if (g_input) {
                    g_input->down = false;
                    g_input->released = true;
                    ReleaseCapture();
                }
                return 0;
            }
            case WM_MOUSEWHEEL: {
                if (g_input) g_input->wheel += (float)GET_WHEEL_DELTA_WPARAM(wp) / 120.f;
                return 0;
            }
            case WM_CHAR: {
                if (g_input) {
                    wchar_t c = (wchar_t)wp;
                    if (c == VK_BACK || c == 8) g_input->keyBackspace = true;
                    else if (c == '\r') g_input->keyEnter = true;
                    else if (c == 27) g_input->keyEscape = true;
                    else if (c == '\t') g_input->keyTab = true;
                    else if (c >= 32) g_input->textInput.push_back((uint32_t)c);
                }
                return 0;
            }
            case WM_KEYDOWN: {
                if (g_input) {
                    if (wp == VK_BACK) g_input->keyBackspace = true;
                    if (wp == VK_RETURN) g_input->keyEnter = true;
                    if (wp == VK_ESCAPE) g_input->keyEscape = true;
                    if (wp == VK_TAB) g_input->keyTab = true;
                    g_input->ctrlDown = (GetKeyState(VK_CONTROL) & 0x8000) != 0;
                    g_input->shiftDown = (GetKeyState(VK_SHIFT) & 0x8000) != 0;
                    if (wp == 'V' && g_input->ctrlDown) {
                        if (OpenClipboard(hwnd)) {
                            HANDLE h = GetClipboardData(CF_UNICODETEXT);
                            if (h) {
                                wchar_t* p = (wchar_t*)GlobalLock(h);
                                if (p) {
                                    for (wchar_t* q = p; *q; q++) g_input->textInput.push_back((uint32_t)*q);
                                    GlobalUnlock(h);
                                }
                            }
                            CloseClipboard();
                        }
                    }
                }
                return 0;
            }
            case WM_SETCURSOR: {
                // keep the cursor we set from the render loop
                return TRUE;
            }
            case WM_GETMINMAXINFO: {
                auto mmi = (MINMAXINFO*)lp;
                mmi->ptMinTrackSize.x = 700;
                mmi->ptMinTrackSize.y = 480;
                return 0;
            }
            case WM_CLOSE: {
                if (self) self->quit = true;
                return 0;
            }
            case WM_DESTROY: {
                PostQuitMessage(0);
                return 0;
            }
            case WM_ERASEBKGND:
                return 1;
        }
        return DefWindowProcW(hwnd, msg, wp, lp);
    }
};

void RendererState_SetInput(InputState* in) { g_input = in; }

namespace {
std::mutex& RenderMutex() {
    static std::mutex m;
    return m;
}
}  // namespace
void RendererLockInit() {}
RenderLock::RenderLock() { RenderMutex().lock(); }
RenderLock::~RenderLock() { RenderMutex().unlock(); }

void PlatformSleep(int ms) { Sleep((DWORD)ms); }

Platform* CreatePlatform() { return new WinPlatform(); }
void DestroyPlatform(Platform* p) { delete p; }

WinPlatform* AsWinPlatform(Platform* p) { return dynamic_cast<WinPlatform*>(p); }

// ================================================================ file system
string ExePath() {
    wchar_t buf[MAX_PATH * 2];
    DWORD n = GetModuleFileNameW(nullptr, buf, MAX_PATH * 2);
    return ToUtf8(std::wstring(buf, n));
}
string ExeDir() {
    string p = ExePath();
    size_t s = p.find_last_of("\\/");
    return s == string::npos ? p : p.substr(0, s);
}
string AppDataDir() {
    wchar_t* path = nullptr;
    string base;
    if (SUCCEEDED(SHGetKnownFolderPath(FOLDERID_RoamingAppData, 0, nullptr, &path))) {
        base = ToUtf8(path);
        CoTaskMemFree(path);
    } else {
        base = ExeDir();
    }
    string dir = PathJoin(base, "CritLauncher");
    MakeDirs(dir);
    return dir;
}
bool FileExists(const string& path) {
    DWORD a = GetFileAttributesW(ToWide(path).c_str());
    return a != INVALID_FILE_ATTRIBUTES && !(a & FILE_ATTRIBUTE_DIRECTORY);
}
bool DirExists(const string& path) {
    DWORD a = GetFileAttributesW(ToWide(path).c_str());
    return a != INVALID_FILE_ATTRIBUTES && (a & FILE_ATTRIBUTE_DIRECTORY);
}
bool MakeDirs(const string& path) {
    std::wstring w = ToWide(path);
    if (w.empty()) return false;
    for (size_t i = 0; i < w.size(); i++) {
        if (w[i] == L'\\' || w[i] == L'/') {
            std::wstring sub = w.substr(0, i);
            if (sub.size() >= 2 && !(sub.size() == 2 && sub[1] == L':')) CreateDirectoryW(sub.c_str(), nullptr);
        }
    }
    CreateDirectoryW(w.c_str(), nullptr);
    return DirExists(path);
}
bool IsDir(const string& path) { return DirExists(path); }
std::vector<string> ListDir(const string& path) {
    std::vector<string> out;
    WIN32_FIND_DATAW fd;
    HANDLE h = FindFirstFileW(ToWide(PathJoin(path, "*")).c_str(), &fd);
    if (h == INVALID_HANDLE_VALUE) return out;
    do {
        std::wstring name = fd.cFileName;
        if (name == L"." || name == L"..") continue;
        out.push_back(ToUtf8(name));
    } while (FindNextFileW(h, &fd));
    FindClose(h);
    std::sort(out.begin(), out.end(), [](const string& a, const string& b) { return Lower(a) < Lower(b); });
    return out;
}
string ReadFileText(const string& path) {
    std::vector<uint8_t> data = ReadFileBytes(path);
    return string(data.begin(), data.end());
}
std::vector<uint8_t> ReadFileBytes(const string& path) {
    std::vector<uint8_t> out;
    HANDLE f = CreateFileW(ToWide(path).c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING,
                           FILE_ATTRIBUTE_NORMAL, nullptr);
    if (f == INVALID_HANDLE_VALUE) return out;
    LARGE_INTEGER size;
    if (GetFileSizeEx(f, &size)) {
        out.resize((size_t)size.QuadPart);
        DWORD read = 0;
        size_t total = 0;
        while (total < out.size() && ReadFile(f, out.data() + total, (DWORD)(out.size() - total), &read, nullptr))
            total += read;
        out.resize(total);
    }
    CloseHandle(f);
    return out;
}
bool WriteFileText(const string& path, const string& text) {
    return WriteFileBytes(path, std::vector<uint8_t>(text.begin(), text.end()));
}
bool WriteFileBytes(const string& path, const std::vector<uint8_t>& data) {
    MakeDirs(PathParent(path));
    HANDLE f = CreateFileW(ToWide(path).c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL,
                           nullptr);
    if (f == INVALID_HANDLE_VALUE) return false;
    DWORD written = 0;
    BOOL ok = WriteFile(f, data.data(), (DWORD)data.size(), &written, nullptr);
    CloseHandle(f);
    return ok && written == data.size();
}
uint64_t FileSize(const string& path) {
    WIN32_FILE_ATTRIBUTE_DATA d{};
    if (!GetFileAttributesExW(ToWide(path).c_str(), GetFileExInfoStandard, &d)) return 0;
    return ((uint64_t)d.nFileSizeHigh << 32) | d.nFileSizeLow;
}
uint64_t FileMTime(const string& path) {
    WIN32_FILE_ATTRIBUTE_DATA d{};
    if (!GetFileAttributesExW(ToWide(path).c_str(), GetFileExInfoStandard, &d)) return 0;
    return ((uint64_t)d.ftLastWriteTime.dwHighDateTime << 32) | d.ftLastWriteTime.dwLowDateTime;
}
bool DeleteFileAt(const string& path) { return DeleteFileW(ToWide(path).c_str()) != 0; }
bool RemoveDirRecursive(const string& path) {
    std::wstring w = ToWide(path);
    w.push_back(0);
    SHFILEOPSTRUCTW op{};
    op.wFunc = FO_DELETE;
    op.pFrom = w.c_str();
    op.fFlags = FOF_NO_UI | FOF_SILENT | FOF_NOCONFIRMATION;
    return SHFileOperationW(&op) == 0;
}
bool RenamePath(const string& from, const string& to) {
    return MoveFileExW(ToWide(from).c_str(), ToWide(to).c_str(), MOVEFILE_REPLACE_EXISTING) != 0;
}
string PathJoin(const string& a, const string& b) {
    if (a.empty()) return b;
    if (!a.empty() && (a.back() == '\\' || a.back() == '/')) return a + b;
    return a + "\\" + b;
}
string PathFileName(const string& path) {
    size_t s = path.find_last_of("\\/");
    return s == string::npos ? path : path.substr(s + 1);
}
string PathExtension(const string& path) {
    string name = PathFileName(path);
    size_t s = name.find_last_of('.');
    return s == string::npos ? "" : Lower(name.substr(s));
}
string PathParent(const string& path) {
    size_t s = path.find_last_of("\\/");
    return s == string::npos ? "" : path.substr(0, s);
}
bool OpenFolder(const string& path) {
    MakeDirs(path);
    return (INT_PTR)ShellExecuteW(nullptr, L"open", ToWide(path).c_str(), nullptr, nullptr, SW_SHOWNORMAL) > 32;
}
bool OpenUrl(const string& url) {
    return (INT_PTR)ShellExecuteW(nullptr, L"open", ToWide(url).c_str(), nullptr, nullptr, SW_SHOWNORMAL) > 32;
}
bool RunDetached(const string& exe, const std::vector<string>& args, const string& cwd) {
    string cmd = "\"" + exe + "\"";
    for (auto& a : args) cmd += " \"" + a + "\"";
    std::wstring wcmd = ToWide(cmd), wcwd = ToWide(cwd);
    STARTUPINFOW si{};
    si.cb = sizeof(si);
    PROCESS_INFORMATION pi{};
    BOOL ok = CreateProcessW(nullptr, wcmd.data(), nullptr, nullptr, FALSE, 0, nullptr, cwd.empty() ? nullptr : wcwd.c_str(),
                             &si, &pi);
    if (!ok) return false;
    CloseHandle(pi.hThread);
    CloseHandle(pi.hProcess);
    return true;
}

struct ProcessHandle {
    HANDLE process = nullptr;
    HANDLE readPipe = nullptr;
    HANDLE writePipe = nullptr;
    std::shared_ptr<std::string> output = std::make_shared<string>();
    int exitCode = -1;
    bool exited = false;
};

std::shared_ptr<ProcessHandle> SpawnProcess(const string& exe, const std::vector<string>& args, const string& cwd,
                                            bool hidden) {
    auto h = std::make_shared<ProcessHandle>();
    SECURITY_ATTRIBUTES sa{sizeof(sa), nullptr, TRUE};
    CreatePipe(&h->readPipe, &h->writePipe, &sa, 0);
    SetHandleInformation(h->readPipe, HANDLE_FLAG_INHERIT, 0);
    string cmd = "\"" + exe + "\"";
    for (auto& a : args) cmd += " \"" + a + "\"";
    std::wstring wcmd = ToWide(cmd);
    STARTUPINFOW si{};
    si.cb = sizeof(si);
    si.dwFlags = STARTF_USESTDHANDLES;
    si.hStdOutput = h->writePipe;
    si.hStdError = h->writePipe;
    si.hStdInput = nullptr;
    if (hidden) {
        si.dwFlags |= STARTF_USESHOWWINDOW;
        si.wShowWindow = SW_HIDE;
    }
    PROCESS_INFORMATION pi{};
    std::wstring wcwd = ToWide(cwd);
    std::vector<wchar_t> mod(cmd.begin(), cmd.end());
    BOOL ok = CreateProcessW(nullptr, wcmd.data(), nullptr, nullptr, TRUE, hidden ? CREATE_NO_WINDOW : 0, nullptr,
                             cwd.empty() ? nullptr : wcwd.c_str(), &si, &pi);
    if (!ok) return nullptr;
    CloseHandle(h->writePipe);
    h->writePipe = nullptr;
    CloseHandle(pi.hThread);
    h->process = pi.hProcess;
    return h;
}

bool ProcessAlive(const std::shared_ptr<ProcessHandle>& h) {
    if (!h || !h->process) return false;
    DWORD code = 0;
    if (GetExitCodeProcess(h->process, &code)) {
        if (code != STILL_ACTIVE) {
            h->exitCode = (int)code;
            h->exited = true;
            return false;
        }
        return true;
    }
    return false;
}
int ProcessExitCode(const std::shared_ptr<ProcessHandle>& h) { return h ? h->exitCode : -1; }
uint64_t ProcessPid(const std::shared_ptr<ProcessHandle>& h) {
    return h && h->process ? (uint64_t)GetProcessId(h->process) : 0;
}
void ProcessKill(const std::shared_ptr<ProcessHandle>& h) {
    if (h && h->process) TerminateProcess(h->process, 0);
}
bool ProcessReadOutput(const std::shared_ptr<ProcessHandle>& h, string& out) {
    if (!h || !h->readPipe) return false;
    DWORD avail = 0;
    if (!PeekNamedPipe(h->readPipe, nullptr, 0, nullptr, &avail, nullptr) || avail == 0) return false;
    string buf(avail, '\0');
    DWORD read = 0;
    if (!ReadFile(h->readPipe, buf.data(), avail, &read, nullptr) || read == 0) return false;
    buf.resize(read);
    h->output->append(buf);
    out = buf;
    return true;
}

bool RunCapture(const string& exe, const std::vector<string>& args, string& out, int timeoutMs) {
    auto h = SpawnProcess(exe, args, "", true);
    if (!h) return false;
    double start = NowSeconds();
    char buf[4096];
    for (;;) {
        if (h->readPipe) {
            DWORD avail = 0;
            if (PeekNamedPipe(h->readPipe, nullptr, 0, nullptr, &avail, nullptr) && avail > 0) {
                DWORD read = 0;
                if (ReadFile(h->readPipe, buf, std::min<DWORD>(avail, sizeof buf), &read, nullptr) && read) {
                    out.append(buf, read);
                }
            }
        }
        if (!ProcessAlive(h)) break;
        if ((NowSeconds() - start) * 1000.0 > timeoutMs) {
            ProcessKill(h);
            return false;
        }
        Sleep(10);
    }
    // drain
    if (h->readPipe) {
        DWORD avail = 0;
        while (PeekNamedPipe(h->readPipe, nullptr, 0, nullptr, &avail, nullptr) && avail > 0) {
            DWORD read = 0;
            if (ReadFile(h->readPipe, buf, std::min<DWORD>(avail, sizeof buf), &read, nullptr) && read) out.append(buf, read);
            else break;
        }
    }
    return true;
}

int ReadClipboardText(string& out) {
    if (!OpenClipboard(nullptr)) return 0;
    HANDLE h = GetClipboardData(CF_UNICODETEXT);
    if (h) {
        const wchar_t* p = (const wchar_t*)GlobalLock(h);
        if (p) {
            out = ToUtf8(p);
            GlobalUnlock(h);
        }
    }
    CloseClipboard();
    return (int)out.size();
}

void GetMemoryInfo(uint64_t& totalMB, uint64_t& availMB) {
    MEMORYSTATUSEX ms{};
    ms.dwLength = sizeof ms;
    GlobalMemoryStatusEx(&ms);
    totalMB = ms.ullTotalPhys / (1024 * 1024);
    availMB = ms.ullAvailPhys / (1024 * 1024);
}

string SystemSummary() {
    OSVERSIONINFOEXW vi{};
    vi.dwOSVersionInfoSize = sizeof vi;
    uint64_t total = 0, avail = 0;
    GetMemoryInfo(total, avail);
    return Format("Windows %lu.%lu, %llu MB RAM", (unsigned long)vi.dwMajorVersion, (unsigned long)vi.dwMinorVersion,
                  (unsigned long long)total);
}

// ================================================================ crypto helpers
namespace {
struct Sha1Ctx {
    uint32_t h[5] = {0x67452301, 0xEFCDAB89, 0x98BADCFE, 0x10325476, 0xC3D2E1F0};
    uint64_t len = 0;
    uint8_t buf[64];
    size_t bufLen = 0;
    static uint32_t Rol(uint32_t v, int n) { return (v << n) | (v >> (32 - n)); }
    void Block(const uint8_t* p) {
        uint32_t w[80];
        for (int i = 0; i < 16; i++) w[i] = (p[i * 4] << 24) | (p[i * 4 + 1] << 16) | (p[i * 4 + 2] << 8) | p[i * 4 + 3];
        for (int i = 16; i < 80; i++) w[i] = Rol(w[i - 3] ^ w[i - 8] ^ w[i - 14] ^ w[i - 16], 1);
        uint32_t a = h[0], b = h[1], c = h[2], d = h[3], e = h[4];
        for (int i = 0; i < 80; i++) {
            uint32_t f, k;
            if (i < 20) { f = (b & c) | ((~b) & d); k = 0x5A827999; }
            else if (i < 40) { f = b ^ c ^ d; k = 0x6ED9EBA1; }
            else if (i < 60) { f = (b & c) | (b & d) | (c & d); k = 0x8F1BBCDC; }
            else { f = b ^ c ^ d; k = 0xCA62C1D6; }
            uint32_t t = Rol(a, 5) + f + e + k + w[i];
            e = d; d = c; c = Rol(b, 30); b = a; a = t;
        }
        h[0] += a; h[1] += b; h[2] += c; h[3] += d; h[4] += e;
    }
    void Update(const uint8_t* p, size_t n) {
        len += n;
        while (n) {
            size_t take = std::min(n, 64 - bufLen);
            memcpy(buf + bufLen, p, take);
            bufLen += take;
            p += take;
            n -= take;
            if (bufLen == 64) {
                Block(buf);
                bufLen = 0;
            }
        }
    }
    string Final() {
        uint64_t bits = len * 8;
        uint8_t pad = 0x80;
        Update(&pad, 1);
        uint8_t zero = 0;
        while (bufLen != 56) Update(&zero, 1);
        uint8_t lenBuf[8];
        for (int i = 0; i < 8; i++) lenBuf[i] = (uint8_t)(bits >> (56 - i * 8));
        Update(lenBuf, 8);
        string out;
        for (int i = 0; i < 5; i++) {
            char b[9];
            snprintf(b, sizeof b, "%08x", h[i]);
            out += b;
        }
        return out;
    }
};
}  // namespace

string Sha1Hex(const std::vector<uint8_t>& data) {
    Sha1Ctx c;
    c.Update(data.data(), data.size());
    return c.Final();
}

string Sha1File(const string& path) {
    std::vector<uint8_t> data = ReadFileBytes(path);
    return Sha1Hex(data);
}

string Md5Hex(const string& input) { return Sha1Hex(std::vector<uint8_t>(input.begin(), input.end())); }

string Base64Encode(const std::vector<uint8_t>& data) {
    static const char* tbl = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    string out;
    size_t i = 0;
    while (i + 2 < data.size()) {
        uint32_t v = (data[i] << 16) | (data[i + 1] << 8) | data[i + 2];
        out += tbl[(v >> 18) & 63];
        out += tbl[(v >> 12) & 63];
        out += tbl[(v >> 6) & 63];
        out += tbl[v & 63];
        i += 3;
    }
    if (i + 1 == data.size()) {
        uint32_t v = data[i] << 16;
        out += tbl[(v >> 18) & 63];
        out += tbl[(v >> 12) & 63];
        out += "==";
    } else if (i + 2 == data.size()) {
        uint32_t v = (data[i] << 16) | (data[i + 1] << 8);
        out += tbl[(v >> 18) & 63];
        out += tbl[(v >> 12) & 63];
        out += tbl[(v >> 6) & 63];
        out += '=';
    }
    return out;
}

std::vector<uint8_t> Base64Decode(const string& s) {
    auto val = [](char c) -> int {
        if (c >= 'A' && c <= 'Z') return c - 'A';
        if (c >= 'a' && c <= 'z') return c - 'a' + 26;
        if (c >= '0' && c <= '9') return c - '0' + 52;
        if (c == '+') return 62;
        if (c == '/') return 63;
        return -1;
    };
    std::vector<uint8_t> out;
    int buf = 0, bits = 0;
    for (char c : s) {
        int v = val(c);
        if (v < 0) continue;
        buf = (buf << 6) | v;
        bits += 6;
        if (bits >= 8) {
            bits -= 8;
            out.push_back((uint8_t)((buf >> bits) & 0xff));
        }
    }
    return out;
}

// ================================================================ app resources
void Platform::Post(std::function<void()> fn) {
    std::lock_guard<std::mutex> lk(postedMutex_);
    posted_.push_back(std::move(fn));
}
void Platform::RunPosted() {
    std::vector<std::function<void()>> run;
    {
        std::lock_guard<std::mutex> lk(postedMutex_);
        std::swap(run, posted_);
    }
    for (auto& f : run) f();
}

}  // namespace crit

extern "C" {
void crit_pump_platform() {}
}

#endif  // _WIN32
