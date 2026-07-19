#pragma once
// AYMeshComponent.h — Phase 1 E-03: draw-metadata struct.
// The `skinned` flag routes the entity to SkinnedMeshRenderSystem
// instead of RenderSystem when paired with a SkeletonComponent.
//
// Note: `meshPath` / `materialPath` are declared via AY_PROPERTY
// (the macro emits `Type name;`). Don't redeclare them as plain
// fields — that triggers C2086 "重定义".

#include <IAYEntity.h>

#include <string>

namespace ayt::entity
{

#define AY_CURRENT_CLASS MeshComponent
struct MeshComponent : public IComponent {
    const char* getName() const override { return "MeshComponent"; }

    // Path fields declared via the AY_PROPERTY macro below. The
    // expand emits `Type name;` and registers a serializer metadata
    // entry — keep these macro-only.
    AY_PROPERTY(std::string, meshPath,    kAttrSerialize)
    AY_PROPERTY(std::string, materialPath, kAttrSerialize)

    // Phase 1 E-03: when true, RenderSystem skips this entity and
    // SkinnedMeshRenderSystem takes over. Set after adding a
    // SkeletonComponent (the system sets it automatically when the
    // skeleton loads; gameplay code can also set it directly for
    // bind-pose previews).
    AY_PROPERTY(bool, skinned, kAttrSerialize)

    bool castShadow = true;
    bool receiveShadow = true;
    bool visible = true;
    int32_t layer = 0;

    MeshComponent() {
        // AY_PROPERTY expands to `Type name;` with no initializer, so
        // explicit ctor assignment is required to leave the field in
        // a defined state (uninitialized bool would read garbage).
        skinned = false;
    }

    explicit MeshComponent(const char* path)
        : meshPath(path ? path : "") {}

    MeshComponent(const char* mesh, const char* material)
        : meshPath(mesh ? mesh : ""),
          materialPath(material ? material : "") {}

    void setMesh(const char* path) { meshPath = path ? path : ""; }
    void setMaterial(const char* path) { materialPath = path ? path : ""; }

    bool isValid() const { return !meshPath.empty(); }
};
#undef AY_CURRENT_CLASS

} // namespace ayt::entity