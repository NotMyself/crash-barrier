# Diagnosis — Rocksmith+ × Line 6 ASIO Stadium (ticket #502946)

> Phase 2 output. Filled from the golden (Ableton) and crash (Rocksmith+) traces in `/traces`.
> This document goes verbatim to Line 6.

## Summary

**Driver bug, deterministic, reproduced without the game.** `StadiumAsio_OnInterposer3_64.dll`
v0.0.3.15 faults on its own streaming thread about 10 ms after `start()` whenever the active buffer set
created by `createBuffers()` contains **no output channels**. The stream thread indexes its output-buffer
table with `numOutputs - 1` (`StadiumAsio_OnInterposer3_64.dll+0x13B8E`, function `+0x13B70`), which
with `numOutputs == 0` wraps to `0xFFFFFFFF` and reads ~96 GB above the object. Rocksmith+ opens the
guitar connection as an **input-only** set (8 inputs, 0 outputs, 384 frames), so it dies every time;
Ableton Live always activates outputs, so it never does. Repro with the bundled test host, no game needed:

```
asiohost_test --dll CrashBarrier_x64.dll --inputs 1 --outputs 0 --frames 384     # faults every run
asiohost_test --dll CrashBarrier_x64.dll --inputs 2 --outputs 1 --frames 384     # streams fine
```

Workaround implemented in the proxy: an input-only set is padded with one hidden, silent output channel
before it reaches the driver (see Remediation). Fix for Line 6: guard `numOutputs == 0` (and the
symmetric input path) in the streaming thread, or reject output-less sets in `createBuffers`.

## Environment

| Item | Value |
|---|---|
| Driver DLL | `C:\WINDOWS\system32\StadiumAsio_OnInterposer3_64.dll` v0.0.3.15 |
| Interposer | `C:\ProgramData\Line 6\Interposer3\Interposer3_64.dll` v0.0.3.15 (delay-loaded by the driver) |
| OS | Windows 11 build 26100 |
| Golden host | Ableton Live 12.4.2 |
| Crash host | Rocksmith+ (Steam), `Rocksmith+.exe` |
| Proxy | CrashBarrier `<version>` |

## Faulting call

| Field | Value |
|---|---|
| Where | **Not inside any host→driver call.** On a worker thread the ASIO DLL created itself (thread start address `StadiumAsio_OnInterposer3_64.dll+0x529F4`), 16.4 s after the game's launch-time enumeration, while the game auto-connected to its saved device "ASIO Stadium" (the real driver, directly via COM). |
| Exception code | `0xC0000005` access violation |
| Faulting module + offset | `StadiumAsio_OnInterposer3_64.dll` v0.0.3.15 **+ 0x13B8E** (image base 0x180000000, fault at 0x180013B8E) |
| Access kind / target address | **read** of `0x0000022C240B6F48` (a heap-range address, not NULL; pattern consistent with a stale object pointer) |
| Captured by | CrashBarrier's vectored exception handler, first chance, before the game's handler terminated the process |
| Minidump | `traces/rocksmith_20261027-174139_fault_36316_20261027-174139.dmp` (52 MB, with indirectly referenced memory) |
| Trace | `traces/rocksmith_20261027-174139.log` |

### Instruction-level decode of the fault (from the minidump + `dumpbin /DISASM`)

The faulting function begins at `StadiumAsio_OnInterposer3_64.dll+0x13B70` and takes `(this, edx, r8d)`:

```
13B7A  mov  eax, [rcx+2C8h]            ; eax = this->count_2C8
13B83  dec  eax                         ; eax = count - 1
13B8A  lea  r9, [rax+rax*2]             ; r9  = (count-1) * 3   (24-byte table entries)
13B8E  mov  eax, [rcx+r9*8+4C0h]        ; read this->table_4C0[count-1]   <-- FAULT
```

Registers at the fault, read from the dump: `rcx (this) = 0x214240B6AA0`, `rax = 0xFFFFFFFF`,
`r9 = 0x2FFFFFFFD`, so the read address is `this + 0xFFFFFFFF*24 + 0x4C0 = 0x22C240B6F48`, exactly the
reported fault address. Memory at `this` is intact (vtable `StadiumAsio+0x83B28`), with
`[this+0x2C0] = 8`, `[this+0x2C4] = 8` (the 8 in / 8 out channel counts) and **`[this+0x2C8] = 0`**.

