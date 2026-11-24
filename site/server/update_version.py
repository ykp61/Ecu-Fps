#!/usr/bin/env python3
"""Writes /opt/ecufps-site/v/version.json from the latest public GitHub release.

Runs from a systemd timer every 5 minutes. Only reads the public GitHub API; on any error the
previous version.json stays untouched (the page also has a built-in fallback).
"""
import json
import os
import re
import sys
import tempfile
import urllib.request

REPO = "ykp61/Ecu-Fps-releases"
OUT = os.environ.get("ITX_SITE_VERSION_FILE", "/opt/ecufps-site/v/version.json")
API = f"https://api.github.com/repos/{REPO}/releases/latest"
PREFIX = f"https://github.com/{REPO}/releases/download/"


def pick_asset(assets):
    for pattern in (r"^EcuFps-Setup-[\d.]+\.exe$", r"^EcuFps\.exe$"):
        for a in assets:
            if re.match(pattern, a.get("name", "")):
                return a
    return None


def main():
    req = urllib.request.Request(API, headers={"Accept": "application/vnd.github+json", "User-Agent": "ecufps-site-version"})
    with urllib.request.urlopen(req, timeout=20) as r:
        rel = json.load(r)
    version = str(rel.get("tag_name", "")).lstrip("v")
    if not re.fullmatch(r"\d+(\.\d+){1,3}", version):
        sys.exit(f"bad tag {rel.get('tag_name')!r}")
    asset = pick_asset(rel.get("assets", []))
    if not asset:
        sys.exit("no exe asset")
    url = asset.get("browser_download_url", "")
    if not url.startswith(PREFIX):
        sys.exit("unexpected asset url")
    digest = str(asset.get("digest") or "")
    sha = digest[7:] if digest.startswith("sha256:") else ""
    data = {
        "version": version,
        "date": rel.get("published_at"),
        "size": asset.get("size"),
        "url": url,
        "sha256": sha if re.fullmatch(r"[0-9a-f]{64}", sha) else "",
    }
    d = os.path.dirname(OUT)
    fd, tmp = tempfile.mkstemp(dir=d, prefix=".version.")
    with os.fdopen(fd, "w") as f:
        json.dump(data, f)
    os.chmod(tmp, 0o644)
    os.replace(tmp, OUT)
    print(json.dumps(data))


if __name__ == "__main__":
    main()
