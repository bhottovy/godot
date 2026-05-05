#include "tiles_25d_editor_plugin.h"

#include "core/object/callable_mp.h"
#include "core/os/mutex.h"
#include "core/os/os.h"
#include "editor/docks/editor_dock_manager.h"
#include "editor/editor_interface.h"
#include "editor/editor_node.h"
#include "editor/editor_string_names.h"
#include "editor/inspector/multi_node_edit.h"
#include "editor/scene/2d/tiles/tiles_editor_plugin.h"
#include "editor/scene/2d/tiles/tile_set_editor.h"
#include "editor/scene/canvas_item_editor_plugin.h"
#include "editor/settings/editor_settings.h"
#include "editor/themes/editor_scale.h"
#include "scene/2d/tile_map.h"
#include "scene/2d/tile_map_layer.h"
#include "scene/25d/tile_map_layer_25d.h"
#include "scene/gui/control.h"
#include "scene/main/scene_tree.h"
#include "scene/resources/2d/tile_set.h"
#include "scene/resources/image_texture.h"
#include "servers/rendering/rendering_server.h"

extern TileSetEditorPlugin *tile_set_plugin_singleton;
TileMap25DEditorPlugin *tile_map_25d_plugin_singleton = nullptr;

void TileMap25DEditorPlugin::_tile_map_layer_changed() {
	if (tile_map_changed_needs_update) {
		return;
	}
	tile_map_changed_needs_update = true;
	callable_mp(this, &TileMap25DEditorPlugin::_update_tile_map).call_deferred();
}

void TileMap25DEditorPlugin::_update_tile_map() {
	TileMapLayer25D *edited_layer = ObjectDB::get_instance<TileMapLayer25D>(tile_map_layer_id);
	if (edited_layer) {
		Ref<TileSet> tile_set = edited_layer->get_tile_set();
		if (tile_set.is_valid() && tile_set_id != tile_set->get_instance_id()) {
			tile_set_plugin_singleton->edit(tile_set.ptr());
			tile_set_plugin_singleton->make_visible(true);
			tile_set_id = tile_set->get_instance_id();
		} else if (tile_set.is_null()) {
			tile_set_plugin_singleton->edit(nullptr);
			tile_set_plugin_singleton->make_visible(false);
			tile_set_id = ObjectID();
		}
	}
	tile_map_changed_needs_update = false;
}

void TileMap25DEditorPlugin::_select_layer(const StringName &p_name) {
	TileMapLayer25D *edited_layer = ObjectDB::get_instance<TileMapLayer25D>(tile_map_layer_id);
	ERR_FAIL_NULL(edited_layer);

	Node *parent = edited_layer->get_parent();
	if (parent) {
		TileMapLayer25D *new_layer = Object::cast_to<TileMapLayer25D>(parent->get_node_or_null(String(p_name)));
		edit(new_layer);
	}
}

void TileMap25DEditorPlugin::_edit_tile_map_layer(TileMapLayer25D *p_tile_map_layer, int16_t p_z, bool p_show_layer_selector) {
	ERR_FAIL_NULL(p_tile_map_layer);

	editor->edit(p_tile_map_layer);
  editor->set_z_level(p_z);

	// Update the object IDs.
	tile_map_layer_id = p_tile_map_layer->get_instance_id();
	p_tile_map_layer->connect(CoreStringName(changed), callable_mp(this, &TileMap25DEditorPlugin::_tile_map_layer_changed));

	// Update the edited tileset.
	Ref<TileSet> tile_set = p_tile_map_layer->get_tile_set();
	if (tile_set.is_valid()) {
		tile_set_plugin_singleton->edit(tile_set.ptr());
		tile_set_plugin_singleton->open_editor();
		tile_set_id = tile_set->get_instance_id();
	} else {
		tile_set_plugin_singleton->edit(nullptr);
		tile_set_plugin_singleton->make_visible(false);
	}
}

void TileMap25DEditorPlugin::_notification(int p_notification) {
}

void TileMap25DEditorPlugin::edit(Object *p_object) {
	TileMapLayer25D *edited_layer = ObjectDB::get_instance<TileMapLayer25D>(tile_map_layer_id);
	int keep_z = 0;
	if (edited_layer) {
		keep_z = edited_layer->get_meta("_editor_z_", 0);
		edited_layer->disconnect(CoreStringName(changed), callable_mp(this, &TileMap25DEditorPlugin::_tile_map_layer_changed));
	}

	tile_map_layer_id = ObjectID();
	tile_set_id = ObjectID();

	TileMapLayer25D *tile_map_layer = Object::cast_to<TileMapLayer25D>(p_object);
	MultiNodeEdit *multi_node_edit = Object::cast_to<MultiNodeEdit>(p_object);
	if (tile_map_layer) {
		if(keep_z == 0) keep_z = tile_map_layer->get_meta("_editor_z_", 0);
		_edit_tile_map_layer(tile_map_layer, (int16_t)keep_z, false);
	} else if (multi_node_edit) {
		editor->edit(multi_node_edit);
	} else {
		editor->edit(nullptr);
	}
}

bool TileMap25DEditorPlugin::handles(Object *p_object) const {
	MultiNodeEdit *multi_node_edit = Object::cast_to<MultiNodeEdit>(p_object);
	Node *edited_scene = EditorNode::get_singleton()->get_edited_scene();
	if (multi_node_edit && edited_scene) {
		bool only_tile_map_layers = true;
		for (int i = 0; i < multi_node_edit->get_node_count(); i++) {
			if (!Object::cast_to<TileMapLayer25D>(edited_scene->get_node(multi_node_edit->get_node(i)))) {
				only_tile_map_layers = false;
				break;
			}
		}
		return only_tile_map_layers;
	}
	return Object::cast_to<TileMapLayer25D>(p_object) != nullptr;
}

void TileMap25DEditorPlugin::make_visible(bool p_visible) {
	if (p_visible) {
		editor->make_visible();
	} else {
		editor->close();
		TileSetEditor::get_singleton()->close();
	}
}

bool TileMap25DEditorPlugin::forward_canvas_gui_input(const Ref<InputEvent> &p_event) {
	return editor->forward_canvas_gui_input(p_event);
}

void TileMap25DEditorPlugin::forward_canvas_draw_over_viewport(Control *p_overlay) {
	editor->forward_canvas_draw_over_viewport(p_overlay);
}

bool TileMap25DEditorPlugin::is_editor_visible() const {
	return editor->is_visible_in_tree();
}

TileMap25DEditorPlugin::TileMap25DEditorPlugin() {
	if (!TilesEditorUtils::get_singleton()) {
		memnew(TilesEditorUtils);
	}
	tile_map_25d_plugin_singleton = this;

	editor = memnew(TileMapLayer25DEditor);
	editor->set_h_size_flags(Control::SIZE_EXPAND_FILL);
	editor->set_v_size_flags(Control::SIZE_EXPAND_FILL);
	editor->set_custom_minimum_size(Size2(0, 200) * EDSCALE);
	editor->hide();

	EditorDockManager::get_singleton()->add_dock(editor);
	editor->close();
}

TileMap25DEditorPlugin::~TileMap25DEditorPlugin() {
	tile_map_25d_plugin_singleton = nullptr;
}