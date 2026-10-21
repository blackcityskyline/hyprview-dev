cmake_minimum_required(VERSION 3.19)
project(hyprview VERSION "0.3.0" LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

add_compile_definitions(WLR_USE_UNSTABLE)
# -fno-gnu-unique: C++ static-init guard vars (_ZGV...) otherwise emit as STB_GNU_UNIQUE,
# which makes glibc mark the .so NODELETE so dlclose never unmaps it — a same-path
# `hyprctl plugin load` after unload then returns the CACHED old image (same dlopen handle)
# and the hot-reload silently runs stale code. Plain symbols let dlclose truly unmap.
add_compile_options(-Wall -Wextra -Wno-unused-parameter -Wno-missing-field-initializers -fPIC -fno-gnu-unique)

include(GNUInstallDirs)

find_package(PkgConfig REQUIRED)
pkg_check_modules(HYPR_DEPS REQUIRED hyprland)
# cairo reads the PNGs and is already inside this process via Hyprland; the check needs its
# flags directly because it deliberately does not compile against hyprland/ (AGENTS.md 2.8).
pkg_check_modules(CAIRO REQUIRED cairo)
string(REPLACE ";" " " CAIRO_FLAGS "${CAIRO_CFLAGS};${CAIRO_LDFLAGS}")
# Raw GL (glCreateShader/glDrawArrays…) for the plugin's own blur shader.
pkg_check_modules(GLES REQUIRED glesv2)
# Optional: an icon shipped only as SVG has no PNG to load. Without librsvg that app keeps no
# corner icon; nothing else changes.
pkg_check_modules(RSVG IMPORTED_TARGET librsvg-2.0)
# Lua's pkg-config module name varies by distro: `lua` (Arch), `lua5.4`/`lua-5.4`
# (Debian/Nix), or `luajit` (what Hyprland itself links). Take the first that exists.
pkg_search_module(LUA REQUIRED lua lua5.4 lua-5.4 lua54 luajit)

add_library(hyprview SHARED
  src/main.cpp
  src/session/hooks.cpp
  src/session/geometry.cpp
  src/session/lifecycle.cpp
  src/session/preview_visibility.cpp
  src/session/overview_lifetime.cpp
  src/session/pending_click.cpp
  src/session/layer_visibility.cpp
  src/build/build.cpp
  src/input/mouse.cpp
  src/input/keys.cpp
  src/actions/actions.cpp
  src/layout.cpp
  src/model/recency.cpp
  src/model/window_source.cpp
  src/cursor.cpp
  src/blur.cpp
  src/pixel/cache.cpp
  src/pixel/png.cpp
  src/pixel/png_read.cpp
  src/clock/clock.cpp
  src/clock/clocks.cpp
  src/clock/placement.cpp
  src/clock/curves.cpp
  src/config/config.cpp
  src/anim.cpp
  src/anim_resolve.cpp
  src/debug/log.cpp
  src/render/painter.cpp
  src/render/backdrop.cpp
  src/render/frame_pump.cpp
  src/render/window_content.cpp
  src/render/tile_view.cpp
  src/render/carousel_view.cpp
  src/render/strip_view.cpp
  src/render/fx.cpp
  src/render/shadow.cpp
)
set_target_properties(hyprview PROPERTIES PREFIX "")  # produce hyprview.so, not libhyprview.so

target_include_directories(hyprview PRIVATE ${HYPR_DEPS_INCLUDE_DIRS} ${LUA_INCLUDE_DIRS} ${GLES_INCLUDE_DIRS})
foreach(inc ${HYPR_DEPS_INCLUDE_DIRS})
  if(EXISTS "${inc}/hyprland/src")
    target_include_directories(hyprview PRIVATE "${inc}/hyprland/src")
  endif()
endforeach()

target_link_directories(hyprview PRIVATE ${HYPR_DEPS_LIBRARY_DIRS} ${LUA_LIBRARY_DIRS} ${GLES_LIBRARY_DIRS})
target_link_libraries(hyprview PRIVATE ${HYPR_DEPS_LIBRARIES} ${LUA_LIBRARIES} ${GLES_LIBRARIES})
if(RSVG_FOUND)
  target_link_libraries(hyprview PRIVATE PkgConfig::RSVG)
  target_compile_definitions(hyprview PRIVATE HYPRVIEW_HAVE_RSVG)
endif()

install(TARGETS hyprview LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR})

# Dev hot-reload (official same-path unload;load):  cmake --build build --target reload
# This is the upstream-documented dev reload (Hyprland wiki, Plugins/Development):
#   hyprctl plugin unload /abs/plugin.so ; hyprctl plugin load /abs/plugin.so
# It ONLY picks up new code because of -fno-gnu-unique above: C++ static-init guard vars
# (_ZGV...) otherwise emit as STB_GNU_UNIQUE, which makes glibc mark the .so NODELETE so
# dlclose never unmaps it and the same-path load returns the CACHED old image (same dlopen
# handle) -> a silent stale reload. With the flag, dlclose truly unmaps and the same path
# re-mmaps the fresh build. (If you ever load a pre-flag binary, that image sticks NODELETE
# for the life of the Hyprland process and poisons its path until you restart Hyprland once.)
# `hyprctl hyprviewunload` (Overview::hardClose) first does a synchronous, animation-free
# teardown + cancels the recapture timer so the unload can't free plugin-owned render/timer
# state while Hyprland still references it (-> SEGV / 150% IPC-dead spin); the 0.15s settle
# lets one plugin-free frame flush the render pass. A plugin crash kills the whole session —
# prefer a nested Hyprland for risky changes. (For installed plugins the user path is
# `hyprpm enable/disable <name>` + `hyprpm reload`.)
# Layout and exit self-checks. Both are plain asserts-over-arithmetic, no
# compositor, so they run on every build: the carousel exit was shipped broken
# three times (its progress read with the wrong sign) and a screenshot does not
# tell a zero-size card from a small one.
enable_testing()
add_test(NAME recency
         COMMAND ${CMAKE_COMMAND} -E env sh -c
                 "${CMAKE_CXX_COMPILER} -std=c++23 -Wall -Wextra ${CMAKE_SOURCE_DIR}/tests/recency_check.cpp ${CMAKE_SOURCE_DIR}/src/model/recency.cpp -o ${CMAKE_CURRENT_BINARY_DIR}/recency_check && ${CMAKE_CURRENT_BINARY_DIR}/recency_check")
