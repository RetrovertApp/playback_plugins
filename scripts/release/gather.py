#!/usr/bin/env python3
"""Gather a channel's release set from the per-plugin builds caches.

Resolves the roster's pinned submodule revisions into provenance-attested
artifacts on each plugin repo's rolling `builds` release, dispatches builds
only for what is missing, and stages the verified set in channel form next
to the release-set manifest:

    out/
      artifacts/<name>-<target>.tar.zst
      manifest.json

Assembles and verifies; never compiles. An artifact is accepted only if a
build-provenance attestation binds its digest to the pinned source commit
and the roster's harness tag; filenames are convenience, never identity.
"""

import argparse
import hashlib
import json
import os
import re
import shutil
import subprocess
import sys
import time
import tomllib
from datetime import datetime, timezone
from pathlib import Path

TARGETS = ["linux-x86_64", "linux-arm64", "windows-x86_64"]
SIGNER_WORKFLOW = "RetrovertApp/retrovert-build-harness/.github/workflows/build-plugin.yml"
PREDICATE_TYPE = "https://slsa.dev/provenance/v1"
CALLER_WORKFLOW = "release-build.yml"
DISPATCH_TIMEOUT_S = 45 * 60
DISPATCH_POLL_S = 20


def info(msg):
    print(f"gather: {msg}", flush=True)


def fail(msg):
    print(f"gather: error: {msg}", file=sys.stderr, flush=True)
    sys.exit(1)


def run(args, **kwargs):
    return subprocess.run(args, check=True, capture_output=True, text=True, **kwargs).stdout


def gh_json(args, token=None):
    env = dict(os.environ, GH_TOKEN=token) if token else None
    out = subprocess.run(["gh"] + args, check=True, capture_output=True, text=True, env=env).stdout
    return json.loads(out) if out.strip() else None


def load_roster(channel):
    path = Path("channels") / channel / "roster.toml"
    if not path.is_file():
        fail(f"no roster for channel '{channel}' ({path} missing)")
    roster = tomllib.loads(path.read_text())
    harness = roster.get("harness", "")
    plugins = roster.get("plugins", [])
    if not re.fullmatch(r"v[0-9]+", harness):
        fail(f"roster harness tag '{harness}' is not of the form vN")
    if not plugins:
        fail(f"roster for '{channel}' lists no plugins")
    return harness, sorted(plugins)


def submodule_repos():
    """Map submodule path -> owner/repo from .gitmodules."""
    out = run(["git", "config", "-f", ".gitmodules", "--get-regexp", r"submodule\..*\.url"])
    repos = {}
    for line in out.splitlines():
        key, url = line.split(" ", 1)
        path = key[len("submodule.") : -len(".url")]
        m = re.search(r"github\.com[:/]([^/]+)/(.+?)(\.git)?$", url.strip())
        if not m:
            fail(f"cannot parse submodule url '{url}'")
        repos[path] = f"{m.group(1)}/{m.group(2)}"
    return repos


def pinned_revision(path):
    try:
        return run(["git", "rev-parse", f"HEAD:{path}"]).strip()
    except subprocess.CalledProcessError:
        fail(f"no submodule pinned at {path}")


def builds_assets(repo):
    try:
        release = gh_json(["api", f"repos/{repo}/releases/tags/builds"])
    except subprocess.CalledProcessError:
        return {}
    return {a["name"]: a for a in release.get("assets", [])}


def matching_asset(assets, name, target, hv, pin):
    pattern = re.compile(rf"^{re.escape(name)}-{re.escape(target)}-([0-9a-f]+)-{re.escape(hv)}\.tar\.zst$")
    for asset_name in assets:
        m = pattern.match(asset_name)
        if m and pin.startswith(m.group(1)):
            return asset_name
    return None


def dispatch_and_wait(repo, pin, token):
    """Dispatch the caller workflow on the plugin repo and wait for it.

    workflow_dispatch builds a branch head, so the pin must still be that
    head — a historical pin can never regain a valid attestation and needs
    a pin bump instead.
    """
    if not token:
        fail(f"{repo}: artifacts missing and no dispatch token available")
    repo_info = gh_json(["api", f"repos/{repo}"])
    branch = repo_info["default_branch"]
    head = gh_json(["api", f"repos/{repo}/commits/{branch}"])["sha"]
    if head != pin:
        fail(
            f"{repo}: pinned revision {pin[:12]} is not the head of '{branch}' ({head[:12]}); "
            f"the builds cache holds no artifact for it and a dispatched build would attest a "
            f"different commit. Bump the pin to a built revision."
        )
    started = datetime.now(timezone.utc).strftime("%Y-%m-%dT%H:%M:%SZ")
    subprocess.run(
        ["gh", "api", "-X", "POST", f"repos/{repo}/actions/workflows/{CALLER_WORKFLOW}/dispatches", "-f", f"ref={branch}"],
        check=True,
        capture_output=True,
        text=True,
        env=dict(os.environ, GH_TOKEN=token),
    )
    info(f"{repo}: dispatched {CALLER_WORKFLOW} on {branch} for {pin[:12]}")
    deadline = time.monotonic() + DISPATCH_TIMEOUT_S
    while time.monotonic() < deadline:
        time.sleep(DISPATCH_POLL_S)
        runs = gh_json(
            [
                "api",
                "-X",
                "GET",
                f"repos/{repo}/actions/workflows/{CALLER_WORKFLOW}/runs",
                "-f",
                "event=workflow_dispatch",
                "-f",
                f"created=>={started}",
            ]
        )["workflow_runs"]
        runs = [r for r in runs if r["head_sha"] == pin]
        if not runs:
            continue
        newest = max(runs, key=lambda r: r["run_number"])
        if newest["status"] == "completed":
            if newest["conclusion"] == "success":
                info(f"{repo}: dispatched run {newest['html_url']} succeeded")
                return
            fail(f"{repo}: dispatched build concluded '{newest['conclusion']}' ({newest['html_url']})")
    fail(f"{repo}: dispatched build did not complete within {DISPATCH_TIMEOUT_S // 60} minutes")


