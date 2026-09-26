"""Publish the CMake version after the complete GitHub Actions build matrix passes.

Only main pushes/manual runs and matching version-tag pushes can publish. A
published release is immutable; interrupted drafts can resume at the same commit.
"""
import argparse
import json
import os
from pathlib import Path
import re
import subprocess
import tempfile
import urllib.error
import urllib.request

import release
import reapack


def release_tag(event, ref, version):
    tag = f"v{version}"
    if event in ("push", "workflow_dispatch") and ref == "refs/heads/main":
        return tag
    if event == "push" and ref == f"refs/tags/{tag}":
        return tag
    raise ValueError(f"Release requires main or a pushed tag matching CMake version {tag}")


def asset_names(version):
    binaries=[name for pair in release.PLATFORMS.values() for name in pair if name]
    return sorted([release.asset_name(platform, version) for platform in release.PLATFORMS] + binaries + [reapack.bundle_name(version),'SHA256SUMS.txt'])


class GitHub:
    def __init__(self, repository, token):
        if not re.fullmatch(r"[A-Za-z0-9_.-]+/[A-Za-z0-9_.-]+", repository):
            raise ValueError("GITHUB_REPOSITORY must be owner/name")
        self.base = f"https://api.github.com/repos/{repository}/"
        self.token = token

    def get(self, path):
        request = urllib.request.Request(self.base + path, headers={
            "Accept": "application/vnd.github+json",
            "Authorization": f"Bearer {self.token}",
            "X-GitHub-Api-Version": "2022-11-28",
        })
        try:
            with urllib.request.urlopen(request, timeout=60) as response:
                return json.load(response)
        except urllib.error.HTTPError as error:
            if error.code == 404:
                return None
            # Authentication, rate limits and service errors must never be
            # mistaken for a missing release. Do not print credentials.
            raise RuntimeError(f"GitHub API failed: HTTP {error.code} for {path}") from None


def tag_commit(api, tag):
    ref = api.get(f"git/ref/tags/{tag}")
    if ref is None:
        return None
    obj = ref["object"]
    for _ in range(10):
        if obj["type"] == "commit":
            return obj["sha"]
        if obj["type"] != "tag":
            break
        annotation = api.get(f"git/tags/{obj['sha']}")
        if annotation is None:
            break
        obj = annotation["object"]
    raise ValueError(f"Cannot resolve {tag} to a commit")


def find_release(api, tag):
    existing = api.get(f"releases/tags/{tag}")
    if existing is not None:
        return existing
    # The by-tag endpoint documents published releases only. Drafts (including
    # ones without a Git tag yet) must be found through the authenticated list.
    page = 1
    while True:
        entries = api.get(f"releases?per_page=100&page={page}")
        if entries is None:
            raise ValueError("Cannot list repository releases; check Actions permissions")
        for entry in entries:
            if entry["tag_name"] == tag:
                return entry
        if len(entries) < 100:
            return None
        page += 1


def plan(api, tag, sha, expected):
    existing = find_release(api, tag)
    if existing is not None and not existing["draft"]:
        assets = {item["name"] for item in existing.get("assets", []) if item.get("size", 0) > 0}
        if not set(expected).issubset(assets):
            raise ValueError(f"Published {tag} has missing assets; do not overwrite it. Bump the CMake version.")
        return "skip"
    commit = tag_commit(api, tag)
    if commit is not None and commit != sha:
        raise ValueError(f"{tag} points to another commit. Bump the CMake version; existing tags are never moved.")
    if existing is not None:
        if commit is None and existing.get("target_commitish") != sha:
            raise ValueError(f"Draft {tag} targets another commit. Bump the CMake version.")
        return "resume"
    return "create"


def publish(api, directory, repository, tag, sha, expected, run=subprocess.run):
    paths = [directory / name for name in expected]
    if any(not path.is_file() or path.stat().st_size == 0 for path in paths):
        raise ValueError("Refusing to publish: incomplete release directory")
    mode = plan(api, tag, sha, expected)
    if mode == "skip":
        print(f"{tag} is already published; no files were changed.")
        return
    def gh(*args):
        run(["gh", "release", *args, "--repo", repository], check=True)
    if mode == "create":
        gh("create", tag, "--target", sha, "--title", f"ReaGBA {tag[1:]}",
           "--draft")
    # A failed upload leaves an unpublished draft. A retry may replace its
    # partial files only after plan() verifies the exact source commit again.
    gh("upload", tag, *[str(path) for path in paths], "--clobber")
    with tempfile.TemporaryDirectory() as temporary:
        notes = Path(temporary) / "release-notes.md"
        notes.write_text(''.join(f'- {line}\n' for line in reapack.CHANGELOG), encoding="utf-8")
        gh("edit", tag, "--notes-file", str(notes), "--draft=false")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    mode = parser.add_mutually_exclusive_group(required=True)
    mode.add_argument("--plan", action="store_true")
    mode.add_argument("--publish", type=Path, help="Validated output of scripts/release.py")
    args = parser.parse_args()
    version = release.version()
    tag = release_tag(os.environ["GITHUB_EVENT_NAME"], os.environ["GITHUB_REF"], version)
    sha = os.environ["GITHUB_SHA"]
    if not re.fullmatch(r"[a-f0-9]{40}", sha):
        raise ValueError("GITHUB_SHA must identify the exact built commit")
    repository = os.environ["GITHUB_REPOSITORY"]
    api = GitHub(repository, os.environ["GH_TOKEN"])
    expected = asset_names(version)
    if args.plan:
        action = plan(api, tag, sha, expected)
        with open(os.environ["GITHUB_OUTPUT"], "a", encoding="utf-8") as output:
            output.write(f"tag={tag}\npublish={'false' if action == 'skip' else 'true'}\n")
        print(f"{tag}: {action}. Published versions are never overwritten.")
    else:
        publish(api, args.publish, repository, tag, sha, expected)


if __name__ == "__main__":
    main()
