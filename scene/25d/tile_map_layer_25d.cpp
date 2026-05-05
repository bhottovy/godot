#include "tile_map_layer_25d.h"

#include "core/config/engine.h"
#include "core/io/marshalls.h"
#include "core/math/geometry_2d.h"
#include "core/math/random_pcg.h"
#include "core/object/callable_mp.h"
#include "core/object/class_db.h"
//#include "scene/2d/tile_map.h"
#include "scene/gui/control.h"
#include "scene/main/scene_tree.h"
//#include "scene/resources/2d/navigation_mesh_source_geometry_data_2d.h"
#include "scene/resources/material.h"
#include "scene/resources/world_2d.h"
#include "servers/rendering/rendering_server.h"

#ifdef __GNUC__
#pragma GCC diagnostic ignored "-Wunused-variable"
#pragma GCC diagnostic ignored "-Wunused-parameter"
#pragma GCC diagnostic ignored "-Wunused-but-set-variable"
#endif

using namespace TCG;

Vector2i TileMapLayer25D::_coords_to_chunk_coords(const Vector2i &p_coords, int p_chunk_size) const {
	return Vector2i(
			p_coords.x > 0 ? p_coords.x / p_chunk_size : (p_coords.x - (p_chunk_size - 1)) / p_chunk_size,
			p_coords.y > 0 ? p_coords.y / p_chunk_size : (p_coords.y - (p_chunk_size - 1)) / p_chunk_size);
}

Color TileMapLayer25D::_highlight_color(const Color &p_modulate) const {
	if (highlight_mode == HIGHLIGHT_MODE_BELOW) {
		return p_modulate.darkened(0.5);
	}
	if (highlight_mode == HIGHLIGHT_MODE_ABOVE) {
		Color c = p_modulate.darkened(0.5);
		c.a *= 0.3;
		return c;
	}
	return p_modulate;
}

/////////////////////////////// Rendering //////////////////////////////////////
void TileMapLayer25D::_rendering_update(bool p_force_cleanup) {
	RenderingServer *rs = RenderingServer::get_singleton();

	// Check if we should cleanup everything.
	bool forced_cleanup = p_force_cleanup || !enabled || tile_set.is_null() || !is_visible_in_tree();
	if (forced_cleanup && _rendering_was_cleaned_up) {
		return;
	}

	// ----------- Layer level processing -----------
	// Modulate the layer.
	Color layer_modulate = get_self_modulate();
/*#ifdef TOOLS_ENABLED
	if (!forced_cleanup) {
		layer_modulate = _highlight_color(layer_modulate);
		rs->canvas_item_set_self_modulate(get_canvas_item(), layer_modulate);
	}
#endif // TOOLS_ENABLED*/

	// ----------- Quadrants processing -----------

	// List all rendering quadrants to update, creating new ones if needed.
	SelfList<RenderingSlice>::List dirty_slice_list;

	// Check if anything changed that might change the quadrant shape.
	// If so, recreate everything.
	bool quadrant_shape_changed = dirty.flags[DIRTY_FLAGS_LAYER_Y_SORT_ENABLED] || dirty.flags[DIRTY_FLAGS_TILE_SET] ||
			(is_y_sort_enabled() && (dirty.flags[DIRTY_FLAGS_LAYER_Y_SORT_ORIGIN] || dirty.flags[DIRTY_FLAGS_LAYER_X_DRAW_ORDER_REVERSED] || dirty.flags[DIRTY_FLAGS_LAYER_LOCAL_TRANSFORM])) ||
			(!is_y_sort_enabled() && dirty.flags[DIRTY_FLAGS_LAYER_RENDERING_QUADRANT_SIZE]);

	// Free all quadrants.
	if (!_rendering_was_cleaned_up && (forced_cleanup || quadrant_shape_changed)) {
		for (const KeyValue<int16_t, Ref<RenderingSlice>> &kv : chunkData.rendering_slices) {
			for (const RID &ci : kv.value->canvas_items) {
				if (ci.is_valid()) {
					rs->free_rid(ci);
				}
			}
			kv.value->cells.clear();
		}
		chunkData.rendering_slices.clear();
		_rendering_was_cleaned_up = true;
	}

	if (!forced_cleanup) {
		// List all quadrants to update, recreating them if needed.
		if (dirty.flags[DIRTY_FLAGS_TILE_SET] || dirty.flags[DIRTY_FLAGS_LAYER_IN_TREE] || _rendering_was_cleaned_up) {
			// Update all cells.
            for (KeyValue<int16_t, HashMap<Vector2i, CellData>> &kvz : tile_map_layer_levels) {
                for (KeyValue<Vector2i, CellData> &kv : kvz.value) {
                    CellData &cell_data = kv.value;
                    _rendering_slice_update_cell(cell_data, dirty_slice_list);
                }
            }
		} else {
			// Update dirty cells.
			for (SelfList<CellData> *cell_data_list_element = dirty.cell_list.first(); cell_data_list_element; cell_data_list_element = cell_data_list_element->next()) {
				CellData &cell_data = *cell_data_list_element->self();
				_rendering_slice_update_cell(cell_data, dirty_slice_list);
			}
		}

		// Update all dirty quadrants.
		bool needs_set_not_interpolated = SceneTree::is_fti_enabled() && !is_physics_interpolated();
		for (SelfList<RenderingSlice> *slice_list_element = dirty_slice_list.first(); slice_list_element;) {
			SelfList<RenderingSlice> *next_slice_list_element = slice_list_element->next(); // "Hack" to clear the list while iterating.

			const Ref<RenderingSlice> &rendering_slice = slice_list_element->self();

			// Check if the quadrant has a tile.
			bool has_a_tile = false;
			for (SelfList<CellData> *cell_data_list_element = rendering_slice->cells.first(); cell_data_list_element; cell_data_list_element = cell_data_list_element->next()) {
				CellData &cell_data = *cell_data_list_element->self();
				if (cell_data.cell.source_id != TileSet::INVALID_SOURCE) {
					has_a_tile = true;
					break;
				}
			}

			if (has_a_tile) {
				for (RID &ci : rendering_slice->canvas_items) {
					rs->free_rid(ci);
				}
				rendering_slice->canvas_items.clear();

				// Sort the quadrant cells.
				rendering_slice->cells.sort();

				// Those allow to group cell per material or z-index.
				Ref<Material> prev_material;
				int prev_z_index = 0;
				RID prev_ci;

				for (SelfList<CellData> *cell_data_slice_list_element = rendering_slice->cells.first(); cell_data_slice_list_element; cell_data_slice_list_element = cell_data_slice_list_element->next()) {
					CellData &cell_data = *cell_data_slice_list_element->self();

					TileSetAtlasSource *atlas_source = Object::cast_to<TileSetAtlasSource>(*tile_set->get_source(cell_data.cell.source_id));

					// Get the tile data.
					const TileData *tile_data;
					//if (cell_data.runtime_tile_data_cache) {
					//	tile_data = cell_data.runtime_tile_data_cache;
					//} else {
						tile_data = atlas_source->get_tile_data(cell_data.cell.get_atlas_coords(), cell_data.cell.alternative_tile);
					//}

					Ref<Material> mat = tile_data->get_material();
					int tile_z_index = tile_data->get_z_index();

					// Quandrant pos.

					// --- CanvasItems ---
					RID ci;

					// Check if the material or the z_index changed.
					if (prev_ci == RID() || prev_material != mat || prev_z_index != tile_z_index) {
						// If so, create a new CanvasItem.
						ci = rs->canvas_item_create();
						if (needs_set_not_interpolated) {
							rs->canvas_item_set_interpolated(ci, false);
						}
						if (mat.is_valid()) {
							rs->canvas_item_set_material(ci, mat->get_rid());
						}
						rs->canvas_item_set_parent(ci, get_canvas_item());
						rs->canvas_item_set_use_parent_material(ci, mat.is_null());

                        float z_screen_offset = rendering_slice->z * 24.0f * 0.75f;
						Transform2D xform(0, rendering_slice->canvas_items_position - Vector2(0, z_screen_offset));
						rs->canvas_item_set_transform(ci, xform);

						rs->canvas_item_set_light_mask(ci, get_light_mask());
						rs->canvas_item_set_z_as_relative_to_parent(ci, true);
						rs->canvas_item_set_z_index(ci, tile_z_index);
						rs->canvas_item_set_self_modulate(ci, layer_modulate);

						rs->canvas_item_set_default_texture_filter(ci, RSE::CanvasItemTextureFilter(get_texture_filter_in_tree()));
						rs->canvas_item_set_default_texture_repeat(ci, RSE::CanvasItemTextureRepeat(get_texture_repeat_in_tree()));

						rendering_slice->canvas_items.push_back(ci);

						prev_ci = ci;
						prev_material = mat;
						prev_z_index = tile_z_index;

					} else {
						// Keep the same canvas_item to draw on.
						ci = prev_ci;
					}

					const Vector2 local_tile_pos = tile_set->map_to_local(cell_data.coords);

					// Random animation offset.
					real_t random_animation_offset = 0.0;
					if (atlas_source->get_tile_animation_mode(cell_data.cell.get_atlas_coords()) != TileSetAtlasSource::TILE_ANIMATION_MODE_DEFAULT) {
						Array to_hash = { local_tile_pos, get_instance_id() }; // Use instance id as a random hash
						random_animation_offset = RandomPCG(to_hash.hash()).randf();
					}

					// Drawing the tile in the canvas item.
					draw_tile(ci, local_tile_pos - rendering_slice->canvas_items_position, cell_data.z, tile_set, cell_data.cell.source_id, cell_data.cell.get_atlas_coords(), cell_data.cell.alternative_tile, -1, tile_data, random_animation_offset);
				}

				// Reset physics interpolation for any recreated canvas items.
				if (is_physics_interpolated_and_enabled() && is_visible_in_tree()) {
					for (const RID &ci : rendering_slice->canvas_items) {
						rs->canvas_item_reset_physics_interpolation(ci);
					}
				}

			} else {
				// Free the quadrant.
				for (const RID &ci : rendering_slice->canvas_items) {
					if (ci.is_valid()) {
						rs->free_rid(ci);
					}
				}
				rendering_slice->cells.clear();
				chunkData.rendering_slices.erase(rendering_slice->z);
			}

			slice_list_element = next_slice_list_element;
		}

		dirty_slice_list.clear();

		// Reset the drawing indices.
		{
			int index = -(int64_t)0x80000000; // Always must be drawn below children.

			// Sort the quadrants coords per local coordinates.
			LocalVector<Pair<int16_t, Ref<RenderingSlice>>> sorted_slice_keys;
			sorted_slice_keys.reserve(chunkData.rendering_slices.size());
			for (const KeyValue<int16_t, Ref<RenderingSlice>> &kv : chunkData.rendering_slices) {
				sorted_slice_keys.push_back(Pair<int16_t, Ref<RenderingSlice>>(kv.key, kv.value));
			}
            struct SliceZComparator {
				_ALWAYS_INLINE_ bool operator()(const Pair<int16_t, Ref<RenderingSlice>> &a, 
												const Pair<int16_t, Ref<RenderingSlice>> &b) const {
					return a.first < b.first;
				}
			};
			sorted_slice_keys.sort_custom<SliceZComparator>();

			// Set the draw indices.
			for (const Pair<int16_t, Ref<RenderingSlice>> &E : sorted_slice_keys) {
				for (const RID &ci : E.second->canvas_items) {
					RS::get_singleton()->canvas_item_set_draw_index(ci, index++);
				}
			}
		}

		// Updates on rendering changes.
		if (dirty.flags[DIRTY_FLAGS_LAYER_LIGHT_MASK] ||
				dirty.flags[DIRTY_FLAGS_LAYER_TEXTURE_FILTER] ||
				dirty.flags[DIRTY_FLAGS_LAYER_TEXTURE_REPEAT] ||
				dirty.flags[DIRTY_FLAGS_LAYER_SELF_MODULATE] ||
				dirty.flags[DIRTY_FLAGS_LAYER_HIGHLIGHT_MODE]) {
			for (KeyValue<int16_t, Ref<RenderingSlice>> &kv : chunkData.rendering_slices) {
				Ref<RenderingSlice> &rendering_slice = kv.value;
				for (const RID &ci : rendering_slice->canvas_items) {
					rs->canvas_item_set_light_mask(ci, get_light_mask());
					rs->canvas_item_set_default_texture_filter(ci, RSE::CanvasItemTextureFilter(get_texture_filter_in_tree()));
					rs->canvas_item_set_default_texture_repeat(ci, RSE::CanvasItemTextureRepeat(get_texture_repeat_in_tree()));
					rs->canvas_item_set_self_modulate(ci, layer_modulate);
				}
			}
		}
	}

	// -----------
	// Mark the rendering state as up to date.
	_rendering_was_cleaned_up = forced_cleanup;

	// ----------- Occluders processing -----------
}

