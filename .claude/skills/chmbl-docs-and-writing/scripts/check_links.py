#!/usr/bin/env python3
"""Find broken relative markdown links (paths AND #anchors) in a repo.

Usage:  python3 .claude/skills/chmbl-docs-and-writing/scripts/check_links.py [repo_root]

Checks every *.md outside .claude/ and build dirs. For each inline link
[text](target) with a relative target it verifies (a) the file/dir exists and
(b) if there is a #fragment pointing into a markdown file, that a heading in
the target file produces that GitHub anchor slug. Exits 1 if anything broke.
"""
import re
import sys
import pathlib

ROOT = pathlib.Path(sys.argv[1] if len(sys.argv) > 1 else ".").resolve()
SKIP_DIRS = {".git", ".claude", "build", "managed_components", "node_modules"}

LINK_RE = re.compile(r"(?<!\!)\[[^\]]*\]\(([^)\s]+)\)")


def github_slug(heading: str) -> str:
    """GitHub's anchor algorithm: strip md formatting, lowercase, drop
    everything but word chars/spaces/hyphens, spaces -> hyphens."""
    h = re.sub(r"[`*]", "", heading).strip()  # keep _ — GitHub keeps it in slugs
    h = re.sub(r"\[([^\]]*)\]\([^)]*\)", r"\1", h)  # unwrap links in headings
    h = h.lower()
    h = re.sub(r"[^\w\- ]", "", h)  # note: emoji/punct drop out, _ survives
    return h.replace(" ", "-")


def anchors_of(md: pathlib.Path) -> set:
    slugs, seen = set(), {}
    in_fence = False
    for line in md.read_text(encoding="utf-8", errors="replace").splitlines():
        if line.lstrip().startswith("```"):
            in_fence = not in_fence
            continue
        if in_fence:
            continue
        m = re.match(r"^(#{1,6})\s+(.*)", line)
        if m:
            s = github_slug(m.group(2))
            n = seen.get(s, 0)
            seen[s] = n + 1
            slugs.add(s if n == 0 else f"{s}-{n}")
    return slugs


broken = []
for md in sorted(ROOT.rglob("*.md")):
    if any(p in SKIP_DIRS for p in md.relative_to(ROOT).parts):
        continue
    in_fence = False
    for lineno, line in enumerate(
        md.read_text(encoding="utf-8", errors="replace").splitlines(), 1
    ):
        if line.lstrip().startswith("```"):
            in_fence = not in_fence
            continue
        if in_fence:
            continue
        for target in LINK_RE.findall(line):
            if target.startswith(("http://", "https://", "mailto:")):
                continue
            path_part, _, frag = target.partition("#")
            if path_part:
                dest = (md.parent / path_part).resolve()
                if not dest.exists():
                    broken.append(f"{md.relative_to(ROOT)}:{lineno}: missing file  -> {target}")
                    continue
            else:
                dest = md  # pure #fragment link, same file
            if frag and dest.suffix == ".md" and dest.is_file():
                if frag not in anchors_of(dest):
                    broken.append(f"{md.relative_to(ROOT)}:{lineno}: bad anchor    -> {target}")

for b in broken:
    print(b)
print(f"\n{len(broken)} broken link(s)" if broken else "all relative links OK")
sys.exit(1 if broken else 0)
