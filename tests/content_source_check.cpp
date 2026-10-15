// The content-source contract, proven implementable twice.
//
// The claim this file exists to check: model/content_source.hpp is a boundary. Two
// independent implementations can be written against it -- one per domain -- and neither
// needs the other's types, the compositor, or a GL context.
//
// That is the difference between an interface and a switch statement. A switch would have
// two branches asking "which domain is this" in the same functions, and the two would drift.
// Two implementations of one contract cannot drift into agreement-by-accident.
//
// model/content_source.hpp has no Hyprland and no GL, so this compiles with nothing but the
// standard library. If either of those ever creeps in, this stops compiling -- which is the
// guard, not an accident.

#include <cstdio>
#include <cstdlib>
#include <string>
#include <map>
#include <set>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include "../src/model/content_source.hpp"

using gloview::model::GroupRef;
using gloview::model::IContentSource;
using gloview::model::SubjectRef;

namespace {

int g_checks = 0;

void want(bool got, bool expected, const char *what) {
  ++g_checks;
  if (got != expected) {
    std::printf("FAIL %s: got %d, want %d\n", what, got, expected);
    std::exit(1);
  }
}

// --- implementation one: windows ---------------------------------------------------------
// Identities are opaque numbers here. Nothing in the interface says what an id MEANS, which
// is what lets this one pretend to be a compositor handle while the other pretends to be a
// browser tab id.
class FakeWindows final : public IContentSource {
  public:
    std::vector<SubjectRef> enumerate() override { return m_order; }

    GroupRef groupOf(SubjectRef s) const override {
      const auto it = m_group.find(s.id);
      return it == m_group.end() ? GroupRef{} : it->second;
    }

    std::string label(SubjectRef s) const override {
      const auto it = m_label.find(s.id);
      return it == m_label.end() ? std::string{} : it->second;
    }

    bool isTab(SubjectRef) const override { return false; }

    bool activate(SubjectRef s) override {
        if (!m_alive.count(s.id))
            return false;
        m_activated = s.id;
        return true;
    }
    bool close(SubjectRef s) override {
        if (!m_alive.erase(s.id))
            return false;
        m_closed = s.id;
        return true;
    }
    bool moveTo(SubjectRef s, GroupRef g) override {
        if (!m_alive.count(s.id))
            return false;
        m_group[s.id] = g;
        return true;
    }

    // recorded, so the caller side can be checked too
    std::uint64_t activated() const { return m_activated; }
    std::uint64_t closed() const { return m_closed; }

    std::vector<SubjectRef> m_order;
    std::unordered_map<std::uint64_t, GroupRef> m_group;
    std::unordered_map<std::uint64_t, std::string> m_label;
    std::unordered_set<std::uint64_t> m_alive;
    std::uint64_t m_activated = 0, m_closed = 0;
};

// --- implementation two: browser tabs ------------------------------------------------------
// Same contract, unrelated types. The epochs differ, because a browser issues its ids under
// an epoch it owns and the compositor never issues one at all.
class FakeTabs final : public IContentSource {
  public:
    std::vector<SubjectRef> enumerate() override { return m_order; }

    GroupRef groupOf(SubjectRef s) const override {
      for (const auto &[g, ids] : m_groups)
        if (ids.count(s.id))
          return g;
      return GroupRef{};
    }

    std::string label(SubjectRef s) const override {
      for (const auto &[k, v] : m_label)
        if (k == s)
          return v;
      return {};
    }

    bool isTab(SubjectRef) const override { return true; }

    bool alive(SubjectRef s) const {
        for (const auto &a : m_alive)
            if (a == s)
                return true;
        return false;
    }
    bool activate(SubjectRef s) override {
        if (!alive(s))
            return false;
        m_last = s;
        return true;
    }
    bool close(SubjectRef s) override {
        if (!alive(s))
            return false;
        for (auto it = m_alive.begin(); it != m_alive.end(); ++it)
            if (*it == s) {
                m_alive.erase(it);
                break;
            }
        m_last = s;
        return true;
    }
    bool moveTo(SubjectRef s, GroupRef g) override {
        if (!alive(s))
            return false;
        for (auto &[existing, ids] : m_groups)
            ids.erase(s.id);
        for (auto &[existing, ids] : m_groups) {
            if (existing == g) {
                ids.insert(s.id);
                return true;
            }
        }
        m_groups.emplace_back(g, std::set<std::uint64_t>{s.id});
        return true;
    }

    SubjectRef m_last;

