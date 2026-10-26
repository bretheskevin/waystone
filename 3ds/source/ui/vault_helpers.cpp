#include "vault_helpers.h"
#include "keymap_3ds.h"
#include <cstdio>

WizardFieldDef vault_secret_field(size_t value_index, bool recovery) {
    WizardFieldDef f;
    f.value_index = value_index;
    f.label       = vault_secret_label(recovery);
    f.is_secret   = !recovery;
    f.placeholder = "not set";
    f.hint        = vault_secret_hint(recovery, ws_glyph(ws_button(WsAction::ToggleRecovery)));
    return f;
}

bool vault_secret_toggle(u32 kDown, bool* recovery, Wizard* wizard, size_t value_index,
                         const std::function<std::vector<WizardStepDef>()>& make_steps) {
    if (!(kDown & ws_key(WsAction::ToggleRecovery))) return false;
    *recovery = !*recovery;
    printf("[ui] wizard: recovery mode -> %s\n", *recovery ? "on" : "off");
    zeroize_string(wizard->value(value_index));
    wizard->clear_error();
    wizard->reload_steps(make_steps());
    return true;
}
