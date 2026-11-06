#include "callback_shim.h"

#include <windows.h>

#include "logger.h"
#include "seh_guard.h"

namespace cb::shim {
namespace {

const void* g_owner = nullptr;
ASIOCallbacks* g_host = nullptr;
FaultRecord g_fault;
LONG g_switches = 0;
LONG g_switchesTimeInfo = 0;

// Log the first 8 occurrences, then every 1000th.
bool Sample(LONG& counter, LONG& value) {
    value = InterlockedIncrement(&counter);
    return value <= 8 || value % 1000 == 0;
}

const char* SelectorName(long selector) {
    switch (selector) {
        case kAsioSelectorSupported: return "kAsioSelectorSupported";
        case kAsioEngineVersion: return "kAsioEngineVersion";
        case kAsioResetRequest: return "kAsioResetRequest";
        case kAsioBufferSizeChange: return "kAsioBufferSizeChange";
        case kAsioResyncRequest: return "kAsioResyncRequest";
        case kAsioLatenciesChanged: return "kAsioLatenciesChanged";
        case kAsioSupportsTimeInfo: return "kAsioSupportsTimeInfo";
        case kAsioSupportsTimeCode: return "kAsioSupportsTimeCode";
        case kAsioMMCCommand: return "kAsioMMCCommand";
        case kAsioSupportsInputMonitor: return "kAsioSupportsInputMonitor";
        case kAsioSupportsInputGain: return "kAsioSupportsInputGain";
        case kAsioSupportsInputMeter: return "kAsioSupportsInputMeter";
        case kAsioSupportsOutputGain: return "kAsioSupportsOutputGain";
        case kAsioSupportsOutputMeter: return "kAsioSupportsOutputMeter";
        case kAsioOverload: return "kAsioOverload";
        default: return "?";
    }
}

void BufferSwitch(long index, ASIOBool directProcess) {
    LONG n;
    if (Sample(g_switches, n)) log::Line("HOST:bufferSwitch #%ld (index=%ld direct=%ld)", n, index, directProcess);
    ASIOCallbacks* host = g_host;
    if (!host) return;
    Guarded("HOST:bufferSwitch", &g_fault, [&] { host->bufferSwitch(index, directProcess); });
}

void SampleRateDidChange(ASIOSampleRate rate) {
    log::Line("HOST:sampleRateDidChange(%.1f)", rate);
    ASIOCallbacks* host = g_host;
    if (!host) return;
    Guarded("HOST:sampleRateDidChange", &g_fault, [&] { host->sampleRateDidChange(rate); });
}

long AsioMessage(long selector, long value, void* message, double* opt) {
    ASIOCallbacks* host = g_host;
    long r = 0;
    if (host) Guarded("HOST:asioMessage", &g_fault, [&] { r = host->asioMessage(selector, value, message, opt); });
    log::Line("HOST:asioMessage(%s=%ld, value=%ld, message=%p, opt=%p) -> %ld%s", SelectorName(selector), selector,
              value, message, static_cast<void*>(opt), r, host ? "" : " [no host callbacks]");
    return r;
}

ASIOTime* BufferSwitchTimeInfo(ASIOTime* params, long index, ASIOBool directProcess) {
    LONG n;
    if (Sample(g_switchesTimeInfo, n)) {
        log::Line("HOST:bufferSwitchTimeInfo #%ld (params=%p index=%ld direct=%ld)", n, static_cast<void*>(params),
                  index, directProcess);
    }
    ASIOCallbacks* host = g_host;
    if (!host) return nullptr;
    ASIOTime* r = nullptr;
    Guarded("HOST:bufferSwitchTimeInfo", &g_fault,
            [&] { r = host->bufferSwitchTimeInfo(params, index, directProcess); });
    return r;
}

ASIOCallbacks g_shim = {&BufferSwitch, &SampleRateDidChange, &AsioMessage, &BufferSwitchTimeInfo};

}  // namespace

ASIOCallbacks* Acquire(const void* owner, ASIOCallbacks* host) {
    if (g_owner && g_owner != owner) {
        log::Line("callback shim: refused, already owned by proxy %p", g_owner);
        return nullptr;
    }
    g_owner = owner;
    g_host = host;
    g_switches = 0;
    g_switchesTimeInfo = 0;
    log::Line("callback shim: host callbacks %p {bufferSwitch=%p sampleRateDidChange=%p asioMessage=%p "
              "bufferSwitchTimeInfo=%p}",
              static_cast<void*>(host), reinterpret_cast<void*>(host->bufferSwitch),
              reinterpret_cast<void*>(host->sampleRateDidChange), reinterpret_cast<void*>(host->asioMessage),
              reinterpret_cast<void*>(host->bufferSwitchTimeInfo));
    return &g_shim;
}

void Release(const void* owner) {
    if (g_owner != owner) return;
    log::Line("callback shim: released (bufferSwitch=%ld bufferSwitchTimeInfo=%ld calls)", g_switches,
              g_switchesTimeInfo);
    g_owner = nullptr;
    g_host = nullptr;
}

}  // namespace cb::shim
