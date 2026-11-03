# Implementation and verification checklist

Source of truth: `spec/IMPLEMENTATION_PLAN.md` (read completely before edits).
Status: **Pending**, **Implemented / untested**, **Tested**, **Blocked**.
An implemented component is not a verified workflow. Hardware claims require runtime evidence.
No release or goal completion until all mandatory implementation is finished.

## 1. Reproducible build and dependencies
| ID | Requirement | Status / evidence |
|---|---|---|
| B01 | Review existing repository and experimental starter | Tested: repository empty, no commits or remote refs; adjacent frame contains one incomplete header and inconsistent build scripts; do not reuse architecture. |
| B02 | C++20, CMake, MSVC, native x64; no global AVX2 requirement | Tested: CMake Release x64 builds with MSVC 19.51, C++20; no /arch:AVX2 flag. |
| B03 | Qt 6 Widgets; FFmpeg libraries; Windows APIs; no Python runtime | Implemented / partially tested: Qt 6.10.0 Widgets, FFmpeg 63 libraries, native Windows APIs; dependency-staged native executable runs. Full app workflows pending. |
| B04 | Pinned dependencies, reproducible bootstrap/build, source and licence notices | Implemented / untested bootstrap: dependencies.lock.json and scripts/bootstrap.ps1; font hashes verified; FFmpeg archive SHA256 matches. Complete source/licence distribution pending. |
| B05 | Bundle Inter and Geist Mono; no name, brand or logo | Implemented / partially tested: both font files and OFL notices bundled, startup registers fonts; UI consistency pending. |
| B06 | Release dependency staging; per-user installer and uninstall | Pending |
| B07 | Windows 10 22H2 and Windows 11 support | Pending; host Windows 10 19045, AMD iGPU + NVIDIA RTX 5070 Ti |

## 2. Hardware and encoder discovery
| ID | Requirement | Status / evidence |
|---|---|---|
| H01 | Enumerate adapters and attached displays, capture adapter affinity | Tested discovery: AMD Radeon iGPU, NVIDIA RTX 5070 Ti; 2560x1440@240 and 1080x1920@144 SDR displays. Capture opens matching display adapter; calibration pending. |
| H02 | Complete FFmpeg video encoder catalogue; exclude still-image/unsuitable streams | Tested: 120 video encoders discovered through av_codec_iterate; catalogue test excludes image formats and retains FFV1. |
| H03 | Separate build inclusion, hardware match, initialization, encoded-frame verification | Tested: isolated child probes; 48 successful packet-output tests / 120 catalogue records. Absent hardware and initialization failures retain reasons. |
| H04 | Live/export classification, reasons, identity/device/pixel formats/rate controls | Implemented / partially tested: catalogue records implementation, queried pixel formats and private controls, hardware flags and detailed reasons. Live recommendation deliberately gated on calibration. |
| H05 | Codec/container compatibility, tested configurations, HDR capability | Implemented / partially tested: avformat_query_codec compatibility, exact 640x360@60 synthetic configurations recorded. HDR probes pending. |
| H06 | Synthetic tests and capture-path calibration before recommendations | Partial: synthetic initialization/flush/output tests pass; real capture calibration pending. |
| H07 | Driver/dependency keyed cache; never advertise untested maxima | Implemented / partially tested: SHA256 cache key includes DXGI driver IDs, dependencies/configuration and probe policy. No guessed maxima advertised. |
| H08 | Prefer verified hardware H.264, expose HEVC/AV1 and practical software fallback | Partial: verified hardware H.264 selection gated on live calibration; software x264 test passes. Profiles/UI pending. |
| H09 | Individual encoder quality mappings and manual controls | Partial: encoder-specific mappings implemented in media.cpp; source quality/runtime coverage pending. |

