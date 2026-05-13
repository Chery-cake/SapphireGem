#ifndef FRAME_UPDATE_SIGNAL_H_
#define FRAME_UPDATE_SIGNAL_H_

#include "signal.hpp"
#include "window_export.h"
#include <cstdint>

namespace window {

/**
 * @brief Per-window signal fired once at the start of every frame.
 *
 * Receivers can update animation parameters (e.g. @c time) that are consumed
 * by the async compute pass.  Subscribers should be connected during scene
 * load and disconnected during scene unload.
 *
 * Signature: @c void(float deltaTime, uint32_t frameIndex)
 *   - @p deltaTime   Elapsed seconds since the previous frame.
 *   - @p frameIndex  Current frame-in-flight index (0 .. MAX_FRAMES_IN_FLIGHT-1).
 */
using FrameUpdateSignal =
    core::signal::Signal<void(float /*deltaTime*/, uint32_t /*frameIndex*/)>;

} // namespace window

#endif // FRAME_UPDATE_SIGNAL_H_
