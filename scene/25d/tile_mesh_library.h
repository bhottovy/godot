/* TCG Custom class */
#pragma once

#include "core/io/resource.h"
#include "scene/resources/mesh.h"

class TileMeshLibrary : public Resource {
    GDCLASS(TileMeshLibrary, Resource);

    struct TileMesh {
        Ref<Mesh> mesh;
        RID shape;
        String name;
    };

    Vector<TileMesh> items;
};