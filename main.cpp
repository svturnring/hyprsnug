#define WLR_USE_UNSTABLE

#include <unistd.h>

// Standard-library headers must be fully parsed *before* the "private public"
// hack below; otherwise libstdc++ templates (e.g. <sstream>) end up with a
// broken access model (-Wtemplate-body on g++).
#include <algorithm>
#include <cstdint>
#include <format>
#include <optional>
#include <sstream>
#include <string>
#include <vector>

#include <hyprland/src/includes.hpp>

// The fixed size of a freshly created scrolling column (its "column_width"
// fraction) is decided inside CScrollingAlgorithm::newTarget. That function
// runs while handling the embed / DnD drop, long before any Lua script could
// react, so the only way to inherit the column width from the window is to
// reach into the live algorithm instance. Making `private` public for this
// header lets us read m_scrollingData (centerOrFitCol + recalculate).
#define private public
#include <hyprland/src/layout/algorithm/tiled/scrolling/ScrollingAlgorithm.hpp>
#undef private

#include <hyprland/src/plugins/PluginAPI.hpp>
#include <hyprland/src/desktop/DesktopTypes.hpp>
#include <hyprland/src/desktop/view/window/Window.hpp>
#include <hyprland/src/event/EventBus.hpp>
#include <hyprland/src/layout/LayoutManager.hpp>
#include <hyprland/src/layout/space/Space.hpp>
#include <hyprland/src/layout/algorithm/Algorithm.hpp>
#include <hyprland/src/config/ConfigValue.hpp>
#include <hyprland/src/config/shared/complex/ComplexDataTypes.hpp>
#include <hyprland/src/config/values/ConfigValues.hpp>

#include "Globals.hpp"

// Plugin version (semver 2.0.0, see "Versioning" in AGENTS.md). The single
// source of truth is the VERSION file in the repository root: both the Makefile
// and CMake read it and pass it here as HYPRSNUG_VERSION. The fallback only
// appears in ad-hoc builds that bypass the build files.
#ifndef HYPRSNUG_VERSION
#define HYPRSNUG_VERSION "0.0.0-unknown"
#endif

