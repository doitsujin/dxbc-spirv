#pragma once

namespace dxbc_spv::ir {

/**


/** Pass to investigate and fix up shader I/O for various use cases.
 *
 * This includes adjusting I/O locations for tessellation shaders to meet
 * Vulkan requirements, and moving streamout locations to dedicated output
 * locations if necessary and deduplicating multi-stream GS outputs in general. */
class LowerIoPass {

public:

private:



};

}
