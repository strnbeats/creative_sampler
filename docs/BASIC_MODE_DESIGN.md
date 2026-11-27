# Creative Sampler — Basic Mode: продуктовый и архитектурный план

**Этап:** UI-2.4A — аудит и проектирование
**Статус:** концепция; функциональность не реализована
**Проверенная кодовая база:** 27 ноября 2026 года

## 1. Краткий вывод

Basic Mode следует строить как отдельный игровой инструмент поверх общей модели семпла и слайсов. Его центр — автоматически подготовленный kit, шесть назначаемых макроконтролов и выбранный FX Preset. Пользователь получает музыкально пригодный результат сразу после импорта; waveform в этом режиме объясняет структуру материала, но не становится редактором границ.

Фактически сейчас:

- Импорт создаёт `SampleStorage`, waveform peaks и одну область Slice на весь sample. Изменения модели Slice выполняются на control/message thread, а `SlicePlaybackSnapshot` публикуется для audio thread отдельно.
- `AnalysisCoordinator` использует worker-поток и generation/sample identity, запускает BPM, Key и transient анализаторы. Это полезные признаки для будущей нарезки, но они не создают музыкальные части.
- `SliceEngine` умеет создать, разделить, удалить и изменить слайс. В нём нет автоматической стратегии, предварительного плана или атомарной замены всего списка слайсов.
- `SlicePlaybackEngine` воспроизводит Slice voices через MIDI, поддерживает до 16 одновременно звучащих voices. UI-v2 не предоставляет MIDI-триггер слайса; его Play/Stop управляет sample transport.
- Поиск в `src/` не выявил FX Engine, цепочки эффектов, FX Preset модели, макроназначений, модуляции, XY-контрола или LFO. Имеющиеся Volume/Pan/Pitch/ADSR — параметры голоса слайса, а не FX chain.
- Production UI-v2 — Lithic Advanced-подобный экран с waveform, slice navigation и семью параметрами слайса. Переключателя Basic/Advanced, kit launch surface и шести макроконтролов в нём нет.

Следствие: автоматическая музыкальная нарезка и FX/macro система — две новые продуктовые подсистемы. Их не следует имитировать в React или пытаться выразить одними изменениями UI.

## 2. Продуктовая концепция и основной сценарий

### Обещание Basic Mode

«Загрузите звук — Creative Sampler подготовит из него играбельный материал. Выберите характер обработки и играйте, меняя звук шестью крупными макроконтролами».

Basic Mode не показывает ручки границ, sample positions, технические параметры SliceEngine или семипараметрический инспектор. Если движок не нашёл надёжных мест для нарезки, режим честно сохраняет цельный музыкальный фрагмент вместо искусственных разрезов.

### User Flow

1. **Import.** Пользователь выбирает или перетаскивает аудиофайл. UI показывает стадию чтения, а текущий успешно загруженный sample остаётся целым до подтверждённой замены.
2. **Prepare.** На фоне строятся доступные analysis results и кандидатный Slice Plan. UI показывает сдержанное состояние подготовки и не выдаёт промежуточную цельную область за готовый multi-slice kit.
3. **Ready.** C++ принимает валидный план для той же sample identity и revision, создаёт слайсы в общей SliceEngine и публикует playback snapshot. UI показывает готовый kit и его размер.
4. **Play.** Пользователь играет kit через MIDI host. Для Standalone и быстрого предпрослушивания требуется отдельное решение о software trigger pads и безопасном MIDI-триггере через bridge; текущая WebView команды для запуска Slice voice не имеет.
5. **Choose a sound.** Пользователь просматривает FX Presets по музыкальным категориям и выбирает один. Отображаются название, короткое описание и макроназначения, заданные автором пресета.
6. **Perform.** Шесть макроконтролов изменяют одобренные параметры эффектов. Смена пресета меняет подписи, диапазоны и mapping, а текущие значения подтверждаются C++.
7. **Switch mode.** Advanced открывает те же Sample и Slice данные с точным waveform и ручным редактированием. Возврат в Basic не запускает анализ заново, не сбрасывает границы, MIDI mapping, FX Preset или значения макросов.

Basic kit — это представление общей SliceEngine, а не вторая копия слайсов и не отдельный playback engine.

## 3. Аудит текущей архитектуры

