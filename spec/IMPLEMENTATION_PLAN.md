# C++ Gaming Clipper — Detailed Build Specification

## Handoff instructions for Codex

This document is the implementation specification for a future Codex `/goal` task. Writing or reading this file alone is not an instruction to start implementation. Start building only when the user explicitly invokes the implementation task.

When authorised to implement, complete the scope below, verify the result, and report material limitations. Use the name SippingCloftware. Use Windows UI typography for the capture HUD and Inter and Geist Mono for the main window.

Suggested goal text:

> Implement the application described in IMPLEMENTATION_PLAN.md. Work through the complete specification, build and test SippingCloftware, and produce a distributable Windows installer and verification report. Report the hardware paths actually verified.

## 1. Objective and scope

SippingCloftware is a Windows tray application for recording gaming highlights and producing convenient, high-quality clips. It is intended for personal use by a small group of friends.

Priorities, in order:

1. Reliable capture and correctly saved clips.
2. Minimal impact on gameplay.
3. High visual and audio quality.
4. Convenient clipping, compression, and sharing.
5. A clean interface based on the supplied sketch.

The first release should be substantial and usable, without requiring commercial studio polish.

### Supported systems

- Windows 10 22H2 and Windows 11.
- Native x64 builds for Intel and AMD CPUs.
- NVIDIA, AMD, and Intel graphics, including integrated graphics.
- Software encoding where suitable hardware encoding is unavailable.
- Runtime detection of processor features; do not require AVX2 throughout the application.

“Broad hardware support” means detecting and accommodating available capabilities. It does not mean every codec, resolution, frame rate, or HDR mode can work on every computer.

### Included

- Rolling replay capture.
- Configurable global shortcuts.
- A hidden-until-requested clipping toolbar.
- Selectable clip duration, resolution, frame rate, quality, codec, and container.
- Configurable audio sources and track routing.
- Application-based folders.
- Clip library and playback.
- Trimming, cropping, aspect-ratio exports, and basic audio editing.
- HDR capture and explicit SDR sharing exports.
- Automatic compression to 20 MB.
- File clipboard integration.
- Interactive Windows notifications.

### Excluded

- Captions and transcription.
- Cloud accounts, uploads, or synchronisation.
- Social-network integrations.
- Game injection or anti-cheat workarounds.
- ARM64, macOS, and Linux releases.
- Advanced compositing, effects, or a full video-editing timeline.

Default output location is `Videos\Clips`.

## 2. Interface and interaction design

### Typography and appearance

Bundle the font files so the interface does not depend on installed fonts or internet access.

- **Inter:** navigation, buttons, labels, menus, settings, explanations, and notifications shown inside the app.
- **Geist Mono:** durations, frame rates, dimensions, file sizes, timestamps, shortcuts, and encoder details.
- Apply these fonts consistently to all application-controlled surfaces.
- Native Windows notifications, Explorer, and operating-system dialogs retain their system-controlled typography.

Use a restrained dark appearance:

- Charcoal backgrounds.
- Clearly separated controls.
- Comfortable contrast.
- Green for active/ready states.
- Red for stop/error states.
- A single restrained accent for selection.
- No animated backgrounds, permanent blur effects, or continuous decorative animation.

Support Windows display scaling and keyboard navigation. Controls must remain usable at 100%, 150%, and 200% scaling.

### Main window

Provide three primary areas:

| Area | Purpose |
|---|---|
| **Clips** | Browse, play, organise, edit, copy, and compress saved clips. |
| **Recording** | Configure the capture source, encoder, audio, buffer, and recording quality. |
| **Settings** | Configure shortcuts, folders, startup behaviour, notifications, and advanced options. |

Normal launch goes straight to the system tray. The tray icon opens the main window on demand. Closing the window returns to the tray. The tray menu includes clip toolbar, start or stop recording, and Exit actions. Exit stops recording and closes the process.

Recording starts when SippingCloftware launches. Windows sign-in launch is opt-in in Settings. A first launch without a calibrated SDR profile can start a basic H.264 software profile at up to 720p and 30 FPS. A window source requires reselection after restart.

### Clipping toolbar

