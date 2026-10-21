#pragma once

#include <string>
#include <vector>

#include "subject.hpp"

namespace hyprview::model {

// Where a card ends up: a picked TAB by its group (its browser window), a window by its workspace.
[[nodiscard]] inline bool goesHome(GroupRef group, GroupRef landing, GroupRef landingGroup) {
  if (landing == GroupRef{})
    return true;
  if (landingGroup != GroupRef{})
    return group == landingGroup;
  return group == landing;
}

// Every consumer asks THIS and never reaches for a window handle. No Hyprland, no GL.
class IContentSource {
  public:
    virtual ~IContentSource() = default;

    // NOT const, load-bearing: this call refreshes the ownership a SubjectRef resolves through,
    // so an id from an old enumeration stops resolving.
    [[nodiscard]] virtual std::vector<SubjectRef> enumerate() = 0;

    [[nodiscard]] virtual GroupRef groupOf(SubjectRef s) const = 0;

    [[nodiscard]] virtual std::string label(SubjectRef s) const = 0;

    // Asked once here, so ids from two domains cannot be confused.
    [[nodiscard]] virtual bool isTab(SubjectRef s) const = 0;

    // Each returns false when it did not happen; the caller then does not animate towards a gone thing.
    virtual bool activate(SubjectRef s) = 0;
    virtual bool close(SubjectRef s) = 0;
    virtual bool moveTo(SubjectRef s, GroupRef target) = 0;
};

} // namespace hyprview::model
