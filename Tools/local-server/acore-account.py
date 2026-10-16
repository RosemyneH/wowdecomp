#!/usr/bin/env python3
# ʕ •ᴥ•ʔ✿ SRP6 registration data for an AzerothCore 3.3.5 account, plus a logon check ✿ ʕ •ᴥ•ʔ
import argparse
import hashlib
import os
import socket
import struct
import sys

N = int("894B645E89E1535BBDAD5B8B290650530801B18EBFBF5E8FAB3C82872A3E9BB7", 16)
G = 7


def sha1(*parts: bytes) -> bytes:
    digest = hashlib.sha1()
    for part in parts:
        digest.update(part)
    return digest.digest()


def le(value: int, size: int) -> bytes:
    return value.to_bytes(size, "little")


def registration(username: str, password: str) -> tuple[str, str]:
    username = username.upper()
    password = password.upper()
    salt = os.urandom(32)
    credentials = sha1(f"{username}:{password}".encode())
    x = int.from_bytes(sha1(salt, credentials), "little")
    verifier = pow(G, x, N).to_bytes(32, "little")
    return salt.hex(), verifier.hex()


def interleave(secret: bytes) -> bytes:
    even = secret[0::2]
    odd = secret[1::2]
    start = 0
    while start < len(secret) and secret[start] == 0:
        start += 1
    if start & 1:
        start += 1
    half = start // 2
    h0 = sha1(even[half:])
    h1 = sha1(odd[half:])
    out = bytearray(40)
    for i in range(20):
        out[2 * i] = h0[i]
        out[2 * i + 1] = h1[i]
    return bytes(out)


def logon(host: str, port: int, username: str, password: str) -> None:
    username = username.upper()
    password = password.upper()
    body = b"".join([
        b"WoW\x00",
        bytes((3, 3, 5)),
        struct.pack("<H", 12340),
        b"68x\x00",
        b"niW\x00",
        b"SUne",
        struct.pack("<II", 0, 0),
        bytes((len(username),)),
        username.encode(),
    ])
    packet = bytes((0, 8)) + struct.pack("<H", len(body)) + body
    with socket.create_connection((host, port), 10) as sock:
        sock.sendall(packet)
        header = sock.recv(3)
        if len(header) < 3 or header[0] != 0 or header[1] != 0:
            raise SystemExit(f"auth challenge rejected: {header.hex() if header else 'eof'}")
        server_b = sock.recv(32)
        g_len = sock.recv(1)[0]
        generator = sock.recv(g_len)
        n_len = sock.recv(1)[0]
        prime = sock.recv(n_len)
        salt = sock.recv(32)
        sock.recv(16)
        flags = sock.recv(1)
        if flags and flags[0] != 0:
            raise SystemExit(f"auth security flags {flags[0]} are not handled")

        credentials = sha1(f"{username}:{password}".encode())
        x = int.from_bytes(sha1(salt, credentials), "little")
        n_int = int.from_bytes(prime, "little")
        g_int = int.from_bytes(generator, "little")
        b_int = int.from_bytes(server_b, "little")
        verifier = pow(g_int, x, n_int)
        private = int.from_bytes(os.urandom(32), "little") + n_int.bit_length()
        client_a = pow(g_int, private, n_int)
        a_bytes = le(client_a, 32)
        u = int.from_bytes(sha1(a_bytes, server_b), "little")
        base = (b_int + 3 * (n_int - verifier)) % n_int
        secret = le(pow(base, private + u * x, n_int), 32)
        session = interleave(secret)
        n_hash = bytearray(sha1(prime))
        g_hash = sha1(generator)
        for i, value in enumerate(g_hash):
            n_hash[i] ^= value
        proof = sha1(bytes(n_hash), sha1(username.encode()), salt, a_bytes, server_b, session)
        sock.sendall(bytes((1,)) + a_bytes + proof + bytes(20) + bytes((0, 0)))
        result = sock.recv(2)
        if len(result) < 2 or result[0] != 1 or result[1] != 0:
            raise SystemExit(f"logon failed: {result.hex() if result else 'eof'}")


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("username")
    parser.add_argument("password")
    parser.add_argument("--check", metavar="HOST:PORT")
    args = parser.parse_args()
    if args.check:
        host, _, port = args.check.partition(":")
        logon(host, int(port or "3724"), args.username, args.password)
        print("logon ok")
        return
    salt, verifier = registration(args.username, args.password)
    print(salt)
    print(verifier)


if __name__ == "__main__":
    main()
