#include <string>
#include <windows.h>
#include <commdlg.h>
#include <d3d11.h>
#include <tchar.h>
#include <algorithm>
#include <fstream>
#include <vector>
#include <cstdint>
#include <xaudio2.h>

#pragma comment(lib, "xaudio2.lib")
#pragma comment(lib, "winmm.lib")

#include "imgui.h"
#include "imgui_impl_win32.h"
#include "imgui_impl_dx11.h"
#include "Bus.h"
#include "olc6502.h"
#include "Cartridge.h"

// Constantes de áudio
#define SAMPLE_RATE     44100
#define BUFFER_SIZE     1470     // 2 frames de áudio por buffer
#define BUFFER_COUNT    4        // fila maior = menos starvation
#define RING_SIZE 8192  // potência de 2, maior que 1 frame

// Data
static ID3D11Device* g_pd3dDevice = nullptr;
static ID3D11DeviceContext* g_pd3dDeviceContext = nullptr;
static IDXGISwapChain* g_pSwapChain = nullptr;
static bool g_SwapChainOccluded = false;
static UINT g_ResizeWidth = 0, g_ResizeHeight = 0;
static ID3D11RenderTargetView* g_mainRenderTargetView = nullptr;

// Variáveis globais de áudio
IXAudio2* g_pXAudio2 = nullptr;
IXAudio2MasteringVoice* g_pMasterVoice = nullptr;
IXAudio2SourceVoice* g_pSourceVoice = nullptr;

int16_t g_audioBuffers[BUFFER_COUNT][BUFFER_SIZE];
int     g_currentBuffer = 0;
int     g_bufferPos = 0;

extern IMGUI_IMPL_API LRESULT ImGui_ImplWin32_WndProcHandler(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam);

std::atomic<int> g_ringWrite{ 0 };
std::atomic<int> g_ringRead{ 0 };
int16_t g_ringBuffer[RING_SIZE];

void RingPush(int16_t sample)
{
    int w = g_ringWrite.load(std::memory_order_relaxed);
    int next = (w + 1) & (RING_SIZE - 1);
    if (next != g_ringRead.load(std::memory_order_acquire))
    {
        g_ringBuffer[w] = sample;
        g_ringWrite.store(next, std::memory_order_release);
    }
    // Se cheio, descarta — melhor que bloquear
}

bool RingPop(int16_t& sample)
{
    int r = g_ringRead.load(std::memory_order_relaxed);
    if (r == g_ringWrite.load(std::memory_order_acquire))
        return false; // vazio
    sample = g_ringBuffer[r];
    g_ringRead.store((r + 1) & (RING_SIZE - 1), std::memory_order_release);
    return true;
}

LRESULT WINAPI WndProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
    if (ImGui_ImplWin32_WndProcHandler(hWnd, msg, wParam, lParam))
        return true;

    switch (msg)
    {
    case WM_SIZE:
        if (wParam == SIZE_MINIMIZED)
            return 0;
        g_ResizeWidth = (UINT)LOWORD(lParam); // Queue resize
        g_ResizeHeight = (UINT)HIWORD(lParam);
        return 0;
    case WM_SYSCOMMAND:
        if ((wParam & 0xfff0) == SC_KEYMENU) // Disable ALT application menu
            return 0;
        break;
    case WM_DESTROY:
        ::PostQuitMessage(0);
        return 0;
    }
    return ::DefWindowProcW(hWnd, msg, wParam, lParam);
}

void CreateRenderTarget()
{
    ID3D11Texture2D* pBackBuffer;
    g_pSwapChain->GetBuffer(0, IID_PPV_ARGS(&pBackBuffer));
    g_pd3dDevice->CreateRenderTargetView(pBackBuffer, nullptr, &g_mainRenderTargetView);
    pBackBuffer->Release();
}

void CleanupRenderTarget()
{
    if (g_mainRenderTargetView) { g_mainRenderTargetView->Release(); g_mainRenderTargetView = nullptr; }
}

// Helper functions

