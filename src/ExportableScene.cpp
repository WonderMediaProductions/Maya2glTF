#include "ExportableScene.h"
#include "ExportableNode.h"
#include "MayaException.h"
#include "externals.h"

ExportableScene::ExportableScene(ExportableResources &resources)
    : m_resources(resources) {}

ExportableScene::~ExportableScene() = default;

void ExportableScene::updateCurrentValues() {
    for (auto &&pair : m_table) {
        auto &node = pair.second;
        node->updateNodeTransforms(m_currentTransformCache);
        auto *mesh = node->mesh();
        if (mesh) {
            mesh->updateWeights();
        }
    }
}

void ExportableScene::mergeRedundantShapeNodes() {
    std::set<NodeTable::key_type> redundantKeys;

    for (auto &&pair : m_table) {
        auto &node = pair.second;
        if (node->tryMergeRedundantShapeNode()) {
            redundantKeys.insert(pair.first);
        }
    }

    for (auto &&key : redundantKeys) {
        m_table.erase(key);
    }
}

ExportableNode *ExportableScene::getNode(const MDagPath &dagPath) {
    MStatus status;

    const std::string fullDagPath{dagPath.fullPathName(&status).asChar()};
    THROW_ON_FAILURE(status);

    MObject mayaNode = dagPath.node(&status);
    if (mayaNode.isNull() || status.error()) {
        cerr << "glTF2Maya: skipping '" << fullDagPath
             << "' as it is not a node" << endl;
        return nullptr;
    }

    auto &ptr = m_table[fullDagPath];
    if (ptr == nullptr) {
        ptr.reset(new ExportableNode(dagPath));
        ptr->load(*this, m_initialTransformCache);
    }
    return ptr.get();
}

ExportableNode *ExportableScene::getParent(ExportableNode *node) {
    MStatus status;

    auto parentDagPath = node->dagPath;

    ExportableNode *parentNode = nullptr;

    // Find first selected ancestor node.
    // That is our logical parent.
    while (!parentNode) {
        parentDagPath.pop();
        if (parentDagPath.length() <= 0)
            break;

        parentNode = getNode(parentDagPath);
    }

    return parentNode;
}

void ExportableScene::getAllAccessors(AccessorsPerDagPath &accessors) {
    for (auto &&pair : m_table) {
        auto &node = pair.second;

        node->getAllAccessors(accessors[node->dagPath]);
    }
}

int ExportableScene::distanceToRoot(MDagPath dagPath) {
    int distance;

    // Find first selected ancestor node.
    // That is our logical parent.
    for (distance = 0; dagPath.length() > 0; ++distance) {
        dagPath.pop();
    }

    return distance;
}
