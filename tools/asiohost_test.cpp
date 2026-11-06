// asiohost_test - a minimal ASIO host for smoke-testing CrashBarrier without a DAW.
//
//   asiohost_test                      CoCreateInstance the registered proxy, run a full cycle
//   asiohost_test --dll <path>         load a driver DLL directly (no registration needed)
//   asiohost_test --real               target the Line 6 CLSID instead of the proxy (golden run)
//   asiohost_test --driver <path>      make the proxy load this DLL instead of the Line 6 driver
//                                      (a nonexistent path tests the load-failure path,
//                                       FaultyDriver_x64.dll tests the SEH fault path)
//   asiohost_test --sysref null        pass NULL instead of the console window to init()
//   asiohost_test --sysref desktop     pass GetDesktopWindow(), as Rocksmith+ does
//   asiohost_test --priority N         SetThreadPriority(N) for the calling thread (game: 15 at launch, 2 later)
//   asiohost_test --seconds N          stream for N seconds (default 2)
//   asiohost_test --inputs N --outputs M   channels to create buffers for (default 2 and 2; max 8 each)
//                                      Rocksmith+ uses --inputs 0 --outputs 8 --frames 384 at launch.
//   asiohost_test --frames N           buffer size instead of the driver's preferred size
//   asiohost_test --probes N           first create N instances that only init+query+Release (the
//                                      game's enumeration), then --cycles M full connect cycles,
//                                      each on a fresh instance. Default 0 probes, 1 cycle.
//
// Exit codes: 0 full cycle ok, 2 init() returned ASIOFalse, 1 anything else.
#include <windows.h>

#include <objbase.h>

#include <climits>
#include <cstdio>
#include <cstring>

#include "asio/iasio.h"
#include "constants.h"

namespace {

LONG g_bufferSwitch = 0;
LONG g_bufferSwitchTimeInfo = 0;

void BufferSwitch(long, ASIOBool) { InterlockedIncrement(&g_bufferSwitch); }
void SampleRateDidChange(ASIOSampleRate rate) { printf("  cb sampleRateDidChange(%.1f)\n", rate); }
long AsioMessage(long selector, long value, void*, double*) {
    switch (selector) {
        case kAsioSelectorSupported:
            return (value == kAsioEngineVersion || value == kAsioResetRequest || value == kAsioBufferSizeChange ||
                    value == kAsioResyncRequest || value == kAsioLatenciesChanged || value == kAsioSupportsTimeInfo)
                       ? 1
                       : 0;
        case kAsioEngineVersion: return 2;
        case kAsioSupportsTimeInfo: return 1;
        case kAsioResetRequest:
        case kAsioResyncRequest:
        case kAsioLatenciesChanged:
        case kAsioBufferSizeChange: printf("  cb asioMessage(%ld, %ld)\n", selector, value); return 1;
        default: return 0;
    }
}
ASIOTime* BufferSwitchTimeInfo(ASIOTime* params, long, ASIOBool) {
    InterlockedIncrement(&g_bufferSwitchTimeInfo);
    return params;
}

IASIO* Create(const wchar_t* dll, const CLSID& clsid) {
    IASIO* asio = nullptr;
    if (!dll) {
        const HRESULT hr = CoCreateInstance(clsid, nullptr, CLSCTX_INPROC_SERVER, clsid, reinterpret_cast<void**>(&asio));
        printf("CoCreateInstance -> 0x%08lX\n", hr);
        return asio;
    }
    static HMODULE m = LoadLibraryW(dll);
    if (!m) {
        printf("LoadLibraryW failed: %lu\n", GetLastError());
        return nullptr;
    }
    using GetClassObjectFn = HRESULT(STDAPICALLTYPE*)(REFCLSID, REFIID, void**);
    const auto gco = reinterpret_cast<GetClassObjectFn>(GetProcAddress(m, "DllGetClassObject"));
    if (!gco) {
        printf("no DllGetClassObject export\n");
        return nullptr;
    }
    IClassFactory* f = nullptr;
    HRESULT hr = gco(clsid, IID_IClassFactory, reinterpret_cast<void**>(&f));
    if (FAILED(hr)) {
        printf("DllGetClassObject -> 0x%08lX\n", hr);
        return nullptr;
    }
    hr = f->CreateInstance(nullptr, clsid, reinterpret_cast<void**>(&asio));
    f->Release();
    printf("IClassFactory::CreateInstance -> 0x%08lX\n", hr);
    return SUCCEEDED(hr) ? asio : nullptr;
}

// The game's enumeration-time probe: init, identity, capabilities, Release.
int Probe(IASIO* asio, void* sysRef) {
    const ASIOBool ok = asio->init(sysRef);
    char name[128] = {};
    asio->getDriverName(name);
    long in = 0, out = 0, mn = 0, mx = 0, pref = 0, gran = 0;
    asio->getChannels(&in, &out);
    asio->getBufferSize(&mn, &mx, &pref, &gran);
    asio->outputReady();
    asio->canSampleRate(48000.0);
    printf("probe: init -> %ld '%s' in=%ld out=%ld preferred=%ld; Release -> %lu\n", ok, name, in, out, pref,
           asio->Release());
    return ok == ASIOTrue ? 0 : 2;
}

int RunCycle(const wchar_t* dll, const CLSID& clsid, void* sysRef, long wantIn, long wantOut, long frames,
             int seconds);

}  // namespace

