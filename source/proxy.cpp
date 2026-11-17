// RaceRoom Triples - Direct3D 9 proxy DLL (d3d9.dll) for RRRE64.exe.
//
// What it does
//   RaceRoom only offers resolutions that the graphics adapter reports as
//   display modes. Three independent monitors never report a combined mode
//   such as 7680x1440, so the game will not render that wide. This DLL sits
//   between the game and the real Direct3D 9 runtime and appends the
//   configured triple-screen sizes to the adapter's mode list. Everything
//   else is forwarded unchanged.
//
// How it is put together
//   1. Start-up: work out the log file name, load the real d3d9.dll from the
//      Windows system directory and read RaceRoomTriple.ini.
//   2. D3DProxy wraps IDirect3D9/IDirect3D9Ex. It extends the mode
//      enumeration and hands out wrapped devices.
//   3. DeviceProxy wraps IDirect3DDevice9/IDirect3DDevice9Ex. It refuses
//      exclusive fullscreen and logs the back buffer size really created.
//   4. The exported functions at the bottom are what the game imports.
//
// Experimental. No EXE patch and no driver configuration: the game
// executable, Windows display modes, registry and drivers are not touched.
// This is deliberately not a general DLL injector.

#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <d3d9.h>
#include <cstdarg>
#include <cstdio>
#include <cwchar>
#include <new>
#include "version.h"
#include "policy.h"
#include "config.h"

// ---------------------------------------------------------------------------
// Process-wide state. Written during start-up, read-only afterwards.
// ---------------------------------------------------------------------------

// Guards init() so start-up runs once, whichever export is called first.
static INIT_ONCE initOnce = INIT_ONCE_STATIC_INIT;

// The real Direct3D 9 runtime from the Windows system directory.
static HMODULE realDll = nullptr;

// The functions of the real d3d9.dll that this DLL exports under the same
// name, looked up once during start-up. An entry is null if the real DLL
// could not be loaded or does not have that function.
static struct RealApi {
    IDirect3D9* (WINAPI* Direct3DCreate9)(UINT);
    HRESULT (WINAPI* Direct3DCreate9Ex)(UINT, IDirect3D9Ex**);
    int (WINAPI* D3DPERF_BeginEvent)(D3DCOLOR, const WCHAR*);
    int (WINAPI* D3DPERF_EndEvent)();
    DWORD (WINAPI* D3DPERF_GetStatus)();
    BOOL (WINAPI* D3DPERF_QueryRepeatFrame)();
    void (WINAPI* D3DPERF_SetMarker)(D3DCOLOR, const WCHAR*);
    void (WINAPI* D3DPERF_SetOptions)(DWORD);
    void (WINAPI* D3DPERF_SetRegion)(D3DCOLOR, const WCHAR*);
    void (WINAPI* DebugSetMute)();
    void (WINAPI* Direct3D9EnableMaximizedWindowedModeShim)(BOOL);
    void* (WINAPI* Direct3DShaderValidatorCreate9)();
} realApi;

// Full path of this process's log file. Empty means logging is unavailable.
static wchar_t logPath[MAX_PATH] = {};

// Serialises log writes coming from different threads.
static SRWLOCK logLock = SRWLOCK_INIT;

// The modes offered to the game: parsed from the INI, or the built-in defaults.
static rrconfig::Config configured;

// Where `configured` came from, for the log: "ini" or a "defaults-..." reason.
static const char* configSource = "defaults-not-loaded";

// Windows error code that belongs to configSource, 0 if there is none.
static DWORD configError = 0;

// Full path of RaceRoomTriple.ini. Sized for long paths: 32767 characters
// plus terminator.
constexpr DWORD maxPathChars = 32768;
static wchar_t configPath[maxPathChars] = {};

// Raw contents of the INI file. Static so that 64 KiB do not land on the stack.
static char configBytes[rrconfig::maxBytes];

// Only its address matters. It lies inside this DLL, which is how loadConfig()
// finds the DLL's own path.
static char moduleAnchor;

// ---------------------------------------------------------------------------
// Logging
// ---------------------------------------------------------------------------