add_test(NAME carousel_layout
         COMMAND ${CMAKE_COMMAND} -E env sh -c
                 "${CMAKE_CXX_COMPILER} -std=c++23 -Wall -Wextra -I ${CMAKE_SOURCE_DIR}/src ${CMAKE_SOURCE_DIR}/tests/layout_carousel_check.cpp ${CMAKE_SOURCE_DIR}/src/layout.cpp -o ${CMAKE_CURRENT_BINARY_DIR}/layout_check && ${CMAKE_CURRENT_BINARY_DIR}/layout_check")
add_test(NAME carousel_exit
         COMMAND ${CMAKE_COMMAND} -E env sh -c
                 "${CMAKE_CXX_COMPILER} -std=c++23 -Wall -Wextra ${CMAKE_SOURCE_DIR}/tests/carousel_exit_check.cpp -o ${CMAKE_CURRENT_BINARY_DIR}/exit_check && ${CMAKE_CURRENT_BINARY_DIR}/exit_check")

# The caption limit counts characters, not bytes, and the dots are inside it: both were
# the way to get a 50-character caption that renders as half a broken letter.
add_test(NAME title_text
         COMMAND ${CMAKE_COMMAND} -E env sh -c
                 "${CMAKE_CXX_COMPILER} -std=c++23 -Wall -Wextra -I ${CMAKE_SOURCE_DIR}/src ${CMAKE_SOURCE_DIR}/tests/title_text_check.cpp -o ${CMAKE_CURRENT_BINARY_DIR}/title_text_check && ${CMAKE_CURRENT_BINARY_DIR}/title_text_check")

# Naming: `scan_*` targets read this repository's own source text and match it against a
# rule. They are static analysis, and they were named like tests -- so a ctest run read as
# "everything here executes logic on real data" when three of seventeen never ran any.
# Written in C++ they still scan text: the regexes do not care what language reads them.
#
# A target without the prefix executes something -- the logic under test, a binary, or a
# real symbol table. hook_signatures matches nm -DC output against the running Hyprland
# and load_seam dlopens the .so; both check against reality rather than against a copy of
# the source, so neither takes the prefix.
#
add_test(NAME scan_config_keys
         COMMAND bash -c "${CMAKE_CXX_COMPILER} -std=c++23 -Wall -Wextra ${CMAKE_SOURCE_DIR}/tests/config_keys_check.cpp -o ${CMAKE_CURRENT_BINARY_DIR}/cfg_keys_check && ${CMAKE_CURRENT_BINARY_DIR}/cfg_keys_check ${CMAKE_SOURCE_DIR}")

# The animation precedence chain, headless. Reachable only because resolve() was split
# out of anim.cpp: Config::Values lives in the Hyprland binary, so anim.cpp cannot be
# linked into a test at all.
add_test(NAME anim_resolve
         COMMAND ${CMAKE_COMMAND} -E env sh -c
                 "${CMAKE_CXX_COMPILER} -std=c++23 -Wall -Wextra -I ${CMAKE_SOURCE_DIR}/src ${CMAKE_SOURCE_DIR}/tests/anim_resolve_check.cpp ${CMAKE_SOURCE_DIR}/src/anim_resolve.cpp -o ${CMAKE_CURRENT_BINARY_DIR}/anim_resolve_check && ${CMAKE_CURRENT_BINARY_DIR}/anim_resolve_check")

# The shape-parameter table against the enum it is indexed by. A Param added without a
# key row used to read the next slot's value at runtime: no error, one wrong number.
add_test(NAME anim_params
         COMMAND ${CMAKE_COMMAND} -E env sh -c
                 "${CMAKE_CXX_COMPILER} -std=c++23 -Wall -Wextra -I ${CMAKE_SOURCE_DIR}/src ${CMAKE_SOURCE_DIR}/tests/anim_params_check.cpp ${CMAKE_SOURCE_DIR}/src/anim_resolve.cpp -o ${CMAKE_CURRENT_BINARY_DIR}/anim_params_check && ${CMAKE_CURRENT_BINARY_DIR}/anim_params_check")

# The style sets, and a scan of the motion files for comparisons against names that are
# not in them. Five string comparisons used to be the only record of what a style may be.
add_test(NAME anim_styles
         COMMAND ${CMAKE_COMMAND} -E env sh -c
                 "${CMAKE_CXX_COMPILER} -std=c++23 -Wall -Wextra -I ${CMAKE_SOURCE_DIR}/src ${CMAKE_SOURCE_DIR}/tests/anim_styles_check.cpp ${CMAKE_SOURCE_DIR}/src/anim_resolve.cpp -o ${CMAKE_CURRENT_BINARY_DIR}/anim_styles_check && ${CMAKE_CURRENT_BINARY_DIR}/anim_styles_check ${CMAKE_SOURCE_DIR}")

# The entry ladder, as arithmetic. staggered() reads the spread from anim::param() and
# cannot be linked, so the part under test takes the spread handed in.
add_test(NAME anim_stagger
         COMMAND ${CMAKE_COMMAND} -E env sh -c
                 "${CMAKE_CXX_COMPILER} -std=c++23 -Wall -Wextra -I ${CMAKE_SOURCE_DIR}/src ${CMAKE_SOURCE_DIR}/tests/anim_stagger_check.cpp ${CMAKE_SOURCE_DIR}/src/anim_resolve.cpp -o ${CMAKE_CURRENT_BINARY_DIR}/anim_stagger_check && ${CMAKE_CURRENT_BINARY_DIR}/anim_stagger_check")

