# Verification report (work in progress)

This is an evidence log, not a completed-release claim. See the requirement IDs in
`IMPLEMENTATION_CHECKLIST.md`. Untested and unfinished requirements remain open.

## Environment

- Windows 10 Pro 22H2, build 19045, native x64.
- MSVC 19.51.36260, Windows SDK 10.0.28000.0, Qt 6.10.0.
- FFmpeg N-128352-gec887b13a8-20261103, shared GPL build, libvmaf enabled.
- AMD Radeon integrated graphics, NVIDIA GeForce RTX 5070 Ti.
- Attached SDR displays: 2560×1440 at 240 Hz; 1080×1920 at 144 Hz.
- No Intel GPU, Windows 11 machine or active HDR display is available here.

## Build and core tests

`scripts/build.ps1 -DependencyRoot ../frame/.deps` produces a native Release build,
stages Qt and FFmpeg DLLs, executables and fonts, and runs CTest. The dependency
root is an explicit local cache for this session; the default is repository `.deps`.

The latest completed core test run passes filename sanitization (including Unicode
and DOS reserved names), atomic JSON persistence, monotonic clock, catalogue
filtering, libx264 encoding/flush, failed-process handling, segment retention and
pinning, partial buffers, fixed endpoints, multiple pins, delayed completion and
byte eviction. Raw logs are in the ignored `build/Testing/Temporary/LastTest.log`.
The test DLL deployment error found in the first run was corrected; tests now
have an explicit 120-second timeout.

## Encoder discovery (H01–H09)

An isolated runtime probe enumerated 120 suitable video encoder implementations;
48 produced packets from synthetic 640×360, 60 fps input. These counts describe
this distributed FFmpeg build and this host, not general hardware support.

- NVIDIA H.264, HEVC and AV1: initialized and emitted synthetic packets.
- AMD H.264 and HEVC: initialized and emitted synthetic packets.
- AMD AV1: initialization failed; remains unavailable with its error.
- Intel QSV: matching adapter absent; initialization was not attempted.
- CPU libx264: synthetic encode and delayed packet flush passed.

No maximum resolution or frame-rate claims are inferred from these tests. Live
recommendation remains gated on capture calibration. Full probe JSON is kept in
ignored `test-output/catalogue.json` and the local driver/dependency keyed cache.

## Capture development checks (C01–C10)

The first real WGC/NVENC recording saved and fully decoded an exactly 3-second,
1280×720 at 60 fps H.264 clip, with a fixed requested endpoint despite a 700 ms
delay before stopping. Counters exposed queue overflow on the 240 Hz source and
repeated old imagery; that run is a **failed capture-content check**, not a passed
capture workflow. Frame sampling before readback was corrected. A moving fixture
and a repeated smoke test are required before accepting the correction.

## Audio and unsupported hardware

The corrected WGC window-capture smoke saved a fully decodable, exactly 3-second
1226×720, 60 fps H.264 clip with two audio tracks, no queue overflow and no final
encode backlog. Its fixed endpoint was unchanged after a 700 ms delay. Fifteen
sampled frames at 5 fps contained eleven unique hashes, confirming changing image
content. The mix decoded to 144,000 samples, with measured RMS about −20.26 dB.
The initial WGC surface size adjustment is handled before recording commits its
configuration; a subsequent source resize stops capture explicitly.

This verifies short system-device capture and a separate audio track, not microphone
capture, PTT, long-term drift or all routing combinations. Per-application loopback
requires Windows build 20348 or later according to Microsoft's sample; this
19045 host must expose clearly labelled device capture instead. No unsupported
hardware path is represented as verified.

## Native UI and Windows integration

All four CTest targets pass: core, native Qt UI at scale factors 1, 1.5 and 2.
The UI tests exercise navigation, actual bundled Inter font resolution, numeric
Geist Mono, toolbar Escape dismissal and empty-buffer save disabling, generated
media indexing and thumbnail production, playback and decoder destruction, actual
CF_HDROP file data, external rename recovery, and closing the main window to hide.
The three directly rendered Clips layouts were visually inspected with no clipped
primary controls. Core tests also confirm hotkey conflict preservation and recovery
of an interrupted persisted job without changing its original file bytes.

The Computer Use helper read the app's accessibility tree but repeatedly failed
screen capture (`window capture timed out`) and returned `coordinate input geometry
is unavailable` during input. An OS screenshot captured the occluding full-screen
game rather than the app. No interaction with that game was performed. Qt integration
tests and app-owned widget rendering were used for verification. Actual interactive
overlay placement/exclusion over a game remains unverified.

## Official references consulted

- [Qt 6.10 supported platforms](https://doc.qt.io/qt-6.10/supported-platforms.html)
- [Qt Windows deployment](https://doc.qt.io/qt-6/windows-deployment.html)
- [Windows Graphics Capture and floating-point HDR surfaces](https://learn.microsoft.com/en-us/windows/apps/develop/media-authoring-processing/screen-capture)
- [WASAPI activation and linkage](https://learn.microsoft.com/en-us/windows/win32/api/mmdeviceapi/nf-mmdeviceapi-activateaudiointerfaceasync)
- [Application loopback sample and OS requirement](https://learn.microsoft.com/en-us/samples/microsoft/windows-classic-samples/applicationloopbackaudio-sample/)
- [FFmpeg build source and platform policy](https://github.com/BtbN/FFmpeg-Builds)

## Release gates still open

Remaining capture/audio reliability, full toolbar-save integration, editing, HDR, compression and VMAF,
clipboard, interactive persistent notifications, resource/game performance,
installer and installed-app verification are unfinished. The goal remains active.
