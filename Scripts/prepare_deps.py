"""Fetch pinned dependencies and build the gifski static library for xmake."""

import argparse
import hashlib
import os
from pathlib import Path
import subprocess
import sys

ROOT = Path(__file__).resolve().parent.parent
DEPS = ROOT / "build" / "deps"
REPOSITORIES = {
    "Ling": (
        "https://github.com/xland/Ling.git",
        "b77448ec7e72b1b961d3c6f8f87502d8df72aa5f",
        None,
    ),
    "gifski": (
        "https://github.com/ImageOptim/gifski.git",
        "1060eab4500a20f27e2fa3ab7e85473d0e921cbd",
        "1.34.0",
    ),
}


def run(args, cwd=ROOT, capture=False):
    result = subprocess.run(
        args, cwd=cwd, check=True, stdin=subprocess.DEVNULL,
        stdout=subprocess.PIPE if capture else None,
        encoding="utf-8", errors="replace",
    )
    return result.stdout if capture else None


def prepare_sources():
    DEPS.mkdir(parents=True, exist_ok=True)
    for name, (url, revision, tag) in REPOSITORIES.items():
        destination = DEPS / name
        if not destination.exists():
            if tag:
                run(["git", "clone", "--depth", "1", "--branch", tag, url, str(destination)])
            else:
                destination.mkdir()
                run(["git", "init", str(destination)])
                run(["git", "remote", "add", "origin", url], cwd=destination)
                run(["git", "fetch", "--depth", "1", "origin", revision], cwd=destination)
                run(["git", "checkout", "--detach", "FETCH_HEAD"], cwd=destination)
        actual = run(["git", "rev-parse", "HEAD"], cwd=destination, capture=True).strip()
        if actual != revision:
            raise RuntimeError(f"{destination} must be at {revision}; found {actual}.")
        patch = ROOT / "Scripts" / "patches" / "ling-message-window.patch"
        if name == "Ling":
            # Accept only our pinned patch, preserving the dependency integrity check.
            original = run(["git", "show", "HEAD:include/App.h"],
                           cwd=destination, capture=True)
            patched = original.replace("HWND msgHwnd;", "HWND msgHwnd{ nullptr };")
            current = (destination / "include" / "App.h").read_text(encoding="utf-8")
            changed = run(["git", "diff", "--name-only", "HEAD"],
                          cwd=destination, capture=True).strip()
            if changed not in ("", "include/App.h") or current not in (original, patched):
                raise RuntimeError(f"{destination} has modified dependency sources.")
            if current == original:
                run(["git", "apply", "--check", str(patch)], cwd=destination)
                run(["git", "apply", str(patch)], cwd=destination)
        elif run(["git", "status", "--porcelain", "--untracked-files=no"],
                 cwd=destination, capture=True):
            raise RuntimeError(f"{destination} has modified dependency sources.")


def build_gifski():
    directory = DEPS / "gifski"
    library = directory / "target/x86_64-pc-windows-msvc/release/gifski.lib"
    stamp = directory / "target/screencapture-build.sha256"
    flags = "-C target-feature=+crt-static"
    fingerprint = hashlib.sha256(
        (REPOSITORIES["gifski"][1] + flags).encode()
        + Path(__file__).read_bytes()
        + (directory / "Cargo.lock").read_bytes()
        + run(["cargo", "--version"], capture=True).encode()
    ).hexdigest()
    if library.is_file() and stamp.is_file() and stamp.read_text(encoding="utf-8") == fingerprint:
        return
    environment = os.environ.copy()
    environment["RUSTFLAGS"] = flags
    environment["CARGO_NET_GIT_FETCH_WITH_CLI"] = "true"
    subprocess.run(
        ["cargo", "build", "--locked", "--release", "--lib", "--no-default-features",
         "--features", "gifsicle", "--target", "x86_64-pc-windows-msvc"],
        cwd=directory, env=environment, stdin=subprocess.DEVNULL, check=True,
    )
    if not library.is_file():
        raise RuntimeError(f"Cargo did not create {library}.")
    stamp.write_text(fingerprint, encoding="utf-8")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--gifski", action="store_true", help="Build gifski after fetching sources.")
    args = parser.parse_args()
    if sys.platform != "win32":
        parser.error("ScreenCapture requires Windows.")
    prepare_sources()
    if args.gifski:
        build_gifski()


if __name__ == "__main__":
    main()
