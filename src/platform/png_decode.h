#pragma once
// Minimal PNG reader for the button pictures (assets/glyphs). Non-interlaced; colour types 0, 2, 3, 4, 6 at 8 bits, and
// palette images at 1/2/4/8 bits (what Kenney's sets use). Output is straight (not premultiplied) RGBA8.
#include <cstdint>
#include <string>
#include <vector>

bool decodePng(const std::vector<uint8_t>& file, int& width, int& height, std::vector<uint8_t>& rgba, std::string& err);
