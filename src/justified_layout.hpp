// The MIT License (MIT)
// Copyright 2019 SmugMug, Inc.
//
// Permission is hereby granted, free of charge, to any person obtaining a copy of
// this software and associated documentation files (the "Software"), to deal in
// the Software without restriction, including without limitation the rights to
// use, copy, modify, merge, publish, distribute, sublicense, and/or sell copies
// of the Software, and to permit persons to whom the Software is furnished to do
// so, subject to the following conditions:
//
// The above copyright notice and this permission notice shall be included in all
// copies or substantial portions of the Software.
//
// THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
// IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
// FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
// AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
// LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
// OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
// SOFTWARE.

#pragma once
#include <vector>
#include <string>
#include <algorithm>
#include <cmath>
#include <limits>
#include <memory>
#include <stdexcept>

// Enum for widow row alignment style.
enum class WidowStyle { Left, Center, Justify };

// Layout configuration parameters.
struct LayoutCfg {
    double w = 1060;         // Container width
    double pt = 10;          // Padding top
    double pr = 10;          // Padding right
    double pb = 10;          // Padding bottom
    double pl = 10;          // Padding left
    double sh = 10;          // Spacing horizontal
    double sv = 10;          // Spacing vertical
    double rh = 320;         // Target row height
    double tol = 0.25;       // Target row height tolerance (0.25 = ±25%)
    int maxRows = std::numeric_limits<int>::max(); // Max number of rows
    int breakCadence = 0;    // Full-width breakout row cadence (0 = disabled)
    bool widows = true;      // Show widows (last incomplete row)
    double ar = 0.0;         // Force aspect ratio (0 = disabled)
    WidowStyle ws = WidowStyle::Left; // Widow row style
};

// Image or box layout and aspect ratio.
struct Item {
    double ar;   // Aspect ratio
    double t = 0, l = 0, w = 0, h = 0;
    bool forcedAR = false;
};

// Represents a row of items in the justified layout.
class Row {
public:
    Row(const LayoutCfg& cfg, double top, bool breakout);

    bool add(const Item& it);
    bool done() const;
    void finish(double rh = -1);
    const std::vector<Item>& items() const;
    double height() const;
    bool breakout() const;

private:
    void layout(double newH, WidowStyle ws);

    const LayoutCfg& c;
    double top_;
    bool breakout_;
    double h_ = 0.0;
    std::vector<Item> its_;
    double minAR_, maxAR_;
};

// --- Row Implementation ---

inline Row::Row(const LayoutCfg& cfg, double top, bool breakout)
    : c(cfg), top_(top), breakout_(breakout)
{
    // Compute min/max aspect ratio sum for this row.
    double w = c.w - c.pl - c.pr;
    minAR_ = w / c.rh * (1.0 - c.tol);
    maxAR_ = w / c.rh * (1.0 + c.tol);
}

inline bool Row::add(const Item& it) {
    std::vector<Item> next = its_;
    next.push_back(it);

    double w = c.w - c.pl - c.pr;
    double ws = w - (next.size() - 1) * c.sh;
    double sumAR = 0.0;
    for (const auto& i : next) sumAR += i.ar;
    double targetAR = ws / c.rh;

    // Full-width breakout row
    if (breakout_) {
        if (its_.empty() && it.ar >= 1.0) {
            its_.push_back(it);
            layout(ws / it.ar, WidowStyle::Justify);
            return true;
        }
    }

    if (sumAR < minAR_) {
        its_.push_back(it);
        return true;
    } else if (sumAR > maxAR_) {
        if (its_.empty()) {
            its_.push_back(it);
            layout(ws / sumAR, WidowStyle::Justify);
            return true;
        }
        double prevWS = w - (its_.size() - 1) * c.sh;
        double prevAR = 0.0;
        for (const auto& i : its_) prevAR += i.ar;
        double prevTargetAR = prevWS / c.rh;

        if (std::abs(sumAR - targetAR) > std::abs(prevAR - prevTargetAR)) {
            layout(prevWS / prevAR, WidowStyle::Justify);
            return false;
        } else {
            its_.push_back(it);
            layout(ws / sumAR, WidowStyle::Justify);
            return true;
        }
    } else {
        its_.push_back(it);
        layout(ws / sumAR, WidowStyle::Justify);
        return true;
    }
}

inline bool Row::done() const { return h_ > 0.0; }

inline void Row::finish(double rh) {
    if (rh > 0)
        layout(rh, c.ws);
    else
        layout(c.rh, c.ws);
}

