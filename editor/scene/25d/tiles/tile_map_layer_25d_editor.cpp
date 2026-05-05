#include "tile_map_layer_25d_editor.h"

#include "core/input/input.h"
#include "core/math/geometry_2d.h"
#include "core/math/random_pcg.h"
#include "core/object/callable_mp.h"
#include "core/object/class_db.h"
#include "core/os/keyboard.h"
#include "core/os/os.h"
#include "editor/editor_node.h"
#include "editor/editor_undo_redo_manager.h"
#include "editor/inspector/editor_resource_preview.h"
#include "editor/inspector/multi_node_edit.h"
#include "editor/scene/2d/tiles/tiles_editor_plugin.h"
#include "editor/scene/canvas_item_editor_plugin.h"
#include "editor/settings/editor_command_palette.h"
#include "editor/settings/editor_settings.h"
#include "editor/themes/editor_scale.h"
#include "scene/25d/tile_map_layer_25d.h"
#include "scene/gui/grid_container.h"
#include "scene/gui/split_container.h"
#include "scene/main/scene_tree.h"

TileMapLayer25D *TileMapLayer25DSubEditorPlugin::_get_edited_layer() const {
	return ObjectDB::get_instance<TileMapLayer25D>(edited_tile_map_layer_id);
}

void TileMapLayer25DSubEditorPlugin::_add_to_output_if_tile_changed(HashMap<Vector2i, TileMapCell> &p_output, const TileMapLayer25D *p_layer, int16_t p_z, Vector2i p_coords, const TileMapCell &p_cell) {
	if (p_cell != p_layer->get_cell(p_coords)) {
		p_output[p_coords] = p_cell;
	}
}

void TileMapLayer25DSubEditorPlugin::draw_tile_coords_over_viewport(Control *p_overlay, const TileMapLayer25D *p_edited_layer, Ref<TileSet> p_tile_set, bool p_show_rectangle_size, const Vector2i &p_rectangle_origin) {
	Point2 z_msgpos = Point2(20 * EDSCALE, p_overlay->get_size().y - 60 * EDSCALE);
	String z_text = vformat(" %s %d", TTR("Z: "), edited_z);
	
	Point2 msgpos = Point2(20 * EDSCALE, p_overlay->get_size().y - 20 * EDSCALE);
	String text = String(p_edited_layer->get_coords_from_mouse_position(edited_z));

	if (p_show_rectangle_size) {
		Vector2i rect_size = p_edited_layer->local_viewport_to_map(p_edited_layer->get_local_mouse_position(), edited_z) - p_edited_layer->local_viewport_to_map(p_rectangle_origin, edited_z);
		text += vformat(" %s (%dx%d)", TTR("Drawing Rect:"), Math::abs(rect_size.x) + 1, Math::abs(rect_size.y) + 1);
	}

	Ref<Font> font = p_overlay->get_theme_font(SceneStringName(font), SNAME("Label"));
	int font_size = p_overlay->get_theme_font_size(SceneStringName(font_size), SNAME("Label"));

	p_overlay->draw_string(font, z_msgpos + Point2(1, 1), z_text, HORIZONTAL_ALIGNMENT_LEFT, -1, font_size, Color(0, 0, 0, 0.8));
	p_overlay->draw_string(font, msgpos + Point2(1, 1), text, HORIZONTAL_ALIGNMENT_LEFT, -1, font_size, Color(0, 0, 0, 0.8));
	p_overlay->draw_string(font, msgpos + Point2(-1, -1), text, HORIZONTAL_ALIGNMENT_LEFT, -1, font_size, Color(0, 0, 0, 0.8));
	p_overlay->draw_string(font, msgpos, text, HORIZONTAL_ALIGNMENT_LEFT, -1, font_size, Color(1, 1, 1, 1));
}

void TileMapLayer25DEditorTilesPlugin::tile_set_changed() {
	_update_fix_selected_and_hovered();
	_update_tile_set_sources_list();
	_update_source_display();
	_update_patterns_list();
}

void TileMapLayer25DEditorTilesPlugin::_on_random_tile_checkbox_toggled(bool p_pressed) {
	scatter_controls_container->set_visible(p_pressed);
}

void TileMapLayer25DEditorTilesPlugin::_on_scattering_spinbox_changed(double p_value) {
	scattering = p_value;
}

void TileMapLayer25DEditorTilesPlugin::_update_toolbar() {
	// Stop dragging if needed.
	_stop_dragging();

	// Show only the correct settings.
	bool using_select = (tool_buttons_group->get_pressed_button() == select_tool_button);
	tools_settings_vsep->set_visible(!using_select);
	picker_button->set_visible(!using_select);
	erase_button->set_visible(!using_select);
	random_tile_toggle->set_visible(!using_select);
	bucket_contiguous_checkbox->set_visible(!using_select && tool_buttons_group->get_pressed_button() == bucket_tool_button);
	scatter_controls_container->set_visible(!using_select && random_tile_toggle->is_pressed());
	CanvasItemEditor::get_singleton()->set_current_tool(CanvasItemEditor::TOOL_SELECT);
}

void TileMapLayer25DEditorTilesPlugin::_update_transform_buttons() {
	TileMapLayer25D *edited_layer = _get_edited_layer();
	if (!edited_layer) {
		return;
	}

	Ref<TileSet> tile_set = edited_layer->get_tile_set();
	if (tile_set.is_null() || selection_pattern.is_null()) {
		return;
	}

	if (tile_set->get_tile_shape() != TileSet::TILE_SHAPE_SQUARE && selection_pattern->get_size() != Vector2i(1, 1)) {
		_set_transform_buttons_state({ transform_button_flip_h, transform_button_flip_v }, { transform_button_rotate_left, transform_button_rotate_right },
				TTRC("Can't rotate patterns when using non-square tile grid."));
	} else {
		_set_transform_buttons_state({ transform_button_rotate_left, transform_button_rotate_right, transform_button_flip_h, transform_button_flip_v }, {}, "");
	}
}

void TileMapLayer25DEditorTilesPlugin::_set_transform_buttons_state(const Vector<Button *> &p_enabled_buttons, const Vector<Button *> &p_disabled_buttons, const String &p_why_disabled) {
	for (Button *button : p_enabled_buttons) {
		button->set_disabled(false);
		button->set_tooltip_text("");
	}
	for (Button *button : p_disabled_buttons) {
		button->set_disabled(true);
		button->set_tooltip_text(p_why_disabled);
	}
}

void TileMapLayer25DEditorTilesPlugin::_update_translation() {
	paint_tool_button->set_tooltip_text(TTR("Shift: Draw line.") + "\n" + vformat(TTR("%s+Shift: Draw rectangle."), keycode_get_string((Key)KeyModifierMask::CMD_OR_CTRL)));

	if (scene_tiles_list->is_visible_in_tree()) {
		//_update_scenes_collection_view();
		_update_tile_set_sources_list();
	}
	_update_patterns_list();
}

Vector<TileMapLayer25DSubEditorPlugin::TabData> TileMapLayer25DEditorTilesPlugin::get_tabs() const {
	Vector<TileMapLayer25DSubEditorPlugin::TabData> tabs;
	Vector<Control *> toolbar_controls;
	toolbar_controls.push_back(tilemap_tiles_tools_buttons);
	toolbar_controls.push_back(tools_settings);
	toolbar_controls.push_back(tools_settings_vsep);
	tabs.push_back({ toolbar_controls, wide_toolbar, tiles_bottom_panel });
	tabs.push_back({ toolbar_controls, wide_toolbar, patterns_mc });
	return tabs;
}

void TileMapLayer25DEditorTilesPlugin::_tab_changed() {
	if (tiles_bottom_panel->is_visible_in_tree()) {
		_update_selection_pattern_from_tileset_tiles_selection();
	} else if (patterns_mc->is_visible_in_tree()) {
		_update_selection_pattern_from_tileset_pattern_selection();
	}
}

void TileMapLayer25DEditorTilesPlugin::_update_tile_set_sources_list() {
	// Update the sources.
	int old_current = sources_list->get_current();
	int old_source = -1;
	if (old_current > -1) {
		old_source = sources_list->get_item_metadata(old_current);
	} else {
		old_source = sources_list->get_meta("old_source", -1);
	}
	sources_list->set_meta("old_source", old_source);
	sources_list->clear();
	sources_list->tile_set = Ref<TileSet>();

	TileMapLayer25D *edited_layer = _get_edited_layer();
	if (!edited_layer) {
		return;
	}

	Ref<TileSet> tile_set = edited_layer->get_tile_set();
	if (tile_set.is_null()) {
		return;
	}
	sources_list->tile_set = tile_set;

	if (!tile_set->has_source(old_source)) {
		old_source = -1;
	}

	List<int> source_ids = TilesEditorUtils::get_singleton()->get_sorted_sources(tile_set);
	for (const int &source_id : source_ids) {
		TileSetSource *source = *tile_set->get_source(source_id);

		Ref<Texture2D> texture;
		String item_text;

		// Common to all type of sources.
		if (!source->get_name().is_empty()) {
			item_text = source->get_name();
		}

		// Atlas source.
		TileSetAtlasSource *atlas_source = Object::cast_to<TileSetAtlasSource>(source);
		if (atlas_source) {
			texture = atlas_source->get_texture();
			if (item_text.is_empty()) {
				if (texture.is_valid()) {
					item_text = texture->get_path().get_file();
				} else {
					item_text = vformat(TTR("No Texture Atlas Source (ID: %d)"), source_id);
				}
			}
		}

		// Scene collection source.
		TileSetScenesCollectionSource *scene_collection_source = Object::cast_to<TileSetScenesCollectionSource>(source);
		if (scene_collection_source) {
			texture = tiles_bottom_panel->get_editor_theme_icon(SNAME("PackedScene"));
			if (item_text.is_empty()) {
				if (scene_collection_source->get_scene_tiles_count() > 0) {
					item_text = vformat(TTR("Scene Collection Source (ID: %d)"), source_id);
				} else {
					item_text = vformat(TTR("Empty Scene Collection Source (ID: %d)"), source_id);
				}
			}
		}

		// Use default if not valid.
		if (item_text.is_empty()) {
			item_text = vformat(TTR("Unknown Type Source (ID: %d)"), source_id);
		}
		if (texture.is_null()) {
			texture = missing_atlas_texture_icon;
		}

		sources_list->add_item(item_text, texture);
		sources_list->set_item_metadata(-1, source_id);
	}

	if (sources_list->get_item_count() > 0) {
		if (old_source >= 0) {
			for (int i = 0; i < sources_list->get_item_count(); i++) {
				if ((int)sources_list->get_item_metadata(i) == old_source) {
					sources_list->set_current(i);
					sources_list->ensure_current_is_visible();
					break;
				}
			}
		} else {
			sources_list->set_current(0);
		}
		sources_list->emit_signal(SceneStringName(item_selected), sources_list->get_current());
	}

	// Synchronize the lists.
	TilesEditorUtils::get_singleton()->set_sources_lists_current(sources_list->get_current());
}

void TileMapLayer25DEditorTilesPlugin::_update_source_display() {
	// Update the atlas display.
	TileMapLayer25D *edited_layer = _get_edited_layer();
	if (!edited_layer) {
		return;
	}

	Ref<TileSet> tile_set = edited_layer->get_tile_set();
	if (tile_set.is_null()) {
		return;
	}

	int source_index = sources_list->get_current();
	if (source_index >= 0 && source_index < sources_list->get_item_count()) {
		atlas_sources_split_container->show();
		missing_source_label->hide();

		int source_id = sources_list->get_item_metadata(source_index);
		TileSetSource *source = *tile_set->get_source(source_id);
		TileSetAtlasSource *atlas_source = Object::cast_to<TileSetAtlasSource>(source);
		TileSetScenesCollectionSource *scenes_collection_source = Object::cast_to<TileSetScenesCollectionSource>(source);

		if (atlas_source) {
			scene_tiles_list->hide();
			invalid_source_label->hide();
			tile_atlas_view->show();
			_update_atlas_view();
		} else if (scenes_collection_source) {
			tile_atlas_view->hide();
			invalid_source_label->hide();
			scene_tiles_list->show();
			//_update_scenes_collection_view();
		} else {
			tile_atlas_view->hide();
			scene_tiles_list->hide();
			invalid_source_label->show();
		}
	} else {
		atlas_sources_split_container->hide();
		missing_source_label->show();

		tile_atlas_view->hide();
		scene_tiles_list->hide();
		invalid_source_label->hide();
	}
}

void TileMapLayer25DEditorTilesPlugin::_patterns_item_list_gui_input(const Ref<InputEvent> &p_event) {
	TileMapLayer25D *edited_layer = _get_edited_layer();
	if (!edited_layer) {
		return;
	}

	Ref<TileSet> tile_set = edited_layer->get_tile_set();
	if (tile_set.is_null()) {
		return;
	}

	if (ED_IS_SHORTCUT("tiles_editor/paste", p_event) && p_event->is_pressed() && !p_event->is_echo()) {
		select_last_pattern = true;
		int new_pattern_index = tile_set->get_patterns_count();
		EditorUndoRedoManager *undo_redo = EditorUndoRedoManager::get_singleton();
		undo_redo->create_action(TTR("Add TileSet pattern"));
		undo_redo->add_do_method(*tile_set, "add_pattern", tile_map_clipboard, new_pattern_index);
		undo_redo->add_undo_method(*tile_set, "remove_pattern", new_pattern_index);
		undo_redo->commit_action();
		patterns_item_list->accept_event();
	}

	if (ED_IS_SHORTCUT("tiles_editor/delete", p_event) && p_event->is_pressed() && !p_event->is_echo()) {
		Vector<int> selected = patterns_item_list->get_selected_items();
		EditorUndoRedoManager *undo_redo = EditorUndoRedoManager::get_singleton();
		undo_redo->create_action(TTR("Remove TileSet patterns"));
		for (int i = 0; i < selected.size(); i++) {
			int pattern_index = selected[i];
			undo_redo->add_do_method(*tile_set, "remove_pattern", pattern_index);
			undo_redo->add_undo_method(*tile_set, "add_pattern", tile_set->get_pattern(pattern_index), pattern_index);
		}
		undo_redo->commit_action();
		patterns_item_list->accept_event();
	}
}

void TileMapLayer25DEditorTilesPlugin::_pattern_preview_done(Ref<TileMapPattern> p_pattern, Ref<Texture2D> p_texture) {
	// TODO optimize ?
	for (int i = 0; i < patterns_item_list->get_item_count(); i++) {
		if (patterns_item_list->get_item_metadata(i) == p_pattern) {
			patterns_item_list->set_item_icon(i, p_texture);
			break;
		}
	}
}

void TileMapLayer25DEditorTilesPlugin::_update_patterns_list() {
	TileMapLayer25D *edited_layer = _get_edited_layer();
	if (!edited_layer) {
		return;
	}

	Ref<TileSet> tile_set = edited_layer->get_tile_set();
	if (tile_set.is_null()) {
		return;
	}

	// Recreate the items.
	patterns_item_list->clear();
	for (int i = 0; i < tile_set->get_patterns_count(); i++) {
		int id = patterns_item_list->add_item("");
		patterns_item_list->set_item_metadata(id, tile_set->get_pattern(i));
		patterns_item_list->set_item_tooltip(id, vformat(TTR("Index: %d"), i));
		TilesEditorUtils::get_singleton()->queue_pattern_preview(tile_set, tile_set->get_pattern(i), callable_mp(this, &TileMapLayer25DEditorTilesPlugin::_pattern_preview_done));
	}

	// Update the label visibility.
	patterns_help_label->set_visible(patterns_item_list->get_item_count() == 0);

	// Added a new pattern, thus select the last one.
	if (select_last_pattern) {
		patterns_item_list->select(tile_set->get_patterns_count() - 1);
		patterns_item_list->grab_focus(true);
		_update_selection_pattern_from_tileset_pattern_selection();
	}
	select_last_pattern = false;
}

void TileMapLayer25DEditorTilesPlugin::_update_atlas_view() {
	TileMapLayer25D *edited_layer = _get_edited_layer();
	if (!edited_layer) {
		return;
	}

	Ref<TileSet> tile_set = edited_layer->get_tile_set();
	if (tile_set.is_null()) {
		return;
	}

	int source_id = sources_list->get_item_metadata(sources_list->get_current());
	TileSetSource *source = *tile_set->get_source(source_id);
	TileSetAtlasSource *atlas_source = Object::cast_to<TileSetAtlasSource>(source);
	ERR_FAIL_NULL(atlas_source);

	tile_atlas_view->set_atlas_source(*tile_set, atlas_source, source_id);
	TilesEditorUtils::get_singleton()->synchronize_atlas_view(tile_atlas_view);
	tile_atlas_control->queue_redraw();
}

void TileMapLayer25DEditorTilesPlugin::_update_theme() {
	source_sort_button->set_button_icon(tiles_bottom_panel->get_editor_theme_icon(SNAME("Sort")));
	select_tool_button->set_button_icon(tiles_bottom_panel->get_editor_theme_icon(SNAME("ToolSelect")));
	paint_tool_button->set_button_icon(tiles_bottom_panel->get_editor_theme_icon(SNAME("Edit")));
	line_tool_button->set_button_icon(tiles_bottom_panel->get_editor_theme_icon(SNAME("Line")));
	rect_tool_button->set_button_icon(tiles_bottom_panel->get_editor_theme_icon(SNAME("Rectangle")));
	bucket_tool_button->set_button_icon(tiles_bottom_panel->get_editor_theme_icon(SNAME("Bucket")));

	picker_button->set_button_icon(tiles_bottom_panel->get_editor_theme_icon(SNAME("ColorPick")));
	erase_button->set_button_icon(tiles_bottom_panel->get_editor_theme_icon(SNAME("Eraser")));
	random_tile_toggle->set_button_icon(tiles_bottom_panel->get_editor_theme_icon(SNAME("RandomNumberGenerator")));

	transform_button_rotate_left->set_button_icon(tiles_bottom_panel->get_editor_theme_icon("RotateLeft"));
	transform_button_rotate_right->set_button_icon(tiles_bottom_panel->get_editor_theme_icon("RotateRight"));
	transform_button_flip_h->set_button_icon(tiles_bottom_panel->get_editor_theme_icon("MirrorX"));
	transform_button_flip_v->set_button_icon(tiles_bottom_panel->get_editor_theme_icon("MirrorY"));

	missing_atlas_texture_icon = tiles_bottom_panel->get_editor_theme_icon(SNAME("TileSet"));
	_update_tile_set_sources_list();
}

