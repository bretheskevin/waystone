// Host-only test for the console setup/unlock wizard text rules shared by Switch and 3DS.
// Build (from repo root):
//   g++ -std=c++11 -Wall -Wextra -I shell-common shell-common/tests/test_setup_model.cpp \
//     -o /tmp/test_setup_model && /tmp/test_setup_model
#include "setup_model.h"
#include <cassert>
#include <cstdio>
#include <cstring>

static void test_probe_error_without_status() {
    assert(setup_probe_error(-1) == "Can't reach the server - check the URL and login");
    printf("test_probe_error_without_status PASSED\n");
}

static void test_probe_error_with_status() {
    assert(setup_probe_error(401) == "Can't reach the server - check the URL and login (HTTP 401)");
    assert(setup_probe_error(0) == "Can't reach the server - check the URL and login (HTTP 0)");
    printf("test_probe_error_with_status PASSED\n");
}

static void test_finish_labels() {
    assert(strcmp(setup_finish_label(SetupVaultMode::Create, false), "Create Vault") == 0);
    assert(strcmp(setup_finish_label(SetupVaultMode::Create, true), "Creating vault...") == 0);
    assert(strcmp(setup_finish_label(SetupVaultMode::Join, false), "Join") == 0);
    assert(strcmp(setup_finish_label(SetupVaultMode::Join, true), "Joining vault...") == 0);
    assert(strcmp(setup_work_status(SetupVaultMode::Create), "Creating vault... please wait") == 0);
    assert(strcmp(setup_work_status(SetupVaultMode::Join), "Joining vault... please wait") == 0);
    assert(strcmp(setup_probe_status(), "Checking server...") == 0);
    printf("test_finish_labels PASSED\n");
}

static void test_step_titles() {
    assert(strcmp(setup_vault_step_title(SetupVaultMode::Create), "Vault Passphrase") == 0);
    assert(strcmp(setup_vault_step_title(SetupVaultMode::Join), "Join your vault") == 0);
    printf("test_step_titles PASSED\n");
}

static void test_vault_secret_text() {
    assert(strcmp(vault_secret_label(false), "Vault Passphrase") == 0);
    assert(strcmp(vault_secret_label(true), "Recovery Key") == 0);
    assert(vault_secret_hint(false, "Y") == "enter your vault passphrase (Y to switch to recovery key)");
    assert(vault_secret_hint(true, "Y") == "enter your hex recovery key (Y to switch to passphrase)");
    assert(strcmp(vault_secret_required(false), "Passphrase is required") == 0);
    assert(strcmp(vault_secret_required(true), "Recovery key is required") == 0);
    printf("test_vault_secret_text PASSED\n");
}

static void test_plain_mode_labels() {
    assert(strcmp(setup_finish_label(SetupVaultMode::CreatePlain, false), "Finish") == 0);
    assert(strcmp(setup_finish_label(SetupVaultMode::CreatePlain, true), "Setting up...") == 0);
    assert(strcmp(setup_finish_label(SetupVaultMode::JoinPlain, false), "Join") == 0);
    assert(strcmp(setup_finish_label(SetupVaultMode::JoinPlain, true), "Joining...") == 0);
    assert(strcmp(setup_work_status(SetupVaultMode::CreatePlain), "Setting up... please wait") == 0);
    assert(strcmp(setup_work_status(SetupVaultMode::JoinPlain), "Joining server... please wait") == 0);
    assert(strcmp(setup_vault_step_title(SetupVaultMode::CreatePlain), "Encryption") == 0);
    assert(strcmp(setup_vault_step_title(SetupVaultMode::JoinPlain), "Join your server") == 0);
    printf("test_plain_mode_labels PASSED\n");
}

static void test_vault_step_hints() {
    assert(setup_vault_step_hint(SetupVaultMode::CreatePlain, "Y") ==
           "Saves are stored unencrypted, protected by your WebDAV login (Y to encrypt with a passphrase)");
    assert(setup_vault_step_hint(SetupVaultMode::Create, "Y") ==
           "choose a strong passphrase to encrypt your saves (Y for no encryption)");
    assert(setup_vault_step_hint(SetupVaultMode::JoinPlain, "Y") ==
           "This server stores saves unencrypted - no passphrase needed");
    assert(setup_vault_step_hint(SetupVaultMode::Join, "Y").empty());
    printf("test_vault_step_hints PASSED\n");
}

static void test_mode_is_plain_and_toggle() {
    assert(setup_mode_is_plain(SetupVaultMode::CreatePlain));
    assert(setup_mode_is_plain(SetupVaultMode::JoinPlain));
    assert(!setup_mode_is_plain(SetupVaultMode::Create));
    assert(!setup_mode_is_plain(SetupVaultMode::Join));
    assert(setup_toggle_encryption(SetupVaultMode::CreatePlain) == SetupVaultMode::Create);
    assert(setup_toggle_encryption(SetupVaultMode::Create) == SetupVaultMode::CreatePlain);
    assert(setup_toggle_encryption(setup_toggle_encryption(SetupVaultMode::CreatePlain)) ==
           SetupVaultMode::CreatePlain);
    assert(setup_toggle_encryption(SetupVaultMode::Join) == SetupVaultMode::Join);
    assert(setup_toggle_encryption(SetupVaultMode::JoinPlain) == SetupVaultMode::JoinPlain);
    printf("test_mode_is_plain_and_toggle PASSED\n");
}

