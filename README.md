# distauto — автоматика ректифікаційної колони

ESP32-скетч та браузерний стенд із тим самим інтерфейсом і тим самим C++-алгоритмом.

- **Для ESP32:** відкрити `ukrkraft_kolona_v1_1/ukrkraft_kolona_v1_1.ino` в Arduino IDE. Потрібні Arduino core для ESP32, ElegantOTA, OneWire, DallasTemperature, Adafruit GFX та Adafruit SSD1306. Використовується плата з Bluetooth Classic, як у вихідному скетчі.
- **Симулятор:** сторінка `PAGE` автоматично витягується зі скетчу. Повний `.ino` компілюється в WebAssembly через Emscripten 4.0.20. Додатковий JavaScript замінює тільки HTTP-транспорт і додає керування віртуальним датчиком та часом; формули й алгоритм не дублюються.
- **Розрахунок і перебіг процесу:** [опис прошивки](ukrkraft_kolona_v1_1/README.md).

## Перевірка на Mac

Потрібні Python 3, Node.js та Xcode Command Line Tools (clang++):

```bash
python3 ukrkraft_kolona_v1_1/tests/run.py
```

Для браузерного стенда встановіть [Emscripten SDK](https://emscripten.org/docs/getting_started/downloads.html), активуйте версію `4.0.20` та виконайте:

```bash
python3 simulator/build.py
node simulator/smoke_test.mjs
python3 -m http.server 8080 --directory dist --bind 127.0.0.1
```

Відкрити <http://localhost:8080>. `build.py` також приймає `--emxx /шлях/до/em++`.

У панелі симулятора можна змінити температуру, від’єднати датчик, прискорити час або додати 1/10 хв. Налаштування і стан існують у пам’яті поточної сторінки; оновлення сторінки починає нову симуляцію. Фізичної ESP32 чи керування обладнанням немає. Симуляція не перевіряє реальні драйвери ESP32, електроніку або фізику колони.

## GitHub Pages

Після першого завантаження вибрати **Settings → Pages → Build and deployment → Source → GitHub Actions**. GitHub Pages для приватного особистого репозиторію потребує відповідного тарифу (зокрема GitHub Pro); [умови GitHub Pages](https://docs.github.com/en/pages/getting-started-with-github-pages/what-is-github-pages).

Workflow `.github/workflows/pages.yml` при кожному push у `main`:

1. Перевіряє скетч на комп’ютері та JavaScript вебінтерфейсу.
2. Збирає повний скетч у WebAssembly та витягує HTML.
3. Перевіряє весь цикл від прогріву до тіла, декремент і зупинку вже у WebAssembly.
4. Публікує тільки `dist/` у Pages після успішних перевірок.

Pull request запускає перевірки та збірку без публікації. Для повторного запуску доступний **Actions → Test and deploy simulator → Run workflow**.

Очікувана адреса після успішного деплою: <https://vitaliykovshikov.github.io/distauto/>.

## Структура

```text
ukrkraft_kolona_v1_1/ukrkraft_kolona_v1_1.ino  — джерело прошивки й інтерфейсу
ukrkraft_kolona_v1_1/tests/                    — перевірки на комп’ютері
simulator/core.cpp                            — обгортка скетчу для WebAssembly
simulator/arduino/                             — імітація апаратних бібліотек
simulator/build.py                            — збірка сторінки й WebAssembly
simulator/bridge.js                            — браузерний транспорт
.github/workflows/pages.yml                   — перевірки та публікація
```
