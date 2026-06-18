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
- `versions/v1…v9/` — snapshots aprovados (copiar de volta para restaurar; v7+ espelham
  a árvore: `core/ effects/ platform/web/ build_web.sh`).

Fronteira sagrada: efeitos desenham num framebuffer 64×64; o display final mapeia
o valor da célula para cor. A movimentação é `(tabela - frame) % 256`.

**Dithering (dissolve):** `ditherOn(x,y,reveal)` em `core/Dither.h` — limiar FIXO por
pixel; ao crescer `reveal` só acendem pixels novos, ao diminuir só somem (não piscam).
É a transição padrão de tudo: texto/HUD, boot e as transições do efeito.

## Estado atual (v9) — efeito `Gradient` (TOTALMENTE RANDÔMICO)

**Um único "encoder":** girar = **brilho** (0–100%, camada de display: no ESP
`panel.setBrightness()`, na web o JS escala o RGB de saída — NÃO mexe nas cores);
clicar = **`randomize()`** (sorteia desenho + cor de uma vez). Na UI: 1 slider + 1 botão.
Exports: `viz_seed, viz_randomize, viz_text, viz_setTextVisible`.

**Padrão (tabela):** base = gradiente `x*4` + noise; pipeline repetido **×`kStripes` (=8)**.
Cada passada faz `transposição? → resize? → transposição? → tarja?`, **cada sub-passo com
50% de chance** (`std::rand()&1`). resize = reinterpretar o buffer 4096 em W×H potência de
2 (`a∈[2,10]` ⇒ dims em [4,1024], mín 4px); tarja divide H em N tarjas (`N≤H/2` ⇒ mín 2
linhas/tarja). No fim os 4096 bytes são relidos como 64×64 (sem mover dados).

**Cor randômica** (`ColorParams` sorteado em `randomParams()`; cor é função de `t=v/255`,
`te=t²`):
- **Matiz:** sub-range `[lo,hi]` dentro de `[0.35,1.15]` (sem verde), largura ≥ 50% do
  span; sentido invertível; curva linear ou exp. `hue = lo + (hi−lo)·forma(t)`.
- **Saturação:** máx sempre 1, mín sorteado `[0,1]`; curva lin/exp; **nunca invertida**.
- **Luminância:** sempre exp, mín 0, máx sorteado `[0.5,1]`; nunca invertida. `L=lMax·t²`.

`randomize()` troca desenho **e** cor juntos numa só transição por **dither** de 16 frames
(`tableOld_/colorOld_` → `tableNew_/colorNew_`; re-clique conclui a anterior na hora). Ao
dar `seed()` já cai num estado randômico.

**Texto:** fonte 4×6 (`core/Font.h`). HUD (só do **brilho**) `BRILHO N%` surge/some por
dither (8 frames) no canto inferior-esquerdo ao girar; o RANDOM não mostra texto. Boot
(`main.cpp`): cartela 3 linhas `LESTON`/`64x64x64x64`/`2026 #N` — `N` = `kPieceNo`
(constante no topo, **trocar a cada upload**). Fundo **azul** + título surgem juntos por
dither, seguram, somem por completo e SÓ ENTÃO a imagem entra por dither sobre o preto
(in 16 / hold 90 / out 16 / imagem 16).

## Convenções de trabalho

- **Propor em texto e esperar OK antes de implementar** mudanças de visual/algoritmo.
- A cada marco aprovado: salvar `versions/vN/` + uma memória `project-vN-milestone`.
- TODOs pendentes estão nas memórias `project-todo-*`.
