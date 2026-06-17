# 64x64x64x64

Visuais generativos para um painel de LED **64×64 RGB** controlado por um ESP32.
O conceito é 4D: um volume de voxels **64³** animado ao longo de **64 frames**,
projetado para a grade física 64×64.

## Princípio de arquitetura

A lógica que **gera** a imagem é separada do driver que a **mostra**. Todo
efeito desenha numa abstração de framebuffer 64×64 (`core/Framebuffer.h`).
O mesmo código de efeito roda:

- **hoje** no navegador, compilado para WebAssembly (`platform/web/`);
- **amanhã** no ESP32, empurrando o framebuffer para o painel HUB75
  (`platform/esp/`, a fazer).

```
core/      núcleo portável (cor, framebuffer, interface de efeito)
effects/   os visuais (portáveis)
platform/  web (Emscripten) · esp (HUB75, depois) · sim (desktop, depois)
```

Regras que mantêm tudo portável e compatível com o ESP:
- efeitos animam em função de `t` (segundos), nunca de contagem de frames;
- nada de alocação dinâmica dentro dos efeitos;
- o volume 64³ (~786 KB) é **gerado proceduralmente**, não armazenado
  (não cabe na RAM interna do ESP32).

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
python3 -m http.server -d platform/web 8000
# abrir http://localhost:8000
```