bool TileMapLayer25DEditorTilesPlugin::forward_canvas_gui_input(const Ref<InputEvent> &p_event) {
	if (!(tiles_bottom_panel->is_visible_in_tree() || patterns_mc->is_visible_in_tree())) {
		// If the bottom editor is not visible, we ignore inputs.
		return false;
	}

	if (CanvasItemEditor::get_singleton()->get_current_tool() != CanvasItemEditor::TOOL_SELECT) {
		_stop_dragging();
		return false;
	}

	TileMapLayer25D *edited_layer = _get_edited_layer();
	if (!edited_layer) {
		return false;
	}

	Ref<TileSet> tile_set = edited_layer->get_tile_set();
	if (tile_set.is_null()) {
		return false;
	}

	// Shortcuts
	if (ED_IS_SHORTCUT("tiles_editor/cut", p_event) || ED_IS_SHORTCUT("tiles_editor/copy", p_event)) {
		// Fill in the clipboard.
		if (!tile_map_selection.is_empty()) {
			tile_map_clipboard.instantiate();
			TypedArray<Vector2i> coords_array;
			for (const Vector2i &E : tile_map_selection) {
				coords_array.push_back(E);
			}
			tile_map_clipboard = edited_layer->get_pattern(coords_array);
		}

		if (ED_IS_SHORTCUT("tiles_editor/cut", p_event)) {
			// Delete selected tiles.
			if (!tile_map_selection.is_empty()) {
				EditorUndoRedoManager *undo_redo = EditorUndoRedoManager::get_singleton();
				undo_redo->create_action(TTR("Delete tiles"));
				for (const Vector2i &coords : tile_map_selection) {
					undo_redo->add_do_method(edited_layer, "set_cell", coords, TileSet::INVALID_SOURCE, TileSetSource::INVALID_ATLAS_COORDS, TileSetSource::INVALID_TILE_ALTERNATIVE);
					undo_redo->add_undo_method(edited_layer, "set_cell", coords, edited_layer->get_cell_source_id(coords), edited_layer->get_cell_atlas_coords(coords), edited_layer->get_cell_alternative_tile(coords));
				}
				undo_redo->add_undo_method(this, "_set_tile_map_selection", _get_tile_map_selection());
				tile_map_selection.clear();
				undo_redo->add_do_method(this, "_set_tile_map_selection", _get_tile_map_selection());
				undo_redo->commit_action();
			}
		}

		return true;
	}
	if (ED_IS_SHORTCUT("tiles_editor/paste", p_event)) {
		if (drag_type == DRAG_TYPE_NONE) {
			drag_type = DRAG_TYPE_CLIPBOARD_PASTE;
		}
		CanvasItemEditor::get_singleton()->update_viewport();
		return true;
	}
	if (ED_IS_SHORTCUT("tiles_editor/cancel", p_event)) {
		if (drag_type == DRAG_TYPE_CLIPBOARD_PASTE) {
			drag_type = DRAG_TYPE_NONE;
			CanvasItemEditor::get_singleton()->update_viewport();
			return true;
		}
	}
	if (ED_IS_SHORTCUT("tiles_editor/delete", p_event)) {
		// Delete selected tiles.
		if (!tile_map_selection.is_empty()) {
			EditorUndoRedoManager *undo_redo = EditorUndoRedoManager::get_singleton();
			undo_redo->create_action(TTR("Delete tiles"));
			for (const Vector2i &coords : tile_map_selection) {
				undo_redo->add_do_method(edited_layer, "set_cell", coords, edited_z, TileSet::INVALID_SOURCE, TileSetSource::INVALID_ATLAS_COORDS, TileSetSource::INVALID_TILE_ALTERNATIVE);
				undo_redo->add_undo_method(edited_layer, "set_cell", coords, edited_z, edited_layer->get_cell_source_id(coords), edited_layer->get_cell_atlas_coords(coords), edited_layer->get_cell_alternative_tile(coords));
			}
			undo_redo->add_undo_method(this, "_set_tile_map_selection", _get_tile_map_selection());
			tile_map_selection.clear();
			undo_redo->add_do_method(this, "_set_tile_map_selection", _get_tile_map_selection());
			undo_redo->commit_action();
		}
		return true;
	}

	Ref<InputEventKey> k = p_event;
	if (k.is_valid() && k->is_pressed() && !k->is_echo()) {
		for (BaseButton *b : viewport_shortcut_buttons) {
			if (b->is_disabled()) {
				continue;
			}

			if (b->get_shortcut().is_valid() && b->get_shortcut()->matches_event(p_event)) {
				if (b->is_toggle_mode()) {
					b->set_pressed(b->get_button_group().is_valid() || !b->is_pressed());
				} else {
					// Can't press a button without toggle mode, so just emit the signal directly.
					b->emit_signal(SceneStringName(pressed));
				}
				return true;
			}
		}
	}

	Ref<InputEventMouseMotion> mm = p_event;
	if (mm.is_valid()) {
		has_mouse = true;
		Transform2D xform = CanvasItemEditor::get_singleton()->get_canvas_transform() * edited_layer->get_global_transform_with_canvas();
		Vector2 mpos = xform.affine_inverse().xform(mm->get_position());

		if (edited_layer->local_viewport_to_map(drag_last_mouse_pos, edited_z) != edited_layer->local_viewport_to_map(mpos, edited_z)) {
			pattern_rng.seed(Math::rand());
			rng_base_state = Math::rand();
		}

		switch (drag_type) {
			case DRAG_TYPE_PAINT: {
				HashMap<Vector2i, TileMapCell> to_draw = _draw_line(drag_start_mouse_pos, drag_last_mouse_pos, mpos, drag_erasing);
				for (const KeyValue<Vector2i, TileMapCell> &E : to_draw) {
					Vector2i coords = E.key;
					if (!drag_modified.has(coords)) {
						drag_modified.insert(coords, edited_layer->get_cell(coords));
						if (!drag_erasing && E.value.source_id == TileSet::INVALID_SOURCE) {
							continue;
						}
						edited_layer->set_cell(coords, edited_z, E.value.source_id, E.value.get_atlas_coords(), E.value.alternative_tile);
					}
				}
				_fix_invalid_tiles_in_tile_map_selection();
			} break;
			case DRAG_TYPE_BUCKET: {
				Vector<Vector2i> line = TileMapLayer25DEditor::get_line(edited_layer, edited_layer->local_viewport_to_map(drag_last_mouse_pos, edited_z), edited_layer->local_viewport_to_map(mpos, edited_z));
				for (int i = 0; i < line.size(); i++) {
					if (!drag_modified.has(line[i])) {
						HashMap<Vector2i, TileMapCell> to_draw = _draw_bucket_fill(line[i], bucket_contiguous_checkbox->is_pressed(), drag_erasing);
						for (const KeyValue<Vector2i, TileMapCell> &E : to_draw) {
							Vector2i coords = E.key;
							if (!drag_modified.has(coords)) {
								drag_modified.insert(coords, edited_layer->get_cell(coords));
								if (!drag_erasing && E.value.source_id == TileSet::INVALID_SOURCE) {
									continue;
								}
								edited_layer->set_cell(coords, edited_z, E.value.source_id, E.value.get_atlas_coords(), E.value.alternative_tile);
							}
						}
					}
				}
				_fix_invalid_tiles_in_tile_map_selection();
			} break;
			default:
				break;
		}
		drag_last_mouse_pos = mpos;
		CanvasItemEditor::get_singleton()->update_viewport();

		return true;
	}

	Ref<InputEventMouseButton> mb = p_event;
	if (mb.is_valid()) {
		has_mouse = true;
		Transform2D xform = CanvasItemEditor::get_singleton()->get_canvas_transform() * edited_layer->get_global_transform_with_canvas();
		Vector2 mpos = xform.affine_inverse().xform(mb->get_position());

		if (mb->get_button_index() == MouseButton::LEFT || mb->get_button_index() == MouseButton::RIGHT) {
			if (mb->is_pressed()) {
				// Pressed
				if (erase_button->is_pressed() || mb->get_button_index() == MouseButton::RIGHT) {
					drag_erasing = true;
				}

				if (drag_type == DRAG_TYPE_CLIPBOARD_PASTE) {
					// Cancel tile pasting on right-click
					if (mb->get_button_index() == MouseButton::RIGHT) {
						drag_type = DRAG_TYPE_NONE;
					}
				} else if (tool_buttons_group->get_pressed_button() == select_tool_button) {
					drag_start_mouse_pos = mpos;
					if (tile_map_selection.has(edited_layer->local_viewport_to_map(drag_start_mouse_pos, edited_z)) && !mb->is_shift_pressed() && !mb->is_command_or_control_pressed()) {
						// Move the selection
						_update_selection_pattern_from_tilemap_selection(); // Make sure the pattern is up to date before moving.
						drag_type = DRAG_TYPE_MOVE;
						drag_modified.clear();
						for (const Vector2i &E : tile_map_selection) {
							Vector2i coords = E;
							drag_modified.insert(coords, edited_layer->get_cell(coords));
							edited_layer->set_cell(coords, edited_z, TileSet::INVALID_SOURCE, TileSetSource::INVALID_ATLAS_COORDS, TileSetSource::INVALID_TILE_ALTERNATIVE);
						}
					} else {
						// Select tiles
						drag_type = DRAG_TYPE_SELECT;
					}
				} else {
					// Check if we are picking a tile.
					if (picker_button->is_pressed() || (Input::get_singleton()->is_key_pressed(Key::CMD_OR_CTRL) && !Input::get_singleton()->is_key_pressed(Key::SHIFT))) {
						drag_type = DRAG_TYPE_PICK;
						drag_start_mouse_pos = mpos;
					} else {
						// Paint otherwise.
						if (tool_buttons_group->get_pressed_button() == paint_tool_button && !Input::get_singleton()->is_key_pressed(Key::CMD_OR_CTRL) && !Input::get_singleton()->is_key_pressed(Key::SHIFT)) {
							drag_type = DRAG_TYPE_PAINT;
							drag_start_mouse_pos = mpos;
							drag_modified.clear();
							HashMap<Vector2i, TileMapCell> to_draw = _draw_line(drag_start_mouse_pos, mpos, mpos, drag_erasing);
							for (const KeyValue<Vector2i, TileMapCell> &E : to_draw) {
								if (!drag_erasing && E.value.source_id == TileSet::INVALID_SOURCE) {
									continue;
								}
								Vector2i coords = E.key;
								if (!drag_modified.has(coords)) {
									drag_modified.insert(coords, edited_layer->get_cell(coords));
								}
								edited_layer->set_cell(coords, edited_z, E.value.source_id, E.value.get_atlas_coords(), E.value.alternative_tile);
							}
							_fix_invalid_tiles_in_tile_map_selection();
						} else if (tool_buttons_group->get_pressed_button() == line_tool_button || (tool_buttons_group->get_pressed_button() == paint_tool_button && Input::get_singleton()->is_key_pressed(Key::SHIFT) && !Input::get_singleton()->is_key_pressed(Key::CMD_OR_CTRL))) {
							drag_type = DRAG_TYPE_LINE;
							drag_start_mouse_pos = mpos;
							drag_modified.clear();
						} else if (tool_buttons_group->get_pressed_button() == rect_tool_button || (tool_buttons_group->get_pressed_button() == paint_tool_button && Input::get_singleton()->is_key_pressed(Key::SHIFT) && Input::get_singleton()->is_key_pressed(Key::CMD_OR_CTRL))) {
							drag_type = DRAG_TYPE_RECT;
							drag_start_mouse_pos = mpos;
							drag_modified.clear();
						} else if (tool_buttons_group->get_pressed_button() == bucket_tool_button) {
							drag_type = DRAG_TYPE_BUCKET;
							drag_start_mouse_pos = mpos;
							drag_modified.clear();
							Vector<Vector2i> line = TileMapLayer25DEditor::get_line(edited_layer, edited_layer->local_viewport_to_map(drag_last_mouse_pos, edited_z), edited_layer->local_viewport_to_map(mpos, edited_z));
							for (int i = 0; i < line.size(); i++) {
								if (!drag_modified.has(line[i])) {
									HashMap<Vector2i, TileMapCell> to_draw = _draw_bucket_fill(line[i], bucket_contiguous_checkbox->is_pressed(), drag_erasing);
									for (const KeyValue<Vector2i, TileMapCell> &E : to_draw) {
										Vector2i coords = E.key;
										if (!drag_modified.has(coords)) {
											drag_modified.insert(coords, edited_layer->get_cell(coords));
											if (!drag_erasing && E.value.source_id == TileSet::INVALID_SOURCE) {
												continue;
											}
											edited_layer->set_cell(coords, edited_z, E.value.source_id, E.value.get_atlas_coords(), E.value.alternative_tile);
										}
									}
								}
							}
							_fix_invalid_tiles_in_tile_map_selection();
						}
					}
				}

			} else {
				// Released.
				if (drag_type == DRAG_TYPE_NONE) {
					drag_erasing = false;
					return false;
				} else {
					_stop_dragging();
				}
				drag_erasing = false;
			}

			CanvasItemEditor::get_singleton()->update_viewport();

			return true;
		}
		drag_last_mouse_pos = mpos;
	}

	return false;
}

void TileMapLayer25DEditorTilesPlugin::forward_canvas_draw_over_viewport(Control *p_overlay) {
	const TileMapLayer25D *edited_layer = _get_edited_layer();
	Ref<TileSet> tile_set = edited_layer->get_tile_set();

	Transform2D xform = CanvasItemEditor::get_singleton()->get_canvas_transform() * edited_layer->get_global_transform_with_canvas(); // Origin of the LAYER
	Vector2 mpos = edited_layer->get_local_mouse_position(); // Mouse position relative to layer z=0
	Vector2i tile_shape_size = tile_set->get_tile_size();
	bool drawing_rect = false;

	// Draw the selection.
	if ((tiles_bottom_panel->is_visible_in_tree() || patterns_mc->is_visible_in_tree()) && tool_buttons_group->get_pressed_button() == select_tool_button) {
		// In select mode, we only draw the current selection if we are modifying it (pressing control or shift).
		if (drag_type == DRAG_TYPE_MOVE || (drag_type == DRAG_TYPE_SELECT && !Input::get_singleton()->is_key_pressed(Key::CMD_OR_CTRL) && !Input::get_singleton()->is_key_pressed(Key::SHIFT))) {
			// Do nothing.
		} else {
			Color grid_color = EDITOR_GET("editors/tiles_editor/grid_color");
			Color selection_color = Color::from_hsv(Math::fposmod(grid_color.get_h() + 0.5, 1.0), grid_color.get_s(), grid_color.get_v(), 1.0);
			tile_set->draw_cells_outline(p_overlay, tile_map_selection, selection_color, xform);
		}
	}

	// Handle the preview of the tiles to be placed.
	if ((tiles_bottom_panel->is_visible_in_tree() || patterns_mc->is_visible_in_tree()) && CanvasItemEditor::get_singleton()->get_current_tool() == CanvasItemEditor::TOOL_SELECT && has_mouse) { // Only if the tilemap editor is opened and the viewport is hovered.
		HashMap<Vector2i, TileMapCell> preview;
		Rect2i drawn_grid_rect;

		if (drag_type == DRAG_TYPE_PICK) {
			// Draw the area being picked.
			Rect2i rect = Rect2i(edited_layer->local_viewport_to_map(drag_start_mouse_pos, edited_z), edited_layer->local_viewport_to_map(mpos, edited_z) - edited_layer->local_viewport_to_map(drag_start_mouse_pos, edited_z)).abs();
			rect.size += Vector2i(1, 1);
			for (int x = rect.position.x; x < rect.get_end().x; x++) {
				for (int y = rect.position.y; y < rect.get_end().y; y++) {
					Vector2i coords = Vector2i(x, y);
					if (edited_layer->get_cell_source_id(coords) != TileSet::INVALID_SOURCE) {
						Transform2D tile_xform(0, tile_shape_size, 0, edited_layer->map_to_local_viewport(coords, edited_z));
						tile_set->draw_tile_shape(p_overlay, xform * tile_xform, Color(1.0, 1.0, 1.0), false);
					}
				}
			}
		} else if (drag_type == DRAG_TYPE_SELECT) {
			// Draw the area being selected.
			Rect2i rect = Rect2i(edited_layer->local_viewport_to_map(drag_start_mouse_pos, edited_z), edited_layer->local_viewport_to_map(mpos, edited_z) - edited_layer->local_viewport_to_map(drag_start_mouse_pos, edited_z)).abs();
			rect.size += Vector2i(1, 1);
			RBSet<Vector2i> to_draw;
			for (int x = rect.position.x; x < rect.get_end().x; x++) {
				for (int y = rect.position.y; y < rect.get_end().y; y++) {
					Vector2i coords = Vector2i(x, y);
					if (edited_layer->get_cell_source_id(coords) != TileSet::INVALID_SOURCE) {
						to_draw.insert(coords);
					}
					Transform2D tile_xform(0, tile_shape_size, 0, edited_layer->map_to_local_viewport(coords, edited_z));
					tile_set->draw_tile_shape(p_overlay, xform * tile_xform, Color(1.0, 1.0, 1.0, 0.2), true);
				}
			}
			tile_set->draw_cells_outline(p_overlay, to_draw, Color(1.0, 1.0, 1.0), xform);
		} else if (drag_type == DRAG_TYPE_MOVE) {
			if (!(patterns_item_list->is_visible_in_tree() && patterns_item_list->has_point(patterns_item_list->get_local_mouse_position()))) {
				// Preview when moving.
				Vector2i top_left;
				if (!tile_map_selection.is_empty()) {
					top_left = tile_map_selection.front()->get();
				}
				for (const Vector2i &E : tile_map_selection) {
					top_left = top_left.min(E);
				}
				Vector2i offset = drag_start_mouse_pos - edited_layer->map_to_local_viewport(top_left, edited_z);
				offset = edited_layer->local_viewport_to_map(mpos - offset, edited_z) - edited_layer->local_viewport_to_map(drag_start_mouse_pos - offset, edited_z);

				TypedArray<Vector2i> selection_used_cells = selection_pattern->get_used_cells();
				for (int i = 0; i < selection_used_cells.size(); i++) {
					Vector2i coords = tile_set->map_pattern(offset + top_left, selection_used_cells[i], selection_pattern);
					preview[coords] = TileMapCell(selection_pattern->get_cell_source_id(selection_used_cells[i]), selection_pattern->get_cell_atlas_coords(selection_used_cells[i]), selection_pattern->get_cell_alternative_tile(selection_used_cells[i]));
				}
			}
		} else if (drag_type == DRAG_TYPE_CLIPBOARD_PASTE) {
			// Preview when pasting.
			Vector2 mouse_offset = (Vector2(tile_map_clipboard->get_size()) / 2.0 - Vector2(0.5, 0.5)) * tile_set->get_tile_size();
			TypedArray<Vector2i> clipboard_used_cells = tile_map_clipboard->get_used_cells();
			for (int i = 0; i < clipboard_used_cells.size(); i++) {
				Vector2i coords = tile_set->map_pattern(edited_layer->local_viewport_to_map(mpos - mouse_offset, edited_z), clipboard_used_cells[i], tile_map_clipboard);
				preview[coords] = TileMapCell(tile_map_clipboard->get_cell_source_id(clipboard_used_cells[i]), tile_map_clipboard->get_cell_atlas_coords(clipboard_used_cells[i]), tile_map_clipboard->get_cell_alternative_tile(clipboard_used_cells[i]));
			}
		} else if (!picker_button->is_pressed() && !(drag_type == DRAG_TYPE_NONE && Input::get_singleton()->is_key_pressed(Key::CMD_OR_CTRL) && !Input::get_singleton()->is_key_pressed(Key::SHIFT))) {
			bool expand_grid = false;
			if (tool_buttons_group->get_pressed_button() == paint_tool_button && drag_type == DRAG_TYPE_NONE) {
				// Preview for a single pattern.
				preview = _draw_line(mpos, mpos, mpos, erase_button->is_pressed());
				expand_grid = true;
			} else if (tool_buttons_group->get_pressed_button() == line_tool_button || drag_type == DRAG_TYPE_LINE) {
				if (drag_type == DRAG_TYPE_NONE) {
					// Preview for a single pattern.
					preview = _draw_line(mpos, mpos, mpos, erase_button->is_pressed());
					expand_grid = true;
				} else if (drag_type == DRAG_TYPE_LINE) {
					// Preview for a line pattern.
					preview = _draw_line(drag_start_mouse_pos, drag_start_mouse_pos, mpos, drag_erasing);
					expand_grid = true;
				}
			} else if (drag_type == DRAG_TYPE_RECT) {
				// Preview for a rect pattern.
				preview = _draw_rect(edited_layer->local_viewport_to_map(drag_start_mouse_pos, edited_z), edited_layer->local_viewport_to_map(mpos, edited_z), drag_erasing);
				drawing_rect = !preview.is_empty();
				expand_grid = true;
			} else if (tool_buttons_group->get_pressed_button() == bucket_tool_button && drag_type == DRAG_TYPE_NONE) {
				// Preview for a fill pattern.
				preview = _draw_bucket_fill(edited_layer->local_viewport_to_map(mpos, edited_z), bucket_contiguous_checkbox->is_pressed(), erase_button->is_pressed());
			}

			// Expand the grid if needed
			if (expand_grid && !preview.is_empty()) {
				drawn_grid_rect = Rect2i(preview.begin()->key, Vector2i(0, 0));
				for (const KeyValue<Vector2i, TileMapCell> &E : preview) {
					drawn_grid_rect.expand_to(E.key);
				}
				drawn_grid_rect.size += Vector2i(1, 1);
			}
		}

		if (!preview.is_empty()) {
			const int fading = 5;

			// Draw the lines of the grid behind the preview.
			bool display_grid = EDITOR_GET("editors/tiles_editor/display_grid");
			if (display_grid) {
				Color grid_color = EDITOR_GET("editors/tiles_editor/grid_color");
				Color grid_color_z0 = EDITOR_GET("editors/tiles_editor/grid_color_z0");
				Color grid_color_z0_fill = grid_color_z0.lightened(0.75f);

				Transform2D tile_z0_xform;
				tile_z0_xform.set_origin(edited_layer->map_to_local_viewport(edited_layer->local_viewport_to_map(mpos, edited_z), 0));
				tile_z0_xform.set_scale(tile_shape_size);
				if(edited_z > 0) {
					tile_set->draw_tile_shape(p_overlay, xform * tile_z0_xform, grid_color_z0_fill, true);
					tile_set->draw_tile_shape(p_overlay, xform * tile_z0_xform, grid_color_z0, false);
				}
				if (drawn_grid_rect.size.x > 0 && drawn_grid_rect.size.y > 0) {
					drawn_grid_rect = drawn_grid_rect.grow(fading);
					for (int x = drawn_grid_rect.position.x; x < (drawn_grid_rect.position.x + drawn_grid_rect.size.x); x++) {
						for (int y = drawn_grid_rect.position.y; y < (drawn_grid_rect.position.y + drawn_grid_rect.size.y); y++) {
							Vector2i pos_in_rect = Vector2i(x, y) - drawn_grid_rect.position;

							// Fade out the border of the grid.
							float left_opacity = CLAMP(Math::inverse_lerp(0.0f, (float)fading, (float)pos_in_rect.x), 0.0f, 1.0f);
							float right_opacity = CLAMP(Math::inverse_lerp((float)drawn_grid_rect.size.x, (float)(drawn_grid_rect.size.x - fading), (float)(pos_in_rect.x + 1)), 0.0f, 1.0f);
							float top_opacity = CLAMP(Math::inverse_lerp(0.0f, (float)fading, (float)pos_in_rect.y), 0.0f, 1.0f);
							float bottom_opacity = CLAMP(Math::inverse_lerp((float)drawn_grid_rect.size.y, (float)(drawn_grid_rect.size.y - fading), (float)(pos_in_rect.y + 1)), 0.0f, 1.0f);
							float opacity = CLAMP(MIN(left_opacity, MIN(right_opacity, MIN(top_opacity, bottom_opacity))) + 0.1, 0.0f, 1.0f);

							Transform2D tile_xform;
							tile_xform.set_origin(edited_layer->map_to_local_viewport(Vector2i(x, y), edited_z));
							tile_xform.set_scale(tile_shape_size);
							Color color = grid_color;
							color.a = color.a * opacity;
							tile_set->draw_tile_shape(p_overlay, xform * tile_xform, color, false);
						}
					}
				}
				if(edited_z < 0) {
					tile_set->draw_tile_shape(p_overlay, xform * tile_z0_xform, grid_color_z0_fill, true);
					tile_set->draw_tile_shape(p_overlay, xform * tile_z0_xform, grid_color_z0, false);
				}
			}

			// Draw the preview.
			for (const KeyValue<Vector2i, TileMapCell> &E : preview) {
				Transform2D tile_xform;
				tile_xform.set_origin(edited_layer->map_to_local_viewport(E.key, edited_z));
				tile_xform.set_scale(tile_set->get_tile_size());
				if (tile_set->has_source(E.value.source_id)) {
					TileSetSource *source = *tile_set->get_source(E.value.source_id);
					TileSetAtlasSource *atlas_source = Object::cast_to<TileSetAtlasSource>(source);
					if (atlas_source) {
						// Get tile data.
						TileData *tile_data = atlas_source->get_tile_data(E.value.get_atlas_coords(), E.value.alternative_tile);
						if (!tile_data) {
							continue;
						}

						Rect2i source_rect = atlas_source->get_tile_texture_region(E.value.get_atlas_coords());

						// Compute the destination rectangle in the CanvasItem.
						Rect2 dest_rect;
						bool transpose;
						TileMapLayer25D::compute_transformed_tile_dest_rect(dest_rect, transpose, edited_layer->map_to_local_viewport(E.key, edited_z), source_rect.size, tile_data, E.value.alternative_tile);

						// Get the tile modulation.
						Color modulate = tile_data->get_modulate() * edited_layer->get_modulate_in_tree() * edited_layer->get_self_modulate();

						// Draw the tile.
						p_overlay->draw_set_transform_matrix(xform);
						p_overlay->draw_texture_rect_region(atlas_source->get_texture(), dest_rect, source_rect, modulate * Color(1.0, 1.0, 1.0, 0.5), transpose, tile_set->is_uv_clipping());
						p_overlay->draw_set_transform_matrix(Transform2D());
					} else {
						tile_set->draw_tile_shape(p_overlay, xform * tile_xform, Color(1.0, 1.0, 1.0, 0.5), true);
					}
				} else {
					tile_set->draw_tile_shape(p_overlay, xform * tile_xform, Color(0.0, 0.0, 0.0, 0.5), true);
				}
			}
		}

		draw_tile_coords_over_viewport(p_overlay, edited_layer, tile_set, drawing_rect, drag_start_mouse_pos);
	}
}

