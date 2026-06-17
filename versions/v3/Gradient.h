#pragma once
// Gradient.h — TABELA + leitura deslocada. Pipeline: transposições opcionais
// intercaladas com 4 transformações de tarja.
//
// buildTarget gera o alvo assim:
//   base    : gradiente horizontal x*4 + noise fixo 0..3  -> valores 0..255
//   pipeline: [T?] tarja [T?] tarja [T?] tarja [T?] tarja [T?]
//   - cada [T?] é uma transposição que ACONTECE OU NÃO (50%). É ela que dá, de
//     forma randômica, a orientação (horizontal/vertical) da tarja seguinte.
//   - cada "tarja" divide as LINHAS em N tarjas (N divisor de 64, máx 32:
//     {2,4,8,16,32}) e faz UMA das três coisas, 1/3 cada — então nunca é um no-op:
//       1) só deslocamento: cada tarja i desloca na horizontal (i*V)%64 px, V 1..63
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
// e L. H usa offset/sentido/alcance sorteados. L vai de 0 a um máximo sorteado
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
        float hueComp = hueSpanCur_ * (tHue + hueDirCur_ * (1.f - 2.f * tHue));
        float L = lACur_ + (lBCur_ - lACur_) * tsq;
        float S = sACur_ + (sBCur_ - sACur_) * tSat;
        Rgb c = hsl(hueComp + hueOffsetCur_ * (1.f / 64.f), S, L);
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
  static constexpr int kStripes = 4;      // número de transformações de tarja

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

  // Uma transformação de tarja. Sorteia N (divisor de 64) e o modo (1/3 cada):
  // só deslocamento, só inversão, ou ambos. Escreve em `label` o que ocorreu.
  void applyStripes(const uint8_t* src, uint8_t* dst, char* label, int labelSize) {
    static const int kBandCounts[] = {2, 4, 8, 16, 32};  // sem 64 (evita 1 linha/coluna isolada)
    const int N    = kBandCounts[std::rand() % 5];
    const int mode = std::rand() % 3;                 // 0=desloc,1=inv,2=ambos
    const bool hasShift  = (mode == 0 || mode == 2);
    const bool hasInvert = (mode == 1 || mode == 2);
    const int V = hasShift ? (1 + std::rand() % 63) : 0;        // step 1..63
    const int invType = hasInvert ? (std::rand() % 3) : -1;     // 0=H,1=V,2=ambos
    const bool invH = hasInvert && (invType == 0 || invType == 2);
    const bool invV = hasInvert && (invType == 1 || invType == 2);

    if (hasShift && hasInvert)
      std::snprintf(label, labelSize, "%d/s%d+%s", N, V, invName(invType));
    else if (hasShift)
      std::snprintf(label, labelSize, "%d/s%d", N, V);
    else
      std::snprintf(label, labelSize, "%d/%s", N, invName(invType));

    const int rowsPerBand = kPanelH / N;
    for (int y = 0; y < kPanelH; ++y) {
      int band = y / rowsPerBand;
      int bandTop = band * rowsPerBand;
      int shift = hasShift ? (band * V) % 64 : 0;
      bool oddBand = (band % 2 == 1);
      bool mH = invH && oddBand;                       // espelha tarjas alternadas
      bool mV = invV && oddBand;
      for (int x = 0; x < kPanelW; ++x) {
        int sx = mH ? (kPanelW - 1 - x) : x;
        int sy = mV ? (bandTop + (rowsPerBand - 1 - (y - bandTop))) : y;
        int srcX = (sx + shift) % 64;
        dst[y * kPanelW + x] = src[sy * kPanelW + srcX];
      }
    }
  }

  // Transpõe a tabela (linhas <-> colunas). Assume painel quadrado.
  static void transpose(const uint8_t* src, uint8_t* dst) {
    for (int y = 0; y < kPanelH; ++y)
      for (int x = 0; x < kPanelW; ++x)
        dst[x * kPanelW + y] = src[y * kPanelW + x];
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
    for (int k = 0; k < kStripes; ++k) {
      if (std::rand() & 1) {                       // transposição opcional (50%)
        transpose(cur, nxt); uint8_t* t = cur; cur = nxt; nxt = t;
        appendInfo(n, "[T] ");
      } else {
        appendInfo(n, "[ ] ");
      }
      char label[24];
      applyStripes(cur, nxt, label, sizeof(label));     // tarja
      uint8_t* t = cur; cur = nxt; nxt = t;
      appendInfo(n, label);
      appendInfo(n, " ");
    }
    if (std::rand() & 1) {                          // 5a transposição (final)
      transpose(cur, nxt); uint8_t* t = cur; cur = nxt; nxt = t;
      appendInfo(n, "[T]");
    } else {
      appendInfo(n, "[ ]");
    }

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
  char info_[192];              // descrição do pipeline para a UI
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
