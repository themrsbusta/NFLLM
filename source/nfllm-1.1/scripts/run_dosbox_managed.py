"""Run one isolated DOSBox-X batch under the shared process supervisor."""

from __future__ import annotations

import argparse
import os
from pathlib import Path
import re
import subprocess
import sys


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--work-dir", required=True, type=Path)
    parser.add_argument("--batch", required=True)
    parser.add_argument("--task-id", required=True)
    parser.add_argument("--timeout", type=int, default=180)
    args = parser.parse_args()

    if not re.fullmatch(r"[A-Za-z0-9_]{1,8}\.BAT", args.batch, re.IGNORECASE):
        print("ERROR: --batch must be an 8.3 DOS batch filename.", file=sys.stderr)
        return 2
    if not re.fullmatch(r"[a-zA-Z0-9][a-zA-Z0-9._-]{0,63}", args.task_id):
        print("ERROR: invalid task ID.", file=sys.stderr)
        return 2
    if not 1 <= args.timeout <= 600:
        print("ERROR: timeout must be between 1 and 600 seconds.", file=sys.stderr)
        return 2

    work_dir = args.work_dir.resolve()
    if not work_dir.is_dir():
        print(f"ERROR: DOSBox work directory is missing: {work_dir}", file=sys.stderr)
        return 2
    # DOSBox-X receives the path as a shell command. Reject guest-shell metacharacters.
    if not re.fullmatch(r"[A-Za-z]:\\[A-Za-z0-9_ .\\-]+", str(work_dir)):
        print("ERROR: work path contains unsupported DOS shell characters.", file=sys.stderr)
        return 2

    project_root = Path(__file__).resolve().parents[2]
    supervisor_src = project_root / "HAME-Codex-Bridge" / "src"
    bridge_home = project_root / "tools" / "mica-codex-bridge" / "data"
    if not (supervisor_src / "process_supervisor.py").is_file():
        print("ERROR: shared process supervisor source is unavailable.", file=sys.stderr)
        return 3
    if not (bridge_home / "process-registry.json").is_file():
        print("ERROR: shared process registry is unavailable.", file=sys.stderr)
        return 3

    sys.path.insert(0, str(supervisor_src))
    try:
        from process_supervisor import ProcessRegistry
    except Exception as exc:  # Import errors fail closed before launching DOSBox-X.
        print(f"ERROR: cannot load shared process supervisor: {exc}", file=sys.stderr)
        return 3

    dosbox = Path(os.environ.get("DOSBOX_X", r"C:\DOSBox-X\dosbox-x.exe")).resolve()
    if not dosbox.is_file():
        print(f"ERROR: DOSBox-X executable is missing: {dosbox}", file=sys.stderr)
        return 3

    command = [
        str(dosbox),
        "-noautoexec",
        "-silent",
        "-exit",
        "-c",
        f"mount c {work_dir}",
        "-c",
        "c:",
        "-c",
        args.batch.upper(),
        "-c",
        "exit",
    ]
    stdout_path = work_dir / "DOSBOX.OUT.TXT"
    stderr_path = work_dir / "DOSBOX.ERR.TXT"
    registry = ProcessRegistry(bridge_home)
    try:
        registry.require_capacity(1)
        with stdout_path.open("wb") as stdout, stderr_path.open("wb") as stderr:
            process = registry.launch(
                command,
                task_id=args.task_id,
                classification="temporary_dosbox_validation",
                temporary=True,
                cwd=work_dir,
                stdout=stdout,
                stderr=stderr,
            )
            try:
                return_code = process.wait(timeout=args.timeout)
            except subprocess.TimeoutExpired:
                if not registry.stop(process, grace_seconds=10, force_after_timeout=False):
                    print(
                        f"ERROR: DOSBox-X PID {process.pid} did not stop gracefully; "
                        "it remains tracked and capacity-blocking.",
                        file=sys.stderr,
                    )
                    return 124
                print(f"ERROR: DOSBox-X exceeded {args.timeout}s and was stopped gracefully.", file=sys.stderr)
                return 124
            finally:
                if process.poll() is not None:
                    registry.mark_exited(process, process.returncode)

        print(f"DOSBox-X PID {process.pid} exited with code {return_code}.")
        return int(return_code)
    except Exception as exc:
        print(f"ERROR: managed DOSBox-X launch failed: {exc}", file=sys.stderr)
        return 3


if __name__ == "__main__":
    raise SystemExit(main())
