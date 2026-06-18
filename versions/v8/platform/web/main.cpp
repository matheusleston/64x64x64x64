// main.cpp (web) — ponte Emscripten. Mantém UM framebuffer e UM efeito,
// e expõe funções em C para o JavaScript chamar. NENHUMA lógica de visual
// mora aqui: isto é só plataforma. O mesmo papel será feito por um arquivo
// equivalente em platform/esp/ no futuro.

#include <emscripten.h>
#include <cstdint>
#include <cstdio>

#include "../../core/Framebuffer.h"
#include "../../core/Font.h"
#include "../../effects/Gradient.h"

namespace {

// >>> NÚMERO DA PEÇA: trocar a cada upload (aparece na cartela: "2026 #N"). <<<
constexpr int kPieceNo = 1;
viz::Framebuffer g_fb;
viz::Gradient g_effect;
char g_text[128] = {0};   // texto do HUD (camada de display, fora do efeito)
float g_textReveal = 0.f; // reveal animado do HUD (dithering de entrada/saída)
float g_textTarget = 0.f; // alvo: 1 = visível, 0 = oculto
uint32_t g_lastFrame = 0xFFFFFFFFu;

// Boot: ao ligar (painel/ESP), o título "surge" por dithering, segura, "some"
// por completo e SÓ ENTÃO a imagem entra, também por dithering (aparecendo aos
// poucos sobre o preto). Tudo em quadros lógicos (~2 ticks cada).
constexpr uint32_t kBootIn       = 16;  // dither-in do título (= fade da imagem)
constexpr uint32_t kBootHold     = 90;  // segura o título (dobro do anterior)
constexpr uint32_t kBootTitleOut = 16;  // dither-out do título (some por completo)
constexpr uint32_t kBootImageIn  = 16;  // imagem surge por dithering sobre o preto
constexpr int      kHudDither    = 8;   // dithering do HUD do brilho (metade do boot)

// Cartela do boot (3 linhas), montada uma vez com o número da peça.
const char* splashText() {
  static char buf[40];
  if (!buf[0]) std::snprintf(buf, sizeof(buf), "LESTON\n64x64x64x64\n2026 #%d", kPieceNo);
  return buf;
}

// Texto branco, ancorado no canto inferior esquerdo (1px de margem).
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

  // Boot: a cartela (fundo AZUL + título branco) surge junta por dithering,
  // segura, some junta por completo e SÓ ENTÃO a imagem entra, também por
  // dithering sobre o preto. Mesmo limiar fixo por pixel em tudo.
  const uint32_t bootEnd = kBootIn + kBootHold + kBootTitleOut + kBootImageIn;
  if (frame < bootEnd) {
    float splashReveal = 0.f;  // fundo azul + título (entra/segura/sai juntos)
    float imageReveal = 0.f;   // imagem revelada sobre o preto (fase final)
    bool imagePhase = false;
    if (frame < kBootIn) {
      splashReveal = (frame + 1) / static_cast<float>(kBootIn);
    } else if (frame < kBootIn + kBootHold) {
      splashReveal = 1.f;
    } else if (frame < kBootIn + kBootHold + kBootTitleOut) {
      uint32_t k = frame - kBootIn - kBootHold;
      splashReveal = 1.f - (k + 1) / static_cast<float>(kBootTitleOut);  // -> 0
    } else {
      imagePhase = true;
      uint32_t k = frame - kBootIn - kBootHold - kBootTitleOut;
      imageReveal = (k + 1) / static_cast<float>(kBootImageIn);          // 0 -> 1
    }
    viz::Rgb* fb = g_fb.data();
    for (int y = 0; y < viz::kPanelH; ++y)
      for (int x = 0; x < viz::kPanelW; ++x) {
        int i = y * viz::kPanelW + x;
        if (imagePhase) {                       // imagem onde revelado, senão preto
          if (!viz::ditherOn(x, y, imageReveal)) fb[i] = viz::Rgb{0, 0, 0};
        } else {                                // azul onde revelado, senão preto
          fb[i] = viz::ditherOn(x, y, splashReveal) ? viz::Rgb{0, 0, 255} : viz::Rgb{0, 0, 0};
        }
      }
    if (splashReveal > 0.f) drawBottomLeft(splashText(), splashReveal);
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

// Semente do gerador (a plataforma fornece a entropia; já gera estado randômico).
EMSCRIPTEN_KEEPALIVE void viz_seed(unsigned s) { g_effect.seed(s); }

// Único "encoder": girar = brilho (tratado no JS/display); clicar = randomize.
EMSCRIPTEN_KEEPALIVE void viz_randomize() { g_effect.randomize(); }

// HUD: o JS escreve a string (UTF8, terminada em \0) direto neste buffer.
EMSCRIPTEN_KEEPALIVE char* viz_text() { return g_text; }

// HUD: alvo de visibilidade (1 = surge por dithering, 0 = some por dithering).
EMSCRIPTEN_KEEPALIVE void viz_setTextVisible(int on) { g_textTarget = on ? 1.f : 0.f; }

}  // extern "C"
