// How strongly the see-through parts of the step list are drawn, kept in one place so they can be tuned together.
#pragma once

namespace look {

struct Values {
    double hover = 0.30;         // the row under the pointer: the theme's accent at this opacity
    int hoverOnSelected = 28;    // the same row while selected: white at this alpha (0-255) over the selection
    double previewPanel = 1.0;   // the step preview's background (1 = solid, lower lets the list show through)
    double bandFill = 0.15;      // the selection box's fill: the accent at this opacity (below 0: the style's own box)
    double bandBorder = 0.9;     // the selection box's outline (used when bandFill is 0 or more)
    double bandInner = 0.45;     // a line just inside the outline in the selection's text color: shows over selected steps
};

inline Values& values()
{
    static Values v;
    return v;
}

}  // namespace look