bool CreateDeviceD3D(HWND hWnd)
{
    // Setup swap chain
    DXGI_SWAP_CHAIN_DESC sd;
    ZeroMemory(&sd, sizeof(sd));
    sd.BufferCount = 2;
    sd.BufferDesc.Width = 0;
    sd.BufferDesc.Height = 0;
    sd.BufferDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    sd.BufferDesc.RefreshRate.Numerator = 60;
    sd.BufferDesc.RefreshRate.Denominator = 1;
    sd.Flags = DXGI_SWAP_CHAIN_FLAG_ALLOW_MODE_SWITCH;
    sd.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
    sd.OutputWindow = hWnd;
    sd.SampleDesc.Count = 1;
    sd.SampleDesc.Quality = 0;
    sd.Windowed = TRUE;
    sd.SwapEffect = DXGI_SWAP_EFFECT_DISCARD;

    UINT createDeviceFlags = 0;
    //createDeviceFlags |= D3D11_CREATE_DEVICE_DEBUG;
    D3D_FEATURE_LEVEL featureLevel;
    const D3D_FEATURE_LEVEL featureLevelArray[2] = { D3D_FEATURE_LEVEL_11_0, D3D_FEATURE_LEVEL_10_0, };
    HRESULT res = D3D11CreateDeviceAndSwapChain(nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr, createDeviceFlags, featureLevelArray, 2, D3D11_SDK_VERSION, &sd, &g_pSwapChain, &g_pd3dDevice, &featureLevel, &g_pd3dDeviceContext);
    if (res == DXGI_ERROR_UNSUPPORTED) // Try high-performance WARP software driver if hardware is not available.
        res = D3D11CreateDeviceAndSwapChain(nullptr, D3D_DRIVER_TYPE_WARP, nullptr, createDeviceFlags, featureLevelArray, 2, D3D11_SDK_VERSION, &sd, &g_pSwapChain, &g_pd3dDevice, &featureLevel, &g_pd3dDeviceContext);
    if (res != S_OK)
        return false;

    CreateRenderTarget();
    return true;
}

void CleanupDeviceD3D()
{
    CleanupRenderTarget();
    if (g_pSwapChain) { g_pSwapChain->Release(); g_pSwapChain = nullptr; }
    if (g_pd3dDeviceContext) { g_pd3dDeviceContext->Release(); g_pd3dDeviceContext = nullptr; }
    if (g_pd3dDevice) { g_pd3dDevice->Release(); g_pd3dDevice = nullptr; }
}
std::string OpenFileDialog()
{
    char filename[MAX_PATH] = "";

    OPENFILENAMEA ofn = {};
    ofn.lStructSize = sizeof(ofn);
    ofn.lpstrFile = filename;
    ofn.nMaxFile = MAX_PATH;
    ofn.lpstrFilter = "NES ROM (*.nes)\0*.nes\0All Files\0*.*\0";
    ofn.Flags = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST;

    if (GetOpenFileNameA(&ofn))
        return filename;

    return "";
}

ID3D11Texture2D* g_pScreenTexture = nullptr;
ID3D11ShaderResourceView* g_pScreenSRV = nullptr;

void CreateScreenTexture()
{
    D3D11_TEXTURE2D_DESC desc = {};
    desc.Width = 256;
    desc.Height = 240;
    desc.MipLevels = 1;
    desc.ArraySize = 1;
    desc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    desc.SampleDesc.Count = 1;
    desc.Usage = D3D11_USAGE_DYNAMIC;
    desc.BindFlags = D3D11_BIND_SHADER_RESOURCE;
    desc.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;

    g_pd3dDevice->CreateTexture2D(&desc, nullptr, &g_pScreenTexture);

    D3D11_SHADER_RESOURCE_VIEW_DESC srvDesc = {};
    srvDesc.Format = desc.Format;
    srvDesc.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D;
    srvDesc.Texture2D.MipLevels = 1;

    g_pd3dDevice->CreateShaderResourceView(
        g_pScreenTexture,
        &srvDesc,
        &g_pScreenSRV
    );
}

void UpdateTexture(const uint32_t* framebuffer)
{
    D3D11_MAPPED_SUBRESOURCE mapped;

    HRESULT hr = g_pd3dDeviceContext->Map(
        g_pScreenTexture,
        0,
        D3D11_MAP_WRITE_DISCARD,
        0,
        &mapped
    );

    if (SUCCEEDED(hr))
    {
        memcpy(
            mapped.pData,
            framebuffer,
            256 * 240 * sizeof(uint32_t)
        );

        g_pd3dDeviceContext->Unmap(
            g_pScreenTexture,
            0
        );
    }
}

