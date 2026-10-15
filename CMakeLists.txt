cmake_minimum_required(VERSION 3.19)
project(gloview VERSION "0.3.0" LANGUAGES CXX)

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
# Raw GL (glCreateShader/glDrawArrays…) for the plugin's own blur shader.
pkg_check_modules(GLES REQUIRED glesv2)
# Lua's pkg-config module name varies by distro: `lua` (Arch), `lua5.4`/`lua-5.4`
# (Debian/Nix), or `luajit` (what Hyprland itself links). Take the first that exists.
pkg_search_module(LUA REQUIRED lua lua5.4 lua-5.4 lua54 luajit)

add_library(gloview SHARED
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
)
set_target_properties(gloview PROPERTIES PREFIX "")  # produce gloview.so, not libgloview.so

target_include_directories(gloview PRIVATE ${HYPR_DEPS_INCLUDE_DIRS} ${LUA_INCLUDE_DIRS} ${GLES_INCLUDE_DIRS})
foreach(inc ${HYPR_DEPS_INCLUDE_DIRS})
  if(EXISTS "${inc}/hyprland/src")
    target_include_directories(gloview PRIVATE "${inc}/hyprland/src")
  endif()
endforeach()

target_link_directories(gloview PRIVATE ${HYPR_DEPS_LIBRARY_DIRS} ${LUA_LIBRARY_DIRS} ${GLES_LIBRARY_DIRS})
target_link_libraries(gloview PRIVATE ${HYPR_DEPS_LIBRARIES} ${LUA_LIBRARIES} ${GLES_LIBRARIES})

install(TARGETS gloview LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR})

# Dev hot-reload (official same-path unload;load):  cmake --build build --target reload
# This is the upstream-documented dev reload (Hyprland wiki, Plugins/Development):
#   hyprctl plugin unload /abs/plugin.so ; hyprctl plugin load /abs/plugin.so
# It ONLY picks up new code because of -fno-gnu-unique above: C++ static-init guard vars
# (_ZGV...) otherwise emit as STB_GNU_UNIQUE, which makes glibc mark the .so NODELETE so
# dlclose never unmaps it and the same-path load returns the CACHED old image (same dlopen
# handle) -> a silent stale reload. With the flag, dlclose truly unmaps and the same path
# re-mmaps the fresh build. (If you ever load a pre-flag binary, that image sticks NODELETE
# for the life of the Hyprland process and poisons its path until you restart Hyprland once.)
# `hyprctl gloviewunload` (Overview::hardClose) first does a synchronous, animation-free
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
                 $<TARGET_FILE:gloview> ${HYPRLAND_BIN}
         SKIP_RETURN_CODE 77)

# Hook signatures against the running compositor's exports. A mismatch makes
# findFunctionsByName return nothing and initialize() fail, and hyprpm's plugin.load
# swallows that, so the symptom reads as "my config did not take".
add_test(NAME hook_signatures
         COMMAND bash ${CMAKE_SOURCE_DIR}/tests/hook_signature_check.sh
                 $<TARGET_FILE:gloview> ${HYPRLAND_BIN}
                 ${CMAKE_SOURCE_DIR}/src/session/hooks.cpp
         SKIP_RETURN_CODE 77)

add_custom_target(reload
  COMMAND bash -c "hyprctl gloviewunload || true"
  COMMAND ${CMAKE_COMMAND} -E sleep 0.15
  COMMAND bash -c "hyprctl plugin unload $<TARGET_FILE:gloview> || true"
  # Also drop a hyprpm-installed copy: it autoloads at session start and already holds the
  # shouldRenderWindow hooks, so loading the build path next to it fails to hook (two
  # instances can't trampoline the same prologue). No-op when not installed/loaded.
  # ($ENV{USER} resolves at configure time — a literal $USER gets mangled by the Makefile.)
  COMMAND bash -c "hyprctl plugin unload /var/cache/hyprpm/$ENV{USER}/gloview/gloview.so || true"
  COMMAND hyprctl plugin load $<TARGET_FILE:gloview>
  DEPENDS gloview
  USES_TERMINAL
  COMMENT "Hot-reloading gloview into the running Hyprland"
)