void TileMapLayer25D::_rendering_notification(int p_what) {
	RenderingServer *rs = RenderingServer::get_singleton();
	if (p_what == NOTIFICATION_TRANSFORM_CHANGED || p_what == NOTIFICATION_ENTER_CANVAS || p_what == NOTIFICATION_VISIBILITY_CHANGED) {
		if (tile_set.is_valid()) {
			Transform2D tilemap_xform = get_global_transform();
            for (KeyValue<int16_t, HashMap<Vector2i, CellData>> &kvz : tile_map_layer_levels) {
                for (const KeyValue<Vector2i, CellData> &kv : kvz.value) {
                    //const CellData &cell_data = kv.value;
                    /*for (const LocalVector<RID> &polygons : cell_data.occluders) {
                        for (const RID &rid : polygons) {
                            if (rid.is_null()) {
                                continue;
                            }
                            Transform2D xform(0, tile_set->map_to_local(kv.key));
                            //rs->canvas_light_occluder_attach_to_canvas(rid, get_canvas());
                            //rs->canvas_light_occluder_set_transform(rid, tilemap_xform * xform);
                        }
                    }*/
                }
            }
		}
	} /*else if (p_what == NOTIFICATION_RESET_PHYSICS_INTERPOLATION) {
		if (is_physics_interpolated_and_enabled() && is_visible_in_tree()) {
			for (const KeyValue<Vector2i, Ref<RenderingQuadrant>> &kv : rendering_quadrant_map) {
				for (const RID &ci : kv.value->canvas_items) {
					if (ci.is_valid()) {
						rs->canvas_item_reset_physics_interpolation(ci);
					}
				}
			}
		}
	}*/
}

void TileMapLayer25D::_rendering_slice_update_cell(CellData &r_cell_data, SelfList<RenderingSlice>::List &r_dirty_slice_list) {
	bool is_valid = false;
	int tile_y_sort_origin = 0;

	TileSetSource *source;
	if (tile_set->has_source(r_cell_data.cell.source_id)) {
		source = *tile_set->get_source(r_cell_data.cell.source_id);
		TileSetAtlasSource *atlas_source = Object::cast_to<TileSetAtlasSource>(source);
		if (atlas_source && atlas_source->has_tile(r_cell_data.cell.get_atlas_coords()) && atlas_source->has_alternative_tile(r_cell_data.cell.get_atlas_coords(), r_cell_data.cell.alternative_tile)) {
			is_valid = true;
			const TileData *tile_data;
			tile_data = atlas_source->get_tile_data(r_cell_data.cell.get_atlas_coords(), r_cell_data.cell.alternative_tile);
			tile_y_sort_origin = tile_data->get_y_sort_origin();
		}
	}

	if (is_valid) {
        int16_t z = (int16_t)r_cell_data.z;

		Vector2 canvas_items_position;
		if (is_y_sort_enabled()) {
            float z_offset = z * 24.0f * 0.75f;
			canvas_items_position = Vector2(0, tile_set->map_to_local(r_cell_data.coords).y + tile_y_sort_origin + y_sort_origin - z_offset);
		} else {
			canvas_items_position = Vector2(0, 0);
		}

		Ref<RenderingSlice> rendering_slice;
		if (chunkData.rendering_slices.has(z)) {
			// Reuse existing rendering quadrant.
			rendering_slice = chunkData.rendering_slices[z];
		} else {
			// Create a new rendering quadrant.
			rendering_slice.instantiate();
			rendering_slice->z = z;
			rendering_slice->canvas_items_position = canvas_items_position;
			chunkData.rendering_slices[z] = rendering_slice;
		}

        // Mark old slice dirty
        if(r_cell_data.rendering_slice.is_valid()) {
            if(!r_cell_data.rendering_slice->dirty_slice_list_element.in_list()) {
                r_dirty_slice_list.add(&r_cell_data.rendering_slice->dirty_slice_list_element);
            }
        }

        // Move cell to new slice
		if(r_cell_data.slice_list_element.in_list()) {
            r_cell_data.slice_list_element.remove_from_list();
        }
        r_cell_data.rendering_slice = rendering_slice;
        r_cell_data.rendering_slice->cells.add(&r_cell_data.slice_list_element);

        if (!rendering_slice->dirty_slice_list_element.in_list()) {
            r_dirty_slice_list.add(&rendering_slice->dirty_slice_list_element);
        }
	} else {
		Ref<RenderingSlice> rendering_slice = r_cell_data.rendering_slice;
        r_cell_data.rendering_slice = Ref<RenderingSlice>();
        if (r_cell_data.slice_list_element.in_list() && rendering_slice.is_valid()) {
            rendering_slice->cells.remove(&r_cell_data.slice_list_element);
        }
        if (rendering_slice.is_valid() && !rendering_slice->dirty_slice_list_element.in_list()) {
            r_dirty_slice_list.add(&rendering_slice->dirty_slice_list_element);
        }
	}
}