**Root cause at the driver level:** a worker thread the ASIO DLL created itself (CRT thread thunk at
`+0x529F4`) indexes `table[count-1]` with `count == 0` and no guard, i.e. it processes device data
while the driver object has **zero buffers**. The stack above the fault is entirely driver code with a
frame from `Interposer3Backend64.dll+0x4355` (a USB-backend data callback into the ASIO DLL):
`+0x14A45 ← +0x192B7 ← Interposer3Backend64+0x4355 ← +0x130FB ← +0x1460F`.
No host call is on that stack. Ableton never reaches this state because it calls `createBuffers`
before `start`, and holds the buffers for the whole session.

**Run 4 (state.cfg pointed at "ASIO Stadium (Proxy)"):** identical outcome. The game probed the
proxy at launch exactly as before, never created a second proxy instance, and died 16.6 s after
launch at the same instruction (`+0x13B8E`, `count_2C8 == 0`, fresh dump in `traces/`). In both
dumps the faulting object is a **different allocation** from the one the proxy released (vtable
`+0x83B28`/`+0x83BF0`, `[this+0x18] == 1`, the only object with that vtable in captured memory),
so a live driver object the proxy never saw existed in the process. Either the game ignores the
device name and instantiates "ASIO Stadium" directly, or the driver keeps internal state alive
after the probe. Timing is the same in runs 3 and 4 (16.4 s / 16.6 s after `init`).

**Control run (proxy unregistered, state.cfg still naming the now-absent "ASIO Stadium (Proxy)"):**
the game died anyway. Steam's `gameprocess_log.txt` records the main `Rocksmith+.exe` process living
26 s and exiting with **exit code 3** at 18:05:49, exactly like today's four proxy runs (17:25:25,
17:31:09, 17:41:51, 17:54:51, all exit code 3, all 26–31 s of life) and like every crash Steam
logged since July 2026 (exit code 3 throughout). Conclusions:

- The proxy is not a factor. The brief's "exit code 0xC0000005" is not what Steam records.
- **No device selection and no saved ASIO connection is required.** Launch-time enumeration of the
  Line 6 driver (`init` → probe → `Release` on a TIME_CRITICAL STA worker, desktop HWND as `sysRef`)
  is sufficient; the driver's own thread faults ~16.5 s later with `count_2C8 == 0`.
- The brief's "game lists the device fine; selecting it kills the process" is stale for the current
  game build / driver 0.0.3.15.

**Stealth run** (`install.ps1 -Stealth`, proxy answering for the Stadium CLSID, state.cfg =
"ASIO Stadium", trace `traces/rocksmith-stealth_*.log`): the game's real launch sequence against
the driver, on the TIME_CRITICAL STA worker with the desktop HWND, all within 1.9 s:

1. instance A: `init → getDriverName → getDriverVersion → getChannels → getBufferSize → outputReady
   → canSampleRate(48000) → getChannelInfo×16 → Release`   (enumeration probe)
2. instance B: identical probe → `Release`
3. instance C: probe → `getSampleRate → createBuffers(8 OUTPUT channels only, 384 frames) →
   getLatencies → start → [1.07 s, 57 bufferSwitch] → stop → disposeBuffers → Release`

Every call succeeded. **With the proxy in the path the game did not crash**: still running at
80 s, no exception seen by the vectored handler, versus a fatal fault at ~16.5 s in all five direct
runs. The proxy changes nothing semantically; it adds ~2 ms per call (flushed logging) and ~50 ms
after `init` and after `Release` (thread enumeration), and it never unloads the DLL. The leading
explanation is therefore a **race inside the driver**: `init` spawns its worker threads
asynchronously (they appear ~50 ms after `init` returns), and a host that calls `Release` or
`createBuffers`/`start` within microseconds of `init`, as Rocksmith+ does, tears down or reuses the
object before those threads have bound to it. To be confirmed by (a) repeat stealth runs and
(b) a stealth build with the per-call delays removed, which should bring the crash back.

## init() context in each host

| Field | Ableton (golden, 2026-10-27) | Rocksmith+ |
|---|---|---|
| Calling thread id / description | main thread, description `MainThread` | worker thread, no description, **priority 15 (TIME_CRITICAL)** |
| GUI thread (has message queue) | yes | yes |
| COM apartment | MAINSTA (`CoGetApartmentType` S_OK) | **STA** (a secondary apartment, not the main one) |
| sysRef | Live's main window (`Ableton Live Window Class`, visible) | **`0x1000C`, class `#32769` = the desktop window** |
| sysRef is window / owner thread / owner process | window / calling thread / this process | window / **other thread** / **other process** (desktop owner) |
| When `init` happens | when the user selects the device | **at game launch, during enumeration**, before any UI |
| Object lifetime | kept for the whole session | **init → probe → Release within 140 ms** |

