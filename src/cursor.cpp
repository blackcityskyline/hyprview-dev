#include "cursor.hpp"

#include <algorithm>
#include "config/config.hpp"
#include "render/gl_util.hpp"
#include <hyprutils/math/Region.hpp>

using Render::GL::g_pHyprOpenGL;

namespace gloview {

// `mode` is gone, and so is m_softwareOnly: the config is read where it is used. Keeping the
// parameter would have kept the stale copy alive one call away from being used again.
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
    // Read the config where it is used, every time. It was cached into m_softwareOnly by
    // onOpen, so `set cursor_mode software` did nothing until the overview was reopened --
    // the one setting in the plugin that needed a restart, in a project where every other
    // one answers live (the tween durations set the precedent: config is read on the spot
    // so changing it mid-session takes effect on the next frame).
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
    // Every exit below NAMED, per frame. "The cursor is not visible in the overview but it
    // still moves focus" is five different early returns with the same symptom, and until
    // this existed the module wrote nothing at all -- so the only way to answer was to read
    // the source and pick one, which is how the wrong one gets fixed twice.
    //
    //   CURSOR|mode=hw|no_monitor|no_tex|empty_box|draw|x=..,y=..,w=..,h=..|erase=<0|1>
    //
    // `mode=hw` is the one worth reading twice: it means we deliberately drew nothing
    // because the compositor claims a hardware cursor sits above us, and that claim is
    // exactly what an invisible-but-working cursor disputes.
    const auto bail = [&](const char* why) {
        debug::trace::emitState(std::string("CURSOR|mode=") + why);
    };
    if (!monitor || !Pointer::mgr() || !g_pHyprOpenGL)
        return bail("no_monitor");

    // HW cursor is composited above us: a software copy would trail
    if (hasHardwareCursor(monitor)) {
        // But "we draw nothing" is not "the cursor is handled". The plane has to be
        // repositioned for this frame, and the only caller that does it for us was the mouse
        // path, which returned early on this same branch -- so on frames where the overview
        // did the damage instead of the pointer, nothing moved the plane. Issuing it here
        // too is the redundancy that makes the two paths independent.
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

    // The box is logged as DRAWN, in the same pixels the texture went to. A record that
    // only said "draw" would be true on a frame where the draw was clipped away by the
    // damage -- which is the other candidate for an invisible cursor, and the one a viewer
    // cannot rule out by eye.
    debug::trace::emitState("CURSOR|mode=draw|x=" + std::to_string(curPx.x) +
                            ",y=" + std::to_string(curPx.y) +
                            ",w=" + std::to_string(curPx.w) +
                            ",h=" + std::to_string(curPx.h) +
                            "|erase=" + (erasing ? "1" : "0"));

    m_lastBox     = g;
    m_haveLastBox = true;
}

}
