/* TCG Custom class */
#pragma once

#include "scene/resources/2d/tile_set.h"
#include "scene/25d/tile_maps_25d.h"
#include "scene/25d/tile_mesh_library.h"
#include "servers/rendering/rendering_server_enums.h"

namespace TCG {
	const int CHUNK_SIZE = 16; // Temporarily - will be added to a globally referenced chunking class later
	enum TileMapLayerDataFormat {
		TILE_MAP_LAYER_DATA_FORMAT_0 = 0,
		TILE_MAP_LAYER_DATA_FORMAT_MAX,·
	};

	class ChunkLayerData;
	class RenderingSlice;

	struct CellData {
		Vector2i coords;
		TileMapCell cell;
		int z;

		// Rendering.
		Ref<RenderingSlice> rendering_slice;
		SelfList<CellData> slice_list_element;

		// List elements.
		SelfList<CellData> dirty_list_element;

		bool operator<(const CellData &p_other) const {
			return coords < p_other.coords;
		}

		// For those, copy everything but SelfList elements.
		void operator=(const CellData &p_other) {
			coords = p_other.coords;
			z = p_other.z;
			cell = p_other.cell;
		}

		CellData(const CellData &p_other) :
				slice_list_element(this),
				dirty_list_element(this) {
			coords = p_other.coords;
			z = p_other.z;
			cell = p_other.cell;
		}

		CellData() :
				slice_list_element(this),
				dirty_list_element(this) {}
	};

	class ChunkLayerData : public RefCounted {
		GDCLASS(ChunkLayerData, RefCounted);

		public:
			HashMap<int16_t, Ref<RenderingSlice>> rendering_slices;

			struct MultimeshInstance {
				RID instance;
				RID multimesh;
				struct Item {
					int index = 0;
					Transform3D transform;
					CellKey key;
				};

				Vector<Item> items;
			};

			Vector<MultimeshInstance> multimesh_instances;
			RID collision_debug;
			RID collision_debug_instance;

			bool dirty = false;
			RID static_body;
	};

	class RenderingSlice : public RefCounted {
		GDCLASS(RenderingSlice, RefCounted);

	public:
		int16_t z;
		SelfList<CellData>::List cells;
		List<RID> canvas_items;
		Vector2 canvas_items_position;

		SelfList<RenderingSlice> dirty_slice_list_element;

		RenderingSlice() :
				dirty_slice_list_element(this) {
		}

		~RenderingSlice() {
			cells.clear();
		}
	};
}

class TileMapLayer25D : public Node2D {
	GDCLASS(TileMapLayer25D, Node2D);

public:
	enum HighlightMode {
		HIGHLIGHT_MODE_DEFAULT,
		HIGHLIGHT_MODE_ABOVE,
		HIGHLIGHT_MODE_BELOW,
	};

	enum DebugVisibilityMode {
		DEBUG_VISIBILITY_MODE_DEFAULT,
		DEBUG_VISIBILITY_MODE_FORCE_SHOW,
		DEBUG_VISIBILITY_MODE_FORCE_HIDE,
	};

	enum DirtyFlags {
		DIRTY_FLAGS_LAYER_ENABLED = 0,

