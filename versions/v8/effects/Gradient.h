#pragma once
// Gradient.h — gerador de PADRÃO (tabela) + cor HSL totalmente RANDÔMICA.
// Um único "encoder": girar = brilho (tratado fora, no display); clicar =
// randomize() — sorteia um novo desenho E novos parâmetros de cor, tudo trocando
// junto por uma transição de dithering (limiar fixo por pixel, 16 frames).
//
// Padrão (buildPattern): base gradiente x*4 + noise; pipeline de 4x
// [T?] resize [T?] tarja (estilo jit.scanwrap), voltando a 64x64.
//
// Cor: cada pixel tem t = (tabela - frame) % 256 / 255. As cores são função de t,
// definidas pelos parâmetros sorteados (ColorParams):
//   - Matiz: sub-range [lo,hi] dentro de [0.4,1.1] (sem verde), largura aleatória;
//     pode inverter o sentido; curva linear ou exponencial (t²).
//   - Saturação: máx sempre 1, mín sorteado em [0,1]; curva lin/exp; nunca invertida.
//   - Luminância: sempre exponencial, mín 0, máx sorteado em [0.5,1]; nunca invertida.

#include <cstdio>
#include <cstdlib>
#include "../core/Dither.h"
#include "../core/Effect.h"

namespace viz {

class Gradient : public Effect {
 public:
  Gradient() { initRandom(); }

  void render(Framebuffer& fb, uint32_t frame) override {
    if (frame != lastFrame_) {     // um passo por frame lógico
      lastFrame_ = frame;
      advanceTransition();
    }
    // Durante a transição cada pixel mostra o estado NOVO ou o ANTIGO conforme
    // seu limiar fixo — desenho e cor trocam juntos no mesmo dissolve.
    float prog = transitioning_ ? transitionFrame_ * (1.f / kFadeFrames) : 0.f;
    for (int y = 0; y < kPanelH; ++y) {
      for (int x = 0; x < kPanelW; ++x) {
        int i = y * kPanelW + x;
        int stored;
        const ColorParams* cp;
        if (transitioning_ && ditherOn(x, y, prog)) {
          stored = tableNew_[i]; cp = &colorNew_;
        } else if (transitioning_) {
          stored = tableOld_[i]; cp = &colorOld_;
        } else {
          stored = table_[i];    cp = &colorCur_;
        }
        int v = ((stored - static_cast<int>(frame)) % 256 + 256) % 256;
        fb.set(x, y, colorAt(v * (1.f / 255.f), *cp));
      }
    }
  }

  const char* name() const override { return "gradient"; }

  // Semeia o gerador (entropia da plataforma) e gera o estado inicial randômico.
  void seed(unsigned s) { std::srand(s); initRandom(); }

  // Clique do encoder: novo desenho + novas cores, com transição por dither.
  void randomize() {
    if (transitioning_) applyTransitionNow();   // conclui a transição anterior
    for (int i = 0; i < kPixels; ++i) tableOld_[i] = table_[i];
    colorOld_ = colorCur_;
    buildPattern();                 // novo desenho -> tableNew_
    colorNew_ = randomParams();
    transitionFrame_ = 0;
    transitioning_ = true;
  }

  const char* info() const { return info_; }

 private:
  static constexpr int kFadeFrames = 16;  // duração da transição (dither)
  static constexpr int kStripes = 4;      // passadas de (resize + tarja)

  // Parâmetros de cor sorteados. Cor é função do valor t de cada pixel.
  struct ColorParams {
    float hueLo = 0.4f, hueHi = 1.1f;  // sub-range dentro de [0.4,1.1]
    bool  hueInvert = false;
    bool  hueLinear = false;           // false = exponencial (t²), true = linear
    float satMin = 1.f;                // mín da saturação (máx sempre 1)
    bool  satLinear = false;
    float lumMax = 1.f;                // máx da luminância (mín sempre 0)
  };

