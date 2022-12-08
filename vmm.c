#include "vmm.h"

// Memory Manager implementation
// Implement all other functions here...

#define FIND_ADDRES(sgInfo) (char *)sgInfo->si_addr;
#define GET_PAGE_NUMBER(addr) ((addr - vmm.vm_start)/vmm.page_size) ;
#define GET_PAGE_OFFSET(addr) ((addr - vmm.vm_start) % vmm.page_size) ;

//vmm = (struct VMM *) malloc( sizeof(struct VMM) ) ;

void initVmm(enum policy_type policy, void *vm, int vm_size, int num_frames, int page_size)
{
    resetVmm() ;
    // initialize virtual memory parameters
    vmm.vm_start = vm ;
    vmm.vm_size = vm_size ;
    vmm.vm_policy = policy ;
    vmm.frames = num_frames ;
    vmm.page_size = page_size ;

    // create sigHandler for segmentation fault.
    struct sigaction sa ;
    sa.sa_sigaction= segFaultHandler ;
    sigemptyset(&(sa.sa_mask)) ;
    sa.sa_flags = SA_SIGINFO ;
    if(sigaction(SIGSEGV, &sa, NULL) !=0 ) 
        exit(-1) ;

    if(mprotect(vm, vm_size, PROT_NONE) != 0)
        exit(-1) ;

}

void segFaultHandler(int sig, siginfo_t* sigInfo, void *context) 
{
    resetLoggerData() ;

    char *faultAddr = FIND_ADDRES( sigInfo) ;
    bool writeEnabled = ((ucontext_t *)context)->uc_mcontext.gregs[REG_ERR] & (PF_WRITE);

    if( !validAddres(faultAddr) )
        exit(SIGSEGV) ;

    log_data.virtualPageNo = GET_PAGE_NUMBER(faultAddr) ;
    log_data.virtualPageOffset = GET_PAGE_OFFSET(faultAddr ) ;

    char* pageStartAddr = vmm.vm_start + log_data.virtualPageNo * vmm.page_size ;

    int temp = (log_data.virtualPageNo * vmm.page_size) ;

    bool newPage = false ;

    VM_Page* pagePtr = searchPage( pageStartAddr , &newPage ) ;

    if(newPage || pagePtr->frameNo == -1) //new page
    {
        handleNewPage( pagePtr, writeEnabled ) ;

        if( vmm.active_pages < vmm.frames )
        {
            pagePtr->frameNo = vmm.active_pages++ ;
            addToList(pagePtr) ;
        }
        else 
        {
            if( vmm.vm_policy == MM_FIFO)
            {
                handleFIFO( pagePtr, newPage ) ;
            }
            else
            {
                handleThirdReplacement( pagePtr, newPage ) ;
            }
        }
    }
    else
    {
        handleExistingPage(pagePtr, writeEnabled ) ;
    }

    mm_logger( log_data.virtualPageNo, 
            log_data.cause,
            log_data.pageEvicted,
            log_data.writeBack,
            (pagePtr->frameNo * vmm.page_size) + log_data.virtualPageOffset ) ;

}


bool validAddres(char* address) 
{
    if( address > (vmm.vm_start + vmm.vm_size -1 ) ||
            address < vmm.vm_start) 
        return false ;

    return true ;
}

VM_Page* searchPage(char* address, bool* newPage) 
{
    VM_Page* ptr = vmm.list_head ;

    while(ptr != NULL )
    {
        if(ptr->start_addr == address)
        {
            *newPage = false ;
            return ptr ;
        }
        ptr = ptr->next ;
    }

    VM_Page * pagePtr = (VM_Page *) malloc( sizeof(VM_Page) ) ;
    resetPage(pagePtr, false ) ;
    pagePtr->start_addr = address  ;
    *newPage = true ;
    return pagePtr ;
}

void handleExistingPage(VM_Page* pp, bool write) 
{
    if(pp->onlyRead && write )
    {
        log_data.cause = WriteRO ;
        pp->onlyRead = false ;
        pp->ref = 1 ;
        pp->writeBack = 3 ;

        mprotect(pp->start_addr, vmm.page_size, PROT_READ | PROT_WRITE ) ;
    }
    else
    {
        if( !write )
        {
            log_data.cause = ReadRW ;
            pp->ref = 1 ;
            mprotect(pp->start_addr, vmm.page_size, PROT_READ ) ;
        }
        else
        {
            log_data.cause = WriteRW ;
            pp->ref = 1 ;
            pp->writeBack = 3 ;
            mprotect(pp->start_addr, vmm.page_size, PROT_READ | PROT_WRITE ) ;
        }
    }
}

