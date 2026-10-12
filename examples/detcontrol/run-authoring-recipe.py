"""Run only the Stage66 fresh authoring recipe; not a simulation/test launcher."""
from __future__ import annotations

import argparse
import datetime as dt
import hashlib
import json
from pathlib import Path
import secrets
import subprocess
import sys
import xml.etree.ElementTree as ET


def check(ok: bool, message: str) -> None:
    if not ok:
        raise ValueError(message)


def fingerprint(path: Path) -> dict:
    check(path.is_file() and not path.is_symlink(), f"Not an ordinary file: {path}")
    raw = path.read_bytes()
    return {"path": str(path.resolve()), "bytes": len(raw), "sha256": hashlib.sha256(raw).hexdigest()}


def write_json(path: Path, value: dict) -> None:
    with path.open("x", encoding="utf-8", newline="\n") as stream:
        json.dump(value, stream, ensure_ascii=True, indent=2)
        stream.write("\n")


def main() -> None:
    check(__debug__, "Optimized execution is not accepted")
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--tool", required=True, type=Path)
    parser.add_argument("--output-parent", required=True, type=Path)
    parser.add_argument("--manifest", type=Path, default=Path(__file__).with_name("authoring-recipe.json"))
    args = parser.parse_args()
    tool = args.tool.resolve()
    tool_before = fingerprint(tool)
    parent = args.output_parent.resolve()
    check(parent.is_dir() and not parent.is_symlink(), "Output parent must already be an ordinary directory")
    manifest_path = args.manifest.resolve()
    recipe = json.loads(manifest_path.read_text(encoding="utf-8"))
    check(recipe["schema"] == 1 and recipe["status"] == "recipe-v1", "Wrong immutable recipe profile")
    source_root = manifest_path.parent
    template = source_root / recipe["template"]["path"]
    expected = source_root / recipe["expectedCanonical"]["path"]
    for path, binding in ((template, recipe["template"]), (expected, recipe["expectedCanonical"])):
        actual = fingerprint(path)
        check(actual["sha256"] == binding["sha256"] and actual["bytes"] == binding["bytes"], "Recipe source binding differs")
    expected_bytes = expected.read_bytes()
    check(len(expected_bytes) <= 65536, "Expected level text budget")
    stamp = dt.datetime.now(dt.timezone.utc).strftime("%Y%m%dT%H%M%S")
    run = parent / f"workshop-authoring-{stamp}-{secrets.token_hex(6)}"
    run.mkdir(exist_ok=False)
    report = {"schema": 1, "status": "running", "scope": "actual fresh authoring commands and one static preview only",
              "simulationExecuted": False, "authentication": False, "nativeAcceptance": False,
              "recipe": fingerprint(manifest_path), "template": fingerprint(template), "expectedCanonical": fingerprint(expected),
              "toolBefore": tool_before, "run": str(run), "commands": []}

    def resolve(token: str) -> str:
        if token == "@TEMPLATE":
            return str(template)
        if token.startswith("@RUN/"):
            leaf = token.removeprefix("@RUN/")
            check(leaf and "/" not in leaf and "\\" not in leaf and leaf not in (".", ".."), "Recipe output leaf")
            return str(run / leaf)
        return token

    try:
        check(len(recipe["steps"]) == 7, "Exactly six authoring commands plus one preview expected")
        for index, step in enumerate(recipe["steps"]):
            argv = [str(tool), *(resolve(token) for token in step["argv"])]
            started = dt.datetime.now(dt.timezone.utc).isoformat()
            completed = subprocess.run(argv, stdout=subprocess.PIPE, stderr=subprocess.PIPE, timeout=30, check=False)
            ended = dt.datetime.now(dt.timezone.utc).isoformat()
            check(len(completed.stdout) <= 4 * 1024 * 1024 and len(completed.stderr) <= 4 * 1024 * 1024, "Receipt log budget")
            stdout = run / f"{index:02d}-{step['id']}.stdout.bin"
            stderr = run / f"{index:02d}-{step['id']}.stderr.bin"
            with stdout.open("xb") as stream:
                stream.write(completed.stdout)
            with stderr.open("xb") as stream:
                stream.write(completed.stderr)
            entry = {"index": index, "id": step["id"], "argv": argv, "startedUtc": started, "endedUtc": ended,
                     "exitCode": completed.returncode, "stdout": fingerprint(stdout), "stderr": fingerprint(stderr)}
            report["commands"].append(entry)
            check(completed.returncode == 0, f"Actual authoring command failed: {step['id']}")
            produced = Path(resolve(step["output"]))
            entry["output"] = fingerprint(produced)
        playable = run / "05-playable.dlt"
        actual_bytes = playable.read_bytes()
        first_diff = next((n for n, (a, b) in enumerate(zip(actual_bytes, expected_bytes)) if a != b), None)
        if first_diff is None and len(actual_bytes) != len(expected_bytes):
            first_diff = min(len(actual_bytes), len(expected_bytes))
        report["canonicalByteComparison"] = {"equal": actual_bytes == expected_bytes, "firstDifference": first_diff,
                                             "actual": fingerprint(playable), "expected": fingerprint(expected)}
        check(actual_bytes == expected_bytes, "Fresh tool-generated final canonical text differs from independently authored expected fixture")
        preview = run / "workshop.svg"
        raw_svg = preview.read_bytes()
        check(0 < len(raw_svg) <= 1024 * 1024, "Actual static preview budget")
        root = ET.fromstring(raw_svg)
        ns = {"s": "http://www.w3.org/2000/svg"}
        polygons = root.findall(".//s:polygon", ns)
        paths = root.findall(".//s:polyline", ns)
        check(len(polygons) == 7 and len(paths) == 1, "Actual preview authored inventory")
        actors = {node.attrib["data-actor"]: node for node in polygons}
        check(actors["90"].attrib["data-kind"] == "triangle" and actors["90"].attrib["points"] == "1,0 3,0 3,1", "Independent real filled-wedge vertices")
        check(actors["101"].attrib["points"] == "3.5,0.75 3.75,0.75 3.75,2.25 3.5,2.25", "Independent placed/resized gate endpoints")
        check(paths[0].attrib["data-actor"] == "300" and paths[0].attrib["data-playback"] == "0"
              and paths[0].attrib["points"] == "-1,-0.125 0,-0.125", "Independent actor-pose origin plus authored relative path offsets")
        report["preview"] = {"file": fingerprint(preview), "actors": 7, "pathInstances": 1, "waypoints": 2,
                             "geometryChecks": 3, "scope": "authored initial content; no ticks or CP; browser pixels not authoritative"}
        report["toolAfter"] = fingerprint(tool)
        check(report["toolAfter"]["sha256"] == tool_before["sha256"], "Tool executable changed during recipe")
        report["status"] = "passed"
        write_json(run / "authoring-receipt.json", report)
        print(json.dumps({"status": "passed", "receipt": str(run / "authoring-receipt.json"), "playable": str(playable)}, separators=(",", ":")))
    except BaseException as error:
        report["status"] = "failed"
        report["error"] = str(error)
        write_json(run / "authoring-failure.json", report)
        raise


if __name__ == "__main__":
    try:
        main()
    except Exception as error:
        print(str(error), file=sys.stderr)
        raise SystemExit(1)
