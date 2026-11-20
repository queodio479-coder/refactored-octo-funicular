// SoundSkinPanel.asi - painel ImGui (DX9) para SA-MP (R1/R2/R3/R4/R5/DL)
// Build: Visual Studio, x86, Release, C++17, DLL, extensao final .asi
// Libs: d3d9.lib (o CMake baixa ImGui, miniaudio e stb sozinho)
// Dependencias na mesma pasta: imgui (core + backends dx9/win32) e miniaudio.h
#include <windows.h>
#include <d3d9.h>
#include <cstdio>
#include <cstdarg>
#include <filesystem>
#include <string>
#include <vector>
#include <map>
#include <random>
#include "imgui.h"
#include "imgui_impl_dx9.h"
#include "imgui_impl_win32.h"
#define STB_IMAGE_IMPLEMENTATION
#include "stb_image.h"
#define MINIAUDIO_IMPLEMENTATION
#include "miniaudio.h"

namespace fs = std::filesystem;
extern IMGUI_IMPL_API LRESULT ImGui_ImplWin32_WndProcHandler(HWND, UINT, WPARAM, LPARAM);

// ---------------- Configuracao ----------------
static const char* PANEL_TITLE = "boa adm arrombado";   // titulo (em branco)
struct Config {
    float weaponVol = 1.0f, hitVol = 1.0f, painVol = 1.0f;
    bool  muteOthers = false;
    int   toggleKey = VK_F10;
} cfg;

static bool g_open = false, g_init = false, g_audioFailed = false;
static void Log(const char* fmt, ...) {            // log em SoundSkinPanel/log.txt
    FILE* f = fopen("SoundSkinPanel/log.txt", "a"); if (!f) return;
    va_list a; va_start(a, fmt); vfprintf(f, fmt, a); va_end(a); fputc('\n', f); fclose(f);
}
static HWND g_hwnd = nullptr;
static WNDPROC g_oldWndProc = nullptr;
static ma_engine g_engine;
static int g_skin = -1;
static std::mt19937 rng{ std::random_device{}() };

// ---------------- Pastas ----------------
static const char* WEAPON_FOLDERS[] = { "sniper","deagle","m4","ak47","mp5","shotgun","pistol","others" };

static void CreateFolders() {
    fs::create_directories("SoundSkinPanel/sounds/hitsound");
    fs::create_directories("SoundSkinPanel/sounds/painsound");
    for (auto w : WEAPON_FOLDERS) fs::create_directories(std::string("SoundSkinPanel/sounds/weapons/") + w);
    fs::create_directories("SoundSkinPanel/previews");   // 0.png ... 311.png
}

static void LoadConfig() {
    const char* f = ".\\SoundSkinPanel\\config.ini";
    cfg.weaponVol = GetPrivateProfileIntA("vol", "weapon", 100, f) / 100.f;
    cfg.hitVol    = GetPrivateProfileIntA("vol", "hit",    100, f) / 100.f;
    cfg.painVol   = GetPrivateProfileIntA("vol", "pain",   100, f) / 100.f;
    cfg.muteOthers= GetPrivateProfileIntA("misc","mute",   0,   f) != 0;
}
static void SaveConfig() {
    const char* f = ".\\SoundSkinPanel\\config.ini";
    auto w = [&](const char* s, const char* k, int v) { WritePrivateProfileStringA(s, k, std::to_string(v).c_str(), f); };
    w("vol","weapon",(int)(cfg.weaponVol*100)); w("vol","hit",(int)(cfg.hitVol*100));
    w("vol","pain",(int)(cfg.painVol*100));     w("misc","mute",cfg.muteOthers);
}

