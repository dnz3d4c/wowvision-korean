/* Stand-in for the next nvdaControllerClient.dll in the chain (Sku's speech
 * history proxy or NV Access's client). Every call is appended to the file
 * named by FAKE_NVDA_LOG; FAKE_NVDA_RUNNING=1 makes it answer like a running
 * NVDA (0), anything else like a missing one (RPC_S_SERVER_UNAVAILABLE). */
#include <windows.h>
#include <stdio.h>

#define NOT_RUNNING 1722UL

static unsigned long answer(void)
{
    wchar_t v[8];
    DWORD n = GetEnvironmentVariableW(L"FAKE_NVDA_RUNNING", v, 8);
    return (n == 1 && v[0] == L'1') ? 0UL : NOT_RUNNING;
}

static void record(const wchar_t *what, const wchar_t *text)
{
    wchar_t path[MAX_PATH];
    FILE *f = NULL;
    if (!GetEnvironmentVariableW(L"FAKE_NVDA_LOG", path, MAX_PATH)) return;
    if (_wfopen_s(&f, path, L"a, ccs=UTF-8") || !f) return;
    fwprintf(f, L"%ls:%ls\n", what, text ? text : L"");
    fclose(f);
}

unsigned long __stdcall nvdaController_testIfRunning(void) { record(L"testIfRunning", NULL); return answer(); }
unsigned long __stdcall nvdaController_speakText(const wchar_t *t) { record(L"speakText", t); return answer(); }
unsigned long __stdcall nvdaController_cancelSpeech(void) { record(L"cancelSpeech", NULL); return answer(); }
unsigned long __stdcall nvdaController_brailleMessage(const wchar_t *t) { record(L"brailleMessage", t); return answer(); }
unsigned long __stdcall nvdaController_speakSsml(const wchar_t *s, int level, int prio, int async)
{
    (void)level; (void)prio; (void)async;
    record(L"speakSsml", s);
    return answer();
}
unsigned long __stdcall nvdaController_setOnSsmlMarkReachedCallback(void *cb) { (void)cb; record(L"setCallback", NULL); return answer(); }
unsigned long __stdcall nvdaController_getProcessId(unsigned long *pid)
{
    record(L"getProcessId", NULL);
    if (pid) *pid = 4242;
    return answer();
}
