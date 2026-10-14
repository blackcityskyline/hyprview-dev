#include "../debug/log.hpp"
#include "../overview.hpp"
#include "hooks.hpp"

namespace gloview {

Overview::Overview(HANDLE handle) : m_handle(handle) {}

Overview::~Overview() {
  m_life.hardClose();
  restoreLayers();
  restorePreviewVisibility();
  restoreFill();
  releaseNewWorkspaces();
  if (m_animPump) {
    m_animPump->cancel();
    m_animPump.reset();
  }
  m_scene.clear();
  m_strip.clear();
  cancelPendingClick(); // no timer callback against a half-destroyed Overview
  // m_hooks removes the function hooks on destruction, below every other teardown: a hook
  // body may still call back into a half-torn-down Overview.
}

bool Overview::initialize() { return m_hooks.install(m_handle, this); }

} // namespace gloview
