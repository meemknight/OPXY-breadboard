#pragma once
#include <opxy/draw.hpp>

namespace breadboard {
// A retained PCB scene: board, signal trace, input module and output module,
// plus the lights, coil, pins and travelling signal.  Colors are a newly
// authored interpretation of the OP-1 control roles, not copied artwork.
using Frame = opxy::draw::Frame<0, 0, 64, 0, 7>;

struct DisplayState {
    opxy::u32 phase;
    opxy::u32 note_serial;
    opxy::u32 track;
    opxy::u32 initialized;
};
}
