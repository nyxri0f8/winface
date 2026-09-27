// WinFace engine - simple image container (8-bit, interleaved, top-down).
#pragma once
#include <cstdint>
#include <vector>

namespace fg {

struct Image {
    int w = 0, h = 0, ch = 3;          // ch = 3 (BGR) for camera frames and crops
    std::vector<uint8_t> px;           // w * h * ch bytes, row-major, no padding

    Image() = default;
    Image(int w_, int h_, int ch_ = 3) : w(w_), h(h_), ch(ch_), px(size_t(w_) * h_ * ch_) {}
    bool empty() const { return px.empty(); }
    uint8_t* row(int y) { return px.data() + size_t(y) * w * ch; }
    const uint8_t* row(int y) const { return px.data() + size_t(y) * w * ch; }
};

}  // namespace fg
