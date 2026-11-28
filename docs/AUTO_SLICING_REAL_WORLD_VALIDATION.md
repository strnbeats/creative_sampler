# Auto Slicing — Real-World Validation Status

Дата обновления: 28 ноября 2026 г.
Это технический отчёт по инфраструктуре и пилотным материалам. Пользователь прослушал source primary и подтвердил их пригодность; сами результаты нарезки Transient/Hybrid ещё не прослушивались. Отчёт **не подтверждает музыкальное качество Auto Slicing**.

## Обновление корпуса после curation

Из сохранённого inventory сформирована локальная очередь: 15 Apple Loop drum-кандидатов, 15 melodic-кандидатов и 14 voice/vocal-кандидатов (15 primary и 29 reserve). Пользователь подтвердил пригодность всех primary. 15 primary декодированы FFmpeg 9.1 в WAV PCM 24-bit, 44,1 kHz stereo, и все обработаны существующим Offline Runner. Reserve остаются недекодированными. Это локальная validation-конвертация, не добавление AAC-поддержки в production `AudioImporter`; предыдущий production-importer пилот на репрезентативных AAC CAF завершился 0/15. У primary нет независимой подробной разметки `is_music_loop`/`is_real_vocal`; подтверждение пригодности кандидатов не трактуется как подтверждение этих полей. 15 старых WAV resources остаются отдельной группой sample assets, а не loop-корпусом. Подробности: [AUTO_SLICING_CORPUS_CURATION.md](AUTO_SLICING_CORPUS_CURATION.md). Пользователь прослушал исходные файлы; algorithmic exports ещё не прослушивались, поэтому качество нарезки не валидировано.

## 1. Краткий статус

Создан независимый C++ Offline Validation Runner, который использует production `AudioImporter`, `BPMAnalyzer`, `TransientAnalyzer`, `BeatGridEstimator`, Transient Planner, Hybrid Planner и общий Slice Plan Validator. Оба планировщика получают один и тот же импортированный PCM и один результат анализа.

Технический smoke run на 15 локальных WAV-ассетах завершился успешно: импорт и анализ выполнены для всех файлов, обе стратегии вернули валидные планы с полным покрытием источника. Это **изолированные drum, melodic и vocal sample assets из Logic Alchemy Samples, а не музыкальные loops**. В этих материалах Beat Grid не дала надёжной сетки: Hybrid во всех случаях корректно перешёл на Transient и создал те же границы.

Проверка Apple Loops обнаружила блокер для текущего importer: все 15 выбранных loop-кандидатов имеют CAF-контейнер с AAC audio data, и все 15 получили отказ декодирования. Системная команда `afconvert` также не смогла преобразовать проверенный AAC CAF (`ExtAudioFileSetProperty ('cfmt') failed ('fmt?')`). Оригиналы не менялись.

**Этап остаётся незавершённым как музыкальная валидация.** AAC CAF препятствует прямому импорту в production AudioImporter, но принятые primary теперь декодируются отдельным воспроизводимым локальным FFmpeg шагом и проходят Offline Runner. Нужно прослушать исходники и результаты Transient/Hybrid, заполнить ручные оценки и затем расширить корпус, используя reserve при необходимости.

## 2. Validation corpus и статусы просмотра

Полный локальный inventory содержит 3 578 записей, как описано в [AUTO_SLICING_REAL_AUDIO_INVENTORY.md](AUTO_SLICING_REAL_AUDIO_INVENTORY.md). Сами файлы остались в системных библиотеках. Ни один аудиофайл или результат не добавлялся в Git.

Подготовлены локальные, игнорируемые manifest-файлы:

- `tests/fixtures/auto_slicing/results/manifest.local.csv` — исторический 15-файловый AAC CAF pilot, помеченный `unreviewed`; он отражает предыдущие неудачные попытки импорта.
- `tests/fixtures/auto_slicing/results/corpus-curation/pcm-primary/manifest.pcm.local.csv` — текущие 15 accepted primary после локального PCM decode, готовые к Runner.
- `tests/fixtures/auto_slicing/results/manifest.native-pcm-smoke.csv` — 15 доступных PCM WAV ресурсов Logic, по пять folder/name кандидатов в категориях drums, melodic и vocals. Они подходят для технического smoke test, но не образуют loop corpus.
- `tests/fixtures/auto_slicing/results/manifest.formats.csv` — реальный AIF и реальный LPCM CAF для format smoke test.

