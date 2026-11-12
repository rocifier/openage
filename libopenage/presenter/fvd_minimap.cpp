// Copyright 2026-2026 the openage authors. See copying.md for legal info.

#include "fvd_minimap.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <utility>

#include "network/entity_kind.h"

namespace openage::presenter {

namespace {

const QColor UNEXPLORED{0, 0, 0};
const QColor EXPLORED{38, 56, 30};
const QColor VISIBLE{72, 108, 52};

enum class Mark {
	NONE,
	STRUCTURE,
	FOUNDATION,
	UNIT,
};

Mark mark_for(const network::EntityState &entity) {
	const auto kind = entity.kind();
	if (network::is_tower(kind) or kind == network::EntityKind::HUNTER_TOWER) {
		return Mark::STRUCTURE;
	}
	if (kind == network::EntityKind::FOUNDATION) {
		return Mark::FOUNDATION;
	}
	if (network::is_unit(kind)) {
		return Mark::UNIT;
	}
	return Mark::NONE;
}

void fill_tile(QImage &image, int column, int row, const QColor &color) {
	const QRgb rgb = color.rgb();
	for (int dy = 0; dy < MINIMAP_TILE_PIXELS; ++dy) {
		auto *line = reinterpret_cast<QRgb *>(image.scanLine(row * MINIMAP_TILE_PIXELS + dy));
		for (int dx = 0; dx < MINIMAP_TILE_PIXELS; ++dx) {
			line[column * MINIMAP_TILE_PIXELS + dx] = rgb;
		}
	}
}

} // namespace

QColor minimap_player_color(gamestate::player_id_t player) {
	static const std::array<QColor, 9> colors{
		QColor{"#95abb7"}, QColor{"#d65d52"}, QColor{"#438be5"}, QColor{"#55b66f"},
		QColor{"#e8d16a"}, QColor{"#57c6d1"}, QColor{"#aa75d9"}, QColor{"#bbbbc7"},
		QColor{"#ec9b4c"}};
	return player < colors.size() ? colors[player] : QColor{"#e6e1cd"};
}

QImage render_minimap(const network::WorldState &world, const std::vector<uint8_t> &fog_levels) {
	const int size = static_cast<int>(world.map_size);
	QImage image{std::max(size, 1) * MINIMAP_TILE_PIXELS, std::max(size, 1) * MINIMAP_TILE_PIXELS,
	             QImage::Format_RGB32};
	image.fill(UNEXPLORED);
	if (size <= 0) {
		return image;
	}
	const bool fogged = fog_levels.size() == static_cast<size_t>(size) * size;
	for (int ne = 0; ne < size; ++ne) {
		for (int se = 0; se < size; ++se) {
			const uint8_t level = fogged ? fog_levels[static_cast<size_t>(ne) * size + se] : 255;
			const QColor &color = level >= 255 ? VISIBLE : level > 0 ? EXPLORED : UNEXPLORED;
			fill_tile(image, se, size - 1 - ne, color);
		}
	}

	// Structures first, units on top so a Builder beside a wall stays visible.
	for (const bool units : {false, true}) {
		for (const auto &entity : world.entities) {
			const Mark mark = mark_for(entity);
			if (mark == Mark::NONE or (mark == Mark::UNIT) != units) {
				continue;
			}
			const auto tile = entity.position.to_tile();
			if (tile.ne < 0 or tile.se < 0 or tile.ne >= size or tile.se >= size) {
				continue;
			}
			QColor color = minimap_player_color(entity.owner_id);
			if (mark == Mark::UNIT) {
				color = color.lighter(135);
			}
			else if (mark == Mark::FOUNDATION) {
				color = color.darker(160);
			}
			if (entity.remembered) {
				color = color.darker(170);
			}
			const int column = static_cast<int>(tile.se);
			const int row = size - 1 - static_cast<int>(tile.ne);
			fill_tile(image, column, row, color);
			// Units are a single tile wide: draw them as a 2x2-tile dot so
			// they stay visible once the minimap is scaled down.
			if (mark == Mark::UNIT) {
				for (const auto &[dc, dr] : {std::pair{1, 0}, std::pair{0, 1}, std::pair{1, 1}}) {
					if (column + dc < size and row + dr < size) {
						fill_tile(image, column + dc, row + dr, color);
					}
				}
			}
		}
	}
	return image;
}

coord::tile minimap_pixel_to_tile(double x, double y, uint32_t map_size) {
	const auto limit = static_cast<double>(map_size) - 1.0;
	const double se = std::clamp(std::floor(x / MINIMAP_TILE_PIXELS), 0.0, limit);
	const double row = std::clamp(std::floor(y / MINIMAP_TILE_PIXELS), 0.0, limit);
	return coord::tile{static_cast<coord::tile_t>(limit - row), static_cast<coord::tile_t>(se)};
}

std::pair<double, double> minimap_position_to_pixel(const coord::phys3 &position, uint32_t map_size) {
	const double ne = position.ne.to_double();
	const double se = position.se.to_double();
	return {se * MINIMAP_TILE_PIXELS, (static_cast<double>(map_size) - ne) * MINIMAP_TILE_PIXELS};
}

} // namespace openage::presenter
