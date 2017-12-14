// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "garnet/bin/ui/root_presenter/display_flipper.h"

#if defined(countof)
// Workaround for compiler error due to Zircon defining countof() as a macro.
// Redefines countof() using GLM_COUNTOF(), which currently provides a more
// sophisticated implementation anyway.
#undef countof
#include <glm/glm.hpp>
#define countof(X) GLM_COUNTOF(X)
#else
// No workaround required.
#include <glm/glm.hpp>
#endif
#include <glm/ext.hpp>
#include <glm/gtc/constants.hpp>

namespace root_presenter {
namespace {

constexpr float kPi = glm::pi<float>();
}

DisplayFlipper::DisplayFlipper() {}

bool DisplayFlipper::OnEvent(const mozart::InputEventPtr& event,
                             scenic_lib::Scene* scene,
                             const DisplayMetrics& display_metrics,
                             bool* continue_dispatch_out) {
  FXL_DCHECK(continue_dispatch_out);
  bool invalidate = false;
  if (event->is_pointer()) {
    const mozart::PointerEventPtr& pointer = event->get_pointer();

    // Mouse coordinates don't need to be transformed.
    if (pointer->type != mozart::PointerEvent::Type::MOUSE) {
      // Take into account screen rotation.
      // TODO(MZ-389): Do this in Scenic instead.
      if (display_flipped_) {
        auto new_coords =
            FlipPointerCoordinates(pointer->x, pointer->y, display_metrics);
        pointer->x = new_coords.first;
        pointer->y = new_coords.second;
        *continue_dispatch_out = true;
      }
    }
  } else if (event->is_keyboard()) {
    const mozart::KeyboardEventPtr& kbd = event->get_keyboard();
    const uint32_t kVolumeDownKey = 232;
    if (kbd->modifiers == 0 &&
        kbd->phase == mozart::KeyboardEvent::Phase::PRESSED &&
        kbd->code_point == 0 && kbd->hid_usage == kVolumeDownKey) {
      FlipDisplay(scene, display_metrics);
      invalidate = true;
      *continue_dispatch_out = false;
    }
  }

  return invalidate;
}

void DisplayFlipper::FlipDisplay(scenic_lib::Scene* scene,
                                 const DisplayMetrics& display_metrics) {
  if (display_flipped_) {
    scene->SetAnchor(0, 0, 0);
    scene->SetRotation(0, 0, 0, 0);
    scene->SetTranslation(0, 0, 0);
  } else {
    float anchor_x = display_metrics.width_in_pp() / 2;
    float anchor_y = display_metrics.height_in_pp() / 2;

    glm::quat display_rotation = glm::quat(glm::vec3(0, 0, kPi));

    scene->SetAnchor(anchor_x, anchor_y, 0);
    scene->SetRotation(display_rotation.x, display_rotation.y,
                       display_rotation.z, display_rotation.w);
    scene->SetTranslation(display_metrics.width_in_px() / 2 - anchor_x,
                          display_metrics.height_in_px() / 2 - anchor_y, 0);
  }
  display_flipped_ = !display_flipped_;
}

std::pair<float, float> DisplayFlipper::FlipPointerCoordinates(
    float x,
    float y,
    const DisplayMetrics& display_metrics) {
  glm::vec4 pointer_coords(x, y, 0.f, 1.f);

  float logical_width = display_metrics.width_in_pp();
  float logical_height = display_metrics.height_in_pp();
  glm::vec4 rotated_coords =
      glm::translate(glm::vec3(logical_width / 2, logical_height / 2, 0)) *
      glm::rotate(kPi, glm::vec3(0, 0, 1)) *
      glm::translate(glm::vec3(-logical_width / 2, -logical_height / 2, 0)) *
      pointer_coords;
  return std::pair<float, float>(rotated_coords.x, rotated_coords.y);
}

}  // namespace root_presenter
