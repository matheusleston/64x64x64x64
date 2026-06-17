#pragma once
// Plasma.h — primeiro efeito de teste. Puro 2D, barato, só para validar o
// pipeline (core -> framebuffer -> WASM -> canvas) ponta a ponta.
// O efeito "de verdade" com o volume 64^3 projetado vem depois.

#include <cmath>
#include "../core/Effect.h"

namespace viz {

class Plasma : public Effect {
 public:
  void render(Framebuffer& fb, uint32_t frame) override {
    float t = frame * (1.f / 60.f);  // plasma ainda pensa em segundos
    for (int y = 0; y < kPanelH; ++y) {
      for (int x = 0; x < kPanelW; ++x) {
        // Soma de senos clássica — desloca no tempo para animar.
        float fx = x * (1.f / kPanelW);
        float fy = y * (1.f / kPanelH);
        float v = std::sin((fx * 8.f) + t)
                + std::sin((fy * 8.f) + t * 1.3f)
                + std::sin((fx + fy) * 8.f + t * 0.7f);
        v = (v + 3.f) / 6.f;  // normaliza ~[0,1]
        fb.set(x, y, hsv(v + t * 0.05f, 0.85f, 1.0f));
      }
    }
  }

  const char* name() const override { return "plasma"; }
};

}  // namespace viz