# Which leaf drives the master clock, and the backdrop's exit rule. These were three
# separate expressions of two questions, and nobody checked that the three agreed.
add_test(NAME anim_master
         COMMAND ${CMAKE_COMMAND} -E env sh -c
                 "${CMAKE_CXX_COMPILER} -std=c++23 -Wall -Wextra -I ${CMAKE_SOURCE_DIR}/src ${CMAKE_SOURCE_DIR}/tests/anim_master_check.cpp ${CMAKE_SOURCE_DIR}/src/anim_resolve.cpp -o ${CMAKE_CURRENT_BINARY_DIR}/anim_master_check && ${CMAKE_CURRENT_BINARY_DIR}/anim_master_check")

# What the toggle key means, and whether a card's natural box is a destination. Escape,
# Enter and the toggle bind all ended in close() and all showed the same lying exit.
add_test(NAME dismiss
         COMMAND ${CMAKE_COMMAND} -E env sh -c
                 "${CMAKE_CXX_COMPILER} -std=c++23 -Wall -Wextra -I ${CMAKE_SOURCE_DIR}/src ${CMAKE_SOURCE_DIR}/tests/dismiss_check.cpp ${CMAKE_SOURCE_DIR}/src/anim_resolve.cpp -o ${CMAKE_CURRENT_BINARY_DIR}/dismiss_check && ${CMAKE_CURRENT_BINARY_DIR}/dismiss_check")

add_test(NAME scan_draw_sites
         COMMAND bash -c "${CMAKE_CXX_COMPILER} -std=c++23 -Wall -Wextra ${CMAKE_SOURCE_DIR}/tests/draw_sites_check.cpp -o ${CMAKE_CURRENT_BINARY_DIR}/draw_sites_check && ${CMAKE_CURRENT_BINARY_DIR}/draw_sites_check ${CMAKE_SOURCE_DIR}/src")

# The box guard on immediate-mode draws. A grep over the sources rather than a link:
# the guarded wrappers call g_pHyprOpenGL, which lives in the Hyprland binary. Two of
# these shipped broken -- an unchecked flight shadow, which took a session on
# 2026-10-10, and a strip helper testing `width < 0` where the assert wants `> 0`.
# Adding chrome is the normal way to write new code here, and nothing else objects
# when the new draw is unchecked.
add_test(NAME layout
         COMMAND ${CMAKE_COMMAND} -E env sh -c
                 "${CMAKE_CXX_COMPILER} -std=c++23 -Wall -Wextra -I ${CMAKE_SOURCE_DIR}/src ${CMAKE_SOURCE_DIR}/tests/layout_check.cpp ${CMAKE_SOURCE_DIR}/src/layout.cpp -o ${CMAKE_CURRENT_BINARY_DIR}/layout_check && ${CMAKE_CURRENT_BINARY_DIR}/layout_check")

# Two Hyprland-type rules, both of which were paragraphs in AGENTS.md. See the header of
# the check for why the second one is about the value type and not the key type.
add_test(NAME scan_hypr_types
         COMMAND bash -c "${CMAKE_CXX_COMPILER} -std=c++23 -Wall -Wextra ${CMAKE_SOURCE_DIR}/tests/hypr_types_check.cpp -o ${CMAKE_CURRENT_BINARY_DIR}/hypr_types_check && ${CMAKE_CURRENT_BINARY_DIR}/hypr_types_check ${CMAKE_SOURCE_DIR}/src")

# Where a card starts. Header-only and pure, so the decision that produced every visual
# bug of the tab work -- five sites, five rules, one hidden boolean each -- is checkable
# without a compositor.
# The ratchet on AGENTS.md 2.15. A knob read inside paint lands on the tiles painted before
# it and not the ones after -- which is why a caption ends up right on one card and wrong on
# the next. The rule is broken in 111 places today, so the check is a baseline that may only go
# AGENTS.md 2.15, at its actual scope: the geometry layer takes its settings as arguments and
# reads no config. Scoped to the PURE headers, because that is the boundary the rule names --
# the Overview:: wrapper gathers the inputs and render::geometry does the arithmetic. A read
# there takes the header out of reach of every header-only test, which is the only reason any
# of it can be tested without a compositor.
add_test(NAME scan_render_config
         COMMAND bash -c "${CMAKE_CXX_COMPILER} -std=c++23 -Wall -Wextra ${CMAKE_SOURCE_DIR}/tests/render_config_ratchet_check.cpp -o ${CMAKE_CURRENT_BINARY_DIR}/render_config_ratchet_check && ${CMAKE_CURRENT_BINARY_DIR}/render_config_ratchet_check ${CMAKE_SOURCE_DIR}/src")

# Comment weight. Five successive comment blocks inside drawTile described five successive
# versions of one rule, each written to answer the bug visible that day and each cancelling the
# one above it. A comment that is not the current answer is worse than none: it reads as the
# answer. 20.7% today, 10% is the target, and going over the baseline is red.
add_test(NAME scan_comment_weight
         COMMAND bash -c "${CMAKE_CXX_COMPILER} -std=c++23 -Wall -Wextra ${CMAKE_SOURCE_DIR}/tests/comment_weight_check.cpp -o ${CMAKE_CURRENT_BINARY_DIR}/comment_weight_check && ${CMAKE_CURRENT_BINARY_DIR}/comment_weight_check ${CMAKE_SOURCE_DIR}/src")

add_test(NAME origin
         COMMAND ${CMAKE_COMMAND} -E env sh -c
                 "${CMAKE_CXX_COMPILER} -std=c++23 -Wall -Wextra -I ${CMAKE_SOURCE_DIR}/src ${CMAKE_SOURCE_DIR}/tests/origin_check.cpp -o ${CMAKE_CURRENT_BINARY_DIR}/origin_check && ${CMAKE_CURRENT_BINARY_DIR}/origin_check")

