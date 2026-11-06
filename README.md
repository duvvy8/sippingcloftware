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

Recording starts on launch when a capture source is available. In **Settings**,
you can turn that off or choose to launch SippingCloftware when you sign in.
The HUD's **Mic** and **Desktop Audio** switches control which buffered audio
goes into the clip you save.

Bootstrap uses Python only to acquire Qt development libraries. Neither Python nor
Visual Studio is required by the distributed application. An existing matching
dependency cache can be supplied with `bootstrap.ps1 -CacheRoot <directory>`.

Dependencies are specified in `dependencies.lock.json`. Qt is dynamically linked.
FFmpeg includes GPL codecs; distribution must include the matching source and
licence notices. See `licenses/` and the forthcoming distribution report.

The current specification is in `spec/IMPLEMENTATION_PLAN.md`.
See the [implementation checklist](docs/IMPLEMENTATION_CHECKLIST.md) for release status.
