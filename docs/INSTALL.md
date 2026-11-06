# CrashBarrier — install guide

CrashBarrier is a proxy for the Line 6 **Helix Stadium** ASIO driver. It fixes the crash that kills
Rocksmith+ the moment the guitar is connected over ASIO (driver 0.0.3.15 faults when an application
opens only input channels; the proxy quietly adds one silent output channel). For any other
application it is a transparent pass-through that also keeps a diagnostic trace.

Source, analysis and issue tracker: https://github.com/NotMyself/crash-barrier

## What you need

- Windows 11, 64-bit.
- The Line 6 Helix Stadium driver already installed and working (`ASIO Stadium` appears in your DAW).
- PowerShell 7 (`pwsh`). Install with `winget install Microsoft.PowerShell` if `pwsh` is not found.
- An administrator account. Registering an ASIO driver writes to `HKLM`.

## Install

1. Unzip to a **permanent** folder, for example `C:\Program Files\CrashBarrier`. The registry will
   point at the DLL in this folder; if you move or delete it later, run `uninstall.ps1` first.
2. Open PowerShell 7 **as administrator** in that folder.
3. Run:

   ```powershell
   Unblock-File .\*.ps1          # the zip came from the internet; Windows blocks scripts until told otherwise
   .\install.ps1 -Stealth
   ```

   `-Stealth` makes "ASIO Stadium" itself resolve to the proxy, so Rocksmith+, Ableton and anything
   else that already uses the Stadium needs no changes. The script prints the full ASIO registry
   afterwards; `ASIO Stadium` should list `CrashBarrier_x64.dll` with status `ok`.

4. Launch Rocksmith+, connect the guitar, calibrate, play.

If PowerShell refuses to run the scripts, start it with `pwsh -ExecutionPolicy Bypass` and repeat
step 3.

### Without stealth

`.\install.ps1` without `-Stealth` adds a separate device named **ASIO Stadium (Proxy)** and leaves
the original entry alone; you then select the proxy by hand in each application. Useful for
comparing with and without, not recommended for daily use with Rocksmith+ because the game may keep
the original saved.

## Uninstall

```powershell
.\uninstall.ps1      # as administrator; restores the Line 6 registration and prints the registry
```

## Check that it is working

```powershell
.\verify-asio-registry.ps1                 # registry state, any time, no admin needed
.\asiohost_test.exe --inputs 1 --outputs 0 --frames 384   # with the Stadium connected: streams for 2 s and exits 0.
                                           # Against the unpatched driver this exact call crashes.
```

## Traces and reporting a problem

Every program that loads the proxy writes a log to `%LOCALAPPDATA%\CrashBarrier\`
(`trace_<pid>_<timestamp>.log`). If something crashes anyway, a `fault_*.dmp` minidump lands next to
it. To bundle the latest log and dumps for a report:

```powershell
.\collect-traces.ps1 -Tag rocksmith        # copies into a traces\ folder next to the scripts
```

Attach the log (and the dump, if there is one) to an issue at the repository above, together with the
driver version shown near the top of the log. The log contains file paths and process names from your
machine and nothing else.

## Good to know

- The proxy never modifies the Line 6 files. Uninstalling returns the registry to exactly what Line 6
  installed.
- Updating the Line 6 driver may change the bug this proxy works around. After a driver update run the
  `asiohost_test.exe` check above; if it crashes, uninstall the proxy and report the new driver version.
- Rocksmith+ keeps its audio device choice in `Documents\My Games\Rocksmith+\state.cfg`
  (`asiodevicename`, `inputdevicename`, `outputdevicename`). If the game ever refuses to start because
  of a saved audio device, edit those values with the game closed.
- The DLL is not code-signed. Windows SmartScreen may warn when you first run the scripts; the
  `Unblock-File` step above is what clears that.
