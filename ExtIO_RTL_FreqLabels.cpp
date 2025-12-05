
// ExtIO RTL-FreqLabels: CSV lookup (±10 kHz) + jednoduché GUI okno so statickým textom
// Rozhranie Winrad/ExtIO: extern "C", __stdcall, __declspec(dllexport)

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cctype>

#include <windows.h>
#pragma comment(lib, "user32.lib")

#define NAME_MAX   64
#define MODEL_MAX  64

extern "C" {

// ---------- Typy a globálne premenné ----------

typedef void (__stdcall *ExtIOHostCallback)(int, int, float, void*);

static ExtIOHostCallback g_cb = nullptr;
static long  g_lo        = 10000000;         // Hz
static bool  g_running   = false;

// CSV tabuľka
struct FreqName { long hz; char name[128]; };
static FreqName* g_table = nullptr;
static int       g_table_count = 0;

static const long TOL_HZ = 10000;            // ±10 kHz
static char g_last_label[128] = {0};

// GUI
static HWND g_hwnd   = nullptr;              // hlavné okno pluginu
static HWND g_hLabel = nullptr;              // statický text vo vnútri

// ---------- Pomocné funkcie (string/CSV) ----------

static void copy_str(char* dst, const char* src, int maxLen)
{
    if (!dst || !src || maxLen <= 0) return;
    int i = 0;
    for (; i < maxLen - 1 && src[i] != '\0'; ++i) dst[i] = src[i];
    dst[i] = '\0';
}

static char* trim(char* s)
{
    if (!s) return s;
    while (*s && std::isspace((unsigned char)*s)) ++s;
    if (!*s) return s;
    char* e = s + std::strlen(s) - 1;
    while (e > s && std::isspace((unsigned char)*e)) { *e = '\0'; --e; }
    return s;
}

static bool load_csv_labels()
{
    const char* up = ::getenv("USERPROFILE");
    char path[512];
    FILE* f = nullptr;

    if (up) {
        std::snprintf(path, sizeof(path), "%s\\Documents\\HDSDR\\freq_labels.csv", up);
        f = std::fopen(path, "rb");
    }
    if (!f) {
        // fallback – pohľadaj v aktuálnom priečinku (pri HDSDR.exe)
        std::snprintf(path, sizeof(path), "freq_labels.csv");
        f = std::fopen(path, "rb");
    }
    if (!f) return false;

    const size_t LINE_MAX = 1024;
    char line[LINE_MAX];

    // preskoč hlavičku
    if (!std::fgets(line, (int)LINE_MAX, f)) { std::fclose(f); return false; }

    // spočítaj riadky
    int rows = 0;
    while (std::fgets(line, (int)LINE_MAX, f)) {
        if (std::strchr(line, ',')) rows++;
    }
    if (rows <= 0) { std::fclose(f); return false; }

    // alokácia
    g_table = new FreqName[rows];
    g_table_count = 0;

    // načítanie
    std::rewind(f);
    std::fgets(line, (int)LINE_MAX, f); // hlavička
    while (std::fgets(line, (int)LINE_MAX, f)) {
        char* comma = std::strchr(line, ',');
        if (!comma) continue;
        *comma = '\0';
        char* fstr = trim(line);
        char* nstr = trim(comma + 1);

        // odsekni CR/LF v názve
        size_t ln = std::strlen(nstr);
        while (ln && (nstr[ln - 1] == '\r' || nstr[ln - 1] == '\n')) { nstr[--ln] = '\0'; }

        long hz = 0;
        if (std::sscanf(fstr, "%ld", &hz) != 1) continue;
        if (hz <= 0) continue;

        g_table[g_table_count].hz = hz;
        copy_str(g_table[g_table_count].name, nstr, (int)sizeof(g_table[g_table_count].name));
        g_table_count++;
    }
    std::fclose(f);
    return (g_table_count > 0);
}

static const char* find_label_for(long hz)
{
    if (!g_table || g_table_count <= 0) return nullptr;

    // presná zhoda
    for (int i = 0; i < g_table_count; ++i)
        if (g_table[i].hz == hz) return g_table[i].name;

    // v tolerancii ±TOL_HZ (krok 100 Hz)
    for (long d = 0; d <= TOL_HZ; d += 100) {
        long f1 = hz + d, f2 = hz - d;
        for (int i = 0; i < g_table_count; ++i) {
            if (g_table[i].hz == f1) return g_table[i].name;
            if (g_table[i].hz == f2) return g_table[i].name;
        }
    }
    return nullptr;
}

static void update_last_label_for(long hz)
{
    const char* lab = find_label_for(hz);
    if (lab) copy_str(g_last_label, lab, (int)sizeof(g_last_label));
    else     g_last_label[0] = '\0';
}

// ---------- GUI (Win32) ----------

static void set_label_text(HWND h, const char* txt)
{
    // CSV je v UTF-8; pre konverziu na UTF-16 použijeme MultiByteToWideChar
    wchar_t wbuf[256];
    const char* src = (txt && txt[0]) ? txt : "(no match)";
    int n = MultiByteToWideChar(CP_UTF8, 0, src, -1, wbuf, (int)(sizeof(wbuf)/sizeof(wbuf[0])));
    if (n <= 0) {
        // fallback – ANSI
        size_t i=0; for (; i < (sizeof(wbuf)/sizeof(wbuf[0])) - 1 && src[i]; ++i) wbuf[i] = (unsigned char)src[i];
        wbuf[i] = L'\0';
    }
    SetWindowTextW(h, wbuf);
}

static LRESULT CALLBACK GuiWndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
    switch (msg) {
    case WM_CREATE:
        g_hLabel = CreateWindowExW(
            0, L"STATIC", L"",
            WS_CHILD | WS_VISIBLE | SS_LEFT,
            10, 10, 360, 24,
            hwnd, (HMENU)1, (HINSTANCE)GetWindowLongPtr(hwnd, GWLP_HINSTANCE), nullptr);
        set_label_text(g_hLabel, g_last_label);
        return 0;

    case WM_CLOSE:
        ShowWindow(hwnd, SW_HIDE);   // len skryjeme okno
        return 0;

    case WM_DESTROY:
        g_hwnd = nullptr;
        g_hLabel = nullptr;
        return 0;
    }
    return DefWindowProcW(hwnd, msg, wParam, lParam);
}