int wmain(int argc, wchar_t** argv) {
    const wchar_t* dll = nullptr;
    CLSID clsid = cb::CLSID_CrashBarrier;
    bool nullSysRef = false, desktopSysRef = false;
    int seconds = 2, probes = 0, cycles = 1, priority = INT_MIN;
    long wantIn = 2, wantOut = 2, frames = 0;
    for (int i = 1; i < argc; ++i) {
        if (!wcscmp(argv[i], L"--dll") && i + 1 < argc) dll = argv[++i];
        else if (!wcscmp(argv[i], L"--priority") && i + 1 < argc) priority = _wtoi(argv[++i]);
        else if (!wcscmp(argv[i], L"--probes") && i + 1 < argc) probes = _wtoi(argv[++i]);
        else if (!wcscmp(argv[i], L"--cycles") && i + 1 < argc) cycles = _wtoi(argv[++i]);
        else if (!wcscmp(argv[i], L"--inputs") && i + 1 < argc) wantIn = _wtol(argv[++i]);
        else if (!wcscmp(argv[i], L"--outputs") && i + 1 < argc) wantOut = _wtol(argv[++i]);
        else if (!wcscmp(argv[i], L"--frames") && i + 1 < argc) frames = _wtol(argv[++i]);
        else if (!wcscmp(argv[i], L"--real")) clsid = cb::CLSID_Stadium;
        else if (!wcscmp(argv[i], L"--driver") && i + 1 < argc) SetEnvironmentVariableW(cb::kDriverPathEnv, argv[++i]);
        else if (!wcscmp(argv[i], L"--sysref") && i + 1 < argc) {
            ++i;
            nullSysRef = !wcscmp(argv[i], L"null");
            desktopSysRef = !wcscmp(argv[i], L"desktop");
        }
        else if (!wcscmp(argv[i], L"--seconds") && i + 1 < argc) seconds = _wtoi(argv[++i]);
        else {
            printf("unknown argument: %ls\n", argv[i]);
            return 1;
        }
    }

    const HRESULT hr = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    printf("CoInitializeEx -> 0x%08lX (pid %lu tid %lu)\n", hr, GetCurrentProcessId(), GetCurrentThreadId());
    if (priority != INT_MIN) {
        printf("SetThreadPriority(%d) -> %d, now %d\n", priority, SetThreadPriority(GetCurrentThread(), priority),
               GetThreadPriority(GetCurrentThread()));
    }

    void* sysRef = nullptr;
    if (desktopSysRef) {
        sysRef = GetDesktopWindow();
        printf("using the desktop window %p as sysRef\n", sysRef);
    } else if (!nullSysRef) {
        sysRef = GetConsoleWindow();
        if (!sysRef) {  // no console window (e.g. Windows Terminal): make a hidden one on this thread
            sysRef = CreateWindowExW(0, L"STATIC", L"asiohost_test", WS_OVERLAPPED, 0, 0, 10, 10, nullptr, nullptr,
                                     nullptr, nullptr);
            printf("created hidden window %p as sysRef\n", sysRef);
        }
    }

    for (int p = 0; p < probes; ++p) {
        IASIO* probe = Create(dll, clsid);
        if (!probe) return 1;
        if (Probe(probe, sysRef) != 0) return 2;
    }

    int exitCode = 0;
    for (int cycle = 0; cycle < cycles && exitCode == 0; ++cycle) {
        printf("--- cycle %d\n", cycle + 1);
        exitCode = RunCycle(dll, clsid, sysRef, wantIn, wantOut, frames, seconds);
    }
    CoUninitialize();
    return exitCode;
}