static void test_shared_passphrase_text() {
    assert(strcmp(setup_passphrase_label(), "Passphrase") == 0);
    assert(strcmp(setup_confirm_label(), "Confirm Passphrase") == 0);
    assert(strcmp(setup_confirm_hint(), "re-enter your vault passphrase") == 0);
    assert(strcmp(setup_passphrase_mismatch(), "Passphrases do not match") == 0);
    assert(strcmp(setup_confirm_required(), "Please confirm your passphrase") == 0);
    assert(strcmp(recovery_key_warning(),
                  "If you forget your passphrase, this is the ONLY way to recover your vault.") == 0);
    printf("test_shared_passphrase_text PASSED\n");
}

static void test_unlock_text() {
    assert(strcmp(unlock_title(true), "Log in") == 0);
    assert(strcmp(unlock_title(false), "Unlock Vault") == 0);
    assert(strcmp(unlock_finish_label(true), "Log in") == 0);
    assert(strcmp(unlock_finish_label(false), "Unlock") == 0);
    assert(strcmp(unlock_password_hint(true), "Enter your WebDAV password") == 0);
    assert(strcmp(unlock_password_hint(false), "your WebDAV password") == 0);
    assert(unlock_failed_message(true, "boom") == "Login failed: boom");
    assert(unlock_failed_message(true, nullptr) == "Login failed: invalid keys.json");
    assert(unlock_failed_message(false, "boom") == "Unlock failed: boom");
    assert(unlock_failed_message(false, nullptr) == "Unlock failed: wrong passphrase or key");
    printf("test_unlock_text PASSED\n");
}

static void test_mode_after_probe_and_join() {
    typedef SetupVaultMode M;
    assert(setup_mode_after_probe(true, true, M::Create) == M::JoinPlain);
    assert(setup_mode_after_probe(true, false, M::CreatePlain) == M::Join);
    assert(setup_mode_after_probe(false, false, M::Create) == M::Create);
    assert(setup_mode_after_probe(false, false, M::CreatePlain) == M::CreatePlain);
    assert(setup_mode_after_probe(false, false, M::Join) == M::CreatePlain);
    assert(setup_mode_after_probe(false, false, M::JoinPlain) == M::CreatePlain);
    assert(setup_mode_is_join(M::Join) && setup_mode_is_join(M::JoinPlain));
    assert(!setup_mode_is_join(M::Create) && !setup_mode_is_join(M::CreatePlain));
    assert(setup_mode_shows_recovery_key(M::Create));
    assert(!setup_mode_shows_recovery_key(M::CreatePlain));
    assert(!setup_mode_shows_recovery_key(M::Join) && !setup_mode_shows_recovery_key(M::JoinPlain));
    assert(setup_probe_error(503, "") == setup_probe_error(503));
    assert(setup_probe_error(200, "Unrecognized keys.json on server") ==
           "Unrecognized keys.json on server");
    printf("test_mode_after_probe_and_join PASSED\n");
}

static void test_forget_server_text() {
    assert(strcmp(forget_server_title(), "Forget this server?") == 0);
    assert(strcmp(forget_server_detail(),
                  "Deletes the local login (keys, session, recovery file). "
                  "Saves on the server are not touched.") == 0);
    assert(strcmp(forget_server_confirm_label(), "Forget") == 0);
    printf("test_forget_server_text PASSED\n");
}

static void test_setup_welcome_text() {
    assert(strcmp(setup_welcome_title(), "Welcome to Waystone") == 0);
    assert(setup_welcome_hint("(R)") == "Press (R) to begin setup");
    assert(setup_welcome_hint("R:") == "Press R: to begin setup");
    setup_log_prefill("https://user:pw@dav.example", "alice");
    setup_log_prefill("", "");
    printf("test_setup_welcome_text PASSED\n");
}

static void test_prelogin_extra_actions() {
    std::vector<WsAction> unlock = unlock_extra_actions();
    assert(unlock.size() == 2);
    assert(unlock[0] == WsAction::ForgetServer && unlock[1] == WsAction::CheckUpdates);
    assert(setup_step_offers_updates(0));
    assert(!setup_step_offers_updates(1));
    std::vector<WsAction> welcome = setup_extra_actions(0);
    assert(welcome.size() == 1 && welcome[0] == WsAction::CheckUpdates);
    assert(setup_extra_actions(3).empty());
    printf("test_prelogin_extra_actions PASSED\n");
}

int main() {
    test_probe_error_without_status();
    test_probe_error_with_status();
    test_finish_labels();
    test_step_titles();
    test_vault_secret_text();
    test_plain_mode_labels();
    test_vault_step_hints();
    test_mode_is_plain_and_toggle();
    test_shared_passphrase_text();
    test_unlock_text();
    test_mode_after_probe_and_join();
    test_forget_server_text();
    test_setup_welcome_text();
    test_prelogin_extra_actions();
    printf("ALL setup_model TESTS PASSED\n");
    return 0;
}
