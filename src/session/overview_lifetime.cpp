#include "../debug/log.hpp"
#include "../overview.hpp"
#include "hooks.hpp"

namespace hyprview {

Overview::Overview(HANDLE handle) : m_handle(handle) {}

Overview::~Overview() {
  m_life.hardClose();
  restoreLayers();
  restorePreviewVisibility();
  restoreFill();
  releaseNewWorkspaces();
  m_scene.clear();
  m_strip.clear();
  cancelPendingClick(); // no timer callback against a half-destroyed Overview
  // m_hooks removes the function hooks last: a hook body may still call back into a half-torn-down Overview.
}

bool Overview::initialize() { return m_hooks.install(m_handle, this); }

}