// Appends one printf-style line with a time stamp to the log file. The file is
// opened and closed for every line, so nothing is lost if the game crashes.
// Does nothing until start-up has set logPath.
static void note(const char* fmt, ...) {
    if (!logPath[0]) return;

    char message[1024];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(message, sizeof(message), fmt, ap);
    va_end(ap);

    SYSTEMTIME t;
    GetLocalTime(&t);
    char line[1200];
    int len = snprintf(line, sizeof(line), "%02u:%02u:%02u %s\r\n", t.wHour, t.wMinute, t.wSecond, message);
    if (len < 0) return;

    AcquireSRWLockExclusive(&logLock);
    HANDLE f = CreateFileW(logPath, FILE_APPEND_DATA, FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (f != INVALID_HANDLE_VALUE) {
        DWORD written;
        WriteFile(f, line, (DWORD)len, &written, nullptr);
        CloseHandle(f);
    }
    ReleaseSRWLockExclusive(&logLock);
}

// Sets logPath to %LOCALAPPDATA%\RaceRoomTripleHelper\d3d9-<process id>.log.
// Leaves it empty if the path would not fit.
static void prepareLogPath() {
    constexpr wchar_t folder[] = L"\\RaceRoomTripleHelper";
    constexpr wchar_t longestFile[] = L"\\d3d9-4294967295.log";
    wchar_t dir[MAX_PATH];
    const DWORD n = GetEnvironmentVariableW(L"LOCALAPPDATA", dir, MAX_PATH);
    if (!n || n + std::wcslen(folder) + std::wcslen(longestFile) >= MAX_PATH) return;
    wcscat_s(dir, folder);
    CreateDirectoryW(dir, nullptr);
    swprintf_s(logPath, L"%ls\\d3d9-%lu.log", dir, GetCurrentProcessId());
}

// ---------------------------------------------------------------------------
// Configuration loading
// ---------------------------------------------------------------------------

// Falls back to the built-in modes and remembers why.
static void defaults(const char* reason, DWORD error = 0) {
    configSource = reason;
    configError = error;
    rrconfig::defaultModes(configured);
}

// Reads RaceRoomTriple.ini from the folder this DLL was loaded from, not from
// the current working directory. Every failure ends in defaults() with a
// reason that shows up in the log.
static void loadConfig() {
    // Step 1: find the path of this DLL.
    HMODULE module = nullptr;
    if (!GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                            reinterpret_cast<LPCWSTR>(&moduleAnchor), &module)) {
        defaults("defaults-module-path", GetLastError());
        return;
    }
    const DWORD n = GetModuleFileNameW(module, configPath, maxPathChars);
    if (!n || n >= maxPathChars) {  // 0 = failed, full buffer = path was cut off
        const DWORD error = GetLastError();
        configPath[0] = 0;
        defaults("defaults-module-path", error);
        return;
    }

    // Step 2: replace the file name (d3d9.dll) with the name of the INI.
    wchar_t* slash = std::wcsrchr(configPath, L'\\');
    constexpr wchar_t iniName[] = L"RaceRoomTriple.ini";
    const std::size_t remaining = slash ? maxPathChars - std::size_t(slash + 1 - configPath) : 0;
    if (remaining < sizeof(iniName) / sizeof(iniName[0])) {
        configPath[0] = 0;
        defaults("defaults-module-path");
        return;
    }
    wcscpy_s(slash + 1, remaining, iniName);

    // Step 3: open read-only. Full sharing, so an editor that still has the
    // file open does not block us.
    HANDLE file = CreateFileW(configPath, GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                              nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (file == INVALID_HANDLE_VALUE) {
        const DWORD error = GetLastError();
        defaults(error == ERROR_FILE_NOT_FOUND || error == ERROR_PATH_NOT_FOUND ? "defaults-ini-missing" : "defaults-open-error", error);
        return;
    }

    // Step 4: read the whole file at once. Anything larger than the buffer is
    // refused.
    LARGE_INTEGER size = {};
    if (!GetFileSizeEx(file, &size)) {
        const DWORD error = GetLastError();
        CloseHandle(file);
        defaults("defaults-file-size", error);
        return;
    }
    if (size.QuadPart < 0 || size.QuadPart > static_cast<LONGLONG>(rrconfig::maxBytes)) {
        CloseHandle(file);
        defaults("defaults-file-too-large");
        return;
    }
    DWORD read = 0;
    const BOOL ok = ReadFile(file, configBytes, static_cast<DWORD>(size.QuadPart), &read, nullptr);
    const DWORD error = ok ? 0 : GetLastError();
    CloseHandle(file);
    if (!ok || read != static_cast<DWORD>(size.QuadPart)) {
        defaults("defaults-read-error", error);
        return;
    }

    // Step 5: parse. An INI without a single usable mode counts as unusable.
    if (!rrconfig::parse(configBytes, read, configured)) {
        defaults("defaults-invalid-encoding/size");
        return;
    }
    if (!configured.count) {
        defaults("defaults-no-valid-modes");
        return;
    }
    configSource = "ini";
}

// Writes what was loaded to the log: where the modes came from, which INI
// lines were ignored and why, and the final list of modes.
static void configSummary() {
    note("CONFIG version=" RRT_VERSION " source=%s count=%u strict48x9=%u duplicates=%u issues=%u winerror=%lu",
         configSource, configured.count, configured.strict, configured.duplicates, configured.issueCount, configError);

    // The path as UTF-8, so that folder names in any language survive.
    char path[900];
    if (configPath[0] && WideCharToMultiByte(CP_UTF8, 0, configPath, -1, path, sizeof(path), nullptr, nullptr))
        note("CONFIG path=%s", path);

    for (unsigned i = 0; i < configured.storedIssues; ++i)
        note("CONFIG ignored/problem line=%u reason=%s", configured.issues[i].line, rrconfig::reasonName(configured.issues[i].reason));
    for (unsigned i = 0; i < configured.count; ++i) {
        const auto& m = configured.modes[i];
        note("CONFIG mode=%ux%u@%u", m.width, m.height, m.refresh);
    }
}

// ---------------------------------------------------------------------------
// Start-up
// ---------------------------------------------------------------------------

// Stores the address of one function of the real DLL in its RealApi entry.
// The detour through void* is the usual way to turn the untyped address from
// GetProcAddress into a typed function pointer.
template <class Fn>
static void resolve(Fn& entry, const char* name) {
    entry = reinterpret_cast<Fn>(reinterpret_cast<void*>(GetProcAddress(realDll, name)));
}

// Loads the real runtime by absolute path, so that we never load ourselves,
// and fills realApi.
static void loadRealDll() {
    constexpr wchar_t fileName[] = L"\\d3d9.dll";
    wchar_t path[MAX_PATH];
    const UINT n = GetSystemDirectoryW(path, MAX_PATH);
    if (!n || n + std::wcslen(fileName) >= MAX_PATH) {
        note("ERROR: Windows system directory not available");
        return;
    }
    wcscat_s(path, fileName);
    realDll = LoadLibraryW(path);
    if (!realDll) {
        note("ERROR: real d3d9.dll not loaded, winerror=%lu", GetLastError());
        return;
    }
#define RESOLVE(name) resolve(realApi.name, #name)
    RESOLVE(Direct3DCreate9);
    RESOLVE(Direct3DCreate9Ex);
    RESOLVE(D3DPERF_BeginEvent);
    RESOLVE(D3DPERF_EndEvent);
    RESOLVE(D3DPERF_GetStatus);
    RESOLVE(D3DPERF_QueryRepeatFrame);
    RESOLVE(D3DPERF_SetMarker);
    RESOLVE(D3DPERF_SetOptions);
    RESOLVE(D3DPERF_SetRegion);
    RESOLVE(DebugSetMute);
    RESOLVE(Direct3D9EnableMaximizedWindowedModeShim);
    RESOLVE(Direct3DShaderValidatorCreate9);
#undef RESOLVE
}

// One-time start-up, run through initOnce. The log comes first so that the
// other steps can report problems.
static BOOL CALLBACK init(PINIT_ONCE, PVOID, PVOID*) {
    prepareLogPath();
    loadRealDll();
    loadConfig();
    configSummary();
    return TRUE;
}

// The functions of the real d3d9.dll. Every exported function of this DLL
// starts here, which is what triggers start-up on the first call.
static const RealApi& api() {
    InitOnceExecuteOnce(&initOnce, init, nullptr, nullptr);
    return realApi;
}

// ---------------------------------------------------------------------------
// Checks shared by D3DProxy and DeviceProxy
// ---------------------------------------------------------------------------

// The gate for every call that creates or resets a swap chain. Logs the
// request and refuses exclusive fullscreen, because the added modes do not
// exist as real display modes. `where` names the calling function in the log.
static bool allowed(D3DPRESENT_PARAMETERS* p, const char* where) {
    if (!p) {
        note("%s: null parameters rejected", where);
        return false;
    }
    note("%s: requested %ux%u Windowed=%u Hz=%u", where, p->BackBufferWidth, p->BackBufferHeight, p->Windowed, p->FullScreen_RefreshRateInHz);
    if (!p->Windowed) {
        note("BLOCKED: exclusive fullscreen is not supported by this experimental wrapper");
        return false;
    }
    return true;
}

// ---------------------------------------------------------------------------
// DeviceProxy: wrapper around the game's Direct3D device.
//
// Only the methods in the first block do anything of their own. All others
// forward straight to the real device.
//
// Known limit: objects created by the real device (textures, surfaces, swap
// chains and so on) are handed out unwrapped. Their GetDevice() returns the
// real device, not this wrapper.
// ---------------------------------------------------------------------------
class DeviceProxy final : public IDirect3DDevice9Ex {
    IDirect3DDevice9* real;            // the real device; we hold one reference
    IDirect3DDevice9Ex* ex = nullptr;  // the same device as Ex interface, null if it has none
    IDirect3D9* owner;                 // the D3DProxy that created us; GetDirect3D() returns it
    LONG refs = 1;                     // COM reference count of this wrapper
    unsigned lastW = 0, lastH = 0;     // back buffer size last written to the log
    unsigned frames = 0;               // Present calls so far, for the periodic size check
    LONG refusedQueries = 0;           // QueryInterface refusals so far, to limit log lines

    // How many refused QueryInterface calls are written to the log.
    static constexpr LONG loggedRefusals = 16;

public:
    // Takes over the caller's reference to `r`.
    DeviceProxy(IDirect3DDevice9* r, IDirect3D9* o) : real(r), owner(o) {
        owner->AddRef();
        real->QueryInterface(IID_IDirect3DDevice9Ex, (void**)&ex);
        Report();
    }
    ~DeviceProxy() {
        if (ex) ex->Release();
        real->Release();
        owner->Release();
    }

    // Logs the real back buffer size whenever it differs from the size logged
    // last. This line is the proof that the wide resolution was really created.
    void Report() {
        IDirect3DSurface9* s = nullptr;
        if (SUCCEEDED(real->GetBackBuffer(0, 0, D3DBACKBUFFER_TYPE_MONO, &s)) && s) {
            D3DSURFACE_DESC d = {};
            if (SUCCEEDED(s->GetDesc(&d)) && (lastW != d.Width || lastH != d.Height)) {
                lastW = d.Width;
                lastH = d.Height;
                note("ACTUAL BACKBUFFER: %ux%u format=%u", lastW, lastH, d.Format);
            }
            s->Release();
        }
    }

    // ---- IUnknown ----

    // Only the interfaces this class implements are answered. Anything else is
    // refused instead of being passed to the real device, which would hand
    // out an unwrapped object. The first refusals are logged with the
    // interface ID, so an incompatible overlay or tool can be identified.
    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID id, void** out) noexcept override {
        if (!out) return E_POINTER;
        *out = nullptr;
        if (IsEqualGUID(id, IID_IUnknown) || IsEqualGUID(id, IID_IDirect3DDevice9) || (ex && IsEqualGUID(id, IID_IDirect3DDevice9Ex))) {
            *out = static_cast<IDirect3DDevice9Ex*>(this);
            AddRef();
            return S_OK;
        }
        if (InterlockedIncrement(&refusedQueries) <= loggedRefusals)
            note("Device QueryInterface: unsupported interface {%08lX-%04X-%04X-%02X%02X-%02X%02X%02X%02X%02X%02X} rejected",
                 (unsigned long)id.Data1, id.Data2, id.Data3, id.Data4[0], id.Data4[1],
                 id.Data4[2], id.Data4[3], id.Data4[4], id.Data4[5], id.Data4[6], id.Data4[7]);
        return E_NOINTERFACE;
    }
    ULONG STDMETHODCALLTYPE AddRef() noexcept override { return (ULONG)InterlockedIncrement(&refs); }
    ULONG STDMETHODCALLTYPE Release() noexcept override {
        ULONG n = (ULONG)InterlockedDecrement(&refs);
        if (!n) delete this;
        return n;
    }

    // ---- Methods with behaviour of their own ----

    // Returns our D3DProxy rather than the real Direct3D object.
    HRESULT STDMETHODCALLTYPE GetDirect3D(IDirect3D9** out) noexcept override {
        if (!out) return E_POINTER;
        *out = owner;
        owner->AddRef();
        return S_OK;
    }

    // Reset is how the game changes resolution. Checked, forwarded, logged.
    HRESULT STDMETHODCALLTYPE Reset(D3DPRESENT_PARAMETERS* p) noexcept override {
        if (!allowed(p, "Reset")) return D3DERR_INVALIDCALL;
        HRESULT hr = real->Reset(p);
        note("Reset result=0x%08lx", (unsigned long)hr);
        if (SUCCEEDED(hr)) Report();
        return hr;
    }

    // The last parameter describes a fullscreen display mode. Fullscreen has
    // been refused by allowed(), so it is ignored and NULL is passed on, as
    // Direct3D expects for windowed mode.
    HRESULT STDMETHODCALLTYPE ResetEx(D3DPRESENT_PARAMETERS* p, D3DDISPLAYMODEEX* /*fullscreenMode*/) noexcept override {
        if (!ex || !allowed(p, "ResetEx")) return D3DERR_INVALIDCALL;
        HRESULT hr = ex->ResetEx(p, nullptr);
        note("ResetEx result=0x%08lx", (unsigned long)hr);
        if (SUCCEEDED(hr)) Report();
        return hr;
    }

    HRESULT STDMETHODCALLTYPE CreateAdditionalSwapChain(D3DPRESENT_PARAMETERS* p, IDirect3DSwapChain9** out) noexcept override {
        if (!out) return E_POINTER;
        *out = nullptr;
        if (!allowed(p, "CreateAdditionalSwapChain")) return D3DERR_INVALIDCALL;
        return real->CreateAdditionalSwapChain(p, out);
    }

    // Every 300th frame, check whether the back buffer size has changed. This
    // also catches a resize that went past Reset() above, through the real
    // device (see "Known limit").
    HRESULT STDMETHODCALLTYPE Present(const RECT* a, const RECT* b, HWND c, const RGNDATA* d) noexcept override {
        if ((frames++ % 300) == 0) Report();
        return real->Present(a, b, c, d);
    }
    HRESULT STDMETHODCALLTYPE PresentEx(const RECT* a, const RECT* b, HWND c, const RGNDATA* d, DWORD f) noexcept override {
        if (!ex) return D3DERR_INVALIDCALL;
        if ((frames++ % 300) == 0) Report();
        return ex->PresentEx(a, b, c, d, f);
    }

    // ---- Plain forwarding: IDirect3DDevice9 ----

    HRESULT STDMETHODCALLTYPE TestCooperativeLevel() noexcept override { return real->TestCooperativeLevel(); }
    UINT STDMETHODCALLTYPE GetAvailableTextureMem() noexcept override { return real->GetAvailableTextureMem(); }
    HRESULT STDMETHODCALLTYPE EvictManagedResources() noexcept override { return real->EvictManagedResources(); }
    HRESULT STDMETHODCALLTYPE GetDeviceCaps(D3DCAPS9 *caps) noexcept override { return real->GetDeviceCaps(caps); }
    HRESULT STDMETHODCALLTYPE GetDisplayMode(UINT swapchain_idx, D3DDISPLAYMODE *mode) noexcept override { return real->GetDisplayMode(swapchain_idx,mode); }
    HRESULT STDMETHODCALLTYPE GetCreationParameters(D3DDEVICE_CREATION_PARAMETERS *parameters) noexcept override { return real->GetCreationParameters(parameters); }
    HRESULT STDMETHODCALLTYPE SetCursorProperties(UINT hotspot_x, UINT hotspot_y, IDirect3DSurface9 *bitmap) noexcept override { return real->SetCursorProperties(hotspot_x,hotspot_y,bitmap); }
    void STDMETHODCALLTYPE SetCursorPosition(int x, int y, DWORD flags) noexcept override { return real->SetCursorPosition(x,y,flags); }
    WINBOOL STDMETHODCALLTYPE ShowCursor(WINBOOL show) noexcept override { return real->ShowCursor(show); }
    HRESULT STDMETHODCALLTYPE GetSwapChain(UINT swapchain_idx, IDirect3DSwapChain9 **swapchain) noexcept override { return real->GetSwapChain(swapchain_idx,swapchain); }
    UINT STDMETHODCALLTYPE GetNumberOfSwapChains() noexcept override { return real->GetNumberOfSwapChains(); }
    HRESULT STDMETHODCALLTYPE GetBackBuffer(UINT swapchain_idx, UINT backbuffer_idx, D3DBACKBUFFER_TYPE backbuffer_type, IDirect3DSurface9 **backbuffer) noexcept override { return real->GetBackBuffer(swapchain_idx,backbuffer_idx,backbuffer_type,backbuffer); }
    HRESULT STDMETHODCALLTYPE GetRasterStatus(UINT swapchain_idx, D3DRASTER_STATUS *raster_status) noexcept override { return real->GetRasterStatus(swapchain_idx,raster_status); }
    HRESULT STDMETHODCALLTYPE SetDialogBoxMode(WINBOOL enable) noexcept override { return real->SetDialogBoxMode(enable); }
    void STDMETHODCALLTYPE SetGammaRamp(UINT swapchain_idx, DWORD flags, const D3DGAMMARAMP *ramp) noexcept override { return real->SetGammaRamp(swapchain_idx,flags,ramp); }
    void STDMETHODCALLTYPE GetGammaRamp(UINT swapchain_idx, D3DGAMMARAMP *ramp) noexcept override { return real->GetGammaRamp(swapchain_idx,ramp); }
    HRESULT STDMETHODCALLTYPE CreateTexture(UINT width, UINT height, UINT levels, DWORD usage, D3DFORMAT format, D3DPOOL pool, IDirect3DTexture9 **texture, HANDLE *shared_handle) noexcept override { return real->CreateTexture(width,height,levels,usage,format,pool,texture,shared_handle); }
    HRESULT STDMETHODCALLTYPE CreateVolumeTexture(UINT width, UINT height, UINT depth, UINT levels, DWORD usage, D3DFORMAT format, D3DPOOL pool, IDirect3DVolumeTexture9 **texture, HANDLE *shared_handle) noexcept override { return real->CreateVolumeTexture(width,height,depth,levels,usage,format,pool,texture,shared_handle); }
    HRESULT STDMETHODCALLTYPE CreateCubeTexture(UINT edge_length, UINT levels, DWORD usage, D3DFORMAT format, D3DPOOL pool, IDirect3DCubeTexture9 **texture, HANDLE *shared_handle) noexcept override { return real->CreateCubeTexture(edge_length,levels,usage,format,pool,texture,shared_handle); }
    HRESULT STDMETHODCALLTYPE CreateVertexBuffer(UINT size, DWORD usage, DWORD fvf, D3DPOOL pool, IDirect3DVertexBuffer9 **buffer, HANDLE *shared_handle) noexcept override { return real->CreateVertexBuffer(size,usage,fvf,pool,buffer,shared_handle); }
    HRESULT STDMETHODCALLTYPE CreateIndexBuffer(UINT size, DWORD usage, D3DFORMAT format, D3DPOOL pool, IDirect3DIndexBuffer9 **buffer, HANDLE *shared_handle) noexcept override { return real->CreateIndexBuffer(size,usage,format,pool,buffer,shared_handle); }
    HRESULT STDMETHODCALLTYPE CreateRenderTarget(UINT width, UINT height, D3DFORMAT format, D3DMULTISAMPLE_TYPE multisample_type, DWORD multisample_quality, WINBOOL lockable, IDirect3DSurface9 **surface, HANDLE *shared_handle) noexcept override { return real->CreateRenderTarget(width,height,format,multisample_type,multisample_quality,lockable,surface,shared_handle); }
    HRESULT STDMETHODCALLTYPE CreateDepthStencilSurface(UINT width, UINT height, D3DFORMAT format, D3DMULTISAMPLE_TYPE multisample_type, DWORD multisample_quality, WINBOOL discard, IDirect3DSurface9 **surface, HANDLE *shared_handle) noexcept override { return real->CreateDepthStencilSurface(width,height,format,multisample_type,multisample_quality,discard,surface,shared_handle); }
    HRESULT STDMETHODCALLTYPE UpdateSurface(IDirect3DSurface9 *src_surface, const RECT *src_rect, IDirect3DSurface9 *dst_surface, const POINT *dst_point) noexcept override { return real->UpdateSurface(src_surface,src_rect,dst_surface,dst_point); }
    HRESULT STDMETHODCALLTYPE UpdateTexture(IDirect3DBaseTexture9 *src_texture, IDirect3DBaseTexture9 *dst_texture) noexcept override { return real->UpdateTexture(src_texture,dst_texture); }
    HRESULT STDMETHODCALLTYPE GetRenderTargetData(IDirect3DSurface9 *render_target, IDirect3DSurface9 *dst_surface) noexcept override { return real->GetRenderTargetData(render_target,dst_surface); }
    HRESULT STDMETHODCALLTYPE GetFrontBufferData(UINT swapchain_idx, IDirect3DSurface9 *dst_surface) noexcept override { return real->GetFrontBufferData(swapchain_idx,dst_surface); }
    HRESULT STDMETHODCALLTYPE StretchRect(IDirect3DSurface9 *src_surface, const RECT *src_rect, IDirect3DSurface9 *dst_surface, const RECT *dst_rect, D3DTEXTUREFILTERTYPE filter) noexcept override { return real->StretchRect(src_surface,src_rect,dst_surface,dst_rect,filter); }
    HRESULT STDMETHODCALLTYPE ColorFill(IDirect3DSurface9 *surface, const RECT *rect, D3DCOLOR colour) noexcept override { return real->ColorFill(surface,rect,colour); }
    HRESULT STDMETHODCALLTYPE CreateOffscreenPlainSurface(UINT width, UINT height, D3DFORMAT format, D3DPOOL pool, IDirect3DSurface9 **surface, HANDLE *shared_handle) noexcept override { return real->CreateOffscreenPlainSurface(width,height,format,pool,surface,shared_handle); }
    HRESULT STDMETHODCALLTYPE SetRenderTarget(DWORD idx, IDirect3DSurface9 *surface) noexcept override { return real->SetRenderTarget(idx,surface); }
    HRESULT STDMETHODCALLTYPE GetRenderTarget(DWORD idx, IDirect3DSurface9 **surface) noexcept override { return real->GetRenderTarget(idx,surface); }
    HRESULT STDMETHODCALLTYPE SetDepthStencilSurface(IDirect3DSurface9 *depth_stencil) noexcept override { return real->SetDepthStencilSurface(depth_stencil); }
    HRESULT STDMETHODCALLTYPE GetDepthStencilSurface(IDirect3DSurface9 **depth_stencil) noexcept override { return real->GetDepthStencilSurface(depth_stencil); }
    HRESULT STDMETHODCALLTYPE BeginScene() noexcept override { return real->BeginScene(); }
    HRESULT STDMETHODCALLTYPE EndScene() noexcept override { return real->EndScene(); }
    HRESULT STDMETHODCALLTYPE Clear(DWORD rect_count, const D3DRECT *rects, DWORD flags, D3DCOLOR colour, float z, DWORD stencil) noexcept override { return real->Clear(rect_count,rects,flags,colour,z,stencil); }
    HRESULT STDMETHODCALLTYPE SetTransform(D3DTRANSFORMSTATETYPE state, const D3DMATRIX *matrix) noexcept override { return real->SetTransform(state,matrix); }
    HRESULT STDMETHODCALLTYPE GetTransform(D3DTRANSFORMSTATETYPE State, D3DMATRIX *matrix) noexcept override { return real->GetTransform(State,matrix); }
    HRESULT STDMETHODCALLTYPE MultiplyTransform(D3DTRANSFORMSTATETYPE state, const D3DMATRIX *matrix) noexcept override { return real->MultiplyTransform(state,matrix); }
    HRESULT STDMETHODCALLTYPE SetViewport(const D3DVIEWPORT9 *viewport) noexcept override { return real->SetViewport(viewport); }
    HRESULT STDMETHODCALLTYPE GetViewport(D3DVIEWPORT9 *viewport) noexcept override { return real->GetViewport(viewport); }
    HRESULT STDMETHODCALLTYPE SetMaterial(const D3DMATERIAL9 *material) noexcept override { return real->SetMaterial(material); }
    HRESULT STDMETHODCALLTYPE GetMaterial(D3DMATERIAL9 *material) noexcept override { return real->GetMaterial(material); }
    HRESULT STDMETHODCALLTYPE SetLight(DWORD idx, const D3DLIGHT9 *light) noexcept override { return real->SetLight(idx,light); }
    HRESULT STDMETHODCALLTYPE GetLight(DWORD idx, D3DLIGHT9 *light) noexcept override { return real->GetLight(idx,light); }
    HRESULT STDMETHODCALLTYPE LightEnable(DWORD idx, WINBOOL enable) noexcept override { return real->LightEnable(idx,enable); }
    HRESULT STDMETHODCALLTYPE GetLightEnable(DWORD idx, WINBOOL *enable) noexcept override { return real->GetLightEnable(idx,enable); }
    HRESULT STDMETHODCALLTYPE SetClipPlane(DWORD idx, const float *plane) noexcept override { return real->SetClipPlane(idx,plane); }
    HRESULT STDMETHODCALLTYPE GetClipPlane(DWORD idx, float *plane) noexcept override { return real->GetClipPlane(idx,plane); }
    HRESULT STDMETHODCALLTYPE SetRenderState(D3DRENDERSTATETYPE state, DWORD value) noexcept override { return real->SetRenderState(state,value); }
    HRESULT STDMETHODCALLTYPE GetRenderState(D3DRENDERSTATETYPE state, DWORD *value) noexcept override { return real->GetRenderState(state,value); }
    HRESULT STDMETHODCALLTYPE CreateStateBlock(D3DSTATEBLOCKTYPE type, IDirect3DStateBlock9 **stateblock) noexcept override { return real->CreateStateBlock(type,stateblock); }
    HRESULT STDMETHODCALLTYPE BeginStateBlock() noexcept override { return real->BeginStateBlock(); }
    HRESULT STDMETHODCALLTYPE EndStateBlock(IDirect3DStateBlock9 **stateblock) noexcept override { return real->EndStateBlock(stateblock); }
    HRESULT STDMETHODCALLTYPE SetClipStatus(const D3DCLIPSTATUS9 *clip_status) noexcept override { return real->SetClipStatus(clip_status); }
    HRESULT STDMETHODCALLTYPE GetClipStatus(D3DCLIPSTATUS9 *clip_status) noexcept override { return real->GetClipStatus(clip_status); }
    HRESULT STDMETHODCALLTYPE GetTexture(DWORD stage, IDirect3DBaseTexture9 **texture) noexcept override { return real->GetTexture(stage,texture); }
    HRESULT STDMETHODCALLTYPE SetTexture(DWORD stage, IDirect3DBaseTexture9 *texture) noexcept override { return real->SetTexture(stage,texture); }
    HRESULT STDMETHODCALLTYPE GetTextureStageState(DWORD stage, D3DTEXTURESTAGESTATETYPE state, DWORD *value) noexcept override { return real->GetTextureStageState(stage,state,value); }
    HRESULT STDMETHODCALLTYPE SetTextureStageState(DWORD stage, D3DTEXTURESTAGESTATETYPE state, DWORD value) noexcept override { return real->SetTextureStageState(stage,state,value); }
    HRESULT STDMETHODCALLTYPE GetSamplerState(DWORD sampler_idx, D3DSAMPLERSTATETYPE state, DWORD *value) noexcept override { return real->GetSamplerState(sampler_idx,state,value); }
    HRESULT STDMETHODCALLTYPE SetSamplerState(DWORD sampler_idx, D3DSAMPLERSTATETYPE state, DWORD value) noexcept override { return real->SetSamplerState(sampler_idx,state,value); }
    HRESULT STDMETHODCALLTYPE ValidateDevice(DWORD *pass_count) noexcept override { return real->ValidateDevice(pass_count); }
    HRESULT STDMETHODCALLTYPE SetPaletteEntries(UINT palette_idx, const PALETTEENTRY *entries) noexcept override { return real->SetPaletteEntries(palette_idx,entries); }
    HRESULT STDMETHODCALLTYPE GetPaletteEntries(UINT palette_idx, PALETTEENTRY *entries) noexcept override { return real->GetPaletteEntries(palette_idx,entries); }
    HRESULT STDMETHODCALLTYPE SetCurrentTexturePalette(UINT palette_idx) noexcept override { return real->SetCurrentTexturePalette(palette_idx); }
    HRESULT STDMETHODCALLTYPE GetCurrentTexturePalette(UINT *palette_idx) noexcept override { return real->GetCurrentTexturePalette(palette_idx); }
    HRESULT STDMETHODCALLTYPE SetScissorRect(const RECT *rect) noexcept override { return real->SetScissorRect(rect); }
    HRESULT STDMETHODCALLTYPE GetScissorRect(RECT *rect) noexcept override { return real->GetScissorRect(rect); }
    HRESULT STDMETHODCALLTYPE SetSoftwareVertexProcessing(WINBOOL software) noexcept override { return real->SetSoftwareVertexProcessing(software); }
    WINBOOL STDMETHODCALLTYPE GetSoftwareVertexProcessing() noexcept override { return real->GetSoftwareVertexProcessing(); }
    HRESULT STDMETHODCALLTYPE SetNPatchMode(float segment_count) noexcept override { return real->SetNPatchMode(segment_count); }
    float STDMETHODCALLTYPE GetNPatchMode() noexcept override { return real->GetNPatchMode(); }
    HRESULT STDMETHODCALLTYPE DrawPrimitive(D3DPRIMITIVETYPE primitive_type, UINT start_vertex, UINT primitive_count) noexcept override { return real->DrawPrimitive(primitive_type,start_vertex,primitive_count); }
    HRESULT STDMETHODCALLTYPE DrawIndexedPrimitive(D3DPRIMITIVETYPE primitive_type, INT base_vertex_idx, UINT min_vertex_idx, UINT vertex_count, UINT start_idx, UINT primitive_count) noexcept override { return real->DrawIndexedPrimitive(primitive_type,base_vertex_idx,min_vertex_idx,vertex_count,start_idx,primitive_count); }
    HRESULT STDMETHODCALLTYPE DrawPrimitiveUP(D3DPRIMITIVETYPE primitive_type, UINT primitive_count, const void *data, UINT stride) noexcept override { return real->DrawPrimitiveUP(primitive_type,primitive_count,data,stride); }
    HRESULT STDMETHODCALLTYPE DrawIndexedPrimitiveUP(D3DPRIMITIVETYPE primitive_type, UINT min_vertex_idx, UINT vertex_count, UINT primitive_count, const void *index_data, D3DFORMAT index_format, const void *data, UINT stride) noexcept override { return real->DrawIndexedPrimitiveUP(primitive_type,min_vertex_idx,vertex_count,primitive_count,index_data,index_format,data,stride); }
    HRESULT STDMETHODCALLTYPE ProcessVertices(UINT src_start_idx, UINT dst_idx, UINT vertex_count, IDirect3DVertexBuffer9 *dst_buffer, IDirect3DVertexDeclaration9 *declaration, DWORD flags) noexcept override { return real->ProcessVertices(src_start_idx,dst_idx,vertex_count,dst_buffer,declaration,flags); }
    HRESULT STDMETHODCALLTYPE CreateVertexDeclaration(const D3DVERTEXELEMENT9 *elements, IDirect3DVertexDeclaration9 **declaration) noexcept override { return real->CreateVertexDeclaration(elements,declaration); }
    HRESULT STDMETHODCALLTYPE SetVertexDeclaration(IDirect3DVertexDeclaration9 *declaration) noexcept override { return real->SetVertexDeclaration(declaration); }
    HRESULT STDMETHODCALLTYPE GetVertexDeclaration(IDirect3DVertexDeclaration9 **declaration) noexcept override { return real->GetVertexDeclaration(declaration); }
    HRESULT STDMETHODCALLTYPE SetFVF(DWORD fvf) noexcept override { return real->SetFVF(fvf); }
    HRESULT STDMETHODCALLTYPE GetFVF(DWORD *fvf) noexcept override { return real->GetFVF(fvf); }
    HRESULT STDMETHODCALLTYPE CreateVertexShader(const DWORD *byte_code, IDirect3DVertexShader9 **shader) noexcept override { return real->CreateVertexShader(byte_code,shader); }
    HRESULT STDMETHODCALLTYPE SetVertexShader(IDirect3DVertexShader9 *shader) noexcept override { return real->SetVertexShader(shader); }
    HRESULT STDMETHODCALLTYPE GetVertexShader(IDirect3DVertexShader9 **shader) noexcept override { return real->GetVertexShader(shader); }
    HRESULT STDMETHODCALLTYPE SetVertexShaderConstantF(UINT reg_idx, const float *data, UINT count) noexcept override { return real->SetVertexShaderConstantF(reg_idx,data,count); }
    HRESULT STDMETHODCALLTYPE GetVertexShaderConstantF(UINT reg_idx, float *data, UINT count) noexcept override { return real->GetVertexShaderConstantF(reg_idx,data,count); }
    HRESULT STDMETHODCALLTYPE SetVertexShaderConstantI(UINT reg_idx, const int *data, UINT count) noexcept override { return real->SetVertexShaderConstantI(reg_idx,data,count); }
    HRESULT STDMETHODCALLTYPE GetVertexShaderConstantI(UINT reg_idx, int *data, UINT count) noexcept override { return real->GetVertexShaderConstantI(reg_idx,data,count); }
    HRESULT STDMETHODCALLTYPE SetVertexShaderConstantB(UINT reg_idx, const WINBOOL *data, UINT count) noexcept override { return real->SetVertexShaderConstantB(reg_idx,data,count); }
    HRESULT STDMETHODCALLTYPE GetVertexShaderConstantB(UINT reg_idx, WINBOOL *data, UINT count) noexcept override { return real->GetVertexShaderConstantB(reg_idx,data,count); }
    HRESULT STDMETHODCALLTYPE SetStreamSource(UINT stream_idx, IDirect3DVertexBuffer9 *buffer, UINT offset, UINT stride) noexcept override { return real->SetStreamSource(stream_idx,buffer,offset,stride); }
    HRESULT STDMETHODCALLTYPE GetStreamSource(UINT stream_idx, IDirect3DVertexBuffer9 **buffer, UINT *offset, UINT *stride) noexcept override { return real->GetStreamSource(stream_idx,buffer,offset,stride); }
    HRESULT STDMETHODCALLTYPE SetStreamSourceFreq(UINT stream_idx, UINT frequency) noexcept override { return real->SetStreamSourceFreq(stream_idx,frequency); }
    HRESULT STDMETHODCALLTYPE GetStreamSourceFreq(UINT stream_idx, UINT *frequency) noexcept override { return real->GetStreamSourceFreq(stream_idx,frequency); }
    HRESULT STDMETHODCALLTYPE SetIndices(IDirect3DIndexBuffer9 *buffer) noexcept override { return real->SetIndices(buffer); }
    HRESULT STDMETHODCALLTYPE GetIndices(IDirect3DIndexBuffer9 **buffer) noexcept override { return real->GetIndices(buffer); }
    HRESULT STDMETHODCALLTYPE CreatePixelShader(const DWORD *byte_code, IDirect3DPixelShader9 **shader) noexcept override { return real->CreatePixelShader(byte_code,shader); }
    HRESULT STDMETHODCALLTYPE SetPixelShader(IDirect3DPixelShader9 *shader) noexcept override { return real->SetPixelShader(shader); }
    HRESULT STDMETHODCALLTYPE GetPixelShader(IDirect3DPixelShader9 **shader) noexcept override { return real->GetPixelShader(shader); }
    HRESULT STDMETHODCALLTYPE SetPixelShaderConstantF(UINT reg_idx, const float *data, UINT count) noexcept override { return real->SetPixelShaderConstantF(reg_idx,data,count); }
    HRESULT STDMETHODCALLTYPE GetPixelShaderConstantF(UINT reg_idx, float *data, UINT count) noexcept override { return real->GetPixelShaderConstantF(reg_idx,data,count); }
    HRESULT STDMETHODCALLTYPE SetPixelShaderConstantI(UINT reg_idx, const int *data, UINT count) noexcept override { return real->SetPixelShaderConstantI(reg_idx,data,count); }
    HRESULT STDMETHODCALLTYPE GetPixelShaderConstantI(UINT reg_idx, int *data, UINT count) noexcept override { return real->GetPixelShaderConstantI(reg_idx,data,count); }
    HRESULT STDMETHODCALLTYPE SetPixelShaderConstantB(UINT reg_idx, const WINBOOL *data, UINT count) noexcept override { return real->SetPixelShaderConstantB(reg_idx,data,count); }
    HRESULT STDMETHODCALLTYPE GetPixelShaderConstantB(UINT reg_idx, WINBOOL *data, UINT count) noexcept override { return real->GetPixelShaderConstantB(reg_idx,data,count); }
    HRESULT STDMETHODCALLTYPE DrawRectPatch(UINT handle, const float *segment_count, const D3DRECTPATCH_INFO *patch_info) noexcept override { return real->DrawRectPatch(handle,segment_count,patch_info); }
    HRESULT STDMETHODCALLTYPE DrawTriPatch(UINT handle, const float *segment_count, const D3DTRIPATCH_INFO *patch_info) noexcept override { return real->DrawTriPatch(handle,segment_count,patch_info); }
    HRESULT STDMETHODCALLTYPE DeletePatch(UINT handle) noexcept override { return real->DeletePatch(handle); }
    HRESULT STDMETHODCALLTYPE CreateQuery(D3DQUERYTYPE type, IDirect3DQuery9 **query) noexcept override { return real->CreateQuery(type,query); }

    // ---- Plain forwarding: IDirect3DDevice9Ex. Fails if the real device has no Ex interface. ----

    HRESULT STDMETHODCALLTYPE SetConvolutionMonoKernel(UINT width, UINT height, float *rows, float *columns) noexcept override { if(!ex)return D3DERR_INVALIDCALL; return ex->SetConvolutionMonoKernel(width,height,rows,columns); }
    HRESULT STDMETHODCALLTYPE ComposeRects(IDirect3DSurface9 *src_surface, IDirect3DSurface9 *dst_surface, IDirect3DVertexBuffer9 *src_descs, UINT rect_count, IDirect3DVertexBuffer9 *dst_descs, D3DCOMPOSERECTSOP operation, INT offset_x, INT offset_y) noexcept override { if(!ex)return D3DERR_INVALIDCALL; return ex->ComposeRects(src_surface,dst_surface,src_descs,rect_count,dst_descs,operation,offset_x,offset_y); }
    HRESULT STDMETHODCALLTYPE GetGPUThreadPriority(INT *priority) noexcept override { if(!ex)return D3DERR_INVALIDCALL; return ex->GetGPUThreadPriority(priority); }
    HRESULT STDMETHODCALLTYPE SetGPUThreadPriority(INT priority) noexcept override { if(!ex)return D3DERR_INVALIDCALL; return ex->SetGPUThreadPriority(priority); }
    HRESULT STDMETHODCALLTYPE WaitForVBlank(UINT swapchain_idx) noexcept override { if(!ex)return D3DERR_INVALIDCALL; return ex->WaitForVBlank(swapchain_idx); }
    HRESULT STDMETHODCALLTYPE CheckResourceResidency(IDirect3DResource9 **resources, UINT32 resource_count) noexcept override { if(!ex)return D3DERR_INVALIDCALL; return ex->CheckResourceResidency(resources,resource_count); }
    HRESULT STDMETHODCALLTYPE SetMaximumFrameLatency(UINT max_latency) noexcept override { if(!ex)return D3DERR_INVALIDCALL; return ex->SetMaximumFrameLatency(max_latency); }
    HRESULT STDMETHODCALLTYPE GetMaximumFrameLatency(UINT *max_latency) noexcept override { if(!ex)return D3DERR_INVALIDCALL; return ex->GetMaximumFrameLatency(max_latency); }
    HRESULT STDMETHODCALLTYPE CheckDeviceState(HWND dst_window) noexcept override { if(!ex)return D3DERR_INVALIDCALL; return ex->CheckDeviceState(dst_window); }
    HRESULT STDMETHODCALLTYPE CreateRenderTargetEx(UINT width, UINT height, D3DFORMAT format, D3DMULTISAMPLE_TYPE multisample_type, DWORD multisample_quality, WINBOOL lockable, IDirect3DSurface9 **surface, HANDLE *shared_handle, DWORD usage) noexcept override { if(!ex)return D3DERR_INVALIDCALL; return ex->CreateRenderTargetEx(width,height,format,multisample_type,multisample_quality,lockable,surface,shared_handle,usage); }
    HRESULT STDMETHODCALLTYPE CreateOffscreenPlainSurfaceEx(UINT width, UINT Height, D3DFORMAT format, D3DPOOL pool, IDirect3DSurface9 **surface, HANDLE *shared_handle, DWORD usage) noexcept override { if(!ex)return D3DERR_INVALIDCALL; return ex->CreateOffscreenPlainSurfaceEx(width,Height,format,pool,surface,shared_handle,usage); }
    HRESULT STDMETHODCALLTYPE CreateDepthStencilSurfaceEx(UINT width, UINT height, D3DFORMAT format, D3DMULTISAMPLE_TYPE multisample_type, DWORD multisample_quality, WINBOOL discard, IDirect3DSurface9 **surface, HANDLE *shared_handle, DWORD usage) noexcept override { if(!ex)return D3DERR_INVALIDCALL; return ex->CreateDepthStencilSurfaceEx(width,height,format,multisample_type,multisample_quality,discard,surface,shared_handle,usage); }
    HRESULT STDMETHODCALLTYPE GetDisplayModeEx(UINT swapchain_idx, D3DDISPLAYMODEEX *mode, D3DDISPLAYROTATION *rotation) noexcept override { if(!ex)return D3DERR_INVALIDCALL; return ex->GetDisplayModeEx(swapchain_idx,mode,rotation); }
};

