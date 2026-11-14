# Video Editor

A native C++17 / Qt 6 Widgets video clipping app, inspired by the single-window playback workflow of [indy256/video-player](https://github.com/indy256/video-player). This is an independent implementation.

## Run

Launch `dist\VideoEditor.exe`. Keep the entire `dist` folder together; it contains the Qt runtime and helper programs.

1. Paste a YouTube URL and click **Open YouTube**, or choose **Open file** for a local video.
2. **Stream on demand** is the default: playback starts after resolving a combined H.264 video/audio stream, fetching small byte ranges as needed. Seeking fetches data near the new position. Combined preview streams may be limited to 360p; the app displays the preview resolution. **Save selection** resolves fresh higher-quality video/audio URLs and fetches the selected interval at up to 1080p, including nearby keyframes and container metadata needed for precise cuts. It does not cache the entire source video.
   Choose **Download first** to cache the full video before playback for offline seeking. Downloads and streamed exports prefer H.264 up to 1080p with the best available audio, falling back to VP9. AV1 is excluded because the local Qt/Windows ARM64 hardware decoder can produce green frames and its bundled FFmpeg has no software AV1 decoder. Full-download cache files are removed when the application closes normally.
3. **Left-click or drag** on the selection timeline to set IN. **Right-click or drag** to set OUT. Both actions also seek to that point. Use **Shift-click** to seek without changing the selection, or use the separate playback slider.
4. Adjust the IN and OUT time fields for precise times. An end before the start disables export. The timeline supports media longer than 24 hours; the time fields are disabled for those files.
5. Click **Preview selection** to play the interval. Choose **Precise MP4 (re-encode)** or **Original streams (no re-encoding)** in **Save as**, then click **Save selection**.

Space toggles playback when a text/time field is not focused. Ctrl+O opens a local file; Ctrl+S saves the selection. Volume and five-second seek buttons are available below the player. A local filename or YouTube URL may also be passed on the command line.

**Precise MP4** uses FFmpeg H.264 / AAC encoding with millisecond input times, preserving the first video and optional first audio track. Re-encoding gives accurate boundaries within video-frame and audio-packet resolution.

**Original streams** writes MKV using FFmpeg stream copy for both video and audio. It preserves their encoded packets, codecs, resolution and quality without recompressing either track. The app finds the video keyframe at or before IN, aligns audio to that start, and copies through OUT at packet boundaries. The clip can include footage before IN and a few frames or audio packets after OUT. The actual source start is shown after saving. Choose Precise MP4 when exact selection boundaries matter. MKV supports original codecs without forcing an AAC conversion; files without audio also work. YouTube exports retain the existing H.264/VP9 selection policy up to 1080p, independently of the lower-resolution streaming preview. Keyframe discovery copies one video packet through FFmpeg's framehash muxer; it does not decode or encode the video and uses the same seek behavior as the final export.

Both modes write beside the destination under a temporary name, then atomically replace the destination only after successful completion. Cancellation and failed exports preserve an existing destination. The source file cannot be overwritten.

## Build on this machine

The default build uses `C:\Qt\6.11.2\msvc2022_arm64`, Visual Studio's ARM64 compiler, and CMake/Ninja in `C:\Qt\Tools`.

```powershell
# Download current, checksum-verified yt-dlp and Deno into this project's tools folder.
powershell -NoProfile -ExecutionPolicy Bypass -File scripts\setup-tools.ps1
# FFmpeg must be on PATH or in tools (this machine has C:\util\ffmpeg.exe).
powershell -NoProfile -ExecutionPolicy Bypass -File build.ps1 -Test
.\dist\VideoEditor.exe
```

Override `-QtRoot` and `-ToolsRoot` for another compatible MSVC Qt kit. For x64 helper programs use `scripts\setup-tools.ps1 -Architecture x64`. CMake requires Widgets, Multimedia, MultimediaWidgets, Network and Test. Open `CMakeLists.txt` in Qt Creator to develop with a matching kit.

The app searches `tools` beside its executable, its executable directory, then PATH for helpers. `VIDEO_EDITOR_FFMPEG` and `VIDEO_EDITOR_YT_DLP` can override their paths. Deno sits beside yt-dlp to provide YouTube's required JavaScript runtime. The build bundles helper programs from the project's `tools` directory, falling back to PATH. No machine-wide tool installation is changed. Update project helpers with `setup-tools.ps1` and rebuild when YouTube changes.

The downloader ignores global yt-dlp configuration and processes one video per link. Public, finite videos are the intended input. Streaming rejects live videos and videos without a known duration. If a combined preview is unavailable, choose Download first. Stream URLs expire; reopen the link if playback stops after an extended session. Export always resolves fresh URLs. Streaming requires an internet connection while playing, seeking, and saving; Download first requires temporary disk space for the entire source video.

A loopback-only HTTP bridge translates player/FFmpeg seek requests into bounded CDN requests. It honors byte ranges, applies downloader headers, bounds queued data with backpressure, and aborts upstream requests when a player disconnects. No HTTP listener is exposed to other computers.

When YouTube supplies only an estimated file size, the app checks the server for the exact byte length using HEAD, with a one-byte HTTP range probe as a fallback. It never uses the estimate as a seekable file's length. These checks can be canceled without closing the app.

Completed streaming chunks are cached automatically on disk in fixed 2 MiB blocks. Seeking back, replaying, or reopening a video reuses those blocks, including after an app restart. Preview video and higher-quality export tracks have separate identities; each reuses data previously fetched for that same track. Cache identities include the video, format, content length, and media revision so refreshed signed URLs can reuse bytes while changed media cannot.

The streaming cache is limited to 2 GiB and evicts the least recently used chunks as new ones arrive. It lives in Qt's per-user application cache directory under `stream-chunks-v1`; set `VIDEO_EDITOR_CACHE_DIR` to choose another directory. You can remove that directory while the app is closed to clear the cache. Writes are atomic, only complete chunks are retained, and chunks are checked for corruption before playback. A full or unwritable cache does not prevent streaming. Reopening a YouTube video still needs network access to resolve its current stream URLs; cached portions then play without fetching those bytes again.

## Validation

`build.ps1 -Test` generates a five-second video with audio and checks actual Qt decoding, paused seeking, resume, left/right selection behavior, invalid selection controls, export duration, exported video playback, safe overwrite, source protection and cancellation cleanup. FFmpeg and ffprobe are needed for tests.

Original-stream export tests compare SHA-256 packet hashes against a source with H.264 video and FLAC audio, verify a keyframe start and aligned audio, and decode the exported MKV. Video-only sources and cancellation during the keyframe check are covered. The live streaming test also saves an original-stream MKV with separate YouTube video/audio inputs and checks that it decodes.

To run a live YouTube download, decode and export integration check, set `VIDEO_EDITOR_TEST_YOUTUBE` to a short public video URL before running `build.ps1 -Test`. This check depends on network and YouTube availability and is skipped by default. Set `VIDEO_EDITOR_SCREENSHOT` to a PNG path to capture the app during its window workflow test.

The live check also compares decoded frame colors against an independently decoded FFmpeg reference, catching green frames even when Qt reports successful playback. The reported regression URL is `https://youtu.be/b0LkG52V-lI`. For an already downloaded file, set `VIDEO_EDITOR_REGRESSION_VIDEO` to its absolute path and run `build\editor_tests.exe videoColorRegression -platform windows` with Qt DLLs on PATH. Optionally set `VIDEO_EDITOR_REGRESSION_CAPTURE` to an output path prefix to save the decoded frame. Locally opened AV1 files still depend on the Qt kit and graphics driver's AV1 support.

Set `VIDEO_EDITOR_TEST_STREAM=https://www.youtube.com/watch?v=8N8jCOCf1VE` for a live streaming test. It plays the video with audio, seeks halfway through without downloading preceding footage, exports a 2.25-second interval there, checks the output duration and audio/video tracks, and decodes the saved file. It then resolves the video again and verifies that a previously loaded range is served from cache without additional CDN bytes. For videos longer than an hour it also checks that the total source bytes fetched stay below 100 MiB. Local tests check overlapping-range reuse after recreating the bridge, LRU eviction, and rejection of corrupt or partial chunks. Tests use an isolated temporary cache.

Qt, FFmpeg, yt-dlp and Deno retain their respective licenses. Review their redistribution terms before distributing a packaged build; the locally installed FFmpeg reports a GPL-enabled build.
