// Copyright 2016 The Fuchsia Authors
// Copyright (c) 2012-2015 Travis Geiselbrecht
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#include "arm_gicv2.h"

#include <assert.h>
#include <bits.h>
#include <debug.h>
#include <inttypes.h>
#include <lib/ktrace.h>
#include <lib/root_resource_filter.h>
#include <lib/system-topology.h>
#include <sys/types.h>
#include <trace.h>
#include <zircon/boot/driver-config.h>
#include <zircon/errors.h>
#include <zircon/types.h>

#include <arch/arm64/hypervisor/gic/gicv2.h>
#include <arch/arm64/periphmap.h>
#include <arch/ops.h>
#include <arch/regs.h>
#include <dev/interrupt.h>
#include <dev/interrupt/arm_gic_common.h>
#include <dev/interrupt/arm_gicv2_regs.h>
#include <dev/interrupt/arm_gicv2m.h>
#include <dev/interrupt/arm_gicv2m_msi.h>
#include <kernel/cpu.h>
#include <kernel/stats.h>
#include <kernel/thread.h>
#include <ktl/iterator.h>
#include <lk/init.h>
#include <pdev/driver.h>
#include <pdev/interrupt.h>

#define LOCAL_TRACE 0

#include <arch/arm64.h>
#define IFRAME_PC(frame) ((frame)->elr)

static SpinLock gicd_lock;

// values read from zbi
vaddr_t arm_gicv2_gic_base = 0;
uint64_t arm_gicv2_gicd_offset = 0;
uint64_t arm_gicv2_gicc_offset = 0;
uint64_t arm_gicv2_gich_offset = 0;
uint64_t arm_gicv2_gicv_offset = 0;
static uint32_t ipi_base = 0;

static uint max_irqs = 0;

static arm_gicv2::CpuMaskTranslator mask_translator;

static zx_status_t arm_gic_init();

static zx_status_t gic_configure_interrupt(unsigned int vector, enum interrupt_trigger_mode tm,
                                           enum interrupt_polarity pol);

static void suspend_resume_fiq(bool resume_gicc, bool resume_gicd) {}

static uint32_t read_gicd_targetsr(int target_reg) { return GICREG(0, GICD_ITARGETSR(target_reg)); }

uint8_t gic_determine_local_mask(fbl::Function<uint32_t(int)> fetch_gicd_targetsr_reg) {
  union {
    char bytes[4];
    uint32_t value;
  } decoder;

  // The first 7 GICD_ITARGETSR registers only return values that apply to the
  // calling CPU. These are interrupt target registers so each byte is a cpu_mask
  // of what cpus are targeted by their cpu_mask from GIC's perspective. The
  // upside is that we can scan these target regsiters to determine what our
  // cpu_mask is from the GIC's perspective.
  for (int target_reg = 0; target_reg < 8; target_reg++) {
    decoder.value = fetch_gicd_targetsr_reg(target_reg);
    for (int i = 0; i < 4; i++) {
      if (decoder.bytes[i] != 0) {
        return decoder.bytes[i];
      }
    }
  }

  printf("GICv2: unable to determine local GIC mask!\n");
  return 0;
}

static bool gic_is_valid_interrupt(uint vector, uint32_t flags) { return (vector < max_irqs); }

static uint32_t gic_get_base_vector() {
  // ARM Generic Interrupt Controller v2 chapter 2.1
  // INTIDs 0-15 are local CPU interrupts
  return 16;
}

static uint32_t gic_get_max_vector() { return max_irqs; }

static void gic_set_enable(uint vector, bool enable) {
  int reg = vector / 32;
  uint32_t mask = (uint32_t)(1ULL << (vector % 32));

  if (enable) {
    GICREG(0, GICD_ISENABLER(reg)) = mask;
  } else {
    GICREG(0, GICD_ICENABLER(reg)) = mask;
  }
}

