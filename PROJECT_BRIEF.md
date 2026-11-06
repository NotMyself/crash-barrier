# CrashBarrier — Project Brief

**An instrumented ASIO proxy driver that sits between Rocksmith+ and the Line 6 Helix Stadium ASIO driver: diagnoses the crash neither vendor will fix, survives it, and — if the diagnosis permits — fixes it in the shim.**

Working name: `CrashBarrier` (DLL: `CrashBarrier_x64.dll`). Rename via find-and-replace if desired.

---

## 1. Problem statement

Rocksmith+ (Steam, Windows 11) terminates silently whenever it initializes the Line 6 Helix Stadium ASIO driver. The fault is an access violation (`0xC0000005`) raised inside or around the driver's init path, caught by the game's own crash handler (Themida/WinLicense-class DRM + custom handler), and converted into `TerminateProcess` with the exception code as the exit code. No dialog, no WER event, no usable dump.

Both vendors have had the bug report since July 2026 (Line 6 ticket **#502946**). Driver updates 0.0.3.13 → 0.0.3.15 did not fix it. Neither company is moving. This project ends the wait.

### Established facts (July–October 2026 investigation)

| Fact | Evidence |
|---|---|
| Faulting component is the Line 6 ASIO driver DLL | Removing its registry key eliminates the crash entirely; game runs and connects via WASAPI |
| Driver DLL: `C:\WINDOWS\system32\StadiumAsio_OnInterposer3_64.dll` | `HKCR\CLSID\{bf6ad246-4cb9-426c-b41d-520e50f0d983}\InprocServer32` |
| Registered under `HKLM\SOFTWARE\ASIO\ASIO Stadium`, CLSID `{bf6ad246-4cb9-426c-b41d-520e50f0d983}` | Registry audit |
| Current driver version 0.0.3.15 (crash reproduces); also crashed on 0.0.3.13 | File VersionInfo; manual repro 2026-10-13 |
| A legacy `StadiumAsio_OnInterposer_x64.dll` v2.0.0.6 exists in system32 but is **not registered** | CLSID points only at the `...Interposer3` DLL |
| Crash fires on **driver initialization**, not enumeration | Game lists the device fine; selecting it kills the process. Startup crashes occur only when the ASIO device is the *saved* connection (game auto-connects pre-UI, dies ~5s after launch) |
| Exception never goes unhandled | procdump `-e` (second-chance) captured nothing across multiple runs; WER has no Event 1000/1001. Exit code is `0xC0000005` |
| Terminate-time dumps are useless | Full `-t` dump showed the driver DLL **already unloaded** at exit — handler catches AV, frees the driver, terminates |
| Fault-time dumps are blocked externally | AV fires within ~1s of process start (before late attach completes); IFEO launch-time debugging triggers DRM: "A debugger has been found running in your system" |
| **Ableton Live 12 initializes the same driver DLL without issue, same machine** | Repeated verification. The fault is specific to something in Rocksmith+'s init sequence |
| RodeCaster Pro II ASIO driver exonerated | Registry-level removal test; crash persisted with only the Stadium driver registered |
| Stadium's WASAPI endpoints work perfectly in the game | Current workaround: ASIO key removed → WASAPI connection → key restored for DAW use |
| Environment | Windows 11 26100, Helix Stadium XL firmware 1.3.2+, Helix Stadium app 1.4, Rocksmith+ Steam (`Rocksmith+.exe`, ~50 MB, with thin `Rocksmith+Launcher.exe`) |

### Why a proxy is the right instrument

An ASIO "driver" is a **user-mode COM-style DLL** the host loads voluntarily via `CoCreateInstance`. No kernel code, no signing requirement, no debugger for the DRM to detect. A proxy DLL that forwards every call to the real driver inside SEH guards catches the AV **in-process, on our own stack frame, before the game's exception handler sees it** — capturing exactly the faulting-module/offset/call-context evidence that Themida denied us, and converting the crash into a graceful "connection failed."

---

## 2. Goals, in priority order

1. **Crash-proof:** Rocksmith+ with the proxy selected must never terminate due to a driver fault. Worst case: logged failure + `ASE_NotPresent` returned + game shows "connection failed."
2. **Diagnose:** Produce a call-trace diff between a working host (Ableton) and the crashing host (Rocksmith+) that identifies the exact faulting call, its arguments, and the fault address/module — sufficient for Line 6 to fix it from the ticket alone.
3. **Fix (if shimmable):** Based on the diff, remediate in the forwarding layer so Rocksmith+ gets a working low-latency ASIO path to the Stadium.
4. **Document:** The repo is the artifact of record for ticket #502946.

### Non-goals
- No modification of game files, game memory, or the Line 6 DLL. The proxy is a separately registered system-side component (interop, not tampering).
- No 32-bit build. Rocksmith+ is x64; 32-bit ASIO proxying is a `__thiscall` adapter swamp we have no reason to enter.
- No multi-client ASIO, no mixing, no feature additions. Transparent pass-through plus the minimum remediation the diagnosis dictates.
- Not a general-purpose tool (initially). Hardcoding Stadium specifics is acceptable; generalize only if trivial.

---

## 3. Architecture

### 3.1 Component
Single C++20 x64 DLL, MSVC, CMake + Ninja. No runtime dependencies beyond Win32. Static CRT (`/MT`) to avoid redistributable coupling.

```
CrashBarrier_x64.dll
├── DllMain / DllGetClassObject / DllRegisterServer / DllUnregisterServer
├── ClassFactory            — answers CoCreateInstance for the proxy CLSID
├── AsioProxy : IASIO       — vtable-forwarding proxy, SEH-guarded
├── CallbackShim            — wraps host's ASIOCallbacks before passing to real driver
├── RealDriverLoader        — registry-free load of the Line 6 DLL
└── Logger                  — crash-durable trace log
```

### 3.2 The ASIO/COM quirks (handle these first; they bite immediately)

- **`IASIO` is not real COM.** It derives from `IUnknown` but hosts call C++ virtual methods directly on the vtable — no marshaling, no proxy/stub, no typelib, apartment rules ignored in practice. Calls may arrive on any thread.
- **The CLSID is the IID.** ASIO hosts call `CoCreateInstance(clsid, nullptr, CLSCTX_INPROC_SERVER, /*riid=*/clsid, &p)`. The proxy's `QueryInterface` must succeed for `IID_IUnknown` **and for the proxy's own CLSID**.
- **Interface definitions:** vendor clean-room `IASIO`/`ASIOCallbacks`/type headers from **rs_asio** (github.com/mdias/rs_asio, MIT) — battle-tested against games. Do **not** take Steinberg's ASIO SDK as a dependency (license friction for redistribution).
- **x64 blessing:** one calling convention; the vtable proxy is plain virtual overrides.

### 3.3 Registration & loading

- Proxy registers under its **own** CLSID (generate once, commit as a constant) with `InprocServer32` → proxy DLL path, `ThreadingModel=Apartment` (matching the Line 6 entry), plus `HKLM\SOFTWARE\ASIO\ASIO Stadium (Proxy)` with `CLSID` + `Description` values. The game lists it alongside the real entries; the user selects the proxy.
- **Registry-free load of the real driver:** `LoadLibraryW(L"StadiumAsio_OnInterposer3_64.dll")` (system32 resolves; also probe an explicit path from config) → `GetProcAddress("DllGetClassObject")` → `IClassFactory::CreateInstance(nullptr, real_clsid_as_iid, ...)` using Line 6's CLSID `{bf6ad246-4cb9-426c-b41d-520e50f0d983}`. This bypasses `CoCreateInstance` for the inner driver, survives Line 6 re-registrations, and lets both drivers coexist.
- **Lazy instantiation:** do NOT load the real driver in `DllMain` or the class factory (loader lock; and enumeration-time loading is exactly the hazard we're routing around). Load it inside the proxy's `init()` — or first method call — under SEH.
- Ship `install.reg` / `uninstall.reg` (or `regsvr32`-driven `DllRegisterServer`); README documents both. Include the known recovery recipe for a poisoned saved connection (remove `HKLM\SOFTWARE\ASIO\ASIO Stadium (Proxy)` key → launch → reselect WASAPI → restore).

### 3.4 Fault capture (the core deliverable)

- Every forwarded `IASIO` method body lives in a **dedicated SEH wrapper function**: pure `__try/__except`, no C++ objects requiring unwinding in that frame (keep `/EHsc` code in separate functions; mixing SEH and C++ unwinding in one frame is a compile error and a design smell).
- The `__except` filter receives `EXCEPTION_POINTERS` and records: exception code, faulting address, module containing the address via `GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS, ...)` + `GetModuleFileNameW`, offset from module base, the proxied method name, and its logged arguments. Optionally write a minidump (`MiniDumpWriteDump`, `MiniDumpNormal`) from the filter — we are upstream of the game's handler, so this is the fault-time dump that was unobtainable in July.
- On catch: log, return the ASIO error (`ASIOFalse` from `init`, `ASE_NotPresent`/`ASE_HWMalfunction` from others), **do not rethrow**. Process survives.
- **Callback direction:** wrap the host's `ASIOCallbacks` in `createBuffers` — pass the real driver a proxy-owned struct whose functions SEH-guard and forward to the host's originals. Faults in game callbacks must be distinguishable from faults in the driver.

### 3.5 Logging discipline

- Control-path calls (`init`, `getChannels`, `createBuffers`, `setSampleRate`, `start`, `stop`, ...): timestamped, arguments and return values, **flushed per line** (`FlushFileBuffers` or unbuffered writes) — durability up to the instant of a crash is the point.
- Hot path (`bufferSwitch`, `bufferSwitchTimeInfo`, `getSamplePosition`): log first N occurrences then every Nth, counters only. **No per-call file I/O in the audio path** once streaming is established.
- Log file: `%LOCALAPPDATA%\CrashBarrier\trace_<pid>_<timestamp>.log`. Include process image name and PID in the header — the diff tooling keys on host identity.
- Also log `init`-context forensics: calling thread ID, `GetThreadDescription` if any, whether the thread has a message queue (`GetQueueStatus` heuristic), COM apartment state (`CoGetApartmentType`), and the `sysRef` HWND value + `IsWindow(sysRef)`.

### 3.6 Leading hypothesis (pre-registered, to be tested not assumed)

The DLL's name — `OnInterposer3` — and the Stadium ecosystem's app-centric design suggest the "driver" is itself a shim that may create windows, use COM, or IPC to other Line 6 components during `init`. Prime suspect: Rocksmith+ calls `ASIOInit` with a NULL or non-pumping `sysRef`/thread context (e.g., from a threadpool worker), where Ableton passes a real message-pumped window. **The Phase 2 diff confirms or kills this.** Do not build the Phase 3 fix before the diff exists.

---

## 4. Phases & acceptance gates

### Phase 0 — Scaffold
CMake project, CLSID generated and committed, registration plumbing, logger, empty pass-through proxy that loads the real driver lazily.
**Gate:** `regsvr32` succeeds; proxy appears in an ASIO host's device list; `HKLM\SOFTWARE\ASIO` shows the new key; uninstall is clean.

### Phase 1 — Transparent pass-through + instrumentation
Full `IASIO` forwarding with SEH + logging; callback shim in place.
**Gate:** **Ableton Live 12 runs audio through "ASIO Stadium (Proxy)" indistinguishably from the direct driver** — playback and input both, no glitches at the same buffer size, clean stop/start/close. Trace log shows the complete call sequence.

### Phase 2 — The diff (diagnosis)
Capture golden trace (Ableton) and crash trace (Rocksmith+, proxy selected). Rocksmith+ must now survive selection (failed connection is acceptable and expected at this phase).
**Gate:** A written diagnosis in `DIAGNOSIS.md`: last call before fault, full arguments, faulting module+offset, divergences from the golden trace. This document goes verbatim to Line 6 ticket #502946 regardless of Phase 3's outcome.

### Phase 3 — Remediation (contingent on diagnosis)
Implement the shim fix the diff dictates. Candidate interventions, in order of likelihood: synthesize a hidden message-pumped window and substitute it as `sysRef`; marshal all inner-driver calls onto a single proxy-owned pumped thread; ensure COM apartment init (`CoInitializeEx`) on the calling path; normalize call ordering/sample-rate negotiation to match Ableton's sequence.
**Gate:** Rocksmith+ connects via the proxy, completes "turn up and play" calibration, note detection works in a real song, and measured latency subjectively beats the WASAPI path. Stability across 30+ minutes of play.
**Honest exit:** if the fault is internal to the driver regardless of host-visible behavior, Phase 3 may be unachievable. Phases 1–2 still pay for themselves: crash-proofing + the diagnosis Line 6 needs.

---

## 5. Test protocol & safety rails

- **Hosts:** Ableton Live 12 (golden), Rocksmith+ Steam (target). Optional third opinion: REAPER (free eval) — a second well-behaved host makes the diff more credible.
- **Never leave Rocksmith+ with a poisoned saved connection:** after any crashing experiment, the recovery recipe (README) restores WASAPI. The proxy's crash-proofing should make this obsolete after Phase 1 — verify that claim explicitly (select proxy → induced failure → relaunch game → confirm no startup crash loop).
- **Registry hygiene — hard rule from the July post-mortem:** every registry mutation is followed by a read-back verification in the same session. Silent `Remove-Item` failures and unverified assumptions cost us two wrong root-cause declarations in July. `scripts/verify-asio-registry.ps1` prints the full `HKLM\SOFTWARE\ASIO` state; run it after every install/uninstall.
- **procdump interactions:** not needed anymore (the proxy *is* the capture mechanism), and launch-time debuggers trip the game's DRM. Do not reintroduce IFEO against `Rocksmith+.exe`.
- **DRM risk, pre-registered:** the game tolerated loading Line 6's third-party DLL via this mechanism, so it should load ours. Detectable failure mode: proxy absent from the game's device list or refused at selection. Fallback (last resort, documented but not default): register the proxy *under the Stadium's own CLSID* and have it load the real DLL by path — invisible to the game, uglier to maintain.

## 6. Repo layout

```
/src            proxy, class factory, loader, callback shim, logger
/include/asio   vendored clean-room IASIO headers (rs_asio provenance noted)
/scripts        install.reg, uninstall.reg, verify-asio-registry.ps1, collect-traces.ps1
/docs           DIAGNOSIS.md (Phase 2 output), TICKET.md (Line 6 #502946 correspondence log)
/traces         .gitignored; golden + crash traces land here
CMakeLists.txt, README.md, PROJECT_BRIEF.md (this file)
```

## 7. References

- rs_asio (MIT) — clean-room ASIO interface headers, host-interop patterns: github.com/mdias/rs_asio
- FlexASIO — open-source ASIO driver, useful as a second structural reference: github.com/dechamps/FlexASIO
- Line 6 ticket #502946 — full investigation history (July–Oct 2026)
- Driver under test: `StadiumAsio_OnInterposer3_64.dll` v0.0.3.15, CLSID `{bf6ad246-4cb9-426c-b41d-520e50f0d983}`
