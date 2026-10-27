#include "display.h"

namespace breadboard {
using namespace opxy::draw;

void display_reset(DisplayState& state) {
    state.phase = state.note_serial = state.track = state.initialized = 0;
}

static void detune_value(opxy::i32 normalized, char (&text)[8]) {
    // The declared bipolar range maps the Q31 word 0..0x7fffffff to -100..100.
    opxy::i32 value = normalized / 10737418 - 100;
    unsigned at = 0;
    if (value < 0) {
        text[at++] = '-';
        value = -value;
    }
    if (value >= 100) text[at++] = static_cast<char>('0' + value / 100);
    if (value >= 10) text[at++] = static_cast<char>('0' + value / 10 % 10);
    text[at++] = static_cast<char>('0' + value % 10);
    text[at] = 0;
}

void display(DisplayState&, const opxy::draw::Input& input, Frame& frame) {
    // The source artwork is scaled to one fifth inside its SVG group.  Moving
    // a bound path by 60 source pixels therefore gives a clear twelve-pixel
    // lift on the 480 x 220 display.  Zero is its drawn (lowest) position.
    const opxy::i32 lift[4] = {
        (input.parameters[0] >> 27) * 4 * unit,  // capacitor: W Shaper
        (input.parameters[1] >> 27) * 4 * unit,  // wire: octave
        (input.parameters[2] >> 27) * 4 * unit,  // coil: detune_rm
        (input.parameters[3] >> 27) * 4 * unit   // transistor: Digital
    };
    const unsigned first[4] = {0, 17, 18, 27};
    const unsigned count[4] = {17, 1, 9, 10};
    for (unsigned group = 0; group < 4; ++group) {
        for (unsigned offset = 0; offset < count[group]; ++offset)
            place(frame, first[group] + offset, 0, -lift[group], unit, 0, keep);
    }
    char value[8] = {};
    detune_value(input.parameters[2], value);
    header(frame, 2, value);
    // These slots remain in the SVG for future artwork, but are intentionally
    // inactive until an element is assigned their matching node id.
    for (unsigned node = 37; node < 64; ++node) hide(frame, node);
}
}  // namespace breadboard
