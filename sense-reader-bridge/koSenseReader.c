/* WowVision Korean build: Sense Reader proxy for the SAPI2SR bridge.
 *
 * The SAPI2SR engine speaks through the nvdaControllerClient.dll next to it.
 * The Korean installer puts this DLL there and moves the DLL it replaces
 * (Sku's speech history proxy, or NV Access's client) to
 * nvdaControllerClient.next.dll. Every call goes to that next DLL first, so
 * NVDA users get exactly what they had. Only when the next DLL reports an
 * error (NVDA is not running) and Sense Reader is running does the call go to
 * Sense Reader's automation object, SenseReader.Application:
 *
 *   speakText / speakSsml  -> Speak(text)    queued like NVDA's speakText
 *   cancelSpeech           -> StopSpeaking() also empties the queue
 *   testIfRunning          -> 0 while Sense Reader runs
 *
 * So the engine's "skuint" interrupt (cancel, then speak) and its volume-0
 * skip work the same with Sense Reader. Sense Reader is looked up by its
 * XVSRD window owned by xvsrd.exe before any COM call: creating the object
 * while Sense Reader is closed would start it.
 *
 * Marker for the installer (KoreanSenseReader.cs), which uses it to tell this
 * DLL from the one it replaces: */
static const char ko_marker[] = "WowVisionKoreanSenseReaderProxy/1";

#include <windows.h>
#include <ole2.h>
#include <wchar.h>

#define KO_NEXT_NAME L"nvdaControllerClient.next.dll"
#define KO_SR_WINDOW_CLASS L"XVSRD"
#define KO_SR_EXE L"xvsrd.exe"
#define KO_SR_PROGID L"SenseReader.Application"
#define KO_NOT_RUNNING 1722UL /* RPC_S_SERVER_UNAVAILABLE, what NVDA's client returns */

typedef unsigned long(__stdcall *fn_void)(void);
typedef unsigned long(__stdcall *fn_text)(const wchar_t *);
typedef unsigned long(__stdcall *fn_ssml)(const wchar_t *, int, int, int);
typedef unsigned long(__stdcall *fn_ptr)(void *);
typedef unsigned long(__stdcall *fn_pid)(unsigned long *);

/* ---------------------------------------------------------------- next DLL */

static HMODULE g_next;
static LONG g_next_tried;
static const wchar_t *g_next_name = KO_NEXT_NAME;

static HMODULE next_module(void)
{
    wchar_t path[MAX_PATH];
    HMODULE self = NULL;
    HMODULE m;
    wchar_t *slash;
    if (InterlockedCompareExchange(&g_next_tried, 1, 0) != 0) return g_next;
    if (!GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                            (LPCWSTR)&next_module, &self))
        return NULL;
    if (!GetModuleFileNameW(self, path, MAX_PATH)) return NULL;
    slash = wcsrchr(path, L'\\');
    if (!slash) return NULL;
    slash[1] = 0;
    if (wcscat_s(path, MAX_PATH, g_next_name) != 0) return NULL;
    m = LoadLibraryW(path);
    g_next = m;
    return m;
}

static FARPROC next_proc(const char *name)
{
    HMODULE m = next_module();
    return m ? GetProcAddress(m, name) : NULL;
}

#ifdef KO_TEST
static void next_reset(const wchar_t *name)
{
    if (g_next) FreeLibrary(g_next);
    g_next = NULL;
    g_next_tried = 0;
    g_next_name = name;
}
#endif

/* ----------------------------------------------------------- Sense Reader */

static int sr_running_real(void)
{
    wchar_t image[MAX_PATH];
    DWORD len = MAX_PATH, pid = 0;
    size_t n, exe = wcslen(KO_SR_EXE);
    HANDLE proc;
    int ok;
    HWND w = FindWindowW(KO_SR_WINDOW_CLASS, NULL);
    if (!w || !GetWindowThreadProcessId(w, &pid) || !pid) return 0;
    proc = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pid);
    if (!proc) return 0;
    ok = QueryFullProcessImageNameW(proc, 0, image, &len);
    CloseHandle(proc);
    if (!ok) return 0;
    n = wcslen(image);
    return n > exe && image[n - exe - 1] == L'\\' && _wcsicmp(image + n - exe, KO_SR_EXE) == 0;
}

/* Calls a method of SenseReader.Application with no argument or one string. */
static int sr_call(const wchar_t *method, const wchar_t *arg)
{
    HRESULT init;
    int ok = 0;
    if (!sr_running_real()) return 0;
    init = CoInitializeEx(NULL, COINIT_MULTITHREADED);
    if (SUCCEEDED(init) || init == RPC_E_CHANGED_MODE) {
        CLSID clsid;
        IDispatch *app = NULL;
        if (SUCCEEDED(CLSIDFromProgID(KO_SR_PROGID, &clsid)) &&
            SUCCEEDED(CoCreateInstance(&clsid, NULL, CLSCTX_LOCAL_SERVER, &IID_IDispatch, (void **)&app))) {
            DISPID id;
            LPOLESTR name = (LPOLESTR)method;
            if (SUCCEEDED(app->lpVtbl->GetIDsOfNames(app, &IID_NULL, &name, 1, LOCALE_USER_DEFAULT, &id))) {
                VARIANT v;
                DISPPARAMS params = {0};
                VariantInit(&v);
                if (arg) {
                    v.vt = VT_BSTR;
                    v.bstrVal = SysAllocString(arg);
                    params.rgvarg = &v;
                    params.cArgs = 1;
                }
                if (!arg || v.bstrVal)
                    ok = SUCCEEDED(app->lpVtbl->Invoke(app, id, &IID_NULL, LOCALE_USER_DEFAULT, DISPATCH_METHOD,
                                                       &params, NULL, NULL, NULL));
                VariantClear(&v);
            }
            app->lpVtbl->Release(app);
        }
        if (SUCCEEDED(init)) CoUninitialize();
    }
    return ok;
}

