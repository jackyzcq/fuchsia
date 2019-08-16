// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/media/audio/audio_core/audio_admin.h"

#include <fcntl.h>
#include <lib/async/cpp/task.h>
#include <lib/async/default.h>

#include "src/media/audio/audio_core/audio_core_impl.h"

namespace media {
namespace audio {

AudioAdmin::AudioAdmin(AudioCoreImpl* service) : service_(service) {}

void AudioAdmin::SetInteraction(fuchsia::media::Usage active, fuchsia::media::Usage affected,
                                fuchsia::media::Behavior behavior) {
  if (active.Which() == fuchsia::media::Usage::Tag::kCaptureUsage &&
      affected.Which() == fuchsia::media::Usage::Tag::kCaptureUsage) {
    active_rules_.SetRule(active.capture_usage(), affected.capture_usage(), behavior);
  } else if (active.Which() == fuchsia::media::Usage::Tag::kCaptureUsage &&
             affected.Which() == fuchsia::media::Usage::Tag::kRenderUsage) {
    active_rules_.SetRule(active.capture_usage(), affected.render_usage(), behavior);

  } else if (active.Which() == fuchsia::media::Usage::Tag::kRenderUsage &&
             affected.Which() == fuchsia::media::Usage::Tag::kCaptureUsage) {
    active_rules_.SetRule(active.render_usage(), affected.capture_usage(), behavior);

  } else if (active.Which() == fuchsia::media::Usage::Tag::kRenderUsage &&
             affected.Which() == fuchsia::media::Usage::Tag::kRenderUsage) {
    active_rules_.SetRule(active.render_usage(), affected.render_usage(), behavior);
  }
}

AudioAdmin::~AudioAdmin() { Shutdown(); }

zx_status_t AudioAdmin::Init() {
  LoadDefaults();
  return ZX_OK;
}

bool AudioAdmin::IsActive(fuchsia::media::AudioRenderUsage usage) {
  auto usage_index = fidl::ToUnderlying(usage);
  return active_streams_playback_[usage_index].size() > 0;
}

bool AudioAdmin::IsActive(fuchsia::media::AudioCaptureUsage usage) {
  auto usage_index = fidl::ToUnderlying(usage);
  return active_streams_capture_[usage_index].size() > 0;
}

void AudioAdmin::SetUsageNone(fuchsia::media::AudioRenderUsage usage) {
  service_->SetRenderUsageGainAdjustment(usage, none_gain_db_);
}

void AudioAdmin::SetUsageNone(fuchsia::media::AudioCaptureUsage usage) {
  service_->SetCaptureUsageGainAdjustment(usage, none_gain_db_);
}

void AudioAdmin::SetUsageMute(fuchsia::media::AudioRenderUsage usage) {
  service_->SetRenderUsageGainAdjustment(usage, mute_gain_db_);
}

void AudioAdmin::SetUsageMute(fuchsia::media::AudioCaptureUsage usage) {
  service_->SetCaptureUsageGainAdjustment(usage, mute_gain_db_);
}

void AudioAdmin::SetUsageDuck(fuchsia::media::AudioRenderUsage usage) {
  service_->SetRenderUsageGainAdjustment(usage, duck_gain_db_);
}

void AudioAdmin::SetUsageDuck(fuchsia::media::AudioCaptureUsage usage) {
  service_->SetCaptureUsageGainAdjustment(usage, duck_gain_db_);
}

void AudioAdmin::ApplyPolicies(fuchsia::media::AudioCaptureUsage active) {
  for (int i = 0; i < fuchsia::media::RENDER_USAGE_COUNT; i++) {
    auto affected = static_cast<fuchsia::media::AudioRenderUsage>(i);
    switch (active_rules_.GetPolicy(active, affected)) {
      case fuchsia::media::Behavior::NONE:
        SetUsageNone(affected);
        break;
      case fuchsia::media::Behavior::DUCK:
        SetUsageDuck(affected);
        break;
      case fuchsia::media::Behavior::MUTE:
        SetUsageMute(affected);
        break;
    }
  }
  for (int i = 0; i < fuchsia::media::CAPTURE_USAGE_COUNT; i++) {
    auto affected = static_cast<fuchsia::media::AudioCaptureUsage>(i);
    switch (active_rules_.GetPolicy(active, affected)) {
      case fuchsia::media::Behavior::NONE:
        SetUsageNone(affected);
        break;
      case fuchsia::media::Behavior::DUCK:
        SetUsageDuck(affected);
        break;
      case fuchsia::media::Behavior::MUTE:
        SetUsageMute(affected);
        break;
    }
  }
}

void AudioAdmin::ApplyPolicies(fuchsia::media::AudioRenderUsage active) {
  for (int i = 0; i < fuchsia::media::RENDER_USAGE_COUNT; i++) {
    auto affected = static_cast<fuchsia::media::AudioRenderUsage>(i);
    switch (active_rules_.GetPolicy(active, affected)) {
      case fuchsia::media::Behavior::NONE:
        SetUsageNone(affected);
        break;
      case fuchsia::media::Behavior::DUCK:
        SetUsageDuck(affected);
        break;
      case fuchsia::media::Behavior::MUTE:
        SetUsageMute(affected);
        break;
    }
  }
  for (int i = 0; i < fuchsia::media::CAPTURE_USAGE_COUNT; i++) {
    auto affected = static_cast<fuchsia::media::AudioCaptureUsage>(i);
    switch (active_rules_.GetPolicy(active, affected)) {
      case fuchsia::media::Behavior::NONE:
        SetUsageNone(affected);
        break;
      case fuchsia::media::Behavior::DUCK:
        SetUsageDuck(affected);
        break;
      case fuchsia::media::Behavior::MUTE:
        SetUsageMute(affected);
        break;
    }
  }
}

void AudioAdmin::UpdatePolicy() {
  // TODO(perley): convert this to an array of Usage unions or something else
  //               that makes it at least a little flexible.
  // The processing order of this represents the 'priorities' of the streams
  // with this implementation.
  if (IsActive(fuchsia::media::AudioCaptureUsage::COMMUNICATION)) {
    ApplyPolicies(fuchsia::media::AudioCaptureUsage::COMMUNICATION);
  } else if (IsActive(fuchsia::media::AudioRenderUsage::COMMUNICATION)) {
    ApplyPolicies(fuchsia::media::AudioRenderUsage::COMMUNICATION);
  } else if (IsActive(fuchsia::media::AudioCaptureUsage::SYSTEM_AGENT)) {
    ApplyPolicies(fuchsia::media::AudioCaptureUsage::SYSTEM_AGENT);
  } else if (IsActive(fuchsia::media::AudioRenderUsage::SYSTEM_AGENT)) {
    ApplyPolicies(fuchsia::media::AudioRenderUsage::SYSTEM_AGENT);
  } else if (IsActive(fuchsia::media::AudioRenderUsage::INTERRUPTION)) {
    ApplyPolicies(fuchsia::media::AudioRenderUsage::INTERRUPTION);
  } else if (IsActive(fuchsia::media::AudioCaptureUsage::FOREGROUND)) {
    ApplyPolicies(fuchsia::media::AudioCaptureUsage::FOREGROUND);
  } else if (IsActive(fuchsia::media::AudioRenderUsage::MEDIA)) {
    ApplyPolicies(fuchsia::media::AudioRenderUsage::MEDIA);
  } else if (IsActive(fuchsia::media::AudioCaptureUsage::BACKGROUND)) {
    ApplyPolicies(fuchsia::media::AudioCaptureUsage::BACKGROUND);
  } else {
    ApplyPolicies(fuchsia::media::AudioRenderUsage::BACKGROUND);
  }
}

void AudioAdmin::UpdateRendererState(fuchsia::media::AudioRenderUsage usage, bool active,
                                     fuchsia::media::AudioRenderer* renderer) {
  auto usage_index = fidl::ToUnderlying(usage);
  FXL_DCHECK(usage_index < fuchsia::media::RENDER_USAGE_COUNT);
  if (active) {
    active_streams_playback_[usage_index].insert(renderer);
  } else {
    active_streams_playback_[usage_index].erase(renderer);
  }

  UpdatePolicy();
}

void AudioAdmin::UpdateCapturerState(fuchsia::media::AudioCaptureUsage usage, bool active,
                                     fuchsia::media::AudioCapturer* capturer) {
  auto usage_index = fidl::ToUnderlying(usage);
  FXL_DCHECK(usage_index < fuchsia::media::CAPTURE_USAGE_COUNT);
  if (active) {
    active_streams_capture_[usage_index].insert(capturer);
  } else {
    active_streams_capture_[usage_index].erase(capturer);
  }

  UpdatePolicy();
}

void AudioAdmin::Shutdown() {}

void AudioAdmin::PolicyRules::ResetInteractions() {
  for (int i = 0; i < fuchsia::media::RENDER_USAGE_COUNT; i++) {
    auto active = static_cast<fuchsia::media::AudioRenderUsage>(i);
    for (int j = 0; j < fuchsia::media::RENDER_USAGE_COUNT; j++) {
      auto affected = static_cast<fuchsia::media::AudioRenderUsage>(j);
      SetRule(active, affected, fuchsia::media::Behavior::NONE);
    }
    for (int j = 0; j < fuchsia::media::CAPTURE_USAGE_COUNT + 1; j++) {
      auto affected = static_cast<fuchsia::media::AudioCaptureUsage>(j);
      SetRule(active, affected, fuchsia::media::Behavior::NONE);
    }
  }
  for (int i = 0; i < fuchsia::media::CAPTURE_USAGE_COUNT; i++) {
    auto active = static_cast<fuchsia::media::AudioCaptureUsage>(i);
    for (int j = 0; j < fuchsia::media::RENDER_USAGE_COUNT; j++) {
      auto affected = static_cast<fuchsia::media::AudioRenderUsage>(j);
      SetRule(active, affected, fuchsia::media::Behavior::NONE);
    }
    for (int j = 0; j < fuchsia::media::CAPTURE_USAGE_COUNT; j++) {
      auto affected = static_cast<fuchsia::media::AudioCaptureUsage>(j);
      SetRule(active, affected, fuchsia::media::Behavior::NONE);
    }
  }
}

}  // namespace audio
}  // namespace media
