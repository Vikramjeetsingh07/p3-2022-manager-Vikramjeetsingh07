#include "vmm.h"

// Memory Manager implementation
// Implement all other functions here...

#define FIND_ADDRES(sgInfo) (char *)sgInfo->si_addr;
#define GET_PAGE_NUMBER(addr) ((addr - vmm.vm_start)/vmm.page_size) ;
#define GET_PAGE_OFFSET(addr) ((addr - vmm.vm_start) % vmm.page_size) ;



void initVmm(enum policy_type policy, void *vm, int vm_size, int num_frames, int page_size)
{
    resetVmm() ;
    vmm.vm_start = vm ;
    vmm.vm_size = vm_size ;
    vmm.vm_policy = policy ;
    vmm.frames = num_frames ;
    vmm.page_size = page_size ;

 
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

// This is helper function to check if it is in the limit of the address given
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
{   int refbit1=1;
    int writebit=3;
    if(pp->onlyRead && write )
    {
        log_data.cause = WriteRO ;
        pp->onlyRead = false ;
        pp->ref = refbit1 ;
        pp->writeBack =writebit ;

        mprotect(pp->start_addr, vmm.page_size, PROT_READ | PROT_WRITE ) ;
    }
    else
    {
        if( write )
        {   log_data.cause = WriteRW ;
            pp->ref = refbit1 ;
            pp->writeBack = writebit ;
            mprotect(pp->start_addr, vmm.page_size, PROT_READ | PROT_WRITE ) ;  

        }
        else
        {   log_data.cause = ReadRW ;
            pp->ref = refbit1 ;
            mprotect(pp->start_addr, vmm.page_size, PROT_READ ) ;  

        }
    }
}

void handleNewPage( VM_Page* pp, bool write)
{
    if( write )
    {  
        log_data.cause = WriteNPP ;
        pp->ref = 1 ;
        pp->writeBack = 3 ;
        mprotect(pp->start_addr, vmm.page_size, PROT_READ | PROT_WRITE ) ;


    }
    else
    {   log_data.cause = ReadNPP;
        pp->onlyRead = 1 ;
        pp->ref  = 1 ;
        mprotect(pp->start_addr, vmm.page_size, PROT_READ );

   
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
   
    if( pagePtr->next == NULL)
    {
        vmm.list_tail = pagePtr->prev ;
    }
    else
    {
        pagePtr->next->prev = pagePtr->prev ;
    }

    if(pagePtr->prev != NULL)
    {  
        pagePtr->prev->next = pagePtr->next ;
       
    }
    else
    {
      vmm.list_head = pagePtr->next ;
    }

   
    pagePtr->prev = NULL ;
    pagePtr->next = NULL ;

}

void handleFIFO(VM_Page* pagePtr, bool newPage)
{   int notpossible=-1;
    int pageFrame = notpossible ;
    int bit1=1;//to help understand logic where we actually ean bit we use these two
    int bit0=0;
    bool writeflag=true;

    VM_Page* ptr = vmm.list_head ;

    while( ptr != NULL )
    {
        if(ptr->frameNo > notpossible)
        {
            removeFromList(ptr ) ;
            pageFrame = ptr->frameNo ;

            log_data.pageEvicted = (ptr->start_addr - vmm.vm_start) / vmm.page_size ;

            if( ptr->writeBack > bit0 )
                log_data.writeBack = writeflag;
            resetPage(ptr, writeflag) ;
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
{   int notpossible=-1;
    int pageFrame = notpossible ;
    bool intial_flag= false;
    bool write_flag= true;
    bool pageEvicted = intial_flag;
    int bit1=1;// to help understand logic 
    int bit0=0;

   
    while (pageEvicted == intial_flag)
    {
       
        VM_Page *nextPtr = vmm.list_head ;
        int limit= vmm.clockIndex;
        for (int i = 0; i < limit; i++)
        {
            nextPtr = nextPtr->next;
        }

       
        while (nextPtr != NULL)
        {
            vmm.clockIndex= vmm.clockIndex+1;

            if (nextPtr->frameNo > notpossible)
            {
               
                mprotect(nextPtr->start_addr, vmm.page_size, PROT_NONE);

               
                if (nextPtr->ref == bit1)
                {
                    nextPtr->ref = bit0;
                }
                else if ((nextPtr->writeBack & bit1 << bit1) > 0)
                {
                   
                    nextPtr->writeBack = bit1;
                }
                else
                {
                   
                    pageEvicted = write_flag ;
                    log_data.pageEvicted =
                        ((nextPtr->start_addr)
                         - vmm.vm_start) / vmm.page_size;
                    pageFrame = nextPtr->frameNo;
                    nextPtr->frameNo = notpossible;

                   
                    if ((nextPtr->writeBack & bit1 << bit0) > 0) {
                        nextPtr->writeBack = bit0;
                        log_data.writeBack = write_flag;
                    }
                    else
                    {
                        log_data.writeBack = intial_flag;
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

{   bool set_to_start = false;
    pp->prev = NULL ;
    pp->next = NULL ;
    pp->frameNo  = -1 ;
    pp->onlyRead =  set_to_start ;
    pp->ref =  set_to_start;
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
{   int reset=-1;
    log_data.virtualPageNo  = reset ;
    log_data.virtualPageOffset = reset ;
    log_data.pageEvicted = reset;
    log_data.writeBack = false ;
}
