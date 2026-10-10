# AGENTS.md — правила разработки gloview

Прочитай перед изменениями. Всё, что раньше лежало в трёх файлах, здесь.

## Правила

1. **Плагин = `.so` в адресном пространстве Hyprland.** Любой краш, use-after-free
   или RASSERT внутри плагина роняет ВСЮ сессию пользователя. Рискованные изменения
   (хуки, порядок проходов, teardown) проверяй в nested Hyprland.
2. **Hot-reload:** `cmake --build build --target reload` — сам делает безопасный
   teardown (`hyprctl gloviewunload`, синхронный hardClose), выгружает обе копии
   (build + hyprpm-кэш) и грузит свежую. Не грузи `plugin load` вручную, пока
   загружена hyprpm-копия: два инстанса не могут поставить hook на
   `shouldRenderWindow` дважды — второй молча останется без хука.
   **Перед любым `git checkout` проверь, что станет с файлами вне git:** tracked
   на одной ветке + gitignored на другой = checkout их УДАЛИТ без предупреждения.
3. **`-fno-gnu-unique` в CMakeLists не удалять.** Без него static-init guard'ы
   становятся STB_GNU_UNIQUE, glibc помечает .so NODELETE, `dlclose` не выгружает,
   и hot-reload молча гоняет СТАРЫЙ код.
4. **Хуки ставятся по demangled-signature substring** (`shouldRenderWindow`,
   `damageSurface`, `CSeatManager::setPointerFocus/setKeyboardFocus`). Ломаются при
   смене ABI Hyprland (уже ломались на 0.56 — namespace move). Несовпадение =
   `findFunctionsByName` ничего не находит → `initialize()` возвращает false →
   `plugin.load` глотает ошибку → выглядит как «конфиг не подхватился». После
   обновления Hyprland: `ctest -R hook_signatures`.
5. **currentFB нельзя использовать как источник блюра/полос:** фуллскрин-окно на
   активном воркспейсе идёт через «solitary client» fast path в обход
   `shouldRenderWindow`. Источники — только `backdropSource()` /
   `renderBackdropSource()`.
6. **Координаты:** весь chrome пишется в monitor-local LOGICAL пикселях; перед
   любым immediate GL вызовом — через `pxb()/pxr()` (масштаб + `.round()`,
   round обязателен: pixman-регион renderBorder'а trunc'ает дробные боксы → дырки
   в тонких рамках). Live-поверхности окон конвертируют себя сами.
   Всё, что рисует в приватный FBO, требует `RPT_EXPORT` + `fbSize=(W,H)`
   с сохранением/восстановлением окружающего состояния.
7. **Конфиг читается только через `cfg*`-хелперы** (V2 `value()`), НЕ через
   deprecated `HyprlandAPI::getConfigValue()` — последний игнорирует Lua-конфиг.
8. **Порядок `paint()` менять осознанно** (`render/painter.cpp:217`): backdrop →
   tiles/carousel → ghosts → pulses → strip → strip windows → strip buttons →
   pulses → swapfx → drag → aboveLayers → cursor. Правило: сидящее ПОВЕРХ контента
   рисуется ПОСЛЕ него (кнопки — после своих поверхностей, иначе opaque-контент
   окон затирает их). Два осознанных исключения: счётчик карусели рисуется ДО
   карточек, `pushOut()` двигает хром из paint.
9. **`m_pendingDeactivate`:** не деактивировать mid-frame — должен отрисоваться
   финальный кадр оверлея, иначе один прозрачный кадр (flicker).
10. **Владение вводом ≠ скрытие окон.** `shouldRenderWindow` прячет окна только от
    рендерера; фокус им раздаёт `CSeatManager`, и через `Event::bus()` это не
    отменяется. Пока овервью владеет экраном (включая паркованные кадры —
    `openedOrParked()`, не `active()`), хуки `setPointerFocus`/`setKeyboardFocus`
    не дают фокусу уйти в окна. Хук на `send*` бесполезен: seat продолжает считать
    окно сфокусированным и маршрутизировать в него.
