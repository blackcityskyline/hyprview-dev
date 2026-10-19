#pragma once

#include <string>
#include <vector>

#include "subject.hpp"

namespace gloview::model {

// Every consumer asks THIS and never reaches for a window handle; the compositor is reached
// only inside an implementation. No Hyprland and no GL, so ctest can build the seam.
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

} // namespace gloview::model
