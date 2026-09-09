#pragma once

#include <cstddef>

namespace onion::overlay {

inline constexpr float kRefFontSize = 18.0f;
inline constexpr float kMinTextWidth = 32.0f;

/* Approximate 18pt bold advances; padded per-core spaces are much narrower. */
inline constexpr float glyph_advance(unsigned char ch) {
  if (ch == ' ')
    return 5.0f;
  if (ch >= '0' && ch <= '9')
    return 11.0f;
  switch (ch) {
  case '%':
    return 15.0f;
  case '.':
  case '|':
    return 5.0f;
  case '-':
    return 8.0f;
  default:
    return 12.0f;
  }
}

inline float estimate_text_width(const char *text,
                                 float font_size = kRefFontSize) {
  if (!text || !text[0])
    return 0.0f;

  const float scale =
      (font_size > 0.0f ? font_size : kRefFontSize) / kRefFontSize;
  float width = 6.0f * scale;
  for (std::size_t i = 0; text[i]; ++i)
    width += glyph_advance(static_cast<unsigned char>(text[i])) * scale;

  const float min_width = kMinTextWidth * scale;
  return width < min_width ? min_width : width;
}

} // namespace onion::overlay
