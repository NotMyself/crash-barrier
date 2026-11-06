# CrashBarrier

A transparent ASIO proxy for the Line 6 **Helix Stadium** driver on Windows x64. It was built to find
out why Rocksmith+ died the instant it touched that driver, and it ended up both explaining the crash
and working around it.

**The bug, in one sentence:** Line 6's `StadiumAsio_OnInterposer3_64.dll` v0.0.3.15 faults on its own
streaming thread about 10 ms after `start()` whenever the buffer set created by `createBuffers()`
contains no output channels. Rocksmith+ opens the guitar as an input-only set, so it crashed on every
connect. DAWs always activate outputs, so they never saw it.

**The workaround:** when a host asks for an input-only set, the proxy adds one hidden, silent output
channel to what the driver sees and hands the host back exactly what it asked for. Everything else is
pass-through. Full analysis, instruction-level decode, a 200-line reproduction and the verification
runs are in [`docs/DIAGNOSIS.md`](docs/DIAGNOSIS.md); the dated log for Line 6 ticket #502946 is in
[`docs/TICKET.md`](docs/TICKET.md).

## Status

| | |
|---|---|
| Root cause | Reproduced deterministically without the game (`asiohost_test --inputs 1 --outputs 0`) |
| Rocksmith+ with the proxy | Connects, calibrates, plays. Two sessions, zero faults (2026-11-06) |
| Ableton Live 12 through the proxy | Indistinguishable from the direct driver |
| Driver fix from Line 6 | Not yet. The proxy is a shim until then |
| Tested on | Windows 11 26100, Helix Stadium XL, driver 0.0.3.15, Rocksmith+ (Steam) |

The driver-object offsets the proxy logs for forensics are specific to driver 0.0.3.15. A new driver
version needs the reproduction run below before trusting the shim.

## Download

