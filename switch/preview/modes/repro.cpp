// Scripted crash repros over the real dashboard; pair with the PREVIEW_SANITIZE=1 build.
// repro-sync-a: tile -> A -> GamePanel "Sync now" -> sync dialog Done -> B -> A on the tile again
// after REPRO_DELAY_MS, for REPRO_CYCLES cycles, then quit.
#include <borealis.hpp>
#include "registry.h"
#include "kit_preview.h"
#include <cstdlib>
#include <deque>
#include <typeinfo>
#include <vector>
#include "title_list_activity.h"
#include "kit/confirm_dialog.h"
#include "kit/title_grid.h"

namespace {

int env_int(const char* name, int fallback) {
    const char* v = getenv(name);
    return v && *v ? atoi(v) : fallback;
}

bool focus_on_tile() { return dynamic_cast<TitleTile*>(brls::Application::getCurrentFocus()) != nullptr; }

// Read-only peek at borealis' private activity/focus stacks: an explicit instantiation may name
// private members, so this needs no `#define private public` (which can change class layout).
template <class Tag, typename Tag::type Member>
struct Peek {
    friend typename Tag::type peek(Tag) { return Member; }
};
struct ActivitiesTag { using type = std::vector<brls::Activity*>*; friend type peek(ActivitiesTag); };
struct FocusStackTag { using type = std::vector<brls::View*>*; friend type peek(FocusStackTag); };
struct TokensTag { using type = unsigned*; friend type peek(TokensTag); };
template struct Peek<ActivitiesTag, &brls::Application::activitiesStack>;
template struct Peek<FocusStackTag, &brls::Application::focusStack>;
template struct Peek<TokensTag, &brls::Application::blockInputsTokens>;

void dump_stack(const char* when) {
    const std::vector<brls::Activity*>& stack = *peek(ActivitiesTag{});
    printf("[repro] stack @%s: %zu activities, focusStack=%zu, tokens=%u, focus=%p\n", when, stack.size(),
           peek(FocusStackTag{})->size(), *peek(TokensTag{}), (void*)brls::Application::getCurrentFocus());
    for (size_t i = 0; i < stack.size(); i++) {
        brls::Activity* a = stack[i];
        printf("[repro]   [%zu] %s %p hidden=%d\n", i, typeid(*a).name(), (void*)a, (int)a->isHidden());
    }
}

struct Step {
    int delay_ms;
    std::function<void()> fn;
};

class ReproScript {
  public:
    void add(int delay_ms, std::function<void()> fn) { steps_.push_back({delay_ms, std::move(fn)}); }

    void start() {
        last_us_ = brls::getCPUTimeUsec();
        timer_.setCallback([this] { tick(); });
        timer_.start(1);
    }

  private:
    void tick() {
        const brls::Time now = brls::getCPUTimeUsec();
        while (!steps_.empty() && now - last_us_ >= (brls::Time)steps_.front().delay_ms * 1000) {
            Step s = std::move(steps_.front());
            steps_.pop_front();
            last_us_ = now;
            s.fn();
            if (s.delay_ms > 0) break;
        }
    }

    std::deque<Step> steps_;
    brls::Time last_us_ = 0;
    brls::RepeatingTimer timer_;
};

void press(const char* what, brls::ControllerButton b) {
    printf("[repro] press %s (focus=%s)\n", what, focus_on_tile() ? "tile" : "other");
    dump_stack("before press");
    preview_press(b);
}

// Pushes the dashboard and returns a script whose first step focuses a tile.
ReproScript* dashboard_script(Session& session) {
    preview_seed_session(session);
    brls::Application::pushActivity(new TitleListActivity(preview_sync_controller(&session.config), &session));
    auto* s = new ReproScript();
    s->add(PREVIEW_READY_DELAY_MS, [] {
        printf("[repro] focus a tile\n");
        for (int i = 0; i < 3; i++) preview_press(brls::BUTTON_RIGHT);
    });
    return s;
}

void finish(ReproScript* s, int delay_ms) {
    s->add(delay_ms, [] {
        printf("[repro] all cycles done (focus=%s) -> quit\n", focus_on_tile() ? "tile" : "other");
        brls::Application::quit();
    });
    s->start();
}

void run_repro(Session& session) {
    const int delay  = env_int("REPRO_DELAY_MS", 250);
    const int cycles = env_int("REPRO_CYCLES", 4);
    printf("[repro] start: delay=%d ms cycles=%d\n", delay, cycles);

    ReproScript* s = dashboard_script(session);
    for (int c = 0; c < cycles; c++) {
        s->add(300, [c] {
            printf("[repro] cycle %d: open game panel\n", c);
            g_preview_sync_running = false;
            g_preview_sync_done    = false;
            press("A (open game)", ws_brls(WsAction::OpenGame));
        });
        s->add(500, [] { press("A (sync now)", ws_brls(WsAction::Confirm)); });
        s->add(600, [] {
            printf("[repro] stub sync -> running\n");
            g_preview_sync_running = true;
        });
        s->add(400, [] {
            printf("[repro] stub sync -> done\n");
            g_preview_sync_running = false;
            g_preview_sync_done    = true;
        });
        s->add(500, [] { press("B (close dialog)", ws_brls(WsAction::Back)); });
        s->add(delay, [delay] {
            printf("[repro] %d ms after close\n", delay);
            press("A (open game again)", ws_brls(WsAction::OpenGame));
        });
        s->add(400, [] {
            if (!focus_on_tile()) return;
            printf("[repro] A was dropped (inputs blocked); retry\n");
            press("A (open game retry)", ws_brls(WsAction::OpenGame));
        });
        s->add(500, [c] {
            printf("[repro] cycle %d: close game panel\n", c);
            press("B (close panel)", ws_brls(WsAction::Back));
        });
    }
    finish(s, 600);
}

// A-confirm closes a dialog whose root just started a click animation.
void run_repro_confirm(Session& session) {
    const int cycles = env_int("REPRO_CYCLES", 4);
    printf("[repro] confirm-a start: cycles=%d\n", cycles);

    ReproScript* s = dashboard_script(session);
    for (int c = 0; c < cycles; c++) {
        s->add(300, [c] {
            printf("[repro] cycle %d: push confirm dialog\n", c);
            OverlayActivity::push(new ConfirmDialog("Repro", "A confirms and closes",
                                                    [c] { printf("[repro] cycle %d: confirmed\n", c); }));
        });
        s->add(400, [] { press("A (confirm)", ws_brls(WsAction::Confirm)); });
        s->add(600, [] { dump_stack("after confirm"); });
    }
    finish(s, 300);
}

}

static PreviewModeReg reg_repro("repro-sync-a", &run_repro);
static PreviewModeReg reg_repro_confirm("repro-confirm-a", &run_repro_confirm);
