# Справка hyprview

Вынесено из `AGENTS.md`, чтобы то, что читают перед каждой правкой, осталось
правилами, а не справочником.

| Документ | Что внутри |
|---|---|
| `../AGENTS.md` | правила, архитектура, чеклист. Читать перед изменением |
| `manual-test-cases.md` | 46 ручных кейсов, 12 помечены `·часто·` |
| `ROADMAP.md` | что сделано и что дальше |

---

# 1. Карта исходников

## Ядро

| Файл | Ответственность |
|---|---|
| `main.cpp` | регистрация: конфиг-таблицы, единый реестр действий `kActions` (таблица данных, а не ветвление диспетчеров; из одной строки регистрируются dispatch / hyprctl / Lua) |
| `overview.hpp` | единственный владелец состояния; объявления всех подсистем и всей отрисовки |
| `layout.hpp` / `.cpp` | чистая геометрия без Hyprland — эталон юнит-тестируемого домена |
| `render/geometry.hpp` | решения рендера, чистые: считают боксы и ничего не рисуют. Без `cfg::`: всё, что нужно, — аргумент. Капсула — `captionLayout`, выход из-под карточки — `pushOut`, отступ полосы — `stripPush`, полёт свопа — `swapFlight`, база бэкдропа — `backdropBase`, радиусы — `clampRound`/`contentRadiusPx`, глиф «✕» — `glyphRect` |

## Подсистемы (`src/model/`, без Hyprland в заголовках)

| Файл | Что держит | Тест |
|---|---|---|
| `scene.hpp` | плитки, выбор, интенты; `tileAtRest`, `swapTiles`, `armAppear` | `recency`, `carousel_*` |
| `strip.hpp` | карточки стрипа | `carousel_layout` |
| `recency.*` | порядок фокуса, `promote` | `recency` |
| `drag.hpp` | **ФСМ перетаскивания.** Шаблон на тип-ручку | `drag` |
| `lifecycle.hpp` | **ФСМ владения экраном:** `Own::{Closed,Parked,Screen}` + флаг входа | `lifecycle` |
| `scroll.hpp` | позиции скролла стрипа, инвариант `[0,max]` | `scroll` |
| `effects.hpp` | ghosts, pulses, swapfx | — |
| `view/state.hpp` | режимы: `run` / `desktop` / `allOverride` / `wsSlideDir` / `expoFlip` | — |

## Анимации и конфиг

| Файл | Ответственность |
|---|---|
| `anim.hpp` / `anim.cpp` | **реестр анимаций:** `enum Id`, `kRows`, дефолты, `setTuning`, Lua-правила. `config.*` о нём не знает |
| `clock/clock.cpp`, `clocks.cpp` | домен времени: tween, прогрессы, resolved anim cfg |
| `clock/curves.cpp` | реестр кривых, Lua C API (**внутри `extern "C"`**) |
| `config/config.hpp` / `.cpp` | типизированная схема + `kInts`/`kFloats`/`kStrs`/`kColors`; `cfg::override` |

## Рендер

| Файл | Ответственность |
|---|---|
| `render/gl_util.hpp` | **единственный путь к композитору.** `safeRenderRect`, `drawable()`, `dropShadow`, `strokeRing`/`strokeRingPx` |
| `render/painter.cpp` | `renderStage`, порядок проходов, `paint()` |
| `render/backdrop.cpp` | источники бэкдропа, `primeBackdrop()`, парковка |
| `render/tile_view.cpp`, `carousel_view.cpp`, `strip_view.cpp` | поверхности: карточка / ряд / полоса |
| `render/fx.cpp` | `renderSwapFX`, `swapDests`, `pushOut`, геометрия драга |
| `render/window_content.cpp` | live-поверхность окна |
| `blur.hpp` / `blur.cpp` | dual-Kawase + gaussian, все шейдеры GLSL-литералами здесь |
| `pixel/cache.cpp` | кэш подписей и снапшотов |

## Ввод и действия

| Файл | Ответственность |
|---|---|
| `input/mouse.cpp` | **единственный писатель `Drag`**, стрип, скролл |
| `input/keys.cpp` | клавиатура, alt-tab, digit-jump |
| `actions/actions.cpp` | `swapWindows`, `landAfterSwap`, `switchToWorkspace`, реестр команд `cmd()` |
| `build/build.cpp` | `buildTiles` / `buildStrip` / `layoutTiles` / `replayReflow` / `startTileGlide` / `captureCurrentBoxes` |
| `session/hooks.cpp` | демонтаж и `initialize()`. Правится на смене ABI Hyprland |
| `session/geometry.cpp` | геометрия стрипа, blur-акцессоры, forced-visibility |
| `session/lifecycle.cpp` | `open`/`close`/`hardClose`/`toggle`/`deactivate` |

