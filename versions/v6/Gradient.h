#pragma once
// Gradient.h — gerador de PADRÃO (tabela) + display HSL controlado por 3
// "encoders". Hoje a UI tem 3 sliders + 3 botões; no ESP serão 3 encoders.
// Cada encoder chama um setter aqui (valor -> setter; clique -> ação).
//
// Padrão (buildPattern): base gradiente x*4 + noise; pipeline de 4x
// [T?] resize [T?] tarja (estilo jit.scanwrap), voltando a 64x64. Trocar o
// desenho (newPattern) faz fade de 16 frames na tabela.
//
// Cor (HSL), controlada AO VIVO pelos encoders (sem fade). t = valor da célula
// em [0,1] após o movimento; te = t² (curva exponencial). HSL:
//   - Hue: encoder 1 = MÁXIMO do hue (slider 0 -> hue 0); encoder 2 = range (desce do máx
//     até o mín: 0 = uma cor só .. 1 = espectro todo); botão 1 inverte o sentido
//     e botão 2 alterna exp/linear — AMBOS com fade de 16 frames.
//   - Saturação: fixa em 100%.
//   - Luminância: mín sempre 0; máx = encoder 3 (0.5..1, exponencial). Botão 3 =
//     novo desenho (roda o pipeline) sem mexer nas cores.
//
// Display: v = (tabela - frame) % 256.   (transpose assume W*H = kPixels)

#include <cstdio>
#include <cstdlib>
#include "../core/Effect.h"

namespace viz {

class Gradient : public Effect {
 public:
  Gradient() { buildPattern(); applyPatternNow(); }

  void render(Framebuffer& fb, uint32_t frame) override {
    if (frame != lastFrame_) {     // um passo por frame lógico
      lastFrame_ = frame;
      advanceTransition();   // fade da tabela (novo desenho)
      advanceColorFades();   // fade dos toggles de hue (inverter / curva)
    }
    for (int y = 0; y < kPanelH; ++y) {
      for (int x = 0; x < kPanelW; ++x) {
        int stored = table_[y * kPanelW + x];
        int v = ((stored - static_cast<int>(frame)) % 256 + 256) % 256;
        float t = v * (1.f / 255.f);
        float te = t * t;                              // curva exponencial
        // hue: curva exp<->linear (botão 2, faded) e inversão (botão 1, faded).
        float hCurve = te + (t - te) * hueLinearCur_;          // exp(0)..lin(1)
        float hShaped = hCurve + (1.f - 2.f * hCurve) * hueInvertCur_;
        // base (enc 1) = MÁXIMO do hue; range (enc 2) desce até o mínimo.
        float hue = hueBase_ - hueRange_ * (1.f - hShaped);    // hsl faz o wrap
        float S = 1.f;                                 // saturação fixa em 100%
        float L = lumMax_ * te;                        // mín 0, máx (enc 3)
        fb.set(x, y, hsl(hue, S, L));
      }
    }
  }

  const char* name() const override { return "gradient"; }

  // Semeia o gerador (entropia da plataforma) e carrega o padrão inicial.
  void seed(unsigned s) { std::srand(s); buildPattern(); applyPatternNow(); }

  // --- Controles dos 3 "encoders" (UI agora, hardware depois) ---------------
  void setHueBase(int v0_255) { hueBase_ = (v0_255 + 32) * (1.f / 256.f); }  // enc 1 (offset +32), máx do hue
  void toggleHueInvert() { hueInvertTarget_ = 1.f - hueInvertTarget_; }      // botão 1 (faded)
  void setHueRange(int pct) { hueRange_ = pct * 0.01f; }               // encoder 2 (desce até o mín)
  void toggleHueCurve() { hueLinearTarget_ = 1.f - hueLinearTarget_; } // botão 2 (faded, exp/linear)
  void setLumMax(int pct) { lumMax_ = pct * 0.01f; }                   // encoder 3
  void newPattern() { startTransition(); }                            // botão 3

  const char* info() const { return info_; }

 private:
  static constexpr int kFadeFrames = 16;  // duração da transição de desenho
  static constexpr int kStripes = 4;      // passadas de (resize + tarja)