void TileMapLayer25DEditorTilesPlugin::_mouse_exited_viewport() {
	has_mouse = false;
	CanvasItemEditor::get_singleton()->update_viewport();
}

TileMapCell TileMapLayer25DEditorTilesPlugin::_pick_random_tile(Ref<TileMapPattern> p_pattern) {
	TileMapLayer25D *edited_layer = _get_edited_layer();
	if (!edited_layer) {
		return TileMapCell();
	}

	Ref<TileSet> tile_set = edited_layer->get_tile_set();
	if (tile_set.is_null()) {
		return TileMapCell();
	}

	TypedArray<Vector2i> used_cells = p_pattern->get_used_cells();
	double sum = 0.0;
	for (int i = 0; i < used_cells.size(); i++) {
		int source_id = p_pattern->get_cell_source_id(used_cells[i]);
		Vector2i atlas_coords = p_pattern->get_cell_atlas_coords(used_cells[i]);
		int alternative_tile = p_pattern->get_cell_alternative_tile(used_cells[i]);

		TileSetSource *source = *tile_set->get_source(source_id);
		TileSetAtlasSource *atlas_source = Object::cast_to<TileSetAtlasSource>(source);
		if (atlas_source) {
			TileData *tile_data = atlas_source->get_tile_data(atlas_coords, alternative_tile);
			ERR_FAIL_NULL_V(tile_data, TileMapCell());
			sum += tile_data->get_probability();
		} else {
			sum += 1.0;
		}
	}

	double empty_probability = sum * scattering;
	double current = 0.0;
	double rand = pattern_rng.random(0.0, sum + empty_probability);
	for (int i = 0; i < used_cells.size(); i++) {
		int source_id = p_pattern->get_cell_source_id(used_cells[i]);
		Vector2i atlas_coords = p_pattern->get_cell_atlas_coords(used_cells[i]);
		int alternative_tile = p_pattern->get_cell_alternative_tile(used_cells[i]);

		TileSetSource *source = *tile_set->get_source(source_id);
		TileSetAtlasSource *atlas_source = Object::cast_to<TileSetAtlasSource>(source);
		if (atlas_source) {
			current += atlas_source->get_tile_data(atlas_coords, alternative_tile)->get_probability();
		} else {
			current += 1.0;
		}

		if (current >= rand) {
			return TileMapCell(source_id, atlas_coords, alternative_tile);
		}
	}
	return TileMapCell();
}

HashMap<Vector2i, TileMapCell> TileMapLayer25DEditorTilesPlugin::_draw_line(Vector2 p_start_drag_mouse_pos, Vector2 p_from_mouse_pos, Vector2 p_to_mouse_pos, bool p_erase) {
	TileMapLayer25D *edited_layer = _get_edited_layer();
	if (!edited_layer) {
		return HashMap<Vector2i, TileMapCell>();
	}

	Ref<TileSet> tile_set = edited_layer->get_tile_set();
	if (tile_set.is_null()) {
		return HashMap<Vector2i, TileMapCell>();
	}

	// Get or create the pattern.
	Ref<TileMapPattern> pattern = p_erase ? erase_pattern : selection_pattern;

	HashMap<Vector2i, TileMapCell> output;
	if (!pattern->is_empty()) {
		// Paint the tiles on the tile map.
		if (!p_erase && random_tile_toggle->is_pressed()) {
			pattern_rng.set_state(rng_base_state);
			// Paint a random tile.
			Vector<Vector2i> line = TileMapLayer25DEditor::get_line(edited_layer, edited_layer->local_viewport_to_map(p_from_mouse_pos, edited_z), edited_layer->local_viewport_to_map(p_to_mouse_pos, edited_z));
			for (int i = 0; i < line.size(); i++) {
				_add_to_output_if_tile_changed(output, edited_layer, edited_z, line[i], _pick_random_tile(pattern));
			}
		} else {
			// Paint the pattern.
			// If we paint several tiles, we virtually move the mouse as if it was in the center of the "brush"
			Vector2 mouse_offset = (Vector2(pattern->get_size()) / 2.0 - Vector2(0.5, 0.5)) * tile_set->get_tile_size();
			Vector2i last_hovered_cell = edited_layer->local_viewport_to_map(p_from_mouse_pos - mouse_offset, edited_z);
			Vector2i new_hovered_cell = edited_layer->local_viewport_to_map(p_to_mouse_pos - mouse_offset, edited_z);
			Vector2i drag_start_cell = edited_layer->local_viewport_to_map(p_start_drag_mouse_pos - mouse_offset, edited_z);

			TypedArray<Vector2i> used_cells = pattern->get_used_cells();
			Vector2i offset = Vector2i(Math::posmod(drag_start_cell.x, pattern->get_size().x), Math::posmod(drag_start_cell.y, pattern->get_size().y)); // Note: no posmodv for Vector2i for now. Meh.s
			Vector<Vector2i> line = TileMapLayer25DEditor::get_line(edited_layer, (last_hovered_cell - offset) / pattern->get_size(), (new_hovered_cell - offset) / pattern->get_size());
			for (int i = 0; i < line.size(); i++) {
				Vector2i top_left = line[i] * pattern->get_size() + offset;
				for (int j = 0; j < used_cells.size(); j++) {
					Vector2i coords = tile_set->map_pattern(top_left, used_cells[j], pattern);
					_add_to_output_if_tile_changed(output, edited_layer, edited_z, coords, TileMapCell(pattern->get_cell_source_id(used_cells[j]), pattern->get_cell_atlas_coords(used_cells[j]), pattern->get_cell_alternative_tile(used_cells[j])));
				}
			}
		}
	}
	return output;
}

HashMap<Vector2i, TileMapCell> TileMapLayer25DEditorTilesPlugin::_draw_rect(Vector2i p_start_cell, Vector2i p_end_cell, bool p_erase) {
	TileMapLayer25D *edited_layer = _get_edited_layer();
	if (!edited_layer) {
		return HashMap<Vector2i, TileMapCell>();
	}

	Ref<TileSet> tile_set = edited_layer->get_tile_set();
	if (tile_set.is_null()) {
		return HashMap<Vector2i, TileMapCell>();
	}

	// Create the rect to draw.
	Rect2i rect = Rect2i(p_start_cell, p_end_cell - p_start_cell).abs();
	rect.size += Vector2i(1, 1);

	// Get or create the pattern.
	Ref<TileMapPattern> pattern = p_erase ? erase_pattern : selection_pattern;

	ERR_FAIL_COND_V(pattern->is_empty(), (HashMap<Vector2i, TileMapCell>()));

	// Compute the offset to align things to the bottom or right.
	bool aligned_right = p_end_cell.x < p_start_cell.x;
	bool valigned_bottom = p_end_cell.y < p_start_cell.y;
	Vector2i offset = Vector2i(aligned_right ? -(pattern->get_size().x - (rect.get_size().x % pattern->get_size().x)) : 0, valigned_bottom ? -(pattern->get_size().y - (rect.get_size().y % pattern->get_size().y)) : 0);

	HashMap<Vector2i, TileMapCell> output;
	if (!pattern->is_empty()) {
		if (!p_erase && random_tile_toggle->is_pressed()) {
			pattern_rng.set_state(rng_base_state);
			// Paint a random tile.
			for (int x = 0; x < rect.size.x; x++) {
				for (int y = 0; y < rect.size.y; y++) {
					Vector2i coords = rect.position + Vector2i(x, y);
					_add_to_output_if_tile_changed(output, edited_layer, edited_z, coords, _pick_random_tile(pattern));
				}
			}
		} else {
			// Paint the pattern.
			TypedArray<Vector2i> used_cells = pattern->get_used_cells();
			for (int x = 0; x <= rect.size.x / pattern->get_size().x; x++) {
				for (int y = 0; y <= rect.size.y / pattern->get_size().y; y++) {
					Vector2i pattern_coords = rect.position + Vector2i(x, y) * pattern->get_size() + offset;
					for (int j = 0; j < used_cells.size(); j++) {
						Vector2i coords = pattern_coords + used_cells[j];
						if (rect.has_point(coords)) {
							_add_to_output_if_tile_changed(output, edited_layer, edited_z, coords, TileMapCell(pattern->get_cell_source_id(used_cells[j]), pattern->get_cell_atlas_coords(used_cells[j]), pattern->get_cell_alternative_tile(used_cells[j])));
						}
					}
				}
			}
		}
	}

	return output;
}

HashMap<Vector2i, TileMapCell> TileMapLayer25DEditorTilesPlugin::_draw_bucket_fill(Vector2i p_coords, bool p_contiguous, bool p_erase) {
	TileMapLayer25D*edited_layer = _get_edited_layer();
	if (!edited_layer) {
		return HashMap<Vector2i, TileMapCell>();
	}

	Ref<TileSet> tile_set = edited_layer->get_tile_set();
	if (tile_set.is_null()) {
		return HashMap<Vector2i, TileMapCell>();
	}

	HashMap<Vector2i, TileMapCell> output;

	// Get or create the pattern.
	Ref<TileMapPattern> pattern = p_erase ? erase_pattern : selection_pattern;

	if (!pattern->is_empty()) {
		TileMapCell source_cell = edited_layer->get_cell(p_coords, edited_z);

		// If we are filling empty tiles, compute the tilemap boundaries.
		Rect2i boundaries;
		if (source_cell.source_id == TileSet::INVALID_SOURCE) {
			boundaries = edited_layer->get_used_rect();
		}

		pattern_rng.set_state(rng_base_state);
		if (p_contiguous) {
			// Replace continuous tiles like the source.
			RBSet<Vector2i> already_checked;
			List<Vector2i> to_check;
			to_check.push_back(p_coords);
			while (!to_check.is_empty()) {
				Vector2i coords = to_check.back()->get();
				to_check.pop_back();
				if (!already_checked.has(coords)) {
					if (source_cell.source_id == edited_layer->get_cell_source_id(coords) &&
							source_cell.get_atlas_coords() == edited_layer->get_cell_atlas_coords(coords) &&
							source_cell.alternative_tile == edited_layer->get_cell_alternative_tile(coords) &&
							(source_cell.source_id != TileSet::INVALID_SOURCE || boundaries.has_point(coords))) {
						if (!p_erase && random_tile_toggle->is_pressed()) {
							// Paint a random tile.
							_add_to_output_if_tile_changed(output, edited_layer, edited_z, coords, _pick_random_tile(pattern));
						} else {
							// Paint the pattern.
							Vector2i pattern_coords = (coords - p_coords) % pattern->get_size(); // Note: it would be good to have posmodv for Vector2i.
							pattern_coords.x = pattern_coords.x < 0 ? pattern_coords.x + pattern->get_size().x : pattern_coords.x;
							pattern_coords.y = pattern_coords.y < 0 ? pattern_coords.y + pattern->get_size().y : pattern_coords.y;
							if (pattern->has_cell(pattern_coords)) {
								_add_to_output_if_tile_changed(output, edited_layer, edited_z, coords, TileMapCell(pattern->get_cell_source_id(pattern_coords), pattern->get_cell_atlas_coords(pattern_coords), pattern->get_cell_alternative_tile(pattern_coords)));
							} else {
								_add_to_output_if_tile_changed(output, edited_layer, edited_z, coords, TileMapCell());
							}
						}

						// Get surrounding tiles (handles different tile shapes).
						TypedArray<Vector2i> around = tile_set->get_surrounding_cells(coords);
						for (int i = 0; i < around.size(); i++) {
							to_check.push_back(around[i]);
						}
					}
					already_checked.insert(coords);
				}
			}
		} else {
			// Replace all tiles like the source.
			TypedArray<Vector2i> to_check;
			if (source_cell.source_id == TileSet::INVALID_SOURCE) {
				Rect2i rect = edited_layer->get_used_rect();
				if (!rect.has_area()) {
					rect = Rect2i(p_coords, Vector2i(1, 1));
				}
				for (int x = boundaries.position.x; x < boundaries.get_end().x; x++) {
					for (int y = boundaries.position.y; y < boundaries.get_end().y; y++) {
						to_check.append(Vector2i(x, y));
					}
				}
			} else {
				to_check = edited_layer->get_used_cells();
			}
			for (int i = 0; i < to_check.size(); i++) {
				Vector2i coords = to_check[i];
				if (source_cell.source_id == edited_layer->get_cell_source_id(coords) &&
						source_cell.get_atlas_coords() == edited_layer->get_cell_atlas_coords(coords) &&
						source_cell.alternative_tile == edited_layer->get_cell_alternative_tile(coords) &&
						(source_cell.source_id != TileSet::INVALID_SOURCE || boundaries.has_point(coords))) {
					if (!p_erase && random_tile_toggle->is_pressed()) {
						// Paint a random tile.
						_add_to_output_if_tile_changed(output, edited_layer, edited_z, coords, _pick_random_tile(pattern));
					} else {
						// Paint the pattern.
						Vector2i pattern_coords = (coords - p_coords) % pattern->get_size(); // Note: it would be good to have posmodv for Vector2i.
						pattern_coords.x = pattern_coords.x < 0 ? pattern_coords.x + pattern->get_size().x : pattern_coords.x;
						pattern_coords.y = pattern_coords.y < 0 ? pattern_coords.y + pattern->get_size().y : pattern_coords.y;
						if (pattern->has_cell(pattern_coords)) {
							_add_to_output_if_tile_changed(output, edited_layer, edited_z, coords, TileMapCell(pattern->get_cell_source_id(pattern_coords), pattern->get_cell_atlas_coords(pattern_coords), pattern->get_cell_alternative_tile(pattern_coords)));
						} else {
							_add_to_output_if_tile_changed(output, edited_layer, edited_z, coords, TileMapCell());
						}
					}
				}
			}
		}
	}
	return output;
}

