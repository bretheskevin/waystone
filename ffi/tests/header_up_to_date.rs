use std::path::Path;

#[test]
fn committed_header_matches_cbindgen_output() {
    let manifest_dir = env!("CARGO_MANIFEST_DIR");
    let workspace_root = Path::new(manifest_dir)
        .parent()
        .expect("ffi crate must be inside a workspace");

    let config = cbindgen::Config::from_file(workspace_root.join("ffi/cbindgen.toml"))
        .expect("cbindgen.toml must be readable");

    let bindings = cbindgen::Builder::new()
        .with_crate(workspace_root.join("ffi"))
        .with_config(config)
        .generate()
        .expect("cbindgen must be able to parse the crate");

    let mut generated_bytes = Vec::new();
    bindings.write(&mut generated_bytes);
    let generated =
        String::from_utf8(generated_bytes).expect("cbindgen output must be valid UTF-8");

    let committed =
        std::fs::read_to_string(concat!(env!("CARGO_MANIFEST_DIR"), "/include/waystone.h"))
            .expect("ffi/include/waystone.h must exist");

    if generated != committed {
        panic!(
            "ffi/include/waystone.h is stale!\n\
             Regenerate with:\n  \
             cbindgen --config ffi/cbindgen.toml --crate waystone-ffi --output ffi/include/waystone.h\n\n\
             Diff (first 500 chars of generated):\n{}\n",
            &generated[..generated.len().min(500)]
        );
    }
}
