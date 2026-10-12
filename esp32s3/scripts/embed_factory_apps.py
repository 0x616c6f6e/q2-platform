"""Embed trusted factory Lua packages and resolution-selected PNGs in read-only firmware.

Inputs are explicit Q2-App and product JSON paths; no implicit sibling repository access.
Uses Q2-App's validator/bundler so desktop and embedded execute the same Lua sources.
"""
from pathlib import Path
import argparse
import importlib.util
import json
import re


def write_changed(path, text):
    """Avoid recompiling binary assets when the generated contents are unchanged."""
    path.parent.mkdir(parents=True, exist_ok=True)
    if not path.exists() or path.read_text(encoding="utf8") != text:
        path.write_text(text, encoding="utf8")


def generate(app_dir, config_path, output):
    """Validate all packages before emitting a deterministic source/header pair."""
    app_dir, output = Path(app_dir).resolve(), Path(output)
    spec = importlib.util.spec_from_file_location("q2app", app_dir / "scripts/q2app.py")
    packer = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(packer)
    cfg = json.loads(Path(config_path).read_text(encoding="utf8"))
    factory = json.loads((app_dir / "factory.json").read_text(encoding="utf8"))
    selected = [item["directory"] for item in factory["apps"] if item["factory"]]
    if not 1 <= len(selected) <= 8 or len(set(selected)) != len(selected):
        raise ValueError("factory app count/duplicates")
    startup = cfg["app"]["startup"]
    if startup != factory["default_app"] or startup not in selected:
        raise ValueError("startup must be the factory default lifecycle app")
    width, height = cfg["display"]["width"], cfg["display"]["height"]
    resolution = f"{width}x{height}"
    grants = cfg["app"]["grants"]
    if len(grants) > 32 or any(not packer.CAP.fullmatch(g) for g in grants):
        raise ValueError("invalid product grants")
    code = ['/* Generated factory packages. Edit APP sources/product JSON, not this file. */',
            '#include "q2_factory_bundle.h"']
    records, ids = [], set()

    def blob(name, data):
        # Terminate text as well as binary: reported size excludes the extra zero.
        code.append(f"static const unsigned char {name}[] = {{")
        payload = data + b"\0"
        for offset in range(0, len(payload), 24):
            code.append("    " + ",".join(str(n) for n in payload[offset:offset+24]) + ",")
        code.append("};")

    for i, name in enumerate(selected):
        if not re.fullmatch(r"[a-zA-Z0-9_-]+", name):
            raise ValueError("invalid factory directory")
        folder = app_dir / "apps" / name
        if folder.is_symlink() or any(p.is_symlink() for p in folder.rglob("*")):
            raise ValueError("factory packages must not contain symlinks")
        meta, manifest = packer.manifest(folder / "manifest.json")
        if meta["id"] in ids:
            raise ValueError("duplicate factory UUID")
        ids.add(meta["id"])
        if not set(meta["capabilities"]["required"]) <= set(grants):
            raise ValueError(f"{name}: required capabilities missing from app.grants")
        source = packer.bundle(folder)
        blob(f"manifest_{i}", manifest)
        blob(f"source_{i}", source)
        assets = {f.name: f for f in sorted((folder / "assets").glob("*.png"))}
        assets.update({f.name: f for f in sorted((folder / "assets" / resolution).glob("*.png"))})
        if len(assets) > 64:
            raise ValueError("too many factory images")
        rows = []
        for j, (asset, path) in enumerate(sorted(assets.items())):
            if not re.fullmatch(r"[A-Za-z0-9_.-]+\.png", asset) or len("assets/" + asset) >= 96:
                raise ValueError("invalid asset alias")
            data = path.read_bytes()
            if not data.startswith(b"\x89PNG\r\n\x1a\n") or len(data) > 256 * 1024:
                raise ValueError("invalid or oversized PNG")
            blob(f"image_{i}_{j}", data)
            rows.append(f'    {{"assets/{asset}", image_{i}_{j}, {len(data)}}},')
        if not rows or (meta.get("icon") and meta["icon"][7:] not in assets):
            raise ValueError("missing factory assets/icon")
        code += [f"static const q2_embedded_image_t images_{i}[] = {{", *rows, "};"]
        allowed = [g for g in grants if g in meta["capabilities"]["required"] + meta["capabilities"]["optional"]]
        code.append(f"static const char *const grants_{i}[] = {{" + ",".join(json.dumps(g) for g in allowed) + "};")
        home = name == startup
        if home:
            code.append(f'const char q2_factory_home_id[] = "{meta["id"]}";')
        records.append('    {"%s", "%s", {(const char *)manifest_%d, (const char *)source_%d, %d, %d, grants_%d, %d, true, %s}, images_%d, %d},'
                       % (meta["id"], meta.get("icon", ""), i, i, len(manifest), len(source), i,
                          len(allowed), "true" if home else "false", i, len(rows)))
    code += ["const q2_embedded_package_t q2_factory_packages[] = {", *records, "};",
             f"const size_t q2_factory_package_count = {len(records)};"]
    header = '''/* Generated read-only product catalog. */
#ifndef Q2_FACTORY_BUNDLE_H
#define Q2_FACTORY_BUNDLE_H
#include "embedded_apps.h"
extern const q2_embedded_package_t q2_factory_packages[];
extern const size_t q2_factory_package_count;
extern const char q2_factory_home_id[];
'''
    header += f"#define Q2_FACTORY_WIDTH {width}u\n#define Q2_FACTORY_HEIGHT {height}u\n#endif\n"
    write_changed(output / "q2_factory_bundle.h", header)
    write_changed(output / "q2_factory_bundle.c", "\n".join(code) + "\n")


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--app-dir", type=Path, required=True)
    parser.add_argument("--config", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    args = parser.parse_args()
    try:
        generate(args.app_dir, args.config, args.output)
    except (ValueError, OSError, KeyError) as error:
        parser.exit(2, f"Factory bundle: {error}\n")