Follow the supplied sketch as a narrow vertical counterpart to the Windows Snipping Tool toolbar. Show it near the active display's left edge on the hotkey. Apply a uniform translucent dim layer to that display while the toolbar is visible. Keep the HUD and dim layer out of replay capture. Suppress the dim layer on capture paths where exclusion cannot be verified.

Use a native Windows backdrop brush under a translucent charcoal tint, clipped to the toolbar and the open menu, with a solid dark fallback. Slide the toolbar and adjoining menus in briefly, respecting Windows' animation preference. Dim the active display with a capture-excluded layer. Dismiss the toolbar and dim layer on Escape or switching to another app. Group the Save Clip and recording icons at the top. Show the current container, frame rate, resolution, quality and duration on the short strip.

Click a current value to open its presets in a small adjacent stack. Click a preset to slide the group's single highlight to that button's actual bounds. The labels stay in place, and keyboard selection follows the same behavior. Keep secondary choices such as a custom duration and encoder settings in the advanced panel.

Place **Auto Compress** below duration. It starts off each time the toolbar opens. Put **Mic** and **Desktop Audio** below it, both on when the toolbar opens. Apply them to the saved clip using the replay's separate audio mixes. Capture continues using the configured audio sources.

Provide:

- Duration: 10s, 30s, 1m, 3m, 5m, Custom.
- Frame rate: source rate and applicable lower choices, including 15, 30, 60, and 120 fps.
- Resolution: source resolution and applicable lower choices.
- Container: compatible choices among MP4, MKV, WebM, and MOV.
- Quality: Maximum, High, Balanced, and Compact, with encoder-specific mappings.
- An advanced panel for codec and encoder implementation.

Escape dismisses the toolbar and dim layer. Saving dismisses both immediately and processing continues in the background.

### Clip timing

Opening the toolbar records a fixed endpoint for the clip.

For example:

- The user presses the shortcut at 12:00:00.
- They spend five seconds selecting “30s”.
- The requested clip remains the footage ending at 12:00:00.

Do not move the endpoint to the time the Save button is clicked.

Pin the relevant buffer data while the toolbar is open. Release pins when the toolbar is dismissed or ownership transfers to an export job.

If insufficient footage exists, show the available duration before saving. Never claim a 30-second clip contains 30 seconds when only 12 seconds were buffered.

### Recording settings versus clip settings

Keep these distinct:

- **Recording settings** determine what enters the replay buffer.
- **Clip settings** determine how buffered footage is exported.

Lower resolution or frame rate can be applied during export. Higher resolution, extra frames, and lost detail cannot be recovered.

Disable impossible retrospective choices with an explanation. Changes to recording settings that require restarting capture must warn that the buffer will reset and require approval.

## 3. Recording engine, compatibility, and audio

### Architecture

Use:

- C++20.
- CMake and MSVC.
- Qt 6 Widgets.
- Windows Graphics Capture and Direct3D 11.
- Prefer borderless DXGI Desktop Duplication for SDR display recording. Windows Graphics Capture remains available for window capture, HDR, and display fallback. Its recording border follows Windows' policy.
- FFmpeg libraries for codec discovery, encoding, decoding, filtering, and muxing.
- WASAPI for audio capture.
- Windows clipboard, notification, and shell APIs.

Python may be used for development tooling but must not be required to run the distributed app.

Separate these responsibilities:

| Component | Responsibility |
|---|---|
| Capture engine | Acquire frames, retain timestamps, and handle capture-device changes. |
| Encoder registry | Discover implementations, test capabilities, and select valid configurations. |
| Audio engine | Capture, resample, synchronise, mix, and route sources. |
| Replay buffer | Store bounded encoded segments and pin requested intervals. |
| Export queue | Execute immutable save, edit, and compression jobs. |
| Library/editor | Present saved files and construct export requests. |
| Windows integration | Shortcuts, tray, clipboard, Explorer, notifications, and startup registration. |

### Encoder catalogue

Discover the complete video-encoder catalogue included in the distributed FFmpeg build. Do not restrict discovery to a hardcoded list of popular codecs.

Include available implementations for mainstream and specialist formats, such as:

- H.264/AVC.
- H.265/HEVC.
- AV1.
- VP8 and VP9.
- MPEG video variants.
- FFV1.
- ProRes.
- DNx variants.
- Additional usable video encoders present in the build.

