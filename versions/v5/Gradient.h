#pragma once
// Gradient.h — TABELA + leitura deslocada. Pipeline EXPERIMENTAL: 4 passadas de
// (transposição opcional + redimensionamento + tarja), e volta a 64x64.
//
// buildTarget gera o alvo assim:
//   base    : gradiente horizontal x*4 + noise fixo 0..3  -> valores 0..255
//   pipeline: [T?] resize [T?] tarja  (x4)  -> 64x64
//   - [T?]: transposição que ACONTECE OU NÃO (50%), reordenando os dados — uma
//     antes do resize e outra antes da tarja, em cada passada.
//   - resize: o buffer linear de 4096 é REINTERPRETADO numa resolução W×H
//     (potência de 2, W*H=4096, mín 2) — em ordem de linhas, estilo jit.scanwrap,
//     sem mover dados. Volta a 64x64 só reinterpretando no fim.
//   - cada "tarja" divide as LINHAS (da forma atual) em N tarjas (N divisor de H,
//     máx H = até 1 linha) e faz UMA das três coisas, 1/3 cada — nunca é no-op:
//       1) só deslocamento: cada tarja i desloca na horizontal (i*V)%W px, V 1..W-1
//       2) só inversão: tarjas alternadas invertidas
//       3) deslocamento + inversão
//     Quando há inversão, o tipo é sorteado 1/3 cada: H, V ou H+V.
//
// Transição: randomize() guarda a tabela atual (tableOld) e gera a nova
// (tableNew); ao longo de kFadeFrames frames lógicos cada célula interpola
// old->new. O movimento segue rolando sobre as tabelas intermediárias.
//
// Início: carrega já randomizado, SEM fade (o fade de entrada será de
// intensidade, via brilho do painel no ESP). Semente vem da plataforma.
//
// Display: v = (tabela - frame) % 256, mapeado em HSL. O mesmo v controla H, S
// e L. H é confinado ao arco [0.35, 1.15] (mod 1 exclui o verde [0.15,0.35]), com
// offset/sentido/largura sorteados deslizando dentro desse arco. L vai de 0 a um máximo sorteado
// em [0.5,1] (permite branco) e usa SEMPRE a curva exponencial (t²); S vai de um
// mínimo sorteado até 1. Hue e saturação, separadamente, usam curva linear ou
// exponencial (50% cada). Tudo com o fade de 16 frames.
//   (transpose assume painel quadrado)

#include <cstdio>
#include <cstdlib>
#include "../core/Effect.h"

namespace viz {

class Gradient : public Effect {
 public:
  Gradient() { buildTarget(); applyTargetNow(); }

  void render(Framebuffer& fb, uint32_t frame) override {
    if (frame != lastFrame_) {     // um passo de interpolação por frame lógico
      lastFrame_ = frame;
      advanceTransition();
    }
    for (int y = 0; y < kPanelH; ++y) {
      for (int x = 0; x < kPanelW; ++x) {
        int stored = table_[y * kPanelW + x];               // 0..255
        int v = ((stored - static_cast<int>(frame)) % 256 + 256) % 256;
        // O valor vira HSL: S no máximo; o MESMO valor controla H e L, ambos
        // indo só até a metade — L até 0.5 (sem branco) e H até 0.5 (sem
        // arco-íris completo). Somamos o offset de hue sorteado (0..63 -> /64),
        // que gira a paleta a cada randomização (hsl() já faz o wrap do hue).
        // hueDirCur_ (0..1) inverte o sentido do hue: 0 = sobe com L, 1 = desce.
        // hueSpanCur_ é o alcance do hue no círculo (0.25..0.5).
        // L e S interpolam entre dois extremos sorteados (em t=0 e t=1), o que
        // permite inverter (e, no caso de L, chegar ao branco).
        // L sempre exponencial (t²). Hue e saturação escolhem (com fade) entre
        // linear (t) e exponencial (t²) conforme os booleanos sorteados.
        float t = v * (1.f / 255.f);
        float tsq = t * t;
        float tHue = t + (tsq - t) * hueExpCur_;
        float tSat = t + (tsq - t) * satExpCur_;
        // hue confinado a [0.35, 1.15] (mod 1 -> exclui o verde [0.15,0.35]).
        float tDir = tHue + hueDirCur_ * (1.f - 2.f * tHue);      // inverte
        float width = hueSpanCur_;                               // largura
        float base = kHueLo + (hueOffsetCur_ * (1.f / 64.f)) * (kHueRange - width);
        float hue = base + width * tDir;                         // sempre em [0.4,1.1]
        float L = lACur_ + (lBCur_ - lACur_) * tsq;
        float S = sACur_ + (sBCur_ - sACur_) * tSat;
        Rgb c = hsl(hue, S, L);
        fb.set(x, y, c);
      }
    }
  }