static void gic_init_percpu_early() {
  GICREG(0, GICC_CTLR) = 0x201;  // EnableGrp1 and EOImodeNS
  GICREG(0, GICC_PMR) = 0xff;    // unmask interrupts at all priority levels
}

// TODO(fxbug.dev/38135): This function is potentially unused and may be removable.
[[maybe_unused]] static void arm_gic_suspend_cpu(uint level) { suspend_resume_fiq(false, false); }

// TODO(fxbug.dev/38135): This function is potentially unused and may be removable.
[[maybe_unused]] static void arm_gic_resume_cpu(uint level) {
  interrupt_saved_state_t state;
  bool resume_gicd = false;

  gicd_lock.AcquireIrqSave(state);
  if (!(GICREG(0, GICD_CTLR) & 1)) {
    dprintf(SPEW, "%s: distributor is off, calling arm_gic_init instead\n", __func__);
    arm_gic_init();
    resume_gicd = true;
  } else {
    gic_init_percpu_early();
  }
  gicd_lock.ReleaseIrqRestore(state);
  suspend_resume_fiq(true, resume_gicd);
}

// disable for now. we will need to add suspend/resume support to dev/pdev for this to work
#if 0
LK_INIT_HOOK_FLAGS(arm_gic_suspend_cpu, arm_gic_suspend_cpu,
                   LK_INIT_LEVEL_PLATFORM, LK_INIT_FLAG_CPU_SUSPEND);

LK_INIT_HOOK_FLAGS(arm_gic_resume_cpu, arm_gic_resume_cpu,
                   LK_INIT_LEVEL_PLATFORM, LK_INIT_FLAG_CPU_RESUME);
#endif

static unsigned int arm_gic_max_cpu() { return (GICREG(0, GICD_TYPER) >> 5) & 0x7; }

static zx_status_t arm_gic_init() {
  uint i;

  uint32_t typer = GICREG(0, GICD_TYPER);
  uint32_t it_lines_number = BITS_SHIFT(typer, 4, 0);
  max_irqs = (it_lines_number + 1) * 32;
  printf("GICv2 detected: max interrupts %u, max_cpus %u, TYPER %#x\n", max_irqs, arm_gic_max_cpu(),
         typer);
  DEBUG_ASSERT(max_irqs <= MAX_INT);

  for (i = 0; i < max_irqs; i += 32) {
    GICREG(0, GICD_ICENABLER(i / 32)) = ~0;
    GICREG(0, GICD_ICPENDR(i / 32)) = ~0;
  }

  if (arm_gic_max_cpu() > 0) {
    // Set external interrupts to target cpu 0
    for (i = 32; i < max_irqs; i += 4) {
      GICREG(0, GICD_ITARGETSR(i / 4)) = 0x01010101;
    }
  }
  // Initialize all the SPIs to edge triggered
  for (i = GIC_BASE_SPI; i < max_irqs; i++) {
    gic_configure_interrupt(i, IRQ_TRIGGER_MODE_EDGE, IRQ_POLARITY_ACTIVE_HIGH);
  }

  GICREG(0, GICD_CTLR) = 1;  // enable GIC0

  gic_init_percpu_early();

  return ZX_OK;
}

static zx_status_t arm_gic_sgi(unsigned int irq, unsigned int flags, unsigned int cpu_mask) {
  unsigned int val = ((flags & ARM_GIC_SGI_FLAG_TARGET_FILTER_MASK) << 24) |
                     ((cpu_mask & 0xff) << 16) | ((flags & ARM_GIC_SGI_FLAG_NS) ? (1U << 15) : 0) |
                     (irq & 0xf);

  if (irq >= 16) {
    return ZX_ERR_INVALID_ARGS;
  }

  LTRACEF("GICD_SGIR: %x\n", val);

  GICREG(0, GICD_SGIR) = val;

  return ZX_OK;
}