namespace {

// RAII connection holders. Resetting them (in PLUGIN_EXIT) unregisters the
// listeners.
CHyprSignalListener g_listenerWindowFloating;
CHyprSignalListener g_listenerDragEnd;
CHyprSignalListener g_listenerMoveToWorkspace;
CHyprSignalListener g_listenerWindowOpen;
CHyprSignalListener g_listenerTick;

// The window that is being re-tiled at the very end of a tiled DnD drag.
//
// A tiled drag floats the window (the ~85% "ghost" that follows the mouse) and
// re-tiles it on drop. The floating-size event for that re-tile fires *before*
// the drag controller restores the window's original floating size, so reading
// the width there would use the ghost size. We remember the window instead and
// re-fit it from the drag-end event, which fires *after* the original size is
// restored.
WP<Desktop::View::CWindow> g_dragWindow;

// Plugin configuration values, bound once, eagerly, the moment PLUGIN_INIT has
// registered every "plugin:hyprsnug:*" key (pluginConfig() is force-constructed
// at the end of PLUGIN_INIT with "(void)pluginConfig();").
//
// Never bind a CConfigValue lazily from inside an event handler: while the
// config is being reloaded Hyprland briefly drops a plugin's registered values,
// and the first (re)bind inside a handler then trips the RASSERT in
// Config::mgr()->getConfigValue ("Something went really fucking wrong with
// config values"), aborting the compositor. Eager binding at init keeps the
// bind off the event path and makes that abort impossible at event time.
struct SPluginConfig {
    CConfigValue<Config::INTEGER> enabled{"plugin:hyprsnug:enabled"};
    CConfigValue<Config::STRING>  mode{"plugin:hyprsnug:mode"};
    CConfigValue<Config::INTEGER> syncPseudoSize{"plugin:hyprsnug:sync_pseudosize"};
    CConfigValue<Config::INTEGER> fixDragGhost{"plugin:hyprsnug:fix_drag_ghost"};
};

// Constructed on its first call, which is deliberately PLUGIN_INIT.
SPluginConfig& pluginConfig() {
    static SPluginConfig CFG;
    return CFG;
}

/*
    A tiled window moved to another workspace (movetoworkspace / Super+Shift+N)
    is re-tiled by CScrollingAlgorithm::newTarget into a fresh column created at
    the *default* width, so the window changes size. The moveToWorkspace event
    fires *before* that re-tile happens, so the fit is deferred to the next
    compositor tick. The captured width is the window's actual pixel width right
    before the move -- i.e. the size that must be preserved in the destination
    workspace.
*/
struct SPendingMoveFit {
    PHLWINDOW window;
    float     widthPx;
};
std::vector<SPendingMoveFit> g_pendingMoveFits;

/*
    If the window is tiled in the built-in scrolling layout and is the *sole*
    member of its column, resize that column so its width is inherited from the
    window's (last floating) size.

    Sole-member check: in scrolling, embedding a window always creates a fresh
    column, and a dragged window either creates its own column or joins an
    existing one. Only "own" columns are fitted -- resizing a column the window
    was dropped *into* would resize unrelated windows.
*/
void fitColumnToWindow(const PHLWINDOW& w, std::optional<float> widthPx = std::nullopt, std::optional<float> heightPx = std::nullopt) {
    if (!*pluginConfig().enabled)
        return;

    if (!w || w->isFloating())
        return;

    const auto TARGET = w->layoutTarget();
    if (!TARGET)
        return;

    if (!w->m_workspace || !w->m_workspace->space())
        return;

    const auto ALGO = w->m_workspace->space()->algorithm();
    if (!ALGO)
        return;

    auto* const SCROLLING = dynamic_cast<Layout::Tiled::CScrollingAlgorithm*>(ALGO->tiledAlgo().get());
    if (!SCROLLING)
        return; // not the scrolling layout, nothing to fit

    const auto TDATA = SCROLLING->dataFor(TARGET);
    if (!TDATA)
        return;

    const auto COL = TDATA->column.lock();
    if (!COL)
        return;

    if (COL->targetDatas.size() != 1)
        return;

    // widthPx / heightPx override the last floating size: workspace moves and
    // windows opened straight into the layout supply the size that must be
    // preserved, not a stale floating size from earlier in the window's life.
    const auto FS    = TARGET->lastFloatingSize();
    const float WINW = widthPx.value_or(FS.x);
    if (WINW <= 0.F)
        return; // the window never had a floating size (e.g. always tiled)

    const auto USABLE = SCROLLING->usableArea();
    if (USABLE.w <= 0.F)
        return;

    // 0.57 keeps the pseudo size and the floating size as two independent
    // quantities: a *pseudo* window is rendered at its pseudo size minus the
    // border reserved area (see CWindowTarget::updatePos), not at the column
    // size. Pin the pseudo size on every embed / re-tile so a stale size can
    // never make the window visibly change size, and in exact mode inflate it
    // by the border reserved extents so the rendered box equals the floating
    // size exactly (360 + 2*1 border = 362 pseudo -> 360 box).
    const auto   BORDER_RESERVED = w->getFullWindowReservedArea();
    double RESERVED_W           = (double)BORDER_RESERVED.topLeft.x + (double)BORDER_RESERVED.bottomRight.x;
    double RESERVED_H           = (double)BORDER_RESERVED.topLeft.y + (double)BORDER_RESERVED.bottomRight.y;

    // A window opened straight into the layout is fitted from the window.open
    // event, which fires before the compositor has measured the decoration
    // reserved extents (g_pDecorationPositioner->forceRecalcFor runs later in
    // CWindow::mapWindow), so getFullWindowReservedArea() can read 0 there even
    // though the window will render with a border. Fall back to the effective
    // border size (two edges per axis) so an exact fit is not off by the border.
    if (RESERVED_W <= 0. && RESERVED_H <= 0.) {
        const double BORDER = std::max(0.0, (double)w->m_ruleApplicator->borderSize().valueOrDefault());
        RESERVED_W = 2. * BORDER;
        RESERVED_H = 2. * BORDER;
    }

    if (*pluginConfig().syncPseudoSize && TARGET->isPseudo()) {
        const float FSY = heightPx.value_or(FS.y);
        if (FS.x > 0.F && FSY > 0.F) {
            if (*pluginConfig().mode == "exact")
                TARGET->setPseudoSize({(float)((double)WINW + RESERVED_W), (float)((double)FSY + RESERVED_H)});
            else
                TARGET->setPseudoSize({WINW, FSY});
        }
    }

    // Column width is stored as a *fraction* of the usable width, not as pixels:
    // the scrolling layout turns it into pixels with stripBox.w = usable * size
    // (see ScrollTapeController::calculateStripSize). Hyprland has no pixel-based
    // column width, so the fraction cannot be "set to WINW" directly.
    float frac = std::clamp((float)((double)WINW / (double)USABLE.w), 0.05F, 1.0F);

    // Enforce a pixel-exact window width. The layout derives the window's box
    // from the column and then consumes gaps_in and the border decoration, so a
    // bare fraction would leave the window narrower. Instead of guessing that
    // compensation up front (which drifts, e.g. with per-workspace gaps_in
    // rules), set the fraction, recalculate, read the window's *own* resulting
    // goal box back (geometricBox(GEOMETRIC_GOAL) -- the same quantity
    // lastFloatingSize measures, after all gaps/border are applied) and correct
    // the fraction by the measured error. A couple of passes converge to a
    // window width that round-trips to exactly WINW.
    //
    // Pseudo windows are exempt from the convergence loop: they are rendered at
    // their pseudo size (minus the border reserved area) regardless of the
    // column width, so there is nothing to converge -- the column only has to be
    // wide enough to contain the (inflated) pseudo size without scaling.
    const bool PSEUDO = TARGET->isPseudo();

    if (*pluginConfig().mode == "exact" && !PSEUDO) {
        for (int i = 0; i < 6; ++i) {
            COL->setColumnWidth(frac);
            SCROLLING->m_scrollingData->centerOrFitCol(COL);
            SCROLLING->m_scrollingData->recalculate();

            const double ACTUAL = w->geometricBox(Desktop::View::IGeometric::GEOMETRIC_GOAL).w;
            const double ERR    = (double)WINW - ACTUAL; // measured deviation in px

            // Stop once the window box is at least WINW but less than half a
            // pixel wider, so client-side rounding can never cut a pixel off.
            if (ERR <= 0. && -ERR < 0.5)
                break;

            const float NEXT = std::clamp((float)((double)frac + ERR / (double)USABLE.w), 0.05F, 1.0F);
            if (NEXT == frac) // pinned to a clamp bound already; cannot improve
                break;
            frac = NEXT;
        }
    } else {
        // "inherit" (and any unknown value), and pseudo windows: the column gets
        // the bare window-width fraction, no compensation.
        //
        // Pseudo + exact: the window renders at its (inflated) pseudo size, so
        // the column must be able to contain it or CWindowTarget::updatePos will
        // scale the window down to fit. Widen the column by the border reserved
        // extents as well, so the strip wraps the window's exact outer size.
        if (PSEUDO && *pluginConfig().mode == "exact")
            frac = std::clamp((float)(((double)WINW + RESERVED_W) / (double)USABLE.w), 0.05F, 1.0F);

        COL->setColumnWidth(frac);
        SCROLLING->m_scrollingData->centerOrFitCol(COL);
        SCROLLING->m_scrollingData->recalculate();
    }
}

} // namespace

