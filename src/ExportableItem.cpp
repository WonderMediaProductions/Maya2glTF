#include "ExportableItem.h"
#include "NodeAnimation.h"
#include "externals.h"

ExportableItem::~ExportableItem() = default;

std::unique_ptr<NodeAnimation>
ExportableItem::createAnimation(const ExportableFrames &frameTimes,
                                const double scaleFactor) {
    return nullptr;
}