static zx_status_t gic_mask_interrupt(unsigned int vector) {
  LTRACEF("vector %u\n", vector);
  if (vector >= max_irqs) {
    return ZX_ERR_INVALID_ARGS;
  }

  gic_set_enable(vector, false);

  return ZX_OK;
}

static zx_status_t gic_unmask_interrupt(unsigned int vector) {
  LTRACEF("vector %u\n", vector);
  if (vector >= max_irqs) {
    return ZX_ERR_INVALID_ARGS;
  }

  gic_set_enable(vector, true);

  return ZX_OK;
}

static zx_status_t gic_deactivate_interrupt(unsigned int vector) {
  if (vector >= max_irqs) {
    return ZX_ERR_INVALID_ARGS;
  }

  uint32_t reg = 1 << (vector % 32);
  GICREG(0, GICD_ICACTIVER(vector / 32)) = reg;

  return ZX_OK;
}

static zx_status_t gic_configure_interrupt(unsigned int vector, enum interrupt_trigger_mode tm,
                                           enum interrupt_polarity pol) {
  // Only configurable for SPI interrupts
  if ((vector >= max_irqs) || (vector < GIC_BASE_SPI)) {
    return ZX_ERR_INVALID_ARGS;
  }

  if (pol != IRQ_POLARITY_ACTIVE_HIGH) {
    // TODO: polarity should actually be configure through a GPIO controller
    return ZX_ERR_NOT_SUPPORTED;
  }

  // type is encoded with two bits, MSB of the two determine type
  // 16 irqs encoded per ICFGR register
  uint32_t reg_ndx = vector >> 4;
  uint32_t bit_shift = ((vector & 0xf) << 1) + 1;
  uint32_t reg_val = GICREG(0, GICD_ICFGR(reg_ndx));
  if (tm == IRQ_TRIGGER_MODE_EDGE) {
    reg_val |= (1 << bit_shift);
  } else {
    reg_val &= ~(1 << bit_shift);
  }
  GICREG(0, GICD_ICFGR(reg_ndx)) = reg_val;

  const uint32_t clear_reg = vector / 32;
  const uint32_t clear_mask = 1 << (vector % 32);
  GICREG(0, GICD_ICPENDR(clear_reg)) = clear_mask;

  return ZX_OK;
}

static zx_status_t gic_get_interrupt_config(unsigned int vector, enum interrupt_trigger_mode* tm,
                                            enum interrupt_polarity* pol) {
  if (vector >= max_irqs) {
    return ZX_ERR_INVALID_ARGS;
  }

  if (tm) {
    *tm = IRQ_TRIGGER_MODE_EDGE;
  }
  if (pol) {
    *pol = IRQ_POLARITY_ACTIVE_HIGH;
  }

  return ZX_OK;
}

static unsigned int gic_remap_interrupt(unsigned int vector) { return vector; }

static void gic_handle_irq(iframe_t* frame) {
  // get the current vector
  uint32_t iar = GICREG(0, GICC_IAR);
  unsigned int vector = iar & 0x3ff;

  if (vector >= 0x3fe) {
    // spurious
    return;
  }

  // tracking external hardware irqs in this variable
  if (vector >= 32)
    CPU_STATS_INC(interrupts);

  cpu_num_t cpu = arch_curr_cpu_num();

  ktrace_tiny(TAG_IRQ_ENTER, (vector << 8) | cpu);

  LTRACEF_LEVEL(2, "iar 0x%x cpu %u currthread %p vector %u pc %#" PRIxPTR "\n", iar, cpu,
                Thread::Current::Get(), vector, (uintptr_t)IFRAME_PC(frame));

  // deliver the interrupt
  interrupt_eoi eoi;
  if (!pdev_invoke_int_if_present(vector, &eoi)) {
    eoi = IRQ_EOI_DEACTIVATE;
  }
  GICREG(0, GICC_EOIR) = iar;
  if (eoi == IRQ_EOI_DEACTIVATE) {
    GICREG(0, GICC_DIR) = iar;
  }

  LTRACEF_LEVEL(2, "cpu %u exit\n", cpu);

  ktrace_tiny(TAG_IRQ_EXIT, (vector << 8) | cpu);
}

