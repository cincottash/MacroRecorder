// How strongly the see-through parts of the step list are drawn, kept in one place so they can be tuned together.
#pragma once

namespace look {

struct Values {
    double hover = 0.22;         // the row under the pointer: the theme's accent at this opacity
    int hoverOnSelected = 28;    // the same row while selected: white at this alpha (0-255) over the selection
    double previewPanel = 1.0;   // the step preview's background (1 = solid, lower lets the list show through)
    double bandFill = -1;        // the selection box's fill: the accent at this opacity (below 0: the style's own box)
    double bandBorder = 0.9;     // the selection box's outline (used when bandFill is 0 or more)
};

inline Values& values()
{
    static Values v;
    return v;
}

}  // namespace look
