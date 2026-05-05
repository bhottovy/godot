/* TCG Custom class */
#pragma once

#include "editor/scene/2d/tiles/tile_map_layer_editor.h"
#include "core/math/random_pcg.h"
#include "core/os/thread.h"
#include "editor/docks/editor_dock.h"
#include "editor/scene/2d/tiles/tile_atlas_view.h"
#include "scene/gui/box_container.h"
#include "scene/gui/check_box.h"
#include "scene/gui/flow_container.h"
#include "scene/gui/item_list.h"
#include "scene/gui/margin_container.h"
#include "scene/gui/menu_button.h"
#include "scene/gui/option_button.h"
#include "scene/gui/separator.h"
#include "scene/gui/spin_box.h"
#include "scene/gui/split_container.h"
#include "scene/gui/tab_bar.h"
#include "scene/gui/tree.h"

class TileMapLayer25D;
class TileMapLayer25DEditor;
class TileSetSourceItemList;
class GridContainer;

class TileMapLayer25DSubEditorPlugin : public Object {
	GDSOFTCLASS(TileMapLayer25DSubEditorPlugin, Object);

protected:
	ObjectID edited_tile_map_layer_id;
	TileMapLayer25D *_get_edited_layer() const;
	int16_t edited_z;
	static void _add_to_output_if_tile_changed(HashMap<Vector2i, TileMapCell> &p_output, const TileMapLayer25D *p_layer, int16_t p_z, Vector2i p_coords, const TileMapCell &p_cell);

public:
	struct TabData {
		Vector<Control *> toolbar;
		Control *wide_toolbar;
		Control *panel = nullptr;
	};

	virtual Vector<TabData> get_tabs() const {
		return Vector<TabData>();
	}

	virtual bool forward_canvas_gui_input(const Ref<InputEvent> &p_event) { return false; }
	virtual void forward_canvas_draw_over_viewport(Control *p_overlay) {}
	virtual void tile_set_changed() {}
	virtual void edit(ObjectID p_tile_map_layer_id) {}
	virtual void draw_tile_coords_over_viewport(Control *p_overlay, const TileMapLayer25D *p_edited_layer, Ref<TileSet> p_tile_set, bool p_show_rectangle_size, const Vector2i &p_rectangle_origin);
	virtual void update_layout(EditorDock::DockLayout p_layout) {}
	virtual void update_z_level(int16_t p_z) {}
};

class TileMapLayer25DEditorTilesPlugin : public TileMapLayer25DSubEditorPlugin {
	GDCLASS(TileMapLayer25DEditorTilesPlugin, TileMapLayer25DSubEditorPlugin);

public:
	enum TileTransformType {
		TRANSFORM_ROTATE_LEFT,
		TRANSFORM_ROTATE_RIGHT,
		TRANSFORM_FLIP_H,
		TRANSFORM_FLIP_V,
	};

private:
	///// Toolbar /////
	HBoxContainer *wide_toolbar = nullptr;

	BoxContainer *tilemap_tiles_tools_buttons = nullptr;
	Ref<ButtonGroup> tool_buttons_group;
	Button *select_tool_button = nullptr;
	Button *paint_tool_button = nullptr;
	Button *line_tool_button = nullptr;
	Button *rect_tool_button = nullptr;
	Button *bucket_tool_button = nullptr;

	BoxContainer *tools_settings = nullptr;

	SwitchSeparator *tools_settings_vsep = nullptr;
	Button *picker_button = nullptr;
	Button *erase_button = nullptr;

	BoxContainer *transform_toolbar = nullptr;
	Button *transform_button_rotate_left = nullptr;
	Button *transform_button_rotate_right = nullptr;
	Button *transform_button_flip_h = nullptr;
	Button *transform_button_flip_v = nullptr;
	SwitchSeparator *transform_separator = nullptr;

	CheckBox *bucket_contiguous_checkbox = nullptr;
	Button *random_tile_toggle = nullptr;

	BoxContainer *scatter_controls_container = nullptr;
	float scattering = 0.0;
	Label *scatter_label = nullptr;
	SpinBox *scatter_spinbox = nullptr;
	void _on_random_tile_checkbox_toggled(bool p_pressed);
	void _on_scattering_spinbox_changed(double p_value);

	void _update_toolbar();
	void _update_transform_buttons();
	void _set_transform_buttons_state(const Vector<Button *> &p_enabled_buttons, const Vector<Button *> &p_disabled_buttons, const String &p_why_disabled);
	void _update_translation();

	///// Tilemap editing. /////
	bool has_mouse = false;
	void _mouse_exited_viewport();