// ---------------- Audio ----------------
static std::string PickFile(const std::string& dir) {
    std::vector<std::string> files;
    if (!fs::exists(dir)) return "";
    for (auto& e : fs::directory_iterator(dir)) {
        auto x = e.path().extension().string();
        if (x == ".wav" || x == ".mp3" || x == ".ogg" || x == ".flac") files.push_back(e.path().string());
    }
    if (files.empty()) return "";
    return files[rng() % files.size()];
}
static ma_context g_ctx;
static bool EnsureAudio() {                         // so abre o audio quando precisa tocar algo
    if (g_init) return true;
    if (g_audioFailed) return false;
    ma_backend backends[] = { ma_backend_winmm, ma_backend_dsound, ma_backend_wasapi };
    ma_result r = ma_context_init(backends, 3, nullptr, &g_ctx);
    if (r == MA_SUCCESS) {
        ma_engine_config ec = ma_engine_config_init(); ec.pContext = &g_ctx;
        r = ma_engine_init(&ec, &g_engine);
    }
    Log("audio init: %d", (int)r);
    if (r != MA_SUCCESS) { g_audioFailed = true; return false; }
    g_init = true; return true;
}
static void PlayFrom(const std::string& dir, float vol) {
    std::string f = PickFile(dir);
    if (f.empty()) return;
    if (!EnsureAudio()) return;
    // ma_engine_play_sound nao tem volume por som; usa grupo implicito: ajusta via sound
    static std::vector<ma_sound*> live;
    for (size_t i = 0; i < live.size();) {            // limpa sons terminados
        if (ma_sound_at_end(live[i])) { ma_sound_uninit(live[i]); delete live[i]; live.erase(live.begin()+i); }
        else ++i;
    }
    auto* s = new ma_sound;
    if (ma_sound_init_from_file(&g_engine, f.c_str(), MA_SOUND_FLAG_DECODE, nullptr, nullptr, s) == MA_SUCCESS) {
        ma_sound_set_volume(s, vol); ma_sound_start(s); live.push_back(s);
    } else delete s;
}
static const char* FolderForWeapon(int id) {
    switch (id) {
        case 34: case 33: return "sniper";
        case 24: return "deagle";
        case 31: return "m4";
        case 30: return "ak47";
        case 29: return "mp5";
        case 25: case 26: case 27: return "shotgun";
        case 22: case 23: return "pistol";
        default: return "others";
    }
}
void PlayHitSound()  { PlayFrom("SoundSkinPanel/sounds/hitsound",  cfg.hitVol); }   // ligue ao seu evento de acerto
void PlayPainSound() { PlayFrom("SoundSkinPanel/sounds/painsound", cfg.painVol); }

// ---------------- Leitura do jogo (GTA SA 1.0 US, igual em todas as versoes do SA-MP) ----------------
static DWORD PlayerPed() { return *(DWORD*)0xB6F5F0; }

static void GameTick() {
    DWORD ped = PlayerPed();
    if (!ped) return;
    static int lastClip = -1, lastType = -1; static float lastHp = -1;
    BYTE slot = *(BYTE*)(ped + 0x718);
    DWORD w = ped + 0x5A0 + slot * 0x1C;
    int type = *(int*)w, clip = *(int*)(w + 8);
    if (type == lastType && clip < lastClip && type > 0)           // tiro dado = municao do pente caiu
        PlayFrom(std::string("SoundSkinPanel/sounds/weapons/") + FolderForWeapon(type), cfg.weaponVol);
    lastType = type; lastClip = clip;

    float hp = *(float*)(ped + 0x540);
    if (lastHp > 0 && hp < lastHp) PlayPainSound();                // levou dano
    lastHp = hp;
}

// Skin local (so voce ve)
static void ApplySkin(int id) {
    DWORD ped = PlayerPed();
    if (!ped || id < 0 || id > 311) return;
    ((void(__cdecl*)(int,int))0x4087E0)(id, 2);       // CStreaming::RequestModel
    ((void(__cdecl*)(bool))0x40EA10)(false);          // LoadAllRequestedModels
    ((void(__thiscall*)(DWORD,int))0x5E4880)(ped, id);// CPed::SetModelIndex
    g_skin = id;
}

// ---------------- Skins: miniaturas sob demanda ----------------
static std::map<int, IDirect3DTexture9*> g_tex;
static IDirect3DDevice9* g_dev = nullptr;
static IDirect3DTexture9* GetPreview(int id) {
    auto it = g_tex.find(id);
    if (it != g_tex.end()) return it->second;
    IDirect3DTexture9* t = nullptr;
    std::string p = "SoundSkinPanel/previews/" + std::to_string(id) + ".png";
    if (fs::exists(p)) {
        int w, h, n; unsigned char* px = stbi_load(p.c_str(), &w, &h, &n, 4);
        if (px) {
            if (SUCCEEDED(g_dev->CreateTexture(w, h, 1, 0, D3DFMT_A8R8G8B8, D3DPOOL_MANAGED, &t, nullptr))) {
                D3DLOCKED_RECT lr;
                if (SUCCEEDED(t->LockRect(0, &lr, nullptr, 0))) {
                    for (int y = 0; y < h; y++) {
                        unsigned char* d = (unsigned char*)lr.pBits + y * lr.Pitch;
                        for (int x = 0; x < w; x++) {          // RGBA -> BGRA
                            unsigned char* sp = px + (y * w + x) * 4;
                            d[x*4+0] = sp[2]; d[x*4+1] = sp[1]; d[x*4+2] = sp[0]; d[x*4+3] = sp[3];
                        }
                    }
                    t->UnlockRect(0);
                } else { t->Release(); t = nullptr; }
            }
            stbi_image_free(px);
        }
    }
    g_tex[id] = t;                                     // guarda mesmo nulo p/ nao tentar todo frame
    return t;
}
static void ReleaseTextures() { for (auto& k : g_tex) if (k.second) k.second->Release(); g_tex.clear(); }