## Прочее

| Файл | Ответственность |
|---|---|
| `debug/log.{hpp,cpp}` | трейс: `ANIM\|` `LIFE\|` `DRAG\|` `SCROLL\|` `SWAP\|` |
| `cursor.{hpp,cpp}` | HW/SW курсор поверх овервью (следствие `RENDER_LAST_MOMENT`) |
| `tools/anim_matrix.py` | прогон анимаций по сценариям через шов |
| `tools/runtime_check.py` | проверки, которые требуют живого композитора |
| `tools/sync_upstream.sh` | клонирование исходников Hyprland/aquamarine на ревизиях живой сессии |

---

# 2. Блюр-пайплайн

Самодостаточный GL-конвейер, минующий `decoration:blur:*`. Dual-Kawase пирамида +
сепарабельный 9-tap gaussian на её дне. Все шейдеры — GLSL raw-литералы `R"#(`
в `src/blur.cpp`, отдельных `.glsl` нет.

```
Источник (W x H)
  └─ [1] DOWN-CHAIN, реальный 2x (blur.cpp)
       уровень 1: W/2 x H/2 … уровень N = m_fbA;  m_levels == 0 → прямой блит
  └─ [2] GAUSSIAN на дне, m_passes раз
       H: m_fbA → m_fbB,  V: m_fbB → m_fbA
  └─ [3] UP-CHAIN, переиспользует буферы down-chain
```

- **Радиус измеряется в тексерах нижнего буфера**: `size * strength / 2^levels`,
  а не сырое значение конфига.
- `resolution` — **первичный** регулятор силы размытия, не только anti-aliasing.
  Глубина = `clamp(log2(resolution), 0, kMaxLevels=5)`.
- Blend-state полностью захватывается и восстанавливается в `CBlurFilter::render()`
  — иначе портится последующий рендер.
- Вызов требует активного мониторного рендера (`m_renderData.pMonitor`) и
  проекции `RPT_EXPORT`; фильтр переключает viewport/fbSize на промежуточных FBO
  и оставляет финальный `(W,H)`.
- **Блюр не анимируется сам.** Альфа едет на единственных часах `m_progress` через
  `blurAlpha()`. Отдельного блюр-твина нет — fades сам блит.
- Dim в фильтре отсутствует: `renderBackdrop()` рисует `backdrop_color` отдельным
  rect'ом, поэтому кэш переиспользуем, а fade живёт в одном месте.

## Ключи блюра

| Ключ | Тип | Дефолт | Смысл |
|---|---|---|---|
| `blur` | float | `1.0` | сила блюра, 0 = выкл |
| `blur_passes` | int 1..16 | `3` | итераций гауссиана на дне |
| `blur_size` | int 1..200 | `8` | радиус в пикселях экрана |
| `blur_resolution` | int 1..32 | `4` | глубина пирамиды |
| `backdrop_color` | colour | `0x73070a10` | dim, рисуется отдельным rect'ом |

---

# 3. Формат трейса

Одна грамматика на все семейства, чтобы один парсер читал всё:

```
<ТЕГ>|<событие>|key=value|key=value|...
```

Значения **не содержат** пробелов и `|`. Каждый сегмент после первых двух —
`key=value` с непустым ключом и ровно одним `=`. Это проверяется
`scan_anim_coverage` и тестом `drag`/`lifecycle`.

## Семейства

| Тег | События | Источник |
|---|---|---|
| `ANIM\|` | `begin`, `end`, `reject` | `beginSwapFX`, `animStart` |
| `LIFE\|` | `park`, `promote`, `abort`, `close`, `requestDeactivate`, `deactivate`, `open` | `Lifecycle` |
| `DRAG\|` | `release`, `branch` | `input/mouse.cpp` |
| `SCROLL\|` | `setMax`, `glide`, `snap` | `ScrollState` |
| `SWAP\|` | `decide` | `swapWindows` |
| `CMD\|` | `ok`, `err` | шов `cmd()` |
| `STATE\|` | — | команда `state` |
| `TABS\|` | `load`, `match`, `activate` | `buildTiles`, `activateTab` |
| `GL\|` | `at=paint`, `leak` | `state_fence.hpp` |
| `RING\|` | `kind=<...>` | `log.cpp` |
| `CURSOR\|` | `mode=<...>` | `cursor.cpp` |
| `CURSORMV\|` | `at=<open\|closed>` | `input/mouse.cpp` |
| `DOMAIN\|` | `to=<windows\|tabs\|both>` | `lifecycle.cpp` |
| `TILEBOX\|` | `i=<n>` | `clock/placement.cpp` |
| `TRACE\|` | `gate=<n>` | команда `trace` |

Пять семейств гейтятся маской (биты ниже). Остальные теги пишутся всегда,
пока `debug_trace` не ноль.

