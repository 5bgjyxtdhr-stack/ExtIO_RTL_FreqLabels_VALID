
// ExtIO minimalny skeleton + CSV lookup (bez GUI)
// Rozhranie: Winrad/ExtIO (HDSDR-kompatibilne): extern "C", __stdcall, __declspec(dllexport)

#include <cstdio>
#include <cstring>
#include <cctype>
#include <cstdlib>   // pre ::getenv

// Pomocné maximum dĺžok
#define NAME_MAX  64
#define MODEL_MAX 64

extern "C" {

// ---------- Typy a globálne premenné ----------

typedef void (__stdcall *ExtIOHostCallback)(int, int, float, void*);

static ExtIOHostCallback g_cb = nullptr;
static long  g_lo        = 10000000;         // Hz
static bool  g_running   = false;

// CSV lookup tabuľka: udržiavame si páry (freq_hz -> názov)
struct FreqName { long hz; char name[128]; };
static FreqName* g_table = nullptr;
static int       g_table_count = 0;

// Tolerancia pre zhodu (±10 kHz)
static const long TOL_HZ = 10000;

// Posledný nájdený názov (pre GUI v ďalšom kroku)
static char g_last_label[128] = {0};

// ---------- Pomocné funkcie ----------

static void copy_str(char* dst, const char* src, int maxLen)
{
    if (!dst || !src || maxLen <= 0) return;
    int i = 0;
    for (; i < maxLen - 1 && src[i] != '\0'; ++i) dst[i] = src[i];
    dst[i] = '\0';
}

// Orezanie whitespace z oboch strán (in-place)
static char* trim(char* s)
{
    if (!s) return s;
    while (*s && std::isspace((unsigned char)*s)) ++s;
    if (!*s) return s;
    char* e = s + std::strlen(s) - 1;
    while (e > s && std::isspace((unsigned char)*e)) { *e = '\0'; --e; }
    return s;
}

// Parsovanie CSV (format: freq_hz,name), UTF-8/ASCII
static bool load_csv_labels()
{
    // Zložíme cestu: %USERPROFILE%\Documents\HDSDR\freq_labels.csv
    const char* up = ::getenv("USERPROFILE");  // MSVC: getenv je v global namespace
    if (!up) return false;

    char path[512];
    std::snprintf(path, sizeof(path), "%s\\Documents\\HDSDR\\freq_labels.csv", up);

    FILE* f = std::fopen(path, "rb");
    if (!f) return false;

    // Spočítame riadky (okrem hlavičky)
    const size_t LINE_MAX = 1024;
    char line[LINE_MAX];
    int rows = 0;

    // Preskoč hlavičku (prvý riadok)
    if (!std::fgets(line, (int)LINE_MAX, f)) {
        std::fclose(f);
        return false;
    }
    // Spočítame zvyšné
    while (std::fgets(line, (int)LINE_MAX, f)) {
        if (std::strchr(line, ',')) rows++;
    }
    if (rows <= 0) { std::fclose(f); return false; }

    // Alokácia tabuľky
    g_table = new FreqName[rows];
    g_table_count = 0;

    // Rewind a načítanie dát
    std::rewind(f);
    // preskoč hlavičku
    std::fgets(line, (int)LINE_MAX, f);

    while (std::fgets(line, (int)LINE_MAX, f)) {
        // hľadáme prvú čiarku
        char* comma = std::strchr(line, ',');
        if (!comma) continue;

        *comma = '\0';
        char* fstr = trim(line);
        char* nstr = trim(comma + 1);

        // odstránime koncový CR/LF v názve
        size_t ln = std::strlen(nstr);
        while (ln && (nstr[ln - 1] == '\r' || nstr[ln - 1] == '\n')) { nstr[--ln] = '\0'; }

        // freq_hz -> long
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

// Nájde presnú alebo najbližšiu zhodu v tolerancii ±TOL_HZ
static const char* find_label_for(long hz)
{
    if (!g_table || g_table_count <= 0) return nullptr;

    // 1) presná zhoda
    for (int i = 0; i < g_table_count; ++i) {
        if (g_table[i].hz == hz) return g_table[i].name;
    }
    // 2) ± tolerancia (krok 100 Hz, aby to bolo rýchle a robustné)
    for (long d = 0; d <= TOL_HZ; d += 100) {
        long f1 = hz + d;
        long f2 = hz - d;
        for (int i = 0; i < g_table_count; ++i) {
            if (g_table[i].hz == f1) return g_table[i].name;
            if (g_table[i].hz == f2) return g_table[i].name;
        }
    }
    return nullptr;
}

// Uloží posledný label (pre GUI krok 2)
static void update_last_label_for(long hz)
{
    const char* lab = find_label_for(hz);
    if (lab) copy_str(g_last_label, lab, (int)sizeof(g_last_label));
    else     g_last_label[0] = '\0';
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
    // Načítaj CSV (ak sa nepodarí, nič sa nedeje – len nebudú labely)
    load_csv_labels();
    return true;
}

__declspec(dllexport) void __stdcall CloseHW(void)
{
    if (g_table) { delete[] g_table; g_table = nullptr; }
    g_table_count = 0;
}

__declspec(dllexport) int __stdcall StartHW(long LOfreq)
{
    g_lo = LOfreq;
    g_running = true;
    // pri štarte rovno prepočítame label
    update_last_label_for(g_lo);
    return 1024; // IQ páry na callback (>=512 a násobky 512)
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
    update_last_label_for(g_lo); // CSV lookup (±10 kHz)
    return 0; // OK
}

__declspec(dllexport) long __stdcall GetHWLO(void)
{
    return g_lo;
}

// Dôležité pre HDSDR: vráť samplovaciu rýchlosť v Hz (>= 4000)
__declspec(dllexport) long __stdcall GetHWSR(void)
{
    return 2400000; // dočasne pevná hodnota (2.4 MS/s)
}

__declspec(dllexport) int __stdcall GetStatus(void)
{
    return g_running ? 0 : 1; // 0 = OK
}

__declspec(dllexport) void __stdcall ShowGUI(void)
{
    // Zatiaľ NO-OP: GUI doplníme v ďalšom kroku (statický text s g_last_label)
}

__declspec(dllexport) void __stdcall TuneChanged(long freq)
{
    // Niektoré hosty volajú aj pri zmene TUNE; doplníme lookup aj sem.
    update_last_label_for(freq);
}

} // extern "C"
