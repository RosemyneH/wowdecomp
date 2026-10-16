#!/usr/bin/env python3
"""ʕ •ᴥ•ʔ✿ Semantic SPIR-V comparison for shader pin bumps: exit 0 same, 1 differs (invariants hold), 2 blocked ✿ ʕ •ᴥ•ʔ"""
import re
import struct
import subprocess
import sys
import tempfile
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
SHADERS = ROOT / "Engine/Source/Gx/CGxDeviceVulkan/Shaders"


def extract_words(inc_path):
    words = []
    for token in re.findall(r"0x[0-9a-fA-F]+", Path(inc_path).read_text()):
        words.append(int(token, 16))
    return words


def write_spv(words, path):
    Path(path).write_bytes(b"".join(struct.pack("<I", w) for w in words))


def run(*args):
    return subprocess.run(args, capture_output=True, text=True)


def disassemble(spv, out):
    canonical = str(out) + ".canon.spv"
    result = run("spirv-opt", "--canonicalize-ids", str(spv), "-o", canonical)
    if result.returncode != 0:
        return None
    result = run("spirv-dis", "--no-header", "--no-color", canonical)
    return result.stdout if result.returncode == 0 else None


def expected_layout():
    """std140 offsets for the FfpUniforms block (mat4/vec4/ivec4 members only)."""
    text = (SHADERS / "FfpUniforms.glsl").read_text()
    body = text[text.index("{") + 1:text.index("} u;")]
    offsets = []
    offset = 0
    for decl in body.split(";"):
        decl = decl.strip()
        if not decl:
            continue
        m = re.match(r"(mat4|vec4|ivec4)\s+\w+(?:\[(\d+)\])?", decl)
        if not m:
            raise SystemExit(f"unsupported uniform member: {decl!r}")
        count = int(m.group(2) or 1)
        stride = 64 if m.group(1) == "mat4" else 16
        offsets.append((offset, stride, count, m.group(1) == "mat4"))
        offset += stride * count
    return offsets, offset


def uniform_block_offsets(text, member_count):
    """Member offsets of the std140 block. Other structs in the module are ignored."""
    groups = {}
    for struct, index, offset in re.findall(r"OpMemberDecorate (%\w+) (\d+) Offset (\d+)", text):
        groups.setdefault(struct, []).append((int(index), int(offset)))
    for members in groups.values():
        if len(members) != member_count:
            continue
        members.sort()
        return [offset for _, offset in members]
    return None


def check_invariants(spv, label):
    problems = []
    text = run("spirv-dis", "--no-color", str(spv)).stdout
    offsets, total = expected_layout()
    if total != 768:
        problems.append(f"computed block size {total} != 768")
    expected_offsets = [o for o, _, _, _ in offsets]
    offsets_found = uniform_block_offsets(text, len(expected_offsets))
    if offsets_found != expected_offsets:
        problems.append(f"{label}: member offsets {offsets_found} != expected {expected_offsets}")
    if len(re.findall(r"RowMajor", text)) < 4:
        problems.append(f"{label}: fewer than 4 RowMajor matrix members")
    if "DescriptorSet 0" not in text or "Binding 0" not in text:
        problems.append(f"{label}: uniform block is not at set 0 binding 0")
    capabilities = sorted(re.findall(r"OpCapability (\w+)", text))
    if capabilities != ["Shader"]:
        problems.append(f"{label}: capabilities {capabilities} != [Shader]")
    return problems


def main():
    rev = sys.argv[1] if len(sys.argv) > 1 else "HEAD"
    with tempfile.TemporaryDirectory() as tmp:
        tmp = Path(tmp)
        blocked = False
        differed = False
        for shader in ["Ffp.vert", "Ffp.frag"]:
            baseline_inc = tmp / f"{shader}.baseline.inc"
            baseline_inc.write_text(run("git", "-C", str(ROOT), "show", f"{rev}:Engine/Source/Gx/CGxDeviceVulkan/Shaders/Generated/{shader}.inc").stdout)
            candidate_inc = SHADERS / "Generated" / f"{shader}.inc"
            baseline, candidate = tmp / "base.spv", tmp / "cand.spv"
            write_spv(extract_words(baseline_inc), baseline)
            write_spv(extract_words(candidate_inc), candidate)
            for label, spv in [("baseline", baseline), ("candidate", candidate)]:
                result = run("spirv-val", "--target-env", "vulkan1.1", str(spv))
                if result.returncode != 0:
                    print(f"BLOCKED: {shader} {label} fails spirv-val:\n{result.stdout}{result.stderr}")
                    blocked = True
                problems = check_invariants(spv, f"{shader} {label}")
                for problem in problems:
                    print(f"BLOCKED: {problem}")
                    blocked = True
            base_dis = disassemble(baseline, tmp / "b")
            cand_dis = disassemble(candidate, tmp / "c")
            if base_dis is None or cand_dis is None:
                print(f"BLOCKED: {shader} could not be canonicalized/disassembled")
                blocked = True
            elif base_dis != cand_dis:
                differed = True
                print(f"DIFFERS: {shader} semantic diff (review with spirv-diff):")
                import difflib
                for line in list(difflib.unified_diff(base_dis.splitlines(), cand_dis.splitlines(), lineterm=""))[:40]:
                    print(" ", line)
        if blocked:
            return 2
        print("DIFFERS: review required" if differed else "SAME: no semantic difference")
        return 1 if differed else 0


if __name__ == "__main__":
    sys.exit(main())