В `corpus-curation/human_review.csv` 15 primary имеют статус `accepted` по подтверждению пользователя; точные подкатегории и свойства loop/реального вокала остаются неразмеченными. Ratings и comments о результатах стратегий остаются пустыми. Название папки или файла не считается доказательством слышимого содержимого. В частности, `Choir`/`Vox` candidates нельзя автоматически считать разборчивыми вокальными фразами.

## 3. Импорт и codec verification

| Проверка | Образец | Результат production importer | PCM / декодер |
|---|---|---|---|
| WAV LPCM | 15 ресурсов Logic Alchemy | 15/15 успешно; полные PCM-данные прошли finite-sample проверку | `WAV file`, исходная глубина по reader, внутренний буфер float32 |
| AIF LPCM | `IBFILA1A1X03.aif` | успешно; 311 454 frames | `AIFF file`, 16-bit source, mono 44,1 kHz; внутренний буфер float32 |
| CAF LPCM | `AP v1 Sticks_consolidated.caf` | успешно; 267 264 frames | `CoreAudio supported file`, 32-bit source, stereo 44,1 kHz; внутренний буфер float32 |
| CAF AAC, previous importer pilot | 15 representative Apple Loop candidates | 0/15; импортёр сообщает, что CAF damaged или codec нельзя декодировать | `AudioFormatManager` не создал reader; `afconvert` вернул `cfmt`/`fmt?` |
| CAF AAC-LC, accepted primary | 15 кандидатов из текущего corpus | 15/15 декодированы локально FFmpeg 9.1 в WAV PCM 24-bit; затем 15/15 успешно обработаны Offline Runner | FFmpeg AAC decoder; production AudioImporter отдельно не менялся |
| Другие CAF codecs | В inventory не найдены | Не проверялись | Inventory сообщает только AAC и LPCM CAF |

На всех 15 accepted primary FFprobe прочитал `codec_name=aac`, `profile=LC`, 44 100 Hz, 2 channels, `fltp`; CAF `desc` содержит codec ID `'aac '`, frames-per-packet 1024 и 0 bits/channel (нормально для compressed audio). Magic cookie — 39 bytes. CAF `pakt` содержит число AAC packets, valid frames, priming=2 112 frames и индивидуальный remainder. Например, `Concrete Jungle Beat.caf`: 554 packets, 564 480 valid frames, 2 112 priming и 704 remainder. У всех 15 FFmpeg WAV-копии совпали по длительности, sample rate и channels с источником; output frames совпали с CAF valid frames. SHA-256 каждой исходной CAF до и после декодирования совпал.

Диагностика показывает, что CoreAudio metadata reader открывает контейнер и packet table, но AAC-to-PCM converter не создаётся: `afconvert -f WAVE -d LEI16` завершает `ExtAudioFileSetProperty ('cfmt') failed ('fmt?')`; AVFoundation `AVAssetReader` завершился NSError -11800 с underlying OSStatus -1. JUCE `AudioImporter` получает `nullptr` от `AudioFormatManager::createReaderFor()`. Поэтому причина не в расширении или повреждённом заголовке; отказ происходит при настройке/выполнении системного AAC decode path на этих CAF. Конкретная внутренняя причина OSStatus -1 непрозрачна, поэтому не утверждаем, что это общая неспособность CoreAudio декодировать любой AAC.

Вместо добавления decoder dependency в production был выбран локальный FFmpeg 9.1 CLI для validation-only конвертации. Он декодирует эти AAC-LC payload, и его результаты проверяются до Runner. Новая зависимость/связь к JUCE, Audio Importer, CMake production targets не добавлялась. Исходники остаются неизменными; PCM-копии записаны только в ignored `results/`.

FLAC, MP3, OGG, M4A и отдельные AAC-файлы в корпусе не встретились. Отдельная поддержка этих расширений не проверялась.

## 4. Offline Validation Runner

Исходник находится в `tools/auto_slicing_validation/main.cpp`; CMake target — `AutoSlicingValidationRunner`. Инструмент не запускает UI, PluginEditor, Audio Engine или realtime callback.

Для каждой строки manifest runner:

