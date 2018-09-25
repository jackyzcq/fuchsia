// Copyright 2018 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#include <dev/address_provider/address_provider.h>
#include <lib/pci/pio.h>

zx_status_t PioPcieAddressProvider::Translate(const uint bus_id,
                                              const uint device_id,
                                              const uint function_id,
                                              vaddr_t* virt,
                                              paddr_t* phys) {
    *virt = Pci::PciBdfAddr(static_cast<uint8_t>(bus_id),
                            static_cast<uint8_t>(device_id),
                            static_cast<uint8_t>(function_id), 0);
    return ZX_OK;
}

fbl::RefPtr<PciConfig> PioPcieAddressProvider::CreateConfig(const uintptr_t addr) {
    return PciConfig::Create(addr, PciAddrSpace::PIO);
}