static void gic_handle_fiq(iframe_t* frame) { PANIC_UNIMPLEMENTED; }

static void gic_send_ipi(cpu_mask_t logical_target, mp_ipi_t ipi) {
  const cpu_mask_t target = mask_translator.LogicalMaskToGic(logical_target);

  uint gic_ipi_num = ipi + ipi_base;

  if (target != 0) {
    LTRACEF("target 0x%x, gic_ipi %u\n", target, gic_ipi_num);
    arm_gic_sgi(gic_ipi_num, ARM_GIC_SGI_FLAG_NS, target);
  }
}

static interrupt_eoi arm_ipi_halt_handler(void*) {
  LTRACEF("cpu %u\n", arch_curr_cpu_num());

  arch_disable_ints();
  while (true) {
  }

  return IRQ_EOI_DEACTIVATE;
}

static void gic_init_percpu() {
  mp_set_curr_cpu_online(true);
  unmask_interrupt(MP_IPI_GENERIC + ipi_base);
  unmask_interrupt(MP_IPI_RESCHEDULE + ipi_base);
  unmask_interrupt(MP_IPI_INTERRUPT + ipi_base);
  unmask_interrupt(MP_IPI_HALT + ipi_base);

  const cpu_num_t logical_num = arch_curr_cpu_num();

  system_topology::Node* node = nullptr;
  auto status =
      system_topology::Graph::GetSystemTopology().ProcessorByLogicalId(logical_num, &node);
  if (status == ZX_OK) {
    DEBUG_ASSERT(node->entity_type == ZBI_TOPOLOGY_ENTITY_PROCESSOR &&
                 node->entity.processor.architecture == ZBI_TOPOLOGY_ARCH_ARM);
    mask_translator.SetGicIdForLogicalId(logical_num,
                                         node->entity.processor.architecture_info.arm.gic_id);
  } else {
    printf("arm_gicv2: unable to get logical processor %u in topology, status: %d\n", logical_num,
           status);
  }

  // Lookup the gic_mask for this processor via GIC registers and compare it
  // to what we were told by the bootloader, warn if there is a mismatch.
  const cpu_mask_t gic_mask = gic_determine_local_mask(read_gicd_targetsr);
  const cpu_mask_t assigned_gic_mask = mask_translator.GetGicMask(logical_num);
  if (gic_mask != assigned_gic_mask) {
    printf(
        "arm_gicv2: WARNING assigned gic_id of %u does not match processor's gic_id of %u."
        "Successful operation is unlikely!",
        assigned_gic_mask, gic_mask);
  }
  LTRACEF("logical_cpu_mask: %u programmatic_gic_mask: %u assigned_gic_mask: %u\n",
          cpu_num_to_mask(arch_curr_cpu_num()), gic_mask, assigned_gic_mask);
}

static void gic_shutdown() {
  // Turn off all GIC0 interrupts at the distributor.
  GICREG(0, GICD_CTLR) = 0;
}

// Returns true if any PPIs are enabled on the calling CPU.
static bool is_ppi_enabled() {
  DEBUG_ASSERT(arch_ints_disabled());

  // PPIs are 16-31.
  uint32_t ppi_mask = 0xffff0000;

  // GICD_ISENABLER0 is banked so it corresponds to *this* CPU's interface.
  return (GICREG(0, GICD_ISENABLER(0)) & ppi_mask) != 0;
}