void TileMapLayer25D::_update_cells_callback(bool p_force_cleanup) {
	if (!GDVIRTUAL_IS_OVERRIDDEN(_update_cells)) {
		return;
	}

	// Check if we should cleanup everything.
	bool forced_cleanup = p_force_cleanup || !enabled || tile_set.is_null() || !is_visible_in_tree();

	// List all the dirty cell's positions to notify script of cell updates.
	TypedArray<Vector2i> dirty_cell_positions;
	for (SelfList<CellData> *cell_data_list_element = dirty.cell_list.first(); cell_data_list_element; cell_data_list_element = cell_data_list_element->next()) {
		CellData &cell_data = *cell_data_list_element->self();
		dirty_cell_positions.push_back(cell_data.coords);
	}

	GDVIRTUAL_CALL(_update_cells, dirty_cell_positions, forced_cleanup);
}

void TileMapLayer25D::_tile_set_changed() {
	dirty.flags[DIRTY_FLAGS_TILE_SET] = true;
	_queue_internal_update();
	emit_signal(CoreStringName(changed));
}

void TileMapLayer25D::_renamed() {
	emit_signal(CoreStringName(changed));
}

void TileMapLayer25D::_update_notify_local_transform() {
	bool notify = is_using_kinematic_bodies() || is_y_sort_enabled();
	if (!notify) {
		if (is_y_sort_enabled()) {
			notify = true;
		}
	}
	set_notify_local_transform(notify);
}

void TileMapLayer25D::_queue_internal_update() {
	if (pending_update) {
		return;
	}
	// Don't update when outside the tree, it doesn't do anything useful, and causes threading problems.
	if (is_inside_tree()) {
		pending_update = true;
		callable_mp(this, &TileMapLayer25D::_deferred_internal_update).call_deferred();
	}
}

void TileMapLayer25D::_deferred_internal_update() {
	// Other updates.
	if (!pending_update) {
		return;
	}

	// Update dirty quadrants on layers.
	_internal_update(false);
}

void TileMapLayer25D::_internal_update(bool p_force_cleanup) {
	// Find TileData that need a runtime modification.
	// This may add cells to the dirty list if a runtime modification has been notified.
	//_build_runtime_update_tile_data(p_force_cleanup);

	// Callback for implementing custom subsystems.
	// This may add to the dirty list if some cells are changed inside _update_cells.
	_update_cells_callback(p_force_cleanup);

	// Update all subsystems.
	_rendering_update(p_force_cleanup);

	//_clear_runtime_update_tile_data();

	// Clear the "what is dirty" flags.
	for (int i = 0; i < DIRTY_FLAGS_MAX; i++) {
		dirty.flags[i] = false;
	}

	// List the cells to delete definitely.
	Vector<CellKey> to_delete;
	for (SelfList<CellData> *cell_data_list_element = dirty.cell_list.first(); cell_data_list_element; cell_data_list_element = cell_data_list_element->next()) {
		CellData &cell_data = *cell_data_list_element->self();
		// Select the cell from tile_map if it is invalid.
		if (cell_data.cell.source_id == TileSet::INVALID_SOURCE) {
			to_delete.push_back(CellKey(cell_data.coords, cell_data.z));
		}
	}

	// Remove cells that are empty after the cleanup.
	for (const CellKey &cell : to_delete) {
        tile_map_layer_levels.get(cell.z).erase(cell.coords());
	}

	// Clear the dirty cells list.
	dirty.cell_list.clear();

	pending_update = false;
}

void TileMapLayer25D::_physics_interpolated_changed() {
	RenderingServer *rs = RenderingServer::get_singleton();

	bool interpolated = is_physics_interpolated();
	bool needs_reset = interpolated && is_visible_in_tree();

	for (const KeyValue<int16_t, Ref<RenderingSlice>> &kv : chunkData.rendering_slices) {
		for (const RID &ci : kv.value->canvas_items) {
			if (ci.is_valid()) {
				rs->canvas_item_set_interpolated(ci, interpolated);
				if (needs_reset) {
					rs->canvas_item_reset_physics_interpolation(ci);
				}
			}
		}
	}
}

void TileMapLayer25D::_notification(int p_what) {
	switch (p_what) {
		case NOTIFICATION_POSTINITIALIZE: {
			connect(SNAME("renamed"), callable_mp(this, &TileMapLayer25D::_renamed));
			break;
		}
		case NOTIFICATION_ENTER_TREE: {
			_update_notify_local_transform();
			dirty.flags[DIRTY_FLAGS_LAYER_IN_TREE] = true;
			_queue_internal_update();
		} break;

		case NOTIFICATION_EXIT_TREE: {
			dirty.flags[DIRTY_FLAGS_LAYER_IN_TREE] = true;
			// Update immediately on exiting, and force cleanup.
			_internal_update(true);
		} break;

		case NOTIFICATION_ENTER_CANVAS: {
			dirty.flags[DIRTY_FLAGS_LAYER_IN_CANVAS] = true;
			_queue_internal_update();
		} break;

		case NOTIFICATION_EXIT_CANVAS: {
			dirty.flags[DIRTY_FLAGS_LAYER_IN_CANVAS] = true;
			// Update immediately on exiting, and force cleanup.
			_internal_update(true);
		} break;

		case NOTIFICATION_VISIBILITY_CHANGED: {
			dirty.flags[DIRTY_FLAGS_LAYER_VISIBILITY] = true;
			_queue_internal_update();
		} break;
	}

	_rendering_notification(p_what);
}