  const char* name() const override { return "gradient"; }

  // "Click do encoder": inicia a transição para um novo alvo sorteado.
  void randomize() { startTransition(); }

  // Semeia o gerador (entropia da plataforma) e carrega já randomizado, sem fade.
  void seed(unsigned s) { std::srand(s); buildTarget(); applyTargetNow(); }

  // Descrição compacta do pipeline sorteado (para a UI).
  const char* info() const { return info_; }

 private:
  static constexpr int kFadeFrames = 16;  // duração fixa da transição
  static constexpr int kStripes = 4;      // passadas de (resize + tarja) — experimento
  // hue confinado a [kHueLo, kHueLo+kHueRange] = [0.35, 1.15]; com módulo, exclui
  // a faixa do verde [0.15, 0.35]. O output nunca sai desse arco.
  static constexpr float kHueLo = 0.35f;
  static constexpr float kHueRange = 0.8f;

  void applyTargetNow() {
    for (int i = 0; i < kPixels; ++i) table_[i] = tableNew_[i];
    hueOffsetCur_ = static_cast<float>(hueOffsetNew_);   // sem fade
    hueDirCur_ = static_cast<float>(hueDirNew_);
    hueSpanCur_ = hueSpanNew_;
    lACur_ = lANew_; lBCur_ = lBNew_;
    sACur_ = sANew_; sBCur_ = sBNew_;
    hueExpCur_ = static_cast<float>(hueExpNew_);
    satExpCur_ = static_cast<float>(satExpNew_);
    transitioning_ = false;
  }

  void startTransition() {
    for (int i = 0; i < kPixels; ++i) tableOld_[i] = table_[i];
    hueOffsetOld_ = hueOffsetCur_;   // de onde a cor parte
    hueDirOld_ = hueDirCur_;
    hueSpanOld_ = hueSpanCur_;
    lAOld_ = lACur_; lBOld_ = lBCur_;
    sAOld_ = sACur_; sBOld_ = sBCur_;
    hueExpOld_ = hueExpCur_; satExpOld_ = satExpCur_;
    buildTarget();                   // sorteia tableNew_ e todos os parâmetros de cor
    transitionFrame_ = 0;
    transitioning_ = true;
  }

  void advanceTransition() {
    if (!transitioning_) return;
    ++transitionFrame_;
    if (transitionFrame_ >= kFadeFrames) {
      transitioning_ = false;
      for (int i = 0; i < kPixels; ++i) table_[i] = tableNew_[i];
      hueOffsetCur_ = static_cast<float>(hueOffsetNew_);
      hueDirCur_ = static_cast<float>(hueDirNew_);
      hueSpanCur_ = hueSpanNew_;
      lACur_ = lANew_; lBCur_ = lBNew_;
      sACur_ = sANew_; sBCur_ = sBNew_;
      hueExpCur_ = static_cast<float>(hueExpNew_);
      satExpCur_ = static_cast<float>(satExpNew_);
      return;
    }
    for (int i = 0; i < kPixels; ++i) {
      table_[i] = lerp(tableOld_[i], tableNew_[i], transitionFrame_, kFadeFrames);
    }
    // mesmo fade de 16 frames para todos os parâmetros de cor.
    float p = static_cast<float>(transitionFrame_) / kFadeFrames;
    hueOffsetCur_ = hueOffsetOld_ + (hueOffsetNew_ - hueOffsetOld_) * p;
    hueDirCur_    = hueDirOld_    + (hueDirNew_    - hueDirOld_)    * p;
    hueSpanCur_   = hueSpanOld_   + (hueSpanNew_   - hueSpanOld_)   * p;
    lACur_ = lAOld_ + (lANew_ - lAOld_) * p;
    lBCur_ = lBOld_ + (lBNew_ - lBOld_) * p;
    sACur_ = sAOld_ + (sANew_ - sAOld_) * p;
    sBCur_ = sBOld_ + (sBNew_ - sBOld_) * p;
    hueExpCur_ = hueExpOld_ + (hueExpNew_ - hueExpOld_) * p;
    satExpCur_ = satExpOld_ + (satExpNew_ - satExpOld_) * p;
  }

  static uint8_t lerp(int a, int b, int num, int den) {
    return static_cast<uint8_t>(a + ((b - a) * num) / den);
  }

  static const char* invName(int t) {  // 0=H,1=V,2=ambos
    return t == 0 ? "H" : (t == 1 ? "V" : "HV");
  }

  static float randf() { return (std::rand() % 1024) * (1.f / 1023.f); }  // [0,1]
  static int pct(float v) { return static_cast<int>(v * 100.f + 0.5f); }  // 0..100 p/ UI
  static int log2i(int n) { int e = 0; while (n > 1) { n >>= 1; ++e; } return e; }