		DIRTY_FLAGS_LAYER_IN_TREE,
		DIRTY_FLAGS_LAYER_IN_CANVAS,
		DIRTY_FLAGS_LAYER_LOCAL_TRANSFORM,
		DIRTY_FLAGS_LAYER_VISIBILITY,
		DIRTY_FLAGS_LAYER_SELF_MODULATE,
		DIRTY_FLAGS_LAYER_Y_SORT_ENABLED,
		DIRTY_FLAGS_LAYER_Y_SORT_ORIGIN,
		DIRTY_FLAGS_LAYER_X_DRAW_ORDER_REVERSED,
		DIRTY_FLAGS_LAYER_Z_INDEX,
		DIRTY_FLAGS_LAYER_LIGHT_MASK,
		DIRTY_FLAGS_LAYER_TEXTURE_FILTER,
		DIRTY_FLAGS_LAYER_TEXTURE_REPEAT,
		DIRTY_FLAGS_LAYER_RENDERING_QUADRANT_SIZE,
		DIRTY_FLAGS_LAYER_COLLISION_ENABLED,
		DIRTY_FLAGS_LAYER_USE_KINEMATIC_BODIES,
		DIRTY_FLAGS_LAYER_PHYSICS_QUADRANT_SIZE,
		DIRTY_FLAGS_LAYER_COLLISION_VISIBILITY_MODE,
		DIRTY_FLAGS_LAYER_OCCLUSION_ENABLED,
		DIRTY_FLAGS_LAYER_NAVIGATION_ENABLED,
		DIRTY_FLAGS_LAYER_NAVIGATION_MAP,
		DIRTY_FLAGS_LAYER_NAVIGATION_VISIBILITY_MODE,
		DIRTY_FLAGS_LAYER_RUNTIME_UPDATE,
		DIRTY_FLAGS_LAYER_HIGHLIGHT_MODE,

		DIRTY_FLAGS_LAYER_INDEX_IN_TILE_MAP_NODE, // For compatibility.

		DIRTY_FLAGS_LAYER_GROUP_SELECTED_LAYERS,
		DIRTY_FLAGS_LAYER_GROUP_HIGHLIGHT_SELECTED,

		DIRTY_FLAGS_TILE_SET,

		DIRTY_FLAGS_MAX,
	};

private:
	static constexpr float FP_ADJUST = 0.00001;

	// Properties.
	HashMap<int16_t, HashMap<Vector2i, TCG::CellData>> tile_map_layer_levels;
	//HashMap<Vector2i, CellData> tile_map_layer_data;

	bool enabled = true;
	Ref<TileSet> tile_set;

	HighlightMode highlight_mode = HIGHLIGHT_MODE_DEFAULT;

	int y_sort_origin = 0;

	bool collision_enabled = true;
	bool use_kinematic_bodies = false;
	DebugVisibilityMode collision_visibility_mode = DEBUG_VISIBILITY_MODE_DEFAULT;

	bool occlusion_enabled = true;

	bool navigation_enabled = true;
	RID navigation_map_override;
	DebugVisibilityMode navigation_visibility_mode = DEBUG_VISIBILITY_MODE_DEFAULT;

	// Internal.
	bool pending_update = false;

	// Dirty flag. Allows knowing what was modified since the last update.
	struct {
		bool flags[DIRTY_FLAGS_MAX] = { false };
		SelfList<TCG::CellData>::List cell_list;
	} dirty;

	// Rect cache.
	mutable Rect2 rect_cache;
	mutable bool rect_cache_dirty = true;
	mutable Rect2i used_rect_cache;
	mutable bool used_rect_cache_dirty = true;

	// Runtime tile data.
	void _update_cells_callback(bool p_force_cleanup);

	// Coords to chunk coords
	Vector2i _get_chunk_coords() const;
	Vector2i _coords_to_chunk_coords(const Vector2i &p_coords, int p_chunk_size) const;

	TCG::ChunkLayerData chunkData;
	bool _rendering_was_cleaned_up = true;
	void _rendering_update(bool p_force_cleanup);
	void _rendering_notification(int p_what);
	Color _highlight_color(const Color &p_modulate) const;
	void _rendering_slice_update_cell(TCG::CellData &r_cell_data, SelfList<TCG::RenderingSlice>::List &r_dirty_slice_list);

/*#ifdef DEBUG_ENABLED
	void _rendering_draw_cell_debug(const RID &p_canvas_item, const Vector2 &p_chunk_pos, float z, const TCG::CellData &r_cell_data);
#endif // DEBUG_ENABLED*/

	bool _scenes_was_cleaned_up = true;
	void _scenes_update(bool p_force_cleanup);
	void _scenes_clear_cell(TCG::CellData &r_cell_data);
	void _scenes_update_cell(TCG::CellData &r_cell_data);
/*#ifdef DEBUG_ENABLED
	void _scenes_draw_cell_debug(const RID &p_canvas_item, const Vector2 &p_chunk_pos, float z, const TCG::CellData &r_cell_data);
#endif // DEBUG_ENABLED*/
	void _set_scene_transform_with_alternative(Node2D *p_scene, const Vector2 &p_cell_position, const int p_alternative_id);

