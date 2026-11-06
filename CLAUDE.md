# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## What this is

A Windows x64 ASIO proxy driver (`CrashBarrier_x64.dll`) that sits between an ASIO host and the Line 6
Helix Stadium driver (`StadiumAsio_OnInterposer3_64.dll`, v0.0.3.15). It exists to diagnose and work
around a driver crash that killed Rocksmith+ (Line 6 ticket #502946). Read `docs/DIAGNOSIS.md` before
touching the remediation: the root cause is known and reproduced (driver faults on its own stream
thread when `createBuffers` has zero output channels), and the shim is one deliberate padding step in
`AsioProxy::createBuffers`. `PROJECT_BRIEF.md` is the original plan; some of its premises were later
shown wrong and are corrected in `DIAGNOSIS.md`.

## Commands (PowerShell 7)

```powershell
.\scripts\build.ps1                 # configure + build release (enters the VS dev shell; needs CMake, Ninja, MSVC)
.\scripts\build.ps1 -Config debug
.\scripts\test.ps1                  # hardware-free self-tests; this is what CI runs (exit 1 on any failure)
.\scripts\package.ps1               # dist\CrashBarrier-<version>-x64.zip, same as the CI artifact / release asset
.\scripts\version.ps1               # semver from git tags; build.ps1 stamps it into the DLL and trace header
.\scripts\install.ps1 -Stealth      # elevated. Proxy answers for the Stadium's own CLSID; hosts need no changes
.\scripts\install.ps1               # elevated. Separate "ASIO Stadium (Proxy)" device instead
.\scripts\uninstall.ps1             # elevated. Restores the Line 6 registration; always verify by read-back
.\scripts\verify-asio-registry.ps1  # read-only dump of HKLM\SOFTWARE\ASIO with CLSIDs resolved
.\scripts\collect-traces.ps1 -Tag <host> [-Count n]   # copy newest %LOCALAPPDATA%\CrashBarrier traces+dumps to traces/
```

Build flags are `/W4 /WX`; a warning fails the build. A host that currently has the DLL loaded (Ableton,
Rocksmith+, a hung test host) makes the link step fail with a locked file: close the host first.
Registration needs an elevated shell; from a non-elevated session use
`Start-Process pwsh -Verb RunAs` and expect a UAC prompt the user must click.

### Tests

There is no unit-test framework. The test harness is `asiohost_test.exe` (a minimal ASIO host) plus
`FaultyDriver_x64.dll` (a fake driver that access-violates in `getChannels`). The hardware must be
connected for the real-driver runs; without it `init` returns 0 and nothing is proven.

```powershell
$b = '.\build\release'
& $b\asiohost_test.exe --dll $b\CrashBarrier_x64.dll                                  # full streaming cycle, 2 in / 2 out
& $b\asiohost_test.exe --dll $b\CrashBarrier_x64.dll --driver $b\FaultyDriver_x64.dll  # SEH path: must log FAULT + minidump, exit 1, host survives
& $b\asiohost_test.exe --dll $b\CrashBarrier_x64.dll --driver C:\nonexistent.dll       # load-failure path, exit 2
& $b\asiohost_test.exe --dll $b\CrashBarrier_x64.dll --inputs 1 --outputs 0 --frames 384   # the Rocksmith+ bug; must stream (shim), used to fault
& $b\asiohost_test.exe --dll $b\CrashBarrier_x64.dll --sysref desktop --probes 2 --cycles 2 --inputs 8 --outputs 0 --frames 384  # replay of the game's launch sequence
& $b\asiohost_test.exe --dll C:\Windows\System32\StadiumAsio_OnInterposer3_64.dll --real --inputs 1 --outputs 0   # same against the raw driver: crashes (0xC0000005)
```

Exit codes: 0 full cycle ok, 2 `init` returned false, 1 anything else. After any run, read the trace in
`%LOCALAPPDATA%\CrashBarrier\trace_<pid>_<timestamp>.log`; the test host's stdout is a summary only.
Real hosts for acceptance: Ableton Live 12 (golden) and Rocksmith+ (Steam app id 2834910; its audio
config is `Documents\My Games\Rocksmith+\state.cfg`, OneDrive-redirected on this machine; on a crash
Steam's `logs\gameprocess_log.txt` records exit code 3).

## Architecture: the parts that take several files to see

**Call path.** Host → `CoCreateInstance` (CLSID doubles as IID; see `AsioProxy::QueryInterface`) →
`DllGetClassObject` in `dllmain.cpp` → `class_factory.cpp` → `AsioProxy` (`asio_proxy.cpp`), which lazily
loads the real driver in `init()` via `real_driver.cpp` (`LoadLibraryW` → `DllGetClassObject` →
`IClassFactory::CreateInstance`; never `CoCreateInstance`, never `FreeLibrary`). Every forwarded method
goes through `Fwd`/`Call` → `Guarded` → `SehCall` in `seh_guard.cpp`. Driver → host callbacks go through
`callback_shim.cpp`, a process-global `ASIOCallbacks` with a single owner (ASIO callbacks carry no context
pointer), tagged `HOST:` in the log.

**Two modes, decided per activation.** If the host asks for `CLSID_CrashBarrier`, the proxy reports
itself as "ASIO Stadium (Proxy)". If it asks for `CLSID_Stadium` (stealth install), `module.h`'s
`Stealth()` is true and the proxy must be indistinguishable from the real driver: it accepts the
Stadium CLSID as IID and returns the real driver name. Anything host-visible that differs between the
two modes belongs behind `Stealth()`.

**SEH discipline.** `SehCall` is the only `__try` frame in the project and must stay free of C++ objects
with destructors; it takes a thunk + context so lambdas live in the caller's frame. The filter
(`ReportException`) is shared with the vectored handler in `veh.cpp`, which observes first-chance
access violations on threads the proxy never calls into (that is how the real fault was caught: the
driver dies on its own stream thread, not inside any host call). The VEH always continues the search,
caps itself at 50 reports / 3 dumps, and pins the DLL so it can never outlive its code.

**Driver lifetime.** The real DLL is loaded once per process and never unloaded, regardless of
`DllCanUnloadNow`. `LogDriverFields` reads driver-object offsets `+0x2C0..+0x2D4` (buffer counts,
decoded in `DIAGNOSIS.md`); these are v0.0.3.15-specific and are forensics only. If the driver version
changes, expect those offsets and the `+0x13B8E` fault address to move, and re-verify the shim with the
`--inputs 1 --outputs 0` run before assuming anything.

**Logging.** `logger.cpp` writes `%LOCALAPPDATA%\CrashBarrier\trace_<pid>_<ts>.log`, one `WriteFile` +
`FlushFileBuffers` per line, so the trace survives `TerminateProcess`. Control-path calls log every
time; hot-path calls (`bufferSwitch*`, `getSamplePosition`, `outputReady`, and refusals after a fault)
log the first 8 then every 1000th. Never add per-call I/O to the hot path. A trace that ends without
`DllMain: process detach` means the process was terminated abruptly.

**ASIO header.** `include/asio/iasio.h` is written from the public interface description, not the
Steinberg SDK and not rs_asio's `asio.h` (which carries Steinberg's copyright). Keep it that way.
`ASIOSamples`/`ASIOTimeStamp` are hi/lo 32-bit pairs on Windows, confirmed against this driver.

## Evidence handling

`traces/` is git-ignored except `.gitkeep`; dumps are ~52 MB each. Every registry mutation is followed
by a read-back (`verify-asio-registry.ps1`), a hard rule from the project's history of wrong root-cause
calls. Do not reintroduce IFEO/debugger attachment against `Rocksmith+.exe`: its DRM detects it, and
the proxy's VEH already provides fault-time dumps.
