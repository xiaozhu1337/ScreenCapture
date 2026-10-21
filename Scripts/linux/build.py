"""Build and verify the Linux application and its pinned ImageReader fork."""
from pathlib import Path
import argparse
import os
import shutil
import subprocess
import sys

ROOT = Path(__file__).resolve().parents[2]
OCR = ROOT / "build/deps/ImageReader"
REVISION = "f755df2e501754c1679f6cac75c48bf8e287c463"
PROXY = "http://127.0.0.1:7890"
ENV = {**os.environ, "HTTP_PROXY": PROXY, "HTTPS_PROXY": PROXY}

def run(*args, cwd=ROOT, env=ENV):
    subprocess.run(args, cwd=cwd, env=env, check=True)

def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--install", type=Path, help="Install under this prefix after testing")
    args = parser.parse_args()
    if sys.platform != "linux" or os.uname().machine != "x86_64":
        parser.error("This build recipe currently supports Linux x86_64")
    if not (OCR / ".git").exists():
        OCR.parent.mkdir(parents=True, exist_ok=True)
        run("git", "clone", "--single-branch", "--branch", "linux-support",
            "https://github.com/xiaozhu1337/ImageReader.git", str(OCR))
    run("git", "fetch", "origin", REVISION, cwd=OCR)
    run("git", "checkout", "--detach", REVISION, cwd=OCR)
    run("mise", "trust", str(OCR / "mise.toml"), cwd=OCR)
    run("mise", "install", cwd=OCR)
    run("mise", "exec", "--", "xmake", "f", "-P", str(OCR), "-p", "linux", "-a", "x86_64", "-m", "release", "-y", cwd=OCR)
    run("mise", "exec", "--", "xmake", "build", "-P", str(OCR), "ImageReader", "LinuxGuiTests", cwd=OCR)
    run("mise", "exec", "--", "python", "Scripts/test_linux.py", cwd=OCR)
    run("mise", "exec", "--", "xmake", "f", "-p", "linux", "-a", "x86_64", "-m", "release", "-y")
    run("mise", "exec", "--", "xmake")
    output = ROOT / "build/linux/x86_64/release"
    plugin = OCR / "build/linux/x86_64/release"
    shutil.copy2(plugin / "ImageReader", output / "ImageReader")
    for name in ("models", "lib"):
        shutil.copytree(plugin / name, output / name, dirs_exist_ok=True)
    run("mise", "exec", "--", "xmake", "build", "ScreenCaptureTests")
    tests = {**ENV, "QT_QPA_PLATFORM": "offscreen", "QT_FORCE_STDERR_LOGGING": "1",
             "PATH": str(output) + os.pathsep + os.environ.get("PATH", "")}
    run(str(ROOT / "build/tests/release/ScreenCaptureTests"), env=tests)
    for target in ("ScreenCaptureDesktopTests", "ScreenCaptureSettingsTests", "ScreenCapturePortalTests",
                   "ScreenCaptureCanvasParityTests", "ScreenCaptureRecordingTests", "ScreenCaptureMediaRegressionTests"):
        run("mise", "exec", "--", "xmake", "build", target)
        run("dbus-run-session", "--", str(ROOT / "build/tests/release" / target), env=tests)
    if args.install:
        run("mise", "exec", "--", "xmake", "install", "-o", str(args.install.resolve()), "ScreenCapture")
    print(f"Ready: {output / 'ScreenCapture'}")

if __name__ == "__main__":
    main()