| Область | Реально есть | Ограничение для Basic |
|---|---|---|
| Импорт и sample | `AudioImportService`, `SampleStorage`, metadata, waveform generation; при успешной загрузке Processor назначает новый `sampleId` | Импорт сразу устанавливает целый Slice; нет статуса auto-slicing или готового kit |
| Analysis | `BPMAnalyzer`, `KeyAnalyzer`, `TransientAnalyzer`; coordinator работает в одном worker thread, отменяет старые generation и игнорирует поздние результаты | Анализаторы возвращают признаки, а не классификацию источника и не границы музыкальных фраз |
| BPM | Кандидаты, confidence/score, ambiguous и detection status; эффективный диапазон по умолчанию 50–220 BPM; короткий или неритмичный материал может вернуть отсутствие значения | Confidence — эвристика, не калиброванная вероятность; BPM не доказывает наличие устойчивой сетки |
| Key | Тональность, лад, кандидаты, confidence и status | Тональность помогает гармоническому контексту, но не определяет slice boundaries |
| Transients | Sample positions, strength, optional confidence, spectral flux, total count и truncated/insufficient status | Ударные события не равны музыкальным фразам; плотный вокал и шум могут дать много событий |
| SliceEngine | Сортированный список непересекающихся Slice с `[startSample, endSample)`, MIDI note, параметрами и монотонной revision; create/split/delete/move/reset | Нет batch replace/transaction метода, auto provenance, выбранной стратегии, batch validation или Undo для генерации |
| Playback | Immutable `SlicePlaybackSnapshot`, MIDI Note On/Off, polyphony до 16 voices; существующая обработка Volume/Pan/Pitch/ADSR | WebView не запускает выбранный Slice; active voice telemetry ограничена счётчиком, MIDI-полифония не тождественна Sample Transport |
| MIDI раскладка | Новый Slice получает ноту по `SliceId`: начиная с C4, затем до G#9 с циклическим повтором через 68 ID | ID накапливаются и между заменами Sample; нота первого Slice нового sample не обязана быть C4. После wrap повторяющаяся нота может запускать несколько regions. Для auto kit нужен явный mapping по порядку kit, не по глобальному ID |
| FX | FX processor/chain/preset-кода не найдено; проектные FX tests и FX classes отсутствуют | Вся FX обработка, эффекты, управление хвостами и сохранение — разработка с нуля |
| Parameters | APVTS содержит глобальный `volume` (0–1, сохранение/host parameter); семь Volume/Pan/Pitch/ADSR хранятся в SliceParameters | Нельзя направлять Basic macros в одну из этих систем по умолчанию: нужны отдельные FX destinations и собственная preset mapping модель |
| UI-v2 | Typed/versioned protocol v2, snapshot, per-channel waveform query, sample metadata, analysis, Slice IDs/parameters, import, transport, `setUiV2SliceParameter` | Нет Basic mode, auto-slice endpoint/state, FX catalog/preset/macro messages или slice-note trigger |
| Plugin persistence | `getStateInformation`/`setStateInformation` сейчас сериализуют APVTS ValueTree, в которой есть Master Volume | FX chain, preset ID, макронастройки, пользовательская библиотека и Slice state не имеют спроектированной persistence схемы |

### Примечание о документах

`web-ui/UI_ARCHITECTURE.md` и `web-ui/src-v2/INTEGRATION_CONTRACT.md` содержат полезные принципы владения состоянием, stale protection и realtime safety, но описывают более ранние срезы UI-v2. В текущем коде уже существуют production versioned snapshot и команда редактирования параметров. Текущие возможности выше сверены с `PluginEditor.cpp`, `PluginProcessor.cpp` и протоколом v2. Ранняя `web-ui/exploration/DESIGN_SPEC.md` описывает Expressive, а исторический `ART_DIRECTION.md` ранжировал Prism выше Lithic; это решение позже заменено явным выбором пользователя. Для Basic визуальной основой остаётся выбранная пользователем Lithic, а не исторические рекомендации этих документов.

## 4. Automatic Musical Slicing

### Рекомендуемый pipeline

1. **Import identity.** Processor присваивает sample новый `sampleId`; прежний auto-slice результат больше не может примениться к этой загрузке.
2. **Analysis.** Существующие BPM/Key/Transient анализаторы выполняются вне audio callback. Анализ остаётся отменяемым по sample/generation.
3. **Feature summary.** На worker строится компактная сводка: длительность, доля тишины/энергии, плотность и устойчивость transient событий, интервалы между сильными onset, BPM confidence, сеточная устойчивость и наличие крупных пауз. Не копировать PCM в UI и не запускать работу на audio thread.
4. **Strategy selection.** Правила выбирают одну из стратегий ниже или понижают уверенность. Это эвристическая классификация, а не обещание распознавать источник как «вокал» или «барабаны» с ML-точностью.
5. **Slice Plan.** Worker возвращает value object: sampleId, analysis generation, ожидаемую Slice revision, стратегию, confidence/причину выбора и упорядоченные границы в sample frames. Сам `SliceEngine` worker не меняет.
6. **Validation and commit.** На message thread C++ проверяет идентичность, revision, целые sample-domain границы, монотонность, длительности, лимит количества и отсутствие overlap. Вся замена применяется атомарно как одна операция SliceEngine с одной revision. Невалидный план не должен оставлять частичный kit.
7. **Playback publication.** Существующий Processor timer публикует новый immutable snapshot. Audio thread получает только готовый snapshot и фиксированные MIDI mapping; генерация списка и выделение памяти остаются вне callback.
8. **UI confirmation.** React отображает только подтверждённый Slice state и его status. Запоздавший plan/analysis отбрасывается, если поменялся sampleId или generation.

Можно расширить orchestration `AnalysisCoordinator` и выдавать Slice Plan как результат той же generation после анализа. Это предпочтительнее второго независимого анализа того же PCM. Если реализация отдельного worker всё же потребуется, он должен быть ограничен и разделять отмену/проверку identity; не создавать конкурирующий SliceEngine.

