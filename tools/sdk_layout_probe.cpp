// sdk_layout_probe: prints the size and alignment of the SDK classes the game
// allocates or derives from. tools/sdk_layout_check.sh compiles it against the
// prebuilt SDK headers (what the game compiles against) and against the
// patched SDK source (what the staged runtime and plugin were built from);
// the two outputs must match. See tools/sdk_layout_check.sh.
#include <rex/input/device_assignment.h>
#include <rex/input/input_driver.h>
#include <rex/logging/sink.h>
#include <rex/runtime.h>
#include <rex/ui/imgui_dialog.h>
#include <rex/ui/imgui_drawer.h>
#include <rex/ui/overlay/achievement_toast.h>
#include <rex/ui/overlay/achievements_overlay.h>
#include <rex/ui/overlay/console_overlay.h>
#include <rex/ui/overlay/debug_overlay.h>
#include <rex/ui/overlay/settings_overlay.h>
#include <rex/ui/window.h>
#include <rex/ui/window_listener.h>

#include <cstdio>

#define PROBE(type) std::printf("%-36s %6zu %3zu\n", #type, sizeof(type), alignof(type))

int main() {
  // Allocated by the game (rex_app.cpp and src/).
  PROBE(rex::Runtime);
  PROBE(rex::LogCaptureSink);
  PROBE(rex::input::SharedAssignment);
  PROBE(rex::ui::ImGuiDrawer);
  PROBE(rex::ui::DebugOverlayDialog);
  PROBE(rex::ui::FrameStats);
  PROBE(rex::ui::SettingsDialog);
  PROBE(rex::ui::ConsoleDialog);
  PROBE(rex::ui::AchievementToastDialog);
  PROBE(rex::ui::AchievementsOverlayDialog);
  // Base classes of the game's own classes.
  PROBE(rex::ui::ImGuiDialog);
  PROBE(rex::ui::Window);
  PROBE(rex::ui::WindowListener);
  PROBE(rex::ui::WindowInputListener);
  PROBE(rex::input::InputDriver);
  return 0;
}
