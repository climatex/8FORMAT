// 8TSR resident stub for 8" drive support
// Compile for DOS (unsigned char: byte, unsigned int: word)
// SMALL memory model

#include <stdlib.h>
#include <dos.h>

// Occupies interrupts INT 0E8h (installation check/unload),
//                     INT 0E9h (original INT 08  - timer IRQ0),
//                     INT 0EAh (original INT 13h - disk services).

// From 8TSR.ASM
extern unsigned char far DiskInterrupt();

// Original vectors
void interrupt (*oldINTE8)();
void interrupt (*oldINT08)(); 
void interrupt (*oldINT13)();

// Stack pointers and PSP
unsigned int far nTopSS;    // Top of TSR stack segment and pointer
unsigned int far nTopSP;
unsigned int far nCallerSS; // Caller stack segment and pointer
unsigned int far nCallerSP;
unsigned int far nTSRPSP;   // PSP of the TSR

// INT 1Eh Diskette parameter table (DPT) pointer
unsigned int far* pDPTOffset = (unsigned int far*)MK_FP(0, 0x1E*4);
unsigned int far* pDPTSegment = (unsigned int far*)MK_FP(0, 0x1E*4+2);
  
// Data from the DPT to be read and modified
unsigned char far* pSectorSize = NULL;
unsigned char far* pEOT = NULL;
unsigned char far* pGapLength = NULL;
unsigned char far* pDTL = NULL;
unsigned char far* pGap3Length = NULL;
unsigned char far* pSpecifyHUT = NULL;
unsigned char far* pSpecifyHLT = NULL;
unsigned char far* pSpecifyIdleTime = NULL;

// Data obtained from the command line
// Unfortunately, these must be uppercase as they are linked together with extrn 8TSR.ASM
unsigned char far NDRIVENUMBER;
unsigned char far NTRACKS;
unsigned char far NHEADS;

// New data obtained from the command line, to be updated in the DPT
unsigned char far nNewSectorSize;
unsigned char far NNEWEOT; //extrn for 8TSR, so uppercase
unsigned char far nNewGapLength;
unsigned char far nNewDTL;
unsigned char far nNewGap3Length;

// Avoiding math.h, the maximum value obtained from here is 1024 anyway
unsigned int pow2(unsigned char to)
{
  unsigned int nResult = 2;
  unsigned char nIdx = 1;
  
  // Skipping 2^0... :)
  for (; nIdx < to; nIdx++)
  {
    nResult *= 2;
  }
  
  return nResult;
}

// Installation check.
unsigned char InstallationCheck()
{
  // Call INT E8h with AX=0xDEAD, shall return 0xBEEF in ES:DI
  unsigned int nIsInstalled = 0;

  if (getvect(0xe8) != NULL)
  { 
    union REGS regs;
    struct SREGS sregs;
    
    regs.x.ax = 0xDEAD;
    sregs.es = FP_SEG(&nIsInstalled);
    regs.x.di = FP_OFF(&nIsInstalled);    
    int86x(0xE8, &regs, &regs, &sregs);
  }
    
  return nIsInstalled == 0xBEEF;
}

// Determine if we run from an A: or B: already
unsigned char RunsFromFloppy()
{
  unsigned nDrive;
  _dos_getdrive(&nDrive);
  
  return nDrive < 2;
}

// Get DOS program segment prefix
unsigned int far GetPSP()
{
  union REGS regs;
  regs.h.ah = 0x51;
  int86(0x21, &regs, &regs);
  
  return regs.x.bx;
}

// Set DOS program segment prefix
void far SetPSP(unsigned int nPSP)
{
  union REGS regs;
  regs.h.ah = 0x50;
  regs.x.bx = nPSP;
  int86(0x21, &regs, &regs);
}

// Unload an existing TSR (if called multiple times)
void UnloadTSR()
{ 
  union REGS regs;
  regs.x.ax = 0;
  int86(0xE8, &regs, &regs);
}