11. **Заголовки Lua (`<lua.h>`, `<lauxlib.h>`) — всегда внутри `extern "C"`.**
    Без него C-функции манглятся в C++-имена и `dlopen` падает с
    `undefined symbol: _Z14luaL_checktype...` — ДО `PLUGIN_INIT`, то есть выглядит
    как «тихая ошибка конфига». Стоило суток. Файлы с Lua C API: `src/anim.cpp`,
    `src/clock/curves.cpp`.
12. **`const double x = cfg::…` в namespace scope — нельзя.** Инициализатор
    выполняется до `PLUGIN_INIT`, конфиг ещё не прочитан.
13. **`hl.config{…}` в пользовательском конфиге — только под `pcall`.** Без него
    ошибка одного ключа обрушивает загрузку конфига, Hyprland выходит молча, без
    падения и без отчёта. `hl.plugin.load` тоже под pcall — он глотает ошибку
    загрузки .so, и это выглядит как «конфиг не подхватился».

## Минные поля (семантика, которую легко сломать)

- `close()`: resume закрытия — `m_timeline.seek(1.0 - m_progress)`; при закрытии
  `m_progress = 1 - timeline.raw()` (инверсия направления!). Менять вместе.
- Кэш блюра — ЧИСТЫЙ блюр без dim и без анимации: dim рисуется отдельным rect'ом
  поверх блита, fade — только альфа блита `k=eased()`. `blur_strength` означает
  ТОЛЬКО радиус фильтра. Ключ кэша = srcId + passes/size/resolution/strength,
  и он проверяется КАЖДЫЙ кадр вне dirty-guard — единственный способ поймать смену
  источника (обои/видео/окно). В dirty-guard кэш не инвалидируется никогда.
- **Бэкдроп-композит НЕ протухает.** Раньше был TTL в 3 секунды + `hideLayers()` в
  `open()` — это и давало чёрный первый кадр: композит истекал и пересобирался из
  текстуры обоев, которая в этот момент пересоздавалась (то есть была чёрной).
  Теперь живёт, пока источник реально не сменится; неудачная попытка уходит в
  scratch-Framebuffer и не может затереть удачную.
- Границы перехода (политика, утверждена пользователем): реальные окна
  переключаются ЖЁСТКО — скрыты с первого кадра open, восстановлены только
  после полного окончания close-анимации. НИКАКИХ фейдов окна: во время глайда
  окно меняет размер и позицию, тающий оригинал под плывущим превью читается
  как задвоение. Бесшовность дают превью, попиксельно идентичные реальным
  окнам в natural-боксах на обоих концах. Фейд — только у блюра (кроссфейд
  бэкдропа). Превью не наследуют WINDOW_ALPHA_FADE в windowRealAlpha.
- Tween: длительности читаются на месте КАЖДЫЙ кадр (живой отклик на смену конфига),
  Tween владеет только стартовой точкой.

## Блюр-пайплайн

Самодостаточный GL-конвейер, минующий `decoration:blur:*`. Dual-Kawase пирамида +
сепарабельный 9-tap gaussian на её дне. Все шейдеры — GLSL raw-литералы `R"#(` в
`src/blur.cpp`, отдельных `.glsl` нет.

```
Источник (W x H)
  └─ [1] DOWN-CHAIN, реальный 2x (blur.cpp:379-393)
       уровень 1: W/2 x H/2 … уровень N = m_fbA;  m_levels == 0 → прямой блит
  └─ [2] GAUSSIAN на дне (blur.cpp:395-398), m_passes раз
       H: m_fbA → m_fbB,  V: m_fbB → m_fbA
  └─ [3] UP-CHAIN (blur.cpp:400-416), переиспользует буферы down-chain
```

- Dim в фильтре отсутствует (blur.cpp:419) — `renderBackdrop()` рисует
  `backdrop_color` отдельным rect'ом, поэтому кэш переиспользуем, а fade живёт
  в одном месте.
- **Радиус измеряется в текселях нижнего буфера** (blur.cpp:374-375):
  `size * strength / 2^levels`, а не сырое значение конфига.
- `resolution` — первичный регулятор силы размытия, не только anti-aliasing.
  Глубина = `clamp(log2(resolution), 0, kMaxLevels=5)` (blur.cpp:250).
- Blend-state полностью захватывается и восстанавливается в
  `CBlurFilter::render()` — иначе портит последующие draw-вызовы.
