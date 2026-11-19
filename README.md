# Creative Sampler

Модульный VST3 sampler на C++ и JUCE. Реализованы Phase 0 (проектный каркас), Phase 1 (импорт и playback Sample) и Phase 1.1 (band-limited resampling и validation).

## Сборка

Нужны CMake 3.22+, совместимый C++20 toolchain и JUCE 8 или новее, доступный как CMake package либо исходный каталог.

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
- Воспроизведение с mono/stereo маршрутизацией и polyphase windowed-sinc sample-rate conversion, сглаживанием Play/Stop и параметром Volume.
- Общая таблица FIR коэффициентов: 63 taps, 256 fractional phases, 64 selectable cutoff steps; 4,193,280 bytes, строится один раз до audio processing.
- Фиксированный lock-free handoff на audio thread; отложенное освобождение Sample выполняется message thread.
- File Chooser, drag & drop, метаданные и понятные состояния импорта.
- `CreativeSamplerTests` проверяет импорт, отказоустойчивость, waveform, playback, frequency response, alias suppression, DC, границы, block-size consistency, замену Sample и retirement-queue backpressure.
- Для базовых CPU/memory измерений: `./build/CreativeSamplerTests_artefacts/Release/CreativeSamplerTests --benchmark`.

Полный декодированный Sample хранится в RAM; импорт ограничивает один Sample размером 2 GiB и сообщает об этом до выделения буфера. Форматы, отличные от WAV и AIFF, не заявлены. Анализ, slicing, polyphony, pitch/time-stretch и FX ещё не реализованы.

## Передача Sample и время жизни

`AudioImportService` выполняет декодирование и построение waveform в единственном worker-потоке. Отмена проверяется между чанками чтения и при построении waveform. `AsyncUpdater` передаёт результат на message thread; ошибки не меняют уже установленный Sample.

После успешного импорта message thread передаёт `unique_ptr<SampleStorage>` в атомарный pending-slot. Audio thread забирает сырой указатель через acquire/release обмен и становится единственным владельцем активного указателя. UI не использует этот указатель и получает только копируемые metadata и отдельный неизменяемый `WaveformData`.

При замене audio thread делает 5 ms fade-out и помещает предыдущий Sample в SPSC retirement queue. Если очередь заполнена, замена ждёт свободного места. Таймер Processor на message thread извлекает старые указатели и освобождает их. Поэтому `processBlock()` не меняет reference count, не блокируется и не уничтожает большие буферы. При уничтожении Processor сначала останавливается импорт и audio callback (со стороны host), затем оставшиеся объекты освобождаются вне realtime-потока.

## Phase 1.1 — audio quality validation

Playback использует stateless polyphase windowed-sinc resampler: 63 taps, 256 фаз и 64 шага cutoff. Коэффициенты общей таблицы (~4.19 MB) строятся при создании движка; в `process()` выполняется фиксированное число умножений и сложений без аллокаций и состояния между блоками.

Release measurements на локальном стенде для 96→44.1 kHz: passband 10 kHz +0.001 dB, alias от 30 kHz −85.73 dB, swept energy выше Nyquist −85.09 dB; ошибка DC для 0.37 — 1e−8. Проверена матрица source/host 44.1, 48, 88.2 и 96 kHz, включая одинаковые rates.

Базовый Release benchmark: 5 s stereo/96 kHz source занимает 3,840,000 bytes, waveform peaks — 240,064 bytes; построение peaks занимает около 0.55 ms. Peak-data traversal 900 px stereo — около 1.3 ms на 1000 проходов. Playback занимает 0.24–0.27% realtime при host 44.1/48 kHz и 0.50–0.59% при 96 kHz для block sizes 64–1024. Это локальное микротестирование движка, не DAW CPU meter.

Тесты и сборка не заменяют DAW smoke test: доступен только GarageBand, который не принимает VST3; REAPER и `pluginval` отсутствуют.
