#pragma once
#include "session.h"
#include "wizard.h"
#include "keys_file.h"
#include "vault_remote.h"
#include "setup_model.h"
#include <3ds.h>
#include <functional>
#include <string>
#include <vector>

// The vault secret field shared by the unlock screen and the setup join step: passphrase
// (masked) or recovery key, toggled by WsAction::ToggleRecovery.
WizardFieldDef vault_secret_field(size_t value_index, bool recovery);

// When kDown holds WsAction::ToggleRecovery: flips *recovery, wipes the secret field and
// reloads the wizard with make_steps() (built for the new mode). Returns true when it toggled.
bool vault_secret_toggle(u32 kDown, bool* recovery, Wizard* wizard, size_t value_index,
                         const std::function<std::vector<WizardStepDef>()>& make_steps);
