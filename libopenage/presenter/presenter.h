// Copyright 2019-2024 the openage authors. See copying.md for legal info.

#pragma once

#include <atomic>
#include <chrono>
#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

#include "coord/pixel.h"
#include "gamestate/types.h"
#include "network/messages.h"
#include "presenter/guest_clock.h"
#include "presenter/hud_mouse_capture.h"

#include "renderer/window.h"
#include "time/time.h"
#include "util/path.h"


class QQmlPropertyMap;
class QPointF;

namespace qtgui {
class GuiApplication;
}

namespace openage {

namespace gamestate {
class GameSimulation;
}

namespace input {
class InputManager;

namespace game {
class Controller;
}
}

namespace time {
class TimeLoop;
}

namespace network {
class ReplicationTracker;
class RelayLobbyClient;
}

namespace renderer {
class RenderPass;
class Renderer;
class Texture2d;
class ShaderProgram;
class Window;

namespace camera {
class Camera;
class CameraManager;
} // namespace camera

namespace gui {
class GUI;
}

namespace hud {
class HudRenderStage;
}

namespace screen {
class ScreenRenderStage;
}

namespace skybox {
class SkyboxRenderStage;
}

namespace terrain {
class TerrainRenderStage;
}

namespace world {
class WorldRenderStage;
class RenderEntity;
}

namespace resources {
class AssetManager;
}

} // namespace renderer

namespace presenter {

class Presenter {
public:
	/**
	 * Create a new presenter.
	 *
	 * @param path Root directory path.
	 * @param simulation Game simulation. Can be set later with \p set_engine()
	 * @param time_loop Time loop which controls simulation time. Can be set later with \p set_time_loop()
	 */
	Presenter(const util::Path &path,
	          const std::shared_ptr<gamestate::GameSimulation> &simulation = nullptr,
	          const std::shared_ptr<time::TimeLoop> &time_loop = nullptr);

	~Presenter();

	/**
	 * Start the presenter and initialize subsystems.
	 *
	 * @param window_settings The settings to customize the display window (e.g. size, display mode, vsync).
	 * @param on_initialized Callback invoked after the renderer is attached and before the event loop starts.
	 */
	void run(const renderer::window_settings window_settings = {},
	         const std::function<void()> &on_initialized = {});

	/**
	 * Request that the presenter exits its draw loop.
	 *
	 * This can be called from another thread while the presenter is starting or
	 * running.
	 */
	void stop();

	/**
	 * Set the game simulation controlled by this presenter.
	 *
	 * @param simulation Game simulation.
	 */
	void set_simulation(const std::shared_ptr<gamestate::GameSimulation> &simulation);

	/**
	 * Set the time loop controlled by this presenter.
	 *
	 * @param time_loop Time loop.
	 */
	void set_time_loop(const std::shared_ptr<time::TimeLoop> &time_loop);

	/** Bind the recipient-specific network stream before starting the presenter. */
	void set_fvd_replication(const std::shared_ptr<network::ReplicationTracker> &replication);

	/**
	 * Configure the relay lobby and host world replication cadence before starting.
	 *
	 * @param enabled Whether to present the online lobby (solo CLI skips it).
	 * @param update_interval Positive interval between authoritative world captures.
	 */
	void set_fvd_lobby_enabled(bool enabled, std::chrono::milliseconds update_interval);

	/**
	 * Initialize the Qt application managing the graphical views. Required
	 * for creating windows.
	 *
	 * @returns Pointer to openage's application wrapper,
	 */
	static std::shared_ptr<qtgui::GuiApplication> init_window_system();

protected:
	/**
	 * Initialize all graphics subsystems of the presenter, i.e.
	 *     - window creation
	 *     - main renderer
	 *     - component renderers (Terrain, Game Entities, GUI)
	 */
	void init_graphics(const renderer::window_settings &window_settings = {});

	/**
	 * Initialize the GUI.
	 */
	void init_gui();

	/**
	 * Initialize the input management.
	 */
	void init_input();

	/**
	 * Initialize the final render pass that renders the results of all previous
	 * render passes to the window screen.
	 */
	void init_final_render_pass();

	// void init_audio();

	/**
	 * Render all configured render passes in sequence.
	 */
	void render();

	/** Qt offscreen input needs presenter-level click handling. */
	std::string fvd_lobby_action_at(const QPointF &position) const;
	void handle_fvd_lobby_action(const std::string &action);
	void refresh_fvd_lobby_state();
	void refresh_fvd_guest_visuals();

	// TODO: remove and move into our config/settings system
	util::Path root_dir;

	// graphis components
	/**
	 * Windowing GUI Application wrapper.
	 */
	std::shared_ptr<qtgui::GuiApplication> gui_app;

	/**
	 * Display window.
	 */
	std::shared_ptr<renderer::Window> window;

	/**
	 * openage's graphics renderer.
	 */
	std::shared_ptr<renderer::Renderer> renderer;

	/**
	 * Camera for viewing things.
	 */
	std::shared_ptr<renderer::camera::Camera> camera;

	/**
	 * Qt-based GUI for interface.
	 */
	std::shared_ptr<renderer::gui::GUI> gui;

