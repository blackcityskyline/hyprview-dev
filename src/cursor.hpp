#pragma once

#include <hyprland/src/desktop/DesktopTypes.hpp>
#include <hyprland/src/helpers/math/Math.hpp>
#include <hyprland/src/render/OpenGL.hpp>
#include <hyprland/src/render/Renderer.hpp>
#include <hyprland/src/pointer/PointerManager.hpp>
#include <hyprland/src/pointer/cursor/CursorShapeOverrideController.hpp>
#include <hyprutils/math/Box.hpp>

using CBox = Hyprutils::Math::CBox;
struct CHyprColor;

namespace hyprview {

// HW plane: above framebuffer, no trails
class CCursorModule {
public:
    CCursorModule()  = default;
    ~CCursorModule() = default;

    void onOpen(PHLMONITOR monitor);
    void onClose();

    // software copy, drawn after every pass
    void renderOnTop(PHLMONITOR monitor) const;

    CBox moveDamage(PHLMONITOR monitor) const;

    bool hasHardwareCursor(PHLMONITOR monitor) const;

    void resetCursorBox() { m_haveLastBox = false; }

private:
    mutable CBox m_lastBox{};
    mutable bool m_haveLastBox = false;
};

}