// ---------------------------------------------------------------------------
// D3DProxy: wrapper around the Direct3D 9 object. The heart of this DLL.
//
// The game asks how many display modes an adapter has and then fetches them
// by index. We answer with the adapter's own list followed by our modes:
//
//   index 0 .. native-1              the adapter's real modes
//   index native .. native+added-1   modes from the configuration
// ---------------------------------------------------------------------------
class D3DProxy final : public IDirect3D9Ex {
    IDirect3D9* real;            // the real Direct3D object; we hold one reference
    IDirect3D9Ex* ex = nullptr;  // the same object as Ex interface, null if it has none
    LONG refs = 1;               // COM reference count of this wrapper
    unsigned adapterCount = 0;   // adapters that get added modes, at most maxAdapters

    // Per adapter: the configured modes it is offered, as indices into
    // configured.modes. counts[a] says how many entries of selected[a] are used.
    unsigned selected[rrpolicy::maxAdapters][rrpolicy::maxModes]{};
    unsigned counts[rrpolicy::maxAdapters]{};

    // Number of added modes for adapter `a`, given how many modes of its own
    // it has in the requested format. Zero if it has none, so that formats
    // the adapter does not support stay unsupported.
    unsigned added(UINT a, UINT native) const {
        return a < adapterCount && native > 0 ? counts[a] : 0;
    }