void TileMapLayer25DEditorTilesPlugin::_stop_dragging() {
	if (drag_type == DRAG_TYPE_NONE) {
		return;
	}

	TileMapLayer25D*edited_layer = _get_edited_layer();
	if (!edited_layer) {
		return;
	}

	Ref<TileSet> tile_set = edited_layer->get_tile_set();
	if (tile_set.is_null()) {
		return;
	}

	Transform2D xform = CanvasItemEditor::get_singleton()->get_canvas_transform() * edited_layer->get_global_transform_with_canvas();
	Vector2 mpos = xform.affine_inverse().xform(CanvasItemEditor::get_singleton()->get_viewport_control()->get_local_mouse_position());

	EditorUndoRedoManager *undo_redo = EditorUndoRedoManager::get_singleton();
	switch (drag_type) {
		case DRAG_TYPE_SELECT: {
			undo_redo->create_action_for_history(TTR("Change selection"), EditorNode::get_editor_data().get_current_edited_scene_history_id(), UndoRedo::MERGE_DISABLE, false, false);
			undo_redo->add_undo_method(this, "_set_tile_map_selection", _get_tile_map_selection());

			if (!Input::get_singleton()->is_key_pressed(Key::SHIFT) && !Input::get_singleton()->is_key_pressed(Key::CMD_OR_CTRL)) {
				tile_map_selection.clear();
			}
			Rect2i rect = Rect2i(edited_layer->local_viewport_to_map(drag_start_mouse_pos, edited_z), edited_layer->local_viewport_to_map(mpos, edited_z) - edited_layer->local_viewport_to_map(drag_start_mouse_pos, edited_z)).abs();
			for (int x = rect.position.x; x <= rect.get_end().x; x++) {
				for (int y = rect.position.y; y <= rect.get_end().y; y++) {
					Vector2i coords = Vector2i(x, y);
					if (Input::get_singleton()->is_key_pressed(Key::CMD_OR_CTRL)) {
						if (tile_map_selection.has(coords)) {
							tile_map_selection.erase(coords);
						}
					} else {
						if (edited_layer->get_cell_source_id(coords) != TileSet::INVALID_SOURCE) {
							tile_map_selection.insert(coords);
						}
					}
				}
			}
			undo_redo->add_do_method(this, "_set_tile_map_selection", _get_tile_map_selection());
			undo_redo->commit_action(false);

			_update_selection_pattern_from_tilemap_selection();
			_update_tileset_selection_from_selection_pattern();
		} break;
		case DRAG_TYPE_MOVE: {
			if (patterns_item_list->is_visible_in_tree() && patterns_item_list->has_point(patterns_item_list->get_local_mouse_position())) {
				// Restore the cells.
				for (KeyValue<Vector2i, TileMapCell> kv : drag_modified) {
					edited_layer->set_cell(kv.key, edited_z, kv.value.source_id, kv.value.get_atlas_coords(), kv.value.alternative_tile);
				}

				if (!EditorNode::get_singleton()->is_resource_read_only(tile_set)) {
					// Creating a pattern in the pattern list.
					select_last_pattern = true;
					int new_pattern_index = tile_set->get_patterns_count();
					undo_redo->create_action(TTR("Add TileSet pattern"));
					undo_redo->add_do_method(*tile_set, "add_pattern", selection_pattern, new_pattern_index);
					undo_redo->add_undo_method(*tile_set, "remove_pattern", new_pattern_index);
					undo_redo->commit_action();
				}
			} else {
				// Get the top-left cell.
				Vector2i top_left;
				if (!tile_map_selection.is_empty()) {
					top_left = tile_map_selection.front()->get();
				}
				for (const Vector2i &E : tile_map_selection) {
					top_left = top_left.min(E);
				}

				// Get the offset from the mouse.
				Vector2i offset = drag_start_mouse_pos - tile_set->map_to_local(top_left);
				offset = edited_layer->local_viewport_to_map(mpos - offset, edited_z) - edited_layer->local_viewport_to_map(drag_start_mouse_pos - offset, edited_z);

				TypedArray<Vector2i> selection_used_cells = selection_pattern->get_used_cells();

				// Build the list of cells to undo.
				Vector2i coords;
				HashMap<Vector2i, TileMapCell> cells_undo;
				for (int i = 0; i < selection_used_cells.size(); i++) {
					coords = tile_set->map_pattern(top_left + offset, selection_used_cells[i], selection_pattern);
					cells_undo[coords] = TileMapCell(edited_layer->get_cell_source_id(coords), edited_layer->get_cell_atlas_coords(coords), edited_layer->get_cell_alternative_tile(coords));
				}
				for (int i = 0; i < selection_used_cells.size(); i++) {
					coords = tile_set->map_pattern(top_left, selection_used_cells[i], selection_pattern);
					cells_undo[coords] = TileMapCell(drag_modified[coords].source_id, drag_modified[coords].get_atlas_coords(), drag_modified[coords].alternative_tile);
				}

				// Build the list of cells to do.
				HashMap<Vector2i, TileMapCell> cells_do;
				for (int i = 0; i < selection_used_cells.size(); i++) {
					coords = tile_set->map_pattern(top_left, selection_used_cells[i], selection_pattern);
					cells_do[coords] = TileMapCell();
				}
				for (int i = 0; i < selection_used_cells.size(); i++) {
					coords = tile_set->map_pattern(top_left + offset, selection_used_cells[i], selection_pattern);
					cells_do[coords] = TileMapCell(selection_pattern->get_cell_source_id(selection_used_cells[i]), selection_pattern->get_cell_atlas_coords(selection_used_cells[i]), selection_pattern->get_cell_alternative_tile(selection_used_cells[i]));
				}

				// Move the tiles.
				undo_redo->create_action(TTR("Move tiles"));
				for (const KeyValue<Vector2i, TileMapCell> &E : cells_do) {
					undo_redo->add_do_method(edited_layer, "set_cell", E.key, E.value.source_id, E.value.get_atlas_coords(), E.value.alternative_tile);
				}
				for (const KeyValue<Vector2i, TileMapCell> &E : cells_undo) {
					undo_redo->add_undo_method(edited_layer, "set_cell", E.key, E.value.source_id, E.value.get_atlas_coords(), E.value.alternative_tile);
				}

				// Update the selection.
				undo_redo->add_undo_method(this, "_set_tile_map_selection", _get_tile_map_selection());
				tile_map_selection.clear();
				for (int i = 0; i < selection_used_cells.size(); i++) {
					coords = tile_set->map_pattern(top_left + offset, selection_used_cells[i], selection_pattern);
					tile_map_selection.insert(coords);
				}
				undo_redo->add_do_method(this, "_set_tile_map_selection", _get_tile_map_selection());
				undo_redo->commit_action();
			}
		} break;
		case DRAG_TYPE_PICK: {
			Rect2i rect = Rect2i(edited_layer->local_viewport_to_map(drag_start_mouse_pos, edited_z), edited_layer->local_viewport_to_map(mpos, edited_z) - edited_layer->local_viewport_to_map(drag_start_mouse_pos, edited_z)).abs();
			rect.size += Vector2i(1, 1);

			int picked_source = -1;
			TypedArray<Vector2i> coords_array;
			for (int x = rect.position.x; x < rect.get_end().x; x++) {
				for (int y = rect.position.y; y < rect.get_end().y; y++) {
					Vector2i coords = Vector2i(x, y);

					int source = edited_layer->get_cell_source_id(coords);
					if (source != TileSet::INVALID_SOURCE) {
						coords_array.push_back(coords);
						if (picked_source == -1) {
							picked_source = source;
						} else if (picked_source != source) {
							picked_source = -2;
						}
					}
				}
			}

			if (picked_source >= 0) {
				for (int i = 0; i < sources_list->get_item_count(); i++) {
					if (int(sources_list->get_item_metadata(i)) == picked_source) {
						sources_list->set_current(i);
						TilesEditorUtils::get_singleton()->set_sources_lists_current(i);
						break;
					}
				}
				sources_list->ensure_current_is_visible();
			}

			Ref<TileMapPattern> new_selection_pattern = edited_layer->get_pattern(coords_array);
			if (!new_selection_pattern->is_empty()) {
				selection_pattern = new_selection_pattern;
				_update_tileset_selection_from_selection_pattern();
			}
			picker_button->set_pressed(false);
		} break;
		case DRAG_TYPE_PAINT: {
			undo_redo->create_action(TTR("Paint tiles"));
			for (const KeyValue<Vector2i, TileMapCell> &E : drag_modified) {
				undo_redo->add_do_method(edited_layer, "set_cell", E.key, edited_layer->get_cell_source_id(E.key), edited_layer->get_cell_atlas_coords(E.key), edited_layer->get_cell_alternative_tile(E.key));
				undo_redo->add_undo_method(edited_layer, "set_cell", E.key, E.value.source_id, E.value.get_atlas_coords(), E.value.alternative_tile);
			}
			undo_redo->commit_action(false);
		} break;
		case DRAG_TYPE_LINE: {
			HashMap<Vector2i, TileMapCell> to_draw = _draw_line(drag_start_mouse_pos, drag_start_mouse_pos, mpos, drag_erasing);
			undo_redo->create_action(TTR("Paint tiles"));
			for (const KeyValue<Vector2i, TileMapCell> &E : to_draw) {
				if (!drag_erasing && E.value.source_id == TileSet::INVALID_SOURCE) {
					continue;
				}
				undo_redo->add_do_method(edited_layer, "set_cell", E.key, E.value.source_id, E.value.get_atlas_coords(), E.value.alternative_tile);
				undo_redo->add_undo_method(edited_layer, "set_cell", E.key, edited_layer->get_cell_source_id(E.key), edited_layer->get_cell_atlas_coords(E.key), edited_layer->get_cell_alternative_tile(E.key));
			}
			undo_redo->commit_action();
		} break;
		case DRAG_TYPE_RECT: {
			HashMap<Vector2i, TileMapCell> to_draw = _draw_rect(edited_layer->local_viewport_to_map(drag_start_mouse_pos, edited_z), edited_layer->local_viewport_to_map(mpos, edited_z), drag_erasing);
			undo_redo->create_action(TTR("Paint tiles"));
			for (const KeyValue<Vector2i, TileMapCell> &E : to_draw) {
				if (!drag_erasing && E.value.source_id == TileSet::INVALID_SOURCE) {
					continue;
				}
				undo_redo->add_do_method(edited_layer, "set_cell", E.key, E.value.source_id, E.value.get_atlas_coords(), E.value.alternative_tile);
				undo_redo->add_undo_method(edited_layer, "set_cell", E.key, edited_layer->get_cell_source_id(E.key), edited_layer->get_cell_atlas_coords(E.key), edited_layer->get_cell_alternative_tile(E.key));
			}
			undo_redo->commit_action();
		} break;
		case DRAG_TYPE_BUCKET: {
			undo_redo->create_action(TTR("Paint tiles"));
			for (const KeyValue<Vector2i, TileMapCell> &E : drag_modified) {
				undo_redo->add_do_method(edited_layer, "set_cell", E.key, edited_layer->get_cell_source_id(E.key), edited_layer->get_cell_atlas_coords(E.key), edited_layer->get_cell_alternative_tile(E.key));
				undo_redo->add_undo_method(edited_layer, "set_cell", E.key, E.value.source_id, E.value.get_atlas_coords(), E.value.alternative_tile);
			}
			undo_redo->commit_action(false);
		} break;
		case DRAG_TYPE_CLIPBOARD_PASTE: {
			Vector2 mouse_offset = (Vector2(tile_map_clipboard->get_size()) / 2.0 - Vector2(0.5, 0.5)) * tile_set->get_tile_size();
			undo_redo->create_action(TTR("Paste tiles"));
			TypedArray<Vector2i> used_cells = tile_map_clipboard->get_used_cells();
			for (int i = 0; i < used_cells.size(); i++) {
				Vector2i coords = tile_set->map_pattern(edited_layer->local_viewport_to_map(mpos - mouse_offset, edited_z), used_cells[i], tile_map_clipboard);
				undo_redo->add_do_method(edited_layer, "set_cell", coords, tile_map_clipboard->get_cell_source_id(used_cells[i]), tile_map_clipboard->get_cell_atlas_coords(used_cells[i]), tile_map_clipboard->get_cell_alternative_tile(used_cells[i]));
				undo_redo->add_undo_method(edited_layer, "set_cell", coords, edited_layer->get_cell_source_id(coords), edited_layer->get_cell_atlas_coords(coords), edited_layer->get_cell_alternative_tile(coords));
			}
			undo_redo->commit_action();
		} break;
		default:
			break;
	}
	drag_type = DRAG_TYPE_NONE;
}

void TileMapLayer25DEditorTilesPlugin::_apply_transform(TileTransformType p_type) {
	if (selection_pattern.is_null() || selection_pattern->is_empty()) {
		return;
	}

	Ref<TileMapPattern> transformed_pattern;
	transformed_pattern.instantiate();

	Vector2i size = selection_pattern->get_size();
	for (int y = 0; y < size.y; y++) {
		for (int x = 0; x < size.x; x++) {
			Vector2i src_coords = Vector2i(x, y);
			if (!selection_pattern->has_cell(src_coords)) {
				continue;
			}

			Vector2i dst_coords;

			if (p_type == TRANSFORM_ROTATE_LEFT) {
				dst_coords = Vector2i(y, size.x - x - 1);
			} else if (p_type == TRANSFORM_ROTATE_RIGHT) {
				dst_coords = Vector2i(size.y - y - 1, x);
			} else if (p_type == TRANSFORM_FLIP_H) {
				dst_coords = Vector2i(size.x - x - 1, y);
			} else if (p_type == TRANSFORM_FLIP_V) {
				dst_coords = Vector2i(x, size.y - y - 1);
			}

			transformed_pattern->set_cell(dst_coords,
					selection_pattern->get_cell_source_id(src_coords), selection_pattern->get_cell_atlas_coords(src_coords),
					_get_transformed_alternative(selection_pattern->get_cell_alternative_tile(src_coords), p_type));
		}
	}
	selection_pattern = transformed_pattern;
	CanvasItemEditor::get_singleton()->update_viewport();
}

int TileMapLayer25DEditorTilesPlugin::_get_transformed_alternative(int p_alternative_id, TileTransformType p_transform) {
	bool transform_flip_h = p_alternative_id & TileSetAtlasSource::TRANSFORM_FLIP_H;
	bool transform_flip_v = p_alternative_id & TileSetAtlasSource::TRANSFORM_FLIP_V;
	bool transform_transpose = p_alternative_id & TileSetAtlasSource::TRANSFORM_TRANSPOSE;

	switch (p_transform) {
		case TRANSFORM_ROTATE_LEFT: { // (h, v, t) -> (v, !h, !t)
			bool negated_flip_h = !transform_flip_h;
			transform_flip_h = transform_flip_v;
			transform_flip_v = negated_flip_h;
			transform_transpose = !transform_transpose;
		} break;
		case TRANSFORM_ROTATE_RIGHT: { // (h, v, t) -> (!v, h, !t)
			bool negated_flip_v = !transform_flip_v;
			transform_flip_v = transform_flip_h;
			transform_flip_h = negated_flip_v;
			transform_transpose = !transform_transpose;
		} break;
		case TRANSFORM_FLIP_H: { // (h, v, t) -> (!h, v, t)
			transform_flip_h = !transform_flip_h;
		} break;
		case TRANSFORM_FLIP_V: { // (h, v, t) -> (h, !v, t)
			transform_flip_v = !transform_flip_v;
		} break;
	}

	return TileSetAtlasSource::alternative_no_transform(p_alternative_id) |
			int(transform_flip_h) * TileSetAtlasSource::TRANSFORM_FLIP_H |
			int(transform_flip_v) * TileSetAtlasSource::TRANSFORM_FLIP_V |
			int(transform_transpose) * TileSetAtlasSource::TRANSFORM_TRANSPOSE;
}

void TileMapLayer25DEditorTilesPlugin::_update_fix_selected_and_hovered() {
	TileMapLayer25D*edited_layer = _get_edited_layer();
	if (!edited_layer) {
		hovered_tile.source_id = TileSet::INVALID_SOURCE;
		hovered_tile.set_atlas_coords(TileSetSource::INVALID_ATLAS_COORDS);
		hovered_tile.alternative_tile = TileSetSource::INVALID_TILE_ALTERNATIVE;
		tile_set_selection.clear();
		patterns_item_list->deselect_all();
		tile_map_selection.clear();
		selection_pattern.instantiate();
		return;
	}
	Ref<TileSet> tile_set = edited_layer->get_tile_set();
	if (tile_set.is_null()) {
		hovered_tile.source_id = TileSet::INVALID_SOURCE;
		hovered_tile.set_atlas_coords(TileSetSource::INVALID_ATLAS_COORDS);
		hovered_tile.alternative_tile = TileSetSource::INVALID_TILE_ALTERNATIVE;
		tile_set_selection.clear();
		patterns_item_list->deselect_all();
		tile_map_selection.clear();
		selection_pattern.instantiate();
		return;
	}

	int source_index = sources_list->get_current();
	if (source_index < 0 || source_index >= sources_list->get_item_count()) {
		hovered_tile.source_id = TileSet::INVALID_SOURCE;
		hovered_tile.set_atlas_coords(TileSetSource::INVALID_ATLAS_COORDS);
		hovered_tile.alternative_tile = TileSetSource::INVALID_TILE_ALTERNATIVE;
		tile_set_selection.clear();
		patterns_item_list->deselect_all();
		tile_map_selection.clear();
		selection_pattern.instantiate();
		return;
	}

	int source_id = sources_list->get_item_metadata(source_index);

	// Clear hovered if needed.
	if (source_id != hovered_tile.source_id ||
			!tile_set->has_source(hovered_tile.source_id) ||
			!tile_set->get_source(hovered_tile.source_id)->has_tile(hovered_tile.get_atlas_coords()) ||
			!tile_set->get_source(hovered_tile.source_id)->has_alternative_tile(hovered_tile.get_atlas_coords(), hovered_tile.alternative_tile)) {
		hovered_tile.source_id = TileSet::INVALID_SOURCE;
		hovered_tile.set_atlas_coords(TileSetSource::INVALID_ATLAS_COORDS);
		hovered_tile.alternative_tile = TileSetSource::INVALID_TILE_ALTERNATIVE;
	}

	// Cleanup tile set selection.
	for (RBSet<TileMapCell>::Element *E = tile_set_selection.front(); E;) {
		RBSet<TileMapCell>::Element *N = E->next();
		const TileMapCell *selected = &(E->get());
		if (!tile_set->has_source(selected->source_id) ||
				!tile_set->get_source(selected->source_id)->has_tile(selected->get_atlas_coords()) ||
				!tile_set->get_source(selected->source_id)->has_alternative_tile(selected->get_atlas_coords(), selected->alternative_tile)) {
			tile_set_selection.erase(E);
		}
		E = N;
	}

	// Cleanup selection.
	for (const KeyValue<Vector2i, TileMapCell> &E : selection_pattern->get_pattern()) {
		const Vector2i key = E.key;
		const TileMapCell &selected = E.value;
		if (!tile_set->has_source(selected.source_id) ||
				!tile_set->get_source(selected.source_id)->has_tile(selected.get_atlas_coords()) ||
				!tile_set->get_source(selected.source_id)->has_alternative_tile(selected.get_atlas_coords(), selected.alternative_tile)) {
			selection_pattern->remove_cell(key);
		}
	}
}

void TileMapLayer25DEditorTilesPlugin::_fix_invalid_tiles_in_tile_map_selection() {
	TileMapLayer25D*edited_layer = _get_edited_layer();
	if (!edited_layer) {
		return;
	}

	RBSet<Vector2i> to_remove;
	for (Vector2i selected : tile_map_selection) {
		TileMapCell cell = edited_layer->get_cell(selected);
		if (cell.source_id == TileSet::INVALID_SOURCE && cell.get_atlas_coords() == TileSetSource::INVALID_ATLAS_COORDS && cell.alternative_tile == TileSetAtlasSource::INVALID_TILE_ALTERNATIVE) {
			to_remove.insert(selected);
		}
	}

	for (Vector2i cell : to_remove) {
		tile_map_selection.erase(cell);
	}
}
void TileMapLayer25DEditorTilesPlugin::patterns_item_list_empty_clicked(const Vector2 &p_pos, MouseButton p_mouse_button_index) {
	if (p_mouse_button_index == MouseButton::LEFT) {
		_update_selection_pattern_from_tileset_pattern_selection();
	}
}

void TileMapLayer25DEditorTilesPlugin::_update_selection_pattern_from_tilemap_selection() {
	TileMapLayer25D*edited_layer = _get_edited_layer();
	if (!edited_layer) {
		return;
	}

	Ref<TileSet> tile_set = edited_layer->get_tile_set();
	if (tile_set.is_null()) {
		return;
	}

	selection_pattern.instantiate();

	TypedArray<Vector2i> coords_array;
	for (const Vector2i &E : tile_map_selection) {
		coords_array.push_back(E);
	}
	selection_pattern = edited_layer->get_pattern(coords_array);
	_update_transform_buttons();
}

void TileMapLayer25DEditorTilesPlugin::_update_selection_pattern_from_tileset_tiles_selection() {
	TileMapLayer25D*edited_layer = _get_edited_layer();
	if (!edited_layer) {
		return;
	}

	Ref<TileSet> tile_set = edited_layer->get_tile_set();
	if (tile_set.is_null()) {
		return;
	}

	// Clear the tilemap selection.
	tile_map_selection.clear();

	// Clear the selected pattern.
	selection_pattern.instantiate();

	// Group per source.
	HashMap<int, List<const TileMapCell *>> per_source;
	for (const TileMapCell &E : tile_set_selection) {
		per_source[E.source_id].push_back(&(E));
	}

	int vertical_offset = 0;
	for (const KeyValue<int, List<const TileMapCell *>> &E_source : per_source) {
		// Per source.
		List<const TileMapCell *> unorganized;
		Rect2i encompassing_rect_coords;
		HashMap<Vector2i, const TileMapCell *> organized_pattern;

		TileSetSource *source = *tile_set->get_source(E_source.key);
		TileSetAtlasSource *atlas_source = Object::cast_to<TileSetAtlasSource>(source);
		if (atlas_source) {
			// Organize using coordinates.
			for (const TileMapCell *current : E_source.value) {
				if (current->alternative_tile == 0) {
					organized_pattern[current->get_atlas_coords()] = current;
				} else {
					unorganized.push_back(current);
				}
			}

			// Compute the encompassing rect for the organized pattern.
			HashMap<Vector2i, const TileMapCell *>::Iterator E_cell = organized_pattern.begin();
			if (E_cell) {
				encompassing_rect_coords = Rect2i(E_cell->key, Vector2i(1, 1));
				for (; E_cell; ++E_cell) {
					encompassing_rect_coords.expand_to(E_cell->key + Vector2i(1, 1));
					encompassing_rect_coords.expand_to(E_cell->key);
				}
			}
		} else {
			// Add everything unorganized.
			for (const TileMapCell *cell : E_source.value) {
				unorganized.push_back(cell);
			}
		}

		// Now add everything to the output pattern.
		for (const KeyValue<Vector2i, const TileMapCell *> &E_cell : organized_pattern) {
			selection_pattern->set_cell(E_cell.key - encompassing_rect_coords.position + Vector2i(0, vertical_offset), E_cell.value->source_id, E_cell.value->get_atlas_coords(), E_cell.value->alternative_tile);
		}
		Vector2i organized_size = selection_pattern->get_size();
		int unorganized_index = 0;
		for (const TileMapCell *cell : unorganized) {
			selection_pattern->set_cell(Vector2(organized_size.x + unorganized_index, vertical_offset), cell->source_id, cell->get_atlas_coords(), cell->alternative_tile);
			unorganized_index++;
		}
		vertical_offset += MAX(organized_size.y, 1);
	}
	CanvasItemEditor::get_singleton()->update_viewport();
	_update_transform_buttons();
}

void TileMapLayer25DEditorTilesPlugin::_update_selection_pattern_from_tileset_pattern_selection() {
	TileMapLayer25D*edited_layer = _get_edited_layer();
	if (!edited_layer) {
		return;
	}

	Ref<TileSet> tile_set = edited_layer->get_tile_set();
	if (tile_set.is_null()) {
		return;
	}

	// Clear the tilemap selection.
	tile_map_selection.clear();

	// Clear the selected pattern.
	selection_pattern.instantiate();

	if (patterns_item_list->get_selected_items().size() >= 1) {
		selection_pattern = patterns_item_list->get_item_metadata(patterns_item_list->get_selected_items()[0]);
	}

	CanvasItemEditor::get_singleton()->update_viewport();
}