# Overview must not hold state that has exactly one writer. The god object accumulated one
# correct-looking field per commit; the property that separates them from the genuinely shared
# state is which file writes them, and that is greppable. Registry half guards the extractions
# already made, the counting half names the next one.
add_test(NAME scan_ownership
         COMMAND bash -c "${CMAKE_CXX_COMPILER} -std=c++23 -Wall -Wextra ${CMAKE_SOURCE_DIR}/tests/ownership_check.cpp -o ${CMAKE_CURRENT_BINARY_DIR}/ownership_check && ${CMAKE_CURRENT_BINARY_DIR}/ownership_check ${CMAKE_SOURCE_DIR}/src")

# The drag FSM. model/drag.hpp is templated on its window handle so it carries no
# hyprland/ include, which is what makes the state machine reachable from here at all --
# every one of its invariants was previously a property of the order of fourteen field
# assignments in input/mouse.cpp.
add_test(NAME drag
         COMMAND ${CMAKE_COMMAND} -E env sh -c
                 "${CMAKE_CXX_COMPILER} -std=c++23 -Wall -Wextra -I ${CMAKE_SOURCE_DIR}/src ${CMAKE_SOURCE_DIR}/tests/drag_check.cpp -o ${CMAKE_CURRENT_BINARY_DIR}/drag_check && ${CMAKE_CURRENT_BINARY_DIR}/drag_check")

# The tile engines, as arithmetic. computeCarousel has its own check; computeLayout,
# which produces every box the grid/rows/natural views hand the renderer, had none --
# and layoutGrid could emit a NEGATIVE box when the gaps outgrew the area, which is
# what the compositor's render asserts turn on.
add_test(NAME curves
         COMMAND ${CMAKE_COMMAND} -E env sh -c
                 "${CMAKE_CXX_COMPILER} -std=c++23 -Wall -Wextra -I ${CMAKE_SOURCE_DIR}/src ${CMAKE_SOURCE_DIR}/tests/curves_check.cpp ${CMAKE_SOURCE_DIR}/tests/hypr_stub.cpp ${CMAKE_SOURCE_DIR}/src/clock/curves.cpp -o ${CMAKE_CURRENT_BINARY_DIR}/curves_check -llua -lhyprutils && ${CMAKE_CURRENT_BINARY_DIR}/curves_check")

# The load seam. Needs the real .so, so it only means anything once it has been
# built: `ctest -R load_seam` afterwards, or CI, where the .so always exists.
# SKIP_RETURN_CODE so a machine without Hyprland reports skip, not a fake failure.
find_program(HYPRLAND_BIN Hyprland)
add_test(NAME scan_anim_coverage
         COMMAND bash -c "${CMAKE_CXX_COMPILER} -std=c++23 -Wall -Wextra ${CMAKE_SOURCE_DIR}/tests/anim_coverage_check.cpp -o ${CMAKE_CURRENT_BINARY_DIR}/anim_cov_check && ${CMAKE_CURRENT_BINARY_DIR}/anim_cov_check ${CMAKE_SOURCE_DIR}")

# A SubjectRef is a number, and somebody has to hold the window it stands for. This pins who:
# the source, via the alive list it refreshes on enumerate. The crashing revision built a
# shared pointer straight from the id and took the compositor down with it, and neither file
# can be compiled without Hyprland, so a grep is the only place the rule can live.
add_test(NAME scan_subject_ownership
         COMMAND bash -c "${CMAKE_CXX_COMPILER} -std=c++23 -Wall -Wextra ${CMAKE_SOURCE_DIR}/tests/subject_ownership_check.cpp -o ${CMAKE_CURRENT_BINARY_DIR}/subject_own_check && ${CMAKE_CURRENT_BINARY_DIR}/subject_own_check ${CMAKE_SOURCE_DIR}")

# src/model/ may not name a window. A tab has none, so a window-typed field is a field that
# is null in the other domain and every reader then skips or crashes with nothing written
# down. The two adapter files are exempt by name; workspaces are not covered, because they
# have no tab equivalent and are workspace-shaped by nature.
add_test(NAME scan_model_window_types
         COMMAND bash -c "${CMAKE_CXX_COMPILER} -std=c++23 -Wall -Wextra ${CMAKE_SOURCE_DIR}/tests/model_window_types_check.cpp -o ${CMAKE_CURRENT_BINARY_DIR}/model_win_check && ${CMAKE_CURRENT_BINARY_DIR}/model_win_check ${CMAKE_SOURCE_DIR}")

# What the browser side said, and what the plugin may conclude from it. The rules are all
# refusals: a parse that guesses reads as a browser with fewer tabs, and nothing would say so.
# Pure C++ -- no compositor, no socket, no extension -- which is the point of parsing here and
# nowhere else.
add_test(NAME tab_feed
         COMMAND bash -c "${CMAKE_CXX_COMPILER} -std=c++23 -Wall -Wextra ${CMAKE_SOURCE_DIR}/tests/tab_feed_check.cpp -o ${CMAKE_CURRENT_BINARY_DIR}/tab_feed_check && ${CMAKE_CURRENT_BINARY_DIR}/tab_feed_check")

# Which Hyprland window is which browser window. The two sides share one fact -- a browser
# window's title is its active tab's title -- so the match is by title, and every rule here
# is about refusing to guess. A wrong match files one browser's tabs under another's window
# and nothing about the result looks wrong on screen.
add_test(NAME tab_match
         COMMAND bash -c "${CMAKE_CXX_COMPILER} -std=c++23 -Wall -Wextra ${CMAKE_SOURCE_DIR}/tests/tab_match_check.cpp -o ${CMAKE_CURRENT_BINARY_DIR}/tab_match_check && ${CMAKE_CURRENT_BINARY_DIR}/tab_match_check")

# The second IContentSource implementation, built only out of the interface: no Hyprland type,
# no window, no workspace. That is what makes `virtual` on the interface mean something, and it
# is why this compiles under ctest without a compositor.
add_test(NAME tab_source
         COMMAND bash -c "${CMAKE_CXX_COMPILER} -std=c++23 -Wall -Wextra ${CMAKE_SOURCE_DIR}/tests/tab_source_check.cpp -o ${CMAKE_CURRENT_BINARY_DIR}/tab_source_check && ${CMAKE_CURRENT_BINARY_DIR}/tab_source_check")