void TileMapLayer25D::_bind_methods() {
	// Generic cells manipulations and access.
	ClassDB::bind_method(D_METHOD("set_cell", "coords", "z", "source_id", "atlas_coords", "alternative_tile"), 
        static_cast<void (TileMapLayer25D::*)(const Vector2i &, int16_t, int, const Vector2i &, int)>(&TileMapLayer25D::set_cell), 
        DEFVAL(0), DEFVAL(TileSet::INVALID_SOURCE), DEFVAL(TileSetSource::INVALID_ATLAS_COORDS), DEFVAL(0));
    
    ClassDB::bind_method(D_METHOD("erase_cell", "coords", "z"),
        static_cast<void (TileMapLayer25D::*)(const Vector2i &, int16_t)>(&TileMapLayer25D::erase_cell),
        DEFVAL(0));
	ClassDB::bind_method(D_METHOD("fix_invalid_tiles"), &TileMapLayer25D::fix_invalid_tiles);
	ClassDB::bind_method(D_METHOD("clear"), &TileMapLayer25D::clear);

	ClassDB::bind_method(D_METHOD("get_cell_source_id", "coords", "z"), &TileMapLayer25D::get_cell_source_id);
	ClassDB::bind_method(D_METHOD("get_cell_atlas_coords", "coords", "z"), &TileMapLayer25D::get_cell_atlas_coords);
	ClassDB::bind_method(D_METHOD("get_cell_alternative_tile", "coords", "z"), &TileMapLayer25D::get_cell_alternative_tile);
	ClassDB::bind_method(D_METHOD("get_cell_tile_data", "coords", "z"), &TileMapLayer25D::get_cell_tile_data);
    ClassDB::bind_method(D_METHOD("get_cell_tile_set_key", "coords", "z"), &TileMapLayer25D::get_cell_tile_data);

	ClassDB::bind_method(D_METHOD("is_cell_flipped_h", "coords", "z"), &TileMapLayer25D::is_cell_flipped_h);
	ClassDB::bind_method(D_METHOD("is_cell_flipped_v", "coords", "z"), &TileMapLayer25D::is_cell_flipped_v);
	ClassDB::bind_method(D_METHOD("is_cell_transposed", "coords", "z"), &TileMapLayer25D::is_cell_transposed);

	ClassDB::bind_method(D_METHOD("get_used_cells"), &TileMapLayer25D::get_used_cells);
	ClassDB::bind_method(D_METHOD("get_used_cells_by_id", "source_id", "atlas_coords", "alternative_tile"), &TileMapLayer25D::get_used_cells_by_id, DEFVAL(TileSet::INVALID_SOURCE), DEFVAL(TileSetSource::INVALID_ATLAS_COORDS), DEFVAL(TileSetSource::INVALID_TILE_ALTERNATIVE));
    ClassDB::bind_method(D_METHOD("get_used_rect"), &TileMapLayer25D::get_used_rect);

	// Patterns.
	ClassDB::bind_method(D_METHOD("get_pattern", "coords_array"), &TileMapLayer25D::get_pattern);
	ClassDB::bind_method(D_METHOD("set_pattern", "position", "pattern"), &TileMapLayer25D::set_pattern);

	// --- Runtime ---
	ClassDB::bind_method(D_METHOD("update_internals"), &TileMapLayer25D::update_internals);

	// --- Shortcuts to methods defined in TileSet ---
	ClassDB::bind_method(D_METHOD("map_pattern", "position_in_tilemap", "coords_in_pattern", "pattern"), &TileMapLayer25D::map_pattern);
	ClassDB::bind_method(D_METHOD("get_surrounding_cells_level", "coords"), &TileMapLayer25D::get_surrounding_cells_level);
	ClassDB::bind_method(D_METHOD("get_surrounding_cells", "coords", "z"), &TileMapLayer25D::get_surrounding_cells);
	ClassDB::bind_method(D_METHOD("get_neighbor_cell", "coords", "neighbor"), &TileMapLayer25D::get_neighbor_cell);
	ClassDB::bind_method(D_METHOD("map_level_to_local", "map_position"), &TileMapLayer25D::map_level_to_local);
	ClassDB::bind_method(D_METHOD("local_to_map_level", "local_position"), &TileMapLayer25D::local_to_map_level);

    ClassDB::bind_method(D_METHOD("map_to_local", "coords", "z"), &TileMapLayer25D::map_to_local);
    ClassDB::bind_method(D_METHOD("local_to_map", "local_position"), &TileMapLayer25D::local_to_map);

	// --- Accessors ---
	ClassDB::bind_method(D_METHOD("set_tile_map_data_from_array", "tile_map_layer_data"), &TileMapLayer25D::set_tile_map_data_from_array);
	ClassDB::bind_method(D_METHOD("get_tile_map_data_as_array"), &TileMapLayer25D::get_tile_map_data_as_array);

	ClassDB::bind_method(D_METHOD("set_enabled", "enabled"), &TileMapLayer25D::set_enabled);
	ClassDB::bind_method(D_METHOD("is_enabled"), &TileMapLayer25D::is_enabled);

	ClassDB::bind_method(D_METHOD("set_tile_set", "tile_set"), &TileMapLayer25D::set_tile_set);
	ClassDB::bind_method(D_METHOD("get_tile_set"), &TileMapLayer25D::get_tile_set);

	ClassDB::bind_method(D_METHOD("set_y_sort_origin", "y_sort_origin"), &TileMapLayer25D::set_y_sort_origin);
	ClassDB::bind_method(D_METHOD("get_y_sort_origin"), &TileMapLayer25D::get_y_sort_origin);

	ClassDB::bind_method(D_METHOD("set_collision_enabled", "enabled"), &TileMapLayer25D::set_collision_enabled);
	ClassDB::bind_method(D_METHOD("is_collision_enabled"), &TileMapLayer25D::is_collision_enabled);
	ClassDB::bind_method(D_METHOD("set_use_kinematic_bodies", "use_kinematic_bodies"), &TileMapLayer25D::set_use_kinematic_bodies);
	ClassDB::bind_method(D_METHOD("is_using_kinematic_bodies"), &TileMapLayer25D::is_using_kinematic_bodies);
	ClassDB::bind_method(D_METHOD("set_collision_visibility_mode", "visibility_mode"), &TileMapLayer25D::set_collision_visibility_mode);
	ClassDB::bind_method(D_METHOD("get_collision_visibility_mode"), &TileMapLayer25D::get_collision_visibility_mode);

	ClassDB::bind_method(D_METHOD("set_occlusion_enabled", "enabled"), &TileMapLayer25D::set_occlusion_enabled);
	ClassDB::bind_method(D_METHOD("is_occlusion_enabled"), &TileMapLayer25D::is_occlusion_enabled);

	GDVIRTUAL_BIND(_update_cells, "coords", "forced_cleanup");

	ADD_PROPERTY(PropertyInfo(Variant::PACKED_BYTE_ARRAY, "tile_map_data", PROPERTY_HINT_NONE, "", PROPERTY_USAGE_NO_EDITOR), "set_tile_map_data_from_array", "get_tile_map_data_as_array");

	ADD_PROPERTY(PropertyInfo(Variant::BOOL, "enabled"), "set_enabled", "is_enabled");
	ADD_PROPERTY(PropertyInfo(Variant::OBJECT, "tile_set", PROPERTY_HINT_RESOURCE_TYPE, TileSet::get_class_static()), "set_tile_set", "get_tile_set");
	ADD_GROUP("Rendering", "");
	ADD_PROPERTY(PropertyInfo(Variant::INT, "y_sort_origin"), "set_y_sort_origin", "get_y_sort_origin");

	ADD_SIGNAL(MethodInfo(CoreStringName(changed)));

	BIND_ENUM_CONSTANT(DEBUG_VISIBILITY_MODE_DEFAULT);
	BIND_ENUM_CONSTANT(DEBUG_VISIBILITY_MODE_FORCE_HIDE);
	BIND_ENUM_CONSTANT(DEBUG_VISIBILITY_MODE_FORCE_SHOW);
}

void TileMapLayer25D::_validate_property(PropertyInfo &p_property) const {
	if (!Engine::get_singleton()->is_editor_hint()) {
		return;
	}
	if (is_y_sort_enabled()) {
		if (p_property.name == "rendering_quadrant_size") {
			p_property.usage |= PROPERTY_USAGE_READ_ONLY;
		}
	} else {
		if (p_property.name == "x_draw_order_reversed") {
			p_property.usage |= PROPERTY_USAGE_READ_ONLY;
		}
	}
}

void TileMapLayer25D::_update_self_texture_filter(RSE::CanvasItemTextureFilter p_texture_filter) {
	// Set a default texture filter for the whole tilemap.
	CanvasItem::_update_self_texture_filter(p_texture_filter);
	dirty.flags[DIRTY_FLAGS_LAYER_TEXTURE_FILTER] = true;
	_queue_internal_update();
	emit_signal(CoreStringName(changed));
}

void TileMapLayer25D::_update_self_texture_repeat(RSE::CanvasItemTextureRepeat p_texture_repeat) {
	// Set a default texture repeat for the whole tilemap.
	CanvasItem::_update_self_texture_repeat(p_texture_repeat);
	dirty.flags[DIRTY_FLAGS_LAYER_TEXTURE_REPEAT] = true;
	_queue_internal_update();
	emit_signal(CoreStringName(changed));
}

/*#ifdef TOOLS_ENABLED
bool TileMapLayer25D::_edit_is_selected_on_click(const Point2 &p_point, double p_tolerance) const {
	return tile_set.is_valid() && get_cell_source_id(local_to_map(p_point)) != TileSet::INVALID_SOURCE;
}
#endif*/

Rect2 TileMapLayer25D::get_rect(bool &r_changed) const {
	if (tile_set.is_null()) {
		r_changed = rect_cache != Rect2();
		return Rect2();
	}

	// Compute the displayed area of the tilemap.
	r_changed = false;

	return rect_cache;
}

TileMapCell TileMapLayer25D::get_cell(const Vector2i &p_coords, int16_t p_z) const {
    if(!tile_map_layer_levels.has(p_z)) {
        return TileMapCell();
    } else {
        auto it = tile_map_layer_levels.find(p_z);
        return it->value.has(p_coords) ? it->value.find(p_coords)->value.cell : TileMapCell();
    }
}

TileMapCell TileMapLayer25D::get_cell(const CellKey &p_cell_key) const {
	return get_cell(p_cell_key.coords(), p_cell_key.z);
}

