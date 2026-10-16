#!/usr/bin/env python3
"""stdio MCP server for the in-game wowdecomp profiler (/dev/shm/wowdecomp-profile)."""

import json
import mmap
import os
import struct
import sys

SHM = "/dev/shm/wowdecomp-profile"
ZONE_COUNT = 7
FRAME_CAP = 120
NAME_BYTES = 32
HEADER = 40
NAMES = HEADER
FRAMES = 264
ZONE = struct.Struct("<QQII")
FRAME = struct.Struct("<QII")


def read_message():
    headers = {}
    while True:
        line = sys.stdin.buffer.readline()
        if not line or line in (b"\n", b"\r\n"):
            break
        key, value = line.decode("utf-8").split(":", 1)
        headers[key.strip().lower()] = value.strip()
    length = int(headers.get("content-length", "0"))
    if length <= 0:
        return None
    return json.loads(sys.stdin.buffer.read(length))


def write_message(payload):
    body = json.dumps(payload).encode("utf-8")
    sys.stdout.buffer.write(f"Content-Length: {len(body)}\r\n\r\n".encode("utf-8"))
    sys.stdout.buffer.write(body)
    sys.stdout.buffer.flush()


def result(req_id, text):
    write_message({
        "jsonrpc": "2.0",
        "id": req_id,
        "result": {"content": [{"type": "text", "text": text}], "isError": False},
    })


def open_shm():
    if not os.path.exists(SHM):
        return None
    fd = os.open(SHM, os.O_RDWR)
    data = mmap.mmap(fd, 0)
    os.close(fd)
    return data


def load_snapshot(data):
    for _ in range(8):
        serial = struct.unpack_from("<I", data, 32)[0]
        if serial & 1:
            continue
        magic, version, zones, frames, name_bytes, enabled = struct.unpack_from("<IIIIII", data, 0)
        clocks = struct.unpack_from("<Q", data, 24)[0]
        names = []
        for i in range(ZONE_COUNT):
            raw = data[NAMES + i * NAME_BYTES:NAMES + (i + 1) * NAME_BYTES]
            names.append(raw.split(b"\0", 1)[0].decode("utf-8", "replace"))
        copied = []
        base = FRAMES
        size = FRAME.size + ZONE.size * ZONE_COUNT
        for i in range(FRAME_CAP):
            off = base + i * size
            copied.append(bytes(data[off:off + size]))
        serial2 = struct.unpack_from("<I", data, 32)[0]
        if serial == serial2 and not serial2 & 1:
            return {
                "magic": magic,
                "version": version,
                "zones": zones,
                "frames": frames,
                "enabled": enabled,
                "clocks": clocks,
                "names": names,
                "records": copied,
            }
    return None


def parse_frame(blob):
    ticks, serial, _pad = FRAME.unpack_from(blob, 0)
    zones = []
    off = FRAME.size
    for _ in range(ZONE_COUNT):
        self_t, total, count, _zpad = ZONE.unpack_from(blob, off)
        zones.append((self_t, total, count))
        off += ZONE.size
    return ticks, serial, zones


def recent(snap, count):
    records = []
    for blob in snap["records"]:
        ticks, serial, zones = parse_frame(blob)
        if ticks:
            records.append((serial, ticks, zones))
    records.sort(key=lambda item: item[0])
    if count > 0:
        records = records[-count:]
    return records


def tool_status():
    data = open_shm()
    if not data:
        return "No profile shared memory. Start WowClient64 so it can create /dev/shm/wowdecomp-profile."
    snap = load_snapshot(data)
    data.close()
    if not snap or snap["magic"] != 0x46525057:
        return "Profile memory is present but not initialized."
    filled = len(recent(snap, FRAME_CAP))
    clocks = snap["clocks"] or 1
    return (
        f"enabled={snap['enabled']} frames={filled} zones={snap['zones']} "
        f"clocks_per_second={clocks} version={snap['version']}\n"
        + "\n".join(f"{i} {name}" for i, name in enumerate(snap["names"]))
    )


