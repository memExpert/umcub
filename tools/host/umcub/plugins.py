"""Plugins: transports the library does not ship (another adapter, a vendor API).

A plugin is a Python module with a register() function that calls
umcub.register_transport(scheme, open, options, description); open(target,
node, **options) returns a umcub.Connection subclass (send / recv / text /
close, `stream`, `mtu`, `selects_node`). For a CAN adapter python-can does not
know, a python-can interface plugin is usually the better fit: `can:` then
works with it unchanged.

Two ways in:
- installed packages: entry point group "umcub.transports" naming the module
  or its register function; loaded on first use;
- a folder of .py files (files starting with "_" are skipped): load_plugins(folder),
  `umcub --plugins DIR` or $UMCUB_PLUGINS. Only load folders you trust: a plugin
  runs with the full rights of the program.

A plugin that fails to load is reported, not raised: the others still load.
"""
import importlib.util
import os
from dataclasses import dataclass
from importlib import metadata
from pathlib import Path

ENTRY_POINT_GROUP = "umcub.transports"


@dataclass
class Plugin:
    name: str       # entry point or file name
    source: str     # "package <distribution>" or the file path
    error: str = "" # why it did not load; empty when it did


_loaded = {}            # source -> Plugin
_installed_done = False


def _register(obj, plugin):
    try:
        reg = obj if callable(obj) else getattr(obj, "register", None)
        if not callable(reg):
            raise TypeError("no register() function")
        reg()
    except Exception as e:      # a broken plugin must not take the others down
        plugin.error = f"{type(e).__name__}: {e}"
    return plugin


def load_installed():
    """Plugins of installed packages (entry points), once."""
    global _installed_done
    if _installed_done:
        return [p for p in _loaded.values() if p.source.startswith("package ")]
    _installed_done = True
    found = []
    for ep in metadata.entry_points(group=ENTRY_POINT_GROUP):
        dist = ep.dist.name if ep.dist else "?"
        p = Plugin(ep.name, f"package {dist}")
        try:
            obj = ep.load()
        except Exception as e:
            p.error = f"{type(e).__name__}: {e}"
        else:
            _register(obj, p)
        _loaded[p.source + ":" + p.name] = p
        found.append(p)
    return found


def load_plugins(folder=None):
    """Load the installed plugins and the .py files of `folder` (default
    $UMCUB_PLUGINS, if set); a file is loaded once. Returns every Plugin
    found, with `error` set for those that failed."""
    found = load_installed()
    folder = folder or os.environ.get("UMCUB_PLUGINS")
    if not folder:
        return found
    d = Path(folder)
    if not d.is_dir():
        return found + [Plugin(str(d), str(d), "not a folder")]
    for f in sorted(d.glob("*.py")):
        if f.name.startswith("_"):
            continue
        key = str(f.resolve())
        if key in _loaded:
            found.append(_loaded[key])
            continue
        p = Plugin(f.stem, key)
        try:
            spec = importlib.util.spec_from_file_location(f"umcub_plugin_{f.stem}", f)
            mod = importlib.util.module_from_spec(spec)
            spec.loader.exec_module(mod)
        except Exception as e:
            p.error = f"{type(e).__name__}: {e}"
        else:
            _register(mod, p)
        _loaded[key] = p
        found.append(p)
    return found


def plugins():
    """Every plugin loaded so far, with its error if it failed."""
    return list(_loaded.values())