void TileMapLayer25D::draw_tile(RID p_canvas_item, const Vector2 &p_position, float p_z, const Ref<TileSet> p_tile_set, int p_atlas_source_id, const Vector2i &p_atlas_coords, int p_alternative_tile, int p_frame, const TileData *p_tile_data_override, real_t p_normalized_animation_offset) {
	ERR_FAIL_COND(p_tile_set.is_null());
	ERR_FAIL_COND(!p_tile_set->has_source(p_atlas_source_id));
	ERR_FAIL_COND(!p_tile_set->get_source(p_atlas_source_id)->has_tile(p_atlas_coords));
	ERR_FAIL_COND(!p_tile_set->get_source(p_atlas_source_id)->has_alternative_tile(p_atlas_coords, p_alternative_tile));
	TileSetSource *source = *p_tile_set->get_source(p_atlas_source_id);
	TileSetAtlasSource *atlas_source = Object::cast_to<TileSetAtlasSource>(source);
	if (atlas_source) {
		// Check for the frame.
		if (p_frame >= 0) {
			ERR_FAIL_INDEX(p_frame, atlas_source->get_tile_animation_frames_count(p_atlas_coords));
		}

		// Get the texture.
		Ref<Texture2D> tex = atlas_source->get_runtime_texture();
		if (tex.is_null()) {
			return;
		}

		// Check if we are in the texture, return otherwise.
		Vector2i grid_size = atlas_source->get_atlas_grid_size();
		if (p_atlas_coords.x >= grid_size.x || p_atlas_coords.y >= grid_size.y) {
			return;
		}

		// Get tile data.
		const TileData *tile_data = p_tile_data_override ? p_tile_data_override : atlas_source->get_tile_data(p_atlas_coords, p_alternative_tile);

		// Get the tile modulation.
		Color modulate = tile_data->get_modulate();

		// Compute the dest rect.
		Rect2 dest_rect;
		bool transpose;
		compute_transformed_tile_dest_rect(dest_rect, transpose, p_position, atlas_source->get_runtime_tile_texture_region(p_atlas_coords).size, tile_data, p_alternative_tile);

		// Draw the tile.
		if (p_frame >= 0) {
			Rect2i source_rect = atlas_source->get_runtime_tile_texture_region(p_atlas_coords, p_frame);
			tex->draw_rect_region(p_canvas_item, dest_rect, source_rect, modulate, transpose, p_tile_set->is_uv_clipping());
		} else if (atlas_source->get_tile_animation_frames_count(p_atlas_coords) == 1) {
			Rect2i source_rect = atlas_source->get_runtime_tile_texture_region(p_atlas_coords, 0);
			tex->draw_rect_region(p_canvas_item, dest_rect, source_rect, modulate, transpose, p_tile_set->is_uv_clipping());
		} else {
			real_t speed = atlas_source->get_tile_animation_speed(p_atlas_coords);
			real_t animation_duration = atlas_source->get_tile_animation_total_duration(p_atlas_coords) / speed;
			real_t animation_offset = p_normalized_animation_offset * animation_duration;
			// Accumulate durations unaffected by the speed to avoid accumulating floating point division errors.
			// Aka do `sum(duration[i]) / speed` instead of `sum(duration[i] / speed)`.
			real_t time_unscaled = 0.0;
			for (int frame = 0; frame < atlas_source->get_tile_animation_frames_count(p_atlas_coords); frame++) {
				real_t frame_duration_unscaled = atlas_source->get_tile_animation_frame_duration(p_atlas_coords, frame);
				real_t slice_start = time_unscaled / speed;
				real_t slice_end = (time_unscaled + frame_duration_unscaled) / speed;
				RenderingServer::get_singleton()->canvas_item_add_animation_slice(p_canvas_item, animation_duration, slice_start, slice_end, animation_offset);

				Rect2i source_rect = atlas_source->get_runtime_tile_texture_region(p_atlas_coords, frame);
				tex->draw_rect_region(p_canvas_item, dest_rect, source_rect, modulate, transpose, p_tile_set->is_uv_clipping());

				time_unscaled += frame_duration_unscaled;
			}
			RenderingServer::get_singleton()->canvas_item_add_animation_slice(p_canvas_item, 1.0, 0.0, 1.0, 0.0);
		}
	}
}

void TileMapLayer25D::compute_transformed_tile_dest_rect(Rect2 &r_dest_rect, bool &r_transpose, const Vector2 &p_position, const Vector2 &p_dest_rect_size, const TileData *p_tile_data, int p_alternative_tile) {
	DEV_ASSERT(p_tile_data);
	// Conceptually the order of transformations is (starting from the tile centered at the origin):
	// - Per TileSet-tile transforms (transpose then flips).
	// - Translation so texture origin is at the origin.
	// - Per TileMapLayer-cell transforms (transpose then flips).
	// - Translation to target position.

	const bool tile_transpose = p_tile_data->get_transpose();
	const bool tile_flip_h = p_tile_data->get_flip_h();
	const bool tile_flip_v = p_tile_data->get_flip_v();

	const Vector2 texture_origin = p_tile_data->get_texture_origin();

	const bool cell_transpose = bool(p_alternative_tile & TileSetAtlasSource::TRANSFORM_TRANSPOSE);
	const bool cell_flip_h = bool(p_alternative_tile & TileSetAtlasSource::TRANSFORM_FLIP_H);
	const bool cell_flip_v = bool(p_alternative_tile & TileSetAtlasSource::TRANSFORM_FLIP_V);

	const bool final_transpose = tile_transpose != cell_transpose;
	const bool final_flip_h = cell_flip_h != (cell_transpose ? tile_flip_v : tile_flip_h);
	const bool final_flip_v = cell_flip_v != (cell_transpose ? tile_flip_h : tile_flip_v);

	// Rect draw commands swap the size based on the passed transpose, so the size is left non-tranposed here.
	// Position calculations need to use transposed size though.
	Rect2 dest_rect;
	dest_rect.size = p_dest_rect_size;
	dest_rect.size.x += FP_ADJUST;
	dest_rect.size.y += FP_ADJUST;
	Vector2 transposed_size = final_transpose ? Vector2(dest_rect.size.y, dest_rect.size.x) : dest_rect.size;
	if (final_flip_h) {
		dest_rect.size.x = -dest_rect.size.x;
	}
	if (final_flip_v) {
		dest_rect.size.y = -dest_rect.size.y;
	}

	dest_rect.position = -0.5f * transposed_size;
	dest_rect.position -= cell_transpose ? Vector2(texture_origin.y, texture_origin.x) : texture_origin;
	if (cell_flip_h) {
		dest_rect.position.x = -(dest_rect.position.x + transposed_size.x);
	}
	if (cell_flip_v) {
		dest_rect.position.y = -(dest_rect.position.y + transposed_size.y);
	}
	dest_rect.position += p_position;

	r_dest_rect = dest_rect;
	r_transpose = final_transpose;
}

void TileMapLayer25D::set_cell(const Vector2i &p_coords, int16_t p_z, int p_source_id, const Vector2i &p_atlas_coords, int p_alternative_tile) {
	HashMap<int16_t, HashMap<Vector2i, CellData>>::Iterator L = tile_map_layer_levels.find(p_z);

    if(!L) {
        return; //TODO - for now do nothing, later force-create new levels as needed
    }

    // Set the current cell tile (using integer position).
	Vector2i pk(p_coords);
	HashMap<Vector2i, CellData>::Iterator E = L->value.find(pk);

	int source_id = p_source_id;
	Vector2i atlas_coords = p_atlas_coords;
	int alternative_tile = p_alternative_tile;

	if ((source_id == TileSet::INVALID_SOURCE || atlas_coords == TileSetSource::INVALID_ATLAS_COORDS || alternative_tile == TileSetSource::INVALID_TILE_ALTERNATIVE) &&
			(source_id != TileSet::INVALID_SOURCE || atlas_coords != TileSetSource::INVALID_ATLAS_COORDS || alternative_tile != TileSetSource::INVALID_TILE_ALTERNATIVE)) {
		source_id = TileSet::INVALID_SOURCE;
		atlas_coords = TileSetSource::INVALID_ATLAS_COORDS;
		alternative_tile = TileSetSource::INVALID_TILE_ALTERNATIVE;
	}

	if (!E) {
		if (source_id == TileSet::INVALID_SOURCE) {
			return; // Nothing to do, the tile is already empty.
		}

		// Insert a new cell in the tile map.
		CellData new_cell_data;
		new_cell_data.coords = pk;
		E = L->value.insert(pk, new_cell_data);
	} else {
		if (E->value.cell.source_id == source_id && E->value.cell.get_atlas_coords() == atlas_coords && E->value.cell.alternative_tile == alternative_tile) {
			return; // Nothing changed.
		}
	}

	TileMapCell &c = E->value.cell;
	c.source_id = source_id;
	c.set_atlas_coords(atlas_coords);
	c.alternative_tile = alternative_tile;

	// Make the given cell dirty.
	if (!E->value.dirty_list_element.in_list()) {
		dirty.cell_list.add(&(E->value.dirty_list_element));
	}
	_queue_internal_update();

	used_rect_cache_dirty = true;
}

void TileMapLayer25D::set_cell(const Vector3i &p_cell, int p_source_id, const Vector2i &p_atlas_coords, int p_alternative_tile) {
	set_cell(Vector2i(p_cell.x, p_cell.y), p_cell.z, p_source_id, p_atlas_coords, p_alternative_tile);
}