def tool_top(frames):
    data = open_shm()
    if not data:
        return "No profile shared memory. Start the client first."
    snap = load_snapshot(data)
    data.close()
    if not snap:
        return "Profile memory is busy or uninitialized."
    rows = recent(snap, frames)
    if not rows:
        return "No completed World.Render frames yet."
    clocks = snap["clocks"] or 1
    n = len(rows)
    mean = sum(item[1] for item in rows) / n / clocks * 1000.0
    lines = [f"frames={n} mean_frame={mean:.2f} ms", f"{'zone':<16} {'self%':>7} {'total%':>7} {'calls':>8} {'self ms':>8}"]
    totals = [(0, 0, 0) for _ in range(ZONE_COUNT)]
    frame_sum = sum(item[1] for item in rows)
    for _serial, _ticks, zones in rows:
        for i, (self_t, total, count) in enumerate(zones):
            acc = totals[i]
            totals[i] = (acc[0] + self_t, acc[1] + total, acc[2] + count)
    order = sorted(range(ZONE_COUNT), key=lambda i: totals[i][0], reverse=True)
    for i in order:
        self_t, total, count = totals[i]
        if count == 0:
            continue
        lines.append(
            f"{snap['names'][i]:<16} {100.0 * self_t / frame_sum:7.1f} {100.0 * total / frame_sum:7.1f} "
            f"{count / n:8.1f} {self_t / n / clocks * 1000.0:8.2f}"
        )
    return "\n".join(lines)


def tool_frames(count):
    data = open_shm()
    if not data:
        return "No profile shared memory. Start the client first."
    snap = load_snapshot(data)
    data.close()
    if not snap:
        return "Profile memory is busy or uninitialized."
    rows = recent(snap, count)
    clocks = snap["clocks"] or 1
    if not rows:
        return "No completed frames yet."
    lines = ["serial  frame_ms"]
    for serial, ticks, _zones in rows:
        lines.append(f"{serial:6}  {ticks / clocks * 1000.0:8.2f}")
    return "\n".join(lines)


def tool_set_enabled(enabled):
    data = open_shm()
    if not data:
        return "No profile shared memory. Start the client first."
    struct.pack_into("<I", data, 20, 1 if enabled else 0)
    data.close()
    return f"enabled={1 if enabled else 0}"


TOOLS = [
    {
        "name": "profile_status",
        "description": "Show whether the running WowClient profiler is enabled and how many frames it has stored.",
        "inputSchema": {"type": "object", "properties": {}},
    },
    {
        "name": "profile_top",
        "description": "Rank CPU zones over the newest World.Render frames. self% excludes nested zones.",
        "inputSchema": {
            "type": "object",
            "properties": {"frames": {"type": "integer", "description": "How many recent frames to average. Default 30."}},
        },
    },
    {
        "name": "profile_frames",
        "description": "List recent World.Render frame times in milliseconds.",
        "inputSchema": {
            "type": "object",
            "properties": {"count": {"type": "integer", "description": "How many frames to list. Default 20."}},
        },
    },
    {
        "name": "profile_set_enabled",
        "description": "Turn in-game zone timing on or off. The client reads this each zone.",
        "inputSchema": {
            "type": "object",
            "properties": {"enabled": {"type": "boolean"}},
            "required": ["enabled"],
        },
    },
]


def call_tool(name, args):
    if name == "profile_status":
        return tool_status()
    if name == "profile_top":
        return tool_top(int(args.get("frames", 30) or 30))
    if name == "profile_frames":
        return tool_frames(int(args.get("count", 20) or 20))
    if name == "profile_set_enabled":
        return tool_set_enabled(bool(args.get("enabled")))
    return f"Unknown tool {name}"


def main():
    while True:
        message = read_message()
        if message is None:
            return
        method = message.get("method")
        req_id = message.get("id")
        if method == "initialize":
            write_message({
                "jsonrpc": "2.0",
                "id": req_id,
                "result": {
                    "protocolVersion": "2024-11-05",
                    "capabilities": {"tools": {}},
                    "serverInfo": {"name": "wowdecomp-profile", "version": "1"},
                },
            })
        elif method == "tools/list":
            write_message({"jsonrpc": "2.0", "id": req_id, "result": {"tools": TOOLS}})
        elif method == "tools/call":
            params = message.get("params") or {}
            result(req_id, call_tool(params.get("name"), params.get("arguments") or {}))
        elif method and method.startswith("notifications/"):
            continue
        elif req_id is not None:
            write_message({"jsonrpc": "2.0", "id": req_id, "result": {}})


if __name__ == "__main__":
    main()
