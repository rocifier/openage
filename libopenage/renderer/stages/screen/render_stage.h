// Copyright 2022-2026 the openage authors. See copying.md for legal info.

#pragma once

#include <array>
#include <eigen3/Eigen/Dense>
#include <memory>
#include <vector>

#include "util/path.h"

namespace openage::renderer {
class Geometry;
class Renderer;
class RenderPass;
class RenderTarget;
class ShaderProgram;
class Texture2d;
class UniformInput;
class Window;

namespace screen {

/** Rounded panel bounds in normalized screen coordinates. */
struct GlassPanel {
	Eigen::Vector4f rect{0.0f, 0.0f, 0.0f, 0.0f};
	float radius{0.0f};
};

/**
 * Draws the results of previous render passes to the window screen
 * (i.e. the renderers display target). This should always be the
 * last render stage.
 */
class ScreenRenderStage {
public:
	/**
	 * Create a new render stage for drawing to the screen.
	 *
	 * @param window openage window targeted for rendering.
	 * @param renderer openage low-level renderer.
	 * @param shaderdir Directory containing the shader source files.
	 */
	ScreenRenderStage(const std::shared_ptr<Window> &window,
	                  const std::shared_ptr<renderer::Renderer> &renderer,
	                  const util::Path &shaderdir);
	~ScreenRenderStage() = default;

	/**
	 * Get the render pass of the screen renderer.
	 *
	 * @return Render pass for screen drawing.
	 */
	std::shared_ptr<renderer::RenderPass> get_render_pass();

	/** Get the offscreen downsample and upsample passes. */
	std::array<std::shared_ptr<renderer::RenderPass>, 3> get_blur_passes();

	/**
	 * Set the render targets whose outputs should be drawn into the
	 * display target.
	 *
	 * Targets are drawn in the order they appear in the vector.
	 * Alpha blending is enabled.
	 *
	 * TODO: Replace this with a \p set_textures() method that
	 * only receives the output textures.
	 *
	 * @param targets Render targets that should be drawn.
	 */
	void set_render_targets(const std::vector<std::shared_ptr<renderer::RenderTarget>> &targets);

	/** Set the four FVD panel bounds used for the scene backdrop blur. */
	void set_glass_panels(const std::array<GlassPanel, 4> &panels);

private:
	/**
	 * Create the render pass for the screen output.
	 *
	 * Called during initialization of the screen renderer.
	 *
	 * @param shaderdir Directory containing the shader source files.
	 */
	void initialize_render_pass(const util::Path &shaderdir);

	/**
	 * Recreates the renderables and the render pass of the screen renderer
	 * when \p set_render_targets() is called. This is necessary to take
	 * the textures of the new targets into account.
	 */
	void update_render_pass();

	/** Draw the blur passes only while a glass panel is visible. */
	void update_blur_renderables();

	/**
	 * Reference to the openage renderer.
	 */
	std::shared_ptr<renderer::Renderer> renderer;

	/**
	 * Render pass for the screen drawing.
	 */
	std::shared_ptr<renderer::RenderPass> render_pass;
	std::array<std::shared_ptr<renderer::RenderPass>, 3> blur_passes{};
	std::array<std::shared_ptr<renderer::Texture2d>, 3> blur_textures{};
	std::shared_ptr<renderer::Geometry> blur_quad;
	std::array<std::shared_ptr<renderer::UniformInput>, 3> blur_inputs{};
	bool blur_active{false};

	/**
	 * Output texture.
	 */
	std::shared_ptr<renderer::Texture2d> output_texture;

	/**
	 * FBO render targets that are drawn to the screen.
	 *
	 * TODO: Use pass_outputs instead
	 */
	std::vector<std::shared_ptr<renderer::RenderTarget>> render_targets;

	/**
	 * Texture targets of the individual FBOs.
	 */
	std::vector<std::shared_ptr<renderer::Texture2d>> pass_outputs;

	/**
	 * Shader for rendering to the window.
	 */
	std::shared_ptr<renderer::ShaderProgram> display_shader;

	/** Draws a blurred scene behind the Qt panel surfaces. */
	std::shared_ptr<renderer::ShaderProgram> glass_shader;
	std::shared_ptr<renderer::ShaderProgram> downsample_scene_shader;
	std::shared_ptr<renderer::ShaderProgram> downsample_shader;
	std::shared_ptr<renderer::ShaderProgram> upsample_shader;
	std::array<GlassPanel, 4> glass_panels{};
	std::array<std::shared_ptr<renderer::UniformInput>, 4> glass_inputs{};
};

} // namespace screen
} // namespace openage::renderer
