// CrashBarrier - minimal ASIO driver interface declarations.
//
// Written from the publicly documented ASIO 2.x host/driver binary interface
// (vtable order, method signatures, struct layout, error and selector values).
// Contains no Steinberg SDK text. Only what the proxy forwards is declared;
// ASIOTime is passed through as an opaque pointer and never dereferenced.
//
// On Windows the 64-bit sample/time values are hi/lo pairs of 32-bit words, not native
// int64 (confirmed against the Stadium driver's getSamplePosition output, 2026-10-27).
#pragma once

#include <objbase.h>

typedef long ASIOBool;
enum { ASIOFalse = 0, ASIOTrue = 1 };

typedef long ASIOError;
enum {
    ASE_OK = 0,
    ASE_SUCCESS = 0x3f4847a0,
    ASE_NotPresent = -1000,
    ASE_HWMalfunction,
    ASE_InvalidParameter,
    ASE_InvalidMode,
    ASE_SPNotAdvancing,
    ASE_NoClock,
    ASE_NoMemory
};

typedef double ASIOSampleRate;

struct ASIOInt64 {
    unsigned long hi;
    unsigned long lo;
    long long value() const { return static_cast<long long>((static_cast<unsigned long long>(hi) << 32) | lo); }
};
typedef ASIOInt64 ASIOSamples;
typedef ASIOInt64 ASIOTimeStamp;

// asioMessage() selectors (host callback).
enum {
    kAsioSelectorSupported = 1,
    kAsioEngineVersion,
    kAsioResetRequest,
    kAsioBufferSizeChange,
    kAsioResyncRequest,
    kAsioLatenciesChanged,
    kAsioSupportsTimeInfo,
    kAsioSupportsTimeCode,
    kAsioMMCCommand,
    kAsioSupportsInputMonitor,
    kAsioSupportsInputGain,
    kAsioSupportsInputMeter,
    kAsioSupportsOutputGain,
    kAsioSupportsOutputMeter,
    kAsioOverload,
    kAsioNumMessageSelectors
};

#pragma pack(push, 4)

struct ASIOClockSource {
    long index;
    long associatedChannel;
    long associatedGroup;
    ASIOBool isCurrentSource;
    char name[32];
};

struct ASIOChannelInfo {
    long channel;
    ASIOBool isInput;
    ASIOBool isActive;
    long channelGroup;
    long type;  // sample type code; opaque to the proxy
    char name[32];
};

struct ASIOBufferInfo {
    ASIOBool isInput;
    long channelNum;
    void* buffers[2];
};

struct ASIOTime;  // opaque pass-through

struct ASIOCallbacks {
    void (*bufferSwitch)(long doubleBufferIndex, ASIOBool directProcess);
    void (*sampleRateDidChange)(ASIOSampleRate sRate);
    long (*asioMessage)(long selector, long value, void* message, double* opt);
    ASIOTime* (*bufferSwitchTimeInfo)(ASIOTime* params, long doubleBufferIndex, ASIOBool directProcess);
};

#pragma pack(pop)

// The driver object. Hosts obtain it via CoCreateInstance(clsid, ..., /*riid=*/clsid, ...)
// and call these as plain C++ virtuals; there is no IID, no marshaling.
struct IASIO : public IUnknown {
    virtual ASIOBool init(void* sysHandle) = 0;
    virtual void getDriverName(char* name) = 0;  // name buffer: 32 chars
    virtual long getDriverVersion() = 0;
    virtual void getErrorMessage(char* string) = 0;  // string buffer: 124 chars
    virtual ASIOError start() = 0;
    virtual ASIOError stop() = 0;
    virtual ASIOError getChannels(long* numInputChannels, long* numOutputChannels) = 0;
    virtual ASIOError getLatencies(long* inputLatency, long* outputLatency) = 0;
    virtual ASIOError getBufferSize(long* minSize, long* maxSize, long* preferredSize, long* granularity) = 0;
    virtual ASIOError canSampleRate(ASIOSampleRate sampleRate) = 0;
    virtual ASIOError getSampleRate(ASIOSampleRate* sampleRate) = 0;
    virtual ASIOError setSampleRate(ASIOSampleRate sampleRate) = 0;
    virtual ASIOError getClockSources(ASIOClockSource* clocks, long* numSources) = 0;
    virtual ASIOError setClockSource(long reference) = 0;
    virtual ASIOError getSamplePosition(ASIOSamples* sPos, ASIOTimeStamp* tStamp) = 0;
    virtual ASIOError getChannelInfo(ASIOChannelInfo* info) = 0;
    virtual ASIOError createBuffers(ASIOBufferInfo* bufferInfos, long numChannels, long bufferSize,
                                    ASIOCallbacks* callbacks) = 0;
    virtual ASIOError disposeBuffers() = 0;
    virtual ASIOError controlPanel() = 0;
    virtual ASIOError future(long selector, void* opt) = 0;
    virtual ASIOError outputReady() = 0;
};
