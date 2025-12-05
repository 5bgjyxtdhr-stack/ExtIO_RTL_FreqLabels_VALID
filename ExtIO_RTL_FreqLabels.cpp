
// ExtIO_RTL_FreqLabels.cpp
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <commctrl.h>
#include <string>
#include <vector>
#include <fstream>
#include <sstream>
#include <algorithm>
#include <mutex>
#include <cmath>

struct LabelEntry {
    double freq_hz;          // frekvencia v Hz (LO/Tune)
    std::string name_utf8;   // UTF-8 text labelu (CSV)
};

static std::vector<LabelEntry> g_labels;
static std::mutex g_mutex;

static HWND g_hwndMain = nullptr;   // hlavné okno pluginu
static HWND g_hwndText = nullptr;   // statický text vo vnútri
static HFONT g_hFont = nullptr;

// ===== Pomocné funkcie =====
static std::wstring utf8_to_wstr(const std::string& s) {
    if (s.empty()) return L"";
    int n = MultiByteToWideChar(CP_UTF8, 0, s.c_str(), -1, nullptr, 0);
    std::wstring ws(n, L'\0');
    MultiByteToWideChar(CP_UTF8, 0, s.c_str(), -1, &ws[0], n);
    if (!ws.empty() && ws.back() == L'\0') ws.pop_back();
    return ws;
}
static void setTextUTF8(HWND h, const std::string& s) {
    SetWindowTextW(h, utf8_to_wstr(s).c_str());
}

static std::string getModuleDirA() {
    char path[MAX_PATH]{0};
    HMODULE hMod = nullptr;
    GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS |
                       GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                       (LPCSTR)&getModuleDirA, &hMod);
    GetModuleFileNameA(hMod, path, MAX_PATH);
    std::string s(path);
    auto pos = s.find_last_of("\\/");
    return (pos == std::string::npos) ? s : s.substr(0, pos);
}

static bool loadLabelsCSV(const std::string& csvPath) {
    std::ifstream f(csvPath, std::ios::binary);
    if (!f.is_open()) return false;

    std::string line;
    // pokus o detekciu hlavičky
    if (std::getline(f, line)) {
        if (line.find("freq_hz") == std::string::npos) {
            f.seekg(0);
        }
    }

    std::vector<LabelEntry> tmp;
    while (std::getline(f, line)) {
        if (line.empty()) continue;
        char sep = (line.find(';') != std::string::npos) ? ';' : ',';
        std::istringstream iss(line);
        std::string freqStr, name;
        if (!std::getline(iss, freqStr, sep)) continue;
        if (!std::getline(iss, name)) name = "";
        // trim
        freqStr.erase(std::remove_if(freqStr.begin(), freqStr.end(), ::isspace), freqStr.end());
        while (!name.empty() && (name.back() == '\r' || name.back() == '\n')) name.pop_back();

        try {
            double fHz = std::stod(freqStr);
            if (fHz > 0 && !name.empty()) {
                tmp.push_back({ fHz, name });
            }
        } catch (...) {
            // skip
        }
    }
    std::sort(tmp.begin(), tmp.end(), { return a.freq_hz < b.freq_hz; });

    std::lock_guard<std::mutex> lk(g_mutex);
    g_labels.swap(tmp);
    return !g_labels.empty();
}

// presná zhoda / tolerancia (default 5 kHz)
static std::string findLabel(double tunedHz, double toleranceHz = 5000.0, bool roundAirband = true) {
    std::lock_guard<std::mutex> lk(g_mutex);
    // voliteľné: zaokrúhlenie na airband raster 8.33 kHz
    if (roundAirband) {
        // 8.33 kHz ≈ 8333.333 Hz
        const double step = 8333.333;
        tunedHz = std::round(tunedHz / step) * step;
    }
    auto it = std::lower_bound(g_labels.begin(), g_labels.end(), tunedHz,
        { return e.freq_hz < v; });

    auto best = std::string{};
    auto check = &{
        if (iter != g_labels.end()) {
            double df = std::abs(iter->freq_hz - tunedHz);
            if (df <= toleranceHz) best = iter->name_utf8;
        }
    };
    check(it);
    if (best.empty() && it != g_labels.begin()) check(std::prev(it));
    return best;
}

// ===== GUI =====
static LRESULT CALLBACK WndProc(HWND h, UINT m, WPARAM w, LPARAM l) {
    switch (m) {
        case WM_CREATE: {
            g_hwndText = CreateWindowExW(0, L"STATIC", L"—",
                WS_CHILD | WS_VISIBLE, 10, 10, 560, 40, h, nullptr,
                (HINSTANCE)GetWindowLongPtr(h, GWLP_HINSTANCE), nullptr);

            if (!g_hFont) {
                g_hFont = CreateFontW(
                    24, 0, 0, 0, FW_BOLD, FALSE, FALSE, FALSE,
                    DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
                    CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_DONTCARE, L"Segoe UI");
            }
            if (g_hFont) SendMessageW(g_hwndText, WM_SETFONT, (WPARAM)g_hFont, TRUE);
            return 0;
        }
        case WM_CLOSE:
            ShowWindow(h, SW_HIDE); // iba skryť
            return 0;
        case WM_DESTROY:
            if (g_hFont) { DeleteObject(g_hFont); g_hFont = nullptr; }
            g_hwndText = nullptr;
            g_hwndMain = nullptr;
            return 0;
    }
    return DefWindowProcW(h, m, w, l);
}