Prebuilt packages are on the [Releases](https://github.com/NotMyself/crash-barrier/releases) page:
`CrashBarrier-<version>-x64.zip` with the DLL, its PDB, the test host, the install scripts and an
install guide (the same text as [`docs/INSTALL.md`](docs/INSTALL.md)). Every push to `main` also
produces the package as a CI artifact; tags of the form `vX.Y.Z` publish a release.

## Build

Requirements: Visual Studio 2022 or later with the C++ x64 toolset, CMake 3.25+, Ninja, PowerShell 7.
No runtime dependencies; the DLL links the CRT statically.

```powershell
.\scripts\build.ps1          # -> build\release\CrashBarrier_x64.dll, asiohost_test.exe, FaultyDriver_x64.dll
.\scripts\test.ps1           # hardware-free self-tests (load failure, SEH catch + minidump, version stamp)
.\scripts\package.ps1        # -> dist\CrashBarrier-<version>-x64.zip + SHA256SUMS.txt
```

The version comes from git tags (`scripts\version.ps1`): `1.2.3` on a `v1.2.3` tag, `1.2.3-dev.N+sha`
after it. It is stamped into the DLL's version resource and the first line of every trace.

## Install

From an elevated PowerShell:

```powershell
.\scripts\install.ps1 -Stealth        # recommended
.\scripts\verify-asio-registry.ps1    # read-back: "ASIO Stadium" should now resolve to CrashBarrier_x64.dll
```

Stealth mode points the Stadium's own CLSID at the proxy, so every host that selects "ASIO Stadium",
including a Rocksmith+ install that already has it saved, gets the proxy without any reconfiguration.
The alternative `install.ps1` without `-Stealth` registers a separate device called
"ASIO Stadium (Proxy)" that you select by hand; useful for A/B comparison, not for daily use.

The proxy loads the real driver from `system32` by path. Nothing in the Line 6 installation is
modified except, in stealth mode, the one registry value saying which DLL implements the CLSID.

```powershell
.\scripts\uninstall.ps1               # restores the Line 6 registration, verifies by read-back
```

The install location is wherever you built it; don't delete `build\release` while registered.

## What it does when loaded

- Forwards all 21 `IASIO` methods to the real driver inside a structured exception handler. A fault
  inside a call is logged with module and offset, gets a minidump, and comes back to the host as an
  ASIO error instead of a dead process.
- Wraps the host's callbacks the same way, so a fault in the host is distinguishable from one in the
  driver (`HOST:` lines in the trace).
- Installs a process-wide vectored exception handler that logs first-chance access violations on
  **any** thread and writes a dump for the first few. This is what caught the real crash, which
  happens on a thread the driver creates itself, outside every host call. It observes only and never
  changes how the host handles the exception.
- Pads input-only buffer sets with a hidden output (the workaround above).
- Never unloads the driver DLL once loaded, whatever `DllCanUnloadNow` says.

## Traces

Every process that loads the proxy writes `%LOCALAPPDATA%\CrashBarrier\trace_<pid>_<timestamp>.log`,
flushed per line so it survives the host being terminated. Control-path calls are logged with
arguments, results and timing; the audio hot path logs the first 8 calls, then every 1000th. The
`init` lines record the calling thread, its priority, the COM apartment and what `sysRef` really is,
which is how host differences get compared.

A caught fault looks like:

```
FAULT in VEH #1 thread 42128 (start StadiumAsio_OnInterposer3_64.dll+0x529F4): access violation code=0xC0000005 at 0000000180013B8E = C:\WINDOWS\SYSTEM32\StadiumAsio_OnInterposer3_64.dll+0x13B8E
FAULT detail: reading address 000001F45D8220D8
FAULT minidump: C:\Users\you\AppData\Local\CrashBarrier\fault_39832_20261027-181600.dmp
```

`scripts\collect-traces.ps1 -Tag <host>` copies the newest log and its dumps into `traces/`, which is
git-ignored. Dumps are around 50 MB; they include the memory the faulting code referenced.

## Testing without a DAW

`asiohost_test.exe` is a minimal console ASIO host. The Stadium must be connected and on.

```powershell
$b = '.\build\release'
& $b\asiohost_test.exe --dll $b\CrashBarrier_x64.dll                       # 2 in / 2 out, 2 s of streaming, exit 0
& $b\asiohost_test.exe --dll $b\CrashBarrier_x64.dll --inputs 1 --outputs 0 --frames 384
                                                                            # the Rocksmith+ case: streams thanks to the shim
& $b\asiohost_test.exe --dll C:\Windows\System32\StadiumAsio_OnInterposer3_64.dll --real --inputs 1 --outputs 0
                                                                            # same against the raw driver: dies with 0xC0000005
& $b\asiohost_test.exe --dll $b\CrashBarrier_x64.dll --driver $b\FaultyDriver_x64.dll
                                                                            # fake driver that access-violates: proves the SEH path
& $b\asiohost_test.exe --dll $b\CrashBarrier_x64.dll --sysref desktop --probes 2 --cycles 2 --inputs 8 --outputs 0 --frames 384
                                                                            # replays Rocksmith+'s exact launch sequence
```

Options: `--inputs/--outputs N`, `--frames N`, `--seconds N`, `--probes N` (enumeration-style
init/query/release instances first), `--cycles N`, `--sysref null|desktop`, `--driver <dll>` (make the
proxy load this instead of the Line 6 driver), `--real` (target the Stadium CLSID). Exit codes: 0 full
cycle ok, 2 `init` returned false, 1 anything else. The stdout is a summary; the trace is the record.

## If Rocksmith+ won't start

Its audio settings are in `Documents\My Games\Rocksmith+\state.cfg` (Documents may be
OneDrive-redirected). `asiodevicename`, `inputdevicename` and `outputdevicename` name the device; edit
them with the game closed. With the proxy installed this should not be needed: the fatal connect now
succeeds, and a driver fault inside a call is reported to the game as a failed connection.

## Layout

```
include/asio/iasio.h   minimal ASIO interface, written from the public interface description (no SDK text)
src/                   dllmain + registration, class factory, AsioProxy, real-driver loader,
                       SEH guard, vectored handler, callback shim, logger
tools/                 asiohost_test (console host), faulty_driver (fake crashing driver)
scripts/               build, install, uninstall, verify-asio-registry, collect-traces
docs/                  DIAGNOSIS.md (root cause, repro, verification), TICKET.md (#502946 log)
traces/                collected logs and dumps, git-ignored
PROJECT_BRIEF.md       the original plan; where it was wrong, DIAGNOSIS.md says so
```

## Non-goals

No 32-bit build, no multi-client ASIO, no audio processing, no changes to game files or the Line 6
DLL. Hardcoding Stadium specifics is deliberate; the loader, the SEH/VEH instrumentation and the test
host are generic, the shim and the forensic offsets are not.

## License

MIT, see [`LICENSE`](LICENSE). The ASIO interface header is this project's own text; it contains no
Steinberg SDK material, which is what keeps the repository redistributable under these terms.
