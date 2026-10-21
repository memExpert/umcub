#!/usr/bin/env python3
"""Finish an STM32CubeIDE project made by its "STM32 CMake Project" wizard on umcub.

  umcub_cubeide.py <project folder>        (close the project in CubeIDE first)

The wizard turns every CMake preset into a build configuration, but shows the
whole source tree, all build trees included, and offers no way to build a
single target. This adds to the project:
  - resource filters: no .venv, .git, local, __pycache__ or standalone example
    builds; under the build folder only the trees of the configurations, and
    in them only the results (.elf .map .hex .bin .ld), so refreshing stays
    fast and the ELF files can be picked for the Build Analyzer;
  - one Build Targets entry per executable of every configured build tree
    ("<configuration>: <target>", e.g. "bluepill: bluepill_app").
Build each configuration once (CubeIDE configures it), then run this again to
get its targets. Filters and targets made by an earlier run are replaced;
others are kept.
"""
import argparse
import re
import sys
import xml.etree.ElementTree as ET
from pathlib import Path

EXTS = "elf|map|hex|bin|ld"


def die(msg):
    sys.exit(f"umcub_cubeide: {msg}")


def configurations(cproject):
    """[(name, source dir, build dir)] from the wizard's cmakeTarget modules."""
    out = []
    root = ET.parse(cproject).getroot()
    for cfg in root.iter("cconfiguration"):
        settings = cfg.find("storageModule[@moduleId='org.eclipse.cdt.core.settings']")
        cmake = cfg.find("storageModule[@moduleId='com.st.stm32cube.ide.cmake.cmakeTarget']")
        if settings is None or cmake is None:
            continue
        src = Path(cmake.findtext("src", ""))
        build = Path(cmake.findtext("build", ""))
        out.append((settings.get("name"), src, build if build.is_absolute() else src / build))
    return out


def executables(build_dir):
    """Executable targets of a Ninja build tree: "build <name>: phony <name>.elf"."""
    ninja = build_dir / "build.ninja"
    if not ninja.is_file():
        return None
    names = re.findall(r"^build (\S+): phony \S*?\1\.elf$", ninja.read_text(errors="replace"), re.M)
    return sorted(set(names))


def filter_args(key, regex, value):
    return f"1.0-{key}-matches-false-{'true' if regex else 'false'}-{value}"


def project_filters(prefix, build_dirs, src):
    """(resource, type, matcher arguments). Types: 1 include-only, 2 exclude, 4 files, 8 folders, 16 inheritable."""
    j = (lambda s: f"{prefix}/{s}" if prefix else s)
    f = [(prefix, 10, filter_args("projectRelativePath", False, j(d))) for d in (".venv", ".git", "local")]
    f.append((prefix, 26, filter_args("name", False, "__pycache__")))
    f.append((j("examples"), 26, filter_args("name", False, "build")))
    parents = {}
    for b in build_dirs:
        try:
            rel = b.relative_to(src)
        except ValueError:
            continue                        # build tree outside the sources: not in the project
        if len(rel.parts) >= 2:
            parents.setdefault(rel.parent.as_posix(), set()).add(rel.name)
    for parent, names in sorted(parents.items()):
        p = j(parent)
        f.append((p, 9, filter_args("name", True, "|".join(re.sub(r"([.^$*+?()\[\]{}|\\])", r"\\\1", n) for n in sorted(names)))))
        f.append((p, 21, filter_args("name", True, rf".*\.({EXTS})")))
        f.append((p, 26, filter_args("name", True, "CMakeFiles|umcub_probe")))
    return f


