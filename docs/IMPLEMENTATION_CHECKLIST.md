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
| B04 | Pinned dependencies, reproducible bootstrap/build, source and licence notices | Partially tested: pinned lock/bootstrap and default local .deps build; bootstrap -CacheRoot successfully copied Qt/FFmpeg and verified font hashes. Canonical FFmpeg download is now a dated archive with a pinned hash; clean network bootstrap/source notices remain pending. |
| B05 | Bundle Inter and Geist Mono; no name, brand or logo | Implemented / partially tested: both font files and OFL notices bundled, startup registers fonts; UI consistency pending. |
| B06 | Release dependency staging; per-user installer and uninstall | Pending |
| B07 | Windows 10 22H2 and Windows 11 support | Pending; host Windows 10 19045, AMD iGPU + NVIDIA RTX 5070 Ti |

## 2. Hardware and encoder discovery
| ID | Requirement | Status / evidence |
|---|---|---|
| H01 | Enumerate adapters and attached displays, capture adapter affinity | Tested discovery: AMD Radeon iGPU, NVIDIA RTX 5070 Ti; 2560x1440@240 and 1080x1920@144 SDR displays. Capture opens matching display adapter; calibration pending. |
| H02 | Complete FFmpeg video encoder catalogue; exclude still-image/unsuitable streams | Partially tested: 101 video implementations remain after still-image exclusion; raw streams retained as export-only. Catalogue regression tests pass; wider specialist configuration tests remain open. |
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
| C01 | WGC/D3D11 window and monitor capture; DXGI monitor fallback | Partially tested: prior WGC window save/changing-content verification; after restart WGC and DXGI monitor saves at 1280x720/60, zero dropped/backlog, fixed 3s intervals, completely decoded. DXGI output 2.983333s within one-frame tolerance; WGC 3s. Device-loss paths remain untested. |
| C02 | Monotonic timestamps; retain source identity before toolbar focus | Pending |
| C03 | Disk ring: independent compressed ~2s segments, default 300s / 4 GiB | Implemented / untested: recorder and replay.cpp; independent ~2s Matroska segments, defaults 300s / 4 GiB. |
| C04 | Configurable storage/limits; duration and bytes whichever first, actual duration | Tested ring duration/byte limits and actual interval duration in core tests. Settings expose storage path, duration and byte limits. |
| C05 | Bounded queues; dropped frames/backlog and actionable performance guidance | Implemented / untested: bounded 16-frame capture queue, backlog/drop stats, stop when lag exceeds 2s; UI guidance pending. |
| C06 | Fixed toolbar endpoint; interval pinning; release/transfer pin ownership | Tested fixed and delayed endpoints, pin eviction protection, multiple pins and partial intervals; Qt toolbar Escape releases its pin. Queued toolbar interval integration still needs a full recording exercise. |
| C07 | Partial buffer disclosure; delayed packet coverage; immutable multiple queued saves | Partially tested: real fixed endpoint with 700ms delay; media integration test queues two distinct saves and verifies requested 2s/3s durations, retained tracks, full decoding and immutable 30fps settings after caller mutation. Hardware delayed-packet stress remains open. |
| C08 | Safe disk exhaustion/failure, preserve clips, crash recovery | Partially tested: replay session journal, process-creation ownership, guarded file-identity cleanup, active-segment byte reservations; recovery/substituted-file/limit tests pass. Actual forced disk exhaustion/process-crash exercise remains open. |
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
| U06 | Ordered record/container/fps/resolution/duration/compress/save/folder controls | Implemented / partially tested: required order including Auto Compress connected; empty-buffer buttons disabled in native UI tests. Actual recording-to-toolbar compression interaction remains open. |
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
| L04 | Play/show folder/copy/rename/trim/crop/audio/compress actions | Partially tested: native playback/edit/copy/rename workflows plus actual library Auto Compress button queues an already-small original and copies it, without a duplicate, at all three scales. Larger library compression cases remain open. |
| L05 | Rebuildable local index/sidecars; recover external rename/delete | Tested: external rename preserves Windows file identity and rebuilt index finds renamed media; deletion recovery untested. |