# A PNG on disk, decoded. No compositor, no GPU: this is the half that can be checked, and
# the failures it has to survive -- missing, truncated, not a PNG at all -- are the ones the
# extension's half-written directory produces routinely.
add_test(NAME png_read
         COMMAND bash -c "${CMAKE_CXX_COMPILER} -std=c++23 -Wall -Wextra ${CMAKE_SOURCE_DIR}/tests/png_read_check.cpp ${CMAKE_SOURCE_DIR}/src/pixel/png_read.cpp -o ${CMAKE_CURRENT_BINARY_DIR}/png_read_check ${CAIRO_FLAGS} && ${CMAKE_CURRENT_BINARY_DIR}/png_read_check")

# The staleness rule of the subject cache. A tab id belongs to the connection that issued it,
# so a picture made under an old connection must not answer under a new one -- the browser
# is reusing those ids right now. Templated on the stored type so it can be checked without a
# GPU or an ITexture (AGENTS.md 2.8).
add_test(NAME subject_cache
         COMMAND bash -c "${CMAKE_CXX_COMPILER} -std=c++23 -Wall -Wextra ${CMAKE_SOURCE_DIR}/tests/subject_cache_check.cpp -o ${CMAKE_CURRENT_BINARY_DIR}/subject_cache_check && ${CMAKE_CURRENT_BINARY_DIR}/subject_cache_check")

# scan_config_keys proves every key is REGISTERED, which is the easy half: adding the line is
# all it takes to pass. This proves every key is READ. tile_mode sat registered and unread for
# the whole of the tab work, its default happened to match the behaviour, and a user setting
# tile_mode = "tabs" was ignored with no trace of any kind.
add_test(NAME scan_config_keys_read
         COMMAND bash -c "${CMAKE_CXX_COMPILER} -std=c++23 -Wall -Wextra ${CMAKE_SOURCE_DIR}/tests/config_keys_read_check.cpp -o ${CMAKE_CURRENT_BINARY_DIR}/config_keys_read_check && ${CMAKE_CURRENT_BINARY_DIR}/config_keys_read_check ${CMAKE_SOURCE_DIR}")

# Two regressions that both looked like "the preview is missing": the source held only the
# windows that got a tile, so every strip card but the current workspace had nothing to resolve;
# and the window opacity was pinned to 1. Both are a line that was deleted and has to stay put.
add_test(NAME scan_preview_regressions
         COMMAND bash -c "${CMAKE_CXX_COMPILER} -std=c++23 -Wall -Wextra ${CMAKE_SOURCE_DIR}/tests/preview_regressions_check.cpp -o ${CMAKE_CURRENT_BINARY_DIR}/preview_regressions_check && ${CMAKE_CURRENT_BINARY_DIR}/preview_regressions_check ${CMAKE_SOURCE_DIR}")

# key_browser's view switch. The two views hold different subjects, so it is a rebuild and not
# a relayout; and Both stays out of the toggle cycle, which is a product decision with nothing
# left to enforce it once someone tidies the enum into a three-way rotation.
add_test(NAME scan_domain_view
         COMMAND bash -c "${CMAKE_CXX_COMPILER} -std=c++23 -Wall -Wextra ${CMAKE_SOURCE_DIR}/tests/domain_view_check.cpp -o ${CMAKE_CURRENT_BINARY_DIR}/domain_view_check && ${CMAKE_CURRENT_BINARY_DIR}/domain_view_check ${CMAKE_SOURCE_DIR}")

# A card's kind is decided in ONE function and the sites read named answers. It was 33 call
# sites each asking tileWindow() for itself and they disagreed, and every one of the
# disagreements was found by a user report rather than by a test. Adding a kind without a
# window cost three days for that reason alone.
add_test(NAME scan_card_kind
         COMMAND bash -c "${CMAKE_CXX_COMPILER} -std=c++23 -Wall -Wextra ${CMAKE_SOURCE_DIR}/tests/card_kind_check.cpp -o ${CMAKE_CURRENT_BINARY_DIR}/card_kind_check && ${CMAKE_CURRENT_BINARY_DIR}/card_kind_check ${CMAKE_SOURCE_DIR}")

# The brotab adapter, against a fake mediator speaking its real endpoints on a real port# inside its real range. The pid -> ppid hop is what the whole feature rests on and it is
# only checkable against a live process.
add_test(NAME mediator
         COMMAND python3 ${CMAKE_SOURCE_DIR}/bridge/mediator_check.py)

# The pump disarms its timer, never resets it: a reset drops the last shared pointer inside
# the callback that owns it.
add_test(NAME scan_frame_pump
         COMMAND ${CMAKE_COMMAND} -E env sh -c
                 "${CMAKE_CXX_COMPILER} -std=c++23 -Wall -Wextra ${CMAKE_SOURCE_DIR}/tests/frame_pump_check.cpp -o ${CMAKE_CURRENT_BINARY_DIR}/frame_pump_check && ${CMAKE_CURRENT_BINARY_DIR}/frame_pump_check ${CMAKE_SOURCE_DIR}")

# The effects: a pulse is replaced, a flight stands down one surface. The file is a header
# without a compositor, which it only became when the workspace list left it.
add_test(NAME effects
         COMMAND ${CMAKE_COMMAND} -E env sh -c
                 "${CMAKE_CXX_COMPILER} -std=c++23 -Wall -Wextra -I ${CMAKE_SOURCE_DIR}/src ${CMAKE_SOURCE_DIR}/tests/effects_check.cpp -o ${CMAKE_CURRENT_BINARY_DIR}/effects_check && ${CMAKE_CURRENT_BINARY_DIR}/effects_check")

