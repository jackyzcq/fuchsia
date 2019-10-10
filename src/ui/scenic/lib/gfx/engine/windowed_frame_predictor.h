// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_UI_SCENIC_LIB_GFX_ENGINE_WINDOWED_FRAME_PREDICTOR_H_
#define SRC_UI_SCENIC_LIB_GFX_ENGINE_WINDOWED_FRAME_PREDICTOR_H_

#include <lib/zx/time.h>

#include "src/ui/scenic/lib/gfx/engine/frame_predictor.h"

namespace scenic_impl {
namespace gfx {

class WindowedFramePredictor : public FramePredictor {
 public:
  WindowedFramePredictor(zx::duration initial_render_duration_prediction,
                         zx::duration initial_update_duration_prediction);
  ~WindowedFramePredictor();

  // |FramePredictor|
  PredictedTimes GetPrediction(PredictionRequest request) override;

  // |FramePredictor|
  void ReportRenderDuration(zx::duration time_to_render) override;

  // |FramePredictor|
  void ReportUpdateDuration(zx::duration time_to_update) override;

 private:
  // Returns a prediction for how long in total the next frame will take to
  // update and render.
  zx::duration PredictTotalRequiredDuration() const;

  // Safety margin added to prediction time to reduce impact of noise and
  // misprediction. Unfortunately this means minimum possible latency is
  // increased by the same amount.
  const zx::duration kHardcodedMargin = zx::msec(3);  // 3ms

  // Rarely, it is possible for abnormally long GPU contexts to occur, and
  // when they occur we do not want them to mess up future predictions by
  // too much. We therefore clamp RenderDurations by this much.
  const zx::duration kMaxFrameTime = zx::usec(16'667);  // 16.667ms

  // Render time prediction.
  const size_t kRenderPredictionWindowSize = 3;
  DurationPredictor render_duration_predictor_;

  // Update time prediction.
  const size_t kUpdatePredictionWindowSize = 1;
  DurationPredictor update_duration_predictor_;
};

}  // namespace gfx
}  // namespace scenic_impl

#endif  // SRC_UI_SCENIC_LIB_GFX_ENGINE_WINDOWED_FRAME_PREDICTOR_H_
