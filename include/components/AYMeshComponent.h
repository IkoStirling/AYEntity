#pragma once
// AYMesh.h - 网格渲染组件

#include <AYCore.h>
#include <IAYEntity.h>
#include <string>

namespace ayt::entity
{

// =============================================================================
// MeshComponent - 网格渲染组件
// =============================================================================
struct MeshComponent : public IComponent {
    const char* getName() const override { return "MeshComponent"; }

    std::string meshPath;
    std::string materialPath;

    bool castShadow = true;
    bool receiveShadow = true;
    bool visible = true;
    int32_t layer = 0;

    MeshComponent() = default;

    explicit MeshComponent(const char* path) : meshPath(path) {}

    MeshComponent(const char* mesh, const char* material)
        : meshPath(mesh), materialPath(material) {}

    void setMesh(const char* path) { meshPath = path; }
    void setMaterial(const char* path) { materialPath = path; }

    bool isValid() const { return !meshPath.empty(); }
};

AY_COMPONENT(MeshComponent);

} // namespace ayt::entity