  void applyPatternNow() {
    for (int i = 0; i < kPixels; ++i) table_[i] = tableNew_[i];
    transitioning_ = false;
  }

  void startTransition() {
    for (int i = 0; i < kPixels; ++i) tableOld_[i] = table_[i];
    buildPattern();
    transitionFrame_ = 0;
    transitioning_ = true;
  }

  void advanceTransition() {
    if (!transitioning_) return;
    ++transitionFrame_;
    if (transitionFrame_ >= kFadeFrames) {
      transitioning_ = false;
      for (int i = 0; i < kPixels; ++i) table_[i] = tableNew_[i];
      return;
    }
    for (int i = 0; i < kPixels; ++i)
      table_[i] = lerp(tableOld_[i], tableNew_[i], transitionFrame_, kFadeFrames);
  }

  static uint8_t lerp(int a, int b, int num, int den) {
    return static_cast<uint8_t>(a + ((b - a) * num) / den);
  }

  // Move cur em direção a target por no máximo `step` (fade linear).
  static float approach(float cur, float target, float step) {
    if (cur < target) { cur += step; return cur > target ? target : cur; }
    if (cur > target) { cur -= step; return cur < target ? target : cur; }
    return cur;
  }

  // Fade de 16 frames dos toggles de hue (inversão e curva), por frame lógico.
  void advanceColorFades() {
    const float step = 1.f / kFadeFrames;
    hueInvertCur_ = approach(hueInvertCur_, hueInvertTarget_, step);
    hueLinearCur_ = approach(hueLinearCur_, hueLinearTarget_, step);
  }
  static const char* invName(int t) {  // 0=H,1=V,2=ambos
    return t == 0 ? "H" : (t == 1 ? "V" : "HV");
  }
  static int log2i(int n) { int e = 0; while (n > 1) { n >>= 1; ++e; } return e; }

  // Uma transformação de tarja numa matriz W×H (potências de 2). N = divisor de H
  // em [2, H] (pode chegar a 1 linha por tarja). Modo 1/3 cada: só deslocamento
  // (step 1..W-1, shift mod W), só inversão, ou ambos. Escreve em `label`.
  void applyStripes(const uint8_t* src, uint8_t* dst, int W, int H,
                    char* label, int labelSize) {
    const int hexp = log2i(H);                       // H = 2^hexp
    const int N    = 1 << (1 + std::rand() % hexp);  // 2 .. H (até 1 linha/tarja)
    const int mode = std::rand() % 3;                // 0=desloc,1=inv,2=ambos
    const bool hasShift  = (mode == 0 || mode == 2);
    const bool hasInvert = (mode == 1 || mode == 2);
    const int V = hasShift ? (1 + std::rand() % (W - 1)) : 0;   // step 1..W-1
    const int invType = hasInvert ? (std::rand() % 3) : -1;     // 0=H,1=V,2=ambos
    const bool invH = hasInvert && (invType == 0 || invType == 2);
    const bool invV = hasInvert && (invType == 1 || invType == 2);

    if (hasShift && hasInvert)
      std::snprintf(label, labelSize, "%d/s%d+%s", N, V, invName(invType));
    else if (hasShift)
      std::snprintf(label, labelSize, "%d/s%d", N, V);
    else
      std::snprintf(label, labelSize, "%d/%s", N, invName(invType));

    const int rowsPerBand = H / N;
    for (int y = 0; y < H; ++y) {
      int band = y / rowsPerBand;
      int bandTop = band * rowsPerBand;
      int shift = hasShift ? (band * V) % W : 0;
      bool oddBand = (band % 2 == 1);
      bool mH = invH && oddBand;                       // espelha tarjas alternadas
      bool mV = invV && oddBand;
      for (int x = 0; x < W; ++x) {
        int sx = mH ? (W - 1 - x) : x;
        int sy = mV ? (bandTop + (rowsPerBand - 1 - (y - bandTop))) : y;
        int srcX = (sx + shift) % W;
        dst[y * W + x] = src[sy * W + srcX];
      }
    }
  }

