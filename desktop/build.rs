use std::error::Error;

fn main() -> Result<(), Box<dyn Error>> {
    let manifest = std::path::PathBuf::from(std::env::var("CARGO_MANIFEST_DIR")?);
    let path = manifest.join("../VERSION");
    println!("cargo:rerun-if-changed={}", path.display());
    let raw = std::fs::read_to_string(&path)
        .map_err(|e| format!("repo-root VERSION file is missing ({e})"))?;
    let v = raw.trim();
    let ok = v.split('.').count() == 3
        && v.split('.')
            .all(|p| !p.is_empty() && p.bytes().all(|b| b.is_ascii_digit()));
    if !ok {
        return Err(format!("VERSION '{v}' is not X.Y.Z").into());
    }
    println!("cargo:rustc-env=WAYSTONE_VERSION={v}");
    println!(
        "cargo:rustc-env=WAYSTONE_TARGET={}",
        std::env::var("TARGET")?
    );
    Ok(())
}