  // [0,1) uniforme.
  static float frand() { return std::rand() * (1.f / (static_cast<float>(RAND_MAX) + 1.f)); }

  static ColorParams randomParams() {
    ColorParams p;
    // Matiz dentro de [0.35,1.15] (span 0.8), com largura MÍNIMA de 50% (0.40).
    const float kHueLo = 0.35f, kHueSpan = 0.8f;
    float width = 0.5f * kHueSpan + frand() * (0.5f * kHueSpan);  // 0.40..0.80
    p.hueLo = kHueLo + frand() * (kHueSpan - width);              // posição dentro do range
    p.hueHi = p.hueLo + width;
    p.hueInvert = (std::rand() & 1) != 0;
    p.hueLinear = (std::rand() & 1) != 0;
    p.satMin = frand();                // 0..1
    p.satLinear = (std::rand() & 1) != 0;
    p.lumMax = 0.5f + frand() * 0.5f;  // 0.5..1
    return p;
  }

  static Rgb colorAt(float t, const ColorParams& p) {
    float te = t * t;                                  // curva exponencial
    float shHue = p.hueLinear ? t : te;
    if (p.hueInvert) shHue = 1.f - shHue;
    float hue = p.hueLo + (p.hueHi - p.hueLo) * shHue;
    float S = p.satMin + (1.f - p.satMin) * (p.satLinear ? t : te);
    float L = p.lumMax * te;
    return hsl(hue, S, L);
  }

  void initRandom() {
    colorCur_ = randomParams();
    buildPattern();
    for (int i = 0; i < kPixels; ++i) table_[i] = tableNew_[i];
    transitioning_ = false;
  }

  void applyTransitionNow() {
    for (int i = 0; i < kPixels; ++i) table_[i] = tableNew_[i];
    colorCur_ = colorNew_;
    transitioning_ = false;
  }

  void advanceTransition() {
    if (transitioning_ && ++transitionFrame_ >= kFadeFrames) applyTransitionNow();
  }

  static const char* invName(int t) {  // 0=H,1=V,2=ambos
    return t == 0 ? "H" : (t == 1 ? "V" : "HV");
  }
  static int log2i(int n) { int e = 0; while (n > 1) { n >>= 1; ++e; } return e; }

  // Uma transformação de tarja numa matriz W×H (potências de 2). N = divisor de H
  // em [2, H/2] (cada tarja tem no MÍNIMO 2 linhas). Modo 1/3 cada: só deslocamento
  // (step 1..W-1, shift mod W), só inversão, ou ambos. Escreve em `label`.
  void applyStripes(const uint8_t* src, uint8_t* dst, int W, int H,
                    char* label, int labelSize) {
    const int hexp = log2i(H);                           // H = 2^hexp (H >= 4)
    const int N    = 1 << (1 + std::rand() % (hexp - 1)); // 2 .. H/2 (>=2 linhas/tarja)
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

  // Gera o novo padrão em tableNew_ (só a tabela; cor é separada).
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
      // (potência de 2) — sem mover dados (jit.scanwrap em ordem de linhas).
      // a em [2,10] => W e H ambos em [4,1024]: mín 4px (cabe 2 tarjas de 2 linhas).
      int a = 2 + std::rand() % 9;          // expoente: W = 2^a em [4,1024]
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

  uint8_t table_[kPixels];      // desenho estável exibido (fora de transição)
  uint8_t tableOld_[kPixels];   // origem da transição (dither)
  uint8_t tableNew_[kPixels];   // novo desenho
  uint8_t bufA_[kPixels];       // scratch (ping-pong)
  uint8_t bufB_[kPixels];       // scratch (ping-pong)
  char info_[256];              // descrição do pipeline (debug)
  uint32_t lastFrame_ = 0xFFFFFFFFu;
  int transitionFrame_ = 0;
  bool transitioning_ = false;

  ColorParams colorCur_;        // cor estável exibida
  ColorParams colorOld_;        // origem da transição
  ColorParams colorNew_;        // alvo da transição
};

}  // namespace viz
