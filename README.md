# Creative Sampler

Модульный VST3 sampler на C++ и JUCE. Текущий этап — Phase 0: проектный каркас и прототип интерфейса. DSP-функции пока не реализованы; процессор намеренно выдаёт тишину.

## Сборка

Нужны CMake 3.22+, совместимый C++20 toolchain и JUCE 8, доступный как CMake package либо исходный каталог.

```sh
cmake -S . -B build -DJUCE_SOURCE_DIR=/path/to/JUCE
cmake --build build --config Release
```

Если JUCE установлен с CMake package config, вместо `JUCE_SOURCE_DIR` передайте путь к его package config через `JUCE_DIR`.

Формат первой сборки — VST3. Цели AU и standalone в Phase 0 не включены.

## Границы модулей

- `plugin`: JUCE adapter, параметры плагина, сохранение состояния и UI.
- `sample`: импорт, декодирование, метаданные, waveform и управление источником.
- `analysis`: BPM, тональность, транзиенты и confidence.
- `slicing`: модели срезов, стратегии нарезки и ручное редактирование.
- `performance`: MIDI mapping, voice allocation и playback.
- `dsp`: независимые DSP-алгоритмы и FX-модули.
- `library`: форматы instrument/project packs и библиотека.
- `generation`: вариации и пакетная генерация.

Зависимости направляются внутрь от adapter/UI к доменным модулям. Доменная логика не зависит от JUCE UI; аудиопоток не выполняет файловый ввод-вывод, блокировки или тяжёлый анализ.

## Текущая функциональность

- JUCE VST3 synth skeleton с MIDI-входом и mono/stereo выходом.
- Минимальный тёмный editor как технический UI-прототип.
- Состояние и звуковой движок пока не подключены.