void TileMapLayer25DEditorTilesPlugin::_update_tileset_selection_from_selection_pattern() {
	tile_set_selection.clear();
	TypedArray<Vector2i> used_cells = selection_pattern->get_used_cells();
	for (int i = 0; i < used_cells.size(); i++) {
		Vector2i coords = used_cells[i];
		if (selection_pattern->get_cell_source_id(coords) != TileSet::INVALID_SOURCE) {
			tile_set_selection.insert(TileMapCell(selection_pattern->get_cell_source_id(coords), selection_pattern->get_cell_atlas_coords(coords), selection_pattern->get_cell_alternative_tile(coords)));
		}
	}
	_update_source_display();
	tile_atlas_control->queue_redraw();
	alternative_tiles_control->queue_redraw();
	_update_transform_buttons();
}

void TileMapLayer25DEditorTilesPlugin::_tile_atlas_control_draw() {
	TileMapLayer25D*edited_layer = _get_edited_layer();
	if (!edited_layer) {
		return;
	}

	Ref<TileSet> tile_set = edited_layer->get_tile_set();
	if (tile_set.is_null()) {
		return;
	}

	int source_index = sources_list->get_current();
	if (source_index < 0 || source_index >= sources_list->get_item_count()) {
		return;
	}

	int source_id = sources_list->get_item_metadata(source_index);
	if (!tile_set->has_source(source_id)) {
		return;
	}

	TileSetAtlasSource *atlas = Object::cast_to<TileSetAtlasSource>(*tile_set->get_source(source_id));
	if (!atlas) {
		return;
	}

	// Draw the selection.
	Color grid_color = EDITOR_GET("editors/tiles_editor/grid_color");
	Color selection_color = Color::from_hsv(Math::fposmod(grid_color.get_h() + 0.5, 1.0), grid_color.get_s(), grid_color.get_v(), 1.0);
	for (const TileMapCell &E : tile_set_selection) {
		int16_t untransformed_alternative_id = E.alternative_tile & TileSetAtlasSource::UNTRANSFORM_MASK;
		if (E.source_id == source_id && untransformed_alternative_id == 0) {
			for (int frame = 0; frame < atlas->get_tile_animation_frames_count(E.get_atlas_coords()); frame++) {
				Color color = selection_color;
				if (frame > 0) {
					color.a *= 0.3;
				}
				TilesEditorUtils::draw_selection_rect(tile_atlas_control, atlas->get_tile_texture_region(E.get_atlas_coords(), frame), color);
			}
		}
	}

	// Draw the hovered tile.
	if (hovered_tile.get_atlas_coords() != TileSetSource::INVALID_ATLAS_COORDS && hovered_tile.alternative_tile == 0 && !tile_set_dragging_selection) {
		for (int frame = 0; frame < atlas->get_tile_animation_frames_count(hovered_tile.get_atlas_coords()); frame++) {
			Color color = Color(1.0, 0.8, 0.0, frame == 0 ? 0.6 : 0.3);
			TilesEditorUtils::draw_selection_rect(tile_atlas_control, atlas->get_tile_texture_region(hovered_tile.get_atlas_coords(), frame), color);
		}
	}

	// Draw the selection rect.
	if (tile_set_dragging_selection) {
		Vector2i start_tile = tile_atlas_view->get_atlas_tile_coords_at_pos(tile_set_drag_start_mouse_pos, true);
		Vector2i end_tile = tile_atlas_view->get_atlas_tile_coords_at_pos(tile_atlas_control->get_local_mouse_position(), true);

		Rect2i region = Rect2i(start_tile, end_tile - start_tile).abs();
		region.size += Vector2i(1, 1);

		RBSet<Vector2i> to_draw;
		for (int x = region.position.x; x < region.get_end().x; x++) {
			for (int y = region.position.y; y < region.get_end().y; y++) {
				Vector2i tile = atlas->get_tile_at_coords(Vector2i(x, y));
				if (tile != TileSetSource::INVALID_ATLAS_COORDS) {
					to_draw.insert(tile);
				}
			}
		}
		for (const Vector2i &E : to_draw) {
			TilesEditorUtils::draw_selection_rect(tile_atlas_control, atlas->get_tile_texture_region(E));
		}
	}
}

void TileMapLayer25DEditorTilesPlugin::_tile_atlas_control_mouse_exited() {
	hovered_tile.source_id = TileSet::INVALID_SOURCE;
	hovered_tile.set_atlas_coords(TileSetSource::INVALID_ATLAS_COORDS);
	hovered_tile.alternative_tile = TileSetSource::INVALID_TILE_ALTERNATIVE;
	tile_atlas_control->queue_redraw();
}

void TileMapLayer25DEditorTilesPlugin::_tile_atlas_control_gui_input(const Ref<InputEvent> &p_event) {
	TileMapLayer25D*edited_layer = _get_edited_layer();
	if (!edited_layer) {
		return;
	}

	Ref<TileSet> tile_set = edited_layer->get_tile_set();
	if (tile_set.is_null()) {
		return;
	}

	int source_index = sources_list->get_current();
	if (source_index < 0 || source_index >= sources_list->get_item_count()) {
		return;
	}

	int source_id = sources_list->get_item_metadata(source_index);
	if (!tile_set->has_source(source_id)) {
		return;
	}

	TileSetAtlasSource *atlas = Object::cast_to<TileSetAtlasSource>(*tile_set->get_source(source_id));
	if (!atlas) {
		return;
	}

	// Update the hovered tile
	hovered_tile.source_id = source_id;
	hovered_tile.set_atlas_coords(TileSetSource::INVALID_ATLAS_COORDS);
	hovered_tile.alternative_tile = TileSetSource::INVALID_TILE_ALTERNATIVE;
	Vector2i coords = tile_atlas_view->get_atlas_tile_coords_at_pos(tile_atlas_control->get_local_mouse_position());
	if (coords != TileSetSource::INVALID_ATLAS_COORDS) {
		coords = atlas->get_tile_at_coords(coords);
		if (coords != TileSetSource::INVALID_ATLAS_COORDS) {
			hovered_tile.set_atlas_coords(coords);
			hovered_tile.alternative_tile = 0;
		}
	}

	Ref<InputEventMouseMotion> mm = p_event;
	if (mm.is_valid()) {
		tile_atlas_control->queue_redraw();
		alternative_tiles_control->queue_redraw();
	}

	Ref<InputEventMouseButton> mb = p_event;
	if (mb.is_valid() && mb->get_button_index() == MouseButton::LEFT) {
		if (mb->is_pressed()) { // Pressed
			tile_set_dragging_selection = true;
			tile_set_drag_start_mouse_pos = tile_atlas_control->get_local_mouse_position();
			if (!mb->is_shift_pressed()) {
				tile_set_selection.clear();
			}

			if (hovered_tile.get_atlas_coords() != TileSetSource::INVALID_ATLAS_COORDS && hovered_tile.alternative_tile == 0) {
				if (mb->is_shift_pressed() && tile_set_selection.has(TileMapCell(source_id, hovered_tile.get_atlas_coords(), 0))) {
					tile_set_selection.erase(TileMapCell(source_id, hovered_tile.get_atlas_coords(), 0));
				} else {
					tile_set_selection.insert(TileMapCell(source_id, hovered_tile.get_atlas_coords(), 0));
				}
			}
			_update_selection_pattern_from_tileset_tiles_selection();
		} else { // Released
			if (tile_set_dragging_selection) {
				if (!mb->is_shift_pressed()) {
					tile_set_selection.clear();
				}
				// Compute the covered area.
				Vector2i start_tile = tile_atlas_view->get_atlas_tile_coords_at_pos(tile_set_drag_start_mouse_pos, true);
				Vector2i end_tile = tile_atlas_view->get_atlas_tile_coords_at_pos(tile_atlas_control->get_local_mouse_position(), true);
				if (start_tile != TileSetSource::INVALID_ATLAS_COORDS && end_tile != TileSetSource::INVALID_ATLAS_COORDS) {
					Rect2i region = Rect2i(start_tile, end_tile - start_tile).abs();
					region.size += Vector2i(1, 1);

					// To update the selection, we copy the selected/not selected status of the tiles we drag from.
					Vector2i start_coords = atlas->get_tile_at_coords(start_tile);
					if (mb->is_shift_pressed() && start_coords != TileSetSource::INVALID_ATLAS_COORDS && !tile_set_selection.has(TileMapCell(source_id, start_coords, 0))) {
						// Remove from the selection.
						for (int x = region.position.x; x < region.get_end().x; x++) {
							for (int y = region.position.y; y < region.get_end().y; y++) {
								Vector2i tile_coords = atlas->get_tile_at_coords(Vector2i(x, y));
								if (tile_coords != TileSetSource::INVALID_ATLAS_COORDS && tile_set_selection.has(TileMapCell(source_id, tile_coords, 0))) {
									tile_set_selection.erase(TileMapCell(source_id, tile_coords, 0));
								}
							}
						}
					} else {
						// Insert in the selection.
						for (int x = region.position.x; x < region.get_end().x; x++) {
							for (int y = region.position.y; y < region.get_end().y; y++) {
								Vector2i tile_coords = atlas->get_tile_at_coords(Vector2i(x, y));
								if (tile_coords != TileSetSource::INVALID_ATLAS_COORDS) {
									tile_set_selection.insert(TileMapCell(source_id, tile_coords, 0));
								}
							}
						}
					}
				}
				_update_selection_pattern_from_tileset_tiles_selection();
			}
			tile_set_dragging_selection = false;
		}
		tile_atlas_control->queue_redraw();
	}
}

void TileMapLayer25DEditorTilesPlugin::_tile_alternatives_control_draw() {
	TileMapLayer25D*edited_layer = _get_edited_layer();
	if (!edited_layer) {
		return;
	}

	Ref<TileSet> tile_set = edited_layer->get_tile_set();
	if (tile_set.is_null()) {
		return;
	}

	int source_index = sources_list->get_current();
	if (source_index < 0 || source_index >= sources_list->get_item_count()) {
		return;
	}

	int source_id = sources_list->get_item_metadata(source_index);
	if (!tile_set->has_source(source_id)) {
		return;
	}

	TileSetAtlasSource *atlas = Object::cast_to<TileSetAtlasSource>(*tile_set->get_source(source_id));
	if (!atlas) {
		return;
	}

	// Draw the selection.
	for (const TileMapCell &E : tile_set_selection) {
		int16_t untransformed_alternative_id = E.alternative_tile & TileSetAtlasSource::UNTRANSFORM_MASK;
		if (E.source_id == source_id && E.get_atlas_coords() != TileSetSource::INVALID_ATLAS_COORDS && untransformed_alternative_id > 0) {
			Rect2i rect = tile_atlas_view->get_alternative_tile_rect(E.get_atlas_coords(), untransformed_alternative_id);
			if (rect != Rect2i()) {
				TilesEditorUtils::draw_selection_rect(alternative_tiles_control, rect);
			}
		}
	}

	// Draw hovered tile.
	if (hovered_tile.get_atlas_coords() != TileSetSource::INVALID_ATLAS_COORDS && hovered_tile.alternative_tile > 0) {
		Rect2i rect = tile_atlas_view->get_alternative_tile_rect(hovered_tile.get_atlas_coords(), hovered_tile.alternative_tile);
		if (rect != Rect2i()) {
			TilesEditorUtils::draw_selection_rect(alternative_tiles_control, rect, Color(1.0, 0.8, 0.0, 0.5));
		}
	}
}

void TileMapLayer25DEditorTilesPlugin::_tile_alternatives_control_mouse_exited() {
	hovered_tile.source_id = TileSet::INVALID_SOURCE;
	hovered_tile.set_atlas_coords(TileSetSource::INVALID_ATLAS_COORDS);
	hovered_tile.alternative_tile = TileSetSource::INVALID_TILE_ALTERNATIVE;
	alternative_tiles_control->queue_redraw();
}

void TileMapLayer25DEditorTilesPlugin::_tile_alternatives_control_gui_input(const Ref<InputEvent> &p_event) {
	TileMapLayer25D*edited_layer = _get_edited_layer();
	if (!edited_layer) {
		return;
	}

	Ref<TileSet> tile_set = edited_layer->get_tile_set();
	if (tile_set.is_null()) {
		return;
	}

	int source_index = sources_list->get_current();
	if (source_index < 0 || source_index >= sources_list->get_item_count()) {
		return;
	}

	int source_id = sources_list->get_item_metadata(source_index);
	if (!tile_set->has_source(source_id)) {
		return;
	}

	TileSetAtlasSource *atlas = Object::cast_to<TileSetAtlasSource>(*tile_set->get_source(source_id));
	if (!atlas) {
		return;
	}

	// Update the hovered tile
	hovered_tile.source_id = source_id;
	hovered_tile.set_atlas_coords(TileSetSource::INVALID_ATLAS_COORDS);
	hovered_tile.alternative_tile = TileSetSource::INVALID_TILE_ALTERNATIVE;
	Vector3i alternative_coords = tile_atlas_view->get_alternative_tile_at_pos(alternative_tiles_control->get_local_mouse_position());
	Vector2i coords = Vector2i(alternative_coords.x, alternative_coords.y);
	int alternative = alternative_coords.z;
	if (coords != TileSetSource::INVALID_ATLAS_COORDS && alternative != TileSetSource::INVALID_TILE_ALTERNATIVE) {
		hovered_tile.set_atlas_coords(coords);
		hovered_tile.alternative_tile = alternative;
	}

	Ref<InputEventMouseMotion> mm = p_event;
	if (mm.is_valid()) {
		tile_atlas_control->queue_redraw();
		alternative_tiles_control->queue_redraw();
	}

	Ref<InputEventMouseButton> mb = p_event;
	if (mb.is_valid() && mb->get_button_index() == MouseButton::LEFT) {
		if (mb->is_pressed()) { // Pressed
			// Left click pressed.
			if (!mb->is_shift_pressed()) {
				tile_set_selection.clear();
			}

			if (coords != TileSetSource::INVALID_ATLAS_COORDS && alternative != TileSetAtlasSource::INVALID_TILE_ALTERNATIVE) {
				if (mb->is_shift_pressed() && tile_set_selection.has(TileMapCell(source_id, hovered_tile.get_atlas_coords(), hovered_tile.alternative_tile))) {
					tile_set_selection.erase(TileMapCell(source_id, hovered_tile.get_atlas_coords(), hovered_tile.alternative_tile));
				} else {
					tile_set_selection.insert(TileMapCell(source_id, hovered_tile.get_atlas_coords(), hovered_tile.alternative_tile));
				}
			}
			_update_selection_pattern_from_tileset_tiles_selection();
		}
		tile_atlas_control->queue_redraw();
		alternative_tiles_control->queue_redraw();
	}
}

void TileMapLayer25DEditorTilesPlugin::_set_tile_map_selection(const TypedArray<Vector2i> &p_selection) {
	tile_map_selection.clear();
	for (int i = 0; i < p_selection.size(); i++) {
		tile_map_selection.insert(p_selection[i]);
	}
	_update_selection_pattern_from_tilemap_selection();
	_update_tileset_selection_from_selection_pattern();
	CanvasItemEditor::get_singleton()->update_viewport();
}

TypedArray<Vector2i> TileMapLayer25DEditorTilesPlugin::_get_tile_map_selection() const {
	TypedArray<Vector2i> output;
	for (const Vector2i &E : tile_map_selection) {
		output.push_back(E);
	}
	return output;
}

void TileMapLayer25DEditorTilesPlugin::_set_source_sort(int p_sort) {
	for (int i = 0; i != TilesEditorUtils::SOURCE_SORT_MAX; i++) {
		source_sort_button->get_popup()->set_item_checked(i, (i == (int)p_sort));
	}
	TilesEditorUtils::get_singleton()->set_sorting_option(p_sort);
	_update_tile_set_sources_list();
	EditorSettings::get_singleton()->set_project_metadata("editor_metadata", "tile_source_sort", p_sort);
}

void TileMapLayer25DEditorTilesPlugin::_bind_methods() {
	ClassDB::bind_method(D_METHOD("_set_tile_map_selection", "selection"), &TileMapLayer25DEditorTilesPlugin::_set_tile_map_selection);
	ClassDB::bind_method(D_METHOD("_get_tile_map_selection"), &TileMapLayer25DEditorTilesPlugin::_get_tile_map_selection);
}

void TileMapLayer25DEditorTilesPlugin::edit(ObjectID p_tile_map_layer_id) {
	_stop_dragging(); // Avoids staying in a wrong drag state.

	// Disable sort button if the tileset is read-only
	TileMapLayer25D *edited_layer = _get_edited_layer();
	Ref<TileSet> tile_set;
	if (edited_layer) {
		tile_set = edited_layer->get_tile_set();
		if (tile_set.is_valid()) {
			source_sort_button->set_disabled(EditorNode::get_singleton()->is_resource_read_only(tile_set));
		}
	}

	TileMapLayer25D *new_tile_map_layer = ObjectDB::get_instance<TileMapLayer25D>(p_tile_map_layer_id);
	Ref<TileSet> new_tile_set;
	if (new_tile_map_layer) {
		new_tile_set = new_tile_map_layer->get_tile_set();
	}

	if (tile_set.is_valid() && tile_set != new_tile_set) {
		// Clear the selection.
		tile_set_selection.clear();
		patterns_item_list->deselect_all();
		tile_map_selection.clear();
		selection_pattern.instantiate();
	}

	edited_tile_map_layer_id = p_tile_map_layer_id;
}

void TileMapLayer25DEditorTilesPlugin::update_layout(EditorDock::DockLayout p_layout) {
	bool is_vertical = (p_layout == EditorDock::DockLayout::DOCK_LAYOUT_VERTICAL);
	atlas_sources_split_container->set_vertical(is_vertical);
	atlas_sources_split_container->move_child(split_container_left_side, is_vertical ? -1 : 0);
	split_container_left_side->set_vertical(!is_vertical);

	tilemap_tiles_tools_buttons->set_vertical(is_vertical);
	transform_toolbar->set_vertical(is_vertical);
	tools_settings->set_vertical(is_vertical);
	tools_settings_vsep->set_vertical(is_vertical);
	transform_separator->set_vertical(is_vertical);

	wide_toolbar->set_visible(is_vertical);
	bucket_contiguous_checkbox->reparent(is_vertical ? wide_toolbar : tools_settings);
	scatter_controls_container->reparent(is_vertical ? wide_toolbar : tools_settings);

	if (p_layout == EditorDock::DockLayout::DOCK_LAYOUT_FLOATING) {
		patterns_mc->set_theme_type_variation("NoBorderHorizontalBottom");
		patterns_item_list->set_scroll_hint_mode(ItemList::SCROLL_HINT_MODE_TOP);
	} else {
		patterns_mc->set_theme_type_variation(is_vertical ? "" : "NoBorderBottomPanel");
		patterns_item_list->set_scroll_hint_mode(is_vertical ? ItemList::SCROLL_HINT_MODE_DISABLED : ItemList::SCROLL_HINT_MODE_BOTH);
	}
	patterns_item_list->set_theme_type_variation(is_vertical ? "ItemListSecondary" : "");
}

void TileMapLayer25DEditorTilesPlugin::update_z_level(int16_t p_z) {
	edited_z = p_z;
}

