# Zoom WASM — Horizon `e7614d2` (`horizon:mods@0.1.0`)

Este repositório contém um mod WASM de Zoom com comportamento tipo WI-Zoom,
**fixado na revisão antiga** `e7614d2197ebe9ffdaa75e1bbb432dc0ce855e70`
(`0c9a49390^`). Ele usa `api = "0.1"`, `side = "client"`, o pacote WIT
`horizon:mods@0.1.0`, e os exports `events@0.1.0` + `ui-events@0.1.0`.

> **Compatibilidade:** o `main` atual do Horizon (`31f83499…`) removeu o
> runtime WASM, o SDK, o WIT e os mods oficiais WASM em `0c9a49390`
> (`WASM_GONE`, `STOPPED`, `"Doesn't run any more: needs a native port"`).
> Este mod **não roda no `main` atual**; ele roda apenas no runtime da revisão
> `e7614d2`. Não é um `wasm32-wasip2` e não importa WASI.

## Entregável

`zoom-wasm-e7614d2.zip` contém exatamente dois arquivos na raiz:

```
horizon-mod.toml
mod.wasm
```

O `horizon-mod.toml` usa `id = "zoom"`, `api = "0.1"`, `side = "client"`,
`capabilities = ["render", "input", "ui", "storage"]` e
`experimental = ["render", "input", "ui"]`, conforme exigido pelo
`Manifest::parse` da revisão fixada. O `mod.wasm` tem 25.670 bytes.

## O que mudou em relação ao Zoom oficial WASM 1.0.0

Base: `mods/official/zoom/src/lib.rs` em `e7614d2` (cópia em
`reference/e7614d2/official_zoom_lib.rs`).

- **Tecla configurável**, com default `key.keyboard.z`; o binding do jogador
  em Options > Controls continua tendo precedência. Persistida em `storage`
  sob `key`.
- **Nível inicial configurável**, com default **3x** (o oficial começava em
  4x). Aceita valores contínuos em `[1.25, 10]`; o ciclo da UI usa
  `[2, 3, 4, 6, 8, 10]`.
- **Roda do mouse muda o nível enquanto a tecla está pressionada**:
  `claim-wheel(true)` no `key-down`, `claim-wheel(false)` no `key-up`,
  fator multiplicado por `1.25^passos` e limitado a `[1.25, 10]`.
- **Sensibilidade de giro reduzida proporcionalmente**: `turn = 1 / factor`
  quando a opção está ligada; `turn = 1` ao soltar a tecla ou desligar a
  opção. `smooth` continua configurável.
- **Comando `/zoom`**: `status`, `level <1.25-10>`, `key <key…>`,
  `smooth on|off`, `sensitivity on|off`, `reset`.
- **Página de configurações** com texto + 4 botões (ciclo de nível, smooth,
  sensibilidade, ciclo de tecla) e `ui-events` implementado.
- **`events`** registra `/zoom` e `frame`; `tick`, `chat` e `command` de
  comandos desconhecidos têm comportamento definido.

## Verificação

`node tools/verify.mjs mod.wasm` confirma:

- `horizon:mods@0.1.0` sem imports WASI;
- imports `input`, `render`, `storage`, `ui` — exatamente as funções
  `input.register-key`, `input.poll`, `input.claim-wheel`, `render.set-zoom`,
  `storage.get`, `storage.set`, `ui.set-settings`;
- exports `events@0.1.0` e `ui-events@0.1.0`;
- `api_level.rs` de `e7614d2`: `LEVEL = 7`, `neededApiLevel = 0` (todas as
  funções usadas são de nível 0).

`node tools/test-host.mjs` transpõe o componente para JS com `jco`, fornece um
host simulado e valida **30/30** casos: init/registro, página de settings,
tecla, roda (+1/-1), `set-zoom` com `turn` proporcional, `claim-wheel`,
comandos, persistência e ciclos da UI.

## Arquivos

- `horizon-mod.toml`, `mod.wasm`, `zoom-wasm-e7614d2.zip`: entrega.
- `Cargo.toml`, `src/lib.rs`: implementação Rust usando o SDK do Horizon e
  `export_mod!`.
- `vendor/horizon-mod-sdk/{Cargo.toml,src/lib.rs,wit/horizon.wit}`: SDK
  **do repositório Horizon**, não do crates.io; WIT original de 837 linhas.
- `wit/horizon.wit`: cópia integral do WIT em `e7614d2`.
- `wit/zoom.wit`: mundo `zoom-mod` derivado, com apenas os imports/exports
  usados pelo mod (mesmo pacote `horizon:mods@0.1.0`).
- `zig/zoom.c`: core WASM freestanding usado para gerar o `mod.wasm` entregue.
- `js/zoom.mjs`: implementação JS equivalente para o fallback
  `componentize-js`.
- `tools/build.sh`: caminho canônico `rustup target add
  wasm32-unknown-unknown` → `cargo build --release` →
  `wit_component::ComponentEncoder`.
- `tools/componentize/`: helper Rust com `ComponentEncoder::default()
  .module(core)?.validate(true).encode()?`.
- `tools/build-core.sh`, `tools/componentize-core.mjs`: build do core C +
  `wit-component` via `jco-transpile`.
- `tools/build-componentize-js.mjs`: build alternativo ESM → componente.
- `tools/verify.mjs`, `tools/test-host.mjs`, `tools/package.py`: validação e
  empacotamento.
- `reference/e7614d2/`: snapshots de `api_level.rs`, `manifest.rs`,
  `runtime.rs`, Zoom oficial, manifesto/Cargo do exemplo e docs da revisão.

## Sobre a toolchain deste sandbox

O sandbox não tinha `rustc`, `cargo`, `rustup` nem o alvo
`wasm32-unknown-unknown`, e os espelhos normalmente usados para instalar o
toolchain estavam inacessíveis. Por isso o **`mod.wasm` entregue** foi gerado
por um core `wasm32-freestanding` (Zig/clang, sem WASI e sem libc) compilado
de `zig/zoom.c`, depois envolvido por `wit-component` (API
`componentEmbed`/`componentNew` de `jco-transpile`). O resultado é um
componente `horizon:mods@0.1.0` com a mesma ABI que o runtime de `e7614d2`
espera, mas **não é literalmente um `cargo build --target
wasm32-unknown-unknown`**. O código Rust, o SDK vendorizado, o helper
`wit_component::ComponentEncoder` e `tools/build.sh` estão incluídos para
reproduzir o build Rust exato onde a toolchain estiver disponível. O fallback
`npm run build:js` também gera um componente compatível via
`componentize-js`, porém com ~12 MB.

## Coords HUD

O artefato compilado do Coords HUD que o usuário mencionou **não está neste
repositório**: o histórico local tem apenas o commit enxertado `02c8c2e`
(`README.md`), e stash, reflog e objetos soltos não contêm fonte, manifesto,
`api_level` ou imports de componente desse mod. O exemplo upstream de Coords
HUD em `e7614d2` usa `api = "0.1"` e capacidades diferentes
(`game-read`, `hud`, `chat`). Portanto, só é possível afirmar a compatibilidade
declarada de `api = "0.1"` e o nível 0 dos imports de `horizon:mods@0.1.0`;
não foi possível comparar o binário do Coords HUD do usuário com este
`mod.wasm`.

## Comandos

```bash
npm install
npm run build:core      # gera mod.wasm + zip (core C + wit-component)
npm run verify          # imports, exports, api_level, ausência de WASI
npm test                # host simulado, 30 checagens
npm run build:js        # fallback componentize-js
bash tools/build.sh     # caminho Rust exato (requer toolchain Rust)
python3 tools/package.py
```
