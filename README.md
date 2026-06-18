# 64x64x64x64

Visuais generativos para um painel de LED **64×64 RGB** controlado por um **ESP32**.
Hoje rodam como **visualizador web** (C++ portável → WebAssembly); o mesmo código
rodará no ESP32 empurrando o framebuffer para um painel HUB75.

🔗 **Ao vivo:** https://matheusleston.github.io/64x64x64x64/

## Princípio de arquitetura

A lógica que **gera** a imagem é separada do driver que a **mostra**. Todo efeito
desenha numa abstração de framebuffer 64×64 (`core/Framebuffer.h`). O mesmo efeito roda:

- **hoje** no navegador, compilado para WebAssembly (`platform/web/`);
- **amanhã** no ESP32, empurrando o framebuffer para o painel HUB75 (`platform/esp/`, a fazer).

```
core/      portável: Color (hsl), Framebuffer 64×64, Effect (render(fb, frame)),
           Dither (dissolve por limiar fixo), Font (bitmap 4×6 + texto)
effects/   os visuais portáveis (hoje: Gradient)
platform/  web (Emscripten) · esp (HUB75, a fazer)
versions/  snapshots aprovados (v1…v9)
```

Animação é **discreta**: cada efeito desenha o quadro de índice `frame` (0, 1, 2, …).
O contador é dono do harness (navegador hoje, `loop()` do ESP depois), então o mesmo
efeito roda igual nos dois lugares. `core/` e `effects/` são **compartilhados** entre web
e ESP — nunca duplicar.

## Rodar o visualizador (web)

Precisa do Emscripten:

```bash
# instalar emsdk (uma vez)
git clone https://github.com/emscripten-core/emsdk.git
cd emsdk && ./emsdk install latest && ./emsdk activate latest
source ./emsdk_env.sh

# compilar + servir
cd /caminho/para/64x64x64x64
./build_web.sh
python3 -m http.server -d platform/web 8064
# abrir http://localhost:8064  (hard-reload Cmd+Shift+R por causa do cache do .wasm)
```

## Deploy

Site estático. O GitHub Pages publica `platform/web` via Action
(`.github/workflows/pages.yml`); `viz.js`/`viz.wasm` são versionados (CI sem Emscripten).
Um `git push` na `main` republica.