// Returns true if any SPIs are enabled on the calling CPU.
static bool is_spi_enabled() {
  DEBUG_ASSERT(arch_ints_disabled());

  // We're going to check four interrupts at a time.  Build a repeated mask for the current CPU.
  // Each byte in the mask is a CPU bit mask corresponding to CPU0..CPU7 (lsb..msb).
  cpu_num_t cpu_num = arch_curr_cpu_num();
  DEBUG_ASSERT(cpu_num < 8);
  uint32_t mask = 0x01010101U << cpu_num;

  for (unsigned int vector = GIC_BASE_SPI; vector < max_irqs; vector += 4) {
    uint32_t reg = GICREG(0, GICD_ITARGETSR(vector / 4));
    if (reg & mask) {
      return true;
    }
  }

  return false;
}

static void gic_shutdown_cpu() {
  DEBUG_ASSERT(arch_ints_disabled());

  // If we're running on a secondary CPU there's a good chance this CPU will be powered off shortly
  // (PSCI_CPU_OFF).  Sending an interrupt to a CPU that's been powered off may result in an
  // "erronerous state" (see Power State Coordination Interface (PSCI) System Software on ARM
  // specification, 5.5.2).  So before we shutdown the GIC, make sure we've migrated/disabled any
  // and all peripheral interrupts targeted at this CPU (PPIs and SPIs).
  //
  // Note, we don't perform these checks on the boot CPU because we don't call PSCI_CPU_OFF on the
  // boot CPU, and we likely still have PPIs and SPIs targeting the boot CPU.
  DEBUG_ASSERT(arch_curr_cpu_num() == BOOT_CPU_ID || !is_ppi_enabled());
  DEBUG_ASSERT(arch_curr_cpu_num() == BOOT_CPU_ID || !is_spi_enabled());

  // Turn off interrupts at the CPU interface.
  GICREG(0, GICC_CTLR) = 0;
}

static const struct pdev_interrupt_ops gic_ops = {
    .mask = gic_mask_interrupt,
    .unmask = gic_unmask_interrupt,
    .deactivate = gic_deactivate_interrupt,
    .configure = gic_configure_interrupt,
    .get_config = gic_get_interrupt_config,
    .is_valid = gic_is_valid_interrupt,
    .get_base_vector = gic_get_base_vector,
    .get_max_vector = gic_get_max_vector,
    .remap = gic_remap_interrupt,
    .send_ipi = gic_send_ipi,
    .init_percpu_early = gic_init_percpu_early,
    .init_percpu = gic_init_percpu,
    .handle_irq = gic_handle_irq,
    .handle_fiq = gic_handle_fiq,
    .shutdown = gic_shutdown,
    .shutdown_cpu = gic_shutdown_cpu,
    .msi_is_supported = arm_gicv2m_msi_is_supported,
    .msi_supports_masking = arm_gicv2m_msi_supports_masking,
    .msi_mask_unmask = arm_gicv2m_msi_mask_unmask,
    .msi_alloc_block = arm_gicv2m_msi_alloc_block,
    .msi_free_block = arm_gicv2m_msi_free_block,
    .msi_register_handler = arm_gicv2m_msi_register_handler,
};