def update_project(path, prefix, build_dirs, src):
    tree = ET.parse(path)
    root = tree.getroot()
    fr = root.find("filteredResources")
    if fr is None:
        fr = ET.SubElement(root, "filteredResources")
    new = project_filters(prefix, build_dirs, src)
    # filters of an earlier run: the same ones, and the include-only list of build trees (it may have changed)
    ours = {(r, t, a) for r, t, a in new} | {(r, 9, None) for r, t, _ in new if t == 9}
    for flt in list(fr):
        r, t, a = flt.findtext("name", ""), int(flt.findtext("type", "0")), flt.findtext("matcher/arguments", "")
        if (r, t, a) in ours or (r, t, None) in ours:
            fr.remove(flt)
    next_id = max([int(x.findtext("id", "0")) for x in fr] + [0]) + 1
    for i, (r, t, a) in enumerate(new):
        flt = ET.SubElement(fr, "filter")
        ET.SubElement(flt, "id").text = str(next_id + i)
        ET.SubElement(flt, "name").text = r
        ET.SubElement(flt, "type").text = str(t)
        m = ET.SubElement(flt, "matcher")
        ET.SubElement(m, "id").text = "org.eclipse.ui.ide.multiFilter"
        ET.SubElement(m, "arguments").text = a
    ET.indent(tree, "\t")
    tree.write(path, encoding="UTF-8", xml_declaration=True)


def update_targets(path, targets, config_names):
    """Rewrite the buildtargets module of .cproject as text: ET would drop its <?fileVersion?> line."""
    text = path.read_text()
    mod = re.search(r'<storageModule moduleId="org.eclipse.cdt.make.core.buildtargets"\s*/>|'
                    r'<storageModule moduleId="org.eclipse.cdt.make.core.buildtargets">.*?</storageModule>',
                    text, re.S)
    keep = []
    if mod and "<buildTargets>" in mod.group(0):
        for t in re.findall(r"<target .*?</target>", mod.group(0), re.S):
            name = re.search(r'name="([^"]*)"', t).group(1)
            if not any(name.startswith(f"{c}: ") for c in config_names):
                keep.append(t)
    new = [f"""<target name="{n}" path="" targetID="org.eclipse.cdt.build.MakeTargetBuilder">
				<buildCommand>cmake</buildCommand>
				<buildArguments></buildArguments>
				<buildTarget>{a}</buildTarget>
				<stopOnError>true</stopOnError>
				<useDefaultCommand>false</useDefaultCommand>
				<runAllBuilders>false</runAllBuilders>
			</target>""" for n, a in targets]
    body = "".join(f"\t\t\t{t}\n" for t in keep + new)
    module = ('<storageModule moduleId="org.eclipse.cdt.make.core.buildtargets">\n'
              f"\t\t<buildTargets>\n{body}\t\t</buildTargets>\n\t</storageModule>")
    if mod:
        text = text[:mod.start()] + module + text[mod.end():]
    else:
        text = text.replace("</cproject>", f"\t{module}\n</cproject>")
    path.write_text(text)


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("project", type=Path, help="folder of the CubeIDE project (has .project and .cproject)")
    a = ap.parse_args()
    dot_project, cproject = a.project / ".project", a.project / ".cproject"
    if not dot_project.is_file() or not cproject.is_file():
        die(f"{a.project}: no .project / .cproject")
    cfgs = configurations(cproject)
    if not cfgs:
        die("no CMake configurations: make the project with the 'STM32 CMake Project' wizard")
    src = cfgs[0][1].resolve()
    if any(s.resolve() != src for _, s, _ in cfgs):
        die("the configurations use different source folders")
    # the sources: the project folder itself, or a linked folder
    if a.project.resolve() == src:
        prefix = ""
    else:
        links = [l.findtext("name") for l in ET.parse(dot_project).getroot().iter("link")
                 if Path(l.findtext("location", "")).resolve() == src]
        if not links:
            die(f"no linked folder for {src} in {dot_project}")
        prefix = links[0]
    build_dirs = {b.resolve() for _, _, b in cfgs}
    update_project(dot_project, prefix, build_dirs, src)
    targets = []
    for name, _, b in cfgs:
        exe = executables(b)
        if exe is None:
            print(f"{name}: not configured yet ({b}) - build it once, then run this again")
            continue
        targets += [(f"{name}: {x}", f"--build {b.resolve().as_posix()} --target {x}") for x in exe]
        print(f"{name}: {', '.join(exe) or 'no executables'}")
    update_targets(cproject, targets, [n for n, _, _ in cfgs])
    print(f"{dot_project}: filters for {prefix or 'the project folder'}; {cproject}: {len(targets)} build targets")


if __name__ == "__main__":
    main()
