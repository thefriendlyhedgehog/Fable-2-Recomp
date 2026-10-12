"""Reproducible Windows dependency fallback for the 1338ec1 renderer SDK.

The SDK points at an unpublished libmspack commit. Use its previous public pin
and materialize that dependency's symlink files on Windows. No game data is read.
Run only against a dedicated SDK dependency checkout, not a libmspack worktree.
"""
import argparse
from pathlib import Path
import shutil
import subprocess

PUBLIC_PIN = "305907723a4e7ab2018e58040059ffb5e77db837"


def git(directory, *args):
    return subprocess.check_output(["git", "-C", str(directory), *args], text=True).strip()


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("dependency", type=Path)
    args = parser.parse_args()
    dependency = args.dependency.resolve()
    if git(dependency, "rev-parse", "HEAD") != PUBLIC_PIN:
        raise SystemExit(f"Check out public libmspack pin {PUBLIC_PIN} in the dedicated dependency first.")
    # Only symlink blobs from the pinned tree may be overwritten, and only with
    # their original referenced sources inside this exact checkout.
    entries = git(dependency, "ls-tree", "-r", "HEAD").splitlines()
    count = 0
    for entry in entries:
        metadata, relative = entry.split("\t", 1)
        if not metadata.startswith("120000 "):
            continue
        target_text = git(dependency, "show", f"HEAD:{relative}")
        destination = dependency / relative
        source = (destination.parent / target_text).resolve()
        if not source.is_relative_to(dependency) or not source.is_file():
            raise SystemExit(f"Invalid pinned symlink target: {relative}")
        if destination.is_symlink():
            continue
        if destination.exists() and destination.read_bytes() not in (
                target_text.encode(), (target_text + "\n").encode(), source.read_bytes()):
            raise SystemExit(f"Unexpected local edit; refusing to overwrite {destination}")
        shutil.copyfile(source, destination)
        count += 1
    print(f"Materialized {count} pinned libmspack symlink files; renderer code unchanged.")


if __name__ == "__main__":
    main()
