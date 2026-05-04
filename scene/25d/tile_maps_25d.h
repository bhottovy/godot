/* TCG Custom class */
#pragma once

union CellKey {
	struct {
		int x : 24;
		int y : 24;
		int16_t z;
	};
	uint64_t key = 0;

	static uint32_t hash(const CellKey &p_key) {
		return hash_one_uint64(p_key.key);
	}
	_FORCE_INLINE_ bool operator<(const CellKey &p_key) const {
		return key < p_key.key;
	}
	_FORCE_INLINE_ bool operator==(const CellKey &p_key) const {
		return key == p_key.key;
	}

	_FORCE_INLINE_ operator Vector3i() const {
		return Vector3i(x, y, z);
	}

	Vector2i coords() const { return Vector2i(x, y); }

	uint32_t hash() const { return operator Vector3i().hash(); }

	CellKey(Vector3i p_vector) {
		x = p_vector.x;
		y = p_vector.y;
		z = (int16_t)p_vector.z;
	}
	CellKey(Vector2i p_coords, int16_t p_z = 0) {
		x = p_coords.x;
		y = p_coords.y;
		z = p_z;
	}
	CellKey() {}
};

union TileSetKey {
	struct {
		int source_id : 24;
		int atlas_x : 12;
		int atlas_y : 12;
		int alternative_id : 16;
	};
	uint64_t key = 0;

	static uint32_t hash(const TileSetKey &p_key) {
		return hash_one_uint64(p_key.key);
	}

	_FORCE_INLINE_ bool operator==(const TileSetKey &p_key) const {
		return key == p_key.key;
	}

	Vector2i atlas_coords() const { return Vector2i(atlas_x, atlas_y); }

	TileSetKey(int p_source_id, Vector2i p_atlas_coords, int p_alternative_id = -1) {
		source_id = p_source_id;
		atlas_x = p_atlas_coords.x;
		atlas_y = p_atlas_coords.y;
		alternative_id = p_alternative_id;
	}
	TileSetKey() {
		source_id = TileSet::INVALID_SOURCE;
		atlas_x = TileSetSource::INVALID_ATLAS_COORDS.x;
		atlas_y = TileSetSource::INVALID_ATLAS_COORDS.y;
		alternative_id = TileSetSource::INVALID_TILE_ALTERNATIVE;
	}
};