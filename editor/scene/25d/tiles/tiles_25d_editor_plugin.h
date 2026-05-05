#pragma once

#include "core/os/semaphore.h"
#include "editor/plugins/editor_plugin.h"
#include "editor/scene/25d/tiles/tile_map_layer_25d_editor.h"

class TileMap25DEditorPlugin : public EditorPlugin {
	GDCLASS(TileMap25DEditorPlugin, EditorPlugin);

	TileMapLayer25DEditor *editor = nullptr;
	ObjectID tile_map_layer_id;

	bool tile_map_changed_needs_update = false;
	ObjectID tile_set_id; // The TileSet associated with the TileMap.

	void _tile_map_layer_changed();
	void _update_tile_map();
	void _select_layer(const StringName &p_name);

	void _edit_tile_map_layer(TileMapLayer25D *p_tile_map_layer, int16_t p_z, bool p_show_layer_selector);

protected:
	void _notification(int p_notification);

public:
	virtual void edit(Object *p_object) override;
	virtual bool handles(Object *p_object) const override;
	virtual void make_visible(bool p_visible) override;

	virtual bool forward_canvas_gui_input(const Ref<InputEvent> &p_event) override;
	virtual void forward_canvas_draw_over_viewport(Control *p_overlay) override;

	bool is_editor_visible() const;

	TileMap25DEditorPlugin();
	~TileMap25DEditorPlugin();
};