### 2026-10-27 runs 1–3: launch crashes (traces `traces/rocksmith_*.log`)

Correction to the brief's premise: the saved connection was **not** WASAPI. `state.cfg`
(`Documents\My Games\Rocksmith+\state.cfg`, OneDrive-redirected) had
`asiodevicename = "ASIO Stadium"`, `inputdevicename`/`outputdevicename = "ASIO Stadium"`,
`connection_connectionmode = 3`, `asiobuffersize = 384`, `exclusivemode = true`. So every launch
auto-connected to the **real** driver directly via COM; the proxy was only probed during enumeration
(`init(desktop HWND) → getDriverName → getDriverVersion → getChannels → getBufferSize → outputReady →
canSampleRate(48000) → getChannelInfo×16 → Release`, all normal, both driver worker threads gone by
the time `Release` returned) and was never on the crashing path.

Runs 1 and 2 (proxy v0.1.0 without a process-wide handler) therefore showed nothing at fault time;
the interim "FreeLibrary after Release" hypothesis recorded in TICKET.md is withdrawn. Run 3, with
the vectored exception handler, captured the fault described in **Faulting call** above: an access
violation on a driver-created thread, inside the ASIO DLL, with a fault-time minidump.

Golden call sequence (Ableton Live 12.4.2, buffer 512, 48 kHz):
`init → getDriverName → getDriverVersion → getClockSources → setSampleRate(48000) → getChannels → getBufferSize → createBuffers(4ch) → getChannelInfo×20 → getSampleRate → getLatencies×N → canSampleRate×8 → start → [getSampleRate polled 1/s; bufferSwitch only, no bufferSwitchTimeInfo, no asioMessage from driver] → stop → disposeBuffers → Release`.
Driver reports 8 in / 8 out, sample type 17, latencies in=1369 out=1881 at 512 frames.

## Call sequence divergence

_Golden sequence vs crash sequence up to the fault. Mark the first divergence._

## Hypothesis status

- [ ] NULL / non-pumping `sysRef` or thread context (brief §3.6)
- [ ] COM apartment not initialized on calling thread
- [ ] Call ordering / sample-rate negotiation differs
- [ ] Fault internal to the driver regardless of host-visible behaviour

## Driver object fields (v0.0.3.15), decoded by logging them around createBuffers/start

| Offset | Meaning (from test-host runs) | Dying Rocksmith+ instance |
|---|---|---|
| `+0x2C0` | total buffers in the set | 8 |
| `+0x2C4` | input buffers | 8 |
| `+0x2C8` | output buffers — **the index base used by the stream thread** | **0** |
| `+0x2CC` | 1 when the set has no inputs | 0 |
| `+0x2D4` | buffer-switch counter | 1 |

The surviving launch-time instance was the mirror image (8 outputs, 0 inputs): the input path has no
such underflow, so output-only sets stream fine.

## Remediation (Phase 3)

`AsioProxy::createBuffers`: if the host's set has no output channel, pass the driver a copy with one
extra entry `{isInput = 0, channel 0}`, then hand the host back its own entries. The hidden output's
buffers are driver-allocated and never written, so they stay silent. Verified with the test host
(input-only sets stream through the shim for the full run, driver fields show one output).
**Verified in Rocksmith+ on 2026-11-06** (proxy in stealth mode, traces `traces/rocksmith-gate_*`):
two sessions, zero faults, both exited with Steam exit code 0. The game connected the guitar through
the padded input-only set three times (previously fatal every time), calibrated, and streamed the
8-in + 2-out set for 5.5 min and later for 66 s during a song, at a constant 125 callbacks/s. The
first session ended in a game-side error after the song attempt that left no trace in the driver
path (all calls succeeded, no callback stall); it did not recur in the second session. Remaining
gate item: a 30-minute play session and the user's latency/note-detection verdict.
The proxy also runs in stealth mode (`install.ps1 -Stealth`), answering for the Stadium's own CLSID,
so the game needs no reconfiguration.

## Attachments

- `traces/ableton_*.log`
- `traces/rocksmith_*.log`
- `traces/rocksmith_*_fault_*.dmp`
