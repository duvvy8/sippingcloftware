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

`scripts/build.ps1` produces a native Release build using repository `.deps`,
stages Qt and FFmpeg DLLs, executables and fonts, and runs CTest. The dependency
root can also be supplied explicitly with `-DependencyRoot`.

The latest completed core test run passes filename sanitization (including Unicode
and DOS reserved names), atomic JSON persistence, monotonic clock, catalogue
filtering, libx264 encoding/flush, failed-process handling, segment retention and
pinning, partial buffers, fixed endpoints, multiple pins, delayed completion and
byte eviction. Raw logs are in the ignored `build/Testing/Temporary/LastTest.log`.
The test DLL deployment error found in the first run was corrected; tests now
have an explicit 120-second timeout.

## Encoder discovery (H01–H09)

Initial discovery included 120 candidates; further image filtering leaves 101 video
implementations, 46 of which produced synthetic output on this host. These counts describe
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
is unavailable` during input. An OS screenshot captured other desktop content
rather than the app; its owning process was not identified. Qt integration
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

Remaining capture/audio reliability matrix, wider codec controls/configurations,
representative gaming quality and resource/game performance, destination paste,
native popup presentation, installer and installed-app verification remain open.
The goal remains active.

## Editing milestone

All five CTest targets pass: core (13 checks), media (8 checks), and native UI (8 checks each at 100/150/200%). Media checks cover exact and keyframe-rounded fast trimming of a B-frame source, full decode, crop dimensions, portrait fitting with black bars, selected-track gain ratio, muted sharing mix, cancellation cleanup, unchanged SHA256 originals, and two serial immutable interval saves with retained audio. Native UI tests export an edited file and verify editor/player destruction at all three scales. The 200% editor render was visually inspected; export/cancel controls remain visible.

Fast trim initially failed duration validation because command-line timestamp shifting retained extra footage. Packet-level remux now explicitly selects the disclosed GOP interval, rebases timestamps and validates the complete output. No duration tolerance was weakened.

## Resume verification, 2026-11-04

The Release build and all five existing CTest targets passed after the PC restart.
Core now has 18 passing checks, including active-segment byte reservation,
contiguous available duration, refusal to shorten an unavailable endpoint, replay
session recovery and preservation of a file with a substituted identity. Media
has nine passing checks including HDR mastering/CLL metadata preservation through
an edit and explicit tone-mapped BT.709 output with HDR metadata removed.

HDR conversion has numerical half-float/scRGB/PQ tests and actual 10-bit software
encode/decode checks. Prior synthetic probes also decoded 10-bit NVIDIA H.264,
HEVC and AV1, and AMD HEVC on this machine. These do **not** verify real HDR
monitor capture, HDR presentation, Intel hardware, Windows 11, or visual HDR
accuracy. Both attached displays are SDR.

Compression implementation and tests are in progress and are not yet a verified
user workflow. Installer and persistent interactive notifications remain required.

## Compression milestone, 2026-11-04

Six CTest targets pass: core (18 checks), media (9), compression (12), and native
UI at 100%, 150% and 200% (9 each). Compression checks cover the ordered resolution
and frame-rate policy, no upscale/increase for a 240x426/12fps source, fifth-percentile
outlier rejection, exact 20,000,000-byte handling, complete H.264/MP4 compression,
severe degradation rejection, a retained-audio budget failure, noisy >20MB FFV1
constraint failure, cancellation during encoding, temporary cleanup, and a real
serial save-and-compress job. Native UI tests click the library Auto Compress button.

The H.264 success fixture adds validly ignored padding to cross the byte boundary;
it proves the size gate and complete encode/score/validate workflow, **not** that
difficult gaming content can be reduced successfully. The lossless rejection
fixture contains actual noisy encoded frames above 20MB. Representative gaming
visual checks (HUD, foliage, particles, dark scenes and motion), wider codecs,
HDR scoring and full ladder encodes remain open.

Acceptance compares actual finalized bytes, source codec/container, display aspect,
colour fields/available HDR metadata, intended audio stream descriptions and exact
SHA256 hashes of retained audio packet payloads. Selected output undergoes full
decoding and every-frame VMAF at the original display resolution/frame rate. Both
HDR scoring inputs use the same controlled SDR transform; actual output remains HDR.
This HDR scoring path is implemented but not yet runtime verified.

Post-restart native WGC and DXGI monitor capture saved 1280x720/60 H.264 files with
zero dropped frames/backlog and unchanged pinned endpoints. WGC duration was 3s;
DXGI was 2.983333s, within the required one-frame rounding tolerance. The hidden
window fixture was unavailable for WGC window capture in this run (first not ready,
then hidden-window E_INVALIDARG); prior visible-window evidence remains separate.
Fresh discovery contains 101 video implementations after image filtering, 46 with
successful synthetic output on this host. Raw video variants remain export-only.

## Windows integration milestone, 2026-11-04

Release build and all seven CTest targets pass. Counts: core 18, media 9,
compression 14, Windows notification/actions 14 passed plus **one explicit skip**,
and native UI 10 at each scale factor. The skipped check is native toast popup/
history delivery: this user has Windows global notifications disabled
(`ToastEnabled=0`). That preference was inspected read-only and remains unchanged.
The app always mirrors completed jobs and their actions in the Clips window.

Tests use a separate native process and message-only window to hold the clipboard.
Transient contention retries successfully; persistent contention through a complete
compression retains both files, reports the copy failure accurately, offers Retry
Copy and omits the successful-copy/deletion question. Retry then copies the saved
output and makes the appropriate decision available. Same-process clipboard locking
was rejected as inadequate evidence because Windows allowed the other thread to copy.

Native COM activation, strict UUID/action parsing and single-instance delivery pass.
A test registers the actual clipper executable, asks COM to activate it while stopped,
invokes No for a persisted job, verifies both identities, checks that no window became
visible, exits the test instance, and unregisters its test shell registration. This
proves process-restart activation without requiring a visible main window. Clicking
an actual delivered Windows toast remains unverified with system notifications off.

Recycle tests move disposable fixtures into the real Windows Recycle Bin using the
Shell's explicit RecycleItem API. The original is identity-locked and moved by handle
to a persisted staging location first. No permanent-delete API is used as a fallback.
Tests verify repeated Yes, No surviving restart, stale replacement rejection, unchanged
compressed identities, recovery of an interrupted stage, and retention when an open
handle prevents staging. A Recycle Bin service/volume failure after staging remains
untested. Native UI tests also create a real compressed file and click the in-app No
action at 100%, 150% and 200%, retaining both files and disabling further decisions.

Per-user shell registration now supplies Start Menu AUMID/activator properties,
LocalServer32 and Windows display metadata. First-use Windows 10 settings lookup can
return ERROR_NOT_FOUND before an app's first toast; the app permits submission in
that specific case and preserves other errors. API submission is not represented as
proof that Windows presented a popup.


## Capture and source reliability milestone, 2026-11-04

All seven CTest targets pass: core 23 checks, media 9, compression 16,
notification/actions 14 passed with the same explicit native-delivery skip,
and native UI 13 each at 100%, 150% and 200%. The current Release build has no
MSVC compiler warnings in its latest build log.

Stable monitor interface identity takes precedence over a saved enumeration index.
Missing displays stay unavailable instead of selecting a different screen. Windows
must be explicitly reselected after app restart; current PID/executable/process
creation identity is checked before capture. Audio process sources retain the same
identity, detect parent/child route overlap and stop if the process exits.
Native process identity/ancestry, stale identity, missing-source and saved-settings
cases pass. Physical monitor reconnection and Windows 11 process loopback remain
untested.

WASAPI endpoint notifications are registered on a dedicated MTA thread. Native
callbacks compare immutable opaque IDs and set atomic flags only. Default endpoint
changes disclose continued use of the selected device; device removal invalidates
capture, and a reconnect does not silently resume it. The event-policy tests pass.
A new real 3-second WGC/NVENC/system-audio save has two fully decoded AAC tracks,
zero dropped frames/backlog/discontinuities and an unchanged endpoint. Physical
unplugging and changing the user's default devices were not performed.

The native UI tests now capture a real changing window, open the toolbar, delay
selection, save through its button, and destroy the source window. Capture stops
with an explicit error while the previously selected interval saves and fully
validates. Both fixed endpoint and duration tolerance are checked at all scales.
The WGC Closed callback uses revocation and shared state to avoid a dangling
capture-object pointer during teardown.

Live restart tests confirm No retains the running 30fps capture and epoch, while
Yes restarts at the explicitly selected 15fps. Actual capture-path calibration
writes on the selected replay drive, decodes each completed segment, and keys the
recommendation to source identity/geometry, audio, recording settings and storage
path. A matching automatic configuration starts; changing its profile is refused.

Additional compression tests pass for synthetic HDR HEVC/MP4 with mastering/CLL
metadata and exact retained audio packet payloads, identical controlled SDR scoring
transforms, and anamorphic display-aspect preservation. These verify software
pipelines, not real HDR monitor capture or gaming visual quality.

## Native clipping HUD

On Windows 11 build 26200, the desktop compositor rendered the toolbar's live
Acrylic backdrop over a coloured test window. A separate 38% dim layer rendered
below the toolbar. In a desktop screen grab with capture exclusion enabled, both
windows were absent. This check used a controlled test window. Live gameplay and
recorded WGC monitor pixel verification remain open.

The Release build passed core, media, compression, and all three UI scale
targets. The UI tests verified the moving selection, toolbar width and bounds,
Auto Compress arming without queuing, and a real fixed-endpoint save at 15 fps.
The notification suite failed its existing external restart-activation test with
COM error `0x80040154`.

The HUD uses the existing pinned replay interval and export queue. Its Auto
Compress toggle now arms the next save. The queue still uses the existing
same-codec compression policy. Cross-format share profiles remain a separate
implementation milestone.

## SippingCloftware tray and compact toolbar

The Windows 11 compositor check used a bright gradient window behind the HUD.
The tinted Acrylic rail rendered charcoal and remained legible when the preset
panel was open. Both surfaces were checked in the running Qt application.

An isolated Release process started with no visible main window. A posted
Windows hotkey message opened and dismissed the HUD while the controls remained
hidden. Opening the controls on demand, closing them back to the tray, and
exiting the process all passed. The UI suite covers the visible preset panel,
capture affinity, and the highlight settling on the selected row's bounds.

## Audio switches, startup, and RAM spot check

The export regression uses tagged replay audio tracks. All four switch
combinations produced the expected track selection and preserved the replay
segment byte for byte. The WGC toolbar integration also verifies that the
Mic switch is snapshotted into the job. Physical microphone audio quality
requires a live microphone check.

On Windows 11, the Release tray averaged 57.6 MiB resident and 37.9 MiB
private with recording off. The 720p60 H.264 NVENC recording of a synthetic
window averaged 228.1 MiB resident and 518.9 MiB private over 24 samples.
Peak sampled resident memory was 234.2 MiB. The recording used WGC and
system audio. The samples exclude export and do not include GPU memory.

## Shared Acrylic toolbar and menus

The rail, quick presets and advanced option lists share the toolbar's native
window and capture exclusion. On Windows 11, a compositor check over a colored
test backdrop showed the same live tint in the rail and preset panel. Sampled
rail colors and the foreground window stayed identical when the preset panel
opened. UI tests cover opening and choosing an advanced item and saving a clip
from the shared toolbar.

The backdrop brush now follows the two rounded HUD surfaces. A live compositor
render over a gradient showed clear space around the open preset panel. The
DXGI pixel test kept a fixture pixel unchanged with the 38% dim layer visible.
The UI suite checks dismissal on Escape and application deactivation, and
checks that a successful save reports the finished clip's media details.

## PR #1 local Windows 10 verification — 2026-11-06

Validated v11exe's PR head `2270110` with the local compatibility fixes below.
Release x64 built with the pinned Qt/FFmpeg dependencies. All seven CTest targets
passed (75.19 seconds): core, media, compression, notifications, and native UI at
100%, 150%, and 200%. Logs: `test-output/pr1-build-tests.log` and the corresponding
core/media/compression/notifications/UI text reports. Core includes exact pixel
orientation tests for 0/90/180/270 degrees and immutable input preservation.

The initial translucent HUD could not obtain capture exclusion on this Windows
10 compositor and its error dialog blocked the toolbar test. It now recreates
the hidden HUD with an opaque dark surface, requires capture exclusion again,
and displays neither surface if exclusion still fails. Windows 11 can continue
using the original native backdrop. Local tests verify the HUD/menu capture
affinity and a real DXGI pixel remaining unchanged with the dimmer visible.

The second display is portrait. DXGI supplies an unrotated texture on that
display; capture now applies the reported rotation before emitting the desktop
frame. Native DXGI pixel verification and automatic-profile calibration pass on
that display, alongside a real WGC window capture and fixed-endpoint export.
The UI tests use that display, show without activating, and send Qt-local test
events without moving the system pointer. `--screen secondary --no-focus` provides
the same placement/focus behavior for the running application. The main controls
scroll when their contents exceed the available window size.

Notification COM delivery and restart activation pass on this host. Native toast
popup/history remains explicitly skipped because the user's Windows notification
setting is disabled. Existing shell registrations were backed up and restored
around the tests. Physical HDR/Intel/microphone/Windows 11 support was not newly
verified here. This PR sync is not completion of the full implementation plan.

## Desktop save and shortcut fixes — 2026-11-06

The requested test was changed from game capture to main-screen desktop capture.
The running Release app saved a fixed ten-second interval through its persistent
original-save queue using the user's AV1 NVENC, High, source-resolution, 60 fps
profile. Recording settings, audio devices, gains, and saved shortcuts were
compared with the pre-change settings snapshot and retained.

Verified desktop original:
`C:/Users/Michael/Videos/SippingCloftware/Desktop/2026-11-06_19-21-25-503_29f585d4.mp4`.
FFprobe reports 2560×1440 AV1, 60/1 fps, 600 frames, exactly 10.000 seconds,
17,235,982 bytes, and a 48 kHz stereo AAC stream of the same duration. An
independent full FFmpeg video/audio decode completed with exit 0. A decoded frame
shows the moving verification window and readable technical text within the
actual desktop. Recording stats at submission: 3,360 encoded frames, zero queue
drops, zero backlog, no capture error. Captured-source frames and encoded output
frames are distinct counters; this does not certify 60 unique source frames per
second under arbitrary desktop or game load. SHA256 stayed unchanged during
validation; see `test-output/desktop-clip-sha256.txt`. Originals were retained.

The real test exposed two defects that were fixed. A display change from
2150×1440 to 2560×1440 discarded valid encoder tests. Cache loading now proves
that the driver/dependency/CPU/probe identity still matches, retains synthetic
encoder results, and removes capture calibration/live recommendations when the
display configuration differs. A changed driver still rejects the cache. The
second defect counted startup storage work against the recording backlog: storage
and the first encoder now initialize before the media epoch starts, with fresh
capture input at that boundary. The two-second runtime backlog guard remains.

Shortcut fields now have accessible names, inline active/conflict feedback and
native registration suspension while editing, so the app's global shortcuts do
not intercept the sequence being entered. Leaving the app restores active
registrations. Apply validates conflicts, saves the preference, and retains the
previous preference on failure. Ordinary navigation/punctuation/keypad keys are
mapped explicitly; keypad variants that RegisterHotKey cannot distinguish are
rejected. Tests exercise edit/apply/persistence, external conflicts, empty input,
own-action duplicates, native unregister/reregister, and restoring on application
deactivation. Physical global key delivery through the desktop automation helper
was not verified; its activation/input operations failed, so no such pass is
claimed. Existing Ctrl+Shift+S/R bindings remain unchanged.

After the capture/cache/shortcut changes, all seven CTest targets passed in
58.29 seconds: 27 core cases, media, compression, notifications, and 15 native
UI cases at each of 100/150/200% scaling. The final keypad-ambiguity guard was
then rebuilt and its focused core and shortcut UI regressions passed again.
Logs: `test-output/game-save-build-tests.log`, `desktop-final-build.log`,
`desktop-final-core.txt`, and `desktop-final-ui.txt`. The capture fixture was
rebuilt separately and exercised in the actual desktop recording.

**System audio is not certified by this test.** The saved mixed stream decodes
and contains nonzero audio, but the expected 440 Hz system tone was absent. A
test source was explicitly mapped to the configured USB headset endpoint;
waveform rendering returned success and an advancing playback position. Direct
loopback snapshots at 50 and 250 ms also did not establish the tone. Protected
video playback was active; Windows documents restrictions on
[protected-stream loopback capture](https://learn.microsoft.com/en-us/windows/win32/coreaudio/loopback-recording),
but that has not been isolated as the cause. The user requested finishing without
further audio testing. No audio settings or capture timing were changed to force
a pass. Physical microphone quality and long-duration drift remain unverified.
Test source/tone processes were stopped. Native toast popup/history remains
skipped because Windows notifications are disabled.

Checklist mapping: H07 (cache identity), C01/C05/C06 (desktop capture, startup
timing, fixed original save), W01 (editable/persisted shortcuts), A01/A03/A05
(explicit audio verification limit). This scoped repair does not complete the
remaining implementation/packaging requirements.

The final command-client check also caught a startup race: a status-only process
could briefly own the primary lock and make a simultaneously starting app exit.
Status/save clients now only connect to the user-local server. An immediate query
during final startup succeeded, and a subsequent query confirmed active AV1 NVENC
desktop recording with no capture error. The final settings comparison retained
the original recording profile, audio source configuration and shortcut values.
