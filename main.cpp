#include <SDL.h>
#include <GL/gl.h>
#include "imgui.h"
#include "imgui_impl_sdl2.h"
#include "imgui_impl_opengl3.h"
#include "memory.h"
#include "offsets.h"
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

// ---------------- config ----------------
struct Config {
    bool box = true;
    bool headDot = true;
    bool distance = true;
    float maxDistance = 400.0f;
} cfg;

static const char* kCfgPath = "cf_esp.json";

static void SaveConfig() {
    std::ofstream f(kCfgPath);
    f << "{\n"
      << "\"box\":" << (cfg.box ? "true" : "false") << ",\n"
      << "\"headDot\":" << (cfg.headDot ? "true" : "false") << ",\n"
      << "\"distance\":" << (cfg.distance ? "true" : "false") << ",\n"
      << "\"maxDistance\":" << cfg.maxDistance << "\n"
      << "}\n";
}

static bool ReadBool(const std::string& s, const char* key, bool& out) {
    std::string k = std::string("\"") + key + "\":";
    size_t p = s.find(k);
    if (p == std::string::npos) return false;
    p += k.size();
    while (p < s.size() && s[p] == ' ') ++p;
    out = s.compare(p, 4, "true") == 0;
    return true;
}

static void LoadConfig() {
    std::ifstream f(kCfgPath);
    if (!f) return;
    std::stringstream ss;
    ss << f.rdbuf();
    const std::string s = ss.str();
    ReadBool(s, "box", cfg.box);
    ReadBool(s, "headDot", cfg.headDot);
    ReadBool(s, "distance", cfg.distance);
    size_t p = s.find("\"maxDistance\":");
    if (p != std::string::npos) cfg.maxDistance = std::strtof(s.c_str() + p + 14, nullptr);
}

// ---------------- math ----------------
struct V3 { float x, y, z; };
struct Quat { float x, y, z, w; };
// Unity TransformHierarchy element, stride 0x30: pos @0x0, rot @0x10, scale @0x20
struct Tr {
    V3 pos;
    float pad0;
    Quat rot;
    V3 scale;
    float pad1;
};
static_assert(sizeof(Tr) == 0x30, "Tr must match Unity transform stride");

static V3 operator+(V3 a, V3 b) { return {a.x + b.x, a.y + b.y, a.z + b.z}; }
static V3 Mul(V3 a, V3 b) { return {a.x * b.x, a.y * b.y, a.z * b.z}; }
static V3 Scale(V3 a, float s) { return {a.x * s, a.y * s, a.z * s}; }
static V3 Cross(V3 a, V3 b) {
    return {a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x};
}
static V3 Rotate(Quat q, V3 v) {
    V3 u{q.x, q.y, q.z};
    V3 t = Scale(Cross(u, v), 2.0f);
    return v + Scale(t, q.w) + Cross(u, t);
}
static Quat QMul(Quat a, Quat b) {
    return {a.w * b.x + a.x * b.w + a.y * b.z - a.z * b.y,
            a.w * b.y - a.x * b.z + a.y * b.w + a.z * b.x,
            a.w * b.z + a.x * b.y - a.y * b.x + a.z * b.w,
            a.w * b.w - a.x * b.x - a.y * b.y - a.z * b.z};
}

// Walks the parent chain and composes world transform of transform `idx`.
static Tr WorldOf(uintptr_t transforms, uintptr_t parents, int idx, int depth) {
    Tr t = mem::Read<Tr>(transforms + static_cast<uintptr_t>(idx) * sizeof(Tr));
    if (depth > 64) return t;
    int p = mem::Read<int>(parents + static_cast<uintptr_t>(idx) * sizeof(int));
    if (p < 0 || p == idx) return t;
    Tr P = WorldOf(transforms, parents, p, depth + 1);
    Tr w{};
    w.pos = P.pos + Rotate(P.rot, Mul(P.scale, t.pos));
    w.rot = QMul(P.rot, t.rot);
    w.scale = Mul(P.scale, t.scale);
    return w;
}

static bool NativeWorldPos(uintptr_t nativeTransform, V3& out) {
    uintptr_t ta = mem::Read<uintptr_t>(nativeTransform + off::kNative_TransformAccess);
    if (!ta) return false;
    uintptr_t transforms = mem::Read<uintptr_t>(ta + off::kTA_Transforms);
    uintptr_t hier = mem::Read<uintptr_t>(ta + off::kTA_Hierarchy);
    int idx = mem::Read<int>(ta + off::kTA_Index);
    if (!transforms || !hier || idx < 0) return false;
    uintptr_t parents = mem::Read<uintptr_t>(hier + off::kHier_Parents);
    if (!parents) return false;
    out = WorldOf(transforms, parents, idx, 0).pos;
    return true;
}

