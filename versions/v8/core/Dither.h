#pragma once
// Dither.h — máscara de dithering portável usada por transições (texto, efeito,
// boot). Sem dependência de plataforma: compila igual no browser e no ESP.

#include <cstdint>

namespace viz {

// Hash inteiro -> ruído determinístico (sem estado global/rand).
inline uint32_t hash32(uint32_t x) {
  x ^= x >> 16; x *= 0x7feb352dU; x ^= x >> 15; x *= 0x846ca68bU; x ^= x >> 16;
  return x;
}

// Cada pixel (x,y) tem um limiar FIXO (sorteado só pela posição). Acende quando
// `reveal` em [0,1] passa do seu limiar — ao crescer reveal só surgem pixels
// novos, ao diminuir só somem, nunca piscam: um "dissolve" granulado estável.
inline bool ditherOn(int x, int y, float reveal) {
  if (reveal >= 1.f) return true;
  if (reveal <= 0.f) return false;
  uint32_t h = hash32(static_cast<uint32_t>(x) * 374761393U +
                      static_cast<uint32_t>(y) * 668265263U);
  return (h >> 8) * (1.f / 16777216.f) < reveal;  // 24 bits -> [0,1)
}

}  // namespace viz