static void arm_gic_v2_init_early(const void* driver_data, uint32_t length) {
  ASSERT(length >= sizeof(dcfg_arm_gicv2_driver_t));
  auto driver = static_cast<const dcfg_arm_gicv2_driver_t*>(driver_data);
  ASSERT(driver->mmio_phys);

  arm_gicv2_gic_base = periph_paddr_to_vaddr(driver->mmio_phys);
  ASSERT(arm_gicv2_gic_base);
  arm_gicv2_gicd_offset = driver->gicd_offset;
  arm_gicv2_gicc_offset = driver->gicc_offset;
  arm_gicv2_gich_offset = driver->gich_offset;
  arm_gicv2_gicv_offset = driver->gicv_offset;
  ipi_base = driver->ipi_base;

  if (arm_gic_init() != ZX_OK) {
    if (driver->optional) {
      // failed to detect gic v2 but it's marked optional. continue
      return;
    }
    printf("GICv2: failed to detect GICv2, interrupts will be broken\n");
    return;
  }

  dprintf(SPEW, "GICv2 (ID %#x), IPI base %u\n", GICREG(0, GICC_IIDR), ipi_base);

  // pass the list of physical and virtual addresses for the GICv2m register apertures
  if (driver->msi_frame_phys) {
    // the following arrays must be static because arm_gicv2m_init stashes the pointer
    static paddr_t GICV2M_REG_FRAMES[] = {0};
    static vaddr_t GICV2M_REG_FRAMES_VIRT[] = {0};

    GICV2M_REG_FRAMES[0] = driver->msi_frame_phys;
    GICV2M_REG_FRAMES_VIRT[0] = periph_paddr_to_vaddr(driver->msi_frame_phys);
    ASSERT(GICV2M_REG_FRAMES_VIRT[0]);
    arm_gicv2m_init(GICV2M_REG_FRAMES, GICV2M_REG_FRAMES_VIRT, ktl::size(GICV2M_REG_FRAMES));
  }
  pdev_register_interrupts(&gic_ops);

  zx_status_t status = gic_register_sgi_handler(MP_IPI_GENERIC + ipi_base, &mp_mbx_generic_irq);
  DEBUG_ASSERT(status == ZX_OK);
  status = gic_register_sgi_handler(MP_IPI_RESCHEDULE + ipi_base, &mp_mbx_reschedule_irq);
  DEBUG_ASSERT(status == ZX_OK);
  status = gic_register_sgi_handler(MP_IPI_INTERRUPT + ipi_base, &mp_mbx_interrupt_irq);
  DEBUG_ASSERT(status == ZX_OK);
  status = gic_register_sgi_handler(MP_IPI_HALT + ipi_base, &arm_ipi_halt_handler);
  DEBUG_ASSERT(status == ZX_OK);

  gicv2_hw_interface_register();
}

static void arm_gic_v2_init_deny_regions(const void* driver_data, uint32_t length) {
  // Place the physical address of the GICv2 registers on the MMIO deny list.
  // Users will not be able to create MMIO resources which permit mapping of the
  // GIC registers, even if they have access to the root resource.
  ASSERT(length >= sizeof(dcfg_arm_gicv2_driver_t));
  auto driver = static_cast<const dcfg_arm_gicv2_driver_t*>(driver_data);
  ASSERT(driver->mmio_phys);

  root_resource_filter_add_deny_region(driver->mmio_phys + driver->gicc_offset, GICC_REG_SIZE,
                                       ZX_RSRC_KIND_MMIO);
  root_resource_filter_add_deny_region(driver->mmio_phys + driver->gicd_offset, GICD_REG_SIZE,
                                       ZX_RSRC_KIND_MMIO);
  if (driver->gich_offset) {
    root_resource_filter_add_deny_region(driver->mmio_phys + driver->gich_offset, GICH_REG_SIZE,
                                         ZX_RSRC_KIND_MMIO);
  }
  if (driver->gicv_offset) {
    root_resource_filter_add_deny_region(driver->mmio_phys + driver->gich_offset, GICV_REG_SIZE,
                                         ZX_RSRC_KIND_MMIO);
  }
  if (driver->msi_frame_phys) {
    root_resource_filter_add_deny_region(driver->msi_frame_phys, GICV2M_FRAME_REG_SIZE,
                                         ZX_RSRC_KIND_MMIO);
  }
}

LK_PDEV_INIT(arm_gic_v2_init_early, KDRV_ARM_GIC_V2, arm_gic_v2_init_early,
             LK_INIT_LEVEL_PLATFORM_EARLY)
LK_PDEV_INIT(arm_gic_v2_init, KDRV_ARM_GIC_V2, arm_gic_v2_init_deny_regions,
             LK_INIT_LEVEL_PLATFORM + 1)