// ---------------- world to screen ----------------
// Unity convention: m is column-major float[16] (view*proj), clip = M * (x, y, z, 1).
// Unity origin is bottom-left, so screen Y is inverted for ImGui (top-left origin).
static bool WorldToScreen(const float* m, V3 p, ImVec2 screen, ImVec2& out, float& depth) {
    float cx = m[0] * p.x + m[4] * p.y + m[8] * p.z + m[12];
    float cy = m[1] * p.x + m[5] * p.y + m[9] * p.z + m[13];
    float w = m[3] * p.x + m[7] * p.y + m[11] * p.z + m[15];
    if (w < 0.01f) return false;
    out.x = (cx / w * 0.5f + 0.5f) * screen.x;
    out.y = (1.0f - (cy / w * 0.5f + 0.5f)) * screen.y;
    depth = w;  // clip w == view-space depth for Unity perspective, i.e. meters
    return true;
}

// ---------------- game reads ----------------
static bool ReadViewMatrix(float m[16]) {
    if (!off::kViewMatrix_RVA) return false;
    uintptr_t p = mem::Read<uintptr_t>(mem::g_base + off::kViewMatrix_RVA);
    return p && mem::ReadBytes(p, m, 16 * sizeof(float));
}

static std::vector<uintptr_t> ReadCasters() {
    std::vector<uintptr_t> out;
    if (!off::kStatic_CasterList_RVA) return out;
    uintptr_t statics = mem::Read<uintptr_t>(mem::g_base + off::kStatic_CasterList_RVA);
    if (!statics) return out;
    uintptr_t list = mem::Read<uintptr_t>(statics + off::kStatic_CasterList_Off);
    if (!list) return out;
    int count = mem::Read<int>(list + off::kList_Size);
    uintptr_t items = mem::Read<uintptr_t>(list + off::kList_Items);
    if (!items || count <= 0 || count > 64) return out;
    for (int i = 0; i < count; ++i) {
        uintptr_t s = mem::Read<uintptr_t>(items + off::kArr_Data + i * sizeof(uintptr_t));
        if (s) out.push_back(s);
    }
    return out;
}

static bool BonePos(uintptr_t skel, size_t boneOff, V3& out) {
    uintptr_t bone = mem::Read<uintptr_t>(skel + boneOff);
    if (!bone) return false;
    uintptr_t nativeRb = mem::Read<uintptr_t>(bone + off::kUnityObj_CachedPtr);
    if (!nativeRb) return false;
    uintptr_t tr = mem::Read<uintptr_t>(nativeRb + off::kNative_ComponentTransform);
    if (!tr) return false;
    return NativeWorldPos(tr, out);
}

struct Target {
    ImVec2 head;
    ImVec2 feet;
    float depth;
};

static std::vector<Target> CollectTargets(const std::vector<uintptr_t>& casters,
                                          const float* vm, ImVec2 screen) {
    std::vector<Target> out;
    for (uintptr_t s : casters) {
        V3 head{}, pelvis{};
        if (!BonePos(s, off::kSkel_HeadBone, head)) continue;
        if (!BonePos(s, off::kSkel_PelvisBone, pelvis)) continue;
        V3 feet{pelvis.x, pelvis.y - 0.9f, pelvis.z};  // Unity Y-up, approx. 0.9 m pelvis to sole
        ImVec2 hs, fs;
        float d1 = 0.0f, d2 = 0.0f;
        if (!WorldToScreen(vm, head, screen, hs, d1)) continue;
        if (!WorldToScreen(vm, feet, screen, fs, d2)) continue;
        if (d1 > cfg.maxDistance) continue;
        out.push_back({hs, fs, d1});
    }
    return out;
}

static void DrawESP(const std::vector<Target>& ts) {
    ImDrawList* dl = ImGui::GetBackgroundDrawList();
    for (const Target& t : ts) {
        float h = t.feet.y - t.head.y;
        if (h <= 1.0f) continue;
        float w = h * 0.5f;
        ImVec2 tl(t.head.x - w * 0.5f, t.head.y);
        ImVec2 br(t.head.x + w * 0.5f, t.feet.y);
        if (cfg.box) dl->AddRect(tl, br, IM_COL32(255, 64, 64, 255), 0.0f, 0, 1.5f);
        if (cfg.headDot) dl->AddCircleFilled(t.head, 2.5f, IM_COL32(255, 255, 255, 255));
        if (cfg.distance) {
            char buf[32];
            std::snprintf(buf, sizeof buf, "%.0fm", t.depth);
            dl->AddText(ImVec2(br.x + 4.0f, tl.y), IM_COL32(255, 255, 255, 255), buf);
        }
    }
}