Still-image encoders and encoders that cannot produce a suitable video stream should not appear as ordinary replay-recording choices.

Each encoder record must distinguish:

- Included in the build.
- Matching hardware detected.
- Successfully initialised.
- Successfully encoded test frames.
- Appropriate for live capture.
- Appropriate for export only.
- Unavailable, with a reason.

Record codec identity, implementation, device, supported pixel formats, rate-control modes, container compatibility, tested configurations, and HDR capability.

Do not advertise unqueried maximum dimensions or frame rates as verified capabilities.

### Detection and automatic selection

1. Enumerate graphics adapters and attached displays.
2. Detect available hardware encoding backends.
3. Query capabilities.
4. Run small synthetic initialisation and encoding tests.
5. Perform a short capture-path calibration before recommending a configuration.
6. Cache results against relevant driver and dependency versions.

Automatic selection should initially prefer verified hardware H.264 for playback compatibility. Offer AV1 and HEVC recording profiles when supported.

Prefer using the capture adapter when doing so avoids expensive transfers. A dedicated GPU must not automatically win if the full capture pipeline performs worse.

Software encoding must be available where practical, with an explanation of its CPU cost.

### Quality and performance controls

Encoder settings require individual mappings:

- A CQ value from one implementation is not equivalent to a CRF or QP value elsewhere.
- Some codecs do not expose a comparable quality control.
- Lossless and specialist codecs require their own behaviour.
- Container choices must be checked against the selected codec.

Expose a useful automatic configuration and retain advanced manual controls.

Never silently change recording resolution, frame rate, codec, or quality. If recording falls behind:

- Show dropped-frame and backlog information.
- Explain likely causes.
- Offer specific changes.
- Apply changes only after approval.

### Replay storage

Use a disk-backed ring buffer of compressed, independently decodable segments.

Defaults:

- Five minutes requested retention.
- Four GiB maximum storage.
- Approximately two-second segment boundaries.
- Configurable storage location and limits.

Retain whichever limit is reached first. Display actual available duration.

Use bounded frame and packet queues. Never accumulate an unlimited backlog of raw frames.

Track segment timestamps and pin segments used by pending saves. Buffer cleanup must never remove pinned data.

Wait for delayed encoder packets needed to cover a requested endpoint. Do not silently lose the final moments of a highlight.

On disk exhaustion or unrecoverable capture failure, stop safely, preserve completed clips, and explain what happened.

### Audio

Allow users to configure:

- System/output-device audio.
- Microphone devices.
- Per-application audio where supported.
- Source enable/disable.
- Source gain and mute.
- Microphone push-to-talk.
- Separate-track recording.
- Compatibility-mix inclusion.

The default first track is a playback-friendly mix. Optional additional tracks contain selected individual sources.

Avoid capturing a source twice through overlapping system and per-application routes. Explain overlapping selections.

Use a common monotonic clock, resampling, and drift correction to maintain audio/video synchronisation.

Handle disconnected devices and default-device changes explicitly. Never replace a chosen microphone or system source silently.

Windows versions without supported per-application capture must offer clearly labelled system/device capture instead.

### HDR

Preserve HDR through a suitable floating-point capture pipeline and supported 10-bit encoding.

Carry correct colour primaries, transfer characteristics, matrix/range information, and available metadata through recording and exports.

Offer:

- Preserve HDR.
- Explicit tone-mapped SDR copy.

Never relabel HDR as SDR without conversion or silently convert it during ordinary saving.

Disable unsupported combinations with an explanation.

## 4. Library, editing, folders, and shortcuts

### Folder organisation

Save to:

`Videos\Clips\<Application>\<timestamp>.<extension>`

Allow a custom root and application-folder overrides.

Determine the application before the overlay takes focus. Use executable identity rather than a changing window title. Fall back to Desktop when necessary.

Generate safe, unique filenames and support Unicode paths.

### Library

Show:

- Thumbnail.
- Application.
- Capture date.
- Duration.
- Resolution and frame rate.
- Codec and container.
- File size.
- Original/compressed/edited relationship.
- Audio-track count and HDR status.

Provide application filtering and basic search.

Load thumbnails on demand and cache them. Do not continuously scan or decode the entire library while gaming.