TileMapLayer25DEditorTilesPlugin::TileMapLayer25DEditorTilesPlugin() {
	CanvasItemEditor::get_singleton()
			->get_viewport_control()
			->connect(SceneStringName(mouse_exited), callable_mp(this, &TileMapLayer25DEditorTilesPlugin::_mouse_exited_viewport));

	// --- Initialize references ---
	tile_map_clipboard.instantiate();
	selection_pattern.instantiate();

	erase_pattern.instantiate();
	erase_pattern->set_cell(Vector2i(0, 0), TileSet::INVALID_SOURCE, TileSetSource::INVALID_ATLAS_COORDS, TileSetSource::INVALID_TILE_ALTERNATIVE);

	// --- Toolbar ---
	wide_toolbar = memnew(HBoxContainer);

	tilemap_tiles_tools_buttons = memnew(BoxContainer);

	tool_buttons_group.instantiate();

	select_tool_button = memnew(Button);
	select_tool_button->set_theme_type_variation(SceneStringName(FlatButton));
	select_tool_button->set_toggle_mode(true);
	select_tool_button->set_button_group(tool_buttons_group);
	select_tool_button->set_shortcut(ED_SHORTCUT("tiles_editor/selection_tool", TTRC("Selection Tool"), Key::S));
	select_tool_button->connect(SceneStringName(pressed), callable_mp(this, &TileMapLayer25DEditorTilesPlugin::_update_toolbar));
	select_tool_button->set_accessibility_name(TTRC("Selection Tool"));
	tilemap_tiles_tools_buttons->add_child(select_tool_button);
	viewport_shortcut_buttons.push_back(select_tool_button);

	paint_tool_button = memnew(Button);
	paint_tool_button->set_theme_type_variation(SceneStringName(FlatButton));
	paint_tool_button->set_toggle_mode(true);
	paint_tool_button->set_button_group(tool_buttons_group);
	paint_tool_button->set_shortcut(ED_GET_SHORTCUT("tiles_editor/paint_tool"));
	paint_tool_button->set_tooltip_auto_translate_mode(Node::AUTO_TRANSLATE_MODE_DISABLED);
	paint_tool_button->connect(SceneStringName(pressed), callable_mp(this, &TileMapLayer25DEditorTilesPlugin::_update_toolbar));
	paint_tool_button->set_accessibility_name(TTRC("Paint Tool"));
	tilemap_tiles_tools_buttons->add_child(paint_tool_button);
	viewport_shortcut_buttons.push_back(paint_tool_button);

	line_tool_button = memnew(Button);
	line_tool_button->set_theme_type_variation(SceneStringName(FlatButton));
	line_tool_button->set_toggle_mode(true);
	line_tool_button->set_button_group(tool_buttons_group);
	// TRANSLATORS: This refers to the line tool in the tilemap editor.
	line_tool_button->set_shortcut(ED_GET_SHORTCUT("tiles_editor/line_tool"));
	line_tool_button->connect(SceneStringName(pressed), callable_mp(this, &TileMapLayer25DEditorTilesPlugin::_update_toolbar));
	line_tool_button->set_accessibility_name(TTRC("Line Tool"));
	tilemap_tiles_tools_buttons->add_child(line_tool_button);
	viewport_shortcut_buttons.push_back(line_tool_button);

	rect_tool_button = memnew(Button);
	rect_tool_button->set_theme_type_variation(SceneStringName(FlatButton));
	rect_tool_button->set_toggle_mode(true);
	rect_tool_button->set_button_group(tool_buttons_group);
	rect_tool_button->set_shortcut(ED_GET_SHORTCUT("tiles_editor/rect_tool"));
	rect_tool_button->connect(SceneStringName(pressed), callable_mp(this, &TileMapLayer25DEditorTilesPlugin::_update_toolbar));
	rect_tool_button->set_accessibility_name(TTRC("Rect Tool"));
	tilemap_tiles_tools_buttons->add_child(rect_tool_button);
	viewport_shortcut_buttons.push_back(rect_tool_button);

	bucket_tool_button = memnew(Button);
	bucket_tool_button->set_theme_type_variation(SceneStringName(FlatButton));
	bucket_tool_button->set_toggle_mode(true);
	bucket_tool_button->set_button_group(tool_buttons_group);
	bucket_tool_button->set_shortcut(ED_GET_SHORTCUT("tiles_editor/bucket_tool"));
	bucket_tool_button->connect(SceneStringName(pressed), callable_mp(this, &TileMapLayer25DEditorTilesPlugin::_update_toolbar));
	bucket_tool_button->set_accessibility_name(TTRC("Bucket Tool"));
	tilemap_tiles_tools_buttons->add_child(bucket_tool_button);
	viewport_shortcut_buttons.push_back(bucket_tool_button);

	// -- TileMap tool settings --
	tools_settings = memnew(BoxContainer);
	tools_settings_vsep = memnew(SwitchSeparator);
	tools_settings_vsep->set_vertical(false);

	// Picker
	picker_button = memnew(Button);
	picker_button->set_theme_type_variation(SceneStringName(FlatButton));
	picker_button->set_toggle_mode(true);
	picker_button->set_shortcut(ED_GET_SHORTCUT("tiles_editor/picker"));
	Key key = OS::prefer_meta_over_ctrl() ? Key::META : Key::CTRL;
	picker_button->set_tooltip_text(vformat(TTRC("Alternatively hold %s with other tools to pick tile."), find_keycode_name(key)));
	picker_button->connect(SceneStringName(pressed), callable_mp(CanvasItemEditor::get_singleton(), &CanvasItemEditor::update_viewport));
	picker_button->set_accessibility_name(TTRC("Pick"));
	tools_settings->add_child(picker_button);
	viewport_shortcut_buttons.push_back(picker_button);

	// Erase button.
	erase_button = memnew(Button);
	erase_button->set_theme_type_variation(SceneStringName(FlatButton));
	erase_button->set_toggle_mode(true);
	erase_button->set_shortcut(ED_GET_SHORTCUT("tiles_editor/eraser"));
	erase_button->set_tooltip_text(TTRC("Alternatively use RMB to erase tiles."));
	erase_button->connect(SceneStringName(pressed), callable_mp(CanvasItemEditor::get_singleton(), &CanvasItemEditor::update_viewport));
	erase_button->set_accessibility_name(TTRC("Erase"));
	tools_settings->add_child(erase_button);
	viewport_shortcut_buttons.push_back(erase_button);

	// Transform toolbar.
	transform_toolbar = memnew(BoxContainer);
	tools_settings->add_child(transform_toolbar);
	transform_separator = memnew(SwitchSeparator);
	transform_separator->set_vertical(false);
	transform_toolbar->add_child(transform_separator);

	transform_button_rotate_left = memnew(Button);
	transform_button_rotate_left->set_theme_type_variation(SceneStringName(FlatButton));
	transform_button_rotate_left->set_shortcut(ED_SHORTCUT("tiles_editor/rotate_tile_left", TTRC("Rotate Tile Left"), Key::Z));
	transform_toolbar->add_child(transform_button_rotate_left);
	transform_button_rotate_left->connect(SceneStringName(pressed), callable_mp(this, &TileMapLayer25DEditorTilesPlugin::_apply_transform).bind(TRANSFORM_ROTATE_LEFT));
	transform_button_rotate_left->set_accessibility_name(TTRC("Rotate Tile Left"));
	viewport_shortcut_buttons.push_back(transform_button_rotate_left);

	transform_button_rotate_right = memnew(Button);
	transform_button_rotate_right->set_theme_type_variation(SceneStringName(FlatButton));
	transform_button_rotate_right->set_shortcut(ED_SHORTCUT("tiles_editor/rotate_tile_right", TTRC("Rotate Tile Right"), Key::X));
	transform_toolbar->add_child(transform_button_rotate_right);
	transform_button_rotate_right->connect(SceneStringName(pressed), callable_mp(this, &TileMapLayer25DEditorTilesPlugin::_apply_transform).bind(TRANSFORM_ROTATE_RIGHT));
	transform_button_rotate_right->set_accessibility_name(TTRC("Rotate Tile Right"));
	viewport_shortcut_buttons.push_back(transform_button_rotate_right);

	transform_button_flip_h = memnew(Button);
	transform_button_flip_h->set_theme_type_variation(SceneStringName(FlatButton));
	transform_button_flip_h->set_shortcut(ED_SHORTCUT("tiles_editor/flip_tile_horizontal", TTRC("Flip Tile Horizontally"), Key::C));
	transform_toolbar->add_child(transform_button_flip_h);
	transform_button_flip_h->connect(SceneStringName(pressed), callable_mp(this, &TileMapLayer25DEditorTilesPlugin::_apply_transform).bind(TRANSFORM_FLIP_H));
	transform_button_flip_h->set_accessibility_name(TTRC("Flip Tile Horizontally"));
	viewport_shortcut_buttons.push_back(transform_button_flip_h);

	transform_button_flip_v = memnew(Button);
	transform_button_flip_v->set_theme_type_variation(SceneStringName(FlatButton));
	transform_button_flip_v->set_shortcut(ED_SHORTCUT("tiles_editor/flip_tile_vertical", TTRC("Flip Tile Vertically"), Key::V));
	transform_toolbar->add_child(transform_button_flip_v);
	transform_button_flip_v->connect(SceneStringName(pressed), callable_mp(this, &TileMapLayer25DEditorTilesPlugin::_apply_transform).bind(TRANSFORM_FLIP_V));
	transform_button_flip_v->set_accessibility_name(TTRC("Flip Tile Vertically"));
	viewport_shortcut_buttons.push_back(transform_button_flip_v);

	// Continuous checkbox.
	bucket_contiguous_checkbox = memnew(CheckBox);
	bucket_contiguous_checkbox->set_flat(true);
	bucket_contiguous_checkbox->set_text(TTRC("Contiguous"));
	bucket_contiguous_checkbox->set_pressed(true);
	bucket_contiguous_checkbox->hide();
	tools_settings->add_child(bucket_contiguous_checkbox);

	// Random tile checkbox.
	random_tile_toggle = memnew(Button);
	random_tile_toggle->set_theme_type_variation(SceneStringName(FlatButton));
	random_tile_toggle->set_toggle_mode(true);
	random_tile_toggle->set_tooltip_text(TTRC("Place Random Tile"));
	random_tile_toggle->connect(SceneStringName(toggled), callable_mp(this, &TileMapLayer25DEditorTilesPlugin::_on_random_tile_checkbox_toggled));
	tools_settings->add_child(random_tile_toggle);

	// Random tile scattering.
	scatter_controls_container = memnew(BoxContainer);
	scatter_controls_container->set_vertical(false);

	scatter_label = memnew(Label);
	scatter_label->set_tooltip_text(TTRC("Modifies the chance of painting nothing instead of a randomly selected tile."));
	scatter_label->set_text(TTRC("Scattering:"));
	scatter_controls_container->add_child(scatter_label);

	scatter_spinbox = memnew(SpinBox);
	scatter_spinbox->set_min(0.0);
	scatter_spinbox->set_max(1000);
	scatter_spinbox->set_step(0.001);
	scatter_spinbox->set_tooltip_text(TTRC("Modifies the chance of painting nothing instead of a randomly selected tile."));
	scatter_spinbox->get_line_edit()->add_theme_constant_override("minimum_character_width", 4);
	scatter_spinbox->connect(SceneStringName(value_changed), callable_mp(this, &TileMapLayer25DEditorTilesPlugin::_on_scattering_spinbox_changed));
	scatter_spinbox->set_accessibility_name(TTRC("Scattering:"));
	scatter_controls_container->add_child(scatter_spinbox);
	tools_settings->add_child(scatter_controls_container);

	_on_random_tile_checkbox_toggled(false);

	// Default tool.
	paint_tool_button->set_pressed(true);
	_update_toolbar();

	// --- Bottom panel tiles ---
	tiles_bottom_panel = memnew(VBoxContainer);
	// FIXME: This can trigger theme updates when the nodes that we want to update are not yet available.
	// The toolbar should be extracted to a dedicated control and theme updates should be handled through
	// the notification.
	tiles_bottom_panel->connect(SceneStringName(theme_changed), callable_mp(this, &TileMapLayer25DEditorTilesPlugin::_update_theme));
	tiles_bottom_panel->connect(SceneStringName(visibility_changed), callable_mp(this, &TileMapLayer25DEditorTilesPlugin::_stop_dragging));
	tiles_bottom_panel->connect(SceneStringName(visibility_changed), callable_mp(this, &TileMapLayer25DEditorTilesPlugin::_tab_changed));
	tiles_bottom_panel->set_name(TTRC("Tiles"));

	missing_source_label = memnew(Label);
	missing_source_label->set_focus_mode(Control::FOCUS_ACCESSIBILITY);
	missing_source_label->set_text(TTRC("This TileMap's TileSet has no Tile Source configured. Go to the TileSet bottom panel to add one."));
	missing_source_label->set_autowrap_mode(TextServer::AUTOWRAP_WORD_SMART);
	missing_source_label->set_h_size_flags(Control::SIZE_EXPAND_FILL);
	missing_source_label->set_v_size_flags(Control::SIZE_EXPAND_FILL);
	missing_source_label->set_horizontal_alignment(HORIZONTAL_ALIGNMENT_CENTER);
	missing_source_label->set_vertical_alignment(VERTICAL_ALIGNMENT_CENTER);
	missing_source_label->hide();
	tiles_bottom_panel->add_child(missing_source_label);

	atlas_sources_split_container = memnew(SplitContainer);
	atlas_sources_split_container->set_vertical(false);
	atlas_sources_split_container->set_h_size_flags(Control::SIZE_EXPAND_FILL);
	atlas_sources_split_container->set_v_size_flags(Control::SIZE_EXPAND_FILL);
	tiles_bottom_panel->add_child(atlas_sources_split_container);

	split_container_left_side = memnew(BoxContainer);
	split_container_left_side->set_vertical(true);
	split_container_left_side->set_h_size_flags(Control::SIZE_EXPAND_FILL);
	split_container_left_side->set_v_size_flags(Control::SIZE_EXPAND_FILL);
	split_container_left_side->set_stretch_ratio(0.25);
	split_container_left_side->set_custom_minimum_size(Size2(70, 0) * EDSCALE);
	atlas_sources_split_container->add_child(split_container_left_side);

	source_sort_button = memnew(MenuButton);
	source_sort_button->set_flat(false);
	source_sort_button->set_theme_type_variation("FlatMenuButton");
	source_sort_button->set_tooltip_text(TTRC("Sort sources"));
	source_sort_button->set_h_size_flags(Control::SIZE_SHRINK_END);
	source_sort_button->set_v_size_flags(Control::SIZE_SHRINK_END);

	PopupMenu *p = source_sort_button->get_popup();
	p->connect(SceneStringName(id_pressed), callable_mp(this, &TileMapLayer25DEditorTilesPlugin::_set_source_sort));
	p->add_radio_check_item(TTRC("Sort by ID (Ascending)"), TilesEditorUtils::SOURCE_SORT_ID);
	p->add_radio_check_item(TTRC("Sort by ID (Descending)"), TilesEditorUtils::SOURCE_SORT_ID_REVERSE);
	p->add_radio_check_item(TTRC("Sort by Name (Ascending)"), TilesEditorUtils::SOURCE_SORT_NAME);
	p->add_radio_check_item(TTRC("Sort by Name (Descending)"), TilesEditorUtils::SOURCE_SORT_NAME_REVERSE);
	p->set_item_checked(TilesEditorUtils::SOURCE_SORT_ID, true);

	sources_list = memnew(TileSetSourceItemList);
	sources_list->connect(SceneStringName(item_selected), callable_mp(this, &TileMapLayer25DEditorTilesPlugin::_update_source_display).unbind(1));
	sources_list->connect(SceneStringName(item_selected), callable_mp(TilesEditorUtils::get_singleton(), &TilesEditorUtils::set_sources_lists_current));
	sources_list->connect("item_activated", callable_mp(TilesEditorUtils::get_singleton(), &TilesEditorUtils::display_tile_set_editor_panel).unbind(1));
	sources_list->connect(SceneStringName(visibility_changed), callable_mp(TilesEditorUtils::get_singleton(), &TilesEditorUtils::synchronize_sources_list).bind(sources_list, source_sort_button));
	sources_list->connect("sort_request", callable_mp(this, &TileMapLayer25DEditorTilesPlugin::_update_tile_set_sources_list));
	split_container_left_side->add_child(sources_list);
	split_container_left_side->add_child(source_sort_button);

	// Tile atlas source.
	tile_atlas_view = memnew(TileAtlasView);
	tile_atlas_view->set_h_size_flags(Control::SIZE_EXPAND_FILL);
	tile_atlas_view->set_v_size_flags(Control::SIZE_EXPAND_FILL);
	tile_atlas_view->set_texture_grid_visible(false);
	tile_atlas_view->set_tile_shape_grid_visible(false);
	tile_atlas_view->connect("transform_changed", callable_mp(TilesEditorUtils::get_singleton(), &TilesEditorUtils::set_atlas_view_transform));
	atlas_sources_split_container->add_child(tile_atlas_view);

	tile_atlas_control = memnew(Control);
	tile_atlas_control->connect(SceneStringName(draw), callable_mp(this, &TileMapLayer25DEditorTilesPlugin::_tile_atlas_control_draw));
	tile_atlas_control->connect(SceneStringName(mouse_exited), callable_mp(this, &TileMapLayer25DEditorTilesPlugin::_tile_atlas_control_mouse_exited));
	tile_atlas_control->connect(SceneStringName(gui_input), callable_mp(this, &TileMapLayer25DEditorTilesPlugin::_tile_atlas_control_gui_input));
	tile_atlas_view->add_control_over_atlas_tiles(tile_atlas_control);

	alternative_tiles_control = memnew(Control);
	alternative_tiles_control->connect(SceneStringName(draw), callable_mp(this, &TileMapLayer25DEditorTilesPlugin::_tile_alternatives_control_draw));
	alternative_tiles_control->connect(SceneStringName(mouse_exited), callable_mp(this, &TileMapLayer25DEditorTilesPlugin::_tile_alternatives_control_mouse_exited));
	alternative_tiles_control->connect(SceneStringName(gui_input), callable_mp(this, &TileMapLayer25DEditorTilesPlugin::_tile_alternatives_control_gui_input));
	tile_atlas_view->add_control_over_alternative_tiles(alternative_tiles_control);

	// Scenes collection source.
	scene_tiles_list = memnew(ItemList);
	scene_tiles_list->set_auto_translate_mode(Node::AUTO_TRANSLATE_MODE_DISABLED);
	scene_tiles_list->set_h_size_flags(Control::SIZE_EXPAND_FILL);
	scene_tiles_list->set_v_size_flags(Control::SIZE_EXPAND_FILL);
	scene_tiles_list->set_select_mode(ItemList::SELECT_MULTI);
	//scene_tiles_list->connect("multi_selected", callable_mp(this, &TileMapLayer25DEditorTilesPlugin::_scenes_list_multi_selected));
	//scene_tiles_list->connect("empty_clicked", callable_mp(this, &TileMapLayer25DEditorTilesPlugin::_scenes_list_lmb_empty_clicked));
	scene_tiles_list->set_texture_filter(CanvasItem::TEXTURE_FILTER_NEAREST);
	atlas_sources_split_container->add_child(scene_tiles_list);

	// Invalid source label.
	invalid_source_label = memnew(Label);
	invalid_source_label->set_focus_mode(Control::FOCUS_ACCESSIBILITY);
	invalid_source_label->set_text(TTRC("Invalid source selected."));
	invalid_source_label->set_h_size_flags(Control::SIZE_EXPAND_FILL);
	invalid_source_label->set_v_size_flags(Control::SIZE_EXPAND_FILL);
	invalid_source_label->set_horizontal_alignment(HORIZONTAL_ALIGNMENT_CENTER);
	invalid_source_label->set_vertical_alignment(VERTICAL_ALIGNMENT_CENTER);
	invalid_source_label->hide();
	atlas_sources_split_container->add_child(invalid_source_label);

	// --- Bottom panel patterns ---
	patterns_mc = memnew(MarginContainer);
	patterns_mc->set_name(TTRC("Patterns"));
	patterns_mc->connect(SceneStringName(visibility_changed), callable_mp(this, &TileMapLayer25DEditorTilesPlugin::_tab_changed));

	int thumbnail_size = 64;
	patterns_item_list = memnew(ItemList);
	patterns_item_list->set_auto_translate_mode(Node::AUTO_TRANSLATE_MODE_DISABLED);
	patterns_item_list->set_max_columns(0);
	patterns_item_list->set_icon_mode(ItemList::ICON_MODE_TOP);
	patterns_item_list->set_fixed_column_width(thumbnail_size * 3 / 2);
	patterns_item_list->set_max_text_lines(2);
	patterns_item_list->set_fixed_icon_size(Size2(thumbnail_size, thumbnail_size));
	patterns_item_list->set_scroll_hint_mode(ItemList::SCROLL_HINT_MODE_BOTH);
	patterns_item_list->connect(SceneStringName(gui_input), callable_mp(this, &TileMapLayer25DEditorTilesPlugin::_patterns_item_list_gui_input));
	patterns_item_list->connect(SceneStringName(item_selected), callable_mp(this, &TileMapLayer25DEditorTilesPlugin::_update_selection_pattern_from_tileset_pattern_selection).unbind(1));
	patterns_item_list->connect("item_activated", callable_mp(this, &TileMapLayer25DEditorTilesPlugin::_update_selection_pattern_from_tileset_pattern_selection).unbind(1));
	patterns_item_list->connect("empty_clicked", callable_mp(this, &TileMapLayer25DEditorTilesPlugin::patterns_item_list_empty_clicked));
	patterns_mc->add_child(patterns_item_list);

	patterns_help_label = memnew(Label);
	patterns_help_label->set_focus_mode(Control::FOCUS_ACCESSIBILITY);
	patterns_help_label->set_text(TTRC("Drag and drop or paste a TileMap selection here to store a pattern."));
	patterns_help_label->set_auto_translate_mode(Node::AUTO_TRANSLATE_MODE_ALWAYS);
	patterns_help_label->set_autowrap_mode(TextServer::AUTOWRAP_WORD_SMART);
	patterns_help_label->set_horizontal_alignment(HORIZONTAL_ALIGNMENT_CENTER);
	patterns_help_label->set_anchors_and_offsets_preset(Control::PRESET_HCENTER_WIDE);
	patterns_item_list->add_child(patterns_help_label);

	// Update.
	_update_source_display();

	_update_translation();
	EditorSettings::get_singleton()->connect("_translation_changed", callable_mp(this, &TileMapLayer25DEditorTilesPlugin::_update_translation));
}

