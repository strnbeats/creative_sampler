# Creative Sampler

Модульный Standalone/VST3 sampler на C++ и JUCE. Реализованы импорт, воспроизведение с band-limited resampling и фоновый анализ BPM, тональности и транзиентов.

## Сборка

Нужны CMake 3.22+, совместимый C++20 toolchain и JUCE 8 или новее, доступный как CMake package либо исходный каталог.

```sh
cmake -S . -B build -DJUCE_SOURCE_DIR=/path/to/JUCE
cmake --build build --config Release
ctest --test-dir build --output-on-failure
```

Если JUCE установлен с CMake package config, вместо `JUCE_SOURCE_DIR` передайте путь к его package config через `JUCE_DIR`.

Поддерживаются macOS Standalone и VST3 targets. Цель AU не включена.

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
- `AnalysisResult` хранит статусы и типизированные результаты BPM, Key и Transient Detection с проверкой инвариантов. Все три анализатора используют один worker `AnalysisCoordinator` с поддержкой отмены и общий неизменяемый `SampleStorage`.
- Key analysis выполняет chroma-based оценку по JUCE FFT и Krumhansl–Schmuckler major/minor profiles; возвращает альтернативы, confidence и ambiguity.
- BPM определяется по логарифмической RMS energy envelope, onset novelty и fractional-lag tempo autocorrelation. Результат содержит detection status, score, confidence, альтернативы и half/double-time ambiguity.
- Для базовых CPU/memory измерений: `./build/CreativeSamplerTests_artefacts/Release/CreativeSamplerTests --benchmark`.
- Для BPM analysis measurements: `./build/CreativeSamplerTests_artefacts/Release/CreativeSamplerTests --bpm-benchmark`.
- Для Key analysis measurements: `./build/CreativeSamplerTests_artefacts/Release/CreativeSamplerTests --key-benchmark`.

Полный декодированный Sample хранится в RAM; импорт ограничивает один Sample размером 2 GiB и сообщает об этом до выделения буфера. Форматы, отличные от WAV и AIFF, не заявлены. Slicing, polyphony, pitch/time-stretch и FX ещё не реализованы.

## Передача Sample и время жизни

`AudioImportService` выполняет декодирование и построение waveform в единственном worker-потоке. Отмена проверяется между чанками чтения и при построении waveform. `AsyncUpdater` передаёт результат на message thread; ошибки не меняют уже установленный Sample.

После успешного импорта `unique_ptr<SampleStorage>` один раз переводится в `shared_ptr<const SampleStorage>`. Processor передаёт тот же handle анализатору и Playback Engine. Playback Engine создаёт owner-envelope на message thread и передаёт только его сырой указатель через acquire/release pending-slot. Audio thread читает Sample через envelope, не копирует Sample handle и не меняет reference count.

При замене audio thread делает 5 ms fade-out и помещает предыдущий owner-envelope в SPSC retirement queue. Если очередь заполнена, замена ждёт свободного места. Таймер Processor на message thread извлекает envelope и освобождает playback ownership вне audio thread. Если BPM или Key analysis ещё держит Sample, его память остаётся доступна через отдельный shared handle до завершения или отмены анализа. `processBlock()` не блокируется и не освобождает Sample.

## Phase 1.1 — audio quality validation

Playback использует stateless polyphase windowed-sinc resampler: 63 taps, 256 фаз и 64 шага cutoff. Коэффициенты общей таблицы (~4.19 MB) строятся при создании движка; в `process()` выполняется фиксированное число умножений и сложений без аллокаций и состояния между блоками.

Release measurements на локальном стенде для 96→44.1 kHz: passband 10 kHz +0.001 dB, alias от 30 kHz −85.73 dB, swept energy выше Nyquist −85.09 dB; ошибка DC для 0.37 — 1e−8. Проверена матрица source/host 44.1, 48, 88.2 и 96 kHz, включая одинаковые rates.

Базовый Release benchmark: 5 s stereo/96 kHz source занимает 3,840,000 bytes, waveform peaks — 240,064 bytes; построение peaks занимает около 0.55 ms. Peak-data traversal 900 px stereo — около 1.3 ms на 1000 проходов. Playback занимает 0.24–0.27% realtime при host 44.1/48 kHz и 0.50–0.59% при 96 kHz для block sizes 64–1024. Это локальное микротестирование движка, не DAW CPU meter.

## Phase 2A — analysis infrastructure

`AnalysisCoordinator::setCurrentSample()` отмечает выбранный Sample. При каждой загрузке, включая повторную загрузку того же пути, вызывающий код должен выдавать новый ID через `createSampleId()`. `requestAnalysis()` принимает `shared_ptr<const SampleStorage>` и один или несколько независимых анализаторов; запросу назначается возрастающий generation.

Выполнение сериализовано одним worker-потоком. В очереди находится максимум одна ожидающая задача: новый запрос отменяет активную работу и заменяет ожидающую. Анализаторы должны регулярно проверять переданный cancellation token. `latestResult()` возвращает защищённый снимок для чтения UI/message thread; realtime audio path этот API не вызывает.

`AnalysisResult` различает NotAnalyzed, Running, Completed, Failed и Cancelled. Терминальные результаты привязаны к Sample ID и generation; устаревшие завершения игнорируются. Координатор ожидает окончания активного analyzer при shutdown, поэтому реализации analyzer обязаны поддерживать кооперативную отмену.

Phase 2A оставил UI вне анализа и не менял playback ownership. В Phase 2B введён общий immutable Sample handle и автоматический BPM analysis после успешной загрузки; Phase 2C добавила автоматический Key analysis. Phase 2D добавила Transient analysis. UI показывает только фактически полученные состояния и результаты.