Actions:

- Play.
- Show in Folder.
- Copy File.
- Rename.
- Trim.
- Crop/Aspect Ratio.
- Audio.
- Auto Compress.

Store metadata beside clips or in a local index that can be rebuilt from files. External renaming or deletion must not break the library permanently.

### Editing

Edits create new files.

- **Fast trim:** preserve encoded packets where possible and disclose keyframe rounding.
- **Exact trim:** re-encode as necessary to honour selected boundaries.
- Show the resulting duration before export.
- Support free crop plus 16:9, 9:16, and 1:1 output.
- Default aspect-ratio conversion to fitting the entire image.
- Require a visible selected region for destructive cropping.
- Allow audio-track selection, gain changes, mute, and a mixed sharing track.
- Preview on demand; stop playback and release decoding resources when the editor closes.

### Shortcuts

Default shortcuts:

- `Ctrl+Shift+S`: open clipping toolbar.
- `Ctrl+Shift+R`: start/stop recording.

Allow reassignment and detect registration conflicts. Keep existing working bindings if a replacement cannot be registered.

Push-to-talk uses a separately configurable binding.

## 5. Auto Compress to 20 MB

### User-facing contract

Auto Compress is a per-clip toggle below duration. With the toggle armed, Save Clip submits a save-and-compress job.

It must:

1. Save the original selected clip.
2. Finalise and validate that file.
3. Attempt a compressed copy of no more than **20,000,000 bytes**.
4. Keep the duration unchanged.
5. Preserve the original unless the user explicitly chooses to delete it after success.

Save the original with the selected codec and container. Allow the separate share copy to change codec and container. Use verified H.264/MP4 for SDR first and AV1/WebM as a fallback. For HDR, use verified 10-bit HEVC/MP4 and preserve colour and HDR metadata. Maintain display aspect ratio.

If the original already fits:

- Copy that file to the clipboard.
- Report that it already meets the limit.
- Do not create a duplicate.
- Do not offer to delete the only file.

### Processing rules

- Every candidate must come directly from the saved original.
- Never compress a previous rejected candidate.
- Never shorten the video to meet the target.
- Never alter ongoing recording settings.
- Run one export job at a time, below capture priority.
- Provide progress, current candidate settings, and cancellation.
- Cancellation retains the original.

Use approximately 19.5 MB as the internal target, allowing margin for muxing overhead. Final acceptance uses the actual file size, not an estimated bitrate.

Calculate the available video budget from:

`target bytes − audio bytes − container allowance`

Then divide by duration and multiply by eight to obtain the approximate video bitrate budget in bits per second.

Try all audio tracks first. Copy compatible tracks and re-encode incompatible tracks with their original channel layouts. If the all-track search cannot produce a usable share copy, try the compatibility mix alone at 96 kb/s, using the default audio track if there is no labelled mix. Report the audio policy used in the output details.

### Candidate progression

For each codec and audio combination, sample the source dimensions and frame rate, the next lower step on each axis separately and together, and the allowed floor. Use the source value as the floor on an axis already below the limit. Try up to two full encodes in sample score order. Use two-pass bitrate targeting on verified encoders and at most one bitrate correction per encode if it overshoots.

Resolution ladder, where lower than the source:

2160p → 1440p → 1080p → 900p → 720p → 540p → 480p.

Maintain aspect ratio and encoder-required dimension alignment. Treat the ladder as applicable image sizes, not a reason to stretch portrait or ultrawide footage.

Never upscale or increase frame rate. For sources already below 480p or 15 fps, retain those source characteristics rather than increasing them.

“Progressive” means intelligently testing stronger settings, not intentionally slowing processing or repeatedly damaging the same file.

### Quality guard

The app must refuse a severely degraded result even if it meets the byte limit.

Initial engineering thresholds:

- Mean VMAF: at least 90.
- Fifth-percentile frame VMAF: at least 80.

These are starting defaults to validate using gaming footage, not guarantees that all visible artifacts are detected.

Use representative samples, including high-motion intervals, to shortlist candidates. Validate the complete selected output before success.

Compare reference and candidate at a consistent display resolution. For HDR scoring, apply the same controlled SDR conversion to both comparison streams while preserving HDR in the actual output.

