#pragma once
#include <functional>
#include <string>
#include "kit/overlay.h"
#include "saves.h"
#include "session.h"

// Per-game slide-out (A on a tile): Sync now / History / Snapshots. History and Snapshots
// stack their own panels on top; Sync now closes this panel first.
class GamePanel : public SlidePanel {
  public:
    static constexpr float WIDTH = 420.0f;

    GamePanel(TitleInfo title, Session* session, std::string detail,
              std::function<void(TitleInfo)> on_sync_now);

  protected:
    void buildBody(brls::Box* body) override;

  private:
    void openHistory();
    void openSnapshots();

    TitleInfo   title_;
    Session*    session_;
    std::string detail_;
    std::function<void(TitleInfo)> on_sync_now_;
};
