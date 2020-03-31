// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <ddk/binding.h>
#include <ddk/debug.h>
#include <ddk/platform-defs.h>
#include <ddk/protocol/i2cimpl.h>
#include <fbl/algorithm.h>
#include <soc/vs680/vs680-hw.h>

#include "luis.h"

namespace board_luis {

zx_status_t Luis::SdioInit() {
  constexpr zx_bind_inst_t root_match[] = {
      BI_MATCH(),
  };

  constexpr zx_bind_inst_t expander2_i2c_match[] = {
      BI_ABORT_IF(NE, BIND_PROTOCOL, ZX_PROTOCOL_I2C),
      BI_ABORT_IF(NE, BIND_I2C_BUS_ID, 0),
      BI_MATCH_IF(EQ, BIND_I2C_ADDRESS, 0x43),
  };
  constexpr zx_bind_inst_t expander3_i2c_match[] = {
      BI_ABORT_IF(NE, BIND_PROTOCOL, ZX_PROTOCOL_I2C),
      BI_ABORT_IF(NE, BIND_I2C_BUS_ID, 0),
      BI_MATCH_IF(EQ, BIND_I2C_ADDRESS, 0x44),
  };

  const device_fragment_part_t expander2_fragment[] = {
      {fbl::count_of(root_match), root_match},
      {fbl::count_of(expander2_i2c_match), expander2_i2c_match},
  };
  const device_fragment_part_t expander3_fragment[] = {
      {fbl::count_of(root_match), root_match},
      {fbl::count_of(expander3_i2c_match), expander3_i2c_match},
  };

  const device_fragment_t sdio_fragments[] = {
      {fbl::count_of(expander2_fragment), expander2_fragment},
      {fbl::count_of(expander3_fragment), expander3_fragment},
  };

  constexpr pbus_mmio_t sdio_mmios[] = {
      {
          .base = vs680::kSdioBase,
          .length = vs680::kSdioSize,
      },
      {
          .base = vs680::kChipCtrlBase,
          .length = vs680::kChipCtrlSize,
      },
  };

  constexpr pbus_irq_t sdio_irqs[] = {
      {
          .irq = vs680::kSdioIrq,
          .mode = ZX_INTERRUPT_MODE_LEVEL_HIGH,
      },
  };

  constexpr pbus_bti_t sdio_btis[] = {
      {
          .iommu_index = 0,
          .bti_id = BTI_SDIO,
      },
  };

  pbus_dev_t sdio_dev = {};
  sdio_dev.name = "vs680-sdio";
  sdio_dev.vid = PDEV_VID_SYNAPTICS;
  sdio_dev.pid = PDEV_PID_SYNAPTICS_VS680;
  sdio_dev.did = PDEV_DID_VS680_SDHCI1;
  sdio_dev.irq_list = sdio_irqs;
  sdio_dev.irq_count = countof(sdio_irqs);
  sdio_dev.mmio_list = sdio_mmios;
  sdio_dev.mmio_count = countof(sdio_mmios);
  sdio_dev.bti_list = sdio_btis;
  sdio_dev.bti_count = countof(sdio_btis);

  zx_status_t status = pbus_.CompositeDeviceAdd(&sdio_dev, sdio_fragments,
                                                fbl::count_of(sdio_fragments), UINT32_MAX);
  if (status != ZX_OK) {
    zxlogf(ERROR, "%s: CompositeDeviceAdd() error: %d\n", __func__, status);
  }

  return status;
}

}  // namespace board_luis
