use std::{env, fs};
use wit_component::{ComponentEncoder, DecodedWasm};

/// Canonical Horizon e7614d2 build step: wrap a wasm32-unknown-unknown
/// core module with `wit_component::ComponentEncoder` (no wasm32-wasip2).
fn main() -> Result<(), Box<dyn std::error::Error>> {
    let mut args = env::args().skip(1);
    let input = args.next().ok_or("usage: componentize <core.wasm> <mod.wasm>")?;
    let output = args.next().ok_or("usage: componentize <core.wasm> <mod.wasm>")?;

    let core = fs::read(&input)?;
    let wasm = ComponentEncoder::default()
        .module(&core)?
        .validate(true)
        .encode()?;
    fs::write(&output, &wasm)?;

    let (resolve, world) = match wit_component::decode(&wasm)? {
        DecodedWasm::Component(resolve, world) => (resolve, world),
        DecodedWasm::WitPackage(..) => return Err("expected a component".into()),
    };
    let world = &resolve.worlds[world];
    println!(
        "wrote {} ({} bytes; {} imports, {} exports)",
        output,
        wasm.len(),
        world.imports.len(),
        world.exports.len()
    );
    Ok(())
}