void TileMapLayer25D::set_cell(const Vector2i &p_coords, int16_t p_z, const TileMapCell &p_tile_map_cell) {
	set_cell(p_coords, p_z, p_tile_map_cell.source_id, p_tile_map_cell.get_atlas_coords(), p_tile_map_cell.alternative_tile);
}

void TileMapLayer25D::set_cell(const Vector3i &p_cell, const TileMapCell &p_tile_map_cell) {
	set_cell(Vector2i(p_cell.x, p_cell.y), p_cell.z, p_tile_map_cell.source_id, p_tile_map_cell.get_atlas_coords(), p_tile_map_cell.alternative_tile);
}

void TileMapLayer25D::erase_cell(const Vector2i &p_coords, int16_t p_z) {
	set_cell(p_coords, p_z, TileSet::INVALID_SOURCE, TileSetSource::INVALID_ATLAS_COORDS, TileSetSource::INVALID_TILE_ALTERNATIVE);
}

void TileMapLayer25D::fix_invalid_tiles() {
	ERR_FAIL_COND_MSG(tile_set.is_null(), "Cannot call fix_invalid_tiles() on a TileMapLayer without a valid TileSet.");

    for (const KeyValue<int16_t, HashMap<Vector2i, CellData>> &L : tile_map_layer_levels) {
        RBSet<Vector2i> coords;
        for (const KeyValue<Vector2i, CellData> &E : L.value) {
            TileSetSource *source = *tile_set->get_source(E.value.cell.source_id);
            if (!source || !source->has_tile(E.value.cell.get_atlas_coords()) || !source->has_alternative_tile(E.value.cell.get_atlas_coords(), E.value.cell.alternative_tile)) {
                coords.insert(E.key);
            }
        }
        for (const Vector2i &E : coords) {
            set_cell(E, L.key, TileSet::INVALID_SOURCE, TileSetSource::INVALID_ATLAS_COORDS, TileSetSource::INVALID_TILE_ALTERNATIVE);
        }
    }
}

void TileMapLayer25D::clear() {
    for(KeyValue<int16_t, HashMap<Vector2i, CellData>> &kvz : tile_map_layer_levels) {
	// Remove all tiles.
	    for (KeyValue<Vector2i, CellData> &kv : kvz.value) {
		    erase_cell(kv.key);
	    }
    }
	used_rect_cache_dirty = true;
}

int TileMapLayer25D::get_cell_source_id(const Vector2i &p_coords, int16_t p_z) const {
    if(!tile_map_layer_levels.has(p_z)) return TileSet::INVALID_SOURCE;
    HashMap<Vector2i, CellData>::ConstIterator E = get_tile_map_layer_data(p_z).find(p_coords);

	if (!E) {
		return TileSet::INVALID_SOURCE;
	}

	return E->value.cell.source_id;
}

Vector2i TileMapLayer25D::get_cell_atlas_coords(const Vector2i &p_coords, int16_t p_z) const {
	if(!tile_map_layer_levels.has(p_z)) return TileSetSource::INVALID_ATLAS_COORDS;
    HashMap<Vector2i, CellData>::ConstIterator E = get_tile_map_layer_data(p_z).find(p_coords);

	if (!E) {
		return TileSetSource::INVALID_ATLAS_COORDS;
	}

	return E->value.cell.get_atlas_coords();
}

int TileMapLayer25D::get_cell_alternative_tile(const Vector2i &p_coords, int16_t p_z) const {
	if(!tile_map_layer_levels.has(p_z)) return TileSetSource::INVALID_TILE_ALTERNATIVE;
    HashMap<Vector2i, CellData>::ConstIterator E = get_tile_map_layer_data(p_z).find(p_coords);

	if (!E) {
		return TileSetSource::INVALID_TILE_ALTERNATIVE;
	}

	return E->value.cell.alternative_tile;
}

TileMapCell TileMapLayer25D::get_cell_tile(const Vector2i &p_coords, int16_t p_z) const {
	if(!tile_map_layer_levels.has(p_z)) return TileMapCell();
    HashMap<Vector2i, CellData>::ConstIterator E = get_tile_map_layer_data(p_z).find(p_coords);

	if (!E) {
		return TileMapCell();
    }

	return E->value.cell;
}

TileData *TileMapLayer25D::get_cell_tile_data(const Vector2i &p_coords, int16_t p_z) const {
    TileMapCell cell = get_cell_tile(p_coords, p_z);
	int source_id = cell.source_id;
	if (source_id == TileSet::INVALID_SOURCE) {
		return nullptr;
	}

	Ref<TileSetAtlasSource> source = tile_set->get_source(source_id);
	if (source.is_valid()) {
		return source->get_tile_data(cell.get_atlas_coords(), cell.alternative_tile);
	}

	return nullptr;
}

TypedArray<Vector2i> TileMapLayer25D::get_used_cells(int16_t p_z) const {
    // Returns the cells used in the tilemap.
	TypedArray<Vector2i> a;
    if(!tile_map_layer_levels.has(p_z)) return a;

	for (const KeyValue<Vector2i, CellData> &E : get_tile_map_layer_data(p_z)) {
		const TileMapCell &c = E.value.cell;
		if (c.source_id == TileSet::INVALID_SOURCE) {
			continue;
		}
		a.push_back(E.key);
	}

	return a;
}

TypedArray<Vector2i> TileMapLayer25D::get_used_cells_by_id(int16_t p_z, int p_source_id, const Vector2i &p_atlas_coords, int p_alternative_tile) const {
	// Returns the cells used in the tilemap.
	TypedArray<Vector2i> a;
    if(!tile_map_layer_levels.has(p_z)) return a;

	for (const KeyValue<Vector2i, CellData> &E : get_tile_map_layer_data(p_z)) {
		const TileMapCell &c = E.value.cell;
		if (c.source_id == TileSet::INVALID_SOURCE) {
			continue;
		}
		if ((p_source_id == TileSet::INVALID_SOURCE || p_source_id == c.source_id) &&
				(p_atlas_coords == TileSetSource::INVALID_ATLAS_COORDS || p_atlas_coords == c.get_atlas_coords()) &&
				(p_alternative_tile == TileSetSource::INVALID_TILE_ALTERNATIVE || p_alternative_tile == c.alternative_tile)) {
			a.push_back(E.key);
		}
	}

	return a;
}

Rect2i TileMapLayer25D::get_used_rect(int16_t p_z) const {
  if(!tile_map_layer_levels.has(p_z)) return Rect2i();

	// Return the rect of the currently used area.
	if (used_rect_cache_dirty) {
		used_rect_cache = Rect2i();

		bool first = true;
		for (const KeyValue<Vector2i, CellData> &E : get_tile_map_layer_data(p_z)) {
			const TileMapCell &c = E.value.cell;
			if (c.source_id == TileSet::INVALID_SOURCE) {
				continue;
			}
			if (first) {
				used_rect_cache = Rect2i(E.key, Size2i());
				first = false;
			} else {
				used_rect_cache.expand_to(E.key);
			}
		}
		if (!first) {
			// Only if we have at least one cell.
			// The cache expands to top-left coordinate, so we add one full tile.
			used_rect_cache.size += Vector2i(1, 1);
		}
		used_rect_cache_dirty = false;
	}

	return used_rect_cache;
}

bool TileMapLayer25D::is_cell_flipped_h(const Vector2i &p_coords, int16_t p_z) const {
	return get_cell_alternative_tile(p_coords, p_z) & TileSetAtlasSource::TRANSFORM_FLIP_H;
}

bool TileMapLayer25D::is_cell_flipped_v(const Vector2i &p_coords, int16_t p_z) const {
	return get_cell_alternative_tile(p_coords, p_z) & TileSetAtlasSource::TRANSFORM_FLIP_V;
}

bool TileMapLayer25D::is_cell_transposed(const Vector2i &p_coords, int16_t p_z) const {
	return get_cell_alternative_tile(p_coords, p_z) & TileSetAtlasSource::TRANSFORM_TRANSPOSE;
}