    // The same for the Ex enumeration. Our modes are progressive, so a filter
    // that asks for interlaced modes only gets none of them.
    unsigned addedEx(UINT a, const D3DDISPLAYMODEFILTER* f, UINT native) const {
        if (!f) return 0;
        const bool progressive = f->ScanLineOrdering == D3DSCANLINEORDERING_UNKNOWN ||
                                 f->ScanLineOrdering == D3DSCANLINEORDERING_PROGRESSIVE;
        return progressive ? added(a, native) : 0;
    }

    // The i-th added mode of adapter a.
    const rrpolicy::Mode& mode(UINT a, UINT i) const { return configured.modes[selected[a][i]]; }

    // Shared check for CreateDevice and CreateDeviceEx: windowed only, and no
    // adapter-group device (one device driving several fullscreen outputs).
    bool mayCreate(D3DPRESENT_PARAMETERS* p, DWORD flags, const char* where) {
        if (!allowed(p, where)) return false;
        if (flags & D3DCREATE_ADAPTERGROUP_DEVICE) {
            note("BLOCKED: adapter-group device");
            return false;
        }
        return true;
    }

    // Wraps a real device that was just created. If there is no memory for
    // the wrapper, the real device is released and null is returned.
    DeviceProxy* wrapDevice(IDirect3DDevice9* dev) {
        DeviceProxy* wrap = new (std::nothrow) DeviceProxy(dev, this);
        if (!wrap) dev->Release();
        return wrap;
    }

public:
    // Takes over the caller's reference to `r`. Decides once, per adapter,
    // which configured modes fit: the adapter's maximum texture size is the
    // upper bound. If that cannot be read, the limit is 0, which means
    // "unknown" and lets every mode through.
    D3DProxy(IDirect3D9* r) : real(r) {
        real->QueryInterface(IID_IDirect3D9Ex, (void**)&ex);
        const unsigned total = real->GetAdapterCount();
        adapterCount = total < rrpolicy::maxAdapters ? total : rrpolicy::maxAdapters;
        for (unsigned a = 0; a < adapterCount; ++a) {
            D3DCAPS9 caps = {};
            const HRESULT hr = real->GetDeviceCaps(a, D3DDEVTYPE_HAL, &caps);
            const unsigned maxW = SUCCEEDED(hr) ? caps.MaxTextureWidth : 0;
            const unsigned maxH = SUCCEEDED(hr) ? caps.MaxTextureHeight : 0;
            for (unsigned i = 0; i < configured.count; ++i) {
                const auto& m = configured.modes[i];
                if (rrpolicy::withinCaps(m, maxW, maxH)) selected[a][counts[a]++] = i;
                else note("CAPS skipped adapter=%u mode=%ux%u@%u limit=%ux%u", a, m.width, m.height, m.refresh, maxW, maxH);
            }
            note("CAPS adapter=%u maxTexture=%ux%u result=0x%08lx offered=%u", a, maxW, maxH, (unsigned long)hr, counts[a]);
        }
        if (total > adapterCount) note("CAPS adapters above %u keep native modes only", adapterCount);
        note("D3DProxy created; Ex=%u; version=" RRT_VERSION "; windowed only", ex != nullptr);
    }
    ~D3DProxy() {
        if (ex) ex->Release();
        real->Release();
    }

