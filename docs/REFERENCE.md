# Справка gloview

Вынесено из `AGENTS.md`, чтобы то, что читают перед каждой правкой, осталось
правилами, а не справочником.

| Документ | Что внутри |
|---|---|
| `../AGENTS.md` | правила, архитектура, чеклист. Читать перед изменением |
| `manual-test-cases.md` | 44 ручных кейса, 12 помечены `·часто·` |
| `feat-stash-secret.md` | что делает фича «стеллаж» |

---

# 1. Карта исходников

## Ядро

| Файл | Ответственность |
|---|---|
| `main.cpp` | регистрация: конфиг-таблицы, единый реестр действий `kActions` (таблица данных, а не ветвление диспетчеров; из одной строки регистрируются dispatch / hyprctl / Lua) |
| `overview.hpp` | единственный владелец состояния; объявления всех подсистем и всей отрисовки |
| `layout.hpp` / `.cpp` | чистая геометрия без Hyprland — эталон юнит-тестируемого домена |
| `render/geometry.hpp` | геометрия, которая считает боксы и ничего не рисует. Без `cfg::`: всё, что нужно, — аргумент |

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

| Ключ | Строка | Тип | Дефолт | Смысл |
|---|---|---|---|---|
| `blur` | 177 | float | `1.0` | сила блюра, 0 = выкл |
| `blur_passes` | 131 | int 1..16 | `3` | итераций гауссиана на дне |
| `blur_size` | 132 | int 1..200 | `8` | радиус в пикселях экрана |
| `blur_resolution` | 133 | int 1..32 | `4` | глубина пирамиды |
| `backdrop_color` | 219 | colour | `0x73070a10` | dim, рисуется отдельным rect'ом |

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
| `ANIM\|` | `begin`, `end`, `reject` | `beginSwapFX`, `animStart` (13 мест) |
| `LIFE\|` | `park`, `promote`, `abort`, `close`, `requestDeactivate`, `deactivate`, `open` | `Lifecycle` |
| `DRAG\|` | `release`, `branch` | `input/mouse.cpp` |
| `SCROLL\|` | `setMax`, `glide`, `snap` | `ScrollState` |
| `SWAP\|` | `decide` | `swapWindows` |
| `CMD\|` | `ok`, `err` | шов `cmd()` |
| `STATE\|` | — | команда `state` |

`DRAG|` — единственное семейство, **зависящее от человека**: его нельзя
произвести командой, шов ставит состояние, а не двигает мышь.

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
```

`SWAP|decide` — самая полезная запись при разборе свопа: она говорит **почему**
обмен слотами сработал или нет, с обоими боксами. Без неё пришлось бы гадать.

`seq` в `ANIM|` уникален в пределах процесса и связывает `begin` с `end`.

## Ключи

`debug_trace` — **отдельный** от `debug_logs`. Иначе матрица забьётся покадровым
спамом. Включается либо в Lua-конфиге, либо на лету:

```bash
hyprctl eval 'hl.plugin.gloview.cmd("set debug_trace 1")'
```

Внимание: **`debug_trace` сбрасывается при каждом reload** — это новый инстанс
плагина. После `cmake --build build --target reload` включай заново.

## Где лог

`/tmp/gloview.log`. Единственный канал успеха команды: `hyprctl eval` не печатает
возвращаемое значение, поэтому читать надо `CMD|ok` / `CMD|err`.

---

# 4. Ключи анимаций

Реестр в `src/anim.cpp`, `kRows`. Длительности читаются на месте **каждый кадр**,
поэтому правка конфига видна сразу.

| leaf | мс | кривая | стиль | что двигает |
|---|---|---|---|---|
| `open` | | | | открытие овервью |
| `close` | | | | закрытие |
| `glide` | | | | глид плиток `natural → target` |
| `appear` | | | | вход плиток |
| `card` | | | | новая карточка |
| `pulse` | | | | пульс |
| `strip_step` | 300 | easeout | | шаг скролла полосы |
| `ws_enter` | 250 | easeout | `slide` | вход при смене воркспейса |
| `ws_exit` | 250 | easeout | `slide` | уход |
| `expo_in` / `expo_out` | 250 | easeout | | флип all↔one |
| `jump_in` / `jump_out` | 180 | easeout | | digit-jump |
| `swap_main` | | | | ведущая карточка свопа |
| `swap_partner` | | | | вторая карточка |
| `drop` | | | | снос |
| `swap_fly` | | | | полёт после дропа |
| `lift` | | | | подъём при перетаскивании |
| `carousel_step` | 300 | easeout | | шаг карусели |
| `new_card` | 180 | easeout | | масштаб новой карточки |

Стили (`ws_enter_style`, `ws_exit_style`): `slide` \| `slidevert` \| `fade` \| `pop`.
`pop` масштабирует, остальные возвращаются раньше ветки scale-in.

Стили свопа (`grid_swap_style`, `strip_swap_style`): `horizontal` \| `slidevert`
\| `fade` \| `pop`. `horizontal` — дефолт и **единственный, что раньше не
анимировался**; теперь все стили стартуют полёт.

`jump_style` добавляет `instant`.

---

# 5. Приоритет настроек

Снизу вверх, последний выигрывает:

```
config-значение  →  set (команда)  →  rule из gloview.animation  →  строка kRows
```

**Rule бьёт config.** `set` перекрывает всё, но `setTuning` при этом гасит
перекрываемые поля rule — иначе `speed=1.2` из конфига делил бы запрошенные 320.

Ключи пишутся **плоско**: `set layout columns`, а не `set grid layout columns`.
Поле зовётся `grid.layout`, регистрируется как `plugin:gloview:layout`.