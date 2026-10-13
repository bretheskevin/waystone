#pragma once
#include <borealis.hpp>

// Shown at boot when the .nro was launched as a LibraryApplet (e.g. from the
// album): applet mode has too little memory to mount game saves. Explains the
// "hold R over any game" relaunch ritual; the only action is Exit.
class AppletModeActivity : public brls::Activity {
  public:
    brls::View* createContentView() override;
    void onContentAvailable() override;
};