1. Разрешает абсолютный локальный путь либо путь под заданным `--root`.
2. Вычисляет детерминированный локальный dual-64 fingerprint входного файла; fingerprint не криптографический.
3. Декодирует целый файл через production `AudioImporter`, включая проверку ошибок чтения, числа каналов и finite float samples.
4. Запускает production BPM и transient analyzers ровно один раз на неизменённом `SampleStorage`.
5. Создаёт один `AnalysisResult`, оценивает Beat Grid, передаёт те же PCM/analysis Transient и Hybrid Planner и валидирует оба плана.
6. Продолжает batch при ошибке отдельного файла и сохраняет её как локальный `error.json`.

Результаты включают per-file JSON с source ID/revision/generation, fingerprint, decoder name, source bit depth, decoded float32 format, BPM candidates, transients и их frames/confidence/strength/flux, Beat Grid/phase/reliability, планы, границы, provenance, fallback и diagnostics. Runner парсит созданный JSON перед записью, чтобы не публиковать некорректный результат.

Локальные экспорты:

- `metrics.csv` — по две строки на файл, длительности, число слайсов, provenance, fallback и Beat Grid status;
- `category_summary.csv` и `summary.json` — агрегаты batch;
- `boundaries.svg` — waveform с Transient/Hybrid границами;
- `source_decoded.wav`, `transient_audition.wav`, `hybrid_audition.wav` — source copy и audition exports. Стратегические exports соединяют slices с 250 ms тишины; это помогает сравнивать границы, но не сохраняет исходный музыкальный timing между слайсами;
- `listen_catalog.csv` и `human_review.csv` — индекс прослушивания и незаполненный лист оценки.

Все outputs созданы под игнорируемым `tests/fixtures/auto_slicing/results/`. В них могут быть локальные absolute paths, поэтому эти результаты не должны коммититься. AAC CAF decode производится отдельным локальным скриптом `tools/auto_slicing_validation/decode_accepted_aac_caf.py`; он не входит в production importer.

## 5. Пилотные результаты

### Native PCM smoke group: 15 файлов

Это техническая выборка Logic Alchemy: пять drum/percussive isolated assets, пять отдельных melodic notes/multisample assets, пять vocal/phonetic/synthetic assets. Категории взяты из ресурсных папок и имён; слухом не проверены.

- Успешное декодирование и анализ: **15/15**.
- Валидные планы Transient: **15/15**; Hybrid: **15/15**.
- Полное покрытие PCM: **15/15** у обеих стратегий.
- У обеих стратегий одинаковые границы на **15/15** файлах.
- 14/15 файлов обе стратегии оставили одним целым слайсом; один melodic sample получил два.
- Beat Grid для всех 15 привела к `transientFallback`; все планы Hybrid совпали с Transient.

Это ожидаемо для одиночных инструментальных звуков с короткой или неустойчивой ритмической структурой. Значение «один слайс» не является оценкой хорошего/плохого музыкального результата без прослушивания.

### Форматный smoke group: 2 файла

- AIF импортирован, обе стратегии вернули валидный целый план.
- PCM CAF импортирован, обе стратегии вернули одинаковый валидный план на 18 слайсов; grid не использовалась.
- Это подтверждает чтение конкретных AIF и LPCM CAF, но не все возможные вариации этих контейнеров.

### Historical Apple Loop AAC importer pilot: 15 файлов

- Доступность файлов подтверждена.
- Импорт через production `AudioImporter`: **0/15 успешно**, **15 ошибок декодирования**.
- Планировщики не запускались, поскольку runner не получил PCM.
- `afconvert` не смог декодировать проверенный Apple Loop AAC CAF, оригинал не менялся.

Для этих 15 прежних pilot-файлов сравнение Transient/Hybrid не состоялось, потому что production importer не выдал PCM. Новый принятый primary-набор декодирован отдельно и описан ниже.

### Accepted primary AAC CAF corpus: 15 файлов

Пользователь подтвердил пригодность 15 primary: 5 drums, 5 melodic и 5 vocals/voice-like. Локальный конвертер сохранил PCM WAV в `tests/fixtures/auto_slicing/results/corpus-curation/pcm-primary/`; manifest и decode report там же. После конвертации существующий Runner использовал production importer и алгоритмы, а не альтернативные планировщики.

