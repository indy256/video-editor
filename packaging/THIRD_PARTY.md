# Bundled software

Video Editor packages include Qt and its FFmpeg multimedia backend, plus these command-line helpers:

- FFmpeg and ffprobe: static builds from [eugeneware/ffmpeg-static](https://github.com/eugeneware/ffmpeg-static/releases/tag/b6.1.1). The upstream build notes and license are included beside FFmpeg. These builds enable GPL components, including libx264. Source and build instructions are linked in the upstream release.
- yt-dlp: [upstream releases and licensing](https://github.com/yt-dlp/yt-dlp#license).
- Deno: [upstream releases and MIT license](https://github.com/denoland/deno).
- Qt: [licensing and source](https://www.qt.io/licensing/open-source-lgpl-obligations). Qt source versions and SHA-256 hashes are recorded in `scripts/qt-source-hashes.json`.

Windows ARM64 packages use native ARM64 Qt, yt-dlp and Deno. The static FFmpeg CLI runs under Windows x64 emulation; it is separate from Qt's native multimedia libraries.
