#pragma once

#include "ExportableObject.h"
#include "basicTypes.h"

class ExportableResources;
class ExportablePrimitive;
class Arguments;
class ExportableScene;
class ExportableNode;

class ExportableMesh : public ExportableObject {
  public:
    // TODO: Support instancing, for now we create a new mesh for each node.
    // To properly support instance, we need to decide what to do with shapes
    // that are both with and without a skeleton Do we generate two meshes, with
    // and without skinning vertex attributes?
    ExportableMesh(ExportableScene &scene, ExportableNode &node,
                   const MDagPath &shapeDagPath);
    virtual ~ExportableMesh();

    GLTF::Mesh glMesh;
    GLTF::Skin glSkin;

    size_t blendShapeCount() const { return m_weightPlugs.size(); }

    gsl::span<const float> initialWeights() const { return m_initialWeights; }

    std::vector<float> currentWeights() const;

    void attachToNode(GLTF::Node &node);

    void updateWeights();

    void getAllAccessors(std::vector<GLTF::Accessor *> &accessors) const;

  private:
    DISALLOW_COPY_MOVE_ASSIGN(ExportableMesh);

    std::vector<float> m_initialWeights;
    std::vector<MPlug> m_weightPlugs;
    std::vector<std::unique_ptr<ExportablePrimitive>> m_primitives;

    std::vector<Float4x4> m_inverseBindMatrices;
    std::unique_ptr<GLTF::Accessor> m_inverseBindMatricesAccessor;
    std::unique_ptr<GLTF::MorphTargetNames> m_morphTargetNames =
        std::make_unique<GLTF::MorphTargetNames>();
};
