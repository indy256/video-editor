"""Download checksum-verified, standalone helpers for release packages (Python 3.12+)."""
import argparse
import gzip
import hashlib
import json
import os
from pathlib import Path
import platform
import shutil
import subprocess
import sys
import tempfile
import time
import urllib.request
import zipfile


def release(repository, tag="latest"):
    endpoint = "latest" if tag == "latest" else "tags/" + tag
    headers = {"User-Agent": "video-editor-build", "Accept": "application/vnd.github+json"}
    token = os.environ.get("GH_TOKEN") or os.environ.get("GITHUB_TOKEN")
    if token:
        headers["Authorization"] = "Bearer " + token
    request = urllib.request.Request(f"https://api.github.com/repos/{repository}/releases/{endpoint}", headers=headers)
    with urllib.request.urlopen(request, timeout=60) as response:
        return json.load(response)


def download(asset, destination):
    digest = asset.get("digest") or ""
    if not digest.startswith("sha256:") or len(digest) != 71:
        raise RuntimeError(f"Missing SHA-256 digest for {asset['name']}")
    for attempt in range(3):
        try:
            with urllib.request.urlopen(asset["browser_download_url"], timeout=120) as response, destination.open("wb") as output:
                shutil.copyfileobj(response, output)
            with destination.open("rb") as source:
                actual = hashlib.file_digest(source, "sha256").hexdigest()
            if actual != digest[7:]:
                raise RuntimeError(f"Checksum mismatch: {asset['name']}")
            return
        except OSError:
            if attempt == 2:
                raise
            time.sleep(2 ** attempt)


def install_asset(info, name, destination, member=None):
    asset = next(asset for asset in info["assets"] if asset["name"] == name)
    destination.parent.mkdir(parents=True, exist_ok=True)
    with tempfile.TemporaryDirectory() as work:
        archive = Path(work) / name
        download(asset, archive)
        output = Path(work) / "output"
        if member:
            with zipfile.ZipFile(archive) as package, package.open(member) as source, output.open("wb") as target:
                shutil.copyfileobj(source, target)
        elif name.endswith(".gz"):
            with gzip.open(archive, "rb") as source, output.open("wb") as target:
                shutil.copyfileobj(source, target)
        else:
            output = archive
        shutil.copyfile(output, destination)
    destination.chmod(0o755)
    print(f"{info['tag_name']}: verified {name}", flush=True)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--arch", choices=("x64", "arm64"), default="arm64" if platform.machine().lower() in ("arm64", "aarch64") else "x64")
    parser.add_argument("--directory", type=Path, default=Path(__file__).resolve().parents[1] / "tools")
    args = parser.parse_args()
    if sys.platform == "win32":
        suffix, ffmpeg_target = ".exe", "win32-x64"
        yt_asset = "yt-dlp_arm64.exe" if args.arch == "arm64" else "yt-dlp.exe"
        deno_target = "aarch64-pc-windows-msvc" if args.arch == "arm64" else "x86_64-pc-windows-msvc"
    elif sys.platform == "darwin":
        suffix, ffmpeg_target, yt_asset = "", "darwin-" + args.arch, "yt-dlp_macos"
        deno_target = ("aarch64" if args.arch == "arm64" else "x86_64") + "-apple-darwin"
    elif sys.platform.startswith("linux") and args.arch == "x64":
        suffix, ffmpeg_target, yt_asset, deno_target = "", "linux-x64", "yt-dlp_linux", "x86_64-unknown-linux-gnu"
    else:
        parser.error("Supported targets: Windows x64/ARM64, macOS x64/ARM64, Linux x64")
    ffmpeg = release("eugeneware/ffmpeg-static", "b6.1.1")
    for tool in ("ffmpeg", "ffprobe"):
        install_asset(ffmpeg, f"{tool}-{ffmpeg_target}.gz", args.directory / (tool + suffix))
    for name in ("LICENSE", "README"):
        install_asset(ffmpeg, f"{ffmpeg_target}.{name}", args.directory / ("ffmpeg." + name))
    install_asset(release("yt-dlp/yt-dlp"), yt_asset, args.directory / ("yt-dlp" + suffix))
    install_asset(release("denoland/deno"), f"deno-{deno_target}.zip", args.directory / ("deno" + suffix), "deno" + suffix)
    for tool, flag in (("ffmpeg", "-version"), ("ffprobe", "-version"), ("yt-dlp", "--version"), ("deno", "--version")):
        subprocess.run([str((args.directory / (tool + suffix)).resolve()), flag], check=True, stdout=subprocess.DEVNULL)
    if os.environ.get("GITHUB_PATH"):
        with open(os.environ["GITHUB_PATH"], "a", encoding="utf-8") as output:
            output.write(str(args.directory.resolve()) + "\n")


if __name__ == "__main__":
    main()
