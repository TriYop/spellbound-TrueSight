"""Export computed preset values to MixAdvice XML format."""
from __future__ import annotations

import xml.etree.ElementTree as ET
from pathlib import Path

from .analysis import BAND_NAMES
from .stats import PresetValues

_PRESET_DIR = Path.home() / ".config" / "MixAdvice" / "Presets"


def preset_xml_path(name: str) -> Path:
    safe = _safe_filename(name)
    return _PRESET_DIR / f"{safe}.xml"


def export_preset(
    name: str,
    description: str,
    values: PresetValues,
    output_path: Path | str | None = None,
) -> Path:
    """Write preset XML and return the path."""
    out = Path(output_path) if output_path else preset_xml_path(name)
    out.parent.mkdir(parents=True, exist_ok=True)

    root = ET.Element("MixAdvicePreset")
    root.set("name", name)
    root.set("description", description)

    def band_el(tag: str, values_list: list[float]) -> ET.Element:
        el = ET.SubElement(root, tag)
        for band_name, val in zip(BAND_NAMES, values_list):
            el.set(band_name, str(val))
        return el

    band_el("BandRmsDb",      values.band_rms_db)
    band_el("BandMinCorr",    values.band_min_corr)
    band_el("BandTransientDb", values.band_transient_db)

    overall = ET.SubElement(root, "Overall")
    overall.set("rmsDb",   str(values.overall_rms_db))
    overall.set("minCorr", str(values.overall_min_corr))

    tree = ET.ElementTree(root)
    ET.indent(tree, space="    ")

    # Write with XML declaration
    with open(out, "wb") as f:
        f.write(b'<?xml version="1.0" encoding="UTF-8"?>\n')
        tree.write(f, encoding="utf-8", xml_declaration=False)
        f.write(b"\n")

    return out


def _safe_filename(name: str) -> str:
    """Convert preset name to a safe kebab-case filename."""
    import re
    s = name.lower()
    s = re.sub(r"[^\w\s-]", "", s)
    s = re.sub(r"[\s_]+", "-", s).strip("-")
    return s or "preset"