- Декодирование: **15/15**; source hashes без изменений; WAV: 24-bit PCM, 44,1 kHz, stereo; длительности и PCM frame counts совпали с CAF valid frames.
- Runner import/analysis: **15/15**, ошибок нет; Transient plans **15/15 valid**, Hybrid plans **15/15 valid**, coverage у всех полная.
- Slice counts совпали между стратегиями на всех 15 файлах: drums — среднее 12,2, melodic — 4,4, vocals/voice-like — 6,2. Hybrid boundary arrays совпали с Transient на **15/15**; grid-assisted boundaries — 0, short slices по Runner метрике — 0.
- Fallback: drums 0/5; melodic 3/5 (`weakTransients` 2, `noFeasibleSplit` 1); vocals 2/5 (`weakTransients`). Это диагностические outcomes, не музыкальная оценка.
- Автоматические границы и waveform plots доступны в `runner-primary/`; source, Transient и Hybrid audition WAV exports тоже созданы.

Этот результат доказывает техническую декодируемость выбранных AAC CAF и прохождение текущих алгоритмов, но не то, что полученные границы музыкально удачны. User acceptance относилась к исходным кандидатам, а не к слайс-планам.

## 6. Прослушивание и оценка качества

Пользователь прослушал исходные 15 primary и подтвердил, что все подходят для тестирования. Локальный Human Listening Evaluation Player реализован: [инструкция запуска и использования](AUTO_SLICING_HUMAN_REVIEW.md). Он загружает те же WAV и JSON-планы, отображает frame boundaries и позволяет независимо оценить Transient и Hybrid. Плеер технически готов, однако comparative listening и заполнение оценок человеком ещё не завершены. Нельзя делать выводы о музыкальной точности границ, сохранении swing/pickup/legato, резке слов/слогов или пригодности полученных pads.

Для декодируемых smoke samples точные исходные пути и WAV/strategy previews перечислены в локальных `listen_catalog.csv`. Для accepted primary исходные CAF и локальные PCM/Transient/Hybrid previews указаны в `corpus-curation/listen_catalog.html` и `runner-primary/listen_catalog.csv`; также их можно сравнить в плеере по инструкции выше. Следующий human-review шаг — прослушать алгоритмические slices в контексте и сохранить оценки обеих стратегий отдельно. Не переносить `accepted` исходного кандидата автоматически на качество Slice Plan.

## 7. Математическая, техническая и музыкальная готовность

| Вид проверки | Результат |
|---|---|
| Математическая корректность | Smoke-планы прошли существующий validator, полное покрытие подтверждено; это ограниченная выборка файлов, а не доказательство для всех inputs. |
| Техническая корректность | WAV/AIF/LPCM CAF production import повторно прошли Runner smoke; 15 AAC-LC CAF декодированы FFmpeg и приняты Runner; все 30 планов прошли validator с полным покрытием. |
| Музыкальное качество | Пользователь принял исходные кандидаты, но не оценивал нарезки двух стратегий. Их музыкальная пригодность остаётся неподтверждённой. |

**Готовность к Basic Mode: недостаточная.** Runner готов к масштабированию на декодируемых материалах. Интегрировать Auto Slicing в Basic Mode до появления и ручного прослушивания репрезентативных drum, melodic и vocal loop results рано.

## 8. Запуск

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release -DBUILD_TESTING=ON
cmake --build build --target AutoSlicingValidationRunner
./build/AutoSlicingValidationRunner_artefacts/Release/AutoSlicingValidationRunner \
  --manifest tests/fixtures/auto_slicing/results/manifest.native-pcm-smoke.csv \
  --root . \
  --output tests/fixtures/auto_slicing/results/native-pcm-smoke
```

Для принятой AAC CAF выборки локальный путь повторяется так:

```sh
python3 tools/auto_slicing_validation/decode_accepted_aac_caf.py
./build/AutoSlicingValidationRunner_artefacts/Release/AutoSlicingValidationRunner \
  --manifest tests/fixtures/auto_slicing/results/corpus-curation/pcm-primary/manifest.pcm.local.csv \
  --root . \
  --output tests/fixtures/auto_slicing/results/corpus-curation/runner-primary
```

Файлы в `results/` локальные и намеренно игнорируются Git. Не добавлять Apple Loops, PCM exports, CSV с абсолютными путями и per-file JSON в репозиторий.