Do not select a candidate solely because it achieves a metric score. Development validation must include small HUD text, foliage, particles, dark scenes, and motion.

### Acceptance of a compressed file

A candidate succeeds only when all checks pass:

- At most 20,000,000 bytes.
- Fully decodable.
- Codec, container, and audio match the selected share profile.
- Correct colour characteristics.
- Quality guard passed.
- No intentional duration reduction.
- Duration differs by no more than one output-frame interval plus necessary audio-packet rounding.
- Compressed file is finalised and stored under a distinct filename.

Reject failed candidates and clean their temporary files. If all candidates fail, retain the original.

Failure means the app could not satisfy the defined limits with its supported search policy; do not claim mathematical proof that no encoder could ever do so.

## 6. Clipboard, notifications, and deletion

### Clipboard

Copy the actual saved file using Windows file-copy clipboard formats, including `CF_HDROP`.

Do not copy only a textual path.

The compressed file must remain on disk for pasting. Pasting works in destinations that accept file clipboard data; it cannot be guaranteed in every application.

Retry short-lived clipboard contention. If copying fails:

- Keep both files.
- Show “Compressed, but could not copy.”
- Offer Retry Copy.
- Do not show the normal successful-copy notification.

### Notifications

For a failed size/quality attempt:

> File was too big to reduce to 20MB.

Show the specific limiting reason inside the app.

For a successful compression and clipboard copy:

> File compressed and copied to clipboard. Delete unreduced file?

Buttons:

- **Yes**
- **No**

Behaviour:

| Action | Result |
|---|---|
| Yes | Move only the corresponding original file to the Recycle Bin. |
| No | Retain both files. |
| Dismiss or ignore | Retain both files. |
| Click notification body | Open the corresponding clip in the app. |

For an already-small original, report that it already fits and was copied. Do not offer deletion.

Encoding errors, disk failures, cancellations, and clipboard failures require accurate messages rather than being misreported as an oversized file.

### Persistent and safe actions

Associate notifications with persisted job IDs.

- Resolve actions through the stored job record.
- Never accept an arbitrary deletion path from notification arguments.
- Verify the expected original file identity before recycling it.
- Reject stale actions if the path now refers to a different file.
- Make repeated Yes/No activation harmless.
- Keep the compressed file intact.
- Never permanently delete as a fallback if recycling fails.

Notification actions must work after restarting the app. Handle activation without unnecessarily opening the main window.

When Windows suppresses notifications, mirror completion status and the available actions in the app.

## 7. Persistence, testing, and delivery

### Data and jobs

Persist:

- Recording and clip-export preferences.
- Audio configuration.
- Shortcuts.
- Folder overrides.
- Encoder-test cache.
- Clip relationships.
- Outstanding notification actions.

Export jobs must contain immutable snapshots of their source interval and selected settings. Later toolbar changes must not change a queued job.

Use explicit job states:

`Queued → Saving original → Analysing → Encoding → Validating → Copying → Complete`

Also support:

- Already within limit.
- Cannot meet constraints.
- Cancelled.
- Failed.
- Compressed but clipboard copy failed.

After a crash or restart, mark interrupted jobs accurately and retain usable originals. Do not announce success for unfinished work.

### Implementation sequence

1. Establish the reproducible C++ build and dependency packaging.
2. Implement hardware discovery and encoder tests.
3. Implement capture, audio synchronisation, segmented storage, and reliable saves.
4. Implement the toolbar, shortcuts, library, and folder integration.
5. Implement playback and nondestructive editing.
6. Implement HDR preservation and SDR export.
7. Implement compression search, quality validation, and cancellation.
8. Implement clipboard copying and persistent interactive notifications.
9. Measure resource use, fix reliability issues, and package the release.

Existing starter files must be reviewed as incomplete experimental code. Do not preserve their assumptions merely because they already exist.

### Required tests

**Capture and buffer**

- Hotkey-fixed endpoints.
- Partial buffers.
- Delayed encoder packets.
- Multiple queued saves.
- Segment pinning and cleanup.
- Duration and byte limits.
- Overlay exclusion.
- Display changes, sleep/resume, and device loss.
- Low disk space and crash recovery.

**Compatibility**