TileMapLayer25D *TileMapLayer25DEditor::_get_edited_layer() const {
	return ObjectDB::get_instance<TileMapLayer25D>(edited_tile_map_layer_id);
}

void TileMapLayer25DEditor::_find_tile_map_layers_in_scene(Node *p_current, const Node *p_owner, Vector<TileMapLayer25D *> &r_list) const {
	ERR_FAIL_COND(!p_current || !p_owner);

	if (p_current != p_owner) {
		if (!p_current->get_owner()) {
			return;
		}
		if (p_current->get_owner() != p_owner && !p_owner->is_editable_instance(p_current->get_owner())) {
			return;
		}
	}

	TileMapLayer25D *layer = Object::cast_to<TileMapLayer25D>(p_current);
	if (layer) {
		r_list.append(layer);
	}
	for (int i = 0; i < p_current->get_child_count(); i++) {
		Node *child = p_current->get_child(i);
		_find_tile_map_layers_in_scene(child, p_owner, r_list);
	}
}

void TileMapLayer25DEditor::_update_tile_map_layers_in_scene_list_cache() {
	if (!layers_in_scene_list_cache_needs_update) {
		return;
	}
	EditorNode *en = EditorNode::get_singleton();
	Node *edited_scene_root = en->get_edited_scene();
	if (!edited_scene_root) {
		return;
	}

	tile_map_layers_in_scene_cache.clear();
	_find_tile_map_layers_in_scene(edited_scene_root, edited_scene_root, tile_map_layers_in_scene_cache);
	layers_in_scene_list_cache_needs_update = false;
}

void TileMapLayer25DEditor::_node_change(Node *p_node) {
	if (!layers_in_scene_list_cache_needs_update && p_node->is_part_of_edited_scene() && Object::cast_to<TileMapLayer25D>(p_node)) {
		layers_in_scene_list_cache_needs_update = true;
	}
}

void TileMapLayer25DEditor::_notification(int p_what) {
	switch (p_what) {
		case NOTIFICATION_READY: {
			toggle_grid_button->set_pressed_no_signal(EDITOR_GET("editors/tiles_editor/display_grid"));
			toggle_highlight_selected_layer_button->set_pressed_no_signal(EDITOR_GET("editors/tiles_editor/highlight_selected_layer"));
		} break;

		case NOTIFICATION_ENTER_TREE: {
			get_tree()->connect("node_added", callable_mp(this, &TileMapLayer25DEditor::_node_change));
			get_tree()->connect("node_removed", callable_mp(this, &TileMapLayer25DEditor::_node_change));
		} break;

		case NOTIFICATION_EXIT_TREE: {
			get_tree()->disconnect("node_added", callable_mp(this, &TileMapLayer25DEditor::_node_change));
			get_tree()->disconnect("node_removed", callable_mp(this, &TileMapLayer25DEditor::_node_change));
		} break;

		case NOTIFICATION_VISIBILITY_CHANGED: {
			if (custom_overlay) {
				custom_overlay->set_visible(is_visible_in_tree());
			}
			if (is_visible()) {
				// Fix: Don't change the tool if we are in scene paint mode.
				if (CanvasItemEditor::get_singleton()->get_current_tool() != CanvasItemEditor::TOOL_SCENE_PAINT) {
					CanvasItemEditor::get_singleton()->set_current_tool(CanvasItemEditor::TOOL_SELECT);
				}
			}
		} break;

		case NOTIFICATION_THEME_CHANGED: {
			missing_tile_texture = get_editor_theme_icon(SNAME("StatusWarning"));
			warning_pattern_texture = get_editor_theme_icon(SNAME("WarningPattern"));
			advanced_menu_button->set_button_icon(get_editor_theme_icon(SNAME("Tools")));
			select_previous_layer->set_button_icon(get_editor_theme_icon(SNAME("MoveUp")));
			select_next_layer->set_button_icon(get_editor_theme_icon(SNAME("MoveDown")));
			select_all_layers->set_button_icon(get_editor_theme_icon(SNAME("FileList")));
			toggle_grid_button->set_button_icon(get_editor_theme_icon(SNAME("Grid")));
			toggle_highlight_selected_z_level_button->set_button_icon(get_editor_theme_icon(SNAME("TileMapHighlightSelected")));
			toggle_highlight_selected_layer_button->set_button_icon(get_editor_theme_icon(SNAME("TileMapHighlightSelected")));
		} break;

		case NOTIFICATION_INTERNAL_PROCESS: {
			if (is_visible_in_tree() && tile_map_layer_changed_needs_update) {
				_update_bottom_panel();
				tabs_plugins[tabs_bar->get_current_tab()]->tile_set_changed();

				const TileMapLayer25D *edited_layer = _get_edited_layer();
				if (edited_layer && custom_overlay) {
					custom_overlay->set_texture_filter(edited_layer->get_texture_filter_in_tree());
				}

				CanvasItemEditor::get_singleton()->update_viewport();
				tile_map_layer_changed_needs_update = false;
			}
		} break;

		case EditorSettings::NOTIFICATION_EDITOR_SETTINGS_CHANGED: {
			if (EditorSettings::get_singleton()->check_changed_settings_in_group("editors/tiles_editor")) {
				toggle_grid_button->set_pressed_no_signal(EDITOR_GET("editors/tiles_editor/display_grid"));
				toggle_highlight_selected_z_level_button->set_pressed_no_signal(EDITOR_GET("editors/tiles_editor/highlight_selected_layer"));
				toggle_highlight_selected_layer_button->set_pressed_no_signal(EDITOR_GET("editors/tiles_editor/highlight_selected_layer"));
			}
		} break;

		case NOTIFICATION_APPLICATION_FOCUS_OUT: {
			// Simulate mouse released event to stop drawing when editor focus exits.
			Ref<InputEventMouseButton> release;
			release.instantiate();
			release->set_button_index(MouseButton::LEFT);
			forward_canvas_gui_input(release);
		} break;
	}
}

void TileMapLayer25DEditor::_on_grid_toggled(bool p_pressed) {
	EditorSettings::get_singleton()->set("editors/tiles_editor/display_grid", p_pressed);
	CanvasItemEditor::get_singleton()->update_viewport();
}

void TileMapLayer25DEditor::_z_level_changed(float p_value) {
	edited_z = (int16_t)p_value;
	
	for (TileMapLayer25DSubEditorPlugin *tab_plugin : tabs_plugins) {
		tab_plugin->update_z_level(edited_z);
	}
	
	TileMapLayer25D *edited_layer = _get_edited_layer();
	if(edited_layer) {
		edited_layer->set_meta("_editor_z_", edited_z);
	}
	//TODO update_grit(); _update_selection_transform();
}

void TileMapLayer25DEditor::_z_level_mouse_exited() {
	z_level_spinner->get_line_edit()->release_focus();
}

void TileMapLayer25DEditor::_select_previous_layer_pressed() {
	_layers_select_next_or_previous(false);
}

void TileMapLayer25DEditor::_select_next_layer_pressed() {
	_layers_select_next_or_previous(true);
}

void TileMapLayer25DEditor::_select_all_layers_pressed() {
	EditorNode *en = EditorNode::get_singleton();
	Node *edited_scene_root = en->get_edited_scene();
	ERR_FAIL_NULL(edited_scene_root);

	en->get_editor_selection()->clear();
	if (tile_map_layers_in_scene_cache.size() == 1) {
		en->edit_node(tile_map_layers_in_scene_cache[0]);
		en->get_editor_selection()->add_node(tile_map_layers_in_scene_cache[0]);
	} else {
		_update_tile_map_layers_in_scene_list_cache();
		Ref<MultiNodeEdit> multi_node_edit = memnew(MultiNodeEdit);
		for (TileMapLayer25D *layer : tile_map_layers_in_scene_cache) {
			multi_node_edit->add_node(edited_scene_root->get_path_to(layer));
			en->get_editor_selection()->add_node(layer);
		}
		en->push_item(multi_node_edit.ptr());
	}
}

void TileMapLayer25DEditor::_clear_all_layers_highlighting() {
	// Note: This function might be removed if we remove the TileMap node at some point.
	// All processing could be done in _update_all_layers_highlighting otherwise.
	TileMapLayer25D *edited_layer = _get_edited_layer();
	_update_tile_map_layers_in_scene_list_cache();
	for (TileMapLayer25D *layer : tile_map_layers_in_scene_cache) {
		layer->set_highlight_mode(TileMapLayer25D::HIGHLIGHT_MODE_DEFAULT);
	}
}

void TileMapLayer25DEditor::_update_all_layers_highlighting() {
	EditorNode *en = EditorNode::get_singleton();
	Node *edited_scene_root = en->get_edited_scene();
	if (!edited_scene_root) {
		return;
	}

	// Get selected layer.
	TileMapLayer25D *edited_layer = _get_edited_layer();

	bool highlight_selected_layer = EDITOR_GET("editors/tiles_editor/highlight_selected_layer");
	if (edited_layer && highlight_selected_layer) {
		int edited_z_index = edited_layer->get_z_index();

		_update_tile_map_layers_in_scene_list_cache();
			bool passed = false;
			for (TileMapLayer25D *layer : tile_map_layers_in_scene_cache) {
				if (layer == edited_layer) {
					passed = true;
					layer->set_highlight_mode(TileMapLayer25D::HIGHLIGHT_MODE_DEFAULT);
				} else {
					if (passed || layer->get_z_index() > edited_z_index) {
						layer->set_highlight_mode(TileMapLayer25D::HIGHLIGHT_MODE_ABOVE);
					} else {
						layer->set_highlight_mode(TileMapLayer25D::HIGHLIGHT_MODE_BELOW);
					}
				}
			}
	}
}

void TileMapLayer25DEditor::_highlight_selected_z_level_button_toggled(bool p_pressed) {
	TileMapLayer25D *edited_layer = _get_edited_layer();
	if (!edited_layer) {
		return;
	}
}

void TileMapLayer25DEditor::_highlight_selected_layer_button_toggled(bool p_pressed) {
	TileMapLayer25D *edited_layer = _get_edited_layer();
	if (!edited_layer) {
		return;
	}

	EditorSettings::get_singleton()->set("editors/tiles_editor/highlight_selected_layer", p_pressed);
	if (p_pressed) {
		_update_all_layers_highlighting();
	} else {
		_clear_all_layers_highlighting();
	}
}

void TileMapLayer25DEditor::_advanced_menu_button_id_pressed(int p_id) {
	TileMapLayer25D *edited_layer = _get_edited_layer();
	if (!edited_layer) {
		return;
	}

	Ref<TileSet> tile_set = edited_layer->get_tile_set();
	if (tile_set.is_null()) {
		return;
	}

	if (p_id == ADVANCED_MENU_REPLACE_WITH_PROXIES) { // Replace Tile Proxies
		EditorUndoRedoManager *undo_redo = EditorUndoRedoManager::get_singleton();
		undo_redo->create_action(TTRC("Replace Tiles with Proxies"));
		TypedArray<Vector2i> used_cells = edited_layer->get_used_cells();
		for (int i = 0; i < used_cells.size(); i++) {
			Vector2i cell_coords = used_cells[i];
			TileMapCell from = edited_layer->get_cell(cell_coords, edited_z);
			Array to_array = tile_set->map_tile_proxy(from.source_id, from.get_atlas_coords(), from.alternative_tile);
			TileMapCell to;
			to.source_id = to_array[0];
			to.set_atlas_coords(to_array[1]);
			to.alternative_tile = to_array[2];
			if (from != to) {
				undo_redo->add_do_method(edited_layer, "set_cell", cell_coords, to.source_id, to.get_atlas_coords(), to.alternative_tile);
				undo_redo->add_undo_method(edited_layer, "set_cell", cell_coords, from.source_id, from.get_atlas_coords(), from.alternative_tile);
			}
		}

		undo_redo->commit_action();
	}
}

void TileMapLayer25DEditor::_update_bottom_panel() {
	const TileMapLayer25D *edited_layer = _get_edited_layer();
	Ref<TileSet> tile_set;
	if (edited_layer) {
		tile_set = edited_layer->get_tile_set();
	}

	// Update state labels.
	if (is_multi_node_edit) {
		cant_edit_label->set_text(TTRC("Can't edit multiple layers at once."));
		cant_edit_label->show();
	} else if (!edited_layer) {
		cant_edit_label->set_text(TTRC("The selected TileMap has no layer to edit."));
		cant_edit_label->show();
	} else if (!edited_layer->is_enabled() || !edited_layer->is_visible_in_tree()) {
		cant_edit_label->set_text(TTRC("The edited layer is disabled or invisible"));
		cant_edit_label->show();
	} else if (tile_set.is_null()) {
		cant_edit_label->set_text(TTRC("The edited TileMap or TileMapLayer node has no TileSet resource.\nCreate or load a TileSet resource in the Tile Set property in the inspector."));
		cant_edit_label->show();
	} else {
		cant_edit_label->hide();
	}

	// Update tabs visibility.
	for (int i = 0; i < int(tabs_data.size()); i++) {
		TileMapLayer25DSubEditorPlugin::TabData &tab_data = tabs_data[i];
		if (i == tabs_bar->get_current_tab()) {
			tab_data.panel->set_visible(!cant_edit_label->is_visible());
		} else {
			tab_data.panel->hide();
		}
	}
}

Vector<Vector2i> TileMapLayer25DEditor::get_line(const TileMapLayer25D *p_tile_map_layer, Vector2i p_from_cell, Vector2i p_to_cell) {
	ERR_FAIL_NULL_V(p_tile_map_layer, Vector<Vector2i>());

	Ref<TileSet> tile_set = p_tile_map_layer->get_tile_set();
	ERR_FAIL_COND_V(tile_set.is_null(), Vector<Vector2i>());

	if (tile_set->get_tile_shape() == TileSet::TILE_SHAPE_SQUARE) {
		return Geometry2D::bresenham_line(p_from_cell, p_to_cell);
	} else {
		// Adapt the bresenham line algorithm to half-offset shapes.
		// See this blog post: http://zvold.blogspot.com/2010/01/bresenhams-line-drawing-algorithm-on_26.html
		Vector<Point2i> points;

		bool transposed = tile_set->get_tile_offset_axis() == TileSet::TILE_OFFSET_AXIS_VERTICAL;
		p_from_cell = TileSet::transform_coords_layout(p_from_cell, tile_set->get_tile_offset_axis(), tile_set->get_tile_layout(), TileSet::TILE_LAYOUT_STACKED);
		p_to_cell = TileSet::transform_coords_layout(p_to_cell, tile_set->get_tile_offset_axis(), tile_set->get_tile_layout(), TileSet::TILE_LAYOUT_STACKED);
		if (transposed) {
			SWAP(p_from_cell.x, p_from_cell.y);
			SWAP(p_to_cell.x, p_to_cell.y);
		}

		Vector2i delta = p_to_cell - p_from_cell;
		delta = Vector2i(2 * delta.x + Math::abs(p_to_cell.y % 2) - Math::abs(p_from_cell.y % 2), delta.y);
		Vector2i sign = delta.sign();

		Vector2i current = p_from_cell;
		points.push_back(TileSet::transform_coords_layout(transposed ? Vector2i(current.y, current.x) : current, tile_set->get_tile_offset_axis(), TileSet::TILE_LAYOUT_STACKED, tile_set->get_tile_layout()));

		int err = 0;
		if (Math::abs(delta.y) < Math::abs(delta.x)) {
			Vector2i err_step = 3 * delta.abs();
			while (current != p_to_cell) {
				err += err_step.y;
				if (err > Math::abs(delta.x)) {
					if (sign.x == 0) {
						current += Vector2(sign.y, 0);
					} else {
						current += Vector2(bool(current.y % 2) != (sign.x < 0) ? sign.x : 0, sign.y);
					}
					err -= err_step.x;
				} else {
					current += Vector2i(sign.x, 0);
					err += err_step.y;
				}
				points.push_back(TileSet::transform_coords_layout(transposed ? Vector2i(current.y, current.x) : current, tile_set->get_tile_offset_axis(), TileSet::TILE_LAYOUT_STACKED, tile_set->get_tile_layout()));
			}
		} else {
			Vector2i err_step = delta.abs();
			while (current != p_to_cell) {
				err += err_step.x;
				if (err > 0) {
					if (sign.x == 0) {
						current += Vector2(0, sign.y);
					} else {
						current += Vector2(bool(current.y % 2) != (sign.x < 0) ? sign.x : 0, sign.y);
					}
					err -= err_step.y;
				} else {
					if (sign.x == 0) {
						current += Vector2(0, sign.y);
					} else {
						current += Vector2(bool(current.y % 2) ^ (sign.x > 0) ? -sign.x : 0, sign.y);
					}
					err += err_step.y;
				}
				points.push_back(TileSet::transform_coords_layout(transposed ? Vector2i(current.y, current.x) : current, tile_set->get_tile_offset_axis(), TileSet::TILE_LAYOUT_STACKED, tile_set->get_tile_layout()));
			}
		}

		return points;
	}
}

void TileMapLayer25DEditor::_tile_map_layer_changed() {
	tile_map_layer_changed_needs_update = true;
}

void TileMapLayer25DEditor::_tab_changed(int p_tab_id) {
	// Make the plugin edit the correct tilemap.
	tabs_plugins[tabs_bar->get_current_tab()]->edit(edited_tile_map_layer_id);

	// Update toolbar.
	for (TileMapLayer25DSubEditorPlugin::TabData &tab_data : tabs_data) {
		for (Control *toolbar_control : tab_data.toolbar) {
			toolbar_control->hide();
		}
		tab_data.wide_toolbar->hide();
	}

	for (Control *toolbar_control : tabs_data[p_tab_id].toolbar) {
		toolbar_control->show();
	}
	tabs_data[p_tab_id].wide_toolbar->show();

	// Update visible panel.
	for (TileMapLayer25DSubEditorPlugin::TabData &tab_data : tabs_data) {
		tab_data.panel->hide();
	}

	TileMapLayer25D *tile_map_layer = _get_edited_layer();
	if (tile_map_layer) {
		if (tile_map_layer->get_tile_set().is_valid()) {
			tabs_data[tabs_bar->get_current_tab()].panel->show();
		}
	}

	// Graphical update.
	tabs_data[tabs_bar->get_current_tab()].panel->queue_redraw();
	CanvasItemEditor::get_singleton()->update_viewport();
	_update_bottom_panel();
}

void TileMapLayer25DEditor::_layers_select_next_or_previous(bool p_next) {
	const TileMapLayer25D *edited_layer = _get_edited_layer();
	if (!edited_layer) {
		return;
	}

	EditorNode *en = EditorNode::get_singleton();
	Node *edited_scene_root = en->get_edited_scene();
	ERR_FAIL_NULL(edited_scene_root);

	TileMapLayer25D *new_selected_layer = nullptr;
	int inc = p_next ? 1 : -1;
	_update_tile_map_layers_in_scene_list_cache();
	int edited_index = -1;
	for (int i = 0; i < tile_map_layers_in_scene_cache.size(); i++) {
		if (tile_map_layers_in_scene_cache[i] == edited_layer) {
			edited_index = i;
			break;
		}
	}
	new_selected_layer = tile_map_layers_in_scene_cache[Math::posmod(edited_index + inc, tile_map_layers_in_scene_cache.size())];

	ERR_FAIL_NULL(new_selected_layer);
	en->edit_node(new_selected_layer);
	en->get_editor_selection()->clear();
	en->get_editor_selection()->add_node(new_selected_layer);
}

bool TileMapLayer25DEditor::forward_canvas_gui_input(const Ref<InputEvent> &p_event) {
	if (ED_IS_SHORTCUT("tiles_editor/select_next_layer", p_event) && p_event->is_pressed()) {
		_layers_select_next_or_previous(true);
		return true;
	}

	if (ED_IS_SHORTCUT("tiles_editor/select_previous_layer", p_event) && p_event->is_pressed()) {
		_layers_select_next_or_previous(false);
		return true;
	}

	return tabs_plugins[tabs_bar->get_current_tab()]->forward_canvas_gui_input(p_event);
}

void TileMapLayer25DEditor::forward_canvas_draw_over_viewport(Control *p_overlay) {
	if (!custom_overlay) {
		custom_overlay = memnew(Control);
		custom_overlay->set_anchors_and_offsets_preset(Control::PRESET_FULL_RECT);
		custom_overlay->set_mouse_filter(Control::MOUSE_FILTER_IGNORE);
		custom_overlay->set_clip_contents(true);
		custom_overlay->set_draw_behind_parent(true);
		p_overlay->add_child(custom_overlay);
		custom_overlay->connect(SceneStringName(draw), callable_mp(this, &TileMapLayer25DEditor::_draw_overlay));
	}
	custom_overlay->queue_redraw();
}

