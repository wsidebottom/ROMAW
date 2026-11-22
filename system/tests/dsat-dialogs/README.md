# DSAT dialog test application

This original 68K app calls the real SysError trap and supplies no DSAT resources.
It therefore exercises ROMAW's built-in edited tables and native C handlers.

Build on Windows or WSL with Python (no classic SDK or compiler required):

```powershell
python system/tests/dsat-dialogs/build.py
```

The output is `build/dsat-dialogs/DSAT Dialogs`, its required AppleDouble companion
`._DSAT Dialogs`, and `DSAT Dialogs.cmd`. Open the app with the rebuilt ROMAW or
run the generated launcher. Keep the app and AppleDouble sidecar together.
The launcher copies just these two files into your mapped Apps folder under
`DSAT Dialogs Test`, so resource forks work regardless of the host drive format.
It selects the test app's declared 32-bit, 4 MiB application partition and an
8 MiB system partition before initialization, avoiding an automatic relaunch.
The launcher prefers the isolated `build/syserr-port/stage/ROMAW-optimized.exe`
when present, otherwise `windows-binaries/bin/ROMAW.exe`; rebuild the launcher
if the preferred executable is built later.

The chooser offers fourteen previews: default error, numeric error description,
low memory, force quit, RAM parity, Code Fragment, Mixed Mode, stack collision,
three disk/module prompts and the three retained icon-only alerts. Cancel returns
to the chooser. Buttonless previews wait for a click/key before returning.
Quit, Restart, Force Quit and Shut Down exercise real native actions and end the
test application/process; relaunch to inspect another terminating dialog.

For repeatable single-dialog inspection:

```powershell
python system/tests/dsat-dialogs/build.py --show 1
python system/tests/dsat-dialogs/build.py --show 20002
```

These generate `DSAT Dialog 1` and `DSAT Dialog 20002` in the same output folder.
Other signed 16-bit SysError codes are supported with `--show`.

This tests the emulator's built-in error UI, not physical-Mac reset, a complete
System 7 module manager or full Carbon compatibility. Module-name fields remain
empty unless the guest provides the corresponding real module state.