## 3. Capture, audio, storage and saves
| ID | Requirement | Status / evidence |
|---|---|---|
| C01 | WGC/D3D11 window and monitor capture; DXGI monitor fallback | Tested WGC window capture and H.264 NVENC save at 1226x720/60 on this host; source image changes verified via 11 distinct hashes in 15 sampled frames. DXGI and device-loss paths remain untested. |
| C02 | Monotonic timestamps; retain source identity before toolbar focus | Pending |
| C03 | Disk ring: independent compressed ~2s segments, default 300s / 4 GiB | Implemented / untested: recorder and replay.cpp; independent ~2s Matroska segments, defaults 300s / 4 GiB. |
| C04 | Configurable storage/limits; duration and bytes whichever first, actual duration | Tested ring duration/byte limits and actual interval duration in core tests. Settings expose storage path, duration and byte limits. |
| C05 | Bounded queues; dropped frames/backlog and actionable performance guidance | Implemented / untested: bounded 16-frame capture queue, backlog/drop stats, stop when lag exceeds 2s; UI guidance pending. |
| C06 | Fixed toolbar endpoint; interval pinning; release/transfer pin ownership | Tested fixed and delayed endpoints, pin eviction protection, multiple pins and partial intervals; Qt toolbar Escape releases its pin. Queued toolbar interval integration still needs a full recording exercise. |
| C07 | Partial buffer disclosure; delayed packet coverage; immutable multiple queued saves | Partial: isolated smoke waits through a 700ms toolbar delay and exports exactly the selected three seconds. Serial persisted queue implemented; multiple queued real exports still need integration coverage. |
| C08 | Safe disk exhaustion/failure, preserve clips, crash recovery | Pending |
| C09 | Overlay exclusion, display changes, sleep/resume, device loss | Pending |
| C10 | No silent recording changes; restart/buffer reset requires approval | Pending |
| A01 | WASAPI system/device, microphone and supported per-process loopback | Implemented / untested: WASAPI endpoint capture and process-loopback activation. Host build 19045 does not support per-application loopback (requires >=20348). |
| A02 | Source enable, gain, mute, configurable microphone push-to-talk | Implemented / untested: source configuration includes enable/gain/mute/PTT; UI configuration pending. |
| A03 | First compatibility mix plus optional separate source tracks | Partially tested: real system capture exported first compatibility mix and a separate track; both streams fully decoded. Multi-source microphone routing remains untested. |
| A04 | Prevent overlapping system/application duplicate capture; explain conflicts | Implemented / untested: duplicate source and system/application overlap rejection. |
| A05 | Common clock, resampling and drift correction | Partially tested: real QPC-anchored system capture produced 144,000 non-silent samples for a 3s saved interval, RMS -20.26 dB. Long drift remains untested. |
| A06 | Device removal/default changes explicit; no silent source replacement | Partial: chosen endpoint IDs and failure stops capture without source replacement; explicit default-device change notification pending. |
| A07 | Unsupported per-process loopback labelled device fallback | Pending |