Ref<TileMapPattern> TileMapLayer25D::get_pattern(TypedArray<Vector2i> p_coords_array) {
	ERR_FAIL_COND_V(tile_set.is_null(), nullptr);

	Ref<TileMapPattern> output;
	output.instantiate();
	if (p_coords_array.is_empty()) {
		return output;
	}

	Vector2i min = Vector2i(p_coords_array[0]);
	for (int i = 1; i < p_coords_array.size(); i++) {
		min = min.min(p_coords_array[i]);
	}

	Vector<Vector2i> coords_in_pattern_array;
	coords_in_pattern_array.resize(p_coords_array.size());
	Vector2i ensure_positive_offset;
	for (int i = 0; i < p_coords_array.size(); i++) {
		Vector2i coords = p_coords_array[i];
		Vector2i coords_in_pattern = coords - min;
		if (tile_set->get_tile_shape() != TileSet::TILE_SHAPE_SQUARE) {
			if (tile_set->get_tile_layout() == TileSet::TILE_LAYOUT_STACKED) {
				if (tile_set->get_tile_offset_axis() == TileSet::TILE_OFFSET_AXIS_HORIZONTAL && bool(min.y % 2) && bool(coords_in_pattern.y % 2)) {
					coords_in_pattern.x -= 1;
					if (coords_in_pattern.x < 0) {
						ensure_positive_offset.x = 1;
					}
				} else if (tile_set->get_tile_offset_axis() == TileSet::TILE_OFFSET_AXIS_VERTICAL && bool(min.x % 2) && bool(coords_in_pattern.x % 2)) {
					coords_in_pattern.y -= 1;
					if (coords_in_pattern.y < 0) {
						ensure_positive_offset.y = 1;
					}
				}
			} else if (tile_set->get_tile_layout() == TileSet::TILE_LAYOUT_STACKED_OFFSET) {
				if (tile_set->get_tile_offset_axis() == TileSet::TILE_OFFSET_AXIS_HORIZONTAL && bool(min.y % 2) && bool(coords_in_pattern.y % 2)) {
					coords_in_pattern.x += 1;
				} else if (tile_set->get_tile_offset_axis() == TileSet::TILE_OFFSET_AXIS_VERTICAL && bool(min.x % 2) && bool(coords_in_pattern.x % 2)) {
					coords_in_pattern.y += 1;
				}
			}
		}
		coords_in_pattern_array.write[i] = coords_in_pattern;
	}

	for (int i = 0; i < coords_in_pattern_array.size(); i++) {
		Vector2i coords = p_coords_array[i];
		Vector2i coords_in_pattern = coords_in_pattern_array[i];
		output->set_cell(coords_in_pattern + ensure_positive_offset, get_cell_source_id(coords), get_cell_atlas_coords(coords), get_cell_alternative_tile(coords));
	}

	return output;
}

void TileMapLayer25D::set_pattern(const Vector2i &p_position, int16_t p_z, const Ref<TileMapPattern> p_pattern) {
	ERR_FAIL_COND(tile_set.is_null());
	ERR_FAIL_COND(p_pattern.is_null());
    ERR_FAIL_COND(!tile_map_layer_levels.has(p_z));

	TypedArray<Vector2i> used_cells = p_pattern->get_used_cells();
	for (int i = 0; i < used_cells.size(); i++) {
		Vector2i coords = tile_set->map_pattern(p_position, used_cells[i], p_pattern);
		set_cell(coords, p_z, p_pattern->get_cell_source_id(used_cells[i]), p_pattern->get_cell_atlas_coords(used_cells[i]), p_pattern->get_cell_alternative_tile(used_cells[i]));
	}
}

void TileMapLayer25D::update_internals() {
	_internal_update(false);
}

Vector2i TileMapLayer25D::map_pattern(const Vector2i &p_position_in_tilemap, const Vector2i &p_coords_in_pattern, Ref<TileMapPattern> p_pattern) {
	ERR_FAIL_COND_V(tile_set.is_null(), Vector2i());
	return tile_set->map_pattern(p_position_in_tilemap, p_coords_in_pattern, p_pattern);
}

TypedArray<Vector2i> TileMapLayer25D::get_surrounding_cells_level(const Vector2i &p_coords) {
	ERR_FAIL_COND_V(tile_set.is_null(), TypedArray<Vector2i>());
	return tile_set->get_surrounding_cells(p_coords);
}

TypedArray<Vector3i> TileMapLayer25D::get_surrounding_cells(const Vector2i &p_coords, int16_t p_z) {
	ERR_FAIL_COND_V(tile_set.is_null(), TypedArray<Vector3i>());

    TypedArray<Vector3i> cells;
    for (Vector2i coords : tile_set->get_surrounding_cells(p_coords)) {
        Vector3i mid = Vector3i(coords.x, coords.y, p_z);
        cells.push_back(mid);
        cells.push_back(mid + Vector3i::FORWARD);
        cells.push_back(mid + Vector3i::BACK);
    }
	return cells;
}

Vector2i TileMapLayer25D::get_neighbor_cell(const Vector2i &p_coords, TileSet::CellNeighbor p_cell_neighbor) const {
	ERR_FAIL_COND_V(tile_set.is_null(), Vector2i());
	return tile_set->get_neighbor_cell(p_coords, p_cell_neighbor);
}

Vector2 TileMapLayer25D::map_level_to_local(const Vector2i &p_pos) const {
	ERR_FAIL_COND_V(tile_set.is_null(), Vector2());
	return tile_set->map_to_local(p_pos);
}

Vector2i TileMapLayer25D::local_to_map_level(const Vector2 &p_pos) const {
	ERR_FAIL_COND_V(tile_set.is_null(), Vector2i());
	return tile_set->local_to_map(p_pos);
}

Vector3 TileMapLayer25D::map_to_local(const Vector2i &p_pos, int16_t p_z) const {
	ERR_FAIL_COND_V(tile_set.is_null(), Vector3());
    
  Vector2 level_pos = tile_set->map_to_local(p_pos);
	return Vector3(level_pos.x, level_pos.y, p_z * 24);
}

Vector3i TileMapLayer25D::local_to_map(const Vector3 &p_pos) const {
	ERR_FAIL_COND_V(tile_set.is_null(), Vector3i());

	Vector2i level_coords = tile_set->local_to_map(Vector2(p_pos.x, p_pos.y));
	return Vector3i(level_coords.x, level_coords.y, (int16_t)floor(p_pos.z / 24));
}

Vector2i TileMapLayer25D::local_viewport_to_map(const Vector2 &p_pos, int16_t p_z) const {
	ERR_FAIL_COND_V(tile_set.is_null(), Vector2i());
	return tile_set->local_to_map(Vector2(p_pos.x, p_pos.y + (24.0f * 0.75f * (float)p_z)));
}

Vector2 TileMapLayer25D::map_to_local_viewport(const Vector2i &p_coords, int16_t p_z) const {
	ERR_FAIL_COND_V(tile_set.is_null(), Vector2());
	return tile_set->map_to_local(p_coords) + Vector2(0, -(float)p_z * 24.0f * 0.75f);
}

Vector2i TileMapLayer25D::get_coords_from_mouse_position(int16_t p_z) const {
	ERR_FAIL_COND_V(tile_set.is_null(), Vector2i());
	return local_viewport_to_map(get_local_mouse_position(), p_z);
}

void TileMapLayer25D::set_enabled(bool p_enabled) {
	if (enabled == p_enabled) {
		return;
	}
	enabled = p_enabled;
	dirty.flags[DIRTY_FLAGS_LAYER_ENABLED] = true;
	_queue_internal_update();
	emit_signal(CoreStringName(changed));
}

bool TileMapLayer25D::is_enabled() const {
	return enabled;
}

void TileMapLayer25D::set_tile_set(const Ref<TileSet> &p_tile_set) {
	if (p_tile_set == tile_set) {
		return;
	}

	dirty.flags[DIRTY_FLAGS_TILE_SET] = true;
	_queue_internal_update();

	// Set the TileSet, registering to its changes.
	if (tile_set.is_valid()) {
		tile_set->disconnect_changed(callable_mp(this, &TileMapLayer25D::_tile_set_changed));
	}

	tile_set = p_tile_set;

	if (tile_set.is_valid()) {
		tile_set->connect_changed(callable_mp(this, &TileMapLayer25D::_tile_set_changed));
	}

	emit_signal(CoreStringName(changed));

	// Trigger updates for TileSet's read-only status.
	notify_property_list_changed();
}

Ref<TileSet> TileMapLayer25D::get_tile_set() const {
	return tile_set;
}

void TileMapLayer25D::set_highlight_mode(HighlightMode p_highlight_mode) {
	if (p_highlight_mode == highlight_mode) {
		return;
	}
	highlight_mode = p_highlight_mode;
	dirty.flags[DIRTY_FLAGS_LAYER_HIGHLIGHT_MODE] = true;
	_queue_internal_update();
}

TileMapLayer25D::HighlightMode TileMapLayer25D::get_highlight_mode() const {
	return highlight_mode;
}

