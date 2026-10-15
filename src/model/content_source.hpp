#pragma once

#include <string>
#include <vector>

#include "subject.hpp"

namespace gloview::model {

// What the overview may ask about, and ask to have done, for anything it shows.
//
// The arrow points one way. session/, build/, input/, actions/ and render/ ask THIS and
// never reach for a window handle; the compositor is reached only inside an implementation.
// That is the whole point of the file, and it is the reason it has no Hyprland and no GL in
// it: both would make it untestable, and a boundary nobody can test erodes in a single
// commit.
//
// It is an interface rather than a branch on "is this a tab". A branch leaves both paths
// asking the same question side by side, and one of them drifts -- that has already happened
// twice in this codebase over animation styles and over card depth. With two
// implementations, the polymorphism pays for itself.
//
// Deliberately NOT here: anything that produces a texture. Content is a render concern and
// it needs a GL type; a seam that had to name one could not be built by ctest. See
// render/content, which is where that half lives.
//
// Deliberately NOT here either: the Hyprland hooks. shouldRenderWindow hides real windows,
// and in tabs mode there is nothing of ours to hide, so those hooks must go quiet when the
// active domain is not Windows -- which is a decision for the implementation, not a question
// to ask of this interface.
class IContentSource {
  public:
    virtual ~IContentSource() = default;

    // Everything on the run right now, in the order it should be laid out.
    //
    // NOT const, and that is load-bearing. Enumerating is what refreshes what the source
    // holds alive, and holding it alive is the only reason a SubjectRef can be turned back
    // into something usable: the identity is a NUMBER, so the ownership has to live
    // somewhere, and it lives here. A const enumerate() would push that into a mutable
    // member and hide the mutation rather than admit it -- same hazard, smaller hat.
    //
    // Consequently an identity from an OLD enumeration stops resolving the moment a newer
    // one is taken. That is the point: "it went away" and "resurrected a freed pointer" must
    // not look the same from the caller's side, and one of them is a segfault.
    [[nodiscard]] virtual std::vector<SubjectRef> enumerate() = 0;

    // Where a subject lives: a workspace for a window, a browser window for a tab. The
    // hit-test and the strip both ask this instead of reaching for a workspace handle.
    [[nodiscard]] virtual GroupRef groupOf(SubjectRef s) const = 0;

    // What the caption says. Empty is allowed; the caller decides what to show then.
    [[nodiscard]] virtual std::string label(SubjectRef s) const = 0;

    // Is this a tab rather than a window? Two subjects with the same id in different domains
    // must not be confused, and this is where that is asked once.
    [[nodiscard]] virtual bool isTab(SubjectRef s) const = 0;

    // The three verbs the overview performs on a subject. Each returns false when it did not
    // happen -- a window closed itself, a tab the extension had already dropped -- and the
    // caller treats that as "do not animate towards something that is gone".
    virtual bool activate(SubjectRef s) = 0;
    virtual bool close(SubjectRef s) = 0;
    virtual bool moveTo(SubjectRef s, GroupRef target) = 0;
};

} // namespace gloview::model