    std::vector<SubjectRef> m_order;
    std::vector<std::pair<GroupRef, std::set<std::uint64_t>>> m_groups;
    std::vector<std::pair<SubjectRef, std::string>> m_label;
    std::vector<SubjectRef> m_alive;
};

// The caller side, written once against the contract and run against both. This is what
// proves the interface is enough -- if it needs a domain-specific question that is not on
// it, this will not compile.
void drive(IContentSource &src, std::uint64_t expect_id, bool expect_is_tab) {
    const auto all = src.enumerate();
    want(!all.empty(), true, "enumerate returns something");

    const auto first = all.front();
    want(first.id == expect_id, true, "the first subject is the one we put in");

    want(src.isTab(first) == expect_is_tab, true, "isTab agrees with the domain");
    want(!src.label(first).empty(), true, "a label comes back");

    const auto g = src.groupOf(first);
    want(g.id != 0, true, "a group comes back");

    want(src.activate(first), true, "activate succeeds on a live subject");
    want(src.close(first), true, "close succeeds on a live subject");
    // ...and now it is gone, which is the case the caller has to survive.
    want(src.activate(first), false, "activate on a closed subject reports it did nothing");
}

} // namespace

int main() {
    FakeWindows w;
    w.m_order = {SubjectRef{10, 0}, SubjectRef{20, 0}};
    w.m_alive = {10, 20};
    w.m_group[10] = GroupRef{1, 0};
    w.m_group[20] = GroupRef{2, 0};
    w.m_label[10] = "Terminal";
    w.m_label[20] = "Files";

    FakeTabs t;
    t.m_order = {SubjectRef{7, 5}, SubjectRef{8, 5}};
    t.m_alive = {SubjectRef{7, 5}, SubjectRef{8, 5}};
    t.m_groups.emplace_back(GroupRef{100, 1}, std::set<std::uint64_t>{7, 8});
    t.m_label = {{SubjectRef{7, 5}, "docs"}, {SubjectRef{8, 5}, "mail"}};

    drive(w, 10, false);
    drive(t, 7, true);

    // The two must not collide. Window 7 and tab 7 are different subjects, and a caller
    // holding both at once -- which both mode does -- must still tell them apart.
    {
        const SubjectRef win7{7, 0};
        const SubjectRef tab7{7, 5};
        want(win7 != tab7, true, "window 7 and tab 7 differ");
        want(w.isTab(win7) == false && t.isTab(tab7) == true, true,
             "each implementation claims only its own");
        // And the same id under two epochs of the SAME domain must not collide either.
        want(SubjectRef{7, 5} != SubjectRef{7, 6}, true, "two epochs, two subjects");
    }

    // Labels are keyed differently by each implementation, and both answer correctly --
    // one by bare id, the other by the whole SubjectRef. The interface does not care which,
    // and that is the point: a tab id 7 and a window id 7 are the same number.
    {
        want(t.label(SubjectRef{8, 5}) == "mail", true, "tab label by full ref");
        want(t.label(SubjectRef{8, 6}) == "", true, "an unknown epoch has no label");
        want(w.label(SubjectRef{20, 0}) == "Files", true, "window label by id");
        want(w.label(SubjectRef{99, 0}) == "", true, "an unknown window has no label");
    }

    // moveTo relocates, and a subject that is gone cannot be moved -- which is what lets the
    // drop path skip the flight instead of animating towards nothing.
    {
        FakeTabs m;
        m.m_alive = {SubjectRef{7, 5}};
        want(m.moveTo(SubjectRef{7, 5}, GroupRef{200, 1}), true, "move a live tab");
        want(m.groupOf(SubjectRef{7, 5}) == GroupRef{200, 1}, true, "and it landed there");
        want(m.moveTo(SubjectRef{7, 5}, GroupRef{300, 1}), true, "a moved tab is still alive");
        m.m_alive.clear();
        want(m.moveTo(SubjectRef{7, 5}, GroupRef{400, 1}), false, "a gone tab cannot move");
    }

    // An empty source is not an error. It is what the plugin sees before the extension has
    // ever connected, and enumerate() must answer empty rather than throw or invent one.
    {
        FakeTabs none;
        want(none.enumerate().empty(), true, "an empty source enumerates empty");
        want(none.label(SubjectRef{1, 1}).empty(), true, "and labels nothing");
        want(none.groupOf(SubjectRef{1, 1}) == GroupRef{0, 0}, true,
             "and groups into the default");
    }

    std::printf("content source: %d checks\n", g_checks);
    std::puts("content source ok");
    return 0;
}