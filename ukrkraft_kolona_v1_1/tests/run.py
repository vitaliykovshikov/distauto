"""Host simulation only; this does not compile against the ESP32 SDK."""
import json
from pathlib import Path
import subprocess
import tempfile

root = Path(__file__).resolve().parent
with tempfile.TemporaryDirectory(prefix="ukrkraft-tests-") as folder:
    temp = Path(folder)
    for name in ("WiFi", "WebServer", "ElegantOTA", "Preferences", "OneWire",
                 "DallasTemperature", "esp_task_wdt", "Wire", "Adafruit_GFX",
                 "Adafruit_SSD1306", "BluetoothSerial"):
        (temp / f"{name}.h").write_text('#include "stubs.h"\n')
    binary = temp / "auto-test"
    subprocess.run(["clang++", "-std=c++17", "-Wall", "-Wextra", "-Wno-unused-parameter",
                    "-I", str(temp), "-I", str(root / "host"),
                    str(root / "host/auto_test.cpp"), "-o", str(binary)], check=True)
    result = subprocess.run([str(binary)], text=True, capture_output=True, check=True)
    for line in result.stdout.splitlines():
        json.loads(line)
    print(result.stderr.strip())
    print("API responses parse as JSON: OK")
subprocess.run(["node", str(root / "web_test.cjs")], check=True)
