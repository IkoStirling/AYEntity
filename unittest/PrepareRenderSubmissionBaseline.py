"""Export a local Git rendering baseline and isolate its class namespace for A/B probes."""
import argparse
import hashlib
import json
from pathlib import Path
import re
import subprocess


def git(repo, *args):
    return subprocess.check_output(["git", "-C", str(repo), *args])


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("repo", type=Path)
    parser.add_argument("revision")
    parser.add_argument("output", type=Path)
    args = parser.parse_args()
    revision = git(args.repo, "rev-parse", "--verify", "--end-of-options",
                   args.revision + "^{commit}").decode().strip()
    sources = ["src/AYRenderSystem.cpp", "src/AYSkinnedMeshRenderSystem.cpp",
               "include/AYEntity/RenderSystem.h", "include/AYEntity/SkinnedMeshRenderSystem.h"]
    # Read and validate every source before creating output. Never overwrite evidence.
    prepared = []
    for source in sources:
        original = git(args.repo, "show", revision + ":" + source)
        text = original.decode("utf-8-sig")
        pattern = r"(?m)^namespace ayt::entity(?=\s*\{)"
        if len(re.findall(pattern, text)) != 1:
            raise ValueError("Expected one entity namespace in " + source)
        isolated = re.sub(pattern, "namespace ayt::entity::baseline", text)
        if source.endswith(".cpp"):
            header = "SkinnedMeshRenderSystem.h" if "Skinned" in source else "RenderSystem.h"
            original_include = '"AYEntity/' + header + '"'
            if isolated.count(original_include) != 1:
                raise ValueError("Expected one own-header include in " + source)
            isolated = isolated.replace(original_include, '"AYEntityBaseline/' + header + '"')
        destination = ("include/AYEntityBaseline/" + Path(source).name
                       if source.endswith(".h") else Path(source).name)
        prepared.append((source, original, destination, isolated.encode("utf-8")))
    args.output.mkdir(exist_ok=False)
    records = []
    for source, original, destination, isolated in prepared:
        saved = args.output / "originals" / source
        generated = args.output / "isolated" / destination
        saved.parent.mkdir(parents=True, exist_ok=True)
        generated.parent.mkdir(parents=True, exist_ok=True)
        saved.write_bytes(original)
        generated.write_bytes(isolated)
        records.append({"source": source, "gitBlob": git(args.repo, "rev-parse", revision + ":" + source).decode().strip(),
                        "originalSha256": hashlib.sha256(original).hexdigest(),
                        "isolatedSha256": hashlib.sha256(isolated).hexdigest()})
    (args.output / "manifest.json").write_text(json.dumps({
        "revision": revision,
        "edits": ["entity namespace isolated as ayt::entity::baseline",
                  "own-header includes changed to AYEntityBaseline; algorithm bytes otherwise retained"],
        "files": records}, indent=2), encoding="utf-8")
    print(args.output / "isolated")


if __name__ == "__main__":
    main()