## 5. Playback and nondestructive editing
| ID | Requirement | Status / evidence |
|---|---|---|
| E01 | On-demand playback/preview; release decoders on close | Tested Qt playback opens generated clip and releases the media player/decoder on dialog close at all three scale factors. |
| E02 | New outputs, original untouched, resulting duration shown | Tested: edit output is distinct; SHA256 of original remains unchanged after exact/fast/crop/audio/mix/cancel workflows. Editor shows resulting duration before submission. |
| E03 | Packet-copy fast trim with keyframe rounding disclosure | Tested: packet-level remux rounds 2.3–6.7s to disclosed 2–8s keyframes on B-frame source; all tracks retained and output fully decoded. Failed FFmpeg timestamp-shift implementation was replaced, not tolerated. |
| E04 | Exact trim re-encodes selected boundaries | Tested: exact 2.3–6.7s export meets frame/audio rounding tolerance; selected track gain ratio is within tolerance of 0.5. |
| E05 | Free visible crop, 16:9/9:16/1:1; default fit entire image | Partially tested: free crop and visible numeric selection; 9:16 fit creates 100x180 output with black bars and complete source retained. 16:9/1:1 share the same tested helper; additional visual content validation remains pending. |
| E06 | Audio track selection/gain/mute/mixed sharing track | Tested: selected-track export, gain changes, mute and one mixed sharing track; actual silent mixed output verified. Editor preview decoder is destroyed on close at all scale factors. |

## 6. HDR
| ID | Requirement | Status / evidence |
|---|---|---|
| D01 | Float capture pipeline and supported 10-bit encode | Partially tested: WGC FP16/scRGB conversion, numerical half-float/PQ tests and actual 10-bit x265 decode pass. NVIDIA H.264/HEVC/AV1 and AMD HEVC synthetic 10-bit probes passed. Real HDR display capture untested. |
| D02 | Preserve primaries/transfer/matrix/range/available metadata | Partially tested: media integration preserves BT.2020/PQ, mastering and CLL metadata through an edit. Real capture metadata/hardware HDR output remain untested. |
| D03 | Preserve HDR and explicit tone-mapped SDR copy; never relabel/convert silently | Partially tested: explicit SDR edit produces BT.709 and removes HDR metadata; HDR originals retained. Real HDR visual assessment remains open. |
| D04 | Unsupported combinations disabled with reasons | Implemented / partially tested: capture checks HDR display and verified 10-bit encoder; explicit consent for SDR capture of HDR display. UI on real HDR hardware untested. |

## 7. Compression, jobs, cancellation
| ID | Requirement | Status / evidence |
|---|---|---|
| J01 | Persist immutable jobs and exact specified state transitions | Partially tested: immutable save/edit/compression jobs and named analysis/encode/validate/copy/terminal states persisted. Queued save-and-compress, original retention, clipboard failure/retry and restart actions pass; forced process-crash matrix remains open. |
| J02 | Serial export queue below capture priority; progress/settings/cancel | Partially tested: one below-normal export worker and child processes; editable progress and cancellation implemented. Multiple save jobs and edit cancellation cleanup pass. Compression path pending. |
| J03 | Persist preferences/audio/shortcuts/folders/cache/relations/actions | Partially tested: settings/audio/shortcuts/folders/cache/relationships plus notification decisions, copy retry and recycle transaction paths persisted; restart decision tests pass. Wider installed upgrade/recovery tests remain open. |
| J04 | Crash/restart marks interrupted jobs; retains originals, no false success | Tested persistence recovery: interrupted job becomes Failed and original bytes remain untouched. Process-crash integration pending. |
| Q01 | Save/finalize/validate original first; retain unless explicitly recycled | Partially tested: queued save-and-compress validates 3s original before analysis/copy, immutable 30fps snapshot and original retained. Larger capture-to-compression exercise remains open. |
| Q02 | <=20,000,000 bytes; 19.5MB internal target; no shortening or recording changes | Partially tested: actual finalized-byte validation, exact 20MB boundary and 20MB+1 padded source processed; unchanged original hash and duration checks pass. Representative gaming input remains open. |
| Q03 | Preserve codec/container/aspect/colour/intended tracks; same-codec implementation allowed | Partially tested: H.264/MP4 encode retains BT.709, display aspect, audio codec/layout/title and exact audio packet SHA256. Other codec/container/HDR combinations remain open. |
| Q04 | Already small: copy only original, no duplicate or deletion prompt | Tested through queue and native library action: output is original, no duplicate, real file clipboard, deleteDecision Not applicable. Notification delivery not yet implemented. |
| Q05 | Every candidate from original; retained audio budget / container allowance | Partially tested: original is each encode input; actual audio packet bytes subtracted with 1%/64KiB minimum mux allowance. 70s PCM audio-budget failure retains original. |
| Q06 | Original dimensions/fps with stronger efficiency/strength first | Partially tested: candidate policy verifies three descending bitrate targets before geometry/rate changes; FFV1 avoids duplicate ineffective bitrate tests. Wider encoder efficiency verification open. |
| Q07 | >60fps then 60, lower resolution ladder, then 30/24/20/15 with reconsideration | Tested candidate policy ordering at 3840x2160/120; full media encodes at every ladder stage remain untested. |
| Q08 | Aspect/alignment, no upscale/fps increase; retain below 480p/15fps | Partially tested: candidate bounds/order plus 240x426/12fps retention; final display-aspect validation implemented. Anamorphic/ultrawide media verification open. |
| Q09 | Representative high-motion sample shortlist and full VMAF mean>=90/p5>=80 | Partially tested: whole-source YDIF analysis chooses high-motion plus start/mid/end; sample shortlist and every-frame full VMAF implemented. Full synthetic encode passes; severe 32x18/CRF45 degradation and percentile outlier guard rejected. Gaming visual assessment remains open. |
| Q10 | Consistent display resolution and identical HDR SDR comparison transforms | Implemented / partially tested: both inputs normalized to original dimensions/fps; lower-fps duplication scored. Identical controlled peak=10 HDR-to-SDR transform applied to both score streams only. HDR scoring runtime test open. |
| Q11 | Full decode, codec/container/tracks/colour/size and duration tolerance validation | Partially tested: complete H.264/MP4 output validated and decoded, actual bytes and duration, exact audio payload hashes. Wider formats/HDR remain open. |
| Q12 | Distinct final output, rejected temp cleanup, cancellation retains original | Tested synthetic workflows: distinct finalized output, original SHA256 retained, cancelled during candidate encoding and rejected FFV1 search clean temporary directories. Crash cleanup exercise remains open. |
| Q13 | Specific failures incl audio budget/lossless; no mathematical impossibility claim | Tested: actual >20MB noisy FFV1 fails supported same-codec search cleanly; no codec substitution; PCM budget failure and cancellation distinct. Disk/encoder failure integration remains open. |
| Q14 | Visual development checks: HUD/foliage/particles/dark scenes/motion | Pending |

