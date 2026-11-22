# Video Editor

A native C++17 / Qt 6 Widgets video clipping app, inspired by the single-window playback workflow of [indy256/video-player](https://github.com/indy256/video-player). This is an independent implementation.

## Run

Launch `dist\VideoEditor.exe`. Keep the entire `dist` folder together; it contains the Qt runtime and helper programs.

The editor remembers your volume, YouTube URL and loading mode, export mode, and window size and position across restarts. It restores the last IN and OUT markers when you reopen the same local file or YouTube video.

1. Paste a YouTube URL and click **Open YouTube**, or choose **Open file** for a local video. Videos open paused with the first frame visible; click **Play** or press Space to start playback.
2. **Stream on demand** is the default: the preview loads after resolving a combined H.264 video/audio stream, fetching small byte ranges as needed. Seeking fetches data near the new position. Combined preview streams may be limited to 360p; the app displays the preview resolution. **Save selection** resolves fresh higher-quality video/audio URLs and fetches the selected interval at up to 1080p, including nearby keyframes and container metadata needed for precise cuts. It does not cache the entire source video.
   Choose **Download first** to cache the full video before playback for offline seeking. Downloads and streamed exports prefer H.264 up to 1080p with the best available audio, falling back to VP9. AV1 is excluded because the local Qt/Windows ARM64 hardware decoder can produce green frames and its bundled FFmpeg has no software AV1 decoder. Full-download cache files are removed when the application closes normally.
3. **Left-click** the timeline track to move the closest marker, or **left-drag** the **IN** handle above the track or the **OUT** handle below it. The playhead follows the moved marker. To seek while keeping the selection, **Shift-click** the timeline or click or drag the playback slider.
4. Type precise times in the IN and OUT fields, or use **Up** and **Down** in either field to move its marker by one frame at the video's reported frame rate. The preview seeks to the edited marker. An end before the start disables export. The timeline supports media longer than 24 hours; the time fields are disabled for those files.
5. Click **Play selection** next to **Play** to play the interval. In **Save as**, choose **H.264 (MP4)**, **H.265 (MP4, slow)**, or **Original streams (no re-encoding)**. Then click **Save selection**.

Left-click the video to pause or resume at the current position. Left-drag the video area or an empty part of the window to move the editor. When focus is outside the text and time fields, **Space** toggles playback at the current position, **Left Arrow** plays from IN, and **Right Arrow** plays from one second before OUT. If OUT is earlier than one second, **Right Arrow** plays from the beginning. Playback pauses when it reaches OUT from an earlier position. The **Pause** key also toggles playback. Ctrl+O opens a local file; Ctrl+S saves the selection; Esc closes the editor. A local filename or YouTube URL may also be passed on the command line.

**H.264** uses `libx264 -preset veryslow -crf 32 -tag:v avc1`. **H.265** uses `libx265 -preset slow -crf 32 -tag:v hvc1`. Both encoding modes re-encode the first video track using Main profile and `yuv420p`, and the optional first audio track using AAC at 128 kbps. They use `-movflags +faststart` and millisecond input times for accurate boundaries within video-frame and audio-packet resolution.

**Original streams** writes MP4 with faststart using FFmpeg stream copy. It keeps the source video and audio codecs, resolution, and quality. The app finds the video keyframe at or before IN, aligns audio to that start, and copies through OUT at packet boundaries. The clip can include footage before IN and a few frames or audio packets after OUT. The actual source start is shown after saving. Choose H.264 or H.265 for exact selection boundaries or when MP4 cannot hold the source codecs. Files without audio also work. YouTube exports retain the existing H.264/VP9 selection policy up to 1080p, independently of the lower-resolution streaming preview. Keyframe discovery copies one video packet through FFmpeg's framehash muxer and uses the same seek behavior as the final export.

The default export filename is `v.mp4`. Save selection remembers the last chosen export directory across app restarts, falling back to the Movies folder if that directory no longer exists. All modes write beside the destination under a temporary name, then atomically replace the destination only after successful completion. Cancellation and failed exports preserve an existing destination. The source file cannot be overwritten.

## Build on this machine

The default build uses `C:\Qt\6.11.2\msvc2022_arm64`, Visual Studio's ARM64 compiler, and CMake/Ninja in `C:\Qt\Tools`.

```powershell
# Requires Python 3.12+. Download checksum-verified FFmpeg, ffprobe, yt-dlp and Deno.
powershell -NoProfile -ExecutionPolicy Bypass -File scripts\setup-tools.ps1
powershell -NoProfile -ExecutionPolicy Bypass -File build.ps1 -Test
.\dist\VideoEditor.exe
```

Override `-QtRoot` and `-ToolsRoot` for another compatible MSVC Qt kit. For x64 helper programs use `scripts\setup-tools.ps1 -Architecture x64`. CMake requires Widgets, Multimedia, MultimediaWidgets, Network and Test. Open `CMakeLists.txt` in Qt Creator to develop with a matching kit.

The app searches `tools` beside its executable, its executable directory, then PATH for helpers. `VIDEO_EDITOR_FFMPEG`, `VIDEO_EDITOR_YT_DLP` and `VIDEO_EDITOR_DENO` can override their paths. The app passes Deno's location to yt-dlp so packaged YouTube support works without adding helpers to PATH. The build bundles helper programs from the project's `tools` directory, falling back to PATH. No machine-wide tool installation is changed. Update project helpers with `setup-tools.ps1` and rebuild when YouTube changes.

## Automated builds and packages

[CMake build](.github/workflows/build.yml), adapted from [video-player](https://github.com/indy256/video-player/blob/main/.github/workflows/build.yml), runs on pushes, pull requests and manual dispatches. It builds and tests these targets:

| Target | Download |
| --- | --- |
| Windows x64 | `ve-windows-x64.exe` |
| Windows ARM64 | `ve-windows-arm64.exe` |
| macOS ARM64 | `ve-macos-arm64.dmg` |
| Linux x64 | `ve-linux-x64.AppImage` |

Windows downloads are single portable executables that extract their runtime to a per-user cache on first launch. macOS downloads contain `VideoEditor.app`; Linux downloads are executable AppImages. Each includes Qt's multimedia backend, FFmpeg, yt-dlp and Deno. Windows ARM64 uses an x64 FFmpeg CLI under emulation and native ARM64 libraries for playback. Helper downloads are verified against upstream SHA-256 digests; FFmpeg build notes and licensing are included in the package.

CI rebuilds Qt from checksum-verified sources with LTO, caches it by Qt version and compiler identity, and checks the LTO manifest before building the editor. Release application and portable launcher builds also use LTO and size optimizations. Failed test logs are retained as artifacts. Packaged applications must re-encode a short clip and decode its first frame with system-only PATH settings before upload. Pushing a `vX.Y.Z` tag publishes all four packages as a GitHub release after every build succeeds.

For a local CMake build on any supported platform, use a Qt kit with Widgets, Multimedia, MultimediaWidgets and Network, plus Test when running tests. Set up a native compiler and Ninja on PATH, then run:

```sh
python scripts/setup-tools.py
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release -DCMAKE_PREFIX_PATH=/path/to/Qt -DBUILD_TESTING=ON -DVIDEO_EDITOR_DEPLOY=ON
cmake --build build --parallel 2
ctest --test-dir build --output-on-failure
cmake --install build --prefix package
```

Add the `tools` and Qt `bin` directories to PATH for local tests. `-DVIDEO_EDITOR_TOOLS_DIR=/path/to/helpers` selects another helper directory for deployment. `-DVIDEO_EDITOR_REQUIRE_QT_LTO=ON` requires a kit produced by `scripts/build-qt-lto.py`; ordinary local builds can use an installed Qt kit. `-DVIDEO_EDITOR_OPTIMIZE_SIZE=OFF` disables application LTO and size optimizations. Python build-script checks run with `python -m unittest discover -s tests -p "test_*.py"`.

The downloader ignores global yt-dlp configuration and processes one video per link. Public, finite videos are the intended input. Streaming rejects live videos and videos without a known duration. If a combined preview is unavailable, choose Download first. Stream URLs expire; reopen the link if playback stops after an extended session. Export always resolves fresh URLs. Streaming requires an internet connection while playing, seeking, and saving; Download first requires temporary disk space for the entire source video.

A loopback-only HTTP bridge translates player/FFmpeg seek requests into bounded CDN requests. It honors byte ranges, applies downloader headers, bounds queued data with backpressure, and aborts upstream requests when a player disconnects. No HTTP listener is exposed to other computers.

When YouTube supplies only an estimated file size, the app checks the server for the exact byte length using HEAD, with a one-byte HTTP range probe as a fallback. It never uses the estimate as a seekable file's length. These checks can be canceled without closing the app.

Completed streaming chunks are cached automatically on disk in fixed 2 MiB blocks. Seeking back, replaying, or reopening a video reuses those blocks, including after an app restart. Preview video and higher-quality export tracks have separate identities; each reuses data previously fetched for that same track. Cache identities include the video, format, content length, and media revision so refreshed signed URLs can reuse bytes while changed media cannot.

The streaming cache is limited to 2 GiB and evicts the least recently used chunks as new ones arrive. It lives in Qt's per-user application cache directory under `stream-chunks-v1`; set `VIDEO_EDITOR_CACHE_DIR` to choose another directory. You can remove that directory while the app is closed to clear the cache. Writes are atomic, only complete chunks are retained, and chunks are checked for corruption before playback. A full or unwritable cache does not prevent streaming. Reopening a YouTube video still needs network access to resolve its current stream URLs; cached portions then play without fetching those bytes again.

## Validation

`build.ps1 -Test` generates a five-second video with audio and checks actual Qt decoding, paused seeking, playback shortcuts, IN and OUT handle dragging, invalid selection controls, export duration, exported video playback, safe overwrite, source protection and cancellation cleanup. FFmpeg and ffprobe are needed for tests.

Original-stream export tests compare SHA-256 packet hashes for H.264 video with AAC or FLAC audio, verify a keyframe start and aligned audio, and decode the exported MP4. They also cover video-only input, incompatible audio, and cancellation during the keyframe check. The live streaming test saves a stream-copy MP4 with separate YouTube video and audio inputs and checks that it decodes.

To run a live YouTube download, decode and export integration check, set `VIDEO_EDITOR_TEST_YOUTUBE` to a short public video URL before running `build.ps1 -Test`. This check depends on network and YouTube availability and is skipped by default. Set `VIDEO_EDITOR_SCREENSHOT` to a PNG path to capture the app during its window workflow test.

The live check also compares decoded frame colors against an independently decoded FFmpeg reference, catching green frames even when Qt reports successful playback. The reported regression URL is `https://youtu.be/b0LkG52V-lI`. For an already downloaded file, set `VIDEO_EDITOR_REGRESSION_VIDEO` to its absolute path and run `build\editor_tests.exe videoColorRegression -platform windows` with Qt DLLs on PATH. Optionally set `VIDEO_EDITOR_REGRESSION_CAPTURE` to an output path prefix to save the decoded frame. Locally opened AV1 files still depend on the Qt kit and graphics driver's AV1 support.

Set `VIDEO_EDITOR_TEST_STREAM=https://www.youtube.com/watch?v=8N8jCOCf1VE` for a live streaming test. It plays the video with audio, seeks halfway through without downloading preceding footage, exports a 2.25-second interval there, checks the output duration and audio/video tracks, and decodes the saved file. It then resolves the video again and verifies that a previously loaded range is served from cache without additional CDN bytes. For videos longer than an hour it also checks that the total source bytes fetched stay below 100 MiB. Local tests check overlapping-range reuse after recreating the bridge, LRU eviction, and rejection of corrupt or partial chunks. Tests use an isolated temporary cache.

Qt, FFmpeg, yt-dlp and Deno retain their respective licenses. Review their redistribution terms before distributing a packaged build; the locally installed FFmpeg reports a GPL-enabled build.
