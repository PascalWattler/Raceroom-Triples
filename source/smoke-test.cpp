// Smoke test for a built d3d9.dll, without RaceRoom.
//
// It does what the game does: load the DLL, create Direct3D, read the list of
// display modes, create a windowed device and resize it to a triple-screen
// size. The window stays hidden. Success means the added mode is in the list
// and Direct3D really created a back buffer of that size.
//
// Usage: smoke-test <path to d3d9.dll> [width height]
//        Width and height default to 5760 x 1080.
//
// The exit code is 0 on success and 1 otherwise.
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <d3d9.h>
#include <cstdio>
#include <cwchar>

// Prints the reason and ends the program with exit code 1.
static int fail(const char* what, HRESULT hr = S_OK) {
    std::printf("FAILED: %s (0x%08lx)\n", what, (unsigned long)hr);
    return 1;
}

int wmain(int argc, wchar_t** argv) {
    if (argc != 2 && argc != 4) {
        std::puts("Usage: smoke-test <path to d3d9.dll> [width height]");
        return 1;
    }
    const UINT width = argc == 4 ? std::wcstoul(argv[2], nullptr, 10) : 5760;
    const UINT height = argc == 4 ? std::wcstoul(argv[3], nullptr, 10) : 1080;

    // Load the DLL under test and find its entry point.
    HMODULE dll = LoadLibraryW(argv[1]);
    if (!dll) return fail("LoadLibrary", HRESULT_FROM_WIN32(GetLastError()));
    typedef HRESULT (WINAPI* CreateEx)(UINT, IDirect3D9Ex**);
    const auto create = reinterpret_cast<CreateEx>(reinterpret_cast<void*>(GetProcAddress(dll, "Direct3DCreate9Ex")));
    if (!create) return fail("Direct3DCreate9Ex not exported");

    IDirect3D9Ex* d3d = nullptr;
    HRESULT hr = create(D3D_SDK_VERSION, &d3d);
    if (FAILED(hr) || !d3d) return fail("Direct3DCreate9Ex", hr);

    // Read the mode list of the first adapter and look for the wanted size.
    const D3DFORMAT format = D3DFMT_X8R8G8B8;
    const UINT modes = d3d->GetAdapterModeCount(D3DADAPTER_DEFAULT, format);
    UINT wide = 0;
    bool offered = false;
    for (UINT i = 0; i < modes; ++i) {
        D3DDISPLAYMODE m = {};
        if (FAILED(d3d->EnumAdapterModes(D3DADAPTER_DEFAULT, format, i, &m))) return fail("EnumAdapterModes");
        if (m.Width >= 5000) {
            ++wide;
            std::printf("  mode %u: %ux%u@%u\n", i, m.Width, m.Height, m.RefreshRate);
        }
        if (m.Width == width && m.Height == height) offered = true;
    }
    std::printf("Modes: %u in total, %u of them at least 5000 wide\n", modes, wide);
    if (!offered) return fail("the requested size is not in the mode list");

    // An index past the end must be refused, as the real Direct3D does.
    D3DDISPLAYMODE beyond = {};
    if (SUCCEEDED(d3d->EnumAdapterModes(D3DADAPTER_DEFAULT, format, modes, &beyond))) return fail("index past the end was accepted");

    // Create a small windowed device, then resize it, as the game does when a
    // resolution is picked in its menu.
    HWND window = CreateWindowExW(0, L"STATIC", L"smoke-test", WS_POPUP, 0, 0, 64, 64, nullptr, nullptr, nullptr, nullptr);
    if (!window) return fail("CreateWindow", HRESULT_FROM_WIN32(GetLastError()));
    D3DPRESENT_PARAMETERS pp = {};
    pp.Windowed = TRUE;
    pp.SwapEffect = D3DSWAPEFFECT_DISCARD;
    pp.BackBufferFormat = format;
    pp.BackBufferWidth = 1024;
    pp.BackBufferHeight = 768;
    pp.hDeviceWindow = window;
    IDirect3DDevice9Ex* device = nullptr;
    hr = d3d->CreateDeviceEx(D3DADAPTER_DEFAULT, D3DDEVTYPE_HAL, window, D3DCREATE_HARDWARE_VERTEXPROCESSING, &pp, nullptr, &device);
    if (FAILED(hr) || !device) return fail("CreateDeviceEx", hr);

    pp.BackBufferWidth = width;
    pp.BackBufferHeight = height;
    hr = device->ResetEx(&pp, nullptr);
    if (FAILED(hr)) return fail("ResetEx to the triple-screen size", hr);

    IDirect3DSurface9* surface = nullptr;
    D3DSURFACE_DESC desc = {};
    hr = device->GetBackBuffer(0, 0, D3DBACKBUFFER_TYPE_MONO, &surface);
    if (FAILED(hr) || FAILED(surface->GetDesc(&desc))) return fail("GetBackBuffer", hr);
    surface->Release();
    std::printf("Back buffer: %ux%u\n", desc.Width, desc.Height);
    if (desc.Width != width || desc.Height != height) return fail("back buffer has the wrong size");

    // Exclusive fullscreen must be refused by the wrapper.
    pp.Windowed = FALSE;
    if (SUCCEEDED(device->ResetEx(&pp, nullptr))) return fail("exclusive fullscreen was accepted");

    // A frame must go through.
    pp.Windowed = TRUE;
    hr = device->ResetEx(&pp, nullptr);
    if (FAILED(hr)) return fail("ResetEx back to windowed", hr);
    device->Clear(0, nullptr, D3DCLEAR_TARGET, 0, 1.0f, 0);
    hr = device->PresentEx(nullptr, nullptr, nullptr, nullptr, 0);
    if (FAILED(hr)) return fail("PresentEx", hr);

    device->Release();
    d3d->Release();
    DestroyWindow(window);
    std::puts("Smoke test passed: mode offered, back buffer created, fullscreen refused, frame presented.");
    return 0;
}