  // Transpõe uma matriz W×H (linhas <-> colunas) -> H×W. Reordena os dados.
  static void transpose(const uint8_t* src, uint8_t* dst, int W, int H) {
    for (int r = 0; r < H; ++r)
      for (int c = 0; c < W; ++c)
        dst[c * H + r] = src[r * W + c];
  }

  // Transposição opcional (50%): reordena os dados na forma atual e troca W<->H.
  void maybeTranspose(uint8_t*& cur, uint8_t*& nxt, int& W, int& H, int& n) {
    if (std::rand() & 1) {
      transpose(cur, nxt, W, H);
      uint8_t* t = cur; cur = nxt; nxt = t;
      int tmp = W; W = H; H = tmp;
      appendInfo(n, "[T] ");
    } else {
      appendInfo(n, "[ ] ");
    }
  }

  void appendInfo(int& n, const char* s) {
    if (n < 0 || n >= static_cast<int>(sizeof(info_)) - 1) return;
    int r = std::snprintf(info_ + n, sizeof(info_) - n, "%s", s);
    if (r > 0) n += r;
    if (n >= static_cast<int>(sizeof(info_))) n = sizeof(info_) - 1;
  }

  // Gera o novo padrão em tableNew_ (só a tabela; cor é separada e ao vivo).
  void buildPattern() {
    uint8_t* cur = bufA_;
    uint8_t* nxt = bufB_;

    // base: gradiente horizontal + noise fixo (64x64).
    for (int y = 0; y < kPanelH; ++y)
      for (int x = 0; x < kPanelW; ++x)
        cur[y * kPanelW + x] = static_cast<uint8_t>(x * 4 + (std::rand() & 3));

    int n = 0;
    info_[0] = '\0';
    int W = kPanelW, H = kPanelH;          // forma atual da matriz (começa 64x64)
    for (int k = 0; k < kStripes; ++k) {
      maybeTranspose(cur, nxt, W, H, n);    // transposição antes do resize
      // redimensionamento: reinterpreta o buffer 4096 numa nova resolução W×H
      // (potência de 2, mín 2) — sem mover dados (jit.scanwrap em ordem de linhas).
      int a = 1 + std::rand() % 11;         // expoente: W = 2^a em [2,2048]
      W = 1 << a; H = kPixels / W;
      char rb[16];
      std::snprintf(rb, sizeof(rb), "%dx%d ", W, H);
      appendInfo(n, rb);
      maybeTranspose(cur, nxt, W, H, n);    // transposição antes da tarja
      char label[24];
      applyStripes(cur, nxt, W, H, label, sizeof(label));
      uint8_t* t = cur; cur = nxt; nxt = t;
      appendInfo(n, label);
      appendInfo(n, " ");
    }
    // resolução original: o display lê tableNew_ como 64x64 (dados já são 4096).
    for (int i = 0; i < kPixels; ++i) tableNew_[i] = cur[i];
  }

  uint8_t table_[kPixels];      // tabela exibida (interpolada durante a transição)
  uint8_t tableOld_[kPixels];   // origem da transição de desenho
  uint8_t tableNew_[kPixels];   // novo desenho
  uint8_t bufA_[kPixels];       // scratch (ping-pong)
  uint8_t bufB_[kPixels];       // scratch (ping-pong)
  char info_[256];              // descrição do pipeline para a UI
  uint32_t lastFrame_ = 0xFFFFFFFFu;
  int transitionFrame_ = 0;
  bool transitioning_ = false;

  // Controles de cor. Sliders ao vivo; toggles com fade de 16 frames.
  float hueBase_ = 0.f;                               // encoder 1 (máx do hue)
  float hueRange_ = 0.5f;                             // encoder 2 (desce até o mín)
  float hueInvertTarget_ = 0.f, hueInvertCur_ = 0.f;  // botão 1 (faded)
  float hueLinearTarget_ = 0.f, hueLinearCur_ = 0.f;  // botão 2 (faded, 0=exp 1=linear)
  float lumMax_ = 1.0f;                              // encoder 3 (botão 3 = newPattern)
};

}  // namespace viz