# The strip scroll positions. model/scroll.hpp owns the [0, max] invariant that four free
# doubles did not hold -- the current position had no guard at all when the strip shrank.
add_test(NAME scroll
         COMMAND ${CMAKE_COMMAND} -E env sh -c
                 "${CMAKE_CXX_COMPILER} -std=c++23 -Wall -Wextra -I ${CMAKE_SOURCE_DIR}/src ${CMAKE_SOURCE_DIR}/tests/scroll_check.cpp -o ${CMAKE_CURRENT_BINARY_DIR}/scroll_check && ${CMAKE_CURRENT_BINARY_DIR}/scroll_check")

add_test(NAME lifecycle
         COMMAND ${CMAKE_COMMAND} -E env sh -c
                 "${CMAKE_CXX_COMPILER} -std=c++23 -Wall -Wextra -I ${CMAKE_SOURCE_DIR}/src ${CMAKE_SOURCE_DIR}/tests/lifecycle_check.cpp -o ${CMAKE_CURRENT_BINARY_DIR}/lifecycle_check && ${CMAKE_CURRENT_BINARY_DIR}/lifecycle_check")

# The debug_trace family mask. A mask that selects nothing is indistinguishable from a
# trace that was never switched on, so every bit is pinned individually.
add_test(NAME trace_mask
         COMMAND ${CMAKE_COMMAND} -E env sh -c
                 "${CMAKE_CXX_COMPILER} -std=c++23 -Wall -Wextra -I ${CMAKE_SOURCE_DIR}/src ${CMAKE_SOURCE_DIR}/tests/trace_mask_check.cpp -o ${CMAKE_CURRENT_BINARY_DIR}/trace_mask_check && ${CMAKE_CURRENT_BINARY_DIR}/trace_mask_check")

# The content-source contract, implemented twice. The point is that two unrelated
# implementations can be written against it without either one's types leaking into the
# other, and that the caller written once against the contract runs against both.

add_test(NAME content_source
         COMMAND ${CMAKE_COMMAND} -E env sh -c
                 "${CMAKE_CXX_COMPILER} -std=c++23 -Wall -Wextra -I ${CMAKE_SOURCE_DIR}/src ${CMAKE_SOURCE_DIR}/tests/content_source_check.cpp -o ${CMAKE_CURRENT_BINARY_DIR}/content_source_check && ${CMAKE_CURRENT_BINARY_DIR}/content_source_check")

# Subject identity and the epoch rule. model/subject.hpp has no compositor in it.

add_test(NAME subject
         COMMAND ${CMAKE_COMMAND} -E env sh -c
                 "${CMAKE_CXX_COMPILER} -std=c++23 -Wall -Wextra -I ${CMAKE_SOURCE_DIR}/src ${CMAKE_SOURCE_DIR}/tests/subject_check.cpp -o ${CMAKE_CURRENT_BINARY_DIR}/subject_check && ${CMAKE_CURRENT_BINARY_DIR}/subject_check")

# What one window contributes: one entry, or one per tab. model/entries.hpp has no
# compositor in it, which is what makes the three modes checkable here.

add_test(NAME entries
         COMMAND ${CMAKE_COMMAND} -E env sh -c
                 "${CMAKE_CXX_COMPILER} -std=c++23 -Wall -Wextra -I ${CMAKE_SOURCE_DIR}/src ${CMAKE_SOURCE_DIR}/tests/entries_check.cpp -o ${CMAKE_CURRENT_BINARY_DIR}/entries_check && ${CMAKE_CURRENT_BINARY_DIR}/entries_check")

# The carousel slot: which tile the k-th slot holds. view/state.hpp needs no compositor on
# the include path.

add_test(NAME view_slots
         COMMAND ${CMAKE_COMMAND} -E env sh -c
                 "${CMAKE_CXX_COMPILER} -std=c++23 -Wall -Wextra -I ${CMAKE_SOURCE_DIR}/src ${CMAKE_SOURCE_DIR}/tests/view_slots_check.cpp -o ${CMAKE_CURRENT_BINARY_DIR}/view_slots_check && ${CMAKE_CURRENT_BINARY_DIR}/view_slots_check")

# view::State's mode rules. Header-only and Hyprland-free.

add_test(NAME view_state
         COMMAND ${CMAKE_COMMAND} -E env sh -c
                 "${CMAKE_CXX_COMPILER} -std=c++23 -Wall -Wextra -I ${CMAKE_SOURCE_DIR}/src ${CMAKE_SOURCE_DIR}/tests/view_state_check.cpp -o ${CMAKE_CURRENT_BINARY_DIR}/view_state_check && ${CMAKE_CURRENT_BINARY_DIR}/view_state_check")

# Strip::cardAt. model/strip.hpp needs no compositor, so the strip's own geometry is
# checkable on its own.

add_test(NAME strip
         COMMAND ${CMAKE_COMMAND} -E env sh -c
                 "${CMAKE_CXX_COMPILER} -std=c++23 -Wall -Wextra -I ${CMAKE_SOURCE_DIR}/src ${CMAKE_SOURCE_DIR}/tests/strip_check.cpp -o ${CMAKE_CURRENT_BINARY_DIR}/strip_check && ${CMAKE_CURRENT_BINARY_DIR}/strip_check")

# The run's open/close position. progress.hpp is header-only and Hyprland-free.

add_test(NAME progress
         COMMAND ${CMAKE_COMMAND} -E env sh -c
                 "${CMAKE_CXX_COMPILER} -std=c++23 -Wall -Wextra -I ${CMAKE_SOURCE_DIR}/src ${CMAKE_SOURCE_DIR}/tests/progress_check.cpp -o ${CMAKE_CURRENT_BINARY_DIR}/progress_check && ${CMAKE_CURRENT_BINARY_DIR}/progress_check")

# Key-combo parsing. key_combo.hpp is Hyprland-free so the whole rule -- extra modifiers
# fail the match -- is checkable without a compositor. keys.cpp static_asserts the
# modifier bits against Hyprland's enum, so those cannot drift.