`DRAG|` и `SWAP|` не производятся командой: шов ставит состояние, а не двигает
мышь. Автоматических проверок нет — всегда-SKIP хуже отсутствующей.

## Что читает трейс

| Семейство | Чем читается |
|---|---|
| `ANIM\|` | `tools/anim_matrix.py` — замеряет фактическую длительность против запрошенной |
| `LIFE\|` | `tools/runtime_check.py` — порядок парковки и владения экраном |
| `SCROLL\|` | `tools/runtime_check.py` — клампы при пересборке полосы |
| `DRAG\|`, `SWAP\|` | человеком; автоматике недоступно (см. выше) |

## Маска и файл

`debug_trace` — **маска, не флаг**. `0` выключено, `1` включает всё, иначе —
побитовое ИЛИ:

| Бит | Семейство |
|---|---|
| 1 | `ANIM\|` |
| 2 | `DRAG\|` |
| 4 | `LIFE\|` |
| 8 | `SCROLL\|` |
| 16 | `SWAP\|` |

`CMD|`, `TUNING|` и `STATE|` маской **не глушатся**: это канал, по которому
узнают, что команда вообще что-то сделала. Заглушив его фильтром анимаций,
остаётся без сигнала, что трассировка вообще включена.

`cmd("trace")` печатает строку `TRACE|` с путём, размером файла и тем, какие
семейства сейчас выбраны.

Файл ротируется **по размеру** (8 МБ), не по старту сессии: обрезать при
загрузке — значит выбросить ровно тот лог, который нужен, когда что-то сломалось.
Держится три поколения: `hyprview.log.1` — предыдущая сессия.

## Виды записей

```
ANIM|begin|seq=<n>|op=<...>|leaf=<...>|style=<...>|ms=<n>|curve=<...>
     [|win=<class>|surface=<grid|strip>|from=<x,y,w,h>|to=<x,y,w,h>]
ANIM|end|seq=<n>|ms=<measured>
ANIM|reject|seq=<n>|why=<...>

LIFE|<event>|own=<closed|parked|screen>|opening=<0|1>[|parkTicks=<n>]
     [|deactivating=1][|jumpClosing=1]

DRAG|release|press=<tile|stripwin|consumed|empty>|button=<n>
     [|idx=<n>][|winIdx=<n>]|lifted=<0|1>|x=<..>|y=<..>|travelSq=<..>|rmb=<0|1>|win=<..>
DRAG|branch|name=<Tile->stripCard|StripWin->slotSwap|...>[|rmb=<n>|kind=<n>|idx=<n>]

SCROLL|<event>|scrollable=<0|1>|max=<d>|from=<d>|target=<d>|current=<d>

SWAP|decide|ia=<n>|ib=<n>|restA=<0|1>|restB=<0|1>
     |natA=<x,y>|tgtA=<x,y>|natB=<x,y>|tgtB=<x,y>|slots=<0|1>

GL|at=paint|blend=<0|1>|src=<n>|dst=<n>|scissorTest=<0|1>
     |scissor=<x,y,w,h>|fbo=<n>
GL|leak|at=<where>|what=<blend|blend_func|scissor_test|scissor_box|fbo>[,...]
     |blend=<0|1->0|1>|scissorTest=<0|1->0|1>|scissor=<..>-><..>|fbo=<n>-><n>
```

`GL|leak` — состояние GL, которое проход оставил изменённым: `paint()` берёт
отсчёт на входе и сравнивает на выходе (`glStateReport`, `state_fence.hpp`). Молчит,
когда всё восстановлено: запись = утечка. Читается только при `debug_trace` —
`glGet*` синхронизирует пайплайн, каждый кадр ради редкого кадра не оправдан.

`GL|at=paint` — чтение на входе в `paint()`, по кадру. `GL|leak` — что вернулось
на выходе, с числами до и после: имя поля говорит, что искать.

**Граница покрытия.** Fence обрамляет `paint()` и видит каждый кадр. Но композит
бэкдропа кэшируется, пока источник сменился, и переживает close/open — блюр-подпуть,
где save/restore и ломается, выполняется на первой сборке. Ни reopen, ни флип
воркспейса, ни `allws` его не форсируют: PASS этого чека не значит «блюр
восстановился корректно».

Читается автоматикой: `python3 tools/runtime_check.py gl`. Пусто = чисто.

`SWAP|decide` — почему обмен слотами сработал или нет, с обоими боксами.

`seq` в `ANIM|` уникален в пределах процесса и связывает `begin` с `end`.

## Ключи

`debug_trace` — **отдельный** от `debug_logs`. Иначе матрица забьётся покадровым
спамом. Включается либо в Lua-конфиге, либо на лету:

```bash
hyprctl eval 'hl.plugin.hyprview.cmd("set debug_trace 1")'
```