    // ---- IUnknown ----

    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID id, void** out) noexcept override {
        if (!out) return E_POINTER;
        *out = nullptr;
        if (IsEqualGUID(id, IID_IUnknown) || IsEqualGUID(id, IID_IDirect3D9) || (ex && IsEqualGUID(id, IID_IDirect3D9Ex))) {
            *out = static_cast<IDirect3D9Ex*>(this);
            AddRef();
            return S_OK;
        }
        return E_NOINTERFACE;
    }
    ULONG STDMETHODCALLTYPE AddRef() noexcept override { return (ULONG)InterlockedIncrement(&refs); }
    ULONG STDMETHODCALLTYPE Release() noexcept override {
        ULONG n = (ULONG)InterlockedDecrement(&refs);
        if (!n) delete this;
        return n;
    }

    // ---- Mode enumeration: the adapter's modes plus ours ----

    UINT STDMETHODCALLTYPE GetAdapterModeCount(UINT a, D3DFORMAT f) noexcept override {
        const UINT native = real->GetAdapterModeCount(a, f);
        const UINT extra = added(a, native);
        note("GetAdapterModeCount adapter=%u format=%u native=%u added=%u", a, f, native, extra);
        return native + extra;
    }

    // Indices past the adapter's own modes are answered from the configuration.
    HRESULT STDMETHODCALLTYPE EnumAdapterModes(UINT a, D3DFORMAT f, UINT i, D3DDISPLAYMODE* out) noexcept override {
        const UINT native = real->GetAdapterModeCount(a, f);
        if (!rrpolicy::extraIndex(native, i, added(a, native))) return real->EnumAdapterModes(a, f, i, out);
        if (!out) return D3DERR_INVALIDCALL;
        const auto& m = mode(a, i - native);
        out->Width = m.width;
        out->Height = m.height;
        out->RefreshRate = m.refresh;
        out->Format = f;
        note("EnumAdapterModes: offered %ux%u@%u adapter=%u", out->Width, out->Height, out->RefreshRate, a);
        return S_OK;
    }

    UINT STDMETHODCALLTYPE GetAdapterModeCountEx(UINT a, const D3DDISPLAYMODEFILTER* f) noexcept override {
        if (!ex) return 0;
        const UINT native = ex->GetAdapterModeCountEx(a, f);
        const UINT extra = addedEx(a, f, native);
        note("GetAdapterModeCountEx adapter=%u native=%u added=%u", a, native, extra);
        return native + extra;
    }

    HRESULT STDMETHODCALLTYPE EnumAdapterModesEx(UINT a, const D3DDISPLAYMODEFILTER* f, UINT i, D3DDISPLAYMODEEX* out) noexcept override {
        if (!ex) return D3DERR_INVALIDCALL;
        const UINT native = ex->GetAdapterModeCountEx(a, f);
        if (!rrpolicy::extraIndex(native, i, addedEx(a, f, native))) return ex->EnumAdapterModesEx(a, f, i, out);
        if (!out || out->Size != sizeof(*out)) return D3DERR_INVALIDCALL;
        const auto& m = mode(a, i - native);
        out->Width = m.width;
        out->Height = m.height;
        out->RefreshRate = m.refresh;
        out->Format = f->Format;
        out->ScanLineOrdering = D3DSCANLINEORDERING_PROGRESSIVE;
        note("EnumAdapterModesEx: offered %ux%u@%u adapter=%u", out->Width, out->Height, out->RefreshRate, a);
        return S_OK;
    }

    // ---- Device creation: windowed only, result wrapped in a DeviceProxy ----

    HRESULT STDMETHODCALLTYPE CreateDevice(UINT a, D3DDEVTYPE t, HWND w, DWORD flags, D3DPRESENT_PARAMETERS* p, IDirect3DDevice9** out) noexcept override {
        if (!out) return E_POINTER;
        *out = nullptr;
        if (!mayCreate(p, flags, "CreateDevice")) return D3DERR_INVALIDCALL;
        IDirect3DDevice9* dev = nullptr;
        HRESULT hr = real->CreateDevice(a, t, w, flags, p, &dev);
        note("CreateDevice result=0x%08lx", (unsigned long)hr);
        if (SUCCEEDED(hr) && dev) {
            *out = wrapDevice(dev);
            if (!*out) return E_OUTOFMEMORY;
        }
        return hr;
    }

    // The parameter before `out` describes a fullscreen display mode.
    // Fullscreen has been refused by mayCreate(), so it is ignored and NULL
    // is passed on, as Direct3D expects for windowed mode.
    HRESULT STDMETHODCALLTYPE CreateDeviceEx(UINT a, D3DDEVTYPE t, HWND w, DWORD flags, D3DPRESENT_PARAMETERS* p, D3DDISPLAYMODEEX* /*fullscreenMode*/, IDirect3DDevice9Ex** out) noexcept override {
        if (!out) return E_POINTER;
        *out = nullptr;
        if (!ex || !mayCreate(p, flags, "CreateDeviceEx")) return D3DERR_INVALIDCALL;
        IDirect3DDevice9Ex* dev = nullptr;
        HRESULT hr = ex->CreateDeviceEx(a, t, w, flags, p, nullptr, &dev);
        note("CreateDeviceEx result=0x%08lx", (unsigned long)hr);
        if (SUCCEEDED(hr) && dev) {
            *out = wrapDevice(dev);
            if (!*out) return E_OUTOFMEMORY;
        }
        return hr;
    }

    // ---- Plain forwarding: IDirect3D9 ----

    HRESULT STDMETHODCALLTYPE RegisterSoftwareDevice(void *init) noexcept override { return real->RegisterSoftwareDevice(init); }
    UINT STDMETHODCALLTYPE GetAdapterCount() noexcept override { return real->GetAdapterCount(); }
    HRESULT STDMETHODCALLTYPE GetAdapterIdentifier(UINT adapter_idx, DWORD flags, D3DADAPTER_IDENTIFIER9 *identifier) noexcept override { return real->GetAdapterIdentifier(adapter_idx,flags,identifier); }
    HRESULT STDMETHODCALLTYPE GetAdapterDisplayMode(UINT adapter_idx, D3DDISPLAYMODE *mode) noexcept override { return real->GetAdapterDisplayMode(adapter_idx,mode); }
    HRESULT STDMETHODCALLTYPE CheckDeviceType(UINT adapter_idx, D3DDEVTYPE device_type, D3DFORMAT display_format, D3DFORMAT backbuffer_format, WINBOOL windowed) noexcept override { return real->CheckDeviceType(adapter_idx,device_type,display_format,backbuffer_format,windowed); }
    HRESULT STDMETHODCALLTYPE CheckDeviceFormat(UINT adapter_idx, D3DDEVTYPE device_type, D3DFORMAT adapter_format, DWORD usage, D3DRESOURCETYPE resource_type, D3DFORMAT format) noexcept override { return real->CheckDeviceFormat(adapter_idx,device_type,adapter_format,usage,resource_type,format); }
    HRESULT STDMETHODCALLTYPE CheckDeviceMultiSampleType(UINT adapter_idx, D3DDEVTYPE device_type, D3DFORMAT surface_format, WINBOOL windowed, D3DMULTISAMPLE_TYPE multisample_type, DWORD *quality_levels) noexcept override { return real->CheckDeviceMultiSampleType(adapter_idx,device_type,surface_format,windowed,multisample_type,quality_levels); }
    HRESULT STDMETHODCALLTYPE CheckDepthStencilMatch(UINT adapter_idx, D3DDEVTYPE device_type, D3DFORMAT adapter_format, D3DFORMAT rt_format, D3DFORMAT ds_format) noexcept override { return real->CheckDepthStencilMatch(adapter_idx,device_type,adapter_format,rt_format,ds_format); }
    HRESULT STDMETHODCALLTYPE CheckDeviceFormatConversion(UINT adapter_idx, D3DDEVTYPE device_type, D3DFORMAT src_format, D3DFORMAT dst_format) noexcept override { return real->CheckDeviceFormatConversion(adapter_idx,device_type,src_format,dst_format); }
    HRESULT STDMETHODCALLTYPE GetDeviceCaps(UINT adapter_idx, D3DDEVTYPE device_type, D3DCAPS9 *caps) noexcept override { return real->GetDeviceCaps(adapter_idx,device_type,caps); }
    HMONITOR STDMETHODCALLTYPE GetAdapterMonitor(UINT adapter_idx) noexcept override { return real->GetAdapterMonitor(adapter_idx); }

    // ---- Plain forwarding: IDirect3D9Ex. Fails if the real object has no Ex interface. ----

    HRESULT STDMETHODCALLTYPE GetAdapterDisplayModeEx(UINT adapter_idx, D3DDISPLAYMODEEX *mode, D3DDISPLAYROTATION *rotation) noexcept override { if(!ex)return D3DERR_INVALIDCALL; return ex->GetAdapterDisplayModeEx(adapter_idx,mode,rotation); }
    HRESULT STDMETHODCALLTYPE GetAdapterLUID(UINT adapter_idx, LUID *luid) noexcept override { if(!ex)return D3DERR_INVALIDCALL; return ex->GetAdapterLUID(adapter_idx,luid); }
};