	enum DragType {
		DRAG_TYPE_NONE = 0,
		DRAG_TYPE_SELECT,
		DRAG_TYPE_MOVE,
		DRAG_TYPE_PAINT,
		DRAG_TYPE_LINE,
		DRAG_TYPE_RECT,
		DRAG_TYPE_BUCKET,
		DRAG_TYPE_PICK,
		DRAG_TYPE_CLIPBOARD_PASTE,
	};
	DragType drag_type = DRAG_TYPE_NONE;
	bool drag_erasing = false;
	Vector2 drag_start_mouse_pos;
	Vector2 drag_last_mouse_pos;
	HashMap<Vector2i, TileMapCell> drag_modified;

	TileMapCell _pick_random_tile(Ref<TileMapPattern> p_pattern);
	HashMap<Vector2i, TileMapCell> _draw_line(Vector2 p_start_drag_mouse_pos, Vector2 p_from_mouse_pos, Vector2 p_to_mouse_pos, bool p_erase);
	HashMap<Vector2i, TileMapCell> _draw_rect(Vector2i p_start_cell, Vector2i p_end_cell, bool p_erase);
	HashMap<Vector2i, TileMapCell> _draw_bucket_fill(Vector2i p_coords, bool p_contiguous, bool p_erase);
	void _stop_dragging();

	void _apply_transform(TileTransformType p_type);
	int _get_transformed_alternative(int p_alternative_id, TileTransformType p_transform);

	///// Selection system. /////
	RBSet<Vector2i> tile_map_selection;
	Ref<TileMapPattern> tile_map_clipboard;
	Ref<TileMapPattern> selection_pattern;
	Ref<TileMapPattern> erase_pattern;
	void _set_tile_map_selection(const TypedArray<Vector2i> &p_selection);
	TypedArray<Vector2i> _get_tile_map_selection() const;

	RBSet<TileMapCell> tile_set_selection;

	RandomPCG pattern_rng;
	uint32_t rng_base_state = 0;

	void _update_selection_pattern_from_tilemap_selection();
	void _update_selection_pattern_from_tileset_tiles_selection();
	void _update_selection_pattern_from_tileset_pattern_selection();
	void _update_tileset_selection_from_selection_pattern();
	void _update_fix_selected_and_hovered();
	void _fix_invalid_tiles_in_tile_map_selection();

	void patterns_item_list_empty_clicked(const Vector2 &p_pos, MouseButton p_mouse_button_index);

	///// Bottom panel common ////
	BoxContainer *split_container_left_side = nullptr;

	void _tab_changed();

	///// Bottom panel tiles ////
	VBoxContainer *tiles_bottom_panel = nullptr;
	Label *missing_source_label = nullptr;
	Label *invalid_source_label = nullptr;

	TileSetSourceItemList *sources_list = nullptr;
	MenuButton *source_sort_button = nullptr;

	Ref<Texture2D> missing_atlas_texture_icon;
	void _update_tile_set_sources_list();

	void _update_source_display();

	// Atlas sources.
	TileMapCell hovered_tile;
	TileAtlasView *tile_atlas_view = nullptr;
	SplitContainer *atlas_sources_split_container = nullptr;

	bool tile_set_dragging_selection = false;
	Vector2i tile_set_drag_start_mouse_pos;

	Control *tile_atlas_control = nullptr;
	void _tile_atlas_control_mouse_exited();
	void _tile_atlas_control_gui_input(const Ref<InputEvent> &p_event);
	void _tile_atlas_control_draw();

	Control *alternative_tiles_control = nullptr;
	void _tile_alternatives_control_draw();
	void _tile_alternatives_control_mouse_exited();
	void _tile_alternatives_control_gui_input(const Ref<InputEvent> &p_event);

	void _update_atlas_view();
	void _set_source_sort(int p_sort);

	// Scenes collection sources.
	ItemList *scene_tiles_list = nullptr;

	//void _update_scenes_collection_view();
	void _scene_thumbnail_done(const String &p_path, const Ref<Texture2D> &p_preview, const Ref<Texture2D> &p_small_preview, int p_index);
	//void _scenes_list_multi_selected(int p_index, bool p_selected);
	//void _scenes_list_lmb_empty_clicked(const Vector2 &p_pos, MouseButton p_mouse_button_index);

	///// Bottom panel patterns ////
	MarginContainer *patterns_mc = nullptr;
	ItemList *patterns_item_list = nullptr;
	Label *patterns_help_label = nullptr;
	void _patterns_item_list_gui_input(const Ref<InputEvent> &p_event);
	void _pattern_preview_done(Ref<TileMapPattern> p_pattern, Ref<Texture2D> p_texture);
	bool select_last_pattern = false;
	void _update_patterns_list();

	// General
	void _update_theme();
	List<BaseButton *> viewport_shortcut_buttons;