def download_asset(repo, asset_name, dest_dir):
    run(["gh", "release", "download", "builds", "--repo", repo, "--pattern", asset_name, "--dir", str(dest_dir), "--clobber"])
    path = dest_dir / asset_name
    if not path.is_file():
        fail(f"{repo}: download of {asset_name} produced no file")
    return path


def statements(verify_output):
    """Every in-toto statement in `gh attestation verify --format json` output."""
    found = []

    def walk(node):
        if isinstance(node, dict):
            if "predicateType" in node and "predicate" in node:
                found.append(node)
            for value in node.values():
                walk(value)
        elif isinstance(node, list):
            for value in node:
                walk(value)

    walk(verify_output)
    return found


def verify_provenance(path, repo, pin, harness):
    try:
        out = run(
            [
                "gh",
                "attestation",
                "verify",
                str(path),
                "--repo",
                repo,
                "--signer-workflow",
                SIGNER_WORKFLOW,
                "--predicate-type",
                PREDICATE_TYPE,
                "--format",
                "json",
            ]
        )
    except subprocess.CalledProcessError as err:
        fail(f"{path.name}: attestation verification failed: {err.stderr.strip()}")
    expected_builder = f"https://github.com/{SIGNER_WORKFLOW}@refs/tags/{harness}"
    # Repo capitalization differs between .gitmodules and the attestation URI;
    # GitHub treats owner/name case-insensitively.
    expected_uri = f"https://github.com/{repo}@".lower()
    for statement in statements(json.loads(out)):
        predicate = statement["predicate"]
        builder = predicate.get("runDetails", {}).get("builder", {}).get("id", "")
        if builder != expected_builder:
            continue
        for dep in predicate.get("buildDefinition", {}).get("resolvedDependencies", []):
            uri = dep.get("uri", "").lower()
            commit = dep.get("digest", {}).get("gitCommit", "")
            if expected_uri in uri and commit == pin:
                return
    fail(
        f"{path.name}: no attestation binds this artifact to {repo}@{pin[:12]} "
        f"built by {SIGNER_WORKFLOW}@{harness}"
    )


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--channel", required=True)
    parser.add_argument("--version", required=True, type=int)
    parser.add_argument("--out", required=True, type=Path)
    parser.add_argument("--dispatch-token-env", default="DISPATCH_TOKEN")
    args = parser.parse_args()

    dispatch_token = os.environ.get(args.dispatch_token_env, "")

    harness, plugins = load_roster(args.channel)
    hv = "h" + harness[1:]
    repos = submodule_repos()
    source_revision = run(["git", "rev-parse", "HEAD"]).strip()

    artifacts_dir = args.out / "artifacts"
    scratch_dir = args.out / "downloads"
    artifacts_dir.mkdir(parents=True, exist_ok=True)
    scratch_dir.mkdir(parents=True, exist_ok=True)

    manifest_artifacts = []
    for name in plugins:
        path = f"plugins/{name}"
        if path not in repos:
            fail(f"roster plugin '{name}' has no submodule at {path}")
        # Canonicalize the owner/name case; .gitmodules spells it lowercase
        # and gh's certificate matching may not.
        repo = gh_json(["api", f"repos/{repos[path]}"])["full_name"]
        pin = pinned_revision(path)
        info(f"{name}: {repo}@{pin[:12]} ({hv})")

        assets = builds_assets(repo)
        missing = [t for t in TARGETS if not matching_asset(assets, name, t, hv, pin)]
        if missing:
            info(f"{name}: missing {', '.join(missing)}; dispatching a build")
            dispatch_and_wait(repo, pin, dispatch_token)
            assets = builds_assets(repo)

        for target in TARGETS:
            asset_name = matching_asset(assets, name, target, hv, pin)
            if not asset_name:
                fail(f"{name}: no {target} artifact for {pin[:12]} at {hv} after dispatch (complete-release)")
            downloaded = download_asset(repo, asset_name, scratch_dir)
            verify_provenance(downloaded, repo, pin, harness)
            digest = hashlib.sha256(downloaded.read_bytes()).hexdigest()
            channel_name = f"{name}-{target}.tar.zst"
            staged = artifacts_dir / channel_name
            shutil.copyfile(downloaded, staged)
            manifest_artifacts.append(
                {
                    "name": name,
                    "target": target,
                    "path": channel_name,
                    "sha256": digest,
                    "size": staged.stat().st_size,
                    "revision": pin,
                }
            )
            info(f"{name}: verified {asset_name} -> {channel_name} ({digest[:12]}…)")

    manifest_artifacts.sort(key=lambda a: (a["name"], a["target"]))
    manifest = {
        "schema": 1,
        "version": args.version,
        "source_revision": source_revision,
        "published": datetime.now(timezone.utc).strftime("%Y-%m-%dT%H:%M:%SZ"),
        "artifacts": manifest_artifacts,
    }
    manifest_path = args.out / "manifest.json"
    manifest_bytes = (json.dumps(manifest, indent=2) + "\n").encode()
    manifest_path.write_bytes(manifest_bytes)
    manifest_digest = hashlib.sha256(manifest_bytes).hexdigest()
    shutil.rmtree(scratch_dir)

    info(f"staged {len(manifest_artifacts)} artifacts for {args.channel}/v{args.version}")
    info(f"manifest sha256 {manifest_digest} (the generation id if this set publishes)")
    print(manifest_digest)


if __name__ == "__main__":
    main()