void TileMapLayer25D::set_tile_map_data_from_array(const Vector<uint8_t> &p_data) {
    //TODO - save data for multiple levels 
	if (p_data.is_empty()) {
		clear();
		return;
	}

	const int cell_data_struct_size = 12;

	int size = p_data.size();
	const uint8_t *ptr = p_data.ptr();

	// Index in the array.
	int index = 0;

	// First extract the data version.
	ERR_FAIL_COND_MSG(size < 2, "Corrupted tile map data: not enough bytes.");
	uint16_t format = decode_uint16(&ptr[index]);
	index += 2;
	ERR_FAIL_COND_MSG(format >= TileMapLayerDataFormat::TILE_MAP_LAYER_DATA_FORMAT_MAX, vformat("Unsupported tile map data format: %s. Expected format ID lower or equal to: %s", format, TileMapLayerDataFormat::TILE_MAP_LAYER_DATA_FORMAT_MAX - 1));

	// Clear the TileMap.
	clear();

	while (index < size) {
		ERR_FAIL_COND_MSG(index + cell_data_struct_size > size, vformat("Corrupted tile map data: tiles might be missing."));

		// Get a pointer at the start of the cell data.
		const uint8_t *cell_data_ptr = &ptr[index];

		// Extracts position in TileMap.
		int16_t x = decode_uint16(&cell_data_ptr[0]);
		int16_t y = decode_uint16(&cell_data_ptr[2]);

		// Extracts the tile identifiers.
		uint16_t source_id = decode_uint16(&cell_data_ptr[4]);
		uint16_t atlas_coords_x = decode_uint16(&cell_data_ptr[6]);
		uint16_t atlas_coords_y = decode_uint16(&cell_data_ptr[8]);
		uint16_t alternative_tile = decode_uint16(&cell_data_ptr[10]);

		set_cell(Vector2i(x, y), 0, source_id, Vector2i(atlas_coords_x, atlas_coords_y), alternative_tile);
		index += cell_data_struct_size;
	}
}

Vector<uint8_t> TileMapLayer25D::get_tile_map_data_as_array() const {
    //TODO - save data for multiple levels 

	const int cell_data_struct_size = 12;

	Vector<uint8_t> tile_map_data_array;
	if (tile_map_layer_levels.is_empty()) {
		return tile_map_data_array;
	}

    /*
	tile_map_data_array.resize(2 + tile_map_layer_data.size() * cell_data_struct_size);
	uint8_t *ptr = tile_map_data_array.ptrw();

	// Index in the array.
	int index = 0;

	// Save the version.
	encode_uint16(TileMapLayerDataFormat::TILE_MAP_LAYER_DATA_FORMAT_MAX - 1, &ptr[index]);
	index += 2;

	// Save in highest format.
	for (const KeyValue<Vector2i, CellData> &E : tile_map_layer_data) {
		// Get a pointer at the start of the cell data.
		uint8_t *cell_data_ptr = (uint8_t *)&ptr[index];

		// Store position in TileMap.
		encode_uint16((int16_t)(E.key.x), &cell_data_ptr[0]);
		encode_uint16((int16_t)(E.key.y), &cell_data_ptr[2]);

		// Store the tile identifiers.
		encode_uint16(E.value.cell.source_id, &cell_data_ptr[4]);
		encode_uint16(E.value.cell.coord_x, &cell_data_ptr[6]);
		encode_uint16(E.value.cell.coord_y, &cell_data_ptr[8]);
		encode_uint16(E.value.cell.alternative_tile, &cell_data_ptr[10]);

		index += cell_data_struct_size;
	}
    */
	return tile_map_data_array;
}

void TileMapLayer25D::set_self_modulate(const Color &p_self_modulate) {
	if (get_self_modulate() == p_self_modulate) {
		return;
	}
	CanvasItem::set_self_modulate(p_self_modulate);
	dirty.flags[DIRTY_FLAGS_LAYER_SELF_MODULATE] = true;
	_queue_internal_update();
	emit_signal(CoreStringName(changed));
}

void TileMapLayer25D::set_y_sort_enabled(bool p_y_sort_enabled) {
	if (is_y_sort_enabled() == p_y_sort_enabled) {
		return;
	}
	CanvasItem::set_y_sort_enabled(p_y_sort_enabled);
	dirty.flags[DIRTY_FLAGS_LAYER_Y_SORT_ENABLED] = true;
	_queue_internal_update();
	emit_signal(CoreStringName(changed));

	notify_property_list_changed();
	_update_notify_local_transform();
}

void TileMapLayer25D::set_y_sort_origin(int p_y_sort_origin) {
	if (y_sort_origin == p_y_sort_origin) {
		return;
	}
	y_sort_origin = p_y_sort_origin;
	dirty.flags[DIRTY_FLAGS_LAYER_Y_SORT_ORIGIN] = true;
	_queue_internal_update();
	emit_signal(CoreStringName(changed));
}

int TileMapLayer25D::get_y_sort_origin() const {
	return y_sort_origin;
}

void TileMapLayer25D::set_z_index(int p_z_index) {
	if (get_z_index() == p_z_index) {
		return;
	}
	CanvasItem::set_z_index(p_z_index);
	dirty.flags[DIRTY_FLAGS_LAYER_Z_INDEX] = true;
	_queue_internal_update();
	emit_signal(CoreStringName(changed));
}

void TileMapLayer25D::set_light_mask(int p_light_mask) {
	if (get_light_mask() == p_light_mask) {
		return;
	}
	CanvasItem::set_light_mask(p_light_mask);
	dirty.flags[DIRTY_FLAGS_LAYER_LIGHT_MASK] = true;
	_queue_internal_update();
	emit_signal(CoreStringName(changed));
}

void TileMapLayer25D::set_collision_enabled(bool p_enabled) {
	if (collision_enabled == p_enabled) {
		return;
	}
	collision_enabled = p_enabled;
	dirty.flags[DIRTY_FLAGS_LAYER_COLLISION_ENABLED] = true;
	_queue_internal_update();
	emit_signal(CoreStringName(changed));
}

bool TileMapLayer25D::is_collision_enabled() const {
	return collision_enabled;
}

void TileMapLayer25D::set_use_kinematic_bodies(bool p_use_kinematic_bodies) {
	if (use_kinematic_bodies == p_use_kinematic_bodies) {
		return;
	}
	use_kinematic_bodies = p_use_kinematic_bodies;
	dirty.flags[DIRTY_FLAGS_LAYER_USE_KINEMATIC_BODIES] = p_use_kinematic_bodies;
	_queue_internal_update();
	emit_signal(CoreStringName(changed));
}

bool TileMapLayer25D::is_using_kinematic_bodies() const {
	return use_kinematic_bodies;
}

void TileMapLayer25D::set_collision_visibility_mode(TileMapLayer25D::DebugVisibilityMode p_show_collision) {
	if (collision_visibility_mode == p_show_collision) {
		return;
	}
	collision_visibility_mode = p_show_collision;
	dirty.flags[DIRTY_FLAGS_LAYER_COLLISION_VISIBILITY_MODE] = true;
	_queue_internal_update();
	emit_signal(CoreStringName(changed));
}

TileMapLayer25D::DebugVisibilityMode TileMapLayer25D::get_collision_visibility_mode() const {
	return collision_visibility_mode;
}

void TileMapLayer25D::set_occlusion_enabled(bool p_enabled) {
	if (occlusion_enabled == p_enabled) {
		return;
	}
	occlusion_enabled = p_enabled;
	dirty.flags[DIRTY_FLAGS_LAYER_OCCLUSION_ENABLED] = true;
	_queue_internal_update();
	emit_signal(CoreStringName(changed));
}

bool TileMapLayer25D::is_occlusion_enabled() const {
	return occlusion_enabled;
}

#ifndef NAVIGATION_3D_DISABLED
void TileMapLayer25D::set_navigation_enabled(bool p_enabled) {
	if (navigation_enabled == p_enabled) {
		return;
	}
	navigation_enabled = p_enabled;
	dirty.flags[DIRTY_FLAGS_LAYER_NAVIGATION_ENABLED] = true;
	_queue_internal_update();
	emit_signal(CoreStringName(changed));
}

bool TileMapLayer25D::is_navigation_enabled() const {
	return navigation_enabled;
}

void TileMapLayer25D::set_navigation_visibility_mode(TileMapLayer25D::DebugVisibilityMode p_show_navigation) {
	if (navigation_visibility_mode == p_show_navigation) {
		return;
	}
	navigation_visibility_mode = p_show_navigation;
	dirty.flags[DIRTY_FLAGS_LAYER_NAVIGATION_VISIBILITY_MODE] = true;
	_queue_internal_update();
	emit_signal(CoreStringName(changed));
}

TileMapLayer25D::DebugVisibilityMode TileMapLayer25D::get_navigation_visibility_mode() const {
	return navigation_visibility_mode;
}
#endif

TileMapLayer25D::TileMapLayer25D() {
	set_notify_transform(true);
}

TileMapLayer25D::~TileMapLayer25D() {
	clear();
	_internal_update(true);
}