add_test(NAME key_combo
         COMMAND ${CMAKE_COMMAND} -E env sh -c
                 "${CMAKE_CXX_COMPILER} -std=c++23 -Wall -Wextra -I ${CMAKE_SOURCE_DIR}/src ${CMAKE_SOURCE_DIR}/tests/key_combo_check.cpp -o ${CMAKE_CURRENT_BINARY_DIR}/key_combo_check && ${CMAKE_CURRENT_BINARY_DIR}/key_combo_check")

# The above-overview namespace parse. layer_names.hpp is Hyprland-free and reads no config,
# so this is the same file the plugin compiles.

add_test(NAME layer_names
         COMMAND ${CMAKE_COMMAND} -E env sh -c
                 "${CMAKE_CXX_COMPILER} -std=c++23 -Wall -Wextra -I ${CMAKE_SOURCE_DIR}/src ${CMAKE_SOURCE_DIR}/tests/layer_names_check.cpp -o ${CMAKE_CURRENT_BINARY_DIR}/layer_names_check && ${CMAKE_CURRENT_BINARY_DIR}/layer_names_check")

# The render geometry that computes boxes and draws nothing. Header-only and Hyprland-free,
# so the same file the plugin compiles is the file the test compiles.
add_test(NAME render_geometry
         COMMAND ${CMAKE_COMMAND} -E env sh -c
                 "${CMAKE_CXX_COMPILER} -std=c++23 -Wall -Wextra -I ${CMAKE_SOURCE_DIR}/src ${CMAKE_SOURCE_DIR}/tests/render_geometry_check.cpp -o ${CMAKE_CURRENT_BINARY_DIR}/render_geometry_check && ${CMAKE_CURRENT_BINARY_DIR}/render_geometry_check")

# Which desktop entry and which icon file a window class resolves to. Pure filesystem, so the
# test points HOME and XDG at a tree of its own.
add_test(NAME app_icon
         COMMAND ${CMAKE_COMMAND} -E env sh -c
                 "${CMAKE_CXX_COMPILER} -std=c++23 -Wall -Wextra -I ${CMAKE_SOURCE_DIR}/src ${CMAKE_SOURCE_DIR}/tests/app_icon_check.cpp -o ${CMAKE_CURRENT_BINARY_DIR}/app_icon_check && ${CMAKE_CURRENT_BINARY_DIR}/app_icon_check")

# A shadow is a blurred silhouette: the check reads the mask itself, so a blur that was skipped
# (a hard ring) and a colour in the wrong channel both redden here.
add_test(NAME shadow_mask
         COMMAND ${CMAKE_COMMAND} -E env sh -c
                 "${CMAKE_CXX_COMPILER} -std=c++23 -Wall -Wextra -I ${CMAKE_SOURCE_DIR}/src ${CMAKE_SOURCE_DIR}/tests/shadow_check.cpp ${CMAKE_SOURCE_DIR}/src/render/shadow.cpp -o ${CMAKE_CURRENT_BINARY_DIR}/shadow_check $(pkg-config --cflags --libs cairo) && ${CMAKE_CURRENT_BINARY_DIR}/shadow_check")

# The caption's own decision: where the pills land, which edge the band thickens,
# how far the caption leaves a card that reached it. Six fixes landed inline in
# drawTile, where the only observer was the screen; here the same header decides
# it, with no GL and no Hyprland.
add_test(NAME caption_geometry
         COMMAND ${CMAKE_COMMAND} -E env sh -c
                 "${CMAKE_CXX_COMPILER} -std=c++23 -Wall -Wextra -I ${CMAKE_SOURCE_DIR}/src ${CMAKE_SOURCE_DIR}/tests/caption_check.cpp -o ${CMAKE_CURRENT_BINARY_DIR}/caption_check && ${CMAKE_CURRENT_BINARY_DIR}/caption_check")

# The order of the passes in paint(), against the order AGENTS.md 3.4 documents.
# The z-order is the rule, and it is written down in two places; this says they are
# the same order.
add_test(NAME scan_paint_order
         COMMAND ${CMAKE_COMMAND} -E env sh -c
                 "${CMAKE_CXX_COMPILER} -std=c++23 -Wall -Wextra ${CMAKE_SOURCE_DIR}/tests/paint_order_check.cpp -o ${CMAKE_CURRENT_BINARY_DIR}/paint_order_check && ${CMAKE_CURRENT_BINARY_DIR}/paint_order_check ${CMAKE_SOURCE_DIR}")

# The start boxes of a rebuilt deck: one pure function, asked for every tile. The rule
# was in the model while the caller skipped it on an empty capture.
add_test(NAME scan_start_boxes
         COMMAND ${CMAKE_COMMAND} -E env sh -c
                 "${CMAKE_CXX_COMPILER} -std=c++23 -Wall -Wextra ${CMAKE_SOURCE_DIR}/tests/start_boxes_check.cpp -o ${CMAKE_CURRENT_BINARY_DIR}/start_boxes_check && ${CMAKE_CURRENT_BINARY_DIR}/start_boxes_check ${CMAKE_SOURCE_DIR}")

# The badge's schedule and the icon's shadow: one rule each, and the call site must use it.
add_test(NAME scan_badge_icon
         COMMAND ${CMAKE_COMMAND} -E env sh -c
                 "${CMAKE_CXX_COMPILER} -std=c++23 -Wall -Wextra ${CMAKE_SOURCE_DIR}/tests/badge_icon_check.cpp -o ${CMAKE_CURRENT_BINARY_DIR}/badge_icon_check && ${CMAKE_CURRENT_BINARY_DIR}/badge_icon_check ${CMAKE_SOURCE_DIR}")

# The strip band fades on the cards' own number, never on the exit progress.
add_test(NAME scan_strip_band
         COMMAND ${CMAKE_COMMAND} -E env sh -c
                 "${CMAKE_CXX_COMPILER} -std=c++23 -Wall -Wextra ${CMAKE_SOURCE_DIR}/tests/strip_band_check.cpp -o ${CMAKE_CURRENT_BINARY_DIR}/strip_band_check && ${CMAKE_CURRENT_BINARY_DIR}/strip_band_check ${CMAKE_SOURCE_DIR}")

