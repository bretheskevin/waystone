use std::path::Path;
use std::process::Command;

#[test]
fn committed_kotlin_matches_uniffi_bindgen_output() {
    let manifest_dir = env!("CARGO_MANIFEST_DIR");
    let workspace_root = Path::new(manifest_dir)
        .parent()
        .expect("mobile crate must be inside a workspace");

    let build = Command::new("cargo")
        .args(["build", "-p", "waystone-mobile"])
        .current_dir(workspace_root)
        .status()
        .expect("cargo build must succeed");
    assert!(build.success(), "cargo build -p waystone-mobile failed");

    let cdylib = if cfg!(target_os = "macos") {
        workspace_root.join("target/debug/libwaystone_mobile.dylib")
    } else {
        workspace_root.join("target/debug/libwaystone_mobile.so")
    };
    assert!(cdylib.exists(), "cdylib must exist at {:?}", cdylib);

    let temp_dir = tempfile::tempdir().expect("temp dir");

    let bindgen = Command::new("cargo")
        .args([
            "run",
            "-p",
            "waystone-mobile",
            "--bin",
            "uniffi-bindgen",
            "--",
            "generate",
            "--library",
        ])
        .arg(&cdylib)
        .args(["--language", "kotlin", "--out-dir"])
        .arg(temp_dir.path())
        .arg("--no-format")
        .current_dir(workspace_root)
        .status()
        .expect("uniffi-bindgen must run");
    assert!(bindgen.success(), "uniffi-bindgen failed");

    let bindings_dir = Path::new(manifest_dir).join("bindings");
    let generated_kts = find_kt_files(temp_dir.path());
    assert!(
        !generated_kts.is_empty(),
        "uniffi-bindgen produced no .kt files"
    );

    for generated_path in &generated_kts {
        let filename = generated_path.file_name().unwrap().to_str().unwrap();
        let generated = std::fs::read_to_string(generated_path).expect("read generated kt");

        let committed_path = find_kt_file(&bindings_dir, filename);
        let committed = std::fs::read_to_string(&committed_path).unwrap_or_else(|_| {
            panic!(
                "committed binding {:?} not found; regenerate with: ./mobile/generate-bindings.sh",
                committed_path
            )
        });

        if generated != committed {
            panic!(
                "mobile/bindings/{} is stale!\n\
                 Regenerate with: ./mobile/generate-bindings.sh\n\n\
                 First 500 chars of generated:\n{}\n",
                filename,
                &generated[..generated.len().min(500)]
            );
        }
    }
}

fn find_kt_files(dir: &Path) -> Vec<std::path::PathBuf> {
    let mut result = Vec::new();
    if let Ok(entries) = std::fs::read_dir(dir) {
        for entry in entries.flatten() {
            let path = entry.path();
            if path.is_dir() {
                result.extend(find_kt_files(&path));
            } else if path.extension().is_some_and(|e| e == "kt") {
                result.push(path);
            }
        }
    }
    result
}

fn find_kt_file(dir: &Path, filename: &str) -> std::path::PathBuf {
    for path in find_kt_files(dir) {
        if path.file_name().is_some_and(|n| n == filename) {
            return path;
        }
    }
    dir.join(filename)
}
