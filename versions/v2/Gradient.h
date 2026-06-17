#pragma once
// Gradient.h — TABELA + leitura deslocada. Pipeline: transposições opcionais
// intercaladas com 4 transformações de tarja.
//
// buildTarget gera o alvo assim:
//   base    : gradiente horizontal x*4 + noise fixo 0..3  -> valores 0..255
//   pipeline: [T?] tarja [T?] tarja [T?] tarja [T?] tarja [T?]
//   - cada [T?] é uma transposição que ACONTECE OU NÃO (50%). É ela que dá, de
//     forma randômica, a orientação (horizontal/vertical) da tarja seguinte.
//   - cada "tarja" divide as LINHAS em N tarjas (N divisor de 64: {2,4,8,16,32,
//     64}) e faz UMA das três coisas, 1/3 cada — então nunca é um no-op:
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
// Display: v = (tabela - frame) % 256, mapeado em HSL — S máx, e o mesmo v
// controla H e L (ambos só até a metade: L até 0.5 sem branco, H até 0.5 sem
// arco-íris completo).   (transpose assume painel quadrado)

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
        // hueSpanCur_ é o alcance do hue no círculo (0.25..0.5). L segue em 0.5.
        float t = v * (1.f / 255.f);
        float hueComp = hueSpanCur_ * (t + hueDirCur_ * (1.f - 2.f * t));
        Rgb c = hsl(hueComp + hueOffsetCur_ * (1.f / 64.f), 1.f, t * 0.5f);
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
    transitioning_ = false;
  }

  void startTransition() {
    for (int i = 0; i < kPixels; ++i) tableOld_[i] = table_[i];
    hueOffsetOld_ = hueOffsetCur_;   // de onde a cor parte
    hueDirOld_ = hueDirCur_;
    hueSpanOld_ = hueSpanCur_;
    buildTarget();                   // sorteia tableNew_, hueOffsetNew_, hueDirNew_, hueSpanNew_
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
      return;
    }
    for (int i = 0; i < kPixels; ++i) {
      table_[i] = lerp(tableOld_[i], tableNew_[i], transitionFrame_, kFadeFrames);
    }
    // mesmo fade de 16 frames para offset, sentido e alcance do hue.
    float p = static_cast<float>(transitionFrame_) / kFadeFrames;
    hueOffsetCur_ = hueOffsetOld_ + (hueOffsetNew_ - hueOffsetOld_) * p;
    hueDirCur_    = hueDirOld_    + (hueDirNew_    - hueDirOld_)    * p;
    hueSpanCur_   = hueSpanOld_   + (hueSpanNew_   - hueSpanOld_)   * p;
  }

  static uint8_t lerp(int a, int b, int num, int den) {
    return static_cast<uint8_t>(a + ((b - a) * num) / den);
  }

  static const char* invName(int t) {  // 0=H,1=V,2=ambos
    return t == 0 ? "H" : (t == 1 ? "V" : "HV");
  }

  // Uma transformação de tarja. Sorteia N (divisor de 64) e o modo (1/3 cada):
  // só deslocamento, só inversão, ou ambos. Escreve em `label` o que ocorreu.
  void applyStripes(const uint8_t* src, uint8_t* dst, char* label, int labelSize) {
    static const int kBandCounts[] = {2, 4, 8, 16, 32, 64};
    const int N    = kBandCounts[std::rand() % 6];
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
    hueSpanNew_   = 0.25f + (std::rand() % 1024) * (0.25f / 1023.f);  // alcance 0.25..0.5

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
};

}  // namespace viz