### Стратегии по материалу

| Внутренняя стратегия | Когда выбирать | Как расставлять границы | Типичный материал и ограничения |
|---|---|---|---|
| **Transient kit** | Высокая плотность уверенных onset, выраженная атака и низкая потребность в длинном гармоническом контексте | Сильные транзиенты становятся кандидатами; слабые/слишком близкие объединяются. Сохранять хвост, где возможно, избегать срезов внутри длинного затухания | Перкуссия, ударные, короткие FX. Один транзиент может быть фламом или слогом — нужна группировка |
| **Beat/grid** | BPM имеет достаточно высокий confidence, интервалы onset подтверждают период и фазу | Кандидаты сетки на долю/полтакта/такт; к ближайшему сильному onset привязывать только в ограниченном допуске. Сохранить разумный swing, если onset систематически смещены | Ритмичные лупы, электронные паттерны. Один BPM без стабильной фазы недостаточен |
| **Phrase-aware** | Видны крупные паузы/энергетические переломы; устойчивый beat-grid отсутствует или вторичен | Резать на сильных паузах и мягких энергетических долинах; объединять близкие слоги/ноты; избегать границ внутри связных мелодических жестов | Вокал, spoken word, мелодические фразы. Key — контекст, а не готовая разметка фраз |
| **Hybrid** | Есть достоверные transient anchors и сетка, но ни одна сама по себе не даёт хорошей музыкальной структуры | Сильные onset задают локальную артикуляцию, grid стабилизирует длительность и повторяемость; слабые onset удаляются, длинные области делятся по музыкальным опорам | Поп/электронные лупы с вокалом/перкуссией |
| **Texture fallback** | Низкая уверенность, мало транзиентов, ненадёжный BPM и нет убедительных phrase breaks | Если есть безопасные крупные долины — создать несколько широких частей. Если нет — оставить один цельный Slice с явным пояснением «не найдено надёжных точек раздела» | Ambient, drones, шумовые/атмосферные семплы. Не следует выдавать равные случайные куски за музыкальную нарезку |

### Правила количества и качества

- Стратегия выбирается автоматически; Basic не показывает confidence thresholds, onset density или frame positions.
- Для обычного kit разумно начать с целевого диапазона 4–16 частей, а для плотной перкуссии разрешить больше. Начальный верхний лимит — **32 Slice** до сбора реального corpus и измерения UX. Текущий MIDI диапазон предоставляет до 68 уникальных последовательных нот, поэтому не рассчитывать на циклический wrap; если позднее потребуется >68 частей, нужен явный note bank/page mapping.
- При слишком большом числе onset объединять соседние слабые интервалы и ограничивать минимальную музыкальную длительность. Технический минимум `SliceEngine` в 64 sample frames — только защита модели, а не достаточный музыкальный минимум. Минимум должен быть адаптивным: короткие ударные могут быть существенно короче вокальных/phrase частей.
- Для сетки использовать tempo/beat duration и phrase/bar candidates. Неподтверждённое BPM не должно смещать transient slice на воображаемую сетку. При конфликте grid с уверенной атакой предпочитать атаку либо снижать confidence.
- Тишина не становится самостоятельным слайсом, если это не музыкально значимый pickup/pause. Не срезать затухание только ради равной длины. Слабые транзиенты фильтровать относительно локальной энергии и соседних событий, а не одним глобальным порогом.
- Сохранять покрытие sample там, где это музыкально необходимо, но не обязывать SliceEngine устранять все gaps: модель уже допускает осмысленные gaps. Точные правила хвоста нужно подтвердить слуховыми тестами, так как SliceVoice ограничивает voice диапазоном slice и завершает его коротким safety fade.
- Статусы UI: `analysing`, `buildingKit`, `ready`, `fallback`, `failed`. `fallback` должен объяснять, что создана цельная или крупная структура из-за отсутствия надёжных точек. Ошибка генерации не удаляет последний успешно загруженный Sample/kit.
- В UI не показывать false BPM/Key или уверенность как ground truth. Сохранять отдельно detection status и confidence; текущий код сам различает detected, low-confidence, insufficient-data и no-rhythm cases.

### Пользовательское поведение и ручное редактирование

Basic не перемещает границы и не предлагает инструменты Split/Delete/Reset. Можно дать простой выбор заранее подготовленных характеров нарезки, например **Auto**, **Beat**, **Phrase**, но только после доказательства, что это реально улучшает результат; по умолчанию режим выбирает сам двигатель. Ручной slicing остаётся Advanced.

Автоматическая нарезка должна запускаться один раз при загрузке или явном запросе regenerate. Изменения границ в Advanced сохраняются при возврате в Basic. Не запускать auto-slice при каждом переключении режимов. Повторная генерация — отдельная подтверждаемая операция, которая явно заменяет текущую структуру и сбрасывает/пересоздаёт только связанные слайсы.

## 5. FX Preset System

### Наличие сейчас и целевая модель

**Сейчас отсутствуют:** эффекты, runtime FX graph/chain, повторяемые FX instances, preset catalog, preset persistence, effect-parameter validation, macro mapping, XY, modulation и custom LFO. Существующий global APVTS Volume — только выходной уровень, а Volume/Pan/Pitch/ADSR — per-slice voice DSP.

