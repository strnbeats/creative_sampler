# Creative Sampler

Модульный VST3 sampler на C++ и JUCE. Реализованы Phase 0 (проектный каркас), Phase 1 (импорт и playback Sample), Phase 1.1 (band-limited resampling и validation), Phase 2A (инфраструктура анализа) и Phase 2B (BPM detection).

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
- `AnalysisCoordinator` управляет одним background worker и одной ожидающей задачей; новые запросы отменяют устаревшие, а результат публикуется только для актуальных Sample ID и generation.
- `AnalysisResult` хранит статусы и типизированные поля BPM, key и transients с проверкой инвариантов. Реализован `BPMAnalyzer`; Key и Transient Detection пока не реализованы.
- BPM определяется по логарифмической RMS energy envelope, onset novelty и fractional-lag tempo autocorrelation. Результат содержит detection status, score, confidence, альтернативы и half/double-time ambiguity.
- Для базовых CPU/memory измерений: `./build/CreativeSamplerTests_artefacts/Release/CreativeSamplerTests --benchmark`.
- Для BPM analysis measurements: `./build/CreativeSamplerTests_artefacts/Release/CreativeSamplerTests --bpm-benchmark`.

Полный декодированный Sample хранится в RAM; импорт ограничивает один Sample размером 2 GiB и сообщает об этом до выделения буфера. Форматы, отличные от WAV и AIFF, не заявлены. Key/Transient алгоритмы, slicing, polyphony, pitch/time-stretch и FX ещё не реализованы.

## Передача Sample и время жизни

`AudioImportService` выполняет декодирование и построение waveform в единственном worker-потоке. Отмена проверяется между чанками чтения и при построении waveform. `AsyncUpdater` передаёт результат на message thread; ошибки не меняют уже установленный Sample.

После успешного импорта `unique_ptr<SampleStorage>` один раз переводится в `shared_ptr<const SampleStorage>`. Processor передаёт тот же handle анализатору и Playback Engine. Playback Engine создаёт owner-envelope на message thread и передаёт только его сырой указатель через acquire/release pending-slot. Audio thread читает Sample через envelope, не копирует Sample handle и не меняет reference count.

При замене audio thread делает 5 ms fade-out и помещает предыдущий owner-envelope в SPSC retirement queue. Если очередь заполнена, замена ждёт свободного места. Таймер Processor на message thread извлекает envelope и освобождает playback ownership вне audio thread. Если BPM analysis ещё держит Sample, его память остаётся доступна через отдельный shared handle до завершения или отмены анализа. `processBlock()` не блокируется и не освобождает Sample.

## Phase 1.1 — audio quality validation

Playback использует stateless polyphase windowed-sinc resampler: 63 taps, 256 фаз и 64 шага cutoff. Коэффициенты общей таблицы (~4.19 MB) строятся при создании движка; в `process()` выполняется фиксированное число умножений и сложений без аллокаций и состояния между блоками.

Release measurements на локальном стенде для 96→44.1 kHz: passband 10 kHz +0.001 dB, alias от 30 kHz −85.73 dB, swept energy выше Nyquist −85.09 dB; ошибка DC для 0.37 — 1e−8. Проверена матрица source/host 44.1, 48, 88.2 и 96 kHz, включая одинаковые rates.

Базовый Release benchmark: 5 s stereo/96 kHz source занимает 3,840,000 bytes, waveform peaks — 240,064 bytes; построение peaks занимает около 0.55 ms. Peak-data traversal 900 px stereo — около 1.3 ms на 1000 проходов. Playback занимает 0.24–0.27% realtime при host 44.1/48 kHz и 0.50–0.59% при 96 kHz для block sizes 64–1024. Это локальное микротестирование движка, не DAW CPU meter.

## Phase 2A — analysis infrastructure

`AnalysisCoordinator::setCurrentSample()` отмечает выбранный Sample. При каждой загрузке, включая повторную загрузку того же пути, вызывающий код должен выдавать новый ID через `createSampleId()`. `requestAnalysis()` принимает `shared_ptr<const SampleStorage>` и один или несколько независимых анализаторов; запросу назначается возрастающий generation.

Выполнение сериализовано одним worker-потоком. В очереди находится максимум одна ожидающая задача: новый запрос отменяет активную работу и заменяет ожидающую. Анализаторы должны регулярно проверять переданный cancellation token. `latestResult()` возвращает защищённый снимок для чтения UI/message thread; realtime audio path этот API не вызывает.

`AnalysisResult` различает NotAnalyzed, Running, Completed, Failed и Cancelled. Терминальные результаты привязаны к Sample ID и generation; устаревшие завершения игнорируются. Координатор ожидает окончания активного analyzer при shutdown, поэтому реализации analyzer обязаны поддерживать кооперативную отмену.

Phase 2A оставил UI вне анализа и не менял playback ownership. В Phase 2B введён общий immutable Sample handle; processor автоматически запускает BPM analysis после успешной загрузки. UI показывает только фактически полученное состояние и результат.

## Phase 2B — BPM detection

`BPMAnalyzer` работает только в `AnalysisCoordinator` worker. Он downmix-ит каналы через усреднение энергии (без фазового взаимоподавления), строит 100 Hz log-RMS envelope, извлекает положительную novelty и оценивает периодичность нормализованной autocorrelation. Для tempo lag используется половинный шаг и parabolic interpolation. Диапазон по умолчанию — 50–220 BPM и задаётся через Options.

BPM `score` — нормализованная поддержка autocorrelation. `confidence` — эвристическая оценка надёжности, не калиброванная вероятность: учитывает силу периодичности, число onset-событий, отличие от конкурирующих неоктавных кандидатов и штраф за half/double-time ambiguity. При недостаточной длительности возвращается `insufficientData` без BPM; тишина/непериодичный материал получает `noRhythmicContent`. `manualOverride` поддерживается типом результата и проверяется на диапазон, но отдельный UI-контрол коррекции не добавлен.

На детерминированной матрице kick fixtures 60–200 BPM (source 44.1/48/96 kHz, 7–10 секунд) средняя ошибка primary candidate составила около 0.066%; средняя ошибка лучшего кандидата также измеряется отдельно, чтобы не скрывать half/double-time интерпретацию. Это synthetic validation, а не оценка точности на реальной музыкальной коллекции.

Локальный Release benchmark на 48 kHz: 2 s — 0.11 ms, 10 s — 0.73 ms, 60 s — 4.62 ms, 180 s — 14.09 ms. Основной дополнительный feature buffer занимает около 400 bytes на секунду аудио; Sample buffer не копируется. Время — один локальный прогон на синтетическом kick fixture, не realtime audio CPU load.

Тесты и сборка не заменяют DAW smoke test: доступен только GarageBand, который не принимает VST3; REAPER и `pluginval` отсутствуют.