void far Cleanup()
{
  // Free TSR and environment segments   
  union REGS regs;
  struct SREGS sregs;
  
  // Restore old interrupt vectors: IRQ0 (INT8), INT 13h, INT E8h, enable interrupts
  setvect(8,    oldINT08); //timer IRQ
  setvect(0x13, oldINT13); //disk services
  setvect(0xE8, oldINTE8); //us unhooked

  // Free up TSR memory 
  regs.h.ah = 0x49;
  sregs.es = nTSRPSP;
  int86x(0x21,&regs,&regs,&sregs);
      
  // Free up TSR environment  
  regs.h.ah = 0x49;
  sregs.es = *(unsigned far*)(MK_FP(nTSRPSP, 0x2C));
  int86x(0x21,&regs,&regs,&sregs);
}

// INT 0xE8 TSR hook
void interrupt TSRInterrupt()
{
  unsigned int nCallerPSP;
  
  // Installation check?
  if (_AX == 0xDEAD)
  {
    // Write 0xBEEF to ES:DI
    unsigned int far* pOut = (unsigned int far*)MK_FP(_ES,_DI);
    *pOut = 0xBEEF; //Dead beef
    return;
  }
   
  // Silently unload previous instance
  // Save caller stack pointers and switch to local stack
  asm cli
  nCallerSS = _SS;
  nCallerSP = _SP;
  _SS = nTopSS;
  _SP = nTopSP;
  asm sti
  
  // Get caller PSP, switch to local PSP, do cleanup and switch back to caller PSP  
  nCallerPSP = GetPSP();
  SetPSP(nTSRPSP);
  Cleanup();
 
  SetPSP(nCallerPSP);
  
  // Switch back to caller stack
  asm cli
  _SS = nCallerSS;
  _SP = nCallerSP;
  asm sti
}

// New INT 8 - timer interrupt. Just attempt to sync the DPT with what we have set fix.
// Regardless of whoever, or whatever, might have changed it... and then call it a day
void interrupt TimerInterrupt()
{
  asm cli
  *pSectorSize = nNewSectorSize;
  *pEOT = NNEWEOT;
  *pGapLength = nNewGapLength;
  *pDTL = nNewDTL;
  *pGap3Length = nNewGap3Length;
  *pSpecifyHUT = 0xc7;
  *pSpecifyHLT = 0x7e;
  *pSpecifyIdleTime = 0xc8;
  asm sti

  // Continue with the ISR cascade
  oldINT08();
}

