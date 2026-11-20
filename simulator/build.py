"""Build the unchanged ESP32 sketch for a static browser simulator with Emscripten."""
import argparse
import hashlib
from pathlib import Path
import re
import shutil
import subprocess
import tempfile
import zipfile

ROOT = Path(__file__).resolve().parent.parent
HEADERS = ("WiFi", "WebServer", "ElegantOTA", "Preferences", "OneWire", "DallasTemperature",
           "esp_task_wdt", "Wire", "Adafruit_GFX", "Adafruit_SSD1306", "BluetoothSerial")

def build(output: Path, emxx: str):
    output.mkdir(parents=True, exist_ok=True)
    sketch = ROOT / "ukrkraft_kolona_v1_1/ukrkraft_kolona_v1_1.ino"
    sources = [sketch, *sorted(sketch.parent.glob("*.h")), Path(__file__), ROOT / "simulator/core.cpp", ROOT / "simulator/arduino/stubs.h",
               ROOT / "simulator/bridge.js", ROOT / "simulator/panel.html"]
    version = hashlib.sha256(b"\0".join(path.read_bytes() for path in sources)).hexdigest()[:16]
    module_name = f"column-{version}.mjs"
    bridge_name = f"bridge-{version}.js"
    firmware_name = f"distauto-firmware-{version}.zip"
    diagnostic_name = f"distauto-diagnostic-no-ota-{version}.zip"
    html = re.search(r'R"HTML\((.*?)\)HTML"', (sketch.parent / "WebPage.h").read_text(), re.S).group(1)
    panel = (ROOT / "simulator/panel.html").read_text().replace("distauto-firmware.zip",firmware_name)
    css = "\n.sim-panel{border:1px solid var(--accent)}.sim-panel select{width:100%;padding:10px;font-size:18px;border-radius:8px;background:var(--page);color:var(--text);border:1px solid var(--border);box-sizing:border-box}.sim-download{flex:1;display:flex;align-items:center;justify-content:center;color:var(--detail);margin-top:14px;padding:10px;text-align:center}button:disabled{opacity:.5}\n"
    html = html.replace("</style>", css + "</style>")
    html = html.replace("<body>", "<body>\n" + panel, 1)
    html = html.replace("<script>", f'<script src="{bridge_name}"></script>\n<script>', 1)
    html = html.replace("<title>", "<title>Симулятор | ", 1)
    (output / "index.html").write_text(html)
    (output / ".nojekyll").touch()
    bridge = (ROOT / "simulator/bridge.js").read_text().replace("'./column.mjs'", f"'./{module_name}'")
    (output / bridge_name).write_text(bridge)
    # Stable import for the Node.js smoke test; the browser uses only versioned assets.
    (output / "column.mjs").write_text(f"export {{default}} from './{module_name}';\n")
    shutil.copy2(sketch, output / sketch.name)
    with zipfile.ZipFile(output / firmware_name, "w", zipfile.ZIP_DEFLATED) as archive:
        for source in [sketch, *sorted(sketch.parent.glob("*.h")), sketch.parent / "README.md"]:
            archive.write(source, f"{sketch.parent.name}/{source.name}")
    shutil.copy2(output / firmware_name, output / "distauto-firmware.zip")
    # Same controller, settings and UI, with only the OTA compile-time switch changed.
    with zipfile.ZipFile(output / diagnostic_name, "w", zipfile.ZIP_DEFLATED) as archive:
        for source in [sketch, *sorted(sketch.parent.glob("*.h")), sketch.parent / "README.md"]:
            content = source.read_text()
            if source == sketch:
                assert content.count("#define DISTAUTO_ENABLE_OTA 1") == 1
                content = content.replace("#define DISTAUTO_ENABLE_OTA 1", "#define DISTAUTO_ENABLE_OTA 0")
            archive.writestr(f"{sketch.parent.name}/{source.name}", content)
    with tempfile.TemporaryDirectory(prefix="distauto-headers-") as directory:
        headers = Path(directory)
        for name in HEADERS:
            (headers / f"{name}.h").write_text('#include "stubs.h"\n')
        subprocess.run([emxx, "-std=c++17", "-O2", "-I", str(headers), "-I", str(ROOT / "simulator/arduino"),
                        str(ROOT / "simulator/core.cpp"), "--no-entry", "-sMODULARIZE=1", "-sEXPORT_ES6=1",
                        "-sALLOW_MEMORY_GROWTH=1", "-sENVIRONMENT=web,node", '-sEXPORTED_RUNTIME_METHODS=["cwrap"]',
                        "-o", str(output / module_name)], check=True)
    print(f"Built {output / 'index.html'}")

if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--output", type=Path, default=ROOT / "dist")
    parser.add_argument("--emxx", default="em++")
    args = parser.parse_args()
    build(args.output.resolve(), args.emxx)