static int sr_speak_real(const wchar_t *text)
{
    if (!text || !*text) return sr_running_real();
    return sr_call(L"Speak", text);
}

static int sr_stop_real(void) { return sr_call(L"StopSpeaking", NULL); }

static int (*g_sr_running)(void) = sr_running_real;
static int (*g_sr_speak)(const wchar_t *) = sr_speak_real;
static int (*g_sr_stop)(void) = sr_stop_real;

/* ------------------------------------------------------------------- SSML */

static int ko_starts(const wchar_t *s, const wchar_t *prefix)
{
    return wcsncmp(s, prefix, wcslen(prefix)) == 0;
}

/* SSML to the plain text Sense Reader reads: tags dropped (break, p and s
 * leave a space), the five XML entities decoded, others left as written. */
void ko_strip_ssml(const wchar_t *in, wchar_t *out, size_t cap)
{
    static const struct { const wchar_t *name; wchar_t ch; } ents[] = {
        {L"&amp;", L'&'}, {L"&lt;", L'<'}, {L"&gt;", L'>'}, {L"&quot;", L'"'}, {L"&apos;", L'\''}};
    size_t o = 0, i;
    if (!cap) return;
    while (in && *in && o + 1 < cap) {
        if (*in == L'<') {
            const wchar_t *tag = in + 1;
            const wchar_t *end = wcschr(in, L'>');
            if (*tag == L'/') tag++;
            if ((ko_starts(tag, L"break") || ko_starts(tag, L"p>") || ko_starts(tag, L"p ") ||
                 ko_starts(tag, L"s>") || ko_starts(tag, L"s ")) &&
                o > 0 && out[o - 1] != L' ')
                out[o++] = L' ';
            in = end ? end + 1 : in + wcslen(in);
            continue;
        }
        if (*in == L'&') {
            int hit = 0;
            for (i = 0; i < sizeof ents / sizeof ents[0]; i++) {
                if (ko_starts(in, ents[i].name)) {
                    out[o++] = ents[i].ch;
                    in += wcslen(ents[i].name);
                    hit = 1;
                    break;
                }
            }
            if (hit) continue;
        }
        out[o++] = *in++;
    }
    while (o > 0 && out[o - 1] == L' ') o--;
    out[o] = 0;
}

/* ---------------------------------------------------------------- exports */

unsigned long __stdcall nvdaController_testIfRunning(void)
{
    fn_void f = (fn_void)next_proc("nvdaController_testIfRunning");
    unsigned long r = f ? f() : KO_NOT_RUNNING;
    if (r == 0) return 0;
    return g_sr_running() ? 0 : r;
}

unsigned long __stdcall nvdaController_speakText(const wchar_t *text)
{
    fn_text f = (fn_text)next_proc("nvdaController_speakText");
    unsigned long r = f ? f(text) : KO_NOT_RUNNING;
    if (r == 0) return 0;
    return g_sr_running() && g_sr_speak(text) ? 0 : r;
}

unsigned long __stdcall nvdaController_cancelSpeech(void)
{
    fn_void f = (fn_void)next_proc("nvdaController_cancelSpeech");
    unsigned long r = f ? f() : KO_NOT_RUNNING;
    if (r == 0) return 0;
    return g_sr_running() && g_sr_stop() ? 0 : r;
}

unsigned long __stdcall nvdaController_speakSsml(const wchar_t *ssml, int symbolLevel, int priority, int asynchronous)
{
    fn_ssml f = (fn_ssml)next_proc("nvdaController_speakSsml");
    unsigned long r = f ? f(ssml, symbolLevel, priority, asynchronous) : KO_NOT_RUNNING;
    size_t cap;
    wchar_t *plain;
    int ok;
    if (r == 0) return 0;
    if (!ssml || !g_sr_running()) return r;
    cap = wcslen(ssml) + 1;
    plain = (wchar_t *)HeapAlloc(GetProcessHeap(), 0, cap * sizeof(wchar_t));
    if (!plain) return r;
    ko_strip_ssml(ssml, plain, cap);
    ok = g_sr_speak(plain);
    HeapFree(GetProcessHeap(), 0, plain);
    return ok ? 0 : r;
}

/* Braille and the rest have no Sense Reader counterpart: forwarded only. */
unsigned long __stdcall nvdaController_brailleMessage(const wchar_t *text)
{
    fn_text f = (fn_text)next_proc("nvdaController_brailleMessage");
    return f ? f(text) : KO_NOT_RUNNING;
}

unsigned long __stdcall nvdaController_setOnSsmlMarkReachedCallback(void *callback)
{
    fn_ptr f = (fn_ptr)next_proc("nvdaController_setOnSsmlMarkReachedCallback");
    return f ? f(callback) : KO_NOT_RUNNING;
}

unsigned long __stdcall nvdaController_getProcessId(unsigned long *pid)
{
    fn_pid f = (fn_pid)next_proc("nvdaController_getProcessId");
    return f ? f(pid) : KO_NOT_RUNNING;
}

BOOL WINAPI DllMain(HINSTANCE inst, DWORD reason, LPVOID reserved)
{
    (void)reserved;
    if (reason == DLL_PROCESS_ATTACH) {
        DisableThreadLibraryCalls(inst);
        /* keep the marker in the image */
        if (ko_marker[0] == 0) OutputDebugStringA(ko_marker);
    }
    return TRUE;
}