static void ensureWindow() {
    if (g_hwndMain) return;
    const wchar_t* cls = L"ExtIO_FreqLabelsWnd";
    WNDCLASSW wc{};
    wc.style         = CS_HREDRAW | CS_VREDRAW;
    wc.lpfnWndProc   = WndProc;
    wc.hInstance     = GetModuleHandleW(nullptr);
    wc.hCursor       = LoadCursor(nullptr, IDC_ARROW);
    wc.hbrBackground = (HBRUSH)(COLOR_BTNFACE + 1);
    wc.lpszClassName = cls;
    RegisterClassW(&wc);

    g_hwndMain = CreateWindowExW(
        WS_EX_TOPMOST | WS_EX_TOOLWINDOW,
        cls, L"FreqLabels",
        WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX,
        100, 100, 600, 120,
        nullptr, nullptr, wc.hInstance, nullptr);

    ShowWindow(g_hwndMain, SW_SHOWNORMAL);
    UpdateWindow(g_hwndMain);
}

static void updateLabelUI(const std::string& labelUtf8, double tunedHz = 0.0) {
    if (!g_hwndMain) return;
    std::wstring header = L"FreqLabels";
    if (tunedHz > 0.0) {
        wchar_t buf[128];
        swprintf(buf, 128, L"FreqLabels — %.3f MHz", tunedHz / 1e6);
        header = buf;
    }
    SetWindowTextW(g_hwndMain, header.c_str());
    setTextUTF8(g_hwndText ? g_hwndText : g_hwndMain,
                labelUtf8.empty() ? std::string("—") : labelUtf8);
}

// ===== ExtIO exports (Winrad/HDSDR) =====
extern "C" {

// Pozn.: konvencie __stdcall + extern "C" vyžaduje ExtIO špecifikácia. [1](https://github.com/rtlsdrblog/ExtIO_RTL/)
__declspec(dllexport) bool __stdcall InitHW(char* name, char* model, int& type) {
    // zobrazované v HDSDR menu / titulku
    strcpy_s(name, 64, "RTL FreqLabels");
    strcpy_s(model, 64, "AIR");

    // typ: ak ovládaš len LO/tune (bez vlastného streamu), použije sa 4 (sound card path)
    type = 4;  // podľa Winrad/ExtIO spec (pozri "type" v InitHW). [1](https://github.com/rtlsdrblog/ExtIO_RTL/)

    const std::string csv = getModuleDirA() + "\\\\freq_labels.csv";
    loadLabelsCSV(csv); // ak chýba, UI zobrazí "—"
    return true;
}

__declspec(dllexport) bool __stdcall OpenHW(void) {
    return true;
}

__declspec(dllexport) void __stdcall CloseHW(void) {
    if (g_hwndMain) DestroyWindow(g_hwndMain);
}

__declspec(dllexport) int  __stdcall StartHW(long freq) {
    // Ak by DLL posielala IQ cez callback, vrátiš veľkosť bloku (>=512).
    // Keďže tu len riadime a zobrazujeme label, hodnota sa nevyužije.
    return 512;  // kompatibilne s Winrad/ExtIO požiadavkou. [1](https://github.com/rtlsdrblog/ExtIO_RTL/)
}

__declspec(dllexport) void __stdcall StopHW(void) {}

// Hlavný hook: pri každej zmene LO
__declspec(dllexport) int __stdcall SetHWLO(long LOfreq) {
    ensureWindow();
    const double tuned = static_cast<double>(LOfreq);
    const std::string lbl = findLabel(tuned, /*toleranceHz*/ 5000.0, /*roundAirband*/ true);
    updateLabelUI(lbl, tuned);
    return 0; // OK
}

// Voliteľne: reagovať na zmenu "Tune" (nie iba LO)
__declspec(dllexport) void __stdcall TuneChanged(long freq) {
    ensureWindow();
    const double tuned = static_cast<double>(freq);
    const std::string lbl = findLabel(tuned, /*toleranceHz*/ 5000.0, /*roundAirband*/ true);
    updateLabelUI(lbl, tuned);
}

__declspec(dllexport) long __stdcall GetHWLO(void) {
    return 0; // (ak si LO držíš v globále, vráť aktuálnu)
}

// Musí existovať (HDSDR kontroluje prítomnosť), aj keď je "dummy". [1](https://github.com/rtlsdrblog/ExtIO_RTL/)
__declspec(dllexport) int __stdcall GetStatus(void) { return 0; }

__declspec(dllexport) void __stdcall ShowGUI(void) {
    ensureWindow();
    ShowWindow(g_hwndMain, SW_SHOWNORMAL);
}

__declspec(dllexport) void __stdcall HideGUI(void) {
    if (g_hwndMain) ShowWindow(g_hwndMain, SW_HIDE);
}

} // extern "C"