void TileMapLayer25DEditor::_draw_overlay() {
	const TileMapLayer25D *edited_layer = _get_edited_layer();
	if (!edited_layer || !edited_layer->is_visible_in_tree()) {
		return;
	}

	Ref<TileSet> tile_set = edited_layer->get_tile_set();
	if (tile_set.is_null()) {
		return;
	}

	Transform2D xform = CanvasItemEditor::get_singleton()->get_canvas_transform() * edited_layer->get_global_transform_with_canvas();
	Transform2D xform_inv = xform.affine_inverse();
	Vector2i tile_shape_size = tile_set->get_tile_size();

	// Fade the overlay out when size too small.
	Vector2 hint_distance = xform.get_scale() * tile_shape_size;
	float scale_fading = MIN(1, (MIN(hint_distance.x, hint_distance.y) - 5) / 5);
	if (scale_fading > 0) {
		// Draw tiles with invalid IDs in the grid.
		TypedArray<Vector2i> used_cells = edited_layer->get_used_cells();
		for (int i = 0; i < used_cells.size(); i++) {
			Vector2i coords = used_cells[i];
			int tile_source_id = edited_layer->get_cell_source_id(coords);
			if (tile_source_id >= 0) {
				Vector2i tile_atlas_coords = edited_layer->get_cell_atlas_coords(coords);
				int tile_alternative_tile = edited_layer->get_cell_alternative_tile(coords);

				TileSetSource *source = nullptr;
				if (tile_set->has_source(tile_source_id)) {
					source = *tile_set->get_source(tile_source_id);
				}

				if (!source || !source->has_tile(tile_atlas_coords) || !source->has_alternative_tile(tile_atlas_coords, tile_alternative_tile)) {
					// Generate a random color from the hashed identifier of the tiles.
					Array to_hash = { tile_source_id, tile_atlas_coords, tile_alternative_tile };
					uint32_t hash = RandomPCG(to_hash.hash()).rand();

					Color color;
					color = color.from_hsv(
							(float)((hash >> 24) & 0xFF) / 256.0,
							Math::lerp(0.5, 1.0, (float)((hash >> 16) & 0xFF) / 256.0),
							Math::lerp(0.5, 1.0, (float)((hash >> 8) & 0xFF) / 256.0),
							0.8 * scale_fading);

					// Display the warning pattern.
					Transform2D tile_xform;
					tile_xform.set_origin(tile_set->map_to_local(coords));
					tile_xform.set_scale(tile_shape_size);
					tile_set->draw_tile_shape(custom_overlay, xform * tile_xform, color, true, warning_pattern_texture);

					// Draw the warning icon.
					Vector2::Axis min_axis = missing_tile_texture->get_size().min_axis_index();
					Vector2 icon_size;
					icon_size[min_axis] = tile_set->get_tile_size()[min_axis] / 3;
					icon_size[(min_axis + 1) % 2] = (icon_size[min_axis] * missing_tile_texture->get_size()[(min_axis + 1) % 2] / missing_tile_texture->get_size()[min_axis]);
					Rect2 rect = Rect2(xform.xform(tile_set->map_to_local(coords)) - (icon_size * xform.get_scale() / 2), icon_size * xform.get_scale());
					custom_overlay->draw_texture_rect(missing_tile_texture, rect, false, Color(1, 1, 1, scale_fading));
				}
			}
		}

		// Fading on the border.
		const int fading = 5;

		// Determine the drawn area.
		Size2 screen_size = custom_overlay->get_size();
		Rect2i screen_rect;
		screen_rect.position = edited_layer->local_viewport_to_map(xform_inv.xform(Vector2()), edited_z);
		screen_rect.expand_to(edited_layer->local_viewport_to_map(xform_inv.xform(Vector2(0, screen_size.height)), edited_z));
		screen_rect.expand_to(edited_layer->local_viewport_to_map(xform_inv.xform(Vector2(screen_size.width, 0)), edited_z));
		screen_rect.expand_to(edited_layer->local_viewport_to_map(xform_inv.xform(screen_size), edited_z));
		screen_rect = screen_rect.grow(1);

		Rect2i tilemap_used_rect = edited_layer->get_used_rect();

		Rect2i displayed_rect = tilemap_used_rect.intersection(screen_rect);
		displayed_rect = displayed_rect.grow(fading);

		// Reduce the drawn area to avoid crashes if needed.
		int max_size = 100;
		if (displayed_rect.size.x > max_size) {
			displayed_rect = displayed_rect.grow_individual(-(displayed_rect.size.x - max_size) / 2, 0, -(displayed_rect.size.x - max_size) / 2, 0);
		}
		if (displayed_rect.size.y > max_size) {
			displayed_rect = displayed_rect.grow_individual(0, -(displayed_rect.size.y - max_size) / 2, 0, -(displayed_rect.size.y - max_size) / 2);
		}

		// Draw the grid.
		bool display_grid = EDITOR_GET("editors/tiles_editor/display_grid");
		if (display_grid) {
			Color grid_color = EDITOR_GET("editors/tiles_editor/grid_color");
			for (int x = displayed_rect.position.x; x < (displayed_rect.position.x + displayed_rect.size.x); x++) {
				for (int y = displayed_rect.position.y; y < (displayed_rect.position.y + displayed_rect.size.y); y++) {
					Vector2i pos_in_rect = Vector2i(x, y) - displayed_rect.position;

					// Fade out the border of the grid.
					float left_opacity = CLAMP(Math::inverse_lerp(0.0f, (float)fading, (float)pos_in_rect.x), 0.0f, 1.0f);
					float right_opacity = CLAMP(Math::inverse_lerp((float)displayed_rect.size.x, (float)(displayed_rect.size.x - fading), (float)(pos_in_rect.x + 1)), 0.0f, 1.0f);
					float top_opacity = CLAMP(Math::inverse_lerp(0.0f, (float)fading, (float)pos_in_rect.y), 0.0f, 1.0f);
					float bottom_opacity = CLAMP(Math::inverse_lerp((float)displayed_rect.size.y, (float)(displayed_rect.size.y - fading), (float)(pos_in_rect.y + 1)), 0.0f, 1.0f);
					float opacity = CLAMP(MIN(left_opacity, MIN(right_opacity, MIN(top_opacity, bottom_opacity))) + 0.1, 0.0f, 1.0f);

					Transform2D tile_xform;
					tile_xform.set_origin(tile_set->map_to_local(Vector2(x, y)));
					tile_xform.set_scale(tile_shape_size);
					Color color = grid_color;
					color.a = color.a * opacity * scale_fading;
					tile_set->draw_tile_shape(custom_overlay, xform * tile_xform, color, false);
				}
			}
		}

		// Draw the IDs for debug.
		/*Ref<Font> font = get_theme_font(SceneStringName(font), SNAME("Label"));
		for (int x = displayed_rect.position.x; x < (displayed_rect.position.x + displayed_rect.size.x); x++) {
			for (int y = displayed_rect.position.y; y < (displayed_rect.position.y + displayed_rect.size.y); y++) {
				custom_overlay->draw_string(font, xform.xform(tile_set->map_to_local(Vector2(x, y))) + Vector2i(-tile_shape_size.x / 2, 0), vformat("%s", Vector2(x, y)));
			}
		}*/
	}

	// Draw the plugins.
	tabs_plugins[tabs_bar->get_current_tab()]->forward_canvas_draw_over_viewport(custom_overlay);
}

void TileMapLayer25DEditor::edit(Object *p_edited) {
	if (p_edited && p_edited->get_instance_id() == edited_tile_map_layer_id) {
		return;
	}

	_clear_all_layers_highlighting();

	// Disconnect to changes.
	TileMapLayer25D *tile_map_layer = _get_edited_layer();
	if (tile_map_layer) {
		tile_map_layer->disconnect(CoreStringName(changed), callable_mp(this, &TileMapLayer25DEditor::_tile_map_layer_changed));
		tile_map_layer->disconnect(SceneStringName(visibility_changed), callable_mp(this, &TileMapLayer25DEditor::_tile_map_layer_changed));
	}

	// Update the edited layer.
	TileMapLayer25D *new_layer = Object::cast_to<TileMapLayer25D>(p_edited);
	if (new_layer) {
		// Change the edited object.
		edited_tile_map_layer_id = new_layer->get_instance_id();

		tile_map_layer = _get_edited_layer();
		// Connect to changes.
		if (!tile_map_layer->is_connected(CoreStringName(changed), callable_mp(this, &TileMapLayer25DEditor::_tile_map_layer_changed))) {
			tile_map_layer->connect(CoreStringName(changed), callable_mp(this, &TileMapLayer25DEditor::_tile_map_layer_changed));
			tile_map_layer->connect(SceneStringName(visibility_changed), callable_mp(this, &TileMapLayer25DEditor::_tile_map_layer_changed));
		}
	} else {
		edited_tile_map_layer_id = ObjectID();
	}

	// Check if we are trying to use a MultiNodeEdit.
	is_multi_node_edit = Object::cast_to<MultiNodeEdit>(p_edited);

	// Call the plugins and update everything.
	tabs_plugins[tabs_bar->get_current_tab()]->edit(edited_tile_map_layer_id);
	_update_all_layers_highlighting();

	_tile_map_layer_changed();
}

void TileMapLayer25DEditor::set_z_level(int16_t p_z) {
	z_level_spinner->set_value(p_z);
}

void TileMapLayer25DEditor::_update_layer_selector_layout(bool p_is_vertical) {
	layer_selection_hbox->reparent(tile_map_toolbar);
	tile_map_toolbar->move_child(layer_selection_hbox, -5);
	layer_selection_hbox->set_vertical(p_is_vertical);
}

void TileMapLayer25DEditor::update_layout(DockLayout p_layout) {
	bool is_vertical = (p_layout == EditorDock::DockLayout::DOCK_LAYOUT_VERTICAL);
	tile_map_toolbar->set_vertical(is_vertical);
	layer_selector_separator->set_vertical(is_vertical);
	tile_map_toolbar->set_h_size_flags(is_vertical ? SIZE_SHRINK_BEGIN : SIZE_EXPAND_FILL);
	tile_map_toolbar->set_v_size_flags(is_vertical ? SIZE_EXPAND_FILL : SIZE_SHRINK_BEGIN);

	main_box_container->move_child(padding_control, is_vertical ? 0 : 2);
	if (is_vertical) {
		main_box_container->remove_theme_constant_override(SNAME("h_separation"));
	} else {
		main_box_container->add_theme_constant_override(SNAME("h_separation"), 0);
	}

	if (is_vertical) {
		tabs_panel->reparent(tile_map_wide_toolbar);
		tile_map_wide_toolbar->move_child(tabs_panel, 0);
	} else {
		tabs_panel->reparent(tile_map_toolbar);
		tile_map_toolbar->move_child(tabs_panel, 0);
	}

	_update_layer_selector_layout(is_vertical);

	// Propagate layout change to sub plugins
	for (TileMapLayer25DSubEditorPlugin *tab_plugin : tabs_plugins) {
		tab_plugin->update_layout(p_layout);
	}
}

TileMapLayer25DEditor::TileMapLayer25DEditor() {
	set_process_internal(true);
	set_name(TTRC("TileMap25D"));
	set_icon_name("TileMapDock");
	set_dock_shortcut(ED_SHORTCUT_AND_COMMAND("bottom_panels/toggle_tile_map_bottom_panel", TTRC("Open TileMap Dock")));
	set_default_slot(EditorDock::DOCK_SLOT_BOTTOM);
	set_available_layouts(EditorDock::DOCK_LAYOUT_ALL);
	set_global(false);
	set_transient(true);

	main_box_container = memnew(GridContainer);
	main_box_container->set_columns(2);
	add_child(main_box_container);

	// Shortcuts.
	ED_SHORTCUT("tiles_editor/select_next_layer", TTRC("Select Next Tile Map Layer"), Key::PAGEDOWN);
	ED_SHORTCUT("tiles_editor/select_previous_layer", TTRC("Select Previous Tile Map Layer"), Key::PAGEUP);

	// TileMap editor plugins
	tile_map_editor_plugins.push_back(memnew(TileMapLayer25DEditorTilesPlugin));
	//tile_map_editor_plugins.push_back(memnew(TileMapLayerEditorTerrainsPlugin));

	// TabBar.
	tabs_bar = memnew(TabBar);
	tabs_bar->set_theme_type_variation("TabBarInner");
	tabs_bar->set_clip_tabs(false);
	for (int plugin_index = 0; plugin_index < tile_map_editor_plugins.size(); plugin_index++) {
		Vector<TileMapLayer25DSubEditorPlugin::TabData> tabs_vector = tile_map_editor_plugins[plugin_index]->get_tabs();
		for (int tab_index = 0; tab_index < tabs_vector.size(); tab_index++) {
			tabs_bar->add_tab(tabs_vector[tab_index].panel->get_name());
			tabs_data.push_back(tabs_vector[tab_index]);
			tabs_plugins.push_back(tile_map_editor_plugins[plugin_index]);
		}
	}
	tabs_bar->connect("tab_changed", callable_mp(this, &TileMapLayer25DEditor::_tab_changed));

	// --- TileMap toolbar ---
	tile_map_wide_toolbar = memnew(VBoxContainer);
	main_box_container->add_child(tile_map_wide_toolbar);

	tile_map_toolbar = memnew(FlowContainer);
	tile_map_toolbar->set_h_size_flags(SIZE_EXPAND_FILL);
	main_box_container->add_child(tile_map_toolbar);

	padding_control = memnew(Control);
	main_box_container->add_child(padding_control);

	// Tabs.
	tabs_panel = memnew(PanelContainer);
	tabs_panel->set_theme_type_variation("PanelContainerTabbarInner");
	tabs_panel->add_child(tabs_bar);
	tile_map_toolbar->add_child(tabs_panel);

	// Tabs toolbars.
	for (TileMapLayer25DSubEditorPlugin::TabData &tab_data : tabs_data) {
		for (Control *toolbar_control : tab_data.toolbar) {
			toolbar_control->hide();
			if (!toolbar_control->get_parent()) {
				tile_map_toolbar->add_child(toolbar_control);
			}
		}

		tab_data.wide_toolbar->hide();
		if (!tab_data.wide_toolbar->get_parent()) {
			tile_map_wide_toolbar->add_child(tab_data.wide_toolbar);
		}
	}

	// Wide empty separation control. (like BoxContainer::add_spacer())
	Control *c = memnew(Control);
	c->set_mouse_filter(MOUSE_FILTER_PASS);
	c->set_h_size_flags(SIZE_EXPAND_FILL);
	tile_map_toolbar->add_child(c);

	// Layer selector.
	layer_selection_hbox = memnew(BoxContainer);
	tile_map_toolbar->add_child(layer_selection_hbox);

	z_level_spinner = memnew(SpinBox);
	z_level_spinner->set_min(-32767);
	z_level_spinner->set_max(32767);
	z_level_spinner->set_step(1);
	z_level_spinner->set_accessibility_name(TTRC("Change Z Level:"));
	tile_map_toolbar->add_child(z_level_spinner);
	z_level_spinner->get_line_edit()->add_theme_constant_override("minimum_character_width", 2);
	z_level_spinner->get_line_edit()->set_context_menu_enabled(false);
	z_level_spinner->connect(SceneStringName(value_changed), callable_mp(this, &TileMapLayer25DEditor::_z_level_changed));
	z_level_spinner->connect(SceneStringName(mouse_exited), callable_mp(this, &TileMapLayer25DEditor::_z_level_mouse_exited));
	z_level_spinner->get_line_edit()->connect(SceneStringName(mouse_exited), callable_mp(this, &TileMapLayer25DEditor::_z_level_mouse_exited));

	// Highlighting z level.
	toggle_highlight_selected_z_level_button = memnew(Button);
	toggle_highlight_selected_z_level_button->set_theme_type_variation(SceneStringName(FlatButton));
	toggle_highlight_selected_z_level_button->set_toggle_mode(true);
	toggle_highlight_selected_z_level_button->connect(SceneStringName(toggled), callable_mp(this, &TileMapLayer25DEditor::_highlight_selected_z_level_button_toggled));
	toggle_highlight_selected_z_level_button->set_tooltip_text(TTRC("Highlight Current Z Level"));
	tile_map_toolbar->add_child(toggle_highlight_selected_z_level_button);

	select_previous_layer = memnew(Button);
	select_previous_layer->set_theme_type_variation(SceneStringName(FlatButton));
	select_previous_layer->set_tooltip_text(TTRC("Select previous layer"));
	select_previous_layer->connect(SceneStringName(pressed), callable_mp(this, &TileMapLayer25DEditor::_select_previous_layer_pressed));
	layer_selection_hbox->add_child(select_previous_layer);

	select_next_layer = memnew(Button);
	select_next_layer->set_theme_type_variation(SceneStringName(FlatButton));
	select_next_layer->set_tooltip_text(TTRC("Select next layer"));
	select_next_layer->connect(SceneStringName(pressed), callable_mp(this, &TileMapLayer25DEditor::_select_next_layer_pressed));
	layer_selection_hbox->add_child(select_next_layer);

	select_all_layers = memnew(Button);
	select_all_layers->set_theme_type_variation(SceneStringName(FlatButton));
	select_all_layers->connect(SceneStringName(pressed), callable_mp(this, &TileMapLayer25DEditor::_select_all_layers_pressed));
	select_all_layers->set_tooltip_text(TTRC("Select all TileMapLayers in scene"));
	layer_selection_hbox->add_child(select_all_layers);

	// Highlighting selected layer.
	toggle_highlight_selected_layer_button = memnew(Button);
	toggle_highlight_selected_layer_button->set_theme_type_variation(SceneStringName(FlatButton));
	toggle_highlight_selected_layer_button->set_toggle_mode(true);
	toggle_highlight_selected_layer_button->connect(SceneStringName(toggled), callable_mp(this, &TileMapLayer25DEditor::_highlight_selected_layer_button_toggled));
	toggle_highlight_selected_layer_button->set_tooltip_text(TTRC("Highlight Selected TileMap Layer"));
	tile_map_toolbar->add_child(toggle_highlight_selected_layer_button);

	layer_selector_separator = memnew(SwitchSeparator);
	layer_selector_separator->set_vertical(false);
	tile_map_toolbar->add_child(layer_selector_separator);

	// Grid toggle.
	toggle_grid_button = memnew(Button);
	toggle_grid_button->set_theme_type_variation(SceneStringName(FlatButton));
	toggle_grid_button->set_toggle_mode(true);
	toggle_grid_button->set_tooltip_text(TTRC("Toggle grid visibility."));
	toggle_grid_button->connect(SceneStringName(toggled), callable_mp(this, &TileMapLayer25DEditor::_on_grid_toggled));
	tile_map_toolbar->add_child(toggle_grid_button);

	// Advanced settings menu button.
	advanced_menu_button = memnew(MenuButton);
	advanced_menu_button->set_flat(false);
	advanced_menu_button->set_tooltip_text(TTRC("Advanced settings."));
	advanced_menu_button->set_theme_type_variation(SceneStringName(FlatButton));
	advanced_menu_button->get_popup()->add_item(TTRC("Automatically Replace Tiles with Proxies"), ADVANCED_MENU_REPLACE_WITH_PROXIES);
	advanced_menu_button->get_popup()->connect(SceneStringName(id_pressed), callable_mp(this, &TileMapLayer25DEditor::_advanced_menu_button_id_pressed));
	tile_map_toolbar->add_child(advanced_menu_button);

	// A label for editing errors.
	cant_edit_label = memnew(Label);
	cant_edit_label->set_focus_mode(Control::FOCUS_ACCESSIBILITY);
	cant_edit_label->set_autowrap_mode(TextServer::AUTOWRAP_WORD_SMART);
	cant_edit_label->set_anchors_and_offsets_preset(Control::PRESET_HCENTER_WIDE);
	cant_edit_label->set_h_size_flags(SIZE_EXPAND_FILL);
	cant_edit_label->set_v_size_flags(SIZE_EXPAND_FILL);
	cant_edit_label->set_horizontal_alignment(HORIZONTAL_ALIGNMENT_CENTER);
	cant_edit_label->set_vertical_alignment(VERTICAL_ALIGNMENT_CENTER);
	cant_edit_label->hide();
	main_box_container->add_child(cant_edit_label);

	for (unsigned int tab_index = 0; tab_index < tabs_data.size(); tab_index++) {
		main_box_container->add_child(tabs_data[tab_index].panel);
		tabs_data[tab_index].panel->set_v_size_flags(SIZE_EXPAND_FILL);
		tabs_data[tab_index].panel->set_visible(tab_index == 0);
		tabs_data[tab_index].panel->set_h_size_flags(SIZE_EXPAND_FILL);
	}

	_tab_changed(0);
}

TileMapLayer25DEditor::~TileMapLayer25DEditor() {
	for (int i = 0; i < tile_map_editor_plugins.size(); i++) {
		memdelete(tile_map_editor_plugins[i]);
	}
}