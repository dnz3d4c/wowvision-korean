/* Tests for the Sense Reader proxy. The proxy source is compiled into this
 * exe (KO_TEST), so the Sense Reader side can be swapped for recorders and
 * the routing checked without a screen reader. The NVDA side is the real
 * DLL loading path: fake_next.dll sits next to the exe as
 * nvdaControllerClient.next.dll.
 *
 *   test_bridge.exe              offline routing tests (exit code = failures)
 *   test_bridge.exe live-running prints 1 when Sense Reader is found
 *   test_bridge.exe live-speak T speaks T through the real Sense Reader path
 *   test_bridge.exe live-stop    stops Sense Reader through the real path
 */
#define KO_TEST 1
#include "../koSenseReader.c"

#include <stdio.h>

static int failures;
static void check(const char *label, int ok)
{
    printf("%s %s\n", ok ? "PASS" : "FAIL", label);
    if (!ok) failures++;
}

/* --- Sense Reader recorders ---------------------------------------------- */
static int fake_sr_up;
static int sr_speak_calls, sr_stop_calls;
static wchar_t sr_last[256];

static int rec_running(void) { return fake_sr_up; }
static int rec_speak(const wchar_t *t)
{
    sr_speak_calls++;
    wcsncpy_s(sr_last, 256, t, _TRUNCATE);
    return fake_sr_up;
}
static int rec_stop(void) { sr_stop_calls++; return fake_sr_up; }

static wchar_t log_path[MAX_PATH];

static void scenario(int nvda_up, int sr_up)
{
    SetEnvironmentVariableW(L"FAKE_NVDA_RUNNING", nvda_up ? L"1" : L"0");
    fake_sr_up = sr_up;
    sr_speak_calls = sr_stop_calls = 0;
    sr_last[0] = 0;
    DeleteFileW(log_path);
}

static int log_has(const wchar_t *line)
{
    FILE *f = NULL;
    wchar_t buf[512];
    int found = 0;
    if (_wfopen_s(&f, log_path, L"r, ccs=UTF-8") || !f) return 0;
    while (fgetws(buf, 512, f)) {
        buf[wcscspn(buf, L"\r\n")] = 0;
        if (wcscmp(buf, line) == 0) { found = 1; break; }
    }
    fclose(f);
    return found;
}

static void test_strip_ssml(void)
{
    wchar_t out[128];
    ko_strip_ssml(L"<speak>a &amp; b<break time=\"1s\"/>c &lt;3&gt; &quot;q&quot; &apos;</speak>", out, 128);
    check("ssml: tags dropped, entities decoded", wcscmp(out, L"a & b c <3> \"q\" '") == 0);
    ko_strip_ssml(L"plain", out, 128);
    check("ssml: plain text kept", wcscmp(out, L"plain") == 0);
    ko_strip_ssml(L"&unknown; x", out, 128);
    check("ssml: unknown entity kept as written", wcscmp(out, L"&unknown; x") == 0);
    ko_strip_ssml(L"0123456789", out, 4);
    check("ssml: output truncated to the buffer", wcscmp(out, L"012") == 0);
}

static void test_routing(void)
{
    scenario(1, 1);
    check("NVDA running: speakText goes to NVDA", nvdaController_speakText(L"hi") == 0 && log_has(L"speakText:hi"));
    check("NVDA running: Sense Reader not used", sr_speak_calls == 0);

    scenario(0, 1);
    check("NVDA missing, Sense Reader running: speakText succeeds", nvdaController_speakText(L"안녕") == 0);
    check("NVDA missing: text reaches Sense Reader", sr_speak_calls == 1 && wcscmp(sr_last, L"안녕") == 0);
    check("NVDA missing: NVDA side still offered the text first", log_has(L"speakText:안녕"));

    scenario(0, 0);
    check("neither running: speakText returns NVDA's error", nvdaController_speakText(L"x") == 1722UL);
    check("neither running: Sense Reader not called", sr_speak_calls == 0);

    scenario(1, 1);
    check("NVDA running: cancel goes to NVDA", nvdaController_cancelSpeech() == 0 && log_has(L"cancelSpeech:"));
    check("NVDA running: Sense Reader not stopped", sr_stop_calls == 0);
    scenario(0, 1);
    check("Sense Reader: cancel stops it", nvdaController_cancelSpeech() == 0 && sr_stop_calls == 1);
    scenario(0, 0);
    check("neither running: cancel returns NVDA's error", nvdaController_cancelSpeech() == 1722UL && sr_stop_calls == 0);

    scenario(1, 0);
    check("testIfRunning: NVDA running", nvdaController_testIfRunning() == 0);
    scenario(0, 1);
    check("testIfRunning: Sense Reader running counts as running", nvdaController_testIfRunning() == 0);
    scenario(0, 0);
    check("testIfRunning: neither running", nvdaController_testIfRunning() == 1722UL);

    scenario(0, 1);
    check("speakSsml to Sense Reader as plain text",
          nvdaController_speakSsml(L"<speak>x &amp; y</speak>", -1, 1, 1) == 0 && wcscmp(sr_last, L"x & y") == 0);
    scenario(1, 1);
    check("speakSsml with NVDA running stays SSML for NVDA",
          nvdaController_speakSsml(L"<speak>z</speak>", -1, 1, 1) == 0 && log_has(L"speakSsml:<speak>z</speak>") && sr_speak_calls == 0);

    scenario(1, 0);
    {
        unsigned long pid = 0;
        check("getProcessId forwarded", nvdaController_getProcessId(&pid) == 0 && pid == 4242);
    }
    check("brailleMessage forwarded", nvdaController_brailleMessage(L"b") == 0 && log_has(L"brailleMessage:b"));
    check("callback registration forwarded", nvdaController_setOnSsmlMarkReachedCallback(NULL) == 0 && log_has(L"setCallback:"));
}

static void test_missing_next(void)
{
    next_reset(L"no-such-next.dll");
    scenario(0, 1);
    check("no next DLL: Sense Reader still speaks", nvdaController_speakText(L"solo") == 0 && sr_speak_calls == 1);
    scenario(0, 0);
    check("no next DLL, no Sense Reader: an error, not a crash", nvdaController_speakText(L"solo") != 0);
    next_reset(KO_NEXT_NAME);
}

int wmain(int argc, wchar_t **argv)
{
    if (argc >= 2 && wcscmp(argv[1], L"live-running") == 0) {
        printf("%d\n", sr_running_real());
        return 0;
    }
    if (argc >= 3 && wcscmp(argv[1], L"live-speak") == 0) return sr_speak_real(argv[2]) ? 0 : 1;
    if (argc >= 2 && wcscmp(argv[1], L"live-stop") == 0) return sr_stop_real() ? 0 : 1;

    GetTempPathW(MAX_PATH, log_path);
    wcscat_s(log_path, MAX_PATH, L"ko-sr-bridge-test.log");
    SetEnvironmentVariableW(L"FAKE_NVDA_LOG", log_path);
    g_sr_running = rec_running;
    g_sr_speak = rec_speak;
    g_sr_stop = rec_stop;

    test_strip_ssml();
    test_routing();
    test_missing_next();
    DeleteFileW(log_path);
    printf(failures ? "%d FAILED\n" : "ALL PASS\n", failures);
    return failures;
}