	void _tile_set_changed();

	void _renamed();
	void _update_notify_local_transform();

	// Internal updates.
	void _queue_internal_update();
	void _deferred_internal_update();
	void _internal_update(bool p_force_cleanup);

	virtual void _physics_interpolated_changed() override;

protected:
	void _notification(int p_what);

	static void _bind_methods();
	void _validate_property(PropertyInfo &p_property) const;

	virtual void _update_self_texture_filter(RSE::CanvasItemTextureFilter p_texture_filter) override;
	virtual void _update_self_texture_repeat(RSE::CanvasItemTextureRepeat p_texture_repeat) override;

public:
/*#ifdef TOOLS_ENABLED
	virtual bool _edit_is_selected_on_click(const Point2 &p_point, double p_tolerance) const override;
#endif*/

	const HashMap<int16_t, HashMap<Vector2i, TCG::CellData>> &get_tile_map_layer_levels() const {
		return tile_map_layer_levels;
	}
	const HashMap<Vector2i, TCG::CellData> &get_tile_map_layer_data(int16_t p_z = 0) const {
		static const HashMap<Vector2i, TCG::CellData> empty;
		auto it = tile_map_layer_levels.find(p_z);
		return it != tile_map_layer_levels.end() ? it->value : empty;
	}

	// Rect caching.
	Rect2 get_rect(bool &r_changed) const;

	// Not exposed to users.
	TileMapCell get_cell(const Vector2i &p_coords, int16_t p_z = 0) const;
	TileMapCell get_cell(const CellKey &p_cell_key) const;

	static void compute_transformed_tile_dest_rect(Rect2 &r_dest_rect, bool &r_transpose, const Vector2 &p_position, const Vector2 &p_dest_rect_size, const TileData *p_tile_data, int p_alternative_tile);
	static void draw_tile(RID p_canvas_item, const Vector2 &p_position, float p_z, const Ref<TileSet> p_tile_set, int p_atlas_source_id, const Vector2i &p_atlas_coords, int p_alternative_tile, int p_frame = -1, const TileData *p_tile_data_override = nullptr, real_t p_normalized_animation_offset = 0.0);

	////////////// Exposed functions //////////////

	// --- Cells manipulation ---
	// Generic cells manipulations and data access.
	void set_cell(const Vector2i &p_coords, int16_t p_z = 0, int p_source_id = TileSet::INVALID_SOURCE, const Vector2i &p_atlas_coords = TileSetSource::INVALID_ATLAS_COORDS, int p_alternative_tile = 0);
	void set_cell(const Vector3i &p_cell, int p_source_id = TileSet::INVALID_SOURCE, const Vector2i &p_atlas_coords = TileSetSource::INVALID_ATLAS_COORDS, int p_alternative_tile = 0);
	void set_cell(const Vector2i &p_coords, int16_t p_z, const TileMapCell &p_tile_map_cell);
	void set_cell(const Vector3i &p_cell, const TileMapCell &p_tile_map_cell);
	void erase_cell(const Vector2i &p_coords, int16_t p_z = 0);
	void fix_invalid_tiles();
	void clear();

	int get_cell_source_id(const Vector2i &p_coords, int16_t p_z = 0) const;
	Vector2i get_cell_atlas_coords(const Vector2i &p_coords, int16_t p_z = 0) const;
	int get_cell_alternative_tile(const Vector2i &p_coords, int16_t p_z = 0) const;
	TileMapCell get_cell_tile(const Vector2i &p_coords, int16_t p_z = 0) const;
	TileData *get_cell_tile_data(const Vector2i &p_coords, int16_t p_z = 0) const; // Helper method to make accessing the data easier.

	TypedArray<Vector2i> get_used_cells(int16_t p_z = 0) const;
	TypedArray<Vector2i> get_used_cells_by_id(int16_t p_z = 0, int p_source_id = TileSet::INVALID_SOURCE, const Vector2i &p_atlas_coords = TileSetSource::INVALID_ATLAS_COORDS, int p_alternative_tile = TileSetSource::INVALID_TILE_ALTERNATIVE) const;
	Rect2i get_used_rect(int16_t p_z = 0) const;