- Вызов требует активного мониторного рендера (`m_renderData.pMonitor`) и
  проекции `RPT_EXPORT`; фильтр переключает viewport/fbSize на промежуточных FBO
  и оставляет финальный viewport в `(W,H)`.
- Блюр **не анимируется сам**: его альфа едет на единственных часах `m_progress`
  через `blurAlpha()` (overview.hpp:210). Отдельного блюр-твина нет — fades сам
  блит.
- Открытие: овервью сначала *паркуется* (`m_pendingOpen`) — `paint()` зовёт
  `primeBackdrop()` и не рисует ничего, пока композит реально не готов (1-2
  паркованных кадра). Закрытие: тот же `blurAlpha()` гасит блит, без пересчёта.
  Reflow (drag-drop): `m_progress` закреплён на 1.0, блюр остаётся устоявшимся.

Конфиг (`src/config/config.cpp`):

| Ключ | Строка | Тип | Дефолт | Смысл |
|---|---|---|---|---|
| `blur` | 177 | float | `1.0` | сила блюра (0 = выкл) |
| `blur_passes` | 131 | int 1..16 | `3` | итераций гауссиана на дне |
| `blur_size` | 132 | int 1..200 | `8` | радиус в пикселях экрана |
| `blur_resolution` | 133 | int 1..32 | `4` | глубина пирамиды |
| `backdrop_color` | 219 | colour | `0x73070a10` | dim, рисуется отдельно |

## Сборка и проверка

```bash
cmake --build build -j$(nproc)          # цель build/gloview.so; зелёная сборка = минимум
cmake --build build --target reload     # пересобрать + горячо перезагрузить в живой Hyprland
cd build && ctest --output-on-failure
```

Правку `src/anim.cpp` / `src/clock/curves.cpp` (границы `extern "C"`) hot-reload
не подхватит — нужен перезапуск сессии: сбой на `dlopen`, а не в рантайме.

**Проверять сборку честно:** `cmake --build build` печатает `[100%] Built target gloview`
даже когда make под ним падает — exit-код 0, ошибки в stderr. Смотри на дату
`build/gloview.so`, а не на вывод. Инкрементальная сборка скрывает warning'и в
неизменённых TU.

**Падение без coredump — читай первым:**

```bash
ls -t ~/.cache/hyprland/hyprlandCrashReport*.txt | head -1        # точный стек
tail -100 /run/user/1000/hypr/$(ls -t /run/user/1000/hypr | head -1)/hyprland.log
```

`Hyprland --verify-config` как диагностика бесполезен: роняет композитор и БЕЗ
плагина, подсовывая чужие core-дампы. Не делай `rm -rf /run/user/1000/hypr/*` —
вместе с мусором уходят логи живых и упавших сессий.

**Имя функции в crash-отчёте может быть выдуманным.** Символизатор резолвит адрес
по symtab того `.so`, что лежит по пути — после hot-reload это часто старая
сборка. Сверяйся с `nm -DC build/gloview.so`, прежде чем искать функцию.

## Диагностика: гипотеза без красного цикла — не находка

Пять версий подряд (таймер, глобалы в статической инициализации, неизвестный ключ,
verify-config, правила анимаций) — все оказались неверными. Правильный порядок:
(1) прочитать crash-отчёт и лог композитора, (2) воспроизвести на живом оверлее,
(3) только потом строить версию.

Два прибора, которые соврали: `wf-recorder` не воспроизвёл чёрный кадр ни разу, а
детектор по `mean` ловил не кадр, а переход стол↔овервью (обои темнее блюра).
`glReadPixels` подчиняется scissor test — за пределами scissor-бокса не пишет
вовсе, поэтому любая проба обязана делать `glDisable(GL_SCISSOR_TEST)` и
возвращать состояние; буфер в 3 байта при GL_RGBA = переполнение стека = SIGABRT
= падение всей сессии. Мультисэмпловый FB даёт мусор.

## Карта исходников (`src/`)