## 4. Toolbar, shortcuts, library, folders
| ID | Requirement | Status / evidence |
|---|---|---|
| U01 | Charcoal, accessible contrast, active green/error red, restrained selection | Implemented / partially tested: charcoal Qt stylesheet, keyboard focus, green/red status labels; Clips snapshots visually inspected at three scale factors. Full contrast audit pending. |
| U02 | Fonts on all app surfaces; numeric/technical data Geist Mono | Partial: bundled Inter font family resolved from loaded file; Qt test checks actual font resolution; numeric metadata uses Geist Mono. Remaining mixed-content text audit pending. |
| U03 | Keyboard navigation and usable 100/150/200% display scaling | Partially tested: native Qt integration tests and rendered Clips layouts at QT_SCALE_FACTOR 1, 1.5 and 2; no clipped primary controls. Desktop helper screenshot/input geometry failed over a full-screen game, so live overlay interaction remains unverified. |
| U04 | Clips / Recording / Settings; tray close, explicit Exit stops, initially off | Partially tested: Clips/Recording/Settings, tray, close hides and explicit Exit. Initial recording/startup disabled. Real sign-in startup and exit while recording still untested. |
| U05 | Hidden toolbar, vertical left of active display, no full-screen dimming | Implemented / partially tested: hidden vertical toolbar, left active-display placement, no dimming, Windows exclusion API succeeds in Qt UI test. Live monitor exclusion verification pending. |
| U06 | Ordered record/container/fps/resolution/duration/compress/save/folder controls | Partial: start-stop/container/fps/resolution/duration/save/folder connected. Auto Compress awaits sequence step 7; not represented as complete. |
| U07 | Duration presets/custom; source/lower fps including 15/30/60/120 | Implemented / partially tested: toolbar presets and custom duration, source fps plus 120/60/30/15; unavailable higher choices disabled with reasons. |
| U08 | Applicable source/lower resolutions and compatible MP4/MKV/WebM/MOV | Implemented / partially tested: applicable lower export resolutions and compatible containers; retrospective higher dimensions disabled. Wider codec/configuration verification pending. |
| U09 | Four quality presets; adjacent advanced codec/encoder panels/tooltips | Partial: four quality presets and adjacent codec/implementation selector connected. Separate codec grouping and additional encoder controls still need refinement. |
| U10 | Escape dismisses; save/compress immediately dismiss and run in background | Partially tested: Escape hides; Save transfers ownership to serial queue and hides. Full toolbar-save and later compression integration pending. |
| U11 | Recording/export settings distinct; impossible retrospective choices explained | Implemented / partially tested: recording/export snapshots separated; restart approval dialog and explicit incompatibility errors. Approval interaction test pending. |
| W01 | Default Ctrl+Shift+S/R, configurable shortcuts and PTT; retain old on conflict | Partially tested: native global shortcut registration; conflict test confirms previous binding remains registered. Configurable toolbar/record/PTT bindings; actual global key delivery still untested. |
| W02 | Videos/Clips/application/timestamp, root and executable folder overrides | Implemented / partially tested: default Videos/Clips, executable-based folders, root/override settings, Unicode sanitization and unique paths. Installed folder integration pending. |
| W03 | Safe unique Unicode names, executable identity, Desktop fallback | Implemented / partially tested: Win32 executable identity before focus, Desktop fallback, Unicode and reserved-name tests. Focus/override integration pending. |
| L01 | Thumbnail/app/date/duration/dimensions/fps/codec/container/size metadata | Partially tested: indexed metadata displayed, generated media imported, on-demand thumbnail produced, Clips snapshots checked. |
| L02 | Original/compressed/edited relation; audio-track count and HDR status | Implemented / partially tested: metadata includes relationship, track count and HDR; relation flows for edits/compression pending. |
| L03 | Application filter/search; lazy cached thumbnails, no continuous scanning | Implemented / partially tested: app filtering/search, cached lazy thumbnails and explicit/event-based scans; idle resource measurement pending. |
| L04 | Play/show folder/copy/rename/trim/crop/audio/compress actions | Partial: Play/Folder/Copy/Rename implemented; Qt tests exercise play and actual CF_HDROP data. Trim/crop/audio/compress follow later sequence steps. |
| L05 | Rebuildable local index/sidecars; recover external rename/delete | Tested: external rename preserves Windows file identity and rebuilt index finds renamed media; deletion recovery untested. |

## 5. Playback and nondestructive editing
| ID | Requirement | Status / evidence |
|---|---|---|
| E01 | On-demand playback/preview; release decoders on close | Tested Qt playback opens generated clip and releases the media player/decoder on dialog close at all three scale factors. |
| E02 | New outputs, original untouched, resulting duration shown | Pending |
| E03 | Packet-copy fast trim with keyframe rounding disclosure | Pending |
| E04 | Exact trim re-encodes selected boundaries | Pending |
| E05 | Free visible crop, 16:9/9:16/1:1; default fit entire image | Pending |
| E06 | Audio track selection/gain/mute/mixed sharing track | Pending |

## 6. HDR
| ID | Requirement | Status / evidence |
|---|---|---|
| D01 | Float capture pipeline and supported 10-bit encode | Pending |
| D02 | Preserve primaries/transfer/matrix/range/available metadata | Pending |
| D03 | Preserve HDR and explicit tone-mapped SDR copy; never relabel/convert silently | Pending |
| D04 | Unsupported combinations disabled with reasons | Pending |