void handleNewPage( VM_Page* pp, bool write) 
{
    if( !write )
    {
        log_data.cause = ReadNPP;
        pp->onlyRead = 1 ;
        pp->ref  = 1 ;
        mprotect(pp->start_addr, vmm.page_size, PROT_READ );
    }
    else
    {
        log_data.cause = WriteNPP ;
        pp->ref = 1 ;
        pp->writeBack = 3 ;
        mprotect(pp->start_addr, vmm.page_size, PROT_READ | PROT_WRITE ) ;
    }
}

void addToList(VM_Page* pagePtr) 
{
    if( vmm.list_head == NULL )
    {
        vmm.list_head = pagePtr ;
        vmm.list_tail = pagePtr ;
    }
    else
    {
        pagePtr->prev = vmm.list_tail ;
        vmm.list_tail->next = pagePtr ;
        vmm.list_tail = pagePtr ;
    }
}

void removeFromList(VM_Page* pagePtr) 
{
    if(pagePtr->prev == NULL)
    {
        vmm.list_head = pagePtr->next ;
    }
    else
    {
        pagePtr->prev->next = pagePtr->next ;
    }

    if( pagePtr->next == NULL)
    {
        vmm.list_tail = pagePtr->prev ;
    }
    else
    {
        pagePtr->next->prev = pagePtr->prev ;
    }

    pagePtr->prev = NULL ;
    pagePtr->next = NULL ;

}

void handleFIFO(VM_Page* pagePtr, bool newPage)
{
    int pageFrame = -1 ;

    VM_Page* ptr = vmm.list_head ;

    while( ptr != NULL )
    {
        if(ptr->frameNo > -1 )
        {
            removeFromList(ptr ) ;
            pageFrame = ptr->frameNo ;

            log_data.pageEvicted = (ptr->start_addr - vmm.vm_start) / vmm.page_size ;

            if( ptr->writeBack > 0 )
                log_data.writeBack = true ;
            resetPage(ptr, true) ;
            addToList(ptr) ;
            break ;
        }
        ptr = ptr->next ;
    }

    pagePtr->frameNo = pageFrame ;

    if( !newPage )
        removeFromList(pagePtr) ;

    addToList(pagePtr) ;
}

void handleThirdReplacement(VM_Page* pagePtr, bool newPage) 
{
    int pageFrame = -1 ;
    bool pageEvicted = false;

    // Run the loop until a page is removed
    while (pageEvicted == false)
    {
        // Find the first page in cycle.
        VM_Page *nextPtr = vmm.list_head ;
        for (int i = 0; i < vmm.clockIndex; i++)
        {
            nextPtr = nextPtr->next;
        }

        // Iterate till reach the end of the list.
        while (nextPtr != NULL) 
        {
            vmm.clockIndex++;

            if (nextPtr->frameNo > -1)
            {
                //protect the memory
                mprotect(nextPtr->start_addr, vmm.page_size, PROT_NONE);

                // Check first chance
                if (nextPtr->ref == 1)
                {
                    nextPtr->ref = 0;
                } 
                else if ((nextPtr->writeBack & 1 << 1) > 0)
                {
                    // Second chance, check write first bit
                    nextPtr->writeBack = 1;
                }
                else
                {
                    // Current page is evicted.
                    pageEvicted = true;
                    log_data.pageEvicted =
                        ((nextPtr->start_addr) 
                         - vmm.vm_start) / vmm.page_size;
                    pageFrame = nextPtr->frameNo;
                    nextPtr->frameNo = -1;

                    // Check second write bit to determine writeback
                    if ((nextPtr->writeBack & 1 << 0) > 0) {
                        nextPtr->writeBack = 0;
                        log_data.writeBack = true;
                    }
                    else
                    {
                        log_data.writeBack = false;
                    }

                    break;
                }
            }

            nextPtr = nextPtr->next;
        }

        if (!pageEvicted || nextPtr->next == NULL) 
            vmm.clockIndex = 0;
    }

    pagePtr->frameNo = pageFrame;
    if (newPage) addToList(pagePtr);

}

void resetPage(VM_Page* pp, bool protectPage)
{
    pp->prev = NULL ;
    pp->next = NULL ;
    pp->frameNo  = -1 ;
    pp->onlyRead = false ;
    pp->ref = false ;
    pp->writeBack = 0 ;

    if( protectPage && pp->start_addr != NULL)
    {
        mprotect(pp->start_addr, vmm.page_size, PROT_NONE) ;
    }
}

void print(VM_Page * pp)
{
   printf("========= PAGE DATA ======\n%d %d %d %d \n\n ",(pp->start_addr ==NULL), pp->frameNo, pp->onlyRead, pp->writeBack) ;
}

void resetVmm()
{
    vmm.active_pages = 0 ;
    vmm.clockIndex = 0 ;
    vmm.list_head = NULL ;
    vmm.list_tail = NULL ;
}

void resetLoggerData()
{
    log_data.virtualPageNo  = -1 ;
    log_data.virtualPageOffset = -1 ;
    log_data.pageEvicted = -1 ;
    log_data.writeBack = false ;
}

