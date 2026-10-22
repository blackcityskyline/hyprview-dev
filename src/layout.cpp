#include "layout.hpp"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <numeric>

namespace hyprview {

LRect inset(const LRect& a, const LayoutCfg& c) {
    return LRect{a.x + c.padLeft, a.y + c.padTop, std::max(1.0, a.w - c.padLeft - c.padRight), std::max(1.0, a.h - c.padTop - c.padBottom)};
}

namespace {

std::vector<int> rowCounts(int n, int rows) {
    std::vector<int> out(rows, n / rows);
    for (int i = 0; i < n % rows; ++i)
        ++out[i];
    return out;
}

struct RowPlan {
    double           scale = 0.0;
    std::vector<int> counts;
};

RowPlan planForRows(const std::vector<LRect>& w, const LRect& area, const LayoutCfg& cfg, int rows) {
    const auto counts = rowCounts(static_cast<int>(w.size()), rows);

    double sW         = cfg.maxScale;
    double totalH     = 0.0;
    double totalGapsH = std::max(0, rows - 1) * cfg.gap;
    int    idx        = 0;
    for (int r = 0; r < rows; ++r) {
        double rowContentW = 0.0, rowH = 1.0;
        for (int c = 0; c < counts[r]; ++c, ++idx) {
            rowContentW += w[idx].w;
            rowH = std::max(rowH, w[idx].h);
        }
        const double rowGapsW = std::max(0, counts[r] - 1) * cfg.gap;
        const double availW   = std::max(1.0, area.w - rowGapsW);
        sW                    = std::min(sW, availW / std::max(1.0, rowContentW));
        totalH += rowH;
    }
    const double availH = std::max(1.0, area.h - totalGapsH);
    const double s      = std::max(0.01, std::min({sW, availH / std::max(1.0, totalH), cfg.maxScale}));
    return {s, counts};
}

std::vector<LRect> layoutRows(const std::vector<LRect>& w, const LRect& area, const LayoutCfg& cfg) {
    const int n = static_cast<int>(w.size());
    if (n == 0)
        return {};

    std::vector<int> order(n);
    std::iota(order.begin(), order.end(), 0);
    std::sort(order.begin(), order.end(), [&](int a, int b) {
        if (std::abs(w[a].cy() - w[b].cy()) > std::min(w[a].h, w[b].h) * 0.5)
            return w[a].cy() < w[b].cy();
        return w[a].cx() < w[b].cx();
    });
    std::vector<LRect> sorted(n);
    for (int i = 0; i < n; ++i)
        sorted[i] = w[order[i]];

    RowPlan best;
    int     bestRows = 1;
    for (int rows = 1; rows <= n; ++rows) {
        const auto plan = planForRows(sorted, area, cfg, rows);
        if (plan.scale > best.scale) {
            best     = plan;
            bestRows = rows;
        }
    }

    const double s = best.scale;
    double totalH = 0.0;
    {
        int idx = 0;
        for (int r = 0; r < bestRows; ++r) {
            double rowH = 1.0;
            for (int c = 0; c < best.counts[r]; ++c, ++idx)
                rowH = std::max(rowH, sorted[idx].h);
            totalH += rowH * s + (r > 0 ? cfg.gap : 0.0);
        }
    }

    std::vector<LRect> outSorted(n);
    double             y   = area.y + (area.h - totalH) / 2.0;
    int                idx = 0;
    for (int r = 0; r < bestRows; ++r) {
        double rowW = 0.0, rowH = 1.0;
        for (int c = 0; c < best.counts[r]; ++c) {
            rowW += sorted[idx + c].w * s + (c > 0 ? cfg.gap : 0.0);
            rowH = std::max(rowH, sorted[idx + c].h);
        }
        rowH *= s;
        double x = area.x + (area.w - rowW) / 2.0;
        for (int c = 0; c < best.counts[r]; ++c, ++idx) {
            const double tw = sorted[idx].w * s;
            const double th = sorted[idx].h * s;
            outSorted[idx]  = LRect{x, y + (rowH - th) / 2.0, tw, th};
            x += tw + cfg.gap;
        }
        y += rowH + cfg.gap;
    }

    std::vector<LRect> out(n);
    for (int i = 0; i < n; ++i)
        out[order[i]] = outSorted[i];
    return out;
}

std::vector<LRect> layoutGrid(const std::vector<LRect>& w, const LRect& area, const LayoutCfg& cfg) {
    const int n = static_cast<int>(w.size());
    if (n == 0)
        return {};
    const int    cols  = std::max(1, static_cast<int>(std::ceil(std::sqrt(static_cast<double>(n)))));
    const int    rows  = std::max(1, static_cast<int>(std::ceil(static_cast<double>(n) / cols)));
    const double cellW = std::max(1.0, (area.w - (cols - 1) * cfg.gap) / cols);
    const double cellH = std::max(1.0, (area.h - (rows - 1) * cfg.gap) / rows);

    std::vector<LRect> out(n);
    for (int i = 0; i < n; ++i) {
        const int    r     = i / cols;
        const int    c     = i % cols;
        const double slotX = area.x + c * (cellW + cfg.gap);
        const double slotY = area.y + r * (cellH + cfg.gap);
        const double scale = std::min({cellW / w[i].w, cellH / w[i].h, cfg.maxScale});
        const double tw    = w[i].w * scale;
        const double th    = w[i].h * scale;
        out[i]             = LRect{slotX + (cellW - tw) / 2.0, slotY + (cellH - th) / 2.0, tw, th};
    }
    return out;
}

static void fitGroup(const std::vector<LRect> &w, const std::vector<int> &idx, const LRect &cell,
                     const LayoutCfg &cfg, std::vector<LRect> &out) {  // one workspace in a cell
    double minX = 1e9, minY = 1e9, maxX = -1e9, maxY = -1e9;
    for (const int i : idx) {
        const auto& r = w[i];
        minX = std::min(minX, r.x);
        minY = std::min(minY, r.y);
        maxX = std::max(maxX, r.x + r.w);
        maxY = std::max(maxY, r.y + r.h);
    }
    const double bw = std::max(1.0, maxX - minX);
    const double bh = std::max(1.0, maxY - minY);
    const double s  = std::min({cell.w / bw, cell.h / bh, cfg.maxScale});
    const double ox = cell.x + (cell.w - bw * s) / 2.0;
    const double oy = cell.y + (cell.h - bh * s) / 2.0;
    for (const int i : idx)
        out[i] = LRect{ox + (w[i].x - minX) * s, oy + (w[i].y - minY) * s, w[i].w * s, w[i].h * s};
}

std::vector<LRect> layoutNatural(const std::vector<LRect>& w, const LRect& area, const LayoutCfg& cfg,
                                 const std::vector<int> &groups) {
    const int n = static_cast<int>(w.size());
    if (n == 0)
        return {};
    std::vector<LRect> out(n);

    std::vector<int> gs = groups;
    if (static_cast<int>(gs.size()) != n)
        gs.assign(n, 0);
    std::vector<int> uniq = gs;
    std::sort(uniq.begin(), uniq.end());
    uniq.erase(std::unique(uniq.begin(), uniq.end()), uniq.end());
    const int G = static_cast<int>(uniq.size());
    if (G <= 1) {
        std::vector<int> idx(n);
        for (int i = 0; i < n; ++i)
            idx[i] = i;
        fitGroup(w, idx, area, cfg, out);
        return out;
    }

    // The cells are packed: a column is as wide as its group, so the gap is the gap. The grid is
    // the square of the groups, up to the ceiling; the rows past it hang below the screen.
    const int cols = std::min(std::max(1, static_cast<int>(std::ceil(std::sqrt(static_cast<double>(G))))),
                              std::max(1, cfg.cellCols));
    const int rows = (G + cols - 1) / cols;
    const int onScreen = std::min(rows, std::max(1, cfg.cellRows));
    const double gap = std::max(0.0, cfg.cellGap);

    std::vector<std::vector<int>> members(G);
    for (int i = 0; i < n; ++i) {
        for (int g = 0; g < G; ++g)
            if (gs[i] == uniq[g]) {
                members[g].push_back(i);
                break;
            }
    }

    struct Box {
        double w = 0.0, h = 0.0, minX = 0.0, minY = 0.0;
    };
    std::vector<Box> box(G);
    for (int g = 0; g < G; ++g) {
        double minX = 1e9, minY = 1e9, maxX = -1e9, maxY = -1e9;
        for (const int i : members[g]) {
            minX = std::min(minX, w[i].x);
            minY = std::min(minY, w[i].y);
            maxX = std::max(maxX, w[i].x + w[i].w);
            maxY = std::max(maxY, w[i].y + w[i].h);
        }
        box[g] = Box{maxX - minX, maxY - minY, minX, minY};
    }

    auto rowSpan = [&](int r) {
        double width = 0.0, height = 0.0;
        int    count = 0;
        for (int g = r * cols; g < std::min(G, (r + 1) * cols); ++g) {
            width += box[g].w + (count > 0 ? gap : 0.0);
            height = std::max(height, box[g].h);
            ++count;
        }
        return std::pair<double, double>{width, height};
    };

    double widest = 0.0, tall = 0.0;
    for (int r = 0; r < onScreen; ++r) {
        const auto span = rowSpan(r);
        widest = std::max(widest, span.first);
        tall += span.second;
    }
    tall += gap * static_cast<double>(std::max(0, onScreen - 1));
    const double scale = std::min({area.w / std::max(1.0, widest),
                                   area.h / std::max(1.0, tall) * std::max(0.1, cfg.cellScale),
                                   cfg.maxScale});

    double y = area.y + (area.h - tall * scale) / 2.0;
    for (int r = 0; r < rows; ++r) {
        if (r == onScreen)
            y = area.y + area.h + cfg.padBottom;
        const auto span = rowSpan(r);
        double x = area.x + (area.w - span.first * scale) / 2.0;
        for (int g = r * cols; g < std::min(G, (r + 1) * cols); ++g) {
            for (const int i : members[g])
                out[i] = LRect{x + (w[i].x - box[g].minX) * scale, y + (w[i].y - box[g].minY) * scale,
                               w[i].w * scale, w[i].h * scale};
            x += box[g].w * scale + gap;
        }
        y += span.second * scale + gap;
    }
    return out;
}


}

std::vector<LRect> computeLayout(const std::vector<LRect>& naturals, const LRect& area,
                                 const LayoutCfg& cfg, const std::vector<int> &groups) {
    const LRect a = inset(area, cfg);
    switch (cfg.engine) {
        case Engine::Grid: return layoutGrid(naturals, a, cfg);
        case Engine::Natural: return layoutNatural(naturals, a, cfg, groups);
        case Engine::Rows:
        default: return layoutRows(naturals, a, cfg);
    }
}

Engine parseEngine(const char* s) {
    if (!s)
        return Engine::Rows;
    if (std::strcmp(s, "grid") == 0)
        return Engine::Grid;
    if (std::strcmp(s, "natural") == 0)
        return Engine::Natural;
    return Engine::Rows;
}

CarouselLayout computeCarousel(int count, int selected, const LRect& screen,
                               const CarouselMetrics& mt) {
    CarouselLayout L;
    L.area = screen;
    if (count <= 0)
        return L;
    L.cards.resize(static_cast<size_t>(count));

    const int want = std::max(1, mt.visibleCards);
    const int shown = std::min(want, count);
    L.visibleCards = shown;

    const int centerSlot = shown / 2;
    const int leftSlots = centerSlot;
    const int rightSlots = shown - 1 - centerSlot;

    const int sel = ((selected % count) + count) % count;
    std::vector<int> slotOf(static_cast<size_t>(count), 99);
    std::vector<int> idxOfSlot;
    idxOfSlot.reserve(static_cast<size_t>(shown));
    for (int rel = -leftSlots; rel <= rightSlots; ++rel) {
        const int idx = ((sel + rel) % count + count) % count;
        if (slotOf[static_cast<size_t>(idx)] != 99)
            continue; // the first placement of a card wins
        slotOf[static_cast<size_t>(idx)] = rel;
        idxOfSlot.push_back(idx);
    }
    for (const int rel : {-leftSlots - 1, rightSlots + 1}) {
        const int idx = ((sel + rel) % count + count) % count;
        if (slotOf[static_cast<size_t>(idx)] != 99)
            continue;
        slotOf[static_cast<size_t>(idx)] = rel;
        idxOfSlot.push_back(idx);
    }

    const double boxW = std::max(1.0, mt.boxW);
    const double boxH = std::max(1.0, mt.boxH);
    const auto sizeAt = [&](int depth) {
        if (depth <= 0)
            return 1.0;
        if (depth == 1)
            return mt.nearK;
        return mt.farK * std::pow(mt.taper, static_cast<double>(depth - 2));
    };
    const auto wAt = [&](int depth) { return boxW * sizeAt(depth); };
    const auto pitchAt = [&](int depth) { return wAt(depth) * mt.pitchK; };
    const int reach = std::max(leftSlots, rightSlots);
    const double stripW =
        wAt(0) + (pitchAt(reach) + wAt(reach) * 0.5) * static_cast<double>(reach) * 2.0;
    const double stripH = boxH + mt.captionGap + mt.captionH;

    const double availW = std::max(1.0, screen.w - mt.screenMargin * 2.0);
    const double availH = std::max(1.0, screen.h - mt.screenMargin * 2.0);
    const double k =
        std::min({1.0, availW / std::max(1.0, stripW),
                  availH / std::max(1.0, stripH)}) * mt.scale;
    const double kCard =
        std::min(k, std::min(screen.w / boxW, screen.h / stripH));

    L.stripW = stripW * kCard;
    L.stripH = stripH * kCard;
    L.panelH = L.stripH;
    L.stripY = 0.0;
    L.originX = 0.0;
    L.originY = std::round((screen.h - L.panelH) * 0.5);

    const auto placeAt = [&](int slot, CarouselLayout::Card& c) {
        const int depth = std::abs(slot);
        c.visible = true;
        c.depth = depth;
        c.w = boxW * sizeAt(depth) * kCard;
        c.h = boxH * sizeAt(depth) * kCard;
        // x reads c.w, so it has to come after it.
        c.x = (screen.w - c.w) * 0.5 +
              static_cast<double>(slot) * pitchAt(depth) * kCard;
        c.y = (boxH * kCard - c.h) * 0.5;
    };

    for (const int idx : idxOfSlot) {
        const int slot = slotOf[static_cast<size_t>(idx)];
        placeAt(slot, L.cards[static_cast<size_t>(idx)]);
    }
    return L;
}

}