## 7. Compression, jobs, cancellation
| ID | Requirement | Status / evidence |
|---|---|---|
| J01 | Persist immutable jobs and exact specified state transitions | Partial: serial immutable save jobs persisted with crash recovery; compression state machine later in sequence. |
| J02 | Serial export queue below capture priority; progress/settings/cancel | Partial: one below-normal-priority export worker and FFmpeg child; save cancellation implemented, detailed progress/compression cancellation pending. |
| J03 | Persist preferences/audio/shortcuts/folders/cache/relations/actions | Partial: settings/audio/shortcuts/folders/cache/save jobs and sidecar relationships persisted; outstanding notification actions later. |
| J04 | Crash/restart marks interrupted jobs; retains originals, no false success | Tested persistence recovery: interrupted job becomes Failed and original bytes remain untouched. Process-crash integration pending. |
| Q01 | Save/finalize/validate original first; retain unless explicitly recycled | Pending |
| Q02 | <=20,000,000 bytes; 19.5MB internal target; no shortening or recording changes | Pending |
| Q03 | Preserve codec/container/aspect/colour/intended tracks; same-codec implementation allowed | Pending |
| Q04 | Already small: copy only original, no duplicate or deletion prompt | Pending |
| Q05 | Every candidate from original; retained audio budget / container allowance | Pending |
| Q06 | Original dimensions/fps with stronger efficiency/strength first | Pending |
| Q07 | >60fps then 60, lower resolution ladder, then 30/24/20/15 with reconsideration | Pending |
| Q08 | Aspect/alignment, no upscale/fps increase; retain below 480p/15fps | Pending |
| Q09 | Representative high-motion sample shortlist and full VMAF mean>=90/p5>=80 | Pending |
| Q10 | Consistent display resolution and identical HDR SDR comparison transforms | Pending |
| Q11 | Full decode, codec/container/tracks/colour/size and duration tolerance validation | Pending |
| Q12 | Distinct final output, rejected temp cleanup, cancellation retains original | Pending |
| Q13 | Specific failures incl audio budget/lossless; no mathematical impossibility claim | Pending |
| Q14 | Visual development checks: HUD/foliage/particles/dark scenes/motion | Pending |

## 8. Clipboard and persistent interactive notifications
| ID | Requirement | Status / evidence |
|---|---|---|
| I01 | Real CF_HDROP file clipboard, contention retries, file stays on disk | Partially tested: CF_HDROP and Preferred DropEffect are written; native Qt integration reads actual file data from clipboard. Contention and destination paste tests pending. |
| I02 | Clipboard failure retains files, accurate message, Retry Copy, no success toast | Pending |
| I03 | Exact size/quality failure message, limiting reason in app; accurate other errors | Pending |
| I04 | Successful copy exact deletion question with Yes/No; already-small no deletion | Pending |
| I05 | Persisted job IDs resolve actions; body opens corresponding clip | Pending |
| I06 | Yes recycles corresponding identity-verified original only, never permanently deletes | Pending |
| I07 | No/dismiss/ignore retains; repeated/stale actions harmless; compressed intact | Pending |
| I08 | Restart activation works without needless main window, in-app suppressed fallback | Pending |

## 9. Reliability, performance and delivery gates
| ID | Requirement | Status / evidence |
|---|---|---|
| T01 | Capture/buffer test matrix in specification | Partial: core replay endpoint, partial, delayed completion, pinning, duration/byte tests pass; remaining capture matrix open. |
| T02 | Compatibility test matrix, distinguish verified/untested hardware | Pending |
| T03 | Audio test matrix including long drift and removal | Pending |
| T04 | Editing/compression test matrix incl byte boundaries and guard rejection | Pending |
| T05 | Windows integration test matrix incl restart/stale/recycle failure | Partial: shortcut conflict preservation and CF_HDROP clipboard format pass; remaining integration matrix open. |
| T06 | Measure idle CPU/RAM and no recording/decoding/continuous thumbs | Pending |
| T07 | Measure capture CPU/GPU/encoder, disk/buffer, dropped/backlog, export speed | Pending |
| T08 | Representative resolution/fps and game-only versus recording frame-time baseline | Pending |
| T09 | Release build, clean dependency-staged run and installed usage exercise | Pending |
| T10 | Installer dependencies/fonts/notifications/shortcuts/uninstall without dev tools | Pending |
| T11 | Build/usage/troubleshooting, dependency sources/licences, mapped verification report | Pending |

## Work log
- 2026-11-03: Read entire specification. Inspected empty checkout and remote; reviewed incomplete adjacent starter. Verified MSVC installation and Windows SDK, existing Qt 6.10.0 and FFmpeg dependency downloads. No app features implemented yet.


- 2026-11-03: Release and four CTest targets pass (core plus native UI at 100/150/200%). Real WGC/NVENC 3s save, two-track non-silent system audio, fixed endpoint and changing frame content verified. Full-screen desktop test helper cannot obtain screenshot/input geometry; own Qt renders and integration tests used, limitation retained.

