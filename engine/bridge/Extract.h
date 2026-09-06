#pragma once

#include "render/RenderFrame.h"
#include "scene/Scene.h"

namespace velos {

void extractScene(const Scene& scene, RenderFrame& frame, EntityId selected, bool grid);

}