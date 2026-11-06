# SippingCloftware

SippingCloftware is a Windows replay clipper. It starts in the notification area.
New clips default to `Videos/SippingCloftware`. Clips go into the folder for the
app that spent the longest time fullscreen in the saved interval, or into
`Desktop`. Clip metadata is stored in `Metadata` within the library.
Click its tray icon to open Clips, Recording, and Settings. Closing the window
returns to the tray. Use **Exit** on the tray menu to stop recording and quit.

Build on Windows x64 with Visual Studio Desktop C++ tools, Windows SDK and CMake:

```powershell
./scripts/bootstrap.ps1
./scripts/build.ps1
```

Run `build/bin/Release/SippingCloftware.exe`. Pass `--show` to open the main window
immediately. The default shortcuts are **Ctrl+Shift+S** for the clip toolbar and
**Ctrl+Shift+R** to start or stop recording.

Use `--show --screen secondary --no-focus` to open the controls on a second
display without activating them. The HUD and application dialogs also use that
display. `--screen` accepts a Windows screen name as well as `secondary`.

Recording starts on launch when a capture source is available. In **Settings**,
you can turn that off or choose to launch SippingCloftware when you sign in.
The HUD's **Mic** and **Desktop Audio** switches control which buffered audio
goes into the clip you save.

Change bindings in **Settings → Global shortcuts**: focus a shortcut field,
press the new combination, then Tab to **Apply**. Inline feedback confirms the
active binding or explains a conflict. Clip/record hotkeys are temporarily
released while editing so they do not intercept the keys; leaving the app
restores the active bindings. Push-to-talk changes require recording to stop.

For background verification, a running app also accepts these commands without
opening its controls or changing the current recording profile:

```powershell
./build/bin/Release/SippingCloftware.exe --status --result C:/Temp/clipper-status.json
./build/bin/Release/SippingCloftware.exe --save-clip 10 --result C:/Temp/clipper-save.json
```

The save command queues the latest requested seconds from the live replay buffer
through the normal original-save queue. Its JSON result identifies the persistent
job; it does not enable compression or override the recording settings.

Bootstrap uses Python only to acquire Qt development libraries. Neither Python nor
Visual Studio is required by the distributed application. An existing matching
dependency cache can be supplied with `bootstrap.ps1 -CacheRoot <directory>`.

Dependencies are specified in `dependencies.lock.json`. Qt is dynamically linked.
FFmpeg includes GPL codecs; distribution must include the matching source and
licence notices. See `licenses/` and the forthcoming distribution report.

The current specification is in `spec/IMPLEMENTATION_PLAN.md`.
See the [implementation checklist](docs/IMPLEMENTATION_CHECKLIST.md) for release status.