namespace {

int RunCycle(const wchar_t* dll, const CLSID& clsid, void* sysRef, long wantIn, long wantOut, long frames,
             int seconds) {
    IASIO* asio = Create(dll, clsid);
    if (!asio) return 1;
    g_bufferSwitch = g_bufferSwitchTimeInfo = 0;
    const ASIOBool ok = asio->init(sysRef);
    char text[128] = {};
    asio->getDriverName(text);
    printf("init(%p) -> %ld   driver '%s' version %ld\n", sysRef, ok, text, asio->getDriverVersion());
    if (ok != ASIOTrue) {
        asio->getErrorMessage(text);
        printf("error message: '%s'\n", text);
        asio->Release();
        return 2;
    }

    long in = 0, out = 0;
    ASIOError r = asio->getChannels(&in, &out);
    printf("getChannels -> %ld (in=%ld out=%ld)\n", r, in, out);
    long minSize = 0, maxSize = 0, preferred = 0, granularity = 0;
    r = asio->getBufferSize(&minSize, &maxSize, &preferred, &granularity);
    printf("getBufferSize -> %ld (min=%ld max=%ld preferred=%ld gran=%ld)\n", r, minSize, maxSize, preferred,
           granularity);
    ASIOSampleRate rate = 0;
    r = asio->getSampleRate(&rate);
    printf("getSampleRate -> %ld (%.1f)\n", r, rate);

    ASIOBufferInfo infos[16] = {};
    long count = 0;
    for (long c = 0; c < in && c < wantIn && c < 8; ++c) infos[count++] = {ASIOTrue, c, {nullptr, nullptr}};
    for (long c = 0; c < out && c < wantOut && c < 8; ++c) infos[count++] = {ASIOFalse, c, {nullptr, nullptr}};
    if (frames > 0) preferred = frames;
    for (long i = 0; i < count; ++i) {
        ASIOChannelInfo ci = {};
        ci.channel = infos[i].channelNum;
        ci.isInput = infos[i].isInput;
        r = asio->getChannelInfo(&ci);
        printf("getChannelInfo(%ld,%s) -> %ld name='%.32s' type=%ld\n", ci.channel, ci.isInput ? "in" : "out", r,
               ci.name, ci.type);
    }

    ASIOCallbacks callbacks = {&BufferSwitch, &SampleRateDidChange, &AsioMessage, &BufferSwitchTimeInfo};
    r = asio->createBuffers(infos, count, preferred, &callbacks);
    printf("createBuffers(%ld channels, %ld frames) -> %ld\n", count, preferred, r);
    int exitCode = 0;
    if (r == ASE_OK) {
        long latIn = 0, latOut = 0;
        r = asio->getLatencies(&latIn, &latOut);
        printf("getLatencies -> %ld (in=%ld out=%ld)\n", r, latIn, latOut);
        r = asio->start();
        printf("start -> %ld, streaming %d s...\n", r, seconds);
        if (r == ASE_OK) {
            Sleep(static_cast<DWORD>(seconds) * 1000);
            ASIOSamples pos = {};
            ASIOTimeStamp ts = {};
            r = asio->getSamplePosition(&pos, &ts);
            printf("getSamplePosition -> %ld (pos=%lld ts=%lld)\n", r, pos.value(), ts.value());
            printf("stop -> %ld\n", asio->stop());
        } else {
            exitCode = 1;
        }
        printf("callbacks: bufferSwitch=%ld bufferSwitchTimeInfo=%ld\n", g_bufferSwitch, g_bufferSwitchTimeInfo);
        if (g_bufferSwitch + g_bufferSwitchTimeInfo == 0) exitCode = 1;
        printf("disposeBuffers -> %ld\n", asio->disposeBuffers());
    } else {
        exitCode = 1;
    }
    printf("Release -> %lu\n", asio->Release());
    return exitCode;
}

}  // namespace
