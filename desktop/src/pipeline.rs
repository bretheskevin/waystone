// Orchestration delegated to waystone-sync. This module is kept for the integration test only.

#[cfg(test)]
mod tests {
    /// Live two-device round-trip against a real dufs instance.
    ///
    /// Requires: docker run -d -v <vol>:/data -p 5099:5000 sigoden/dufs /data --allow-all
    #[tokio::test]
    #[ignore]
    async fn two_device_push_pull_round_trip() {
        use std::sync::Arc;
        use waystone_core::conflict::{ConflictPolicy, ConflictWinner, SyncDecision, fold_heads};
        use waystone_core::crypto::Vault;
        use waystone_core::model::{
            Confidence, GameRef, NormalizedSave, SaveId, SaveKind, SystemId, build_group_key,
        };

        let server = "http://localhost:5099";

        let (vault1, _recovery) = Vault::init("integration-test").unwrap();
        let keys_json = vault1.keys_json().unwrap();

        let async_dav = crate::webdav::WebDavClient::new(server, None, None);
        async_dav
            .put("/keys.json", keys_json.clone())
            .await
            .unwrap();

        let game_id = SaveId {
            source: "jksv".into(),
            system: SystemId::Switch,
            game: GameRef {
                key: "INTEGRATION_GAME_001".into(),
                display_name: "Integration Test Game".into(),
                confidence: Confidence::Strong,
                title_id: Some("INTEGRATION_GAME_001".into()),
                serial: None,
                rom_crc: None,
            },
            slot: "main".into(),
            kind: SaveKind::Native,
        };
        let group_key = build_group_key(SystemId::Switch, "INTEGRATION_GAME_001", "main");

        let vault2 = Vault::unlock_with_passphrase("integration-test", &keys_json).unwrap();

        let dav1 = crate::webdav::make_blocking_dav(server, None, None)
            .await
            .unwrap();
        let dav2 = crate::webdav::make_blocking_dav(server, None, None)
            .await
            .unwrap();
        let vault1 = Arc::new(vault1);
        let vault2 = Arc::new(vault2);

        let save_old = NormalizedSave {
            id: game_id.clone(),
            group_key: group_key.clone(),
            portable: true,
            mtime: "2026-01-01T00:00:00Z".into(),
            files: vec![("save.dat".into(), b"shared-initial-save".to_vec())],
        };
        waystone_sync::push_one(&vault1, &save_old, "integration-device-1", &dav1).unwrap();
        waystone_sync::push_one(&vault2, &save_old, "integration-device-2", &dav2).unwrap();

        let save_new = NormalizedSave {
            id: game_id.clone(),
            group_key: group_key.clone(),
            portable: true,
            mtime: "2026-01-02T00:00:00Z".into(),
            files: vec![("save.dat".into(), b"device1-progress-content".to_vec())],
        };
        waystone_sync::push_one(&vault1, &save_new, "integration-device-1", &dav1).unwrap();

        let all_heads = waystone_sync::read_remote_heads(&vault2, &save_old, &dav2).unwrap();
        assert_eq!(all_heads.len(), 2, "should find both device heads");

        let (entry2_old, _) = waystone_core::packaging::package(&save_old);
        let decision = waystone_core::conflict::decide_pull(
            Some(&entry2_old.content.hash),
            &entry2_old.mtime,
            &all_heads,
            "integration-device-2",
            ConflictPolicy::NewestWins,
        );

        let head_hash = match &decision {
            SyncDecision::Pull { head_hash } => head_hash.clone(),
            SyncDecision::ConflictResolved {
                winner: ConflictWinner::Remote,
                ..
            } => fold_heads(&all_heads).unwrap().hash,
            other => panic!("expected Pull or ConflictResolved(Remote), got {:?}", other),
        };

        let zip_bytes = waystone_sync::fetch_blob(&vault2, &save_old, &head_hash, &dav2).unwrap();
        let files = waystone_core::packaging::unzip(&zip_bytes).unwrap();
        let save_file = files
            .iter()
            .find(|(p, _)| p == "save.dat")
            .expect("save.dat");
        assert_eq!(
            save_file.1, b"device1-progress-content",
            "device-2 should have received device-1's updated save"
        );
    }
}
