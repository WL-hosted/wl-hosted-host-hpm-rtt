#!/usr/bin/env python3
"""Program an HPM image with process-level OpenOCD retries."""

from __future__ import annotations

import argparse
import shlex
import subprocess
import sys
import time
from pathlib import Path


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(
        description="Program and verify an image, restarting OpenOCD on failure."
    )
    parser.add_argument("--openocd", required=True, type=Path)
    parser.add_argument("--config", required=True, type=Path)
    parser.add_argument("--image", required=True, type=Path)
    parser.add_argument("--attempts", type=int, default=3)
    parser.add_argument("--retry-delay", type=float, default=0.2)
    return parser.parse_args()


def validate_args(args: argparse.Namespace) -> None:
    for name in ("openocd", "config", "image"):
        path = getattr(args, name)
        if not path.is_file():
            raise ValueError(f"{name} file does not exist: {path}")

    if args.attempts < 1:
        raise ValueError("attempts must be at least 1")
    if args.retry_delay < 0:
        raise ValueError("retry-delay must not be negative")


def tcl_brace(value: Path) -> str:
    text = str(value)
    if "{" in text or "}" in text:
        raise ValueError(f"path containing Tcl braces is unsupported: {text}")
    return "{" + text + "}"


def main() -> int:
    args = parse_args()
    try:
        validate_args(args)
        openocd_command = [
            str(args.openocd),
            "-f",
            str(args.config),
            "-c",
            f"init; program_image {tcl_brace(args.image)}; shutdown",
        ]
    except ValueError as error:
        print(f"error: {error}", file=sys.stderr)
        return 2

    for attempt in range(1, args.attempts + 1):
        print(
            f"OpenOCD programming attempt {attempt}/{args.attempts}",
            flush=True,
        )
        print(shlex.join(openocd_command), flush=True)

        try:
            result = subprocess.run(openocd_command, check=False)
        except OSError as error:
            print(f"error: failed to start OpenOCD: {error}", file=sys.stderr)
            return 2
        except KeyboardInterrupt:
            return 130

        if result.returncode == 0:
            print("OpenOCD programming completed")
            return 0

        if attempt < args.attempts:
            print(
                f"OpenOCD attempt {attempt} failed with exit code "
                f"{result.returncode}; restarting OpenOCD",
                file=sys.stderr,
                flush=True,
            )
            time.sleep(args.retry_delay)

    print(
        f"error: OpenOCD failed to program and verify {args.image} "
        f"after {args.attempts} attempts",
        file=sys.stderr,
    )
    return 1


if __name__ == "__main__":
    raise SystemExit(main())