static void ensure_gui()
{
    if (g_hwnd && IsWindow(g_hwnd)) return;

    WNDCLASSEXW wc = { 0 };
    wc.cbSize        = sizeof(wc);
    wc.style         = CS_DBLCLKS | CS_HREDRAW | CS_VREDRAW;
    wc.lpfnWndProc   = GuiWndProc;
    wc.hInstance     = (HINSTANCE)GetModuleHandleW(nullptr);
    wc.hCursor       = LoadCursor(nullptr, IDC_ARROW);
    wc.hbrBackground = (HBRUSH)(COLOR_WINDOW + 1);
    wc.lpszClassName = L"ExtIO_RTL_FreqLabels_Class";
    RegisterClassExW(&wc);

    g_hwnd = CreateWindowExW(
        WS_EX_TOOLWINDOW, wc.lpszClassName, L"ExtIO RTL-FreqLabels",
        WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX,
        CW_USEDEFAULT, CW_USEDEFAULT, 420, 120,
        nullptr, nullptr, wc.hInstance, nullptr);

    if (g_hwnd) {
        ShowWindow(g_hwnd, SW_SHOWNOACTIVATE);
        UpdateWindow(g_hwnd);
    }
}

static void refresh_gui_label()
{
    if (g_hwnd && g_hLabel && IsWindow(g_hLabel)) {
        set_label_text(g_hLabel, g_last_label);
    }
}

// ---------- Povinné/štandardné exporty ExtIO ----------

__declspec(dllexport) bool __stdcall InitHW(char* name, char* model, int& type)
{
    copy_str(name,  "RTL-SDR",        32);
    copy_str(model, "RTL-FreqLabels", 64);
    type = 7; // 32-bit float IQ
    return true;
}

__declspec(dllexport) bool __stdcall OpenHW(void)
{
    load_csv_labels();            // ignorujeme chyby – len nezobrazíme názvy
    return true;
}

__declspec(dllexport) void __stdcall CloseHW(void)
{
    if (g_table) { delete[] g_table; g_table = nullptr; }
    g_table_count = 0;
    if (g_hwnd && IsWindow(g_hwnd)) DestroyWindow(g_hwnd);
    g_hwnd = nullptr; g_hLabel = nullptr;
}

__declspec(dllexport) int __stdcall StartHW(long LOfreq)
{
    g_lo = LOfreq;
    g_running = true;
    update_last_label_for(g_lo);
    refresh_gui_label();
    return 1024; // IQ páry (>=512; násobok 512)
}

__declspec(dllexport) void __stdcall StopHW(void)
{
    g_running = false;
}

__declspec(dllexport) void __stdcall SetCallback(ExtIOHostCallback cb)
{
    g_cb = cb;
}

__declspec(dllexport) int __stdcall SetHWLO(long LOfreq)
{
    g_lo = LOfreq;
    update_last_label_for(g_lo);
    refresh_gui_label();
    return 0; // OK
}

__declspec(dllexport) long __stdcall GetHWLO(void)
{
    return g_lo;
}

__declspec(dllexport) long __stdcall GetHWSR(void)
{
    return 2400000; // 2.4 MS/s – dočasné, kým nepridáme reálne RTL-SDR
}

__declspec(dllexport) int __stdcall GetStatus(void)
{
    return g_running ? 0 : 1; // 0 = OK
}

__declspec(dllexport) void __stdcall ShowGUI(void)
{
    ensure_gui();                // vytvorí/ukáže okno
    refresh_gui_label();         // nastaví text podľa posledného lookupu
    if (g_hwnd) ShowWindow(g_hwnd, SW_SHOWNORMAL);
}

__declspec(dllexport) void __stdcall TuneChanged(long freq)
{
    update_last_label_for(freq);
    refresh_gui_label();
}

} // extern "C"