## Phase 2B — BPM detection

`BPMAnalyzer` работает только в `AnalysisCoordinator` worker. Он downmix-ит каналы через усреднение энергии (без фазового взаимоподавления), строит 100 Hz log-RMS envelope, извлекает положительную novelty и оценивает периодичность нормализованной autocorrelation. Для tempo lag используется половинный шаг и parabolic interpolation. Диапазон по умолчанию — 50–220 BPM и задаётся через Options.

BPM `score` — нормализованная поддержка autocorrelation. `confidence` — эвристическая оценка надёжности, не калиброванная вероятность: учитывает силу периодичности, число onset-событий, отличие от конкурирующих неоктавных кандидатов и штраф за half/double-time ambiguity. При недостаточной длительности возвращается `insufficientData` без BPM; тишина/непериодичный материал получает `noRhythmicContent`. `manualOverride` поддерживается типом результата и проверяется на диапазон, но отдельный UI-контрол коррекции не добавлен.

На детерминированной матрице kick fixtures 60–200 BPM (source 44.1/48/96 kHz, 7–10 секунд) средняя ошибка primary candidate составила около 0.066%; средняя ошибка лучшего кандидата также измеряется отдельно, чтобы не скрывать half/double-time интерпретацию. Это synthetic validation, а не оценка точности на реальной музыкальной коллекции.

Локальный Release benchmark на 48 kHz: 2 s — 0.11 ms, 10 s — 0.73 ms, 60 s — 4.62 ms, 180 s — 14.09 ms. Основной дополнительный feature buffer занимает около 400 bytes на секунду аудио; Sample buffer не копируется. Время — один локальный прогон на синтетическом kick fixture, не realtime audio CPU load.

## Phase 2C — key detection

`KeyAnalyzer` обрабатывает перекрывающиеся Hann frames длительностью около 0.34 s через `juce::dsp::FFT`. Локальные спектральные пики отображаются в 12 pitch classes с распределением между соседними полутонами и сниженным весом верхних гармоник. Chroma агрегируется по четырём участкам Sample и сравнивается с вращёнными Krumhansl–Schmuckler major/minor profiles по Pearson correlation.

Confidence — эвристика: учитывает профильный score, разницу с runner-up, стабильность tonal profile между участками, число кадров, выразительность chroma и неоднозначность. Результат не является статистической вероятностью. Если chroma близка к равномерной, кадров мало, либо присутствуют лишь короткие перкуссионные события, возвращается `insufficientData` без tonic/mode. Короткий аккорд может вернуть низкую уверенность и alternative relative key.

Синтетическая матрица восьми chord progressions: exact key 8/8, tonic 8/8, mode 8/8, правильная тональность в Top-3 8/8. На десяти отрицательных фикстурах (silence, white/pink noise, transient, kick, snare, noise sweep, atonal sequence, short FX, single note) ложных определений 0/10. Это проверка детерминированных fixtures, а не измерение точности на реальной музыкальной коллекции. На одном локальном Release-прогоне для 180 s: 44.1 kHz — 222 ms, 48 kHz — 210 ms, 96 kHz — 403 ms. Явные FFT/window/chroma buffers — около 197 KB при 44.1/48 kHz и 394 KB при 96 kHz; это не RSS и не включает внутренние таблицы FFT. Benchmark запускается через `--key-benchmark`.

Тесты и сборка не заменяют DAW smoke test: доступен только GarageBand, который не принимает VST3; REAPER и `pluginval` отсутствуют.

### Phase 2D — Transient Detection

`TransientAnalyzer` работает через общий `AnalysisCoordinator` и анализирует неизменяемый Sample вне аудиопотока. На коротком centered Hann STFT (около 12 ms, hop 1 ms) оценивается положительный spectral flux, рост RMS и high-frequency emphasis. Порог адаптируется по локальному окну; пики объединяются с минимальным расстоянием, а frame lead компенсируется при переводе позиции в samples.

Событие хранит sample position как источник истины, а также время, normalized position, Strength, Confidence и spectral flux. Strength характеризует относительную выраженность атаки; Confidence — эвристическая оценка по prominence и согласованности features, не вероятность. Пустой/слишком короткий, постоянный DC и неподходящий материал возвращают `insufficientData`. При заполнении лимита списка результат помечается `truncated` и сохраняет полный `totalEventCount`.

UI показывает число транзиентов и рисует максимум 2 000 маркеров на waveform. Редактор обновляет кеш при смене идентификатора или статуса анализа.

### Phase 2D.1 — калибровка транзиентов

Для каждого кандидата детектор сравнивает уровни восьми спектральных полос после предполагаемого начала с локальным фоном перед ним. Для короткого щелчка дополнительное подтверждение и координату даёт вторая разность отсчётов. Confidence учитывает локальное подтверждение атаки и превышение фона по novelty. Strength по-прежнему измеряет относительную выраженность novelty.

На синтетических проверках осталось 16/16 исходных положительных событий. На шумовом фоне найдено 8/9 атак против 6/9, а на двух 30-секундных розовых шумах число ложных маркеров снизилось с 51 и 50 до нуля. Одна тихая двухтональная атака маскируется шумом. Маркеры вблизи краёв файла могут отбрасываться из-за неполного контекста. Точность на реальных записях пока не измерялась.

Рабочие массивы признаков используют 64 байта на кадр. Дополнительно учитывается ёмкость списка кандидатов. Оценка для 180 секунд составляет около 12,1 МБ без аудиобуфера, памяти FFT и опубликованных событий. Прогоны доступны через `CreativeSamplerTests --transient-benchmark` и `CreativeSamplerTests --transient-test`.
