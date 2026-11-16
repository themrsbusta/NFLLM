"""Smoke-test DOS/32A-bound NFLLM in the NF-DOS 6 V23 guest on a 486 CPU."""

from __future__ import annotations

import argparse
import hashlib
import json
import os
from pathlib import Path
import re
import select
import socket
import subprocess
import sys
import time


V23_SHA256 = "53C7345445F70B71D93F2094CAE406BF5E79E4B1AE042C94551B6DE6C9B18C62"


def sha256(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as source:
        for chunk in iter(lambda: source.read(1024 * 1024), b""):
            digest.update(chunk)
    return digest.hexdigest().upper()


def safe_dos_path(path: Path) -> bool:
    return re.fullmatch(r"[A-Za-z]:\\[A-Za-z0-9_ .\\-]+", str(path)) is not None


class Monitor:
    def __init__(self, stream: socket.socket):
        self.stream = stream

    def command(self, text: str, timeout: float = 1.0) -> str:
        self.stream.sendall((text + "\n").encode("ascii"))
        deadline = time.monotonic() + timeout
        quiet_at: float | None = None
        chunks: list[bytes] = []
        while time.monotonic() < deadline:
            readable, _, _ = select.select([self.stream], [], [], 0.03)
            if readable:
                try:
                    chunk = self.stream.recv(65536)
                except OSError:
                    break
                if not chunk:
                    break
                chunks.append(chunk)
                quiet_at = time.monotonic() + 0.12
            elif quiet_at is not None and time.monotonic() >= quiet_at:
                break
        return b"".join(chunks).decode("ascii", errors="replace")

    def screen(self) -> str:
        dump = self.command("xp/2000hx 0xb8000", timeout=7.0)
        words = re.findall(r"(?i)(?<![0-9a-f])0x([0-9a-f]{4})(?![0-9a-f])", dump)
        if len(words) < 2000:
            raise RuntimeError(f"QEMU VGA text read returned only {len(words)} words")
        chars = []
        for item in words[:2000]:
            value = int(item, 16) & 0xFF
            chars.append(chr(value) if 32 <= value <= 126 else " ")
        text = "".join(chars)
        return "\n".join(text[i:i + 80].rstrip() for i in range(0, 2000, 80))

    @staticmethod
    def key(char: str) -> str:
        if char.isascii() and char.isalnum():
            return char.lower()
        mapping = {" ": "spc", ".": "dot", ":": "shift-semicolon", "-": "minus"}
        if char not in mapping:
            raise ValueError(f"Unsupported guest keyboard character: {char!r}")
        return mapping[char]

    def type_line(self, line: str) -> None:
        for char in line:
            self.command(f"sendkey {self.key(char)}", timeout=0.3)
            time.sleep(0.05)
        self.command("sendkey ret", timeout=0.3)
        time.sleep(0.15)

    def run_guest(self, line: str, expect: str, timeout: int) -> str:
        self.type_line("cls")
        time.sleep(0.2)
        self.type_line(line)
        deadline = time.monotonic() + timeout
        screen = ""
        while time.monotonic() < deadline:
            screen = self.screen()
            failure = re.search(
                r"ERROR:|SELFTEST:\s*FAIL|Bad command or file name",
                screen,
                re.IGNORECASE,
            )
            if failure:
                raise RuntimeError(f"Guest reported an error while running {line}:\n{screen}")
            if re.search(expect, screen, re.IGNORECASE | re.DOTALL) and re.search(r"[A-Z]:\\>", screen):
                return screen
            time.sleep(0.2)
        raise TimeoutError(f"Guest command timed out: {line}\n{screen}")


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--model", required=True, type=Path)
    parser.add_argument("--share", required=True, type=Path)
    parser.add_argument(
        "--image",
        type=Path,
        default=Path(__file__).resolve().parents[2] / "NF-DOS-6.0" / "NF-DOS6.0-v23.img",
    )
    parser.add_argument("--qemu", type=Path, default=Path(r"C:\Program Files\qemu\qemu-system-i386.exe"))
    parser.add_argument("--timeout", type=int, default=180)
    args = parser.parse_args()

    project_root = Path(__file__).resolve().parents[2]
    build_dir = (project_root / "NFLLM-1.1" / "build").resolve()
    share = args.share.resolve()
    image = args.image.resolve()
    model = args.model.resolve()
    qemu = args.qemu.resolve()
    if not share.is_relative_to(build_dir):
        print("ERROR: the QEMU FAT share must be inside NFLLM-1.1/build/.", file=sys.stderr)
        return 2
    if share.exists() and any(share.iterdir()):
        print(f"ERROR: refusing to reuse non-empty test share: {share}", file=sys.stderr)
        return 2
    if not image.is_file() or not model.is_file() or not qemu.is_file():
        print("ERROR: QEMU, V23 image, or GGUF model is missing.", file=sys.stderr)
        return 2
    if not 1 <= args.timeout <= 600:
        print("ERROR: timeout must be between 1 and 600 seconds.", file=sys.stderr)
        return 2
    if sha256(image) != V23_SHA256:
        print("ERROR: NF-DOS 6 V23 image hash mismatch; refusing to boot it.", file=sys.stderr)
        return 3

    build_files = [
        (build_dir / "NFLLM.EXE", "NFLLM.EXE"),
        (build_dir / "NFTEST.EXE", "NFTEST.EXE"),
        (build_dir / "compat_probe.exe", "PROBE.EXE"),
    ]
    if any(not source.is_file() for source, _ in build_files):
        print("ERROR: expected 1.1 build outputs are missing.", file=sys.stderr)
        return 3
    share.mkdir(parents=True, exist_ok=True)
    for source, target_name in build_files:
        (share / target_name).write_bytes(source.read_bytes())
    (share / "STORY.GGU").write_bytes(model.read_bytes())

    forbidden = {"DOS4GW.EXE", "DOS32A.EXE", "SB.EXE", "SVER.EXE"}
    present_forbidden = sorted(file.name.upper() for file in share.iterdir() if file.name.upper() in forbidden)
    if present_forbidden:
        print(f"ERROR: external extender tools are in the guest share: {present_forbidden}", file=sys.stderr)
        return 3
    if not safe_dos_path(share):
        print("ERROR: QEMU FAT share path contains unsupported characters.", file=sys.stderr)
        return 3

    supervisor_src = project_root / "HAME-Codex-Bridge" / "src"
    bridge_home = project_root / "tools" / "mica-codex-bridge" / "data"
    if not (supervisor_src / "process_supervisor.py").is_file() or not (bridge_home / "process-registry.json").is_file():
        print("ERROR: shared process supervisor or registry is unavailable.", file=sys.stderr)
        return 3
    sys.path.insert(0, str(supervisor_src))
    try:
        from process_supervisor import ProcessRegistry, process_snapshot
    except Exception as exc:
        print(f"ERROR: cannot load shared process supervisor: {exc}", file=sys.stderr)
        return 3

    for process in process_snapshot():
        executable = os.path.normcase(str(process.get("executable", "")))
        if executable.endswith("qemu-system-i386.exe"):
            print(f"ERROR: another emulator is active (PID {process.get('pid')}); refusing a parallel run.", file=sys.stderr)
            return 4

    port_probe = socket.socket()
    try:
        port_probe.bind(("127.0.0.1", 0))
        port = int(port_probe.getsockname()[1])
    finally:
        port_probe.close()

    log_dir = build_dir / "logs" / "nfdos-v23-dos32a" / share.name
    log_dir.mkdir(parents=True, exist_ok=True)
    stdout_path = log_dir / "qemu.stdout.txt"
    stderr_path = log_dir / "qemu.stderr.txt"
    transcript_path = log_dir / "guest-transcript.txt"
    before_model_hash = sha256(share / "STORY.GGU")
    command = [
        str(qemu), "-machine", "pc", "-accel", "tcg,thread=single", "-cpu", "486",
        "-m", "96M", "-nic", "none",
        "-drive", f"file={image},format=raw,if=floppy,index=0,media=disk,readonly=on",
        "-drive", f"file=fat:rw:{share},format=raw,if=ide,index=0,media=disk",
        "-boot", "order=a", "-monitor", f"tcp:127.0.0.1:{port},server=on,wait=off",
        "-display", "none", "-no-reboot", "-name", "NFLLM-1.1 DOS32A V23 smoke",
    ]

    registry = ProcessRegistry(bridge_home)
    process = None
    monitor: Monitor | None = None
    client: socket.socket | None = None
    transcript: list[str] = []
    checks: list[dict[str, str]] = []
    error_message = ""
    status = "failed"
    try:
        registry.require_capacity(1)
        with stdout_path.open("wb") as stdout, stderr_path.open("wb") as stderr:
            process = registry.launch(
                command,
                task_id=f"nfllm11-v23-smoke-{int(time.time())}",
                classification="temporary_qemu_guest_validation",
                temporary=True,
                cwd=qemu.parent,
                stdout=stdout,
                stderr=stderr,
            )
            client = socket.socket()
            client.settimeout(0.2)
            deadline = time.monotonic() + 60
            while time.monotonic() < deadline:
                if process.poll() is not None:
                    raise RuntimeError(f"QEMU exited during startup with code {process.returncode}")
                try:
                    client.connect(("127.0.0.1", port))
                    break
                except OSError:
                    time.sleep(0.25)
            else:
                raise TimeoutError("QEMU monitor did not become ready within 60 seconds")
            monitor = Monitor(client)
            time.sleep(8)
            boot = ""
            boot_deadline = time.monotonic() + 30
            while time.monotonic() < boot_deadline:
                boot = monitor.screen()
                if re.search(r"A:\\>", boot, re.IGNORECASE):
                    break
                time.sleep(0.5)
            if not re.search(r"A:\\>", boot, re.IGNORECASE):
                raise RuntimeError(f"NF-DOS V23 boot prompt missing:\n{boot}")
            transcript.extend(["BOOT:", boot])

            commands = [
                ("C:", r"C:\\>", 30),
                ("NFTEST.EXE", r"SELFTEST:\s*PASS", 120),
                ("NFLLM.EXE STORY.GGU -p Once -n 1", r"Status:\s*PASS", args.timeout),
            ]
            for guest_command, expected, guest_timeout in commands:
                screen = monitor.run_guest(guest_command, expected, guest_timeout)
                transcript.extend([f"COMMAND: {guest_command}", screen, ""])
                if guest_command == "NFTEST.EXE":
                    checks.append({"name": guest_command, "status": "PASS"})
                elif guest_command.startswith("NFLLM.EXE"):
                    token_match = re.search(r'TOKEN\[(\d+)\]\s+id=(\d+)\s+text="([^"]*)"', screen)
                    check = {"name": "one-token inference", "status": "PASS"}
                    if token_match:
                        check["token_index"] = token_match.group(1)
                        check["token_id"] = token_match.group(2)
                        check["token_text"] = token_match.group(3)
                    checks.append(check)
            status = "pass"
    except Exception as exc:
        transcript.extend(["ERROR:", str(exc)])
        print(f"ERROR: NF-DOS V23 smoke failed: {exc}", file=sys.stderr)
        error_message = str(exc)
        status = "failed"
    finally:
        if monitor is not None:
            try:
                monitor.command("quit", timeout=0.5)
            except OSError:
                pass
        if client is not None:
            try:
                client.close()
            except OSError:
                pass
        if process is not None and process.poll() is None:
            try:
                process.wait(timeout=15)
            except subprocess.TimeoutExpired:
                if not registry.stop(process, grace_seconds=10, force_after_timeout=False):
                    print("ERROR: QEMU is still active and remains tracked.", file=sys.stderr)
        if process is not None and process.poll() is not None:
            registry.mark_exited(process, process.returncode)
        transcript_path.write_text("\n".join(transcript) + "\n", encoding="utf-8")

    after_image_hash = sha256(image)
    report = {
        "schema_version": 1,
        "status": status,
        "guest": "NF-DOS 6 V23",
        "cpu": "QEMU generic 486, TCG single-thread",
        "memory_mib": 96,
        "boot_image": str(image),
        "boot_image_sha256_before": V23_SHA256,
        "boot_image_sha256_after": after_image_hash,
        "boot_image_unchanged": after_image_hash == V23_SHA256,
        "model": str(model),
        "model_sha256": before_model_hash,
        "external_extender_tools_in_guest_share": [],
        "checks": checks,
        "error": error_message,
        "transcript": str(transcript_path),
    }
    report_path = log_dir / "report.json"
    report_path.write_text(json.dumps(report, indent=2) + "\n", encoding="utf-8")
    print(json.dumps(report, indent=2))
    if after_image_hash != V23_SHA256:
        return 5
    return 0 if status == "pass" else 1


if __name__ == "__main__":
    raise SystemExit(main())