int main(int argc, char* argv[])
{
  // Exit code -1: TSR not installed
  // Exit code 0:  TSR installed
  // Exit code 1:  TSR uninstalled (run again)
  
  // Executed manually without proper arguments
  if (argc != 9)
  {
    printf("\nNot to be executed manually\n");
    exit(-1);
  }
   
  // Already installed - unload
  if (InstallationCheck() == 1)
  {
    UnloadTSR();
  }
  
  // Clear the screen  
  asm mov ax,3
  asm int 10h
  
  // Did 8FORMAT run from a floppy?
  if (RunsFromFloppy() == 1)
  {
    printf("WARNING !\n"
           "8FORMAT ran from a floppy itself, and the choice was to update the BIOS DPT.\n"
           "Any subsequent 5.25\" or 3.5\" floppy access may fail, or cause data corruption.\n\n");
  }
  
  // Obtain values from the command line
  // 8TSR drivenumber tracks heads sectorspertrack sectorsize EOT RWgap DTL GAP3
  NDRIVENUMBER = (unsigned char)atoi(argv[1]);
  NTRACKS = (unsigned char)atoi(argv[2]);
  NHEADS = (unsigned char)atoi(argv[3]);
  // New values to be updated in the DPT
  nNewSectorSize = (unsigned char)atoi(argv[4]);
  NNEWEOT = (unsigned char)atoi(argv[5]);
  nNewGapLength = (unsigned char)atoi(argv[6]);
  nNewDTL = (unsigned char)atoi(argv[7]);
  nNewGap3Length = (unsigned char)atoi(argv[8]);
  
  // Write new data into the DPT table
  printf("New BIOS INT 1Eh diskette parameters table (DPT) for all drives:\n\n");
  
  pSpecifyHUT = (unsigned char far*)MK_FP(*pDPTSegment, (*pDPTOffset)+0);
  printf("Step rate time:      was %u ms, now 8 ms\n", 32-(((unsigned int)(*pSpecifyHUT) >> 4) * 2));
  printf("Head unload time:    was %u ms, now 224 ms\n", ((unsigned int)(*pSpecifyHUT) & 0xf) * 32);
  *pSpecifyHUT = 0xc7;
  
  pSpecifyHLT = (unsigned char far*)MK_FP(*pDPTSegment, (*pDPTOffset)+1);
  printf("Head load time:      was %u ms, now 252 ms\n", ((unsigned int)(*pSpecifyHLT) >> 1) * 4);  
  *pSpecifyHLT = 0x7e;
  
  pSpecifyIdleTime = (unsigned char far*)MK_FP(*pDPTSegment, (*pDPTOffset)+2);
  printf("Maximum idle time:   was %u ms, now 11000 ms\n", (unsigned int)(*pSpecifyIdleTime) * 55);  
  *pSpecifyIdleTime = 0xc8;
  
  pSectorSize = (unsigned char far*)MK_FP(*pDPTSegment, (*pDPTOffset)+3);
  printf("Sector size byte:    was %u (%u bytes/sector)", *pSectorSize, pow2(7+(*pSectorSize)));
  *pSectorSize = nNewSectorSize;
  printf(", now %u (%u bytes/sector)\n", *pSectorSize, pow2(7+(*pSectorSize)));
  
  pEOT = (unsigned char far*)MK_FP(*pDPTSegment, (*pDPTOffset)+4);
  printf("Sectors per track:   was %u", *pEOT);
  *pEOT = NNEWEOT;
  printf(", now %u\n", *pEOT);
  
  pGapLength = (unsigned char far*)MK_FP(*pDPTSegment, (*pDPTOffset)+5);
  printf("R/W gap length:      was 0x%02X", *pGapLength);
  *pGapLength = nNewGapLength;
  printf(", now 0x%02X\n", *pGapLength);
  
  pGap3Length = (unsigned char far*)MK_FP(*pDPTSegment, (*pDPTOffset)+7);
  printf("Format gap length:   was 0x%02X", *pGap3Length);  
  *pGap3Length = nNewGap3Length;
  printf(", now 0x%02X\n", *pGap3Length);
  
  pDTL = (unsigned char far*)MK_FP(*pDPTSegment, (*pDPTOffset)+6);
  printf("DTL (transfer len):  was 0x%02X", *pDTL);
  *pDTL = nNewDTL;
  printf(", now 0x%02X\n", *pDTL);
    
  // Reset floppies
  _asm {
    mov cx,4
  }
blip:
  _asm {
    xor ax,ax
    mov dx,cx
    dec dx
    int 13h
    loop blip
  }
  
  // Save the old interrupt handlers, get local PSP
  asm cli
  oldINTE8 = getvect(0xE8);
  oldINT13 = getvect(0x13);
  oldINT08 = getvect(8);
  nTSRPSP = GetPSP();
  
  // Install new interrupt handlers
  setvect(0xE8, TSRInterrupt);
  setvect(0xE9, oldINT08);
  setvect(0xEA, oldINT13);
  setvect(8,    TimerInterrupt);
//setvect(0x13, DiskInterrupt);
  
  // Compiler limitation:
  // far DiskInterrupt() does not contain the "interrupt" keyword. Need to do my own setvect :)
  {
    unsigned int far* pSegment = (unsigned int far*)MK_FP(0, 0x13*4+2);
    unsigned int far* pOffset = (unsigned int far*)MK_FP(0, 0x13*4);
    
    *pSegment = FP_SEG(&DiskInterrupt);
    *pOffset = FP_OFF(&DiskInterrupt);
  }
  
  // Initialize top of stack pointer
  nTopSS = _SS;
  nTopSP = _SP;
  asm sti
  
  printf("\n8-inch drive mechanical parameters successfully applied.\n");
  
  // Terminate and stay resident, exitcode 0 + specify allocated resident memory space
  // PSP: starting address of the program
  // Stack top: end of the program
  // + some "safety space" (2k)
  // Resident memory to be allocated in 16-byte "paragraphs", ergo div 16
  _dos_keep(0, (_SS + ((_SP + 2048) / 16) - nTSRPSP));
  
  // Won't get here :)
  return 0;
}