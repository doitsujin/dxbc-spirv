#pragma once

#include "../ir/ir.h"

namespace dxbc_spv::spirv {

/** Descriptor set and binding */
struct DescriptorBinding {
  uint32_t set = 0u;;
  uint32_t binding = 0u;
};


/** Descriptor and push constant re-mapping interface for SPIR-V lowering.
 *
 * Users of the SPIR-V lowering can provide their own implmentation of this
 * interface in order to map D3D descriptor bindings to Vulkan descriptor
 * set and binding indices. */
class ResourceMapping {

public:

  virtual ~ResourceMapping();

  /** Computes set and binindg mapping for a resource descriptor */
  virtual DescriptorBinding mapDescriptor(
        ir::ScalarType          type,
        uint32_t                regSpace,
        uint32_t                regIndex) = 0;

  /** Computes base offset for a push constant block based on
   *  shader stages declared to access that push constant block. */
  virtual uint32_t mapPushData(
        ir::ShaderStageMask     stages) = 0;

};


/** Basic SPIR-V resource mapping for internal use. */
class BasicResourceMapping : public ResourceMapping {

public:

  BasicResourceMapping();

  ~BasicResourceMapping();

  DescriptorBinding mapDescriptor(
        ir::ScalarType          type,
        uint32_t                regSpace,
        uint32_t                regIndex) override;

  uint32_t mapPushData(
        ir::ShaderStageMask     stages) override;

private:

  uint32_t m_descriptorIndex = 0u;

};

}
