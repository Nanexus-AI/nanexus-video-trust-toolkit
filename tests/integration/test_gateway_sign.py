#!/usr/bin/env python3
"""Short real-process operator/artifact checks using the sealed A2 origin."""
import json
import os
import pathlib
import signal
import subprocess
import sys
import tempfile
from test_live_signing_rtsp import run_case

binary, fixtures = map(pathlib.Path, sys.argv[1:])
env = os.environ.copy()
env.pop("NANEXUS_RTSP_USERNAME", None)
env.pop("NANEXUS_RTSP_PASSWORD", None)


def command(codec, root, endpoint):
    return [str(binary), "gateway-sign", "--codec", codec, "--duration", "2",
            "--key", str(fixtures / "pki/signer.key.pem"), "--cert",
            str(fixtures / "pki/signer-chain.pem"), "--output-root", str(root), endpoint]


def run(args, expected=0, environment=env):
    result = subprocess.run(args, capture_output=True, text=True, env=environment, timeout=6)
    assert result.returncode == expected, (args, result.returncode, result.stdout, result.stderr)
    return result


with tempfile.TemporaryDirectory(prefix="nanexus-a3-cli-") as directory:
    parent = pathlib.Path(directory)
    for codec in ("h264", "h265"):
        for mode in ("continuous", "gap", "disconnect", "cancel"):
            root = parent / f"{codec}-{mode}"

            def client(endpoint):
                args = command(codec, root, endpoint)
                if mode == "cancel":
                    process = subprocess.Popen(args, stdout=subprocess.PIPE, stderr=subprocess.PIPE,
                                               text=True, env=env)
                    try:
                        import time
                        time.sleep(1)
                        process.send_signal(signal.SIGINT)
                        stdout, stderr = process.communicate(timeout=5)
                        assert process.returncode == 3, (stdout, stderr)
                    finally:
                        if process.poll() is None:
                            process.kill()
                            process.wait(timeout=1)
                else:
                    run(args, 0 if mode == "continuous" else 3)
                epochs = list(root.glob("epoch-*"))
                assert len(epochs) == 1
                metadata = json.loads((epochs[0] / "epoch.json").read_text())
                assert metadata["state"] == ("finalized" if mode == "continuous" else "aborted")
                assert metadata["termination_reason"] == {
                    "continuous": "bounded_stop", "gap": "continuity_lost",
                    "disconnect": "failed", "cancel": "cancelled_or_aborted"}[mode]
                assert metadata["source_identity"] == "not_established"
                assert metadata["cross_epoch_continuity"] is False
                media = epochs[0] / f"signed.{codec}"
                assert metadata["bytes_acknowledged"] == media.stat().st_size > 0
                assert (epochs[0].stat().st_mode & 0o777) == 0o700
                assert (media.stat().st_mode & 0o777) == 0o600
                text = (epochs[0] / "epoch.json").read_text()
                assert endpoint not in text and str(fixtures) not in text and "PRIVATE KEY" not in text
                if mode == "continuous":
                    verification = run([str(binary), "verify", "--codec", codec, "--json",
                                        "--ca", str(fixtures / "pki/ca.pem"), str(media)])
                    assert json.loads(verification.stdout)["overall"] == "VALID"
                    assert json.loads(verification.stdout)["source_authenticity"] == "not_established"
                    inspection = run([str(binary), "inspect", "--codec", codec, "--json",
                                      "--ca", str(fixtures / "pki/ca.pem"), str(media)])
                    json.loads(inspection.stdout)
                before = media.read_bytes()
                run(args, 2)  # Collision must never rewrite artifacts or connect.
                assert media.read_bytes() == before
                print(f"PASS {codec} gateway-sign {mode}: durable metadata, collision safety" +
                      ("; existing verify VALID + inspect" if mode == "continuous" else "; aborted"))

            run_case(binary, fixtures, codec, mode, client)

    base = command("h264", parent / "invalid", "rtsp://127.0.0.1:1/synthetic")
    for option, value in (("--duration", "2junk"), ("--duration", "301"),
                          ("--duration", "0"), ("--key", "/missing-key"),
                          ("--codec", "jpeg"), ("--output-root", str(parent / ".." / "escape")),
                          ("--output-root", "relative")):
        changed = base.copy()
        changed[changed.index(option) + 1] = value
        run(changed, 2)
        assert not (parent / "invalid").exists()
    for endpoint in ("rtsp://user:TOP_SECRET@127.0.0.1:1/stream", "rtsp:///empty"):
        result = run(base[:-1] + [endpoint], 2)
        assert "TOP_SECRET" not in result.stderr + result.stdout
    secret_env = env | {"NANEXUS_RTSP_USERNAME": "operator", "NANEXUS_RTSP_PASSWORD": "TOP_SECRET" * 100}
    result = run(base, 2, secret_env)
    assert "TOP_SECRET" not in result.stdout + result.stderr
    assert not (parent / "invalid").exists()
    symlink = parent / "link"
    symlink.symlink_to(parent, target_is_directory=True)
    args = base.copy()
    args[args.index("--output-root") + 1] = str(symlink / "unsafe")
    run(args, 2)
    assert not (parent / "unsafe").exists()
    run(base + ["--duration", "2"], 2)
    print("PASS gateway configuration, duration, identity, credential redaction, traversal/symlink and duplicate option policy")