	bool is_cell_flipped_h(const Vector2i &p_coords, int16_t p_z = 0) const;
	bool is_cell_flipped_v(const Vector2i &p_coords, int16_t p_z = 0) const;
	bool is_cell_transposed(const Vector2i &p_coords, int16_t p_z = 0) const;

	// Patterns.
	Ref<TileMapPattern> get_pattern(TypedArray<Vector2i> p_coords_array);
	void set_pattern(const Vector2i &p_position, int16_t p_z, const Ref<TileMapPattern> p_pattern);

	// Terrains.
	void set_cells_terrain_connect(TypedArray<Vector2i> p_cells, int p_terrain_set, int p_terrain, bool p_ignore_empty_terrains = true);
	void set_cells_terrain_path(TypedArray<Vector2i> p_path, int p_terrain_set, int p_terrain, bool p_ignore_empty_terrains = true);

	// --- Runtime ---
	void update_internals();
	GDVIRTUAL2(_update_cells, TypedArray<Vector2i>, bool);

	// --- Shortcuts to methods defined in TileSet ---
	Vector2i map_pattern(const Vector2i &p_position_in_tilemap, const Vector2i &p_coords_in_pattern, Ref<TileMapPattern> p_pattern);
	TypedArray<Vector2i> get_surrounding_cells_level(const Vector2i &p_coords);
	TypedArray<Vector3i> get_surrounding_cells(const Vector2i &p_coords, int16_t p_z);
	Vector2i get_neighbor_cell(const Vector2i &p_coords, TileSet::CellNeighbor p_cell_neighbor) const;
	Vector2 map_level_to_local(const Vector2i &p_coords) const;
	Vector2i local_to_map_level(const Vector2 &p_pos) const;

	Vector3 map_to_local(const Vector2i &p_coords, int16_t z) const;
	Vector3i local_to_map(const Vector3 &p_pos) const;

	Vector2i local_viewport_to_map(const Vector2 &p_pos, int16_t p_z) const;
	Vector2 map_to_local_viewport(const Vector2i &p_coords, int16_t p_z) const;
	Vector2i get_coords_from_mouse_position(int16_t p_z) const;

	// --- Accessors ---
	void set_tile_map_data_from_array(const Vector<uint8_t> &p_data);
	Vector<uint8_t> get_tile_map_data_as_array() const;

	void set_enabled(bool p_enabled);
	bool is_enabled() const;
	void set_tile_set(const Ref<TileSet> &p_tile_set);
	Ref<TileSet> get_tile_set() const;

	void set_highlight_mode(HighlightMode p_highlight_mode);
	HighlightMode get_highlight_mode() const;

	virtual void set_self_modulate(const Color &p_self_modulate) override;
	virtual void set_y_sort_enabled(bool p_y_sort_enabled) override;
	void set_y_sort_origin(int p_y_sort_origin);
	int get_y_sort_origin() const;
	virtual void set_z_index(int p_z_index) override;
	virtual void set_light_mask(int p_light_mask) override;

	void set_collision_enabled(bool p_enabled);
	bool is_collision_enabled() const;
	void set_use_kinematic_bodies(bool p_use_kinematic_bodies);
	bool is_using_kinematic_bodies() const;
	void set_collision_visibility_mode(DebugVisibilityMode p_show_collision);
	DebugVisibilityMode get_collision_visibility_mode() const;

	void set_occlusion_enabled(bool p_enabled);
	bool is_occlusion_enabled() const;

	void set_navigation_enabled(bool p_enabled);
	bool is_navigation_enabled() const;
	void set_navigation_visibility_mode(DebugVisibilityMode p_show_navigation);
	DebugVisibilityMode get_navigation_visibility_mode() const;

	TileMapLayer25D();
	~TileMapLayer25D();
};

VARIANT_ENUM_CAST(TileMapLayer25D::DebugVisibilityMode);