Внимание: **`debug_trace` сбрасывается при каждом reload** — это новый инстанс
плагина. После `cmake --build build --target reload` включай заново.

## Где лог

`/tmp/hyprview.log`. Единственный канал успеха команды: `hyprctl eval` не печатает
возвращаемое значение, поэтому читать надо `CMD|ok` / `CMD|err`.

---

# 4. Ключи анимаций

Реестр в `src/anim.cpp`, `kRows`; листья, базовые длительности, кривые и стили —
таблица в `README.md`. Здесь только правила:

- Длительности читаются на месте **каждый кадр**, поэтому правка конфига видна сразу.
- Каждый лист читает `<leaf>_enabled`, `<leaf>_ms`, `<leaf>_speed`, `<leaf>_curve`;
  у листьев со стилем есть `<leaf>_style`.
- Стили (`ws_enter_style`, `ws_exit_style`): `slide` | `slidevert` | `fade` | `pop`.
  `slidevert` (и `slide-vertical`) — вертикальная ось.
- Стили свопа (`grid_swap_style`, `strip_swap_style`): тот же набор плюс
  `horizontal` (алиас `slide`); дефолт — `horizontal`.
- `jump_style` добавляет `instant`.

# 5. Приоритет настроек

Снизу вверх, последний выигрывает:

```
config-значение  →  set (команда)  →  rule из hyprview.animation  →  строка kRows
```

**Rule бьёт config.** `set` перекрывает всё, но `setTuning` при этом гасит
перекрываемые поля rule — иначе `speed=1.2` из конфига делил бы запрошенные 320.

Ключи пишутся **плоско**: `set layout columns`, а не `set grid layout columns`.
Поле зовётся `grid.layout`, регистрируется как `plugin:hyprview:layout`.

---

# 6. Браузерная сторона

Два файла в `$XDG_RUNTIME_DIR/hyprview`, оба текстовые: расширение не умеет писать
файлы, поэтому пишет медиатор, а читает плагин.

## Корм (`tabs`)

```
g <generation>              соединение; id вкладок переиспользуются, поэтому id значит
                            что-то только под своим поколением
p <pid> <window>            процесс браузера для окна -- ключ сопоставления (§2.19)
t <window> <tab> <flags> <title...>
```

`flags` — число: бит 0 «активна», бит 1 «выгружена» (`discarded`). Заголовок — остаток
строки, поэтому флаг обязан быть числом, а не словом.

## Запрос (`activate`)

```
a <pid> <window> <tab>
```

Пишет плагин в момент активации карточки вкладки, целиком и через `rename`. Читает
медиатор, у которого родитель — процесс `pid`: у каждого браузера свои id вкладок, и
только он может открыть свою. Запрос старше нескольких секунд отбрасывается: это
состояние, в котором уже никого нет. Медиатор отдаёт расширению
`{name: "activate", window, tab}` вне очереди — native messaging двусторонний.
Расширение делает вкладку активной (выгруженная этим перезагружается) и фокусирует
окно.

По умолчанию карточку получают только загруженные вкладки. Ключ
`show_discarded_tabs` (и команда `discarded`) добавляет выгруженные; окно браузера,
у которого не осталось ни одной видимой вкладки, показывается карточкой окна.

## Превью вкладки

`previews/<tabId>.png` — картинка карточки вкладки: скриншот того, что вкладка показывала,
когда была видимой. Расширение снимает **только видимую** вкладку окна: `tabs.captureTab` в
WebExtensions не существует, `captureVisibleTab` фотографирует одну вкладку на окно. Но
вкладка видима в момент открытия, поэтому снимок есть у каждой. Снимок ужимается до 960 px
по ширине — карточка маленькая, а нативное сообщение ограничено по размеру.

Хранилище браузера (`<localProfileDir>/thumbnails/<md5(url)>.png`, `PageThumbsStorageService`)
не используется: там картинка в 1/4 масштаба, и на большой карточке это пиксели. Вкладка без
снимка показывает иконку. Кэш картинок чистится на каждом закрытии овервью, иначе первый
снимок жил бы до смены поколения.

## Режим all-workspaces в браузерном виде

Вкладки принадлежат браузеру, а не воркспейсу: вид `tabs`/`both` покрывает все
воркспейсы, иначе браузер на соседнем столе не виден. Вход в этот вид включает
all-workspaces, выход возвращает то значение, что было до входа (из 1-ws обратно в
1-ws). Хранит его `m_allForced` в `Overview`; сбрасывается в `deactivate()` вместе с
`setAllOverride(-1)`. Проверяет `runtime_check.py browser`. `syncTiles` в этом виде не
считает окна: плитки — вкладки, и сверка с числом окон пересобирала бы сцену каждый
кадр.

