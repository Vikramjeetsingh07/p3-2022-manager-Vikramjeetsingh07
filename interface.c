#include "interface.h"
#include "vmm.h"
#include <sys/mman.h>
#include <signal.h>

// Interface implementation
// Implement APIs here...

void mm_init(enum policy_type policy, void *vm, int vm_size, int num_frames, int page_size)
{
    initVmm( policy, vm, vm_size, num_frames, page_size) ;
}









