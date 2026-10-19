#include "cursor.hpp"

#include <algorithm>
#include "config/config.hpp"
#include "render/gl_util.hpp"
#include <hyprutils/math/Region.hpp>

using Render::GL::g_pHyprOpenGL;

namespace gloview {

void CCursorModule::onOpen(PHLMONITOR monitor) {
    resetCursorBox();
    (void)monitor;
}

void CCursorModule::onClose() {
    resetCursorBox();
}

bool CCursorModule::hasHardwareCursor(PHLMONITOR monitor) const {
    if (!monitor || !Pointer::mgr())
        return false;
    // Read the config where it is used, every time (AGENTS.md 2.16): caching it made this the one
    // setting that needed the overview reopened.
    if (cfg::behavior.cursor_mode.get() == "software")
        return false;
    return Pointer::mgr()->hasVisibleHWCursor(monitor);
}

CBox CCursorModule::moveDamage(PHLMONITOR monitor) const {
    if (!monitor || !Pointer::mgr())
        return CBox{};

    const CBox cur = Pointer::mgr()->getCursorBoxGlobal();

    if (!m_haveLastBox) {
        // this path is monitor-local; the union below is global
        return CBox{cur.x - monitor->m_position.x, cur.y - monitor->m_position.y,
                    cur.w, cur.h};
    }

    const double x0 = std::min(m_lastBox.x, cur.x);
    const double y0 = std::min(m_lastBox.y, cur.y);
    const double x1 = std::max(m_lastBox.x + m_lastBox.w, cur.x + cur.w);
    const double y1 = std::max(m_lastBox.y + m_lastBox.h, cur.y + cur.h);
    const CBox u{x0, y0, x1 - x0, y1 - y0};

    return u;
}

void CCursorModule::renderOnTop(PHLMONITOR monitor, const CHyprColor& backdropColor) const {
    // Every exit below NAMED, per frame: CURSOR|mode=hw|no_monitor|no_tex|empty_box|draw|... .
    // mode=hw means we drew nothing on the compositor's claim of a hardware cursor above us.
    const auto bail = [&](const char* why) {
        debug::trace::emitState(std::string("CURSOR|mode=") + why);
    };
    if (!monitor || !Pointer::mgr() || !g_pHyprOpenGL)
        return bail("no_monitor");

    // HW cursor is composited above us: a software copy would trail
    if (hasHardwareCursor(monitor)) {
        // The plane must be repositioned even when we draw nothing: the mouse path bailed here.
        if (Pointer::mgr())
            Pointer::mgr()->damageCursor(monitor);
        return bail("hw");
    }

    const auto tex = Pointer::mgr()->getCurrentCursorTexture();
    if (!tex)
        return bail("no_tex");

    const CBox g = Pointer::mgr()->getCursorBoxGlobal();
    if (g.w <= 0 || g.h <= 0)
        return bail("empty_box");

    // pixels, not logical: projection is set per monitor
    const CBox gpl{
        (g.x - monitor->m_position.x) * monitor->m_scale,
        (g.y - monitor->m_position.y) * monitor->m_scale,
        g.w * monitor->m_scale,
        g.h * monitor->m_scale,
    };
    const CBox curPx = CBox{std::round(gpl.x), std::round(gpl.y),
                            std::round(gpl.w), std::round(gpl.h)};

    // erase with an OPAQUE backdrop copy: the backdrop only blends
    const bool erasing = m_haveLastBox && m_lastBox != g;
    if (erasing) {
        const CBox lastPl{
            (m_lastBox.x - monitor->m_position.x) * monitor->m_scale,
            (m_lastBox.y - monitor->m_position.y) * monitor->m_scale,
            m_lastBox.w * monitor->m_scale,
            m_lastBox.h * monitor->m_scale,
        };
        const CBox lastPx = CBox{std::round(lastPl.x), std::round(lastPl.y),
                                 std::round(lastPl.w), std::round(lastPl.h)};
        safeRenderRect("cursor", lastPx,
                                  CHyprColor(backdropColor.r, backdropColor.g,
                                             backdropColor.b, 1.0),
                                  {});
    }

    g_pHyprOpenGL->renderTexture(tex, curPx, {.a = 1.0F});

    debug::trace::emitState("CURSOR|mode=draw|x=" + std::to_string(curPx.x) +
                            ",y=" + std::to_string(curPx.y) +
                            ",w=" + std::to_string(curPx.w) +
                            ",h=" + std::to_string(curPx.h) +
                            "|erase=" + (erasing ? "1" : "0"));

    m_lastBox     = g;
    m_haveLastBox = true;
}

}
