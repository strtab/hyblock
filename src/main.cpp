// hyblock: keeps floating windows from going above the monitor reserved top
// area. Primary mechanism: a function hook on CWindowTarget::setPositionGlobal,
// which every geometry write goes through (mouse drag, dispatchers, rules,
// client requests). Fallback: a per-frame check that re-applies the limit
// through the layout manager.
#include <hyprland/src/Compositor.hpp>
#include <hyprland/src/desktop/state/WindowState.hpp>
#include <hyprland/src/desktop/view/Window.hpp>
#include <hyprland/src/layout/LayoutManager.hpp>
#include <hyprland/src/layout/target/Target.hpp>
#include <hyprland/src/managers/fullscreen/FullscreenController.hpp>
#include <hyprland/src/output/Monitor.hpp>
#include <hyprland/src/plugins/PluginAPI.hpp>
#include <hyprland/src/state/MonitorState.hpp>

#include <algorithm>
#include <cmath>
#include <vector>

inline HANDLE PHANDLE = nullptr;
inline std::vector<CHyprSignalListener> g_listeners;
inline CFunctionHook *g_setPositionHook = nullptr;

using SetPositionFn = void (*)(void *, const Layout::STargetBox &, uint8_t);

// Tolerance in logical pixels to avoid re-applying geometry for sub-pixel
// differences.
static constexpr double EPSILON = 0.5;

// Picks the monitor by the center of the box, which is what the drag logic uses
// too. Falls back to the monitor the window currently belongs to.
static PHLMONITOR monitorForBox(const CBox &box, const PHLWINDOW &window) {
  PHLMONITOR mon = State::monitorState()->query().vec(box.middle()).run();

  if (!mon && window)
    mon = window->m_monitor.lock();

  return mon;
}

// Moves the top edge of the box down to minY.
// For a plain move the whole box is shifted; for a resize the bottom edge stays
// where it is.
static void clampTop(CBox &box, double minY, bool resized) {
  if (box.y + EPSILON >= minY)
    return;

  if (resized)
    box.h = std::max(1.0, box.h - (minY - box.y));

  box.y = minY;
}

static void hkSetPositionGlobal(void *thisptr, const Layout::STargetBox &box,
                                uint8_t flags) {
  Layout::STargetBox adjusted = box;
  const auto TARGET = reinterpret_cast<Layout::ITarget *>(thisptr);

  if (TARGET && TARGET->floating()) {
    const auto WINDOW = TARGET->window();

    // Fullscreen windows must be allowed to cover the reserved area.
    if (WINDOW && !Fullscreen::controller()->isFullscreen(WINDOW)) {
      const auto MON = monitorForBox(box.logicalBox, WINDOW);

      if (MON) {
        // Top edge of the monitor area that is not covered by reserved space
        // (bars, etc.).
        const double MIN_Y = MON->logicalBoxMinusReserved().y;

        // A size change relative to the current box means a resize, otherwise
        // it is a move.
        const CBox CURRENT = TARGET->position();
        const bool RESIZED =
            CURRENT.w > 1 && CURRENT.h > 1 &&
            (std::abs(box.logicalBox.w - CURRENT.w) > EPSILON ||
             std::abs(box.logicalBox.h - CURRENT.h) > EPSILON);

        clampTop(adjusted.logicalBox, MIN_Y, RESIZED);
        clampTop(adjusted.visualBox, MIN_Y, RESIZED);
      }
    }
  }

  (*(SetPositionFn)g_setPositionHook->m_original)(thisptr, adjusted, flags);
}

// Safety net for any path that does not go through the hooked function.
static void clampWindow(const PHLWINDOW &window,
                        const PHLMONITOR &renderedMonitor) {
  if (!Desktop::View::validMapped(window) || window->isHidden())
    return;

  const auto MON = window->m_monitor.lock();

  // Process each window only once per frame, on the monitor it belongs to.
  if (!MON || MON != renderedMonitor)
    return;

  const auto TARGET = window->layoutTarget();

  if (!TARGET || !TARGET->floating() ||
      Fullscreen::controller()->isFullscreen(window))
    return;

  CBox box = TARGET->position();
  const double MIN_Y = MON->logicalBoxMinusReserved().y;

  if (box.y + EPSILON >= MIN_Y)
    return;

  box.y = MIN_Y;
  g_layoutManager->setTargetGeom(box, TARGET);
}

// Do NOT change this function.
APICALL EXPORT std::string PLUGIN_API_VERSION() { return HYPRLAND_API_VERSION; }

APICALL EXPORT PLUGIN_DESCRIPTION_INFO PLUGIN_INIT(HANDLE handle) {
  PHANDLE = handle;

  const std::string COMPOSITOR_HASH = __hyprland_api_get_hash();
  const std::string CLIENT_HASH = __hyprland_api_get_client_hash();

  // Prevents random crashes caused by mismatched header versions.
  if (COMPOSITOR_HASH != CLIENT_HASH) {
    HyprlandAPI::addNotification(PHANDLE,
                                 "[hyblock] Mismatched headers! Can't proceed.",
                                 CHyprColor{1.0, 0.2, 0.2, 1.0}, 5000);
    throw std::runtime_error("[hyblock] Version mismatch");
  }

  // Hook the window target override, not the base ITarget one.
  const auto METHODS =
      HyprlandAPI::findFunctionsByName(PHANDLE, "setPositionGlobal");

  for (const auto &method : METHODS) {
    if (method.demangled.contains("CWindowTarget::setPositionGlobal")) {
      g_setPositionHook = HyprlandAPI::createFunctionHook(
          PHANDLE, method.address, (void *)&hkSetPositionGlobal);
      break;
    }
  }

  if (!g_setPositionHook || !g_setPositionHook->hook()) {
    g_setPositionHook = nullptr;
    HyprlandAPI::addNotification(PHANDLE,
                                 "[hyblock] Failed to hook setPositionGlobal, "
                                 "using per-frame fallback only.",
                                 CHyprColor{1.0, 0.7, 0.2, 1.0}, 7000);
  }

  // Runs right before a monitor frame is rendered.
  g_listeners.emplace_back(
      Event::bus()->m_events.render.pre.listen([](PHLMONITOR monitor) {
        if (!monitor)
          return;

        // Copy, because applying geometry may trigger events that modify the
        // window list.
        const std::vector<PHLWINDOW> WINDOWS =
            Desktop::windowState()->windows();

        for (const auto &window : WINDOWS)
          clampWindow(window, monitor);
      }));

  return {"hyblock",
          "Prevents floating windows from moving above the reserved top area",
          "Inai", "1.0"};
}

APICALL EXPORT void PLUGIN_EXIT() { g_listeners.clear(); }