	/** Qt bridge for FVD player HUD state and build-button actions. */
	std::shared_ptr<QQmlPropertyMap> fvd_hud_state;
	bool fvd_lobby_enabled{false};
	bool fvd_gameplay_active{false};
	bool fvd_reset_pending{false};
	gamestate::player_id_t fvd_assigned_player{0};
	std::shared_ptr<QQmlPropertyMap> fvd_lobby_state;
	std::unique_ptr<network::RelayLobbyClient> fvd_lobby_client;
	std::string fvd_lobby_pressed{};
	std::string fvd_lobby_local_error{};
	bool fvd_lobby_hosting{false};
	uint64_t fvd_lobby_suggested_room_id{0};
	bool fvd_bootstrap_requested{false};
	bool fvd_bootstrap_supplied{false};
	bool fvd_participant_slots_configured{false};
	gamestate::player_id_t fvd_capture_recipient{0};
	std::vector<gamestate::player_id_t> fvd_bootstrapped_recipients{};
	size_t fvd_capture_cursor{0};
	std::vector<gamestate::player_id_t> fvd_recorded_departures{};
	bool fvd_guest_visuals_initialized{false};
	uint64_t fvd_guest_visual_revision{0};
	uint64_t fvd_guest_snapshot_generation{0};
	GuestClock fvd_guest_clock{};
	/** Process only host-confirmed HP changes and entity deaths for visuals. */
	void record_confirmed_combat_effects(
		const network::WorldState &world,
		bool reset,
		const time::time_t &presentation_time);
	/** Deactivate and forget transient combat render entities. */
	void clear_fvd_combat_visuals();
	/** Resolve and cache the animation variants for an FVD ability. */
	const std::vector<std::string> &resolve_fvd_sprite_paths(const std::string &ability);
	/**
	 * Reduce a local snapshot to what the controlled player's side can see,
	 * as a remote guest would receive it, and hide everything else.
	 */
	network::WorldState fvd_local_view(const network::WorldState &world, size_t controlled);
	/** Draw the viewer's fog on the terrain, or remove it without one. */
	void update_fvd_fog(const network::WorldState *world);
	/** Fog levels last sent to the terrain renderer. */
	std::vector<uint8_t> fvd_fog_levels{};
	std::optional<network::WorldState> fvd_previous_combat_world{};
	struct CombatVisual {
		coord::phys3 position;
		bool death;
		std::chrono::steady_clock::time_point started;
		time::time_t presentation_started;
		std::shared_ptr<renderer::world::RenderEntity> render_entity{};
	};
	std::vector<CombatVisual> fvd_combat_visuals{};

	/** Last processed authoritative projectile ID (avoid duplicate visuals). */
	uint64_t fvd_last_guest_arrow_id{0};
	struct LateGuestArrow {
		network::ArrowState shot;
		std::chrono::steady_clock::time_point first_seen;
	};
	/** Cosmetic playback for shots delivered after their impact time. */
	std::vector<LateGuestArrow> fvd_guest_late_arrows{};
	std::unordered_map<gamestate::entity_id_t,
		std::shared_ptr<renderer::world::RenderEntity>> fvd_guest_entities{};
	/** Resolve animated sprite variants from nyan just like the host's Idle system. */
	std::unordered_map<std::string, std::vector<std::string>> fvd_guest_sprite_paths{};
	bool fvd_update_requested{false};
	uint64_t fvd_next_world_revision{2};
	std::chrono::steady_clock::time_point fvd_last_update_request{};
	std::chrono::milliseconds fvd_update_interval{};
	/** Guest HUD source; local-only matches continue to read their simulation. */
	std::shared_ptr<network::ReplicationTracker> fvd_replication;
	/** Most recent screen cursor coordinate, owned by the presenter thread. */
	std::optional<coord::input> placement_cursor;
	/** Prevent HUD gestures from leaking game orders through the panel. */
	HudMouseCapture fvd_hud_capture{};

	/**
	 * Camera manager for camera controls.
	 */
	std::shared_ptr<renderer::camera::CameraManager> camera_manager;

	/**
	 * Graphics output for the map background.
	 */
	std::shared_ptr<renderer::skybox::SkyboxRenderStage> skybox_renderer;

	/**
	 * Graphics output for terrain.
	 */
	std::shared_ptr<renderer::terrain::TerrainRenderStage> terrain_renderer;

	/**
	 * Graphics output for units/buildings.
	 */
	std::shared_ptr<renderer::world::WorldRenderStage> world_renderer;

	/**
	 * Graphics output for the HUD.
	 */
	std::shared_ptr<renderer::hud::HudRenderStage> hud_renderer;

	/**
	 * Final graphics output to the window screen.
	 */
	std::shared_ptr<renderer::screen::ScreenRenderStage> screen_renderer;

	/**
	 * Manager for loading/storing asset resources.
	 */
	std::shared_ptr<renderer::resources::AssetManager> asset_manager;

	/**
	 * Render passes in the openage renderer.
	 */
	std::vector<std::shared_ptr<renderer::RenderPass>> render_passes;

	/**
	 * Game simulation.
	 */
	std::shared_ptr<gamestate::GameSimulation> simulation;

	/**
	 * Time loop.
	 */
	std::shared_ptr<time::TimeLoop> time_loop;

	/**
	 * Input manager.
	 */
	std::shared_ptr<input::InputManager> input_manager;

	/**
	 * Controller whose current selection is displayed by the HUD.
	 */
	std::shared_ptr<input::game::Controller> game_controller;

	/**
	 * Set when another engine subsystem requests shutdown.
	 */
	std::atomic<bool> stop_requested{false};
};

} // namespace presenter
} // namespace openage
