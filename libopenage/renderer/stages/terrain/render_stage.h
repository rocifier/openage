// Copyright 2022-2026 the openage authors. See copying.md for legal info.

#pragma once

#include <cstdint>
#include <memory>
#include <shared_mutex>
#include <vector>

#include "coord/scene.h"
#include "util/path.h"
#include "util/vector.h"


namespace openage {

namespace time {
class Clock;
}

namespace renderer {
class Renderer;
class RenderPass;
class ShaderProgram;
class UniformInput;
class Texture2d;
class Window;

namespace camera {
class Camera;
}

namespace resources {
class AssetManager;
}

namespace terrain {
class RenderEntity;
class TerrainRenderMesh;
class TerrainRenderModel;

/**
 * Manage and render terrain geometry and graphics.
 */
class TerrainRenderStage {
public:
	/**
	 * Create a new render stage for the terrain.
	 *
	 * @param window openage window targeted for rendering.
	 * @param renderer openage low-level renderer.
	 * @param camera Camera used for the rendered scene.
	 * @param shaderdir Directory containing the shader source files.
	 * @param asset_manager Asset manager for loading resources.
	 * @param clock Simulation clock for timing animations.
	 */
	TerrainRenderStage(const std::shared_ptr<Window> &window,
	                   const std::shared_ptr<renderer::Renderer> &renderer,
	                   const std::shared_ptr<renderer::camera::Camera> &camera,
	                   const util::Path &shaderdir,
	                   const std::shared_ptr<renderer::resources::AssetManager> &asset_manager,
	                   const std::shared_ptr<time::Clock> &clock);
	~TerrainRenderStage() = default;

	/**
	 * Get the render pass of the terrain renderer.
	 *
	 * @return Render pass for terrain drawing.
	 */
	std::shared_ptr<renderer::RenderPass> get_render_pass();

	/**
	 * Enable or disable the terrain tile grid.
	 *
	 * @param enabled Whether grid lines are rendered.
	 */
	void set_grid_enabled(bool enabled);

	/**
	 * Darken the terrain by fog of war, or turn fog off with empty levels.
	 * Call from the render thread.
	 *
	 * @param map_size Width and height of the square map in tiles.
	 * @param levels One byte per tile in row-major (ne, se) order:
	 *               0 unexplored, 128 explored, 255 visible.
	 */
	void set_fog(size_t map_size, const std::vector<uint8_t> &levels);

	/**
	 * Control the visual fog transition width in tile units, 0 to 1.
	 * This affects only the terrain shader; visibility and replication
	 * continue using unmodified per-tile fog bitsets.
	 */
	void set_fog_edge_width(float tile_width);

	/**
	 * Draw a square of dry, yellowed grass (e.g. a no-build zone), with its
	 * edge feathered into the surrounding terrain. Purely cosmetic.
	 *
	 * @param centre_ne Centre of the square in tile coordinates (ne).
	 * @param centre_se Centre of the square in tile coordinates (se).
	 * @param half_size Distance from the centre to the square's edge, in tiles.
	 * @param feather Width of the fade outside the edge, in tiles.
	 */
	void set_dry_zone(float centre_ne, float centre_se, float half_size, float feather);

	/** Stop drawing the dry square. */
	void clear_dry_zone();

	/**
	 * Add a new render entity to the terrain renderer.
	 *
	 * This creates a new terrain chunk and add it to the model.
	 *
	 * @param render_entity New render entity.
	 */
	void add_render_entity(const std::shared_ptr<RenderEntity> entity,
	                       const util::Vector2s chunk_size,
	                       const coord::scene2_delta chunk_offset);

	/** Discard terrain meshes from the previous match before rebuilding. */
	void clear_entities();

	/**
	 * Update the terrain mesh and texture information.
	 */
	void update();

	/**
	 * Resize the FBO for the terrain rendering. This basically updates the output
	 * texture size.
	 *
	 * @param width New width of the FBO.
	 * @param height New height of the FBO.
	 */
	void resize(size_t width, size_t height);

private:
	/**
	 * Create the render pass for terrains.
	 *
	 * Called during initialization of the terrain renderer.
	 *
	 * @param width Width of the FBO.
	 * @param height Height of the FBO.
	 * @param shaderdir Directory containing the shader source files.
	 */
	void initialize_render_pass(size_t width,
	                            size_t height,
	                            const util::Path &shaderdir);


	/**
	 * Reference to the openage renderer.
	 */
	std::shared_ptr<renderer::Renderer> renderer;

	/**
	 * Camera for view and projection matrices.
	 */
	std::shared_ptr<renderer::camera::Camera> camera;

	/**
	 * Engine interface for updating terrain draw information.
	 */
	std::shared_ptr<RenderEntity> render_entity;

	/**
	 * 3D model of the terrain.
	 */
	std::shared_ptr<TerrainRenderModel> model;

	/** Retained to create a fresh terrain model on match reset. */
	std::shared_ptr<renderer::resources::AssetManager> asset_manager;

	/**
	 * Render pass for the terrain drawing.
	 */
	std::shared_ptr<renderer::RenderPass> render_pass;

	/**
	 * Shader for rendering the world objects.
	 */
	std::shared_ptr<renderer::ShaderProgram> display_shader;

	/**
	 * Whether terrain tile boundaries are rendered.
	 */
	bool grid_enabled;

	/** Current fog of war; a 1x1 visible placeholder while disabled. */
	std::shared_ptr<renderer::Texture2d> fog_texture;
	bool fog_enabled = false;
	size_t fog_map_size = 1;
	float fog_edge_width = 0.8f;

	/** Dry grass square, see set_dry_zone(). */
	bool dry_zone_enabled = false;
	float dry_zone_centre_ne = 0.0f;
	float dry_zone_centre_se = 0.0f;
	float dry_zone_half_size = 0.0f;
	float dry_zone_feather = 1.0f;

	/** Apply the fog uniforms to one terrain mesh's uniform input. */
	void apply_fog(const std::shared_ptr<renderer::UniformInput> &uniforms) const;

	/**
	 * Simulation clock for timing animations.
	 */
	std::shared_ptr<time::Clock> clock;

	/**
	 * Output texture.
	 */
	std::shared_ptr<renderer::Texture2d> output_texture;

	/**
	 * Depth texture.
	 */
	std::shared_ptr<renderer::Texture2d> depth_texture;

	/**
	 * Mutex for protecting threaded access.
	 */
	std::shared_mutex mutex;
};

} // namespace terrain
} // namespace renderer
} // namespace openage