# A cold class icon: resolved while the scene is built, never inside paint.
add_test(NAME scan_icon_warm
         COMMAND ${CMAKE_COMMAND} -E env sh -c
                 "${CMAKE_CXX_COMPILER} -std=c++23 -Wall -Wextra ${CMAKE_SOURCE_DIR}/tests/icon_warm_check.cpp -o ${CMAKE_CURRENT_BINARY_DIR}/icon_warm_check && ${CMAKE_CURRENT_BINARY_DIR}/icon_warm_check ${CMAKE_SOURCE_DIR}")

# The backdrop's base: the blur, the dim, or the sharp source only with the blur off.
# A sharp frame under a transparent window was the one-frame flash of the desktop.
add_test(NAME scan_backdrop_base
         COMMAND ${CMAKE_COMMAND} -E env sh -c
                 "${CMAKE_CXX_COMPILER} -std=c++23 -Wall -Wextra ${CMAKE_SOURCE_DIR}/tests/backdrop_base_check.cpp -o ${CMAKE_CURRENT_BINARY_DIR}/backdrop_base_check && ${CMAKE_CURRENT_BINARY_DIR}/backdrop_base_check ${CMAKE_SOURCE_DIR}")

# The box a preview's surface element reports: the drawn box. Hyprland clips damage to
# the element's own box, which ignores a render modif, so the preview kept only the part
# lying over the window's real spot.
add_test(NAME scan_preview_box
         COMMAND ${CMAKE_COMMAND} -E env sh -c
                 "${CMAKE_CXX_COMPILER} -std=c++23 -Wall -Wextra -I ${CMAKE_SOURCE_DIR}/src ${CMAKE_SOURCE_DIR}/tests/preview_box_check.cpp -o ${CMAKE_CURRENT_BINARY_DIR}/preview_box_check && ${CMAKE_CURRENT_BINARY_DIR}/preview_box_check ${CMAKE_SOURCE_DIR}")

# How a tile is drawn: the grid, the run, the dragged card. Three builders, so a
# field set in one path and forgotten in another is impossible.
add_test(NAME tile_look
         COMMAND ${CMAKE_COMMAND} -E env sh -c
                 "${CMAKE_CXX_COMPILER} -std=c++23 -Wall -Wextra -I ${CMAKE_SOURCE_DIR}/src ${CMAKE_SOURCE_DIR}/tests/tile_look_check.cpp -o ${CMAKE_CURRENT_BINARY_DIR}/tile_look_check && ${CMAKE_CURRENT_BINARY_DIR}/tile_look_check")

# The close glyph is drawn by one rule, on every surface that draws it.
add_test(NAME scan_close_glyph
         COMMAND ${CMAKE_COMMAND} -E env sh -c
                 "${CMAKE_CXX_COMPILER} -std=c++23 -Wall -Wextra ${CMAKE_SOURCE_DIR}/tests/close_glyph_check.cpp -o ${CMAKE_CURRENT_BINARY_DIR}/close_glyph_check && ${CMAKE_CURRENT_BINARY_DIR}/close_glyph_check ${CMAKE_SOURCE_DIR}")

# The GL state fence's comparison. Reading live GL needs a context, so that half lives in
# gl_util.hpp and runs inside the pass; the comparison is POD arithmetic, split into
# state_fence.hpp precisely so it compiles here with no GL header at all.
#
# This covers the half of GL discipline that had no check: scan_draw_sites catches a raw draw
# outside the helpers, and nothing caught "left the state changed". Two such defects shipped
# -- backdrop leaving blend enabled, blur calling raw glDisable -- and both were found by
# reading code, under a comment claiming they were verified.
add_test(NAME gl_state
         COMMAND ${CMAKE_COMMAND} -E env sh -c
                 "${CMAKE_CXX_COMPILER} -std=c++23 -Wall -Wextra -I ${CMAKE_SOURCE_DIR}/src ${CMAKE_SOURCE_DIR}/tests/gl_state_check.cpp -o ${CMAKE_CURRENT_BINARY_DIR}/gl_state_check && ${CMAKE_CURRENT_BINARY_DIR}/gl_state_check")
         add_test(NAME load_seam
         COMMAND bash ${CMAKE_SOURCE_DIR}/tests/load_seam_check.sh
                 $<TARGET_FILE:hyprview> ${HYPRLAND_BIN}
         SKIP_RETURN_CODE 77)

# Hook signatures against the running compositor's exports. A mismatch makes
# findFunctionsByName return nothing and initialize() fail, and hyprpm's plugin.load
# swallows that, so the symptom reads as "my config did not take".
add_test(NAME hook_signatures
         COMMAND bash ${CMAKE_SOURCE_DIR}/tests/hook_signature_check.sh
                 $<TARGET_FILE:hyprview> ${HYPRLAND_BIN}
                 ${CMAKE_SOURCE_DIR}/src/session/hooks.cpp
         SKIP_RETURN_CODE 77)

add_custom_target(reload
  COMMAND bash -c "hyprctl hyprviewunload || true"
  COMMAND ${CMAKE_COMMAND} -E sleep 0.15
  COMMAND bash -c "hyprctl plugin unload $<TARGET_FILE:hyprview> || true"
  # Also drop a hyprpm-installed copy: it autoloads at session start and already holds the
  # shouldRenderWindow hooks, so loading the build path next to it fails to hook (two
  # instances can't trampoline the same prologue). No-op when not installed/loaded.
  # ($ENV{USER} resolves at configure time — a literal $USER gets mangled by the Makefile.)
  COMMAND bash -c "hyprctl plugin unload /var/cache/hyprpm/$ENV{USER}/hyprview/hyprview.so || true"
  COMMAND hyprctl plugin load $<TARGET_FILE:hyprview>
  DEPENDS hyprview
  USES_TERMINAL
  COMMENT "Hot-reloading hyprview into the running Hyprland"
)
