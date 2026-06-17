// main.cpp (web) — ponte Emscripten. Mantém UM framebuffer e UM efeito,
// e expõe funções em C para o JavaScript chamar. NENHUMA lógica de visual
// mora aqui: isto é só plataforma. O mesmo papel será feito por um arquivo
// equivalente em platform/esp/ no futuro.

#include <emscripten.h>
#include <cstdint>

#include "../../core/Framebuffer.h"
#include "../../core/Font.h"
#include "../../effects/Gradient.h"

namespace {
viz::Framebuffer g_fb;
viz::Gradient g_effect;
char g_text[128] = {0};   // texto do HUD (camada de display, fora do efeito)
float g_textReveal = 0.f; // reveal animado do HUD (dithering de entrada/saída)
float g_textTarget = 0.f; // alvo: 1 = visível, 0 = oculto
uint32_t g_lastFrame = 0xFFFFFFFFu;

// Boot: ao ligar (painel/ESP), o título "surge" por dithering, segura, "some"
// por completo e SÓ ENTÃO a imagem entra, também por dithering (aparecendo aos
// poucos sobre o preto). Tudo em quadros lógicos (~2 ticks cada).
constexpr uint32_t kBootIn       = 8;   // dither-in do título
constexpr uint32_t kBootHold     = 45;  // segura o título
constexpr uint32_t kBootTitleOut = 8;   // dither-out do título (some por completo)
constexpr uint32_t kBootImageIn  = 8;   // imagem surge por dithering sobre o preto
constexpr int      kHudDither    = 4;   // velocidade do dithering do HUD (frames)

// Texto branco, 2 linhas, ancorado no canto inferior esquerdo (1px de margem).
void drawBottomLeft(const char* s, float reveal) {
  int lines = 1;
  for (const char* p = s; *p; ++p) if (*p == '\n') ++lines;
  int y = viz::kPanelH - (lines * viz::kLineAdv - 1) - 1;
  viz::drawTextLeft(g_fb, 1, y, s, viz::Rgb{255, 255, 255}, reveal);
}
}  // namespace

extern "C" {

// Renderiza o quadro de índice `frame` e devolve um ponteiro para os bytes
// RGB8 (kPixels * 3). O JS lê direto da heap do WASM.
EMSCRIPTEN_KEEPALIVE
const uint8_t* viz_render(uint32_t frame) {
  g_effect.render(g_fb, frame);

  // Boot: título (dither-in -> hold -> dither-out completo) e, em seguida, a
  // imagem revelada por dithering sobre o preto. Mesmo limiar fixo por pixel.
  const uint32_t bootEnd = kBootIn + kBootHold + kBootTitleOut + kBootImageIn;
  if (frame < bootEnd) {
    float titleReveal = 0.f;   // texto do boot
    float imageReveal = 0.f;   // 0 = tudo preto .. 1 = imagem completa
    if (frame < kBootIn) {
      titleReveal = (frame + 1) / static_cast<float>(kBootIn);
    } else if (frame < kBootIn + kBootHold) {
      titleReveal = 1.f;
    } else if (frame < kBootIn + kBootHold + kBootTitleOut) {
      uint32_t k = frame - kBootIn - kBootHold;
      titleReveal = 1.f - (k + 1) / static_cast<float>(kBootTitleOut);  // -> 0
    } else {
      uint32_t k = frame - kBootIn - kBootHold - kBootTitleOut;
      imageReveal = (k + 1) / static_cast<float>(kBootImageIn);         // 0 -> 1
    }
    // Apaga (para preto) os pixels que o dithering ainda não revelou da imagem.
    viz::Rgb* fb = g_fb.data();
    for (int y = 0; y < viz::kPanelH; ++y)
      for (int x = 0; x < viz::kPanelW; ++x)
        if (!viz::ditherOn(x, y, imageReveal)) fb[y * viz::kPanelW + x] = viz::Rgb{0, 0, 0};
    if (titleReveal > 0.f) drawBottomLeft("LESTON\n64x64x64x64", titleReveal);
  }

  // HUD: reveal animado (dithering de entrada/saída) por quadro lógico.
  if (frame != g_lastFrame) {
    g_lastFrame = frame;
    const float step = 1.f / kHudDither;
    if (g_textReveal < g_textTarget)
      g_textReveal = (g_textReveal + step > g_textTarget) ? g_textTarget : g_textReveal + step;
    else if (g_textReveal > g_textTarget)
      g_textReveal = (g_textReveal - step < g_textTarget) ? g_textTarget : g_textReveal - step;
  }
  if (g_text[0] && g_textReveal > 0.f)
    drawBottomLeft(g_text, g_textReveal);

  return reinterpret_cast<const uint8_t*>(g_fb.data());
}

EMSCRIPTEN_KEEPALIVE int viz_width() { return viz::kPanelW; }
EMSCRIPTEN_KEEPALIVE int viz_height() { return viz::kPanelH; }

// Semente do gerador de padrão (a plataforma fornece a entropia).
EMSCRIPTEN_KEEPALIVE void viz_seed(unsigned s) { g_effect.seed(s); }

// --- Os 3 "encoders" (sliders/botões agora, encoders no ESP depois) ---------
EMSCRIPTEN_KEEPALIVE void viz_setHueBase(int v) { g_effect.setHueBase(v); }      // enc 1
EMSCRIPTEN_KEEPALIVE void viz_toggleHueInvert() { g_effect.toggleHueInvert(); }  // clique enc 1
EMSCRIPTEN_KEEPALIVE void viz_toggleHueCurve() { g_effect.toggleHueCurve(); }    // clique enc 2
EMSCRIPTEN_KEEPALIVE void viz_setLumMax(int p) { g_effect.setLumMax(p); }        // enc 2
EMSCRIPTEN_KEEPALIVE void viz_newPattern() { g_effect.newPattern(); }            // clique enc 3

// Para inspeção na UI: descrição compacta do pipeline do padrão.
EMSCRIPTEN_KEEPALIVE const char* viz_info() { return g_effect.info(); }

// HUD: o JS escreve a string (UTF8, terminada em \0) direto neste buffer.
EMSCRIPTEN_KEEPALIVE char* viz_text() { return g_text; }

// HUD: alvo de visibilidade (1 = surge por dithering, 0 = some por dithering).
EMSCRIPTEN_KEEPALIVE void viz_setTextVisible(int on) { g_textTarget = on ? 1.f : 0.f; }

}  // extern "C"