class AudioCallback : public IXAudio2VoiceCallback
{
public:
    IXAudio2SourceVoice* pVoice = nullptr;

    // Chamado pela thread de áudio do XAudio2 quando precisa de mais dados
    void STDMETHODCALLTYPE OnBufferEnd(void*) override
    {
        // Preenche buffer com samples do ring buffer
        for (int i = 0; i < BUFFER_SIZE; i++)
        {
            int16_t s = 0;
            RingPop(s); // Se vazio retorna 0 (silêncio)
            g_audioBuffers[g_currentBuffer][i] = s;
        }

        XAUDIO2_BUFFER buf = {};
        buf.AudioBytes = BUFFER_SIZE * sizeof(int16_t);
        buf.pAudioData = (BYTE*)g_audioBuffers[g_currentBuffer];
        pVoice->SubmitSourceBuffer(&buf);

        g_currentBuffer = (g_currentBuffer + 1) % BUFFER_COUNT;
    }

    // Métodos obrigatórios da interface — podem ficar vazios
    void STDMETHODCALLTYPE OnStreamEnd() override {}
    void STDMETHODCALLTYPE OnVoiceProcessingPassEnd() override {}
    void STDMETHODCALLTYPE OnVoiceProcessingPassStart(UINT32) override {}
    void STDMETHODCALLTYPE OnBufferStart(void*) override {}
    void STDMETHODCALLTYPE OnLoopEnd(void*) override {}
    void STDMETHODCALLTYPE OnVoiceError(void*, HRESULT) override {}
};

AudioCallback g_audioCallback;

