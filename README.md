# Creative Sampler

Модульный Standalone/VST3 sampler на C++ и JUCE. Реализованы импорт, воспроизведение с band-limited resampling и фоновый анализ BPM, тональности и транзиентов.

## Сборка

Нужны CMake 3.22+, совместимый C++20 toolchain и JUCE 8 или новее, доступный как CMake package либо исходный каталог.

```sh
npm ci --prefix web-ui
cmake -S . -B build -DJUCE_SOURCE_DIR=/path/to/JUCE
cmake --build build --config Release
ctest --test-dir build --output-on-failure
```

Перед CMake configure установите npm зависимости командой выше: конфигурация автоматически собирает локальный production frontend и встраивает его в приложение. Если JUCE установлен с CMake package config, вместо `JUCE_SOURCE_DIR` передайте путь к его package config через `JUCE_DIR`.

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

## Phase 3A — Slice Engine foundation

`SliceEngine` — независимая message-thread модель, привязанная к уникальному Sample ID и числу исходных samples. При новой загрузке прежняя коллекция заменяется; короткий Sample может быть привязан с пустой коллекцией. Slice использует точный half-open интервал `[startSample, endSample)`, стабильный ID и порядок по начальной позиции. Минимальная длина — 64 sample frames. Пересечения запрещены; gaps допустимы после удаления или при создании диапазона в свободной части Sample.

Выделение диапазона мышью создаёт slice: внутри существующего диапазона оно разделяет родительский slice на левую, выбранную и правую части. Split делит выбранный slice пополам; Delete удаляет его и оставляет явный gap; Reset создаёт один slice на весь Sample. Перетаскивание общей границы двух соприкасающихся slices синхронно меняет конец левого и начало правого; при этом проверяется минимальная длина обоих. Границы у Sample и соседних slices валидируются в sample domain. Изменения масштаба и размера окна не меняют координаты.

Редактируемая коллекция и выбранный slice не читаются `processBlock()`. Текущие операции выполняются на message thread. Playback получает отдельный immutable snapshot с Sample ID, revision, sample-domain границами и MIDI note map.

Waveform перенесён в самостоятельный `WaveformEditorComponent`: компонент рисует существующие multiresolution peaks, selection, sample-domain boundaries, playback position и transient markers; поддерживает выбор slice, drag для создания диапазона, drag boundaries, Split, Delete и Reset. Waveform data не пересчитывается при repaint. Полноценные zoom/scroll, автоматические slicing modes и сохранение slice state пока не реализованы.

## Phase 3B — Slice Playback foundation

`SlicePlaybackSnapshot` строится на message thread из `SliceEngine` и удерживает тот же `SampleHandle`, не копируя аудиобуфер. Проверяются Sample ID, revision, sample rate и непересекающиеся интервалы `[startSample, endSample)`. По умолчанию slice получает MIDI note от C4 вверх по мере выдачи ID (назначения повторяются после MIDI note 127); для дальнейшей mapping UI модель предоставляет `setMidiNote()`.

Processor публикует новый snapshot при импорте Sample и после изменения SliceEngine. Audio thread забирает сырой указатель из атомарного pending slot. Snapshot, заменённый до получения, можно уничтожить на message thread; опубликованный владеется audio engine. Старый snapshot помещается в фиксированную SPSC retirement queue только когда ни один голос его не использует. Если очередь заполнена, движок сохраняет ожидающую замену и повторяет её позже; память не освобождается в `processBlock()`. Timer Processor освобождает retired snapshots на message thread. Каждый голос содержит только фиксированное состояние и заимствует указатель на snapshot, чей владелец удерживается движком до завершения голоса.

`SliceVoice` воспроизводит только назначенный half-open диапазон через существующий windowed-sinc resampler с tap clamping на границе slice. Скорость учитывает отношение sample rate и MIDI pitch относительно C4; velocity масштабирует голос. Начало, Note Off и естественный конец имеют короткий линейный ramp. `SlicePlaybackEngine` обрабатывает MIDI в порядке sample offset, поддерживает до 16 голосов и при заполнении пула заменяет самый старый голос. При одновременном суммировании голосов и основного Sample сигнал усредняется по числу активных слоёв; standalone UI показывает MIDI note на достаточно широких slice labels. Полноценная mapping UI, ADSR, MIDI-channel zones, reverse и voice modes не входят в этот этап.

## Phase 3C.1 — Slice Parameters & ADSR DSP

`SliceParameters` хранит параметры каждого slice отдельно от UI: volume (0–2), pan (-1…1), pitch offset (-24…24 semitones) и ADSR. Времена огибающей ограничены 0–10 секунд, sustain — 0–1. Некорректные и не конечные значения отклоняются до публикации; новые slices начинают с нейтральных параметров. При разрезании существующего slice обе части наследуют его настройки. Reset и замена Sample создают новую модель с параметрами по умолчанию.