	// Update callback
	virtual void tile_set_changed() override;

protected:
	static void _bind_methods();

public:
	virtual Vector<TabData> get_tabs() const override;
	virtual bool forward_canvas_gui_input(const Ref<InputEvent> &p_event) override;
	virtual void forward_canvas_draw_over_viewport(Control *p_overlay) override;
	virtual void update_layout(EditorDock::DockLayout p_layout) override;
	virtual void update_z_level(int16_t p_z) override;

	virtual void edit(ObjectID p_tile_map_layer_id) override;

	TileMapLayer25DEditorTilesPlugin();
};

class TileMapLayer25DEditor : public EditorDock {
	GDCLASS(TileMapLayer25DEditor, EditorDock);

private:
	bool tile_map_layer_changed_needs_update = false;

	ObjectID edited_tile_map_layer_id;
  int16_t edited_z;
	bool is_multi_node_edit = false;
	Vector<TileMapLayer25D *> tile_map_layers_in_scene_cache;
	bool layers_in_scene_list_cache_needs_update = false;
	TileMapLayer25D *_get_edited_layer() const;
	void _find_tile_map_layers_in_scene(Node *p_current, const Node *p_owner, Vector<TileMapLayer25D *> &r_list) const;
	void _update_tile_map_layers_in_scene_list_cache();
	void _node_change(Node *p_node);

	Control *custom_overlay = nullptr;
	void _draw_overlay();

	// Vector to keep plugins.
	Vector<TileMapLayer25DSubEditorPlugin *> tile_map_editor_plugins;

	// Toolbar.
	GridContainer *main_box_container = nullptr;
	VBoxContainer *tile_map_wide_toolbar = nullptr;
	FlowContainer *tile_map_toolbar = nullptr;
	Control *padding_control = nullptr;
	SwitchSeparator *layer_selector_separator = nullptr;

	BoxContainer *layer_selection_hbox = nullptr;
	SpinBox *z_level_spinner = nullptr;
  void _z_level_changed(float p_value);
	void _z_level_mouse_exited();
  Button *toggle_highlight_selected_z_level_button = nullptr;
  void _clear_all_z_levels_highlighting();
  void _update_all_z_levels_highlighting();
  void _highlight_selected_z_level_button_toggled(bool p_pressed);

	Button *select_previous_layer = nullptr;
	void _select_previous_layer_pressed();
	Button *select_next_layer = nullptr;
	void _select_next_layer_pressed();
	Button *select_all_layers = nullptr;
	void _select_all_layers_pressed();

	Button *toggle_highlight_selected_layer_button = nullptr;
	void _clear_all_layers_highlighting();
	void _update_all_layers_highlighting();
	void _highlight_selected_layer_button_toggled(bool p_pressed);
	void _update_layer_selector_layout(bool p_is_vertical);

	Button *toggle_grid_button = nullptr;
	void _on_grid_toggled(bool p_pressed);

	enum {
		ADVANCED_MENU_REPLACE_WITH_PROXIES
	};
	MenuButton *advanced_menu_button = nullptr;
	void _advanced_menu_button_id_pressed(int p_id);

	// Bottom panel.
	Label *cant_edit_label = nullptr;
	TabBar *tabs_bar = nullptr;
	PanelContainer *tabs_panel = nullptr;
	LocalVector<TileMapLayer25DSubEditorPlugin::TabData> tabs_data;
	LocalVector<TileMapLayer25DSubEditorPlugin *> tabs_plugins;
	void _update_bottom_panel();

	// TileMap.
	Ref<Texture2D> missing_tile_texture;
	Ref<Texture2D> warning_pattern_texture;

	// CallBack.
	void _tile_map_layer_changed();
	void _tab_changed(int p_tab_changed);

	// Updates.
	void _layers_select_next_or_previous(bool p_next);

protected:
	void _notification(int p_what);
	void _draw_shape(Control *p_control, Rect2 p_region, TileSet::TileShape p_shape, TileSet::TileOffsetAxis p_offset_axis, Color p_color);
	virtual void update_layout(DockLayout p_layout) override;

public:
	bool forward_canvas_gui_input(const Ref<InputEvent> &p_event);
	void forward_canvas_draw_over_viewport(Control *p_overlay);

	void edit(Object *p_tile_map_layer);

	void set_z_level(int16_t p_z);

	TileMapLayer25DEditor();
	~TileMapLayer25DEditor();

	// Static functions.
	static Vector<Vector2i> get_line(const TileMapLayer25D *p_tile_map_layer, Vector2i p_from_cell, Vector2i p_to_cell);
};

VARIANT_ENUM_CAST(TileMapLayer25DEditorTilesPlugin::TileTransformType);
