#pragma once
// Effect.h — interface de um visual. Animação é DISCRETA: cada chamada
// desenha o quadro de índice `frame` (0, 1, 2, ...). O contador é dono do
// harness (browser hoje, loop() do ESP depois), então o mesmo efeito roda
// igual nos dois lugares. Toda a lógica trabalha em passos de 0..63.

#include <cstdint>
#include "Framebuffer.h"

namespace viz {

class Effect {
 public:
  virtual ~Effect() = default;

  // Desenha um quadro completo em `fb`. `frame` = índice do quadro atual.
  virtual void render(Framebuffer& fb, uint32_t frame) = 0;

  // Nome curto para UI/seleção.
  virtual const char* name() const = 0;
};

}  // namespace viz
