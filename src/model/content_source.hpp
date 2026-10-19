#pragma once

#include <string>
#include <vector>

#include "subject.hpp"

namespace gloview::model {

// What the overview may ask of anything it shows. The arrow points one way: every consumer
// asks THIS and never reaches for a window handle; the compositor is reached only inside an
// implementation. No Hyprland and no GL, so ctest can build the seam. No texture-producing
// verbs: they need a GL type and live in render/content.
class IContentSource {
  public:
    virtual ~IContentSource() = default;

    // Everything on the run now, in layout order. NOT const, load-bearing: the identity is a
    // number, so this call is what refreshes the ownership a SubjectRef resolves through. An
    // id from an old enumeration stops resolving -- "it went away" and "resurrected a freed
    // pointer" must not look alike; one of them is a segfault.
    [[nodiscard]] virtual std::vector<SubjectRef> enumerate() = 0;

    // Where a subject lives: a workspace for a window, a browser window for a tab.
    [[nodiscard]] virtual GroupRef groupOf(SubjectRef s) const = 0;

    // What the caption says. Empty is allowed; the caller decides what to show then.
    [[nodiscard]] virtual std::string label(SubjectRef s) const = 0;

    // Asked once here, so ids from two domains cannot be confused.
    [[nodiscard]] virtual bool isTab(SubjectRef s) const = 0;

    // Each returns false when it did not happen; the caller then does not animate towards
    // something that is gone.
    virtual bool activate(SubjectRef s) = 0;
    virtual bool close(SubjectRef s) = 0;
    virtual bool moveTo(SubjectRef s, GroupRef target) = 0;
};

} // namespace gloview::model