Параметры включаются в immutable `SlicePlaybackSnapshot`, поэтому активный голос использует согласованный набор настроек, захваченный при Note On. Последующее редактирование модели попадёт в голос только при публикации нового snapshot и запуске следующей ноты; уже звучащий голос не меняет параметры посреди playback. DSP-огибающая `ADSREnvelope` — фиксированное состояние без выделений памяти. Она использует линейные Attack, Decay и Release и постоянный Sustain, масштабирует сегменты по частоте дискретизации, начинает Release с текущего уровня и при повторном Note On начинает атаку с нуля. MIDI Note Off запускает заданный Release. На естественном конце диапазона действует ограниченный короткий защитный fade, чтобы голос не обрывался резко; конец slice/sample остаётся жёсткой границей чтения.

Slice volume и velocity масштабируют сигнал голоса. Mono получает equal-power pan, а для stereo применяется balance pan, сохраняющий идентичность левого и правого каналов. Перечисленные параметры доступны в модели и playback DSP, но интерфейс редактирования параметров в этой фазе не добавлен. Огибающая линейная и не поддерживает curve shaping; полноценное сглаживание пользовательских изменений, reverse и эффекты остаются будущими задачами.

### UI technology audit

Изначально для waveform editor был выбран **Native JUCE**: он переиспользует plugin UI и даёт прямой message-thread hit-testing/drag без browser bridge. В Phase UI-1 добавлен отдельный React/JUCE WebView prototype, Native JUCE UI сохранён в проекте. Это проверка гибридного подхода, а не завершённая миграция.

WebView даёт быстрый CSS/SVG/Canvas/WebGL workflow и богатый набор web tooling, но требует отдельного JS↔C++ контракта, встраивания frontend ресурсов и платформенной валидации. В JUCE backend зависит от платформы; Windows WebView2 настраивается отдельно и требует учесть наличие и распространение runtime. Это увеличивает packaging и host-compatibility работу для VST3/Standalone.

Prototype использует JUCE WebView для отдельной рабочей композиции, а backend, модели, параметры и DSP остаются в C++. Waveform отображается по подготовленным peaks; C++ bridge передаёт sample/slice state и маршрутизирует доступные команды. Производительность и совместимость WebView в VST3 host и на Windows ещё требуют отдельной проверки.

## Phase 3C.2 — параметры выбранного слайса

Под waveform расположены семь регуляторов. Перетаскивание меняет значение, двойной щелчок возвращает значение по умолчанию. Панель следует за выбором в waveform. Новые значения применяются к следующим нотам после публикации снимка процессором. Время отображается в миллисекундах или секундах, громкость и sustain — в процентах, высота — в полутонах.

## Phase UI-1 — WebView prototype (не завершена)

Frontend находится в `web-ui/` и собирается React + TypeScript + Vite + Tailwind в один `dist/index.html`; ресурсы встраиваются в `BinaryData`, поэтому запущенный Release UI не использует Vite server, CDN или внешние шрифты. Для чистой сборки сначала выполните `npm ci --prefix web-ui`; затем CMake configure автоматически запускает production build и встраивает страницу. После изменения frontend повторно выполните CMake configure, чтобы обновить embedded bundle.

На macOS Standalone вручную проверена загрузка страницы из JUCE resource provider, показ импортированного WAV (имя, длительность, sample rate, каналы), waveform peaks, Slice list и параметров. Bridge использует JUCE `withNativeFunction` callbacks на message thread: передаёт состояние, Slice ID и выбранный Slice, а также связывает Import, Play/Stop, Split/Delete/Reset и редактирование Volume/Pan с существующим C++ backend. В C++ выдаются актуальные Pitch/ADSR и результаты завершённого анализа для текущего Sample ID; до завершения анализа BPM, Key и число транзиентов остаются пустыми. Audio thread не вызывает WebView и не передаёт frontend состояние.

Проверены вручную импорт файла через React, split/reset, обновление Slice list, выбор Slice и изменение Volume/Pan с отражением тех же значений в существующей Native JUCE панели. Frontend хранит только временное состояние отображения; модель параметров и публикация immutable playback snapshot остаются в C++. Native JUCE UI остаётся в проекте как fallback, но в текущей Standalone-композиции перекрыт WebView.

Ограничения: веб-интерфейс пока не реализует перетаскивание границ/создание диапазона мышью, полноценное удаление есть отдельной командой, но ручное редактирование waveform и полноценные zoom/scroll отсутствуют; часть навигации и Analyze-кнопка — только визуальные элементы. Повторное открытие редактора, DAW host smoke test, VST3 с WebView и Windows/WebView2 не подтверждены. Phase UI-1 остаётся незавершённой до проверки lifecycle/reload, production VST3 host и платформенного развёртывания.
