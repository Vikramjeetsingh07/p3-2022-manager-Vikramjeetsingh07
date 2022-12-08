#ifndef VMM_H
#define VMM_H

#include "interface.h"

// Declare your own data structures and functions here...

//******** ENUMS **************//

enum pfErrorCode {
    PF_PROT  = 1 << 0,
    PF_WRITE = 1 << 1,
    PF_USER  = 1 << 2,
    PF_RSVD  = 1 << 3,
    PF_INSTR = 1 << 4
};

enum FaultType {
    ReadNPP  = 0,
    WriteNPP = 1,
    WriteRO  = 2,
    ReadRW   = 3,
    WriteRW  = 4
};

//******** STRUCTURES **********//

struct VM_Page
{
    char *start_addr  ;
    struct VM_Page *prev  ;
    struct VM_Page *next  ;
    int frameNo  ;
    bool onlyRead ;
    bool ref  ;
    int writeBack ;
};
typedef struct VM_Page VM_Page ;

void resetPage(VM_Page* pp, bool protectPage) ;

void print(VM_Page* pp) ;

struct VMM
{
    char *vm_start;
    int vm_size ;
    int vm_policy ;
    int page_size ;
    int frames ;

    int active_pages ;
    int clockIndex ;

    VM_Page *list_head;
    VM_Page *list_tail;
};
struct VMM  vmm ;

void resetVmm() ;


struct Logger_data
{
    int virtualPageNo ;
    int virtualPageOffset ;
    int pageEvicted ;
    bool writeBack ;
    enum FaultType cause ;
};
struct Logger_data log_data ;

void resetLoggerData() ;


//******** FUNCTIONS *************//
void initVmm(enum policy_type policy,
        void *vm,
        int vm_size,
        int num_frames, 
        int page_size) ;

void segFaultHandler(int sig, 
        siginfo_t* sigInfo,
        void *context) ;

bool validAddres(char* address) ;

VM_Page* searchPage(char* address, bool *) ;

void handleExistingPage(VM_Page* pagePtr, bool) ;

void handleNewPage( VM_Page* pagePtr, bool) ;

void addToList(VM_Page* pagePtr) ;

void removeFromList(VM_Page* pagePtr) ;

void handleFIFO(VM_Page* pagePtr, bool newPage) ;

void handleThirdReplacement(VM_Page* pagePtr,
        bool newPage) ;

#endif