Предлагаемый пресет — типизированное описание полной конфигурации, а не snapshot шести чисел:

```ts
type FxPresetDefinition = {
  schemaVersion: 1;
  presetId: string;
  name: string;
  categories: FxCategory[];
  description: string;
  chain: FxNodeDefinition[];       // порядок обработки
  macros: MacroDefinition[];       // ровно 6 slots, preset supplies names/mappings
  modulation: ModulationRoute[];  // reserved for later capability, may be empty
  initialMacroValues: number[];    // normalized 0..1
};

type FxNodeDefinition = {
  nodeId: string;                 // unique instance ID, even for repeated effect types
  effectType: string;              // resolved by native registry
  enabled: boolean;
  parameters: Record<string, number>;
};

type MacroDefinition = {
  slot: 0 | 1 | 2 | 3 | 4 | 5;
  label: string;
  hint: string;                   // human-readable sound result
  mappings: Array<{
    nodeId: string;
    parameterId: string;
    low: number;
    high: number;
    curve: 'linear' | 'log' | 'easeIn' | 'easeOut';
    bipolarCenter?: number;
  }>;
};

type ModulationRoute = {
  sourceId: string;
  nodeId: string;
  parameterId: string;
  depth: number;
};
```

Это schema proposal, не существующие application types. Native validation must reject unknown effect/parameter IDs, duplicate node IDs, missing mapping targets, non-finite/out-of-range values and unsupported schema versions. Preset data is declarative; no scripts, downloaded binaries or arbitrary plugin code.

Unique `nodeId` позволяет повторно поставить один effect type в разных местах цепи, если runtime registry поддержит несколько независимо подготовленных instances. В первой реализации цепь линейная и без feedback cycles. XY и LFO пока сохраняются в схеме как возможность будущей версии; до фактического DSP нельзя показывать их как работающие источники.

### Хранение, загрузка и применение

- **Built-in presets:** versioned JSON-совместимые определения, поставляемые как локальные assets. Никакого внешнего сервера или новой зависимости.
- **Пользовательские пресеты:** отдельный следующий этап: локальное сохранение валидированных definitions и миграция schema. Не смешивать с первой поставкой built-in library.
- **Plugin state:** сохранять выбранный preset ID, schema version, macro normalized values и совместимые пользовательские overrides в ValueTree/state. Сейчас `getStateInformation` сериализует только APVTS; новая state branch должна сохраняться/восстанавливаться отдельно, не меняя идентификатор Master Volume.
- **Apply:** C++ загружает и валидирует definition, готовит новые effect instances и их буферы вне audio thread, затем атомарно переключает chain на границе блока. Макро-лейблы UI меняет после подтверждения применения. Переход должен иметь параметры ramp/crossfade либо другое тестированное anti-click поведение; хвосты старой цепи потребуют отдельного решения.
- **Repeated effects:** каждая instance имеет отдельные параметры и audio state; общий immutable preset definition можно разделять, mutable delay/filter buffers — нельзя.
- **Factory taxonomy:** Hyperpop / Digicore; Glitch / Digital Chaos; Ambient / Dreamy; Vintage / Lo-Fi; Vocal Processing; Rhythmic / Movement; Experimental / Extreme. Пресет может иметь несколько tags, категории не должны плодить копии definition.

Стартовые названия библиотеки остаются пользовательскими концептами, не реализованными пресетами: **Pink Meltdown, Glass Vocal, Dreamsick, Angel.exe, Broken CD, Liquid Motion, Chrome Tears, Digital Heartbeat, Tape Memories, Infinite Space**. Возможные многокатегорийные теги: Glass Vocal и Chrome Tears — Vocal; Dreamsick и Infinite Space — Ambient; Broken CD — Glitch/Experimental; Digital Heartbeat и Liquid Motion — Rhythmic/Movement; Tape Memories — Vintage; Pink Meltdown и Angel.exe — Hyperpop/Experimental. Это только каталогизация для будущей библиотеки, а не утверждение о звучании готовых FX configurations.

## 6. Шесть Macro Controls и модуляция

### Семантика

Каждый preset задаёт 6 динамически подписанных macro slots. Контрол может управлять несколькими параметрами нескольких узлов; UI оперирует нормализованным `0…1`, а native mapping преобразует значение в реальные диапазоны и кривые. В Basic показываются короткое имя, ясный звуковой результат и текущее значение. Например: `MOTION` / `rhythmic movement` / `42%`. Технический список назначений можно оставить в поясняющем popover, но не в постоянном основном виде.

Рекомендуемые исходные имена для пресетов, которые не задают свои: **Color, Space, Motion, Width, Texture, Blend**. Это fallback labels, а не закреплённые DSP функции. Их назначение зависит от mapping конкретного пресета; нельзя обещать, что каждый preset реализует все шесть одинаково.

Переход preset должен использовать авторские initial values или явно согласованную morph policy, не случайно сохранять позицию ручки с прежним значением при совершенно другой mapping. Предлагаемое поведение v1 — preset применяет заданные initialMacroValues, отображает их после native ACK и сглаживает изменение на DSP стороне.

