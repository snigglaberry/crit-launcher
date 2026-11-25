// Crit Launcher Native — renderer hooks shared with the platform layer.
#pragma once

#include "core/base.h"

namespace crit {
struct InputState;
// Tells the platform where the UI writes its input each frame and how to
// invoke the GPU path (implemented by the platform translation unit).
void SetRendererInput(InputState* input);
}  // namespace crit
