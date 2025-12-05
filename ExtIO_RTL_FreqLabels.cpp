// Minimal valid ExtIO (Winrad/HDSDR) skeleton without GUI calls
// Exact prototypes per Winrad ExtIO spec; exports provided via .def to ensure undecorated names.

extern "C" {

// Callback prototype from spec: void (*Callback)(int cnt, int status, float IQoffs, void* IQdata)
typedef void (*ExtIOHostCallback)(int, int, float, void*);

static ExtIOHostCallback g_cb = 0;
static long g_lo = 10000000; // Hz
static bool g_running = false;

static void copy_str(char* dst, const char* src, int maxLen)
{
    if (!dst || !src || maxLen <= 0) return;
    int i=0; for (; i<maxLen-1 && src[i]; ++i) dst[i]=src[i]; dst[i]='\0';
}

__declspec(dllexport) bool __stdcall InitHW(char* name, char* model, int& type)
{
    // Short names recommended by spec (will appear in HDSDR menu)
    copy_str(name,  "RTL-SDR",        32);
    copy_str(model, "RTL-FreqLabels", 64);
    type = 7; // 32-bit float IQ
    return true;
}

__declspec(dllexport) bool __stdcall OpenHW(void)
{
    return true;
}

__declspec(dllexport) void __stdcall CloseHW(void)
{
}

__declspec(dllexport) int __stdcall StartHW(long freq)
{
    g_lo = freq;
    g_running = true;
    // Return IQ pairs per callback block (>=512 and multiple of 512)
    return 1024;
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
    return 0; // OK
}

__declspec(dllexport) long __stdcall GetHWLO(void)
{
    return g_lo;
}

__declspec(dllexport) int __stdcall GetStatus(void)
{
    return g_running ? 0 : 1; // 0=OK
}

__declspec(dllexport) void __stdcall ShowGUI(void)
{
    // no-op (no User32 dependency), GUI will be added later
}

__declspec(dllexport) void __stdcall TuneChanged(long /*freq*/)
{
    // no-op for now
}

} // extern "C"