// ---------------------------------------------------------------------------
// Exported functions. exports.def lists them for the linker. Each one calls
// the function of the same name in the real d3d9.dll.
// ---------------------------------------------------------------------------
extern "C" {

// The two entry points that matter: create the real Direct3D object and hand
// back our wrapper around it.
IDirect3D9* WINAPI Direct3DCreate9(UINT sdk) {
    const auto create = api().Direct3DCreate9;
    if (!create) return nullptr;
    note("Direct3DCreate9 called, SDK=%u", sdk);
    IDirect3D9* real = create(sdk);
    if (!real) return nullptr;
    auto wrap = new (std::nothrow) D3DProxy(real);
    if (!wrap) real->Release();
    return wrap;
}

HRESULT WINAPI Direct3DCreate9Ex(UINT sdk, IDirect3D9Ex** out) {
    if (!out) return E_POINTER;
    *out = nullptr;
    const auto create = api().Direct3DCreate9Ex;
    if (!create) return D3DERR_NOTAVAILABLE;
    note("Direct3DCreate9Ex called, SDK=%u", sdk);
    IDirect3D9Ex* real = nullptr;
    HRESULT hr = create(sdk, &real);
    if (FAILED(hr)) return hr;
    auto wrap = new (std::nothrow) D3DProxy(real);
    if (!wrap) {
        real->Release();
        return E_OUTOFMEMORY;
    }
    *out = wrap;
    return hr;
}

// Debugging and profiling helpers that programs expect to find in d3d9.dll.
// Passed through unchanged; if the real DLL lacks one, the call does nothing.
int WINAPI D3DPERF_BeginEvent(D3DCOLOR color, const WCHAR* name) {
    const auto fn = api().D3DPERF_BeginEvent;
    return fn ? fn(color, name) : 0;
}
int WINAPI D3DPERF_EndEvent() {
    const auto fn = api().D3DPERF_EndEvent;
    return fn ? fn() : 0;
}
DWORD WINAPI D3DPERF_GetStatus() {
    const auto fn = api().D3DPERF_GetStatus;
    return fn ? fn() : 0;
}
BOOL WINAPI D3DPERF_QueryRepeatFrame() {
    const auto fn = api().D3DPERF_QueryRepeatFrame;
    return fn ? fn() : FALSE;
}
void WINAPI D3DPERF_SetMarker(D3DCOLOR color, const WCHAR* name) {
    const auto fn = api().D3DPERF_SetMarker;
    if (fn) fn(color, name);
}
void WINAPI D3DPERF_SetOptions(DWORD options) {
    const auto fn = api().D3DPERF_SetOptions;
    if (fn) fn(options);
}
void WINAPI D3DPERF_SetRegion(D3DCOLOR color, const WCHAR* name) {
    const auto fn = api().D3DPERF_SetRegion;
    if (fn) fn(color, name);
}
void WINAPI DebugSetMute() {
    const auto fn = api().DebugSetMute;
    if (fn) fn();
}
void WINAPI Direct3D9EnableMaximizedWindowedModeShim(BOOL enable) {
    const auto fn = api().Direct3D9EnableMaximizedWindowedModeShim;
    if (fn) fn(enable);
}
void* WINAPI Direct3DShaderValidatorCreate9() {
    const auto fn = api().Direct3DShaderValidatorCreate9;
    return fn ? fn() : nullptr;
}

}  // extern "C"
