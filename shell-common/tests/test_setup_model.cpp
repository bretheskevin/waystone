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

int main() {
    test_probe_error_without_status();
    test_probe_error_with_status();
    test_finish_labels();
    test_step_titles();
    test_vault_secret_text();
    printf("ALL setup_model TESTS PASSED\n");
    return 0;
}
