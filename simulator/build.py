"""Build the unchanged ESP32 sketch for a static browser simulator with Emscripten."""
import argparse
from pathlib import Path
import re
import shutil
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parent.parent
HEADERS = ("WiFi", "WebServer", "ElegantOTA", "Preferences", "OneWire", "DallasTemperature",
           "esp_task_wdt", "Wire", "Adafruit_GFX", "Adafruit_SSD1306", "BluetoothSerial")

def build(output: Path, emxx: str):
    output.mkdir(parents=True, exist_ok=True)
    sketch = ROOT / "ukrkraft_kolona_v1_1/ukrkraft_kolona_v1_1.ino"
    html = re.search(r'R"HTML\((.*?)\)HTML"', sketch.read_text(), re.S).group(1)
    panel = (ROOT / "simulator/panel.html").read_text()
    css = "\n.sim-panel{border:1px solid #396fa6}.sim-panel select{width:100%;padding:10px;font-size:18px;border-radius:8px;background:#111;color:#eee;border:1px solid #444;box-sizing:border-box}.sim-download{flex:1;display:flex;align-items:center;justify-content:center;color:#9ac6ff;margin-top:14px;padding:10px;text-align:center}button:disabled{opacity:.5}\n"
    html = html.replace("</style>", css + "</style>")
    html = html.replace("<body>", "<body>\n" + panel, 1)
    html = html.replace("<script>", '<script src="bridge.js"></script>\n<script>', 1)
    html = html.replace("<title>", "<title>Симулятор | ", 1)
    (output / "index.html").write_text(html)
    (output / ".nojekyll").touch()
    shutil.copy2(ROOT / "simulator/bridge.js", output / "bridge.js")
    shutil.copy2(sketch, output / sketch.name)
    with tempfile.TemporaryDirectory(prefix="distauto-headers-") as directory:
        headers = Path(directory)
        for name in HEADERS:
            (headers / f"{name}.h").write_text('#include "stubs.h"\n')
        subprocess.run([emxx, "-std=c++17", "-O2", "-I", str(headers), "-I", str(ROOT / "simulator/arduino"),
                        str(ROOT / "simulator/core.cpp"), "--no-entry", "-sMODULARIZE=1", "-sEXPORT_ES6=1",
                        "-sALLOW_MEMORY_GROWTH=1", "-sENVIRONMENT=web,node", '-sEXPORTED_RUNTIME_METHODS=["cwrap"]',
                        "-o", str(output / "column.mjs")], check=True)
    print(f"Built {output / 'index.html'}")

if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--output", type=Path, default=ROOT / "dist")
    parser.add_argument("--emxx", default="em++")
    args = parser.parse_args()
    build(args.output.resolve(), args.emxx)