  // Uma transformação de tarja numa matriz W×H (potências de 2). N = divisor de H
  // em [2, H] (pode chegar a 1 linha por tarja). Modo 1/3 cada: só deslocamento
  // (step 1..W-1, shift mod W), só inversão, ou ambos. Escreve em `label`.
  void applyStripes(const uint8_t* src, uint8_t* dst, int W, int H,
                    char* label, int labelSize) {
    const int hexp = log2i(H);                       // H = 2^hexp
    const int N    = 1 << (1 + std::rand() % hexp);  // 2 .. H (até 1 linha/tarja)
    const int mode = std::rand() % 3;                    // 0=desloc,1=inv,2=ambos
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

  void buildTarget() {
    uint8_t* cur = bufA_;
    uint8_t* nxt = bufB_;

    hueOffsetNew_ = std::rand() % 64;   // rotação de cor desta randomização
    hueDirNew_    = std::rand() % 2;     // 50%: inverte o sentido do hue vs L
    hueSpanNew_   = 0.25f + randf() * 0.25f;          // alcance do hue 0.25..0.5
    // luminância: mínimo sempre 0; máximo sorteado em [0.5, 1].
    lANew_ = 0.f;
    lBNew_ = 0.5f + randf() * 0.5f;
    // saturação: máximo sempre 1; mínimo aleatório em [0, 1].
    sANew_ = randf();
    sBNew_ = 1.f;
    // 50% cada: hue e saturação usam curva linear (0) ou exponencial (1).
    hueExpNew_ = std::rand() % 2;
    satExpNew_ = std::rand() % 2;

    // base: gradiente horizontal + noise fixo.
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

    char hb[36];
    std::snprintf(hb, sizeof(hb), " hue+%d%s span%d", hueOffsetNew_,
                  hueDirNew_ ? " inv" : "", static_cast<int>(hueSpanNew_ * 100.f + 0.5f));
    appendInfo(n, hb);

    char cb[64];
    std::snprintf(cb, sizeof(cb), " L%d-%d S%d-%d%s%s",
                  pct(lANew_), pct(lBNew_), pct(sANew_), pct(sBNew_),
                  hueExpNew_ ? " Hexp" : "", satExpNew_ ? " Sexp" : "");
    appendInfo(n, cb);

    for (int i = 0; i < kPixels; ++i) tableNew_[i] = cur[i];
  }

  uint8_t table_[kPixels];      // tabela exibida (interpolada durante a transição)
  uint8_t tableOld_[kPixels];   // origem da transição
  uint8_t tableNew_[kPixels];   // destino sorteado
  uint8_t bufA_[kPixels];       // scratch (ping-pong)
  uint8_t bufB_[kPixels];       // scratch (ping-pong)
  char info_[256];              // descrição do pipeline para a UI
  uint32_t lastFrame_ = 0xFFFFFFFFu;
  int transitionFrame_ = 0;
  bool transitioning_ = false;
  int hueOffsetNew_ = 0;        // offset de hue sorteado (0..63)
  float hueOffsetOld_ = 0.f;    // offset no início da transição
  float hueOffsetCur_ = 0.f;    // offset exibido (interpolado no fade)
  int hueDirNew_ = 0;           // 0=hue sobe com L, 1=hue desce (50%)
  float hueDirOld_ = 0.f;       // sentido no início da transição
  float hueDirCur_ = 0.f;       // sentido exibido (interpolado no fade)
  float hueSpanNew_ = 0.5f;     // alcance do hue no círculo (0.25..0.5)
  float hueSpanOld_ = 0.5f;     // alcance no início da transição
  float hueSpanCur_ = 0.5f;     // alcance exibido (interpolado no fade)
  // luminância: lA=0 (mín, t=0) e lB=máx em [0.5,1] (t=1), com fade.
  float lANew_ = 0.f,  lBNew_ = 0.5f;
  float lAOld_ = 0.f,  lBOld_ = 0.5f;
  float lACur_ = 0.f,  lBCur_ = 0.5f;
  // saturação: sA=mín aleatório (t=0) e sB=1 (máx, t=1), com fade.
  float sANew_ = 1.f,  sBNew_ = 1.f;
  float sAOld_ = 1.f,  sBOld_ = 1.f;
  float sACur_ = 1.f,  sBCur_ = 1.f;
  // 50% cada: hue/sat usam curva linear (0) ou exponencial (1), com fade.
  int hueExpNew_ = 0;   float hueExpOld_ = 0.f, hueExpCur_ = 0.f;
  int satExpNew_ = 0;   float satExpOld_ = 0.f, satExpCur_ = 0.f;
};

}  // namespace viz
