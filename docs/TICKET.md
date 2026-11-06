# Line 6 ticket #502946 — correspondence and findings log

Investigation history July–October 2026 is summarized in `PROJECT_BRIEF.md` §1.

## 2026-10-27 — static analysis of the driver (CrashBarrier survey)

- `StadiumAsio_OnInterposer3_64.dll` v0.0.3.15 is a thin COM shell. It exports only
  `DllGetClassObject`, `DllCanUnloadNow`, `DllRegisterServer`, `DllUnregisterServer`.
  `InprocServer32` ThreadingModel = `Apartment`.
- It **delay-loads** `Interposer3_64.dll` (v0.0.3.15) from `%ProgramData%\Line 6\Interposer3\`
  (resolved via `SHGetKnownFolderPath` + the literal `\Line 6\Interposer3\`; the directory is not
  on PATH and the DLL is not in system32). Imported entry points: `InterposerInit`, `InterposerCleanup`,
  `InterposerGetVersion`, `InterposerGetDescriptorCount`, `InterposerGetDescriptors`,
  `InterposerOpenPipe`, `InterposerClosePipe`, `InterposerMessage`, `InterposerRegisterCallback`,
  `InterposerDeviceIoControl`, `InterposerReadFileEx`, `InterposerWriteFile`.
- The ASIO DLL itself imports only `MessageBoxW`/`CharLowerA` from USER32 and
  `CoTaskMemFree`/`StringFromCLSID` from ole32. It creates threads (`CreateThread`) and uses
  WINMM timers (`timeGetTime`, `timeKillEvent`).
- `Interposer3_64.dll` imports `RegisterClassExW`, `CreateWindowExW`, `RegisterDeviceNotificationW`,
  `GetMessageA`, `DispatchMessageW`, `TranslateMessage`, `PostQuitMessage`, `DestroyWindow`, and the
  `SetupDi*` device-enumeration family. The window/message-loop machinery lives here, one layer
  below the ASIO object.
- Device table: `L6ConfigInterposer3.json` lists USB VID 0x0E41 PIDs 0x4841 (Stadium Floor) and
  0x4842 (Stadium LT), backend `Interposer3Backend` (`system32\Interposer3BackEnd64.dll`).

## 2026-10-27 — first instrumented runs

- Ableton Live 12.4.2 through the proxy: ~160 s, 15076 buffer switches, zero faults (golden trace).
- Rocksmith+ **initializes every registered ASIO driver at launch** (not on selection) from a
  TIME_CRITICAL worker thread in a secondary STA, passing the **desktop window** as `sysRef`, then
  releases the object ~140 ms later.
- The game's saved connection (`state.cfg`) was the real "ASIO Stadium", so each launch
  auto-connected to the Line 6 driver directly. An interim hypothesis blaming the proxy's
  `FreeLibrary` after `Release` was wrong and is withdrawn; the proxy no longer unloads the driver
  regardless.
- **Fault captured (run 3, 17:41:39):** access violation at
  `StadiumAsio_OnInterposer3_64.dll+0x13B8E` (v0.0.3.15), reading `0x22C240B6F48`, on a thread the
  ASIO DLL created itself (start `+0x529F4`), ~16 s after launch, with no host call on the stack.
  Fault-time minidump written by CrashBarrier's vectored handler before the game's own handler
  terminated the process. See `DIAGNOSIS.md`.

## 2026-10-27 — root cause reproduced without the game

- `createBuffers()` with **zero output channels** followed by `start()` → access violation on the
  driver's streaming thread at `StadiumAsio_OnInterposer3_64.dll+0x13B8E` (reads output table at
  index `numOutputs - 1 = 0xFFFFFFFF`). 1 in/0 out faults; 8 in/0 out faults; 2 in/1 out is fine.
  Reproduced from a 200-line console host (`tools/asiohost_test.cpp`), dumps in `traces/repro-*`.
- Rocksmith+ opens the guitar input as an 8-input, 0-output set at 384 frames → crash on every connect.
  Its launch-time 8-output, 0-input test set streams fine, which is why enumeration alone looked
  innocent in earlier notes.
- Proxy workaround: pad input-only sets with one hidden output channel. Driver fix requested: guard
  `numOutputs == 0` in the stream thread or reject output-less sets in `createBuffers`.

## 2026-11-06 — in-game verification of the workaround

- With the proxy padding input-only sets, Rocksmith+ no longer crashes: guitar connected three
  times, calibration completed, a song streamed on the full 8-in + 2-out set, two clean exits.
- The game's own sequence on connect: `createBuffers(8 inputs, 384)` → `start` → release without
  `stop` ~1 s later → new instance `createBuffers(8 inputs + 2 outputs, 384)` → `start` → play.
  Without the pad, the first `start` is fatal in driver 0.0.3.15.

## Correspondence

_Append dated entries here._