// ---------------- Tema preto ----------------
static void ApplyTheme() {
    ImGuiStyle& s = ImGui::GetStyle();
    s.WindowRounding = 8; s.FrameRounding = 5; s.GrabRounding = 5; s.TabRounding = 5;
    s.WindowBorderSize = 1; s.ScrollbarRounding = 6;
    ImVec4* c = s.Colors;
    ImVec4 bk(0.02f,0.02f,0.02f,0.97f), dk(0.09f,0.09f,0.09f,1), mid(0.16f,0.16f,0.16f,1), wh(1,1,1,1);
    c[ImGuiCol_WindowBg]=bk; c[ImGuiCol_ChildBg]=ImVec4(0,0,0,0); c[ImGuiCol_PopupBg]=bk;
    c[ImGuiCol_Border]=ImVec4(0.3f,0.3f,0.3f,1);
    c[ImGuiCol_Text]=wh; c[ImGuiCol_TextDisabled]=ImVec4(0.5f,0.5f,0.5f,1);
    c[ImGuiCol_TitleBg]=bk; c[ImGuiCol_TitleBgActive]=dk; c[ImGuiCol_TitleBgCollapsed]=bk;
    c[ImGuiCol_FrameBg]=dk; c[ImGuiCol_FrameBgHovered]=mid; c[ImGuiCol_FrameBgActive]=mid;
    c[ImGuiCol_Button]=dk; c[ImGuiCol_ButtonHovered]=mid; c[ImGuiCol_ButtonActive]=ImVec4(0.3f,0.3f,0.3f,1);
    c[ImGuiCol_Tab]=dk; c[ImGuiCol_TabHovered]=mid; c[ImGuiCol_TabActive]=ImVec4(0.22f,0.22f,0.22f,1);
    c[ImGuiCol_SliderGrab]=wh; c[ImGuiCol_SliderGrabActive]=ImVec4(0.8f,0.8f,0.8f,1);
    c[ImGuiCol_CheckMark]=wh; c[ImGuiCol_Header]=dk; c[ImGuiCol_HeaderHovered]=mid; c[ImGuiCol_HeaderActive]=mid;
    c[ImGuiCol_ScrollbarBg]=bk; c[ImGuiCol_ScrollbarGrab]=mid;
}

