#pragma once
// Framebuffer.h — a grade de pixels 64x64 que TODO efeito desenha.
// Esta é a fronteira sagrada do projeto: os efeitos só conhecem este buffer.
// Quem mostra o buffer (canvas no browser, painel HUB75 no ESP) é trocável.

#include <cstdint>
#include "Color.h"

namespace viz {

// Dimensões do painel físico. O conceito é 4D (64^3 voxels x 64 frames),
// mas a saída para o hardware é sempre esta grade 2D.
constexpr int kPanelW = 64;
constexpr int kPanelH = 64;
constexpr int kPixels = kPanelW * kPanelH;

class Framebuffer {
 public:
  // set/get com checagem de limites barata (cast para unsigned pega negativos).
  void set(int x, int y, Rgb c) {
    if (static_cast<unsigned>(x) >= kPanelW) return;
    if (static_cast<unsigned>(y) >= kPanelH) return;
    px_[y * kPanelW + x] = c;
  }

  Rgb get(int x, int y) const { return px_[y * kPanelW + x]; }

  void clear(Rgb c = Rgb{}) {
    for (int i = 0; i < kPixels; ++i) px_[i] = c;
  }

  // Acesso contíguo ao buffer (RGB8 entrelaçado). É o que o harness web lê
  // e o que o driver do ESP vai empurrar para o painel.
  const Rgb* data() const { return px_; }
  Rgb* data() { return px_; }

 private:
  Rgb px_[kPixels];
};

}  // namespace viz