### Визуальная концепция в Lithic

Сохранить тёплую минеральную поверхность, скошенную тёмную waveform plate и ограниченный медный спектр. Basic не копирует Advanced-layout с другим набором элементов: kit и macros образуют один игровой объект.

- Шесть ручек — главная интерактивная полоса, не набор из шести одинаковых карточек. На широком окне это одна выразительная линия из шести sculptural dial objects; на среднем — адаптивная полоса; на 620 px — 3×2 с сохранением крупной рабочей площади и порядка slots.
- Общий корпус — светлый керамико-минеральный диск/многогранная шайба с мягкими фасками и точной теневой кромкой. Позиция задаётся не стандартной единственной риской: выразительный медный сегмент/вставка и последовательность делений встроены в неодинаковую геометрию каждой ручки. Поверхности читаются как Lithic, не как ретро-панель.
- У шести control есть собственный рисунок индикации, который повторяет тип mapping (например, плотность/движение/пространство), но не шесть несвязанных цветов. Медь остаётся основным active accent, вторичные оттенки — приглушённые минеральные тона.
- Значение и единица находятся рядом с контролом и имеют более высокий контраст, чем технические ticks. Label читается сразу; mapping hint появляется при фокусе/наведении или в компактном пояснении.
- Изменение оживляет сам указатель/гравировку коротким направленным движением, без постоянного glow, декоративного шума и произвольных анимаций. На pointer drag используется pointer capture; клавиатура поддерживает стрелки, PageUp/PageDown, Home/End, а доступность реализует slider semantics и читаемое `aria-valuetext`.
- Клики, drag, фокус и изменение preset показывают pending/accepted/error состояния. React может держать только временное значение жеста; подтверждённые значения и mapping принадлежат C++.

Существующий `RotaryControl.tsx` — доступная техническая основа (SVG, pointer capture, keyboard, value/read-only states), но его стандартная круговая шкала и один индикатор не являются финальным макро-дизайном. Его можно использовать как поведенческий reference или расширить компонентом `MacroDial`; копировать Advanced seven-knob rendering без новой art direction не следует.

### Macro runtime и realtime safety

Предложение для первой FX версии: stable slots `macro_1…macro_6` на native side; preset меняет label/mappings, но не сам ID slot. Решение о публикации этих шести параметров как APVTS host parameters надо утвердить до реализации. Это полезно для automation и state, но создаёт требования к обратной совместимости host IDs и preset switching. Если сначала выбрать non-automatable runtime state, это следует сознательно зафиксировать.

Audio thread не может разбирать JSON, обращаться к WebView, выделять/освобождать память, брать blocking locks или строить FX chain. UI-команды валидируются и применяются на message/control thread; крупная подготовка chain идёт в worker; аудиопоток читает preallocated graph и получает сглаженные цели через lock-free/fixed-capacity передачу. Заменённые effect instances удаляются только после доказанного безопасного retirement вне audio callback. Никакие mutex из UI/control path не должны захватываться в `processBlock`.

Modulation v1 следует оставить вне основного scope. Последующие источники могут включать XY и синхронизируемый/свободный LFO; они должны писаться как отдельные route definitions с ограниченным depth и иметь DSP smoothing. React рисует их значения и XY location, но не выполняет modulation/sample processing.

## 7. FX Browser и композиция Basic Mode

### Рекомендуемая визуальная иерархия

1. **Тихий Source Header:** имя sample, import/change, краткая длительность; не конкурирует с инструментом.
2. **Kit Stage:** waveform/сегменты как отпечаток подготовленного материала плюс pad/trigger grid с номерами или музыкальными labels. Сцена объясняет «готовый набор», а не приглашает двигать границы. Активный и выбранный для просмотра pads различимы, но не конфликтуют с MIDI-полифонией.
3. **Preset identity:** выбранное имя, одна строка описания/категории и открываемая browser list. Не превращать весь экран в каталог карточек; список ориентирован на быстрый слуховой выбор по name/tags.
4. **Macro instrument:** шесть главных контролов, общий визуальный baseline и custom dial language.
5. **Performance/status strip:** MIDI/host readiness, анализ/kit status, sample transport если используется. Не выдавать sample playhead за текущий active slice.

На 1440×900 kit stage и macros остаются крупными основными полями. На 900×686 preset list может становиться выдвижной/свернутой панелью, но макросы и возможность запуска kit остаются видимыми. На 620 px kit отображается в 4×4 ячейки с перелистыванием для второй половины при kit >16; шесть macro controls идут 3×2, минимальный целевой диаметр активной зоны 60–68 CSS px, подпись и numeric readout не уменьшаются до технически нечитаемого уровня. Точные breakpoints проверить в WebView окне Standalone и VST3, а не вывести из браузерного mockup.

### Библиотека и каталогизация

Browser должен поддерживать поиск, фильтр категорий и быстрый preview информации о пресете. В первой library версии достаточно локальных factory definitions. Не включать неработающие сердца/favorites, облако или пользовательское сохранение до наличия соответствующего backend. Показывать, если preset меняет chain и macro meanings; переключение не должно оставлять старые названия до native ACK.

