#!/usr/bin/env python3
"""Bounded test-only RTSP origin for tiny synthetic single-slice fixtures.

No downstream server/product surface. Standard-library-only, loopback ephemeral
port, one connection, fixed SDP, fixed RTP NAL packetization, <=5 second lifetime.
"""
import base64
import pathlib
import socket
import struct
import subprocess
import sys
import threading
import time


def nalus(data):
    # Normalize Annex-B start codes only in this fixture origin.
    return [part for part in data.replace(b"\x00\x00\x00\x01", b"\x00\x00\x01").split(b"\x00\x00\x01")[1:] if part]


def run_case(binary, root, codec, mode, client=None):
    media = nalus((root / codec / f"unsigned.{codec}").read_bytes())
    naltype = (lambda n: n[0] & 31) if codec == "h264" else (lambda n: (n[0] >> 1) & 63)
    vcl = (lambda n: naltype(n) in (1, 5)) if codec == "h264" else (lambda n: naltype(n) < 32)
    sets = {naltype(n): base64.b64encode(n).decode() for n in media}
    fmtp = (f"packetization-mode=1;sprop-parameter-sets={sets[7]},{sets[8]}"
            if codec == "h264" else
            f"sprop-vps={sets[32]};sprop-sps={sets[33]};sprop-pps={sets[34]}")
    sdp = ("v=0\r\no=- 1 1 IN IP4 127.0.0.1\r\ns=Synthetic\r\nc=IN IP4 127.0.0.1\r\n"
           f"t=0 0\r\nm=video 0 RTP/AVP 96\r\na=rtpmap:96 {codec.upper()}/90000\r\n"
           f"a=fmtp:96 {fmtp}\r\na=control:track1\r\n").encode()
    stop = threading.Event()
    errors = []
    with socket.socket() as listener:
        listener.bind(("127.0.0.1", 0))
        listener.listen(1)
        listener.settimeout(5)
        port = listener.getsockname()[1]
        endpoint = f"rtsp://127.0.0.1:{port}/synthetic"

        def serve():
            try:
                with listener.accept()[0] as connection:
                    connection.settimeout(0.05)
                    pending = b""
                    playing = False
                    index = sent = 0
                    sequence = 1000
                    timestamp = 90000
                    deadline = time.monotonic() + 5
                    next_media = time.monotonic()
                    while not stop.is_set() and time.monotonic() < deadline:
                        try:
                            received = connection.recv(4096)
                            if not received:
                                return
                            pending += received
                        except socket.timeout:
                            pass
                        # Client RTCP interleaved frames may share this connection.
                        while pending:
                            if pending[0] == 36:
                                if len(pending) < 4:
                                    break
                                size = struct.unpack("!H", pending[2:4])[0]
                                if len(pending) < size + 4:
                                    break
                                pending = pending[size + 4:]
                                continue
                            if b"\r\n\r\n" not in pending:
                                break
                            header, pending = pending.split(b"\r\n\r\n", 1)
                            lines = header.decode().split("\r\n")
                            method = lines[0].split()[0]
                            fields = dict(line.split(":", 1) for line in lines[1:] if ":" in line)
                            cseq = fields.get("CSeq", "1").strip()
                            body = sdp if method == "DESCRIBE" else b""
                            extra = ""
                            if method == "OPTIONS":
                                extra = "Public: OPTIONS, DESCRIBE, SETUP, PLAY, TEARDOWN, GET_PARAMETER\r\n"
                            if method == "DESCRIBE":
                                extra = f"Content-Type: application/sdp\r\nContent-Base: {endpoint}/\r\n"
                            if method == "SETUP":
                                extra = "Transport: RTP/AVP/TCP;unicast;interleaved=0-1\r\nSession: synthetic\r\n"
                            if method == "PLAY":
                                extra = (f"Session: synthetic\r\nRange: npt=0-\r\n"
                                         f"RTP-Info: url={endpoint}/track1;seq=1000;rtptime=90000\r\n")
                                playing = True
                            response = (f"RTSP/1.0 200 OK\r\nCSeq: {cseq}\r\n{extra}"
                                        f"Content-Length: {len(body)}\r\n\r\n").encode() + body
                            connection.sendall(response)
                            if method == "TEARDOWN":
                                return
                        if playing and time.monotonic() >= next_media:
                            nalu = media[index % len(media)]
                            if mode in ("gap", "passive-gap") and sent == 18:
                                sequence += 1  # Unseen RTP loss despite intact TCP.
                            if mode == "disconnect" and sent == 18:
                                return
                            packet = struct.pack("!BBHII", 0x80, 96 | (0x80 if vcl(nalu) else 0),
                                                 sequence & 65535, timestamp & 0xFFFFFFFF, 12345) + nalu
                            assert len(packet) < 65536
                            connection.sendall(b"$\x00" + struct.pack("!H", len(packet)) + packet)
                            sequence += 1
                            sent += 1
                            index += 1
                            if vcl(nalu):
                                timestamp += 3600
                            next_media = time.monotonic() + 0.005
            except (BrokenPipeError, ConnectionResetError):
                pass  # Expected when the guarded client stops on a gap.
            except Exception as error:
                if not stop.is_set():
                    errors.append(error)

        thread = threading.Thread(target=serve, daemon=True)
        thread.start()
        try:
            if client is not None:
                client(endpoint)
            else:
                result = subprocess.run([str(binary), str(root), codec, endpoint, mode],
                                        capture_output=True, text=True, timeout=6)
                assert result.returncode == 0, (codec, mode, result.stdout, result.stderr)
                print(result.stdout, end="")
        finally:
            stop.set()
            thread.join(timeout=1)
        assert not errors, errors
        assert not thread.is_alive(), "fixture thread did not stop"


def main():
    binary, root = pathlib.Path(sys.argv[1]), pathlib.Path(sys.argv[2])
    for codec in ("h264", "h265"):
        for mode in ("continuous", "gap", "cancel", "disconnect", "passive-continuous", "passive-gap"):
            run_case(binary, root, codec, mode)


if __name__ == "__main__":
    main()