| Файл | Ответственность |
|---|---|
| `overview.hpp` | класс Overview, состояние сессии, единственный владелец `m_active` |
| `main.cpp` | регистрация: конфиг-таблицы, единый реестр действий |
| `anim.hpp` / `anim.cpp` | **реестр анимаций**: enum Id, `kRows`, дефолты, Lua-правила. `config.*` о нём не знает |
| `config/config.hpp` / `.cpp` | типизированная схема значений + `kInts`/`kFloats`/`kStrs`/`kColors` |
| `session/session.cpp` | lifecycle (open/close/hardClose/deactivate), хуки, геометрия стрипа |
| `build/build.cpp` | buildTiles/buildStrip/layoutTiles/replayReflow/syncTiles, `stripCardAt` |
| `input/mouse.cpp`, `input/keys.cpp` | мышь, клавиатура, alt-tab, digit-jump |
| `actions/actions.cpp` | drop/swap, `swapWindows`, `landAfterSwap`, `switchToWorkspace` |
| `render/painter.cpp` | `renderStage` + `PainterPass` + `paint()`, единственная точка регистрации прохода |
| `render/backdrop.cpp` | `renderBackdropSource()` (27), `primeBackdrop()` (189), `renderBackdrop()` (227) |
| `render/tile_view.cpp`, `carousel_view.cpp`, `strip_view.cpp`, `fx.cpp` | хром плиток, карусель, стрип, эффекты |
| `render/window_content.cpp` | контент окна (live-поверхность) |
| `blur.hpp` / `blur.cpp` | self-contained dual-Kawase + gaussian |
| `clock/clock.cpp`, `clocks.cpp`, `curves.cpp` | домен времени: tween, прогрессы, resolved anim cfg, реестр кривых |
| `cursor.*` | HW/SW курсор поверх оверлея (следствие RENDER_LAST_MOMENT) |
| `layout.hpp` / `.cpp` | чистая геометрия лейаута, без Hyprland (единственный юнит-домен) |
| `model/` | `scene.hpp` (плитки, выбор, интенты), `strip.hpp`, `effects.hpp` (ghosts/pulses/swapfx), `recency.*`, `model.hpp`. Поля приватные, наружу — намерения |
| `pixel/cache.cpp` | кэш подписей и снапшотов |
| `tests/` | ctest: 13 проверок, включая `load_seam`, `hook_signatures`, `config_keys`, `anim_styles` |

## Открытые вопросы

- **Краш 2026-10-10 16:27 — не расследован.** `Screenshare::CScreenshareFrame::renderWindow()`
  → SEGV, кадр #5 в `gloview.so`; имя функции в отчёте недостоверно (см. выше).
  Второе падение в 16:28 — `Monitor::useFP16()` из `CHyprOpenGLImpl::end()`,
  плагина в стеке нет. Лог плагина обрывается штатно (`deactivate (natural close end)`).
  Тот же стек screenshare был и в 15:01, до правок анимации.
  **Нужен ответ: был ли активен wf-recorder/screenshare в момент падения.**
- **Ховер-заливка плиток/карточек:** не подтверждена кодом. Нужен скрин воспроизведения.
- **Истинный халф-сплит при вставке упирается в API Hyprland**: `assignToSpace`
  уже делает `splitBeside` без фокуса, но «занять половину» — layout-insert API,
  которого нет. Пока не появится — не трогать.

## Что сделано и закрыто

- **`grid_swap_style` / `strip_swap_style` ничего не делали.** Ключи
  зарегистрированы и задокументированы, но call sites просили `anim::style("grid_swap")`,
  а такой строки в `kRows` нет → `{}` → `SwapStyle::Horizontal` → дефолт. Общая
  форма бага — спросить `style()` имя, которое `style()` не резолвит; тест
  `anim_styles` теперь проверяет правило, а не два случая.
- **`Horizontal` был единственным стилем без анимации** — `landAfterMove` на нём
  уходил в `pinNatural` вместо полёта. Дефолт из README не двигал ничего.
- **Своп, который Hyprland не может выразить, невидим.** `switchTargets` двигает
  окна только если их геометрия различается; два окна в одной точке дают
  `goal()` без изменений. Обмен выражается в раскладке оверлеи (`Scene::swapTiles`
  меняет цели карточек, не natural) и только когда пересборка оставила тайл
  без движения (`tileAtRest`) — иначе обмен отменял бы глид.