## 8. Граница Basic / Advanced

| Basic Mode | Advanced Mode |
|---|---|
| Auto analysis и automatic Slice Plan; пользователь не редактирует boundary | Waveform-first точный waveform, sample time ruler, transient inspection |
| Kit launch surface и музыкальные labels | Ручной Slice navigation, выбор, изменение границ и существующие Split/Delete/Reset capability |
| FX Preset Browser | Детальная работа с raw sample/slice; список эффектов/FX chain — отдельное будущее решение, не обязательный наследник Advanced slicer |
| Шесть preset-defined macros | Существующие Volume, Pan, Pitch, Attack, Decay, Sustain, Release выбранного слайса |
| Краткий sample и analysis status без технической перегрузки | Все доступные metadata, анализ и значения с соответствующим уровнем детализации |
| Реальное воспроизведение kit через MIDI или будущие безопасные UI pads | Host MIDI Slice playback; никакой подмены выбором Slice на аудиотракт |

Обе поверхности читают один `sampleId`, один `SliceEngine` и общую цепочку snapshot. Basic kit data и MIDI mapping живут в native model, а не создаются React при переключении вида. Пресет/макро — состояние FX chain плагина, а не значения слайса. Selection, hover, фокус и раскрытый browser остаются локальным UI state. Active voices не выводятся из выбранного pad; это разные состояния.

## 9. C++ / DSP / React / Bridge model

### Source of truth

- **C++ Processor:** sample lifecycle, master APVTS Volume, FX preset/macro state, import state.
- **AnalysisCoordinator:** analysis results и generation. Добавить Slice Plan pipeline или хорошо определённое worker extension.
- **SliceEngine:** единственная общая модель слайсов; auto plan применяется транзакционно на control thread.
- **SlicePlaybackEngine:** принимает snapshot и MIDI. Новая MIDI раскладка должна быть частью Slice DTO/snapshot и утверждённо сериализоваться.
- **FX Runtime:** новая C++ подсистема: effect registry, instance graph, parameters, preset validation, macro mapping, persistent state и bounded modulation.
- **Audio thread:** только уже подготовленные playback/FX structures и сглаженные значения; никаких React/JUCE WebView callbacks.
- **React:** presentation, preset browsing/filter, selected kit pad, фокус, hover и временный macro gesture draft. Не владеет результатом анализа, нарезкой, FX parameters или аудио.

### Bridge shape (предложение)

Сохранять protocol v2 enveloping, `requestId`, sampleId/sliceRevision/generation и stale checks. Новые DTO только после утверждения реализации:

```ts
type BasicPreparation = {
  status: 'idle' | 'analysing' | 'buildingKit' | 'ready' | 'fallback' | 'failed';
  strategy: 'transient' | 'grid' | 'phrase' | 'hybrid' | 'texture' | null;
  confidence: number | null;
  reason: string | null;
  generation: Revision | null;
};

type BasicSnapshot = {
  protocolVersion: 2;
  type: 'basicSnapshot';
  sequence: Revision;
  sample: SampleMetadataDto | null;
  sliceRevision: Revision | null;
  slices: SliceDto[];           // same authoritative slice collection as Advanced
  analysis: AnalysisDto | null;
  preparation: BasicPreparation;
  fx: { presetId: string | null; revision: Revision; macros: Array<{
    slot: 0 | 1 | 2 | 3 | 4 | 5; label: string; hint: string;
    normalizedValue: number; pending: boolean;
  }> };
};

type ApplyFxPreset = {
  protocolVersion: 2; requestId: RequestId; type: 'applyFxPreset';
  presetId: string; expectedFxRevision: Revision;
};

type SetMacro = {
  protocolVersion: 2; requestId: RequestId; type: 'setMacro';
  expectedFxRevision: Revision; slot: 0 | 1 | 2 | 3 | 4 | 5;
  normalizedValue: number;
};
```

The command examples are design proposals, not supported messages today. Auto slicing should normally be native-triggered from sample import; if re-generation later gets an explicit command, it carries sampleId, analysis generation and expected sliceRevision. FX responses should ACK the accepted revision and canonical macro values without resending waveform/slice collections on every drag. State changes should be evented or sampled at bounded cadence; waveform data remains a separate request.

### UI trigger gap

For MIDI host play, no new bridge command is needed. If product Basic must let users click pads in Standalone, add a typed `triggerSlice` request and note lifecycle handling (note-on/one-shot or explicit note-off), validated against current sampleId, SliceId and revision. The bridge must enqueue bounded commands that the audio thread can consume without allocation/lock; never call SliceVoice from the message thread. Define duplicate pad hits, note-off ownership, polyphony/voice stealing and sample replacement before implementing it. Until then, UI must not render clickable pads as if they produced sound.

## 10. Components to add or extend

