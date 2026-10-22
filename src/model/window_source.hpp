#pragma once

#include <hyprland/src/desktop/Workspace.hpp>
#include <hyprland/src/desktop/view/Window.hpp>

#include "content_source.hpp"
#include "window_host.hpp"

namespace hyprview::model {

// A window IS its address and the epoch is always 0, so a tabs SubjectRef would have to carry
// an id that happens to be a valid pointer to be mistaken for one.
class WindowsContentSource final : public IContentSource {
  public:
    explicit WindowsContentSource(const IWindowHost &host) : m_host(host) {}

    [[nodiscard]] std::vector<SubjectRef> enumerate() override;

    // Never rebuilt from the id: that is a use-after-free and it took the compositor down once (CSharedPointer<CWindow>::_delete -> ~CSharedPointer -> Overview::buildTiles).
    [[nodiscard]] PHLWINDOW windowOf(SubjectRef s) const;

    void release() { m_alive.clear(); }

    [[nodiscard]] GroupRef groupOf(SubjectRef s) const override;

    // Title, falling back to class; dots in the class ("kitty.kdots") are a convention.
    [[nodiscard]] std::string label(SubjectRef s) const override;

    [[nodiscard]] bool isTab(SubjectRef s) const override;

    bool activate(SubjectRef s) override;
    bool close(SubjectRef s) override;
    bool moveTo(SubjectRef s, GroupRef target) override;

  private:
    const IWindowHost &m_host;
    // Strong references taken at the last enumerate(); windowOf hands out copies.
    std::vector<SP<Desktop::View::CWindow>> m_alive;
};

} // namespace hyprview::model