// ---------------- Painel ----------------
static void DrawPanel() {
    ImGuiIO& io = ImGui::GetIO();
    float sc = io.DisplaySize.y / 1080.0f; if (sc < 0.6f) sc = 0.6f;
    io.FontGlobalScale = sc * 1.2f;

    ImGui::SetNextWindowSize(ImVec2(620*sc, 520*sc), ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowPos(ImVec2(io.DisplaySize.x*0.5f, io.DisplaySize.y*0.5f), ImGuiCond_FirstUseEver, ImVec2(0.5f,0.5f));
    ImGui::SetNextWindowSizeConstraints(ImVec2(360, 300), ImVec2(io.DisplaySize.x, io.DisplaySize.y));

    // Titulo branco: usamos a barra padrao (movel) e o texto ja e branco no tema
    if (ImGui::Begin(PANEL_TITLE, &g_open, ImGuiWindowFlags_NoCollapse)) {
        if (ImGui::BeginTabBar("tabs")) {
            if (ImGui::BeginTabItem("Sons")) {
                bool ch = false;
                ch |= ImGui::SliderFloat("Volume armas", &cfg.weaponVol, 0, 1, "%.2f");
                ch |= ImGui::SliderFloat("Volume hit sound", &cfg.hitVol, 0, 1, "%.2f");
                ch |= ImGui::SliderFloat("Volume pain sound", &cfg.painVol, 0, 1, "%.2f");
                ch |= ImGui::Checkbox("Mutar audio de outros jogadores", &cfg.muteOthers);
                if (ch) SaveConfig();
                ImGui::Separator();
                ImGui::TextDisabled("Coloque .wav/.mp3/.ogg nas pastas:");
                ImGui::TextWrapped("SoundSkinPanel\\sounds\\hitsound\nSoundSkinPanel\\sounds\\painsound\n"
                                   "SoundSkinPanel\\sounds\\weapons\\<sniper|deagle|m4|ak47|mp5|shotgun|pistol|others>");
                if (ImGui::Button("Testar hit")) PlayHitSound();
                ImGui::SameLine(); if (ImGui::Button("Testar pain")) PlayPainSound();
                ImGui::EndTabItem();
            }
            if (ImGui::BeginTabItem("Skins")) {
                ImGui::Text("Skin atual: %d", g_skin);
                ImGui::BeginChild("grid", ImVec2(0,0), true);
                float cell = 84 * sc, pad = ImGui::GetStyle().ItemSpacing.x;
                int cols = (int)((ImGui::GetContentRegionAvail().x + pad) / (cell + pad)); if (cols < 1) cols = 1;
                ImGuiListClipper clip; clip.Begin((312 + cols - 1) / cols, cell + 22*sc + pad);
                while (clip.Step()) for (int r = clip.DisplayStart; r < clip.DisplayEnd; r++)
                    for (int cI = 0; cI < cols; cI++) {
                        int id = r * cols + cI; if (id > 311) break;
                        if (cI) ImGui::SameLine();
                        ImGui::PushID(id); ImGui::BeginGroup();
                        IDirect3DTexture9* t = GetPreview(id);
                        bool clicked = t ? ImGui::ImageButton("##s", (ImTextureID)t, ImVec2(cell, cell))
                                         : ImGui::Button("sem foto", ImVec2(cell + 8, cell + 8));
                        ImGui::Text("ID %d", id);
                        ImGui::EndGroup(); ImGui::PopID();
                        if (clicked) ApplySkin(id);
                    }
                ImGui::EndChild();
                ImGui::EndTabItem();
            }
            ImGui::EndTabBar();
        }
    }
    ImGui::End();
}

// ---------------- Hooks D3D9 ----------------
typedef HRESULT (WINAPI* EndScene_t)(IDirect3DDevice9*);
typedef HRESULT (WINAPI* Reset_t)(IDirect3DDevice9*, D3DPRESENT_PARAMETERS*);
static EndScene_t oEndScene; static Reset_t oReset;
static bool g_imguiReady = false;

static void TogglePanel() { g_open = !g_open; ImGui::GetIO().MouseDrawCursor = g_open; }

// Acompanha o que voce digita no chat para detectar o comando /bdm
static std::string g_typed;
static void TrackChat(WPARAM w) {
    if (w == VK_RETURN) {                                   // Enter: chat enviou
        std::string t = g_typed; g_typed.clear();
        for (auto& ch : t) ch = (char)tolower((unsigned char)ch);
        while (!t.empty() && t.back() == ' ') t.pop_back();
        if (t == "/bdm") TogglePanel();
    } else if (w == VK_ESCAPE) g_typed.clear();
    else if (w == VK_BACK) { if (!g_typed.empty()) g_typed.pop_back(); }
    else if (w >= 32 && w < 127 && g_typed.size() < 64) g_typed.push_back((char)w);
}

static LRESULT CALLBACK WndProcHook(HWND h, UINT m, WPARAM w, LPARAM l) {
    // Alt+J (Alt gera WM_SYSKEYDOWN) e tecla F10
    if (m == WM_SYSCHAR && (w == 'j' || w == 'J')) return 0;   // evita beep do Alt+J
    // comando /bdm: so acompanha quando o painel nao esta digitando
    if (g_imguiReady && !ImGui::GetIO().WantTextInput) {
        if (m == WM_CHAR) TrackChat(w);
        else if (m == WM_KEYDOWN && (w == VK_RETURN || w == VK_ESCAPE)) TrackChat(w);
    }
    if (g_imguiReady && g_open) {
        ImGui_ImplWin32_WndProcHandler(h, m, w, l);
        if (m >= WM_MOUSEFIRST && m <= WM_MOUSELAST) return 0;   // painel captura o mouse
        if (ImGui::GetIO().WantTextInput && (m == WM_KEYDOWN || m == WM_CHAR)) return 0;
    }
    return CallWindowProcA(g_oldWndProc, h, m, w, l);
}

static HRESULT WINAPI hkEndScene(IDirect3DDevice9* dev) {
    if (!g_imguiReady) {
        g_dev = dev;
        D3DDEVICE_CREATION_PARAMETERS cp; dev->GetCreationParameters(&cp); g_hwnd = cp.hFocusWindow;
        ImGui::CreateContext(); ImGui::GetIO().IniFilename = nullptr;
        ApplyTheme();
        ImGui_ImplWin32_Init(g_hwnd); ImGui_ImplDX9_Init(dev);
        g_oldWndProc = (WNDPROC)SetWindowLongPtrA(g_hwnd, GWLP_WNDPROC, (LONG_PTR)WndProcHook);
        g_imguiReady = true;
        Log("imgui pronto, hwnd=%p", (void*)g_hwnd);
    }
    {   // Alt+J e F10 por leitura direta do teclado
        static bool pj = false, pf = false; static int frames = 0;
        if (++frames == 1) Log("EndScene rodando");
        bool focus = GetForegroundWindow() == g_hwnd;
        bool j = focus && (GetAsyncKeyState(VK_MENU) & 0x8000) && (GetAsyncKeyState('J') & 0x8000);
        bool f = focus && (GetAsyncKeyState(cfg.toggleKey) & 0x8000);
        if ((j && !pj) || (f && !pf)) { TogglePanel(); Log("painel %s", g_open ? "aberto" : "fechado"); }
        pj = j; pf = f;
    }
    GameTick();
    if (g_open) {
        ImGui_ImplDX9_NewFrame(); ImGui_ImplWin32_NewFrame(); ImGui::NewFrame();
        DrawPanel();
        ImGui::EndFrame(); ImGui::Render(); ImGui_ImplDX9_RenderDrawData(ImGui::GetDrawData());
    }
    return oEndScene(dev);
}
static HRESULT WINAPI hkReset(IDirect3DDevice9* dev, D3DPRESENT_PARAMETERS* pp) {
    if (g_imguiReady) { ImGui_ImplDX9_InvalidateDeviceObjects(); ReleaseTextures(); }
    HRESULT r = oReset(dev, pp);
    if (g_imguiReady) ImGui_ImplDX9_CreateDeviceObjects();
    return r;
}

static void PatchVTable() {
    WNDCLASSA wc{}; wc.lpfnWndProc = DefWindowProcA; wc.hInstance = GetModuleHandleA(0); wc.lpszClassName = "ssp_dummy";
    RegisterClassA(&wc);
    HWND hw = CreateWindowA("ssp_dummy", "", WS_OVERLAPPED, 0, 0, 10, 10, 0, 0, wc.hInstance, 0);
    IDirect3D9* d3d = Direct3DCreate9(D3D_SDK_VERSION);
    D3DPRESENT_PARAMETERS pp{}; pp.Windowed = TRUE; pp.SwapEffect = D3DSWAPEFFECT_DISCARD; pp.hDeviceWindow = hw;
    pp.BackBufferFormat = D3DFMT_UNKNOWN;
    IDirect3DDevice9* dev = nullptr;
    HRESULT hr = d3d ? d3d->CreateDevice(D3DADAPTER_DEFAULT, D3DDEVTYPE_NULLREF, hw,
                         D3DCREATE_SOFTWARE_VERTEXPROCESSING, &pp, &dev) : E_FAIL;
    if (FAILED(hr) && d3d) hr = d3d->CreateDevice(D3DADAPTER_DEFAULT, D3DDEVTYPE_HAL, hw,
                         D3DCREATE_SOFTWARE_VERTEXPROCESSING, &pp, &dev);
    Log("dummy device hr=0x%08lX", (unsigned long)hr);
    if (SUCCEEDED(hr) && dev) {
        void** vt = *(void***)dev;                    // vtable compartilhada com o device do jogo
        DWORD old; VirtualProtect(&vt[16], 4*27, PAGE_READWRITE, &old);
        oReset = (Reset_t)vt[16]; oEndScene = (EndScene_t)vt[42];
        vt[16] = hkReset; vt[42] = hkEndScene;
        Log("vtable patch ok");
        VirtualProtect(&vt[16], 4*27, old, &old);
        dev->Release();
    }
    if (d3d) d3d->Release();
    DestroyWindow(hw); UnregisterClassA("ssp_dummy", wc.hInstance);
}

static DWORD WINAPI MainThread(LPVOID) {
    CreateFolders(); LoadConfig();
    Log("---- plugin carregado ----");
    while (!GetModuleHandleA("samp.dll")) Sleep(500);   // espera o SA-MP (qualquer versao)
    Log("samp.dll encontrado");
    Sleep(1500);
    PatchVTable();
    return 0;
}

BOOL APIENTRY DllMain(HMODULE h, DWORD reason, LPVOID) {
    if (reason == DLL_PROCESS_ATTACH) { DisableThreadLibraryCalls(h); CreateThread(0, 0, MainThread, 0, 0, 0); }
    return TRUE;
}
