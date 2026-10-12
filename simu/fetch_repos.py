from __future__ import annotations

import json
import subprocess
from pathlib import Path


def apply_repo_patch(repo_path: Path, patch_path: Path) -> None:
    check = subprocess.run(
        ["git", "-C", repo_path, "apply", "--check", patch_path],
        capture_output=True,
    )
    if check.returncode == 0:
        subprocess.run(
            ["git", "-C", repo_path, "apply", patch_path],
            check=True,
        )
        print(f"applied {patch_path.name} to {repo_path}")
        return

    reverse_check = subprocess.run(
        ["git", "-C", repo_path, "apply", "--reverse", "--check", patch_path],
        capture_output=True,
    )
    if reverse_check.returncode == 0:
        print(f"{patch_path.name} is already applied to {repo_path}")
        return

    raise RuntimeError(f"cannot apply {patch_path} cleanly to {repo_path}")


def clone_or_update_repo(
    repo_url: str,
    repo_path: Path,
    ref: str,
    patch_path: Path | None = None,
) -> None:
    if not repo_path.exists():
        subprocess.run(["git", "clone", repo_url, repo_path], check=True)
    else:
        subprocess.run(
            ["git", "-C", repo_path, "fetch", "--tags"],
            check=True,
        )

    subprocess.run(["git", "-C", repo_path, "checkout", ref], check=True)
    if patch_path is not None:
        apply_repo_patch(repo_path, patch_path)


def fetch_dependencies() -> None:
    script_dir = Path(__file__).resolve().parent
    with (script_dir / "repos.json").open(encoding="utf-8") as config_file:
        repos = json.load(config_file)

    for repo in repos:
        patch_path = repo.get("patch")
        clone_or_update_repo(
            repo["url"],
            script_dir / repo["path"],
            repo["ref"],
            script_dir / patch_path if patch_path else None,
        )


if __name__ == "__main__":
    fetch_dependencies()
