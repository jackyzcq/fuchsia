// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "coding_traits.h"
#include "encoder.h"

namespace fidl {

template <class T>
zx_status_t EncodeObject(T* object, std::vector<uint8_t>* output, const char** error_msg_out) {
  Encoder encoder(Encoder::NO_HEADER, ::fidl::internal::WireFormatVersion::kV1);
  object->Encode(&encoder, encoder.Alloc(EncodingInlineSize<T, fidl::Encoder>(&encoder)));
  if (encoder.CurrentHandleCount() != 0) {
    if (error_msg_out != nullptr) {
      *error_msg_out = "Cannot encode handles with object encoding";
    }
    return ZX_ERR_INVALID_ARGS;
  }
  *output = encoder.TakeBytes();
  return ZX_OK;
}

template <class T>
zx_status_t DecodeObject(uint8_t* bytes, size_t bytes_length, T* object,
                         const char** error_msg_out) {
  HLCPPIncomingMessage msg(
      BytePart(bytes, static_cast<uint32_t>(bytes_length), static_cast<uint32_t>(bytes_length)),
      HandleInfoPart());
  zx_status_t status = msg.Decode(T::FidlType, error_msg_out);
  if (status != ZX_OK) {
    return status;
  }
  Decoder decoder(std::move(msg));
  T::Decode(&decoder, object, 0);
  return ZX_OK;
}

template <class T>
zx_status_t ValidateObject(uint8_t* bytes, size_t bytes_length, T* object,
                           const char** error_msg_out) {
  return HLCPPOutgoingMessage(BytePart(bytes, static_cast<uint32_t>(bytes_length),
                                       static_cast<uint32_t>(bytes_length)),
                              HandleDispositionPart())
      .Validate(T::FidlType, error_msg_out);
}

}  // namespace fidl