static void DrawMenu(const char* status) {
    ImGui::SetNextWindowSize(ImVec2(340, 240), ImGuiCond_FirstUseEver);
    ImGui::Begin("CF ESP");
    if (ImGui::BeginTabBar("tabs")) {
        if (ImGui::BeginTabItem("ESP")) {
            ImGui::Checkbox("Box", &cfg.box);
            ImGui::Checkbox("Head dot", &cfg.headDot);
            ImGui::Checkbox("Distance", &cfg.distance);
            ImGui::SliderFloat("Max distance", &cfg.maxDistance, 10.0f, 1000.0f, "%.0f m");
            ImGui::EndTabItem();
        }
        if (ImGui::BeginTabItem("MISC")) {
            ImGui::TextWrapped("%s", status);
            ImGui::Text("INSERT: menu   END: quit");
            ImGui::EndTabItem();
        }
        if (ImGui::BeginTabItem("CONFIG")) {
            if (ImGui::Button("Save")) SaveConfig();
            ImGui::SameLine();
            if (ImGui::Button("Load")) LoadConfig();
            ImGui::Text("file: %s", kCfgPath);
            ImGui::EndTabItem();
        }
        ImGui::EndTabBar();
    }
    ImGui::End();
}

// ---------------- main ----------------
int main(int argc, char** argv) {
    if (argc < 2) {
        std::fprintf(stderr, "usage: sudo %s <process_name>\n", argv[0]);
        return 1;
    }
    int pid = mem::FindPid(argv[1]);
    if (!pid) {
        std::fprintf(stderr, "process '%s' not found\n", argv[1]);
        return 1;
    }
    if (!mem::Attach(pid)) {
        std::fprintf(stderr, "attach failed: need root, and libil2cpp.so must be mapped\n");
        return 1;
    }
    LoadConfig();

    if (SDL_Init(SDL_INIT_VIDEO) != 0) {
        std::fprintf(stderr, "SDL_Init: %s\n", SDL_GetError());
        return 1;
    }
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 2);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 1);
    SDL_GL_SetAttribute(SDL_GL_DOUBLEBUFFER, 1);
    SDL_GL_SetAttribute(SDL_GL_ALPHA_SIZE, 8);

    SDL_DisplayMode dm;
    SDL_GetCurrentDisplayMode(0, &dm);
    Uint32 flags = SDL_WINDOW_OPENGL | SDL_WINDOW_BORDERLESS | SDL_WINDOW_ALWAYS_ON_TOP |
                   SDL_WINDOW_SKIP_TASKBAR;
#if SDL_VERSION_ATLEAST(2, 0, 22)
    flags |= SDL_WINDOW_TRANSPARENT;
#endif
    SDL_Window* win = SDL_CreateWindow("cf_esp", 0, 0, dm.w, dm.h, flags);
    SDL_GLContext gl = SDL_GL_CreateContext(win);
    SDL_GL_SetSwapInterval(1);

    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGui_ImplSDL2_InitForOpenGL(win, gl);
    ImGui_ImplOpenGL3_Init("#version 130");

    bool running = true;
    bool menuOpen = true;
    while (running) {
        SDL_Event e;
        while (SDL_PollEvent(&e)) {
            ImGui_ImplSDL2_ProcessEvent(&e);
            if (e.type == SDL_QUIT) running = false;
            if (e.type == SDL_KEYDOWN && !e.key.repeat) {
                if (e.key.keysym.sym == SDLK_INSERT) menuOpen = !menuOpen;
                if (e.key.keysym.sym == SDLK_END) running = false;
            }
        }

        ImGui_ImplOpenGL3_NewFrame();
        ImGui_ImplSDL2_NewFrame();
        ImGui::NewFrame();

        int w = 0, h = 0;
        SDL_GetWindowSize(win, &w, &h);
        ImVec2 screen(static_cast<float>(w), static_cast<float>(h));

        float vm[16] = {};
        bool haveVm = ReadViewMatrix(vm);
        std::vector<uintptr_t> casters = ReadCasters();
        if (haveVm && !casters.empty()) {
            DrawESP(CollectTargets(casters, vm, screen));
        }

        char status[160];
        std::snprintf(status, sizeof status,
                      "casters: %zu | view matrix: %s | team/health: FILL (see offsets.h)",
                      casters.size(), haveVm ? "ok" : "FILL kViewMatrix_RVA");
        if (menuOpen) DrawMenu(status);

        ImGui::Render();
        glViewport(0, 0, w, h);
        glClearColor(0.0f, 0.0f, 0.0f, 0.0f);
        glClear(GL_COLOR_BUFFER_BIT);
        ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
        SDL_GL_SwapWindow(win);
    }

    ImGui_ImplOpenGL3_Shutdown();
    ImGui_ImplSDL2_Shutdown();
    ImGui::DestroyContext();
    SDL_GL_DeleteContext(gl);
    SDL_DestroyWindow(win);
    SDL_Quit();
    return 0;
}