| Component | State now | Required next |
|---|---|---|
| `AnalysisCoordinator` / analysis types | Worker, cancellation, BPM/Key/Transient results | Extend result/orchestration with confidence-aware Slice Plan generation and stale identity checks, or add bounded slicing stage to same worker pipeline |
| `SliceEngine` | Mutable message-thread model; single-slice setup; local structural operations | Atomic batch replacement/apply plan, plan validation, deterministic MIDI mapping, origin/strategy metadata, single revision increment |
| `SlicePlaybackSnapshot` / `SlicePlaybackEngine` | Immutable mapping; polyphonic MIDI voices; safe publication | Carry deterministic kit/note mapping and preserve no-allocation callback rules; UI pad trigger only if separately approved |
| `PluginProcessor` | Import, analysis startup, 30 Hz snapshot publishing, APVTS Master Volume | Auto-slicing lifecycle/status, commit validated plan, persistence of FX preset/macro state, eventual FX graph ownership |
| `PluginEditor.cpp` / UI-v2 protocol | Versioned sample/slice/analysis/waveform/transport and parameter paths | Basic snapshot DTO; preset catalog/apply; macro read/write; optional slice trigger; errors, revision and bounded update policy |
| New native FX runtime | Does not exist | Effect registry and validated linear chain with unique node instances; prepare/rebuild off audio thread; smoothed per-node params; safe swap and retirement |
| `web-ui/src-v2/app/App.tsx` | Lithic production Advanced screen; no mode state | Shared application shell with explicit Basic/Advanced route; avoid remounting/reinitializing C++ state |
| React Basic view | Does not exist | `BasicMode`, `KitStage`, `KitPadGrid`, `FxPresetBrowser`, `MacroBank`, preparation/empty/error states |
| `RotaryControl.tsx` | Accessible SVG knob, pointer/keyboard, single arc indicator | Develop separate premium `MacroDial` visual language; reuse verified keyboard/pointer semantics where suitable |
| Preset catalog/state | Does not exist | Versioned definitions, registry validation, factory catalog, plugin state persistence and later user preset workflow |
| CMake/resources | Bundles UI-v2 and current resources | Bundle factory definitions/FX code without new libraries; preserve old UI fallback and both plugin formats |

## 11. Risks and required validation

### Product and algorithm risks

- **False musical structure:** BPM or onset count alone can split a vocal phrase into phonemes. Evaluate each strategy with percussion, loop, sung/spoken phrases, melodic phrases, noisy sounds and ambient textures.
- **Poor fallback:** equal-sized slices look systematic but can cut at musically bad positions. Empty/weak analysis needs explicit coarse fallback and honest UI status.
- **Short slices / huge kits:** model minimum 64 frames is not a user-friendly floor; transient detector default event separation is 12 ms. Candidate merging and target count must be tuned against real samples.
- **MIDI collisions:** current ID-derived note assignment wraps after 68; more than one region can map to the same note. Auto kit needs deterministic explicit mapping before release.
- **Atomicity:** mutating SliceEngine one split at a time can expose partial state and publish multiple revisions. Add one validated transaction, one snapshot, one revision, tests for allocation/validation failure.
- **Realtime:** effect graph construction, buffer allocation and destruction must not leak into `processBlock`; snapshot/macro publication and retirement require stress tests under repeated preset switches and automation.
- **Preset jumps and tails:** different chains may radically change level/tail; define smoothing, dry/wet, clipping headroom, bypass and old-tail behavior before making macro feedback appear immediate.
- **State compatibility:** current APVTS state includes stable Master Volume. Adding macro APVTS IDs or a ValueTree branch must not rename/retype `volume` or break existing sessions.
- **WebView identity and stale work:** sample may change while analysis or preset load is in flight; all asynchronous results need sample/generation/revision guards.
- **Standalone trigger:** host MIDI is enough in many DAWs but not a self-contained click-to-play Standalone experience. Choose whether software pads are a release requirement before UI implementation.

### Suggested tests

- Auto-plan unit tests for empty, short, mono/stereo, sparse transients, dense transients, stable/ambiguous BPM, silence tails, gaps, low-confidence Key, high sample rates, event truncation and cancellation.
- Musical quality corpus review with manually annotated boundary targets for percussion, loops, vocal, melodic and ambient categories. Measure boundary tolerance and listen to every output; do not treat algorithm confidence as quality evidence alone.
- Plan validation tests: out-of-range, unsorted, overlapping, too short, too many slices, stale sampleId/generation/revision; rejected plan leaves previous list/revision/snapshot intact.
- Transaction tests for exact sample-domain boundaries, stable IDs, deterministic MIDI assignments, max kit size and same note collisions. After applying, verify the actual snapshot has the full committed set.
- Playback tests: each mapped MIDI note triggers only intended Slice; note-off/release, polyphony, sample replacement, active voice retirement and 16-voice limit.
- FX unit tests for definition schema, unknown nodes/params, duplicated/invalid node IDs, effect repetition with independent state, bounds/NaN/Infinity, macro curves, multi-target mapping and preset migration.
- DSP tests using deterministic signals: effect order, wet/dry, parameter smoothing, macro-to-effect path, clipping/headroom, tail behavior, mono/stereo, no change at neutral macro, bypass semantics.
- Realtime stress tests: repeated preset swaps, macro automation, simultaneous MIDI, callback allocation/lock instrumentation and safe reclamation. Test render at realistic block sizes and sample rates.
- Bridge/React tests for ACK/error contract, stale commands, preset switching, dynamic labels/ranges, pending value vs accepted value, failed preset application, WebView reopen and rapid macro drags.
- Manual Standalone and VST3/DAW validation: load/replace sample, prepare kit, play each pad/host note, change preset and macros while voices play, save/reopen state, host automation if approved, resize at 620/900/1440 widths and verify fallback.