inline const std::vector<Item>& Row::items() const { return its_; }
inline double Row::height() const { return h_; }
inline bool Row::breakout() const { return breakout_; }

inline void Row::layout(double newH, WidowStyle ws) {
    double left = c.pl;
    double width = c.w - c.pl - c.pr;
    double spacing = c.sh;
    double minH = 0.5 * c.rh, maxH = 2 * c.rh;

    double wsW = width - (its_.size() - 1) * spacing;
    double clampH = std::max(minH, std::min(newH, maxH));
    double scale = (newH != clampH)
        ? ((wsW / clampH) / (wsW / newH))
        : 1.0;
    h_ = clampH;

    for (auto& it : its_) {
        it.t = top_;
        it.w = it.ar * h_ * scale;
        it.h = h_;
        it.l = left;
        left += it.w + spacing;
    }

    switch (ws) {
    case WidowStyle::Justify: {
        left -= (spacing + c.pl);
        double err = (left - width) / its_.size();
        std::vector<int> ce(its_.size(), 0);
        for (size_t i = 0; i < its_.size(); ++i)
            ce[i] = static_cast<int>(std::round((i + 1) * err));
        if (its_.size() == 1) {
            its_[0].w -= std::round(err);
        } else {
            for (size_t i = 0; i < its_.size(); ++i) {
                if (i > 0) {
                    its_[i].l -= ce[i - 1];
                    its_[i].w -= (ce[i] - ce[i - 1]);
                } else {
                    its_[i].w -= ce[i];
                }
            }
        }
        break;
    }
    case WidowStyle::Center: {
        double offset = (width - left) / 2.0;
        for (auto& it : its_) it.l += offset + spacing;
        break;
    }
    case WidowStyle::Left:
    default:
        // Do nothing.
        break;
    }
}

// JustifiedLayout manages the overall justified layout process.
class JustifiedLayout {
public:
    JustifiedLayout(const std::vector<Item>& input, const LayoutCfg& cfg);

    double height() const { return height_; }
    int widows() const { return widows_; }
    const std::vector<Item>& boxes() const { return boxes_; }

private:
    double height_ = 0;
    int widows_ = 0;
    std::vector<Item> boxes_;
    LayoutCfg cfg_;
};

// --- JustifiedLayout Implementation ---

inline JustifiedLayout::JustifiedLayout(const std::vector<Item>& input, const LayoutCfg& cfgIn)
    : cfg_(cfgIn)
{
    double y = cfg_.pt;
    int wc = 0;
    std::vector<Row> rows;
    boxes_.clear();

    // Prepare aspect ratios if forced.
    std::vector<Item> items = input;
    if (cfg_.ar > 0) {
        for (auto& item : items) {
            item.forcedAR = true;
            item.ar = cfg_.ar;
        }
    }

    size_t idx = 0;
    std::unique_ptr<Row> row;
    int rc = 0;

    auto newRow = [&](int rc) -> Row {
        bool breakout = false;
        if (cfg_.breakCadence > 0 && ((rc + 1) % cfg_.breakCadence) == 0)
            breakout = true;
        return Row(cfg_, y, breakout);
    };

    auto addRow = [&](Row& r) {
        rows.push_back(r);
        for (const auto& it : r.items()) boxes_.push_back(it);
        y += r.height() + cfg_.sv;
    };

    while (idx < items.size()) {
        if (!row) row = std::make_unique<Row>(newRow(rc));
        Item& it = items[idx];

        if (std::isnan(it.ar)) throw std::runtime_error("Item has invalid aspect ratio");

        bool added = row->add(it);

        if (row->done()) {
            addRow(*row);
            ++rc;
            if (rc >= cfg_.maxRows) {
                row.reset();
                break;
            }
            row = std::make_unique<Row>(newRow(rc));
            if (!added) {
                row->add(it);
                if (row->done()) {
                    addRow(*row);
                    ++rc;
                    if (rc >= cfg_.maxRows) {
                        row.reset();
                        break;
                    }
                    row = std::make_unique<Row>(newRow(rc));
                }
            }
        }
        ++idx;
    }

    // Widow/orphan row
    if (row && !row->items().empty() && cfg_.widows) {
        double prevH = cfg_.rh;
        if (!rows.empty()) prevH = rows.back().height();
        row->finish(prevH);
        addRow(*row);
        wc = static_cast<int>(row->items().size());
    }

    y -= cfg_.sv;
    y += cfg_.pb;

    height_ = y;
    widows_ = wc;
}