- Windows 10 and 11.
- NVIDIA, AMD, and Intel paths where hardware is available.
- Integrated/dedicated GPU combinations.
- Software fallback.
- Missing or incompatible drivers.
- Container/codec restrictions.
- Unsupported HDR and frame-rate choices.
- CPU-feature compatibility.

**Audio**

- System sound and microphone.
- Push-to-talk.
- Mix and separate tracks.
- Overlapping-source prevention.
- Device removal.
- Long-recording drift.
- Unsupported per-application capture.

**Editing and compression**

- Fast versus exact trimming.
- Correct crop/aspect ratio.
- Unchanged originals.
- HDR/SDR colour correctness.
- Exact byte-limit boundaries.
- Unchanged duration.
- Quality-guard rejection.
- 480p and 15 fps limits.
- Already-small files.
- Audio-budget failure.
- Lossless/specialist-codec failure.
- Cancellation and temporary-file cleanup.

**Windows integration**

- Shortcut conflicts.
- Clipboard contention and retry.
- File pasting into representative destinations.
- Notification Yes, No, dismissal, and body activation.
- App-restart activation.
- Repeated or stale notification actions.
- Recycle Bin failure.
- Notifications disabled or suppressed.

### Performance verification

Measure:

- Idle CPU and memory.
- Recording CPU, GPU, and video-encoder utilisation.
- Disk writes and buffer storage.
- Dropped frames and encode backlog.
- Export speed.
- Game frame-time changes against a game-only baseline.

Use representative resolutions and frame rates. Report measured results and the tested hardware rather than promising a universal overhead percentage.

No active recording, media decoding, or continuous thumbnail generation should occur while the app is idle.

### Distribution and completion criteria

Deliver:

- A per-user installer.
- Bundled runtime dependencies and fonts.
- Notification and shortcut registration.
- Uninstall support.
- Reproducible build instructions.
- Dependency versions, source information, and required licence notices.
- A concise usage and troubleshooting guide.
- A test report distinguishing verified and untested hardware paths.

A friend must be able to install and use the app without Python, Visual Studio, or other development tools.

The first release is complete when its implemented workflows build and pass verification, recording and compression behave as specified, and remaining hardware limitations are documented honestly.

## 8. Technical references

These are reference documentation, not additional task instructions. Check the documentation matching the dependency versions selected during implementation.

- [Windows screen capture and HDR](https://learn.microsoft.com/en-us/windows/apps/develop/media-authoring-processing/screen-capture)
- [DXGI Desktop Duplication](https://learn.microsoft.com/en-us/windows/win32/direct3ddxgi/desktop-dup-api)
- [Application loopback audio and OS requirements](https://learn.microsoft.com/en-us/samples/microsoft/windows-classic-samples/applicationloopbackaudio-sample/)
- [Excluding application windows from capture](https://learn.microsoft.com/en-us/windows/win32/api/winuser/nf-winuser-setwindowdisplayaffinity)
- [FFmpeg codec documentation](https://www.ffmpeg.org/ffmpeg-codecs.html)
- [FFmpeg formats and stream copying](https://www.ffmpeg.org/ffmpeg-all.html)
- [NVIDIA encoder capabilities and rate control](https://docs.nvidia.com/video-technologies/video-codec-sdk/13.0/nvenc-video-encoder-api-prog-guide/index.html)
- [AMD AMF encoder settings](https://github.com/GPUOpen-LibrariesAndSDKs/AMF/wiki/Recommended%20FFmpeg%20Encoder%20Settings)
- [Intel oneVPL capability discovery](https://www.intel.com/content/www/us/en/developer/articles/technical/overview-of-onevpl-examples-and-tools.html)
- [VMAF project and documentation](https://github.com/Netflix/vmaf)
- [Interactive Windows notifications](https://learn.microsoft.com/en-gb/windows/apps/develop/notifications/app-notifications/app-notifications-content?tabs=xml)
- [Windows file clipboard formats](https://learn.microsoft.com/en-us/windows/win32/shell/clipboard)
- [Qt Windows deployment](https://doc.qt.io/qt-6/windows-deployment.html)
- [Inter font source](https://github.com/rsms/inter)
- [Geist font source](https://github.com/vercel/geist-font)