## 8. Clipboard and persistent interactive notifications
| ID | Requirement | Status / evidence |
|---|---|---|
| I01 | Real CF_HDROP file clipboard, contention retries, file stays on disk | Partially tested: real CF_HDROP/Preferred DropEffect, native readback, separate-process short contention retry. Representative destination paste remains open. |
| I02 | Clipboard failure retains files, accurate message, Retry Copy, no success toast | Tested: another process locks clipboard through actual compression; both files retained, correct failure state/message and Retry Copy XML, no deletion/success prompt. Retry succeeds after release and produces the success question. |
| I03 | Exact size/quality failure message, limiting reason in app; accurate other errors | Partially tested: exact failure XML/text and specific original-preserving audio/lossless/cancel/copy states pass. Native toast presentation unverified because Windows global notifications are disabled. |
| I04 | Successful copy exact deletion question with Yes/No; already-small no deletion | Partially tested: exact native toast XML/actions and queued already-small exclusion pass. Actual native delivery/clicks remain unverified under disabled system notifications. |
| I05 | Persisted job IDs resolve actions; body opens corresponding clip | Partially tested: strict UUID/action parser rejects paths and duplicate/unknown fields; COM activation resolves persisted job. Identity-checked body-open handler implemented; body click presentation untested. |
| I06 | Yes recycles corresponding identity-verified original only, never permanently deletes | Tested on disposable files: identity-locked staging, explicit Shell ITransferSource::RecycleItem (no RemoveItem/DeleteFile fallback), corresponding original recycled, compressed identity unchanged. Persisted/repeated Yes passes. |
| I07 | No/dismiss/ignore retains; repeated/stale actions harmless; compressed intact | Partially tested: No survives restart and wins over later Yes; replaced originals rejected; interrupted staged original restored on No; locked-original failure retains both. Dismiss/ignore perform no operation by construction; actual dismissal UI untested. |
| I08 | Restart activation works without needless main window, in-app suppressed fallback | Partially tested: real per-user COM registration starts clipper.exe on activation, resolves persisted No, retains both and exposes no visible window. At all three scales an actual compression job exposes in-app decisions; clicking No retains both and disables further decisions. Native popup/history test explicitly skipped: system ToastEnabled=0; preference unchanged. |

## 9. Reliability, performance and delivery gates
| ID | Requirement | Status / evidence |
|---|---|---|
| T01 | Capture/buffer test matrix in specification | Partial: core replay endpoint, partial, delayed completion, pinning, duration/byte tests pass; remaining capture matrix open. |
| T02 | Compatibility test matrix, distinguish verified/untested hardware | Pending |
| T03 | Audio test matrix including long drift and removal | Pending |
| T04 | Editing/compression test matrix incl byte boundaries and guard rejection | Partial: 8 media tests pass covering fast/exact trim, crop/fit, gain/mute/mix, cancellation, immutable queued saves and unchanged SHA256 originals. HDR and compression cases remain pending. |
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


- 2026-11-03: Editing milestone verified: core 13 checks, media 8 checks, native UI 8 checks at each scale. Original hashes retained; fast trim fixed with native packet remux after timestamp-shift failure. Bootstrap local cache path also verified.

