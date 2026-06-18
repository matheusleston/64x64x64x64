#pragma once
// Color.h — tipos de cor portáveis. Sem dependência de plataforma:
// este mesmo header compila no browser (via Emscripten) e no ESP32.

#include <cmath>
#include <cstdint>

namespace viz {

// Cor de saída: 8 bits por canal, que é o que o painel HUB75 consome.
struct Rgb {
  uint8_t r = 0, g = 0, b = 0;
};

// Construtor utilitário com clamp, útil para os efeitos que calculam em float.
inline Rgb rgb(int r, int g, int b) {
  auto clamp8 = [](int v) -> uint8_t {
    if (v < 0) return 0;
    if (v > 255) return 255;
    return static_cast<uint8_t>(v);
  };
  return Rgb{clamp8(r), clamp8(g), clamp8(b)};
}

// Interpolação linear entre duas cores. t em [0,1].
inline Rgb lerp(Rgb a, Rgb b, float t) {
  if (t < 0.f) t = 0.f;
  if (t > 1.f) t = 1.f;
  return rgb(
      static_cast<int>(a.r + (b.r - a.r) * t),
      static_cast<int>(a.g + (b.g - a.g) * t),
      static_cast<int>(a.b + (b.b - a.b) * t));
}

// --- Convenção 0..63 do projeto -------------------------------------------
// Toda a programação dos efeitos trabalha em níveis de 0 a 63 (64 passos).
// A conversão para o RGB8 que o painel consome acontece SÓ aqui.

// Escala um nível 0..63 para 0..255 (com arredondamento). 63 -> 255 (branco).
inline uint8_t scale63(int level) {
  if (level < 0) level = 0;
  if (level > 63) level = 63;
  return static_cast<uint8_t>((level * 255 + 31) / 63);
}

// Cinza a partir de um nível 0..63. 0 = preto, 63 = branco.
inline Rgb gray63(int level) {
  uint8_t v = scale63(level);
  return Rgb{v, v, v};
}

// HSV -> RGB. h em [0,1], s e v em [0,1]. Base da maioria das paletas.
inline Rgb hsv(float h, float s, float v) {
  h -= static_cast<float>(static_cast<int>(h));  // wrap para [0,1)
  if (h < 0.f) h += 1.f;
  float i = h * 6.f;
  int   ii = static_cast<int>(i);
  float f = i - ii;
  float p = v * (1.f - s);
  float q = v * (1.f - s * f);
  float t = v * (1.f - s * (1.f - f));
  float r, g, b;
  switch (ii % 6) {
    case 0:  r = v; g = t; b = p; break;
    case 1:  r = q; g = v; b = p; break;
    case 2:  r = p; g = v; b = t; break;
    case 3:  r = p; g = q; b = v; break;
    case 4:  r = t; g = p; b = v; break;
    default: r = v; g = p; b = q; break;
  }
  return rgb(static_cast<int>(r * 255), static_cast<int>(g * 255),
             static_cast<int>(b * 255));
}

// HSL -> RGB. h em [0,1), s e l em [0,1]. Diferente de HSV: l=0 é preto,
// l=0.5 é a cor pura/saturada e l=1 é branco.
inline Rgb hsl(float h, float s, float l) {
  h -= static_cast<float>(static_cast<int>(h));  // wrap para [0,1)
  if (h < 0.f) h += 1.f;
  float c = (1.f - std::fabs(2.f * l - 1.f)) * s;
  float hp = h * 6.f;
  float x = c * (1.f - std::fabs(std::fmod(hp, 2.f) - 1.f));
  float r1 = 0.f, g1 = 0.f, b1 = 0.f;
  if (hp < 1.f)      { r1 = c; g1 = x; }
  else if (hp < 2.f) { r1 = x; g1 = c; }
  else if (hp < 3.f) { g1 = c; b1 = x; }
  else if (hp < 4.f) { g1 = x; b1 = c; }
  else if (hp < 5.f) { r1 = x; b1 = c; }
  else               { r1 = c; b1 = x; }
  float m = l - c / 2.f;
  return rgb(static_cast<int>((r1 + m) * 255.f),
             static_cast<int>((g1 + m) * 255.f),
             static_cast<int>((b1 + m) * 255.f));
}

}  // namespace viz