// Main code
int main(int, char**)
{

    timeBeginPeriod(1);
    // Make process DPI aware and obtain main monitor scale
    ImGui_ImplWin32_EnableDpiAwareness();
    float main_scale = ImGui_ImplWin32_GetDpiScaleForMonitor(::MonitorFromPoint(POINT{ 0, 0 }, MONITOR_DEFAULTTOPRIMARY));

    // Create application window
    WNDCLASSEXW wc = { sizeof(wc), CS_CLASSDC, WndProc, 0L, 0L, GetModuleHandle(nullptr), nullptr, nullptr, nullptr, nullptr, L"ImGui Example", nullptr };
    ::RegisterClassExW(&wc);
    HWND hwnd = ::CreateWindowW(wc.lpszClassName, L"NESEmulator", WS_OVERLAPPEDWINDOW, 100, 100, (int)(1280 * main_scale), (int)(800 * main_scale), nullptr, nullptr, wc.hInstance, nullptr);

    // Initialize Direct3D
    if (!CreateDeviceD3D(hwnd))
    {
        CleanupDeviceD3D();
        ::UnregisterClassW(wc.lpszClassName, wc.hInstance);
        return 1;
    }

    CreateScreenTexture();

    // Show the window
    ::ShowWindow(hwnd, SW_SHOWDEFAULT);
    ::UpdateWindow(hwnd);

    // Setup Dear ImGui context
    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO(); (void)io;
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;     // Enable Keyboard Controls
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableGamepad;      // Enable Gamepad Controls

    // Setup Dear ImGui style
    ImGui::StyleColorsDark();

    // Setup scaling
    ImGuiStyle& style = ImGui::GetStyle();
    style.ScaleAllSizes(main_scale);        // Bake a fixed style scale. (until we have a solution for dynamic style scaling, changing this requires resetting Style + calling this again)
    style.FontScaleDpi = main_scale;        // Set initial font scale. (in docking branch: using io.ConfigDpiScaleFonts=true automatically overrides this for every window depending on the current monitor)

    // Setup Platform/Renderer backends
    ImGui_ImplWin32_Init(hwnd);
    ImGui_ImplDX11_Init(g_pd3dDevice, g_pd3dDeviceContext);

    CoInitializeEx(nullptr, COINIT_MULTITHREADED);
    XAudio2Create(&g_pXAudio2, 0, XAUDIO2_DEFAULT_PROCESSOR);
    g_pXAudio2->CreateMasteringVoice(&g_pMasterVoice);

    WAVEFORMATEX wfx = {};
    wfx.wFormatTag = WAVE_FORMAT_PCM;
    wfx.nChannels = 1;
    wfx.nSamplesPerSec = SAMPLE_RATE;
    wfx.wBitsPerSample = 16;
    wfx.nBlockAlign = wfx.nChannels * wfx.wBitsPerSample / 8;
    wfx.nAvgBytesPerSec = wfx.nSamplesPerSec * wfx.nBlockAlign;

    g_pXAudio2->CreateSourceVoice(&g_pSourceVoice, &wfx, 0,
        XAUDIO2_DEFAULT_FREQ_RATIO, &g_audioCallback);
    g_audioCallback.pVoice = g_pSourceVoice;

    // Pré-enfileira 2 buffers vazios para o XAudio2 começar
    memset(g_audioBuffers, 0, sizeof(g_audioBuffers));
    for (int i = 0; i < 2; i++)
    {
        XAUDIO2_BUFFER buf = {};
        buf.AudioBytes = BUFFER_SIZE * sizeof(int16_t);
        buf.pAudioData = (BYTE*)g_audioBuffers[i];
        g_pSourceVoice->SubmitSourceBuffer(&buf);
    }
    g_currentBuffer = 0;
    g_pSourceVoice->Start(0);

    ImVec4 clear_color = ImVec4(0.12f, 0.12f, 0.12f, 1.00f);

    // Emulator setup
    Bus nes;
    std::shared_ptr<Cartridge> cart;

    double cpuCyclesPerSample = 1789773.0 / SAMPLE_RATE;
    double cycleAccumulator = 0.0;
    

    // Main loop
    bool done = false;
    bool full_screen = false;

    static int scale = 3;
    static int scaleIndex = 2;
    const char* scales[] =
    {
        "1x (256x240)",
        "2x (512x480)",
        "3x (768x720)",
        "4x (1024x960)",
        "5x (1280x1200)"
    };

    ImVec2 nes_window = ImVec2(256 * scale, 240 * scale);

    static std::string selectedRom;

    enum class AppState
    {
        Launcher,
        Running
    };

    AppState state = AppState::Launcher;

    while (!done) {
        MSG msg;
        while (::PeekMessage(&msg, nullptr, 0U, 0U, PM_REMOVE))
        {
            ::TranslateMessage(&msg);
            ::DispatchMessage(&msg);
            if (msg.message == WM_QUIT)
                done = true;
        }
        if (done)
            break;

        // Handle window being minimized or screen locked
        if (g_SwapChainOccluded && g_pSwapChain->Present(0, DXGI_PRESENT_TEST) == DXGI_STATUS_OCCLUDED)
        {
            ::Sleep(10);
            continue;
        }
        g_SwapChainOccluded = false;

        // Handle window resize (we don't resize directly in the WM_SIZE handler)
        if (g_ResizeWidth != 0 && g_ResizeHeight != 0)
        {
            CleanupRenderTarget();
            g_pSwapChain->ResizeBuffers(0, g_ResizeWidth, g_ResizeHeight, DXGI_FORMAT_UNKNOWN, 0);
            g_ResizeWidth = g_ResizeHeight = 0;
            CreateRenderTarget();
        }

        // Start the Dear ImGui frame
        ImGui_ImplDX11_NewFrame();
        ImGui_ImplWin32_NewFrame();
        ImGui::NewFrame();

        if (state == AppState::Launcher) {
            ImGui::SetNextWindowPos(ImVec2(0, 0));
            ImGui::SetNextWindowSize(io.DisplaySize);

            ImGui::Begin(
                "Launcher",
                nullptr,
                ImGuiWindowFlags_NoTitleBar |
                ImGuiWindowFlags_NoResize |
                ImGuiWindowFlags_NoMove
            );

            ImGui::SetCursorPos(ImVec2(10, 10));

            if (ImGui::Button("Abrir ROM"))
            {
                std::string path = OpenFileDialog();

                if (!path.empty()) {
                    selectedRom = path;

                    // Load the cartridge
                    cart = std::make_shared<Cartridge>(selectedRom);
                    if (cart->ImageValid()) {
                        // Insert into NES
                        nes.insertCartridge(cart);

                        // Reset NES
                        nes.reset();

                        state = AppState::Running;
                    }
                }
            }
            ImGui::SetCursorPos(ImVec2(100, 10));

            ImGui::SetNextItemWidth(120.0f);

            if (ImGui::Combo("Escala", &scaleIndex, scales, IM_ARRAYSIZE(scales))) {
                scale = scaleIndex + 1;
                nes_window = ImVec2(256 * scale, 240 * scale);
            };

            ImGui::Separator();

            ImGui::End();
        } else {
            // Handle input for controller in port #1
            nes.controller[0] =
                (ImGui::IsKeyDown(ImGuiKey_K) ? 0x80 : 0x00) |           // A
                (ImGui::IsKeyDown(ImGuiKey_L) ? 0x40 : 0x00) |           // B
                (ImGui::IsKeyDown(ImGuiKey_Backspace) ? 0x20 : 0x00) |   // Select
                (ImGui::IsKeyDown(ImGuiKey_Enter) ? 0x10 : 0x00) |       // Start
                (ImGui::IsKeyDown(ImGuiKey_W) ? 0x08 : 0x00) |           // Up
                (ImGui::IsKeyDown(ImGuiKey_S) ? 0x04 : 0x00) |           // Down
                (ImGui::IsKeyDown(ImGuiKey_A) ? 0x02 : 0x00) |           // Left
                (ImGui::IsKeyDown(ImGuiKey_D) ? 0x01 : 0x00);            //Rigth

            if (ImGui::IsKeyPressed(ImGuiKey_F)) {
                full_screen = !full_screen;
                g_pSwapChain->SetFullscreenState(full_screen, nullptr);
            }

            if (ImGui::IsKeyPressed(ImGuiKey_R)) {
                full_screen = false;
                g_pSwapChain->SetFullscreenState(full_screen, nullptr);
                state = AppState::Launcher;
                nes.reset();                
            }

            // While de clock():
            while (!nes.ppu.frame_complete)
            {
                nes.clock();
                cycleAccumulator++;

                if (cycleAccumulator >= cpuCyclesPerSample)
                {
                    cycleAccumulator -= cpuCyclesPerSample;
                    float raw = (float)nes.apu.GetOutputSample();
                    RingPush((int16_t)(raw * 32767.0f));
                }
            }

            if (nes.ppu.frame_complete)
            {
                UpdateTexture(nes.ppu.GetFrameBuffer());
                nes.ppu.frame_complete = false;
            }

            ImGui::SetNextWindowPos(ImVec2(0, 0));
            ImGui::SetNextWindowSize(io.DisplaySize);

            ImGui::Begin(
                "Running",
                nullptr,
                ImGuiWindowFlags_NoTitleBar |
                ImGuiWindowFlags_NoResize |
                ImGuiWindowFlags_NoMove
            );

            if (full_screen) {
                float fnes_x = ((io.DisplaySize.y * nes_window.x) / nes_window.y);
                ImGui::SetCursorPos(ImVec2((io.DisplaySize.x/2) - fnes_x/2 , 0));
                ImGui::Image(
                    (ImTextureID)g_pScreenSRV,
                    ImVec2(fnes_x, io.DisplaySize.y-8)
                );
            }else {
                ImGui::SetCursorPos((io.DisplaySize / 2) - (nes_window / 2));
                ImGui::Image(
                    (ImTextureID)g_pScreenSRV,
                    nes_window
                );
            }

            ImGui::End();
        }

        // Rendering
        ImGui::Render();
        const float clear_color_with_alpha[4] = { clear_color.x * clear_color.w, clear_color.y * clear_color.w, clear_color.z * clear_color.w, clear_color.w };
        g_pd3dDeviceContext->OMSetRenderTargets(1, &g_mainRenderTargetView, nullptr);
        g_pd3dDeviceContext->ClearRenderTargetView(g_mainRenderTargetView, clear_color_with_alpha);
        ImGui_ImplDX11_RenderDrawData(ImGui::GetDrawData());

        // Present
        HRESULT hr = g_pSwapChain->Present(1, 0);   // Present with vsync
        //HRESULT hr = g_pSwapChain->Present(0, 0); // Present without vsync
        g_SwapChainOccluded = (hr == DXGI_STATUS_OCCLUDED);
    }

    g_pSourceVoice->Stop(0);
    g_pSourceVoice->DestroyVoice();
    g_pMasterVoice->DestroyVoice();
    g_pXAudio2->Release();
    CoUninitialize();

    ImGui_ImplDX11_Shutdown();
    ImGui_ImplWin32_Shutdown();
    ImGui::DestroyContext();

    CleanupDeviceD3D();
    ::DestroyWindow(hwnd);
    ::UnregisterClassW(wc.lpszClassName, wc.hInstance);

    timeEndPeriod(1);
    return 0;
}