## 12. Рекомендуемая последовательность последующих фаз

### UI-2.4B — Product contracts and slice-plan prototype

- Утвердить стратегии, лимиты kit, fallback semantics, MIDI mapping, trigger expectations и Basic/Advanced ownership.
- Создать независимые deterministic fixtures и инструментальный offline Slice Plan prototype/tests. Production engine и UI не менять до проверки boundary quality.
- Criteria: один и тот же input даёт один план; сценарии low-confidence имеют объяснимый fallback; качество прослушано на типовых материалах.

### UI-2.4C — Atomic automatic slicing in native model

- Добавить batch validation/commit API к существующему SliceEngine и generation-aware auto plan pipeline на worker; commit только message thread.
- Добавить deterministic MIDI mapping по kit order; не привязывать MIDI note к глобальному Slice ID.
- Criteria: старыми plans нельзя заменить новый sample; ошибка плана не меняет состояние; один commit создаёт один корректный revision/snapshot; playback каждой ноты не конфликтует.

### UI-2.4D — FX Engine foundation

- Утвердить первую линейную цепь и набор реально существующих/новых effect types, persistent state и audio ordering относительно Master Volume.
- Реализовать C++ effect registry, preallocated chain, safe swap/retirement, parameter smoothing и render tests без UI macros.
- Criteria: deterministic offline DSP render подтверждает порядок/значения; realtime stress проходит; мастер-уровень и старые sessions сохранены.

### UI-2.4E — FX Presets and macro runtime

- Ввести schema-versioned preset definitions, built-in catalog, unique FX node IDs и шесть stable macro slots с multi-target mapping/curves.
- Утвердить host automation решение; сохранить preset ID и значения в plugin state.
- Criteria: загрузка, возврат и миграция preset state; macros меняют только назначенные DSP параметры; ACK/state и audio output совпадают.

### UI-2.4F — Lithic Basic Mode exploration and acceptance

- Создать отдельный UI prototype для kit stage, preset browser и шести custom MacroDial; проверить Empty, preparing, ready, fallback и error на 620×663, 900×686, 1440×900.
- Визуально протестировать реальные автоматически полученные plans, а не только ручные mock boundaries. Получить пользовательское approval до production переноса.

### UI-2.4G — Production Basic UI and trigger integration

- Подключить typed Basic snapshot, auto preparation states, preset browser и macros к подтверждённым native capabilities; только если принято — добавить lock-free pad trigger path.
- Сохранить Lithic Advanced как второй режим и старый UI fallback до standalone/DAW acceptance.
- Criteria: один Sample/Slice state при переходе режимов, реальные notes запускают соответствующие slices, preset/macro DSP path audibly verified, state restore и error recovery проходят.

### UI-2.4H — Release acceptance

- Corpus review, manual audio checks, Standalone/VST3, host automation decision, state restoration, realtime instrumentation, WebView lifecycle и responsive checks.
- Только после этих результатов принимать Basic Mode как коммерчески готовый режим.

### Dependency graph

```text
2.4B (plan quality + product rules)
  → 2.4C (atomic native kit state + MIDI map)
  → 2.4F (Basic interaction prototype can use accepted plans)

2.4D (FX runtime)
  → 2.4E (preset schema + macros)
  → 2.4F (macro visual acceptance)

2.4C + 2.4E + 2.4F
  → 2.4G (production UI/optional pad bridge)
  → 2.4H (Standalone + DAW release acceptance)
```

Не следует начинать UI-v2 integration до того, как завершены slice-plan contract и FX runtime/preset semantics. Иначе интерфейс будет визуально обещать поведение, которое ещё не подтверждено DSP.

## Честная оценка готовности

- **Automatic Musical Slicing:** средне-высокая сложность. Анализаторы дают хорошие низкоуровневые признаки, но музыкальное качество границ — новая задача с алгоритмической работой, слуховой проверкой и реальными корпусами. Существующий SliceEngine помогает как конечная модель, но требует atomic plan apply и корректной ното-раскладки.
- **FX Presets/Macros:** высокая сложность и фактически новый DSP subsystem. Нет готового FX Engine, поэтому UI библиотека и красивые ручки не дают продуктовой ценности без цепи, state, smoothing и realtime-safe graph lifecycle.
- **Basic UI:** сама React поверхность умеренно сложна после контрактов, но clickable kit needs trigger path; host MIDI alone does not make UI pad interaction possible.
- **Production readiness:** Basic Mode не готов к реализации одной UI-only фазой. Сначала требуется довести и проверить native slice-plan semantics, MIDI mapping, FX runtime и preset schema. Lithic остается основным языком, но Basic должен иметь собственную игровую композицию и custom macro controls, а не копию Advanced screen.
