// The handover runs BETWEEN frames, never inside the render pass.
//
// deactivate() moves focus and the workspace, and either one re-sets the client cursor:
// CPointerManager::setCursorSurface -> updateCursorBackend -> attemptHardwareCursor ->
// renderHWCursorBuffer (PointerManager.cpp:398,439), which is beginFullFakeRender + endRender.
// beginRender clears m_renderPass (Renderer.cpp:1744) while CRenderPass::render is walking it,
// and the nested endRender leaves m_renderData.pMonitor null (GLRenderer.cpp:100). The outer
// endRender reads that null and calls end() -> CMonitor::useFP16 on null. SIGSEGV 2026-11-10,
// two crash reports, both from the tail of paint().
//
// The rule is one scheduling call, and the words are what the compiler sees: the handover asks
// the event loop for an idle, and no file under src/render/ starts a deactivate at all.

#include <cstdio>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

namespace {

std::string slurp(const std::string &path) {
  std::ifstream f(path);
  if (!f)
    return {};
  return std::string((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
}

// The body of one function: from its signature to the brace that closes it.
std::string bodyOf(const std::string &src, const std::string &signature) {
  const auto at = src.find(signature);
  if (at == std::string::npos)
    return {};
  int depth = 0;
  bool seen = false;
  for (std::size_t i = at; i < src.size(); ++i) {
    if (src[i] == '{') {
      ++depth;
      seen = true;
    } else if (src[i] == '}') {
      --depth;
      if (seen && depth == 0)
        return src.substr(at, i - at + 1);
    }
  }
  return {};
}

} // namespace

int main(int argc, char **argv) {
  const std::string root = argc > 1 ? argv[1] : ".";
  int               failed = 0;

  const std::string LIFE = slurp(root + "/src/session/lifecycle.cpp");
  if (LIFE.empty()) {
    std::printf("FAIL: cannot read src/session/lifecycle.cpp\n");
    return 1;
  }

  const std::string HANDOVER = bodyOf(LIFE, "void Overview::finishPendingDeactivate()");
  if (HANDOVER.empty()) {
    std::printf("FAIL: no Overview::finishPendingDeactivate() body\n");
    failed = 1;
  } else if (HANDOVER.find("doLater(") == std::string::npos) {
    std::printf("FAIL: the handover does not schedule itself on the event loop; paint() runs "
                "inside the render pass\n");
    failed = 1;
  }

  std::vector<std::string> offenders;
  for (const auto &e : std::filesystem::recursive_directory_iterator(root + "/src/render")) {
    if (!e.is_regular_file())
      continue;
    const auto ext = e.path().extension().string();
    if (ext != ".cpp" && ext != ".hpp")
      continue;
    if (slurp(e.path().string()).find("deactivate(") != std::string::npos)
      offenders.push_back(std::filesystem::relative(e.path(), root).string());
  }
  for (const auto &f : offenders) {
    std::printf("FAIL: %s deactivates from render code\n", f.c_str());
    failed = 1;
  }

  if (!failed)
    std::printf("deactivate off-frame: handover scheduled, render/ starts none\n");
  return failed;
}
