// The proxy driver object. Forwards every IASIO call to the Line 6 driver under SEH,
// logs the control path, samples the hot path, and turns faults into ASIO errors.
#pragma once

#include <windows.h>

#include "asio/iasio.h"
#include "real_driver.h"
#include "seh_guard.h"

namespace cb {

class AsioProxy final : public IASIO {
public:
    AsioProxy();
    ~AsioProxy();
    AsioProxy(const AsioProxy&) = delete;
    AsioProxy& operator=(const AsioProxy&) = delete;

    // IUnknown
    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID riid, void** ppv) override;
    ULONG STDMETHODCALLTYPE AddRef() override;
    ULONG STDMETHODCALLTYPE Release() override;

    // IASIO
    ASIOBool init(void* sysHandle) override;
    void getDriverName(char* name) override;
    long getDriverVersion() override;
    void getErrorMessage(char* string) override;
    ASIOError start() override;
    ASIOError stop() override;
    ASIOError getChannels(long* numInputChannels, long* numOutputChannels) override;
    ASIOError getLatencies(long* inputLatency, long* outputLatency) override;
    ASIOError getBufferSize(long* minSize, long* maxSize, long* preferredSize, long* granularity) override;
    ASIOError canSampleRate(ASIOSampleRate sampleRate) override;
    ASIOError getSampleRate(ASIOSampleRate* sampleRate) override;
    ASIOError setSampleRate(ASIOSampleRate sampleRate) override;
    ASIOError getClockSources(ASIOClockSource* clocks, long* numSources) override;
    ASIOError setClockSource(long reference) override;
    ASIOError getSamplePosition(ASIOSamples* sPos, ASIOTimeStamp* tStamp) override;
    ASIOError getChannelInfo(ASIOChannelInfo* info) override;
    ASIOError createBuffers(ASIOBufferInfo* bufferInfos, long numChannels, long bufferSize,
                            ASIOCallbacks* callbacks) override;
    ASIOError disposeBuffers() override;
    ASIOError controlPanel() override;
    ASIOError future(long selector, void* opt) override;
    ASIOError outputReady() override;

private:
    bool Ready(const char* what);
    template <class F>
    bool Call(const char* what, F&& f);  // SEH-guarded; on fault sets faulted_ and error_
    template <class F>
    ASIOError Fwd(const char* what, F&& f);  // Ready() + Call(); ASE_NotPresent on refusal/fault
    void LogInitContext(void* sysRef);
    void LogDriverFields(const char* when);

    LONG refs_ = 1;
    HMODULE module_ = nullptr;
    IASIO* real_ = nullptr;
    bool faulted_ = false;
    char error_[124] = {};
    FaultRecord fault_;
    ThreadSnapshot threadsAtCreate_;
    LONG samplePositionCalls_ = 0;
    LONG outputReadyCalls_ = 0;
    LONG refusals_ = 0;
};

}  // namespace cb