// Do NOT change this function.
APICALL EXPORT std::string PLUGIN_API_VERSION() {
    return HYPRLAND_API_VERSION;
}

APICALL EXPORT PLUGIN_DESCRIPTION_INFO PLUGIN_INIT(HANDLE handle) {
    PHANDLE = handle;

    const std::string COMPOSITOR_HASH = __hyprland_api_get_hash();
    const std::string CLIENT_HASH     = __hyprland_api_get_client_hash();

    if (COMPOSITOR_HASH != CLIENT_HASH) {
        HyprlandAPI::addNotification(PHANDLE, "[hyprsnug] Failure in initialization: Version mismatch (headers ver is not equal to running hyprland ver)",
                                     CHyprColor{1.0, 0.2, 0.2, 1.0}, 5000);
        throw std::runtime_error("[hyprsnug] Version mismatch");
    }

    HyprlandAPI::addConfigValueV2(PHANDLE, makeShared<Config::Values::CIntValue>("plugin:hyprsnug:enabled", "Enable fitting scrolling column widths to window widths", 1));
    HyprlandAPI::addConfigValueV2(PHANDLE, makeShared<Config::Values::CStringValue>(
                                              "plugin:hyprsnug:mode", "Column fit mode: exact (window keeps exact width) or inherit (bare window width)",
                                              "exact", Config::Values::SStringValueOptions{.validator = Config::Values::strChoice({"exact", "inherit"})}));
    HyprlandAPI::addConfigValueV2(PHANDLE, makeShared<Config::Values::CIntValue>(
                                              "plugin:hyprsnug:sync_pseudosize",
                                              "Pin the pseudo size to the floating size on embed/drag so a tiled pseudo window never changes size",
                                              1));
    HyprlandAPI::addConfigValueV2(PHANDLE, makeShared<Config::Values::CIntValue>(
                                              "plugin:hyprsnug:fix_drag_ghost",
                                              "While dragging a pseudo window (Super+LMB) keep the ghost at the pseudo size instead of the full column size",
                                              1));

    // Force-construct the config value accessors now, while every value above
    // is definitely registered, so no event handler ever has to bind them.
    (void)pluginConfig();

    g_listenerWindowFloating = Event::bus()->m_events.window.floating.listen([](const PHLWINDOW& w) {
    // Dragging a *pseudo* window around (Super+LMB): the drag controller floats
    // the window and builds the drag "ghost" box from the *full column*
    // geometry, so the window visually jumps to the whole column height for the
    // duration of the drag. The DefaultFloatingAlgorithm deliberately skips
    // re-positioning the target that is currently being dragged, so forcing the
    // box to the window's pseudo size here is stable for the whole drag.
    if (w->isFloating()) {
        if (*pluginConfig().fixDragGhost) {
            const auto& DC = g_layoutManager->dragController();
            if (DC && !DC->draggingTiled() && DC->dragThresholdReached()) {
                const auto TARGET = DC->target();
                if (TARGET && TARGET->isPseudo() && TARGET->window() == w) {
                    // A pseudo window's rendered box is pseudoSize minus the
                    // border reserved area. Build the ghost from that box so the
                    // drag shows the window at its real size, and keep the
                    // remembered floating size free of the reserved offset
                    // (otherwise the post-drag fit would drift wider each time).
                    const Vector2D PS      = TARGET->pseudoSize();
                    const auto     RSVD    = w->getFullWindowReservedArea();
                    const Vector2D GHOSTBOX{PS.x - RSVD.topLeft.x - RSVD.bottomRight.x, PS.y - RSVD.topLeft.y - RSVD.bottomRight.y};
                    if (GHOSTBOX.x >= 16.F && GHOSTBOX.y >= 16.F) {
                        TARGET->rememberFloatingSize(GHOSTBOX);
                        TARGET->setPositionGlobal(CBox{TARGET->position().pos(), GHOSTBOX});
                    }
                }
            }
        }
        return;
    }

    // Embedding (floating -> tiled). This also fires at the end of a tiled DnD
    // drag, but there the width is still the drag "ghost" size, so the actual
    // re-fit is deferred to the drag-end listener.
    if (g_layoutManager->dragController()->draggingTiled()) {
        // DnD drop in progress: re-fit from the drag-end event instead.
        g_dragWindow = w;
        return;
    }

    g_dragWindow.reset();
    fitColumnToWindow(w);
    });

    // Moving windows (Super+LMB drag). Fires after the drag controller has
    // restored the window's original floating size.
    g_listenerDragEnd = g_layoutManager->dragController()->m_events.ended.listen([] {
        if (const auto W = g_dragWindow.lock()) {
            g_dragWindow.reset();
            fitColumnToWindow(W);
        }
    });

    // Moving a window between workspaces. Fires *before* the window is re-tiled.
    // Re-fit the fresh destination column to the window's pre-move size on the
    // next compositor tick, once the new column exists.
    g_listenerMoveToWorkspace = Event::bus()->m_events.window.moveToWorkspace.listen([](const PHLWINDOW& w, const PHLWORKSPACE& /*workspace*/) {
        if (!w || w->isFloating())
            return;

        const auto TARGET = w->layoutTarget();
        if (!TARGET)
            return;

        const float WIDTH = TARGET->position().size().x;
        if (WIDTH <= 0.F)
            return;

        g_pendingMoveFits.push_back({w, WIDTH});
    });

    g_listenerTick = Event::bus()->m_events.tick.listen([] {
        if (g_pendingMoveFits.empty())
            return;

        std::vector<SPendingMoveFit> pending;
        pending.swap(g_pendingMoveFits);

        for (const auto& FIT : pending) {
            if (FIT.window)
                fitColumnToWindow(FIT.window, FIT.widthPx);
        }
    });

    // A window explicitly opened directly into the strip (its float rule
    // overridden, e.g. float=false) is tiled on a fresh column created at the
    // *default* width, and the pseudo size Hyprland assigned at map is either
    // the size-rule value or a stale copy of the default column geometry -- so
    // the window is scaled to the default column until re-fitted. Fit the
    // column to the window's intended size right here (BR-14/15/16): window.open
    // fires before the entrance animation is set up, so the window never
    // visibly sits at the default column width first.
    //
    // Only pseudo windows are fitted: a fully-tiled window (pseudo disabled by
    // rule, e.g. an editor) intentionally stretches to its column. A size rule
    // fixes the pseudo size at map (see the tiled branch of CWindow::mapWindow)
    // and is that window's intended size; without a size rule the pseudo size
    // was copied from the default column geometry and is useless, so the
    // natural requested size Hyprland remembered at open (lastFloatingSize) is
    // used instead.
    g_listenerWindowOpen = Event::bus()->m_events.window.open.listen([](const PHLWINDOW& w) {
        if (!w || w->isFloating())
            return;

        const auto TARGET = w->layoutTarget();
        if (!TARGET || !TARGET->isPseudo())
            return;

        if (w->m_ruleApplicator->static_.size.has_value()) {
            const Vector2D PS = TARGET->pseudoSize();
            if (PS.x > 0.F && PS.y > 0.F)
                fitColumnToWindow(w, PS.x, PS.y);
        } else {
            fitColumnToWindow(w);
        }
    });

    return {"hyprsnug", "Inherits scrolling column widths from window widths on embed, drag, open into the strip and workspace move", "svtrn", HYPRSNUG_VERSION};
}

APICALL EXPORT void PLUGIN_EXIT() {
    g_listenerWindowFloating.reset();
    g_listenerDragEnd.reset();
    g_listenerMoveToWorkspace.reset();
    g_listenerWindowOpen.reset();
    g_listenerTick.reset();
}