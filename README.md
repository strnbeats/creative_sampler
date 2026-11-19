# Creative Sampler

Модульный VST3 sampler на C++ и JUCE. Реализованы Phase 0 (проектный каркас) и базовая Phase 1 (импорт и playback Sample).

## Сборка

Нужны CMake 3.22+, совместимый C++20 toolchain и JUCE 8, доступный как CMake package либо исходный каталог.

```sh
cmake -S . -B build -DJUCE_SOURCE_DIR=/path/to/JUCE
cmake --build build --config Release
ctest --test-dir build --output-on-failure
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

- Асинхронный импорт WAV/AIFF в mono/stereo.
- Неизменяемое `SampleStorage`, отдельные метаданные и многослойные min/max waveform peaks.
- Воспроизведение с mono/stereo маршрутизацией, Catmull-Rom sample-rate conversion, сглаживанием Play/Stop и параметром Volume.
- Фиксированный lock-free handoff на audio thread; отложенное освобождение Sample выполняется message thread.
- File Chooser, drag & drop, метаданные и понятные состояния импорта.
- `CreativeSamplerTests` проверяет импорт, отказоустойчивость, waveform, playback, sample-rate conversion и конкурентную замену Sample.

Полный декодированный Sample хранится в RAM; импорт ограничивает один Sample размером 2 GiB и сообщает об этом до выделения буфера. Форматы, отличные от WAV и AIFF, не заявлены. Анализ, slicing, polyphony, pitch/time-stretch и FX ещё не реализованы.

## Передача Sample и время жизни

`AudioImportService` выполняет декодирование и построение waveform в единственном worker-потоке. Отмена проверяется между чанками чтения и при построении waveform. `AsyncUpdater` передаёт результат на message thread; ошибки не меняют уже установленный Sample.

После успешного импорта message thread передаёт `unique_ptr<SampleStorage>` в атомарный pending-slot. Audio thread забирает сырой указатель через acquire/release обмен и становится единственным владельцем активного указателя. UI не использует этот указатель и получает только копируемые metadata и отдельный неизменяемый `WaveformData`.

При замене audio thread делает 5 ms fade-out и помещает предыдущий Sample в SPSC retirement queue. Если очередь заполнена, замена ждёт свободного места. Таймер Processor на message thread извлекает старые указатели и освобождает их. Поэтому `processBlock()` не меняет reference count, не блокируется и не уничтожает большие буферы. При уничтожении Processor сначала останавливается импорт и audio callback (со стороны host), затем оставшиеся объекты освобождаются вне realtime-потока.
