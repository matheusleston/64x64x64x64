# 64x64x64x64 — guia para retomar o projeto

Visuais generativos para um painel de LED **64×64 RGB** controlado por um **ESP32**
(hardware ainda não em mãos). Hoje desenvolvemos num **visualizador web**: o mesmo
código C++ portável compila para WebAssembly e rodará no ESP depois.

## Como compilar e rodar (web)

```bash
source ~/emsdk/emsdk_env.sh          # Emscripten (já instalado em ~/emsdk)
./build_web.sh                       # gera platform/web/viz.js + viz.wasm
# servidor já costuma estar rodando em background na porta 8064:
#   python3 -m http.server -d platform/web 8064
# abrir http://localhost:8064  (sempre HARD-RELOAD: Cmd+Shift+R, por causa do cache do .wasm)
```

Depois de qualquer edição em `effects/` ou `core/`: rodar `./build_web.sh` e hard-reload.

## Arquitetura

- `core/` — portável (sem dependência de plataforma): `Color.h` (RGB, `hsl()`),
  `Framebuffer.h` (grade 64×64), `Effect.h` (interface `render(fb, frame)`),
  `Dither.h` (`hash32`/`ditherOn` — máscara de dissolve por limiar fixo),
  `Font.h` (fonte bitmap 4×6 + `drawText`/`drawTextLeft`, com dithering).
- `effects/Gradient.h` — o efeito atual (todo o gerador + cor).
- `platform/web/` — `main.cpp` (ponte Emscripten, expõe `viz_*`; faz o boot/splash
  e desenha o HUD por cima do efeito), `index.html` (canvas + UI), `build_web.sh`.
  No futuro haverá `platform/esp/` reaproveitando `core/` e `effects/`.
- `versions/v1…v7/` — snapshots aprovados (copiar de volta para restaurar; v7 espelha
  a árvore: `core/ effects/ platform/web/ build_web.sh`).

Fronteira sagrada: efeitos desenham num framebuffer 64×64; o display final mapeia
o valor da célula para cor. A movimentação é `(tabela - frame) % 256`.

**Dithering (dissolve):** `ditherOn(x,y,reveal)` em `core/Dither.h` — limiar FIXO por
pixel; ao crescer `reveal` só acendem pixels novos, ao diminuir só somem (não piscam).
É a transição padrão de tudo: texto/HUD, boot e as transições do efeito.

## Estado atual (v7) — efeito `Gradient`

**Padrão (tabela):** base = gradiente `x*4` + noise; pipeline `[T?] resize [T?] tarja`
×4 (resize = reinterpretar o buffer 4096 em W×H potência de 2, estilo jit.scanwrap;
tarja divide H em N tarjas com deslocamento/inversão), volta a 64×64. Trocar o
desenho (`newPattern`) faz transição por **dither** de 16 frames (`tableOld_`→`tableNew_`).

**Cor (HSL), 3 "encoders"** (UI = 3 sliders + 3 botões pretos; no ESP serão encoders
clicáveis que chamam os mesmos setters). `t = valor/255`, `te = t²`:
- **Enc 1 Matiz:** slider 0–255 = máx do hue (`hueBase=(v+32)/256`); clique = inverter (dither).
- **Enc 2 Luminância:** slider 127–255 = máx de L (`lumMax=v/255`); clique = curva exp/linear (dither).
- **Enc 3 Brilho:** slider 0–255 = brilho do painel (só display — no ESP `panel.setBrightness()`,
  na web escala o RGB de saída no canvas, NÃO mexe nas cores); clique = novo desenho.
- `hue = hueBase - hueRange*(1-hShaped)`; `S=1`; `L = lumMax*te`. **`hueRange` é FIXO em 128/255.**

Os dois toggles (inverter/curva) usam o struct `Toggle` (transição por dither, 16 frames).

Setters: `setHueBase, toggleHueInvert, toggleHueCurve, setLumMax, newPattern`. Brilho é
só JS. Texto/HUD: `viz_text()` (buffer) + `viz_setTextVisible(0/1)`.

**Texto:** fonte 4×6 (`core/Font.h`). HUD procedural no canto inferior-esquerdo (2 linhas,
label/valor) surge/some por dither ao mexer num controle. Boot (`main.cpp`): título
`LESTON`/`64x64x64x64` surge por dither, segura, some por completo e SÓ ENTÃO a imagem
entra por dither.

## Convenções de trabalho

- **Propor em texto e esperar OK antes de implementar** mudanças de visual/algoritmo.
- A cada marco aprovado: salvar `versions/vN/` + uma memória `project-vN-milestone`.
- TODOs pendentes estão nas memórias `project-todo-*`.