- **`renderSwapFX` искал цель по tiles первым** и всегда находил, до strip-ветки
  не доходил: в all-workspaces полёт уходил только в грид. Полета теперь два,
  помеченных `toStrip`, и каждая поверхность спрашивает про свой.
- **Каждая поверхность стартует из своей позиции**, курсор подставляется только
  у той, откуда отпустили. Иначе драг стрипа запускал карточку грида из
  thumbnail'а внизу экрана.
- **Все стили, кроме дефолтного, игнорировались** (нашёл `tools/anim_matrix.py`).
  `grid_swap_style` / `strip_swap_style` / `jump_style` не строки kRows, у них свои
  accessors, и они читали handle напрямую — минуя override, который смотрит `style()`.

## Автоматизация и трейс

`hl.plugin.gloview.cmd("<команда>")` — шов автоматизации: `state`, `open`, `close`,
`allws N`, `sel N`, `swap A B`, `movews T WS`, `set KEY VALUE`, `tuning`.
Под Lua-конфигом `hyprctl dispatch` до dispatchers плагина не доходит вообще, поэтому
команда живёт в Lua. **Каждая команда подтверждает себя в логе** (`CMD|ok` / `CMD|err`):
`hyprctl eval` не печатает возвращаемое значение, поэтому лог — единственный канал.

Трейс: `ANIM|begin` (op/style/ms/curve/win/surface/from/to), `ANIM|end` (измеренное ms),
`ANIM|reject`. Ключ `plugin:gloview:debug_trace` — отдельный от `debug_logs`, иначе
матрицу забивает покадровый спам. Формат парсится `tools/anim_matrix.py`, не менять
его без правки скрипта.

`hl.config` работает **только вложенной формой**: `{plugin = {gloview = {...}}}`.
Плоский `["plugin:gloview:x"]` и `hyprctl keyword` не подходят — отвергаются.

Приоритет настроек, снизу вверх: строка kRows → `set` (команда, переопределяет всё) →
rule из `gloview.animation` → config-значение. **Rule бьёт config** (resolve()
применяет его последним) — поэтому `set` не просто пишет input, а гасит
переопределяемые поля rule, иначе `speed=1.2` из конфига делит запрошенные 320 на 1.2.

Прогон: `python3 tools/anim_matrix.py [--quick] [--list]`. Требует ≥2 плиток.

## Правила работы

- **Сессии:** начал — прочёл этот файл; сделал шаг — отметил в «Что сделано» или
  «Открытые вопросы».
- **Один логический шаг = один коммит.** Коммит только после зелёной сборки.
  Стиль сообщений: `type(scope): summary` на английском, тело объясняет why
  (примеры в `git log`).
- **Комментарии в коде** — английские, докунят «почему» и верифицированные факты
  о внутренностях Hyprland (файл:строка источника). Археологию удалённых багов в
  комментариях не плодить — для этого git history. Общие эссе — в общий заголовок
  (`src/render/gl_util.hpp` как образец), не копировать между TU.
- **Дедупликация прежде копипасты:** повторившийся 3+ раз паттерн → хелпер
  (`captureCurrentBoxes`, `releaseNewWorkspaces`, `tileRect`, `landAfterSwap` —
  образцы). Хит-тесты — через `LRect::contains`.
- **Новые карты по окнам** не ключевые raw-указателями (ABA при переиспользовании
  адреса) — PHLWINDOWREF или явный жизненный цикл с очисткой.
- Стиль: 2 пробела, clang-format-подобный; `layout.*` исторически 4 пробела —
  не переформатировать целиком.
- Без эмодзи. README обновлять, если меняется пользовательское поведение или
  добавляются конфиги.

## Чеклист перед коммитом

- [ ] сборка зелёная (error И warning)
- [ ] нет обращений к удалённым членам/API (grep по имени)
- [ ] новые конфиги: в `src/config/config.cpp` (`kInts`/`kFloats`/`kStrs`/`kColors`)
      или `src/anim.cpp` (`kRows`), описаны в README, зелёный `ctest` (ловит
      «поле есть, регистрации нет»)
- [ ] поведение по умолчанию не изменилось (если это не задумано)
- [ ] этот файл обновлён, если изменилась семантика