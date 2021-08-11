/* 8TSR resident stub for 8" drive support
   Compile for DOS (unsigned char: byte, unsigned int: word)
   SMALL memory model */

#include <stdio.h>
#include <stdlib.h>
#include <dos.h>

/* Occupies interrupts INT 0E8h (TSR geometry modifier routine),
                       INT 0E9h (original INT 08  - timer IRQ0),
                       INT 0EAh (original INT 13h - disk services). */

/* From 8TSR.ASM */
extern void far DiskInterrupt();  /* new INT 013h */
extern void far TSRInterrupt();   /* new INT 0E8h */

/* Original vectors */
void interrupt (*oldINT08)(); 
void interrupt (*oldINT13)();

/* TSR EXE image size in bytes and PSP segment */
unsigned int nTSRImageSize;
unsigned int nTSRPSP;

/* Store installation check, TSR PSP and data segment */
unsigned long far* pInstallationCheck = (unsigned long far*)MK_FP(0, 0x4AC);
unsigned int far* pTSRDataSegment = (unsigned int far*)MK_FP(0, 0x4B0);

/* INT 1Eh Diskette parameter table (DPT) pointer */
unsigned int far* pDPTOffset = (unsigned int far*)MK_FP(0, 0x1E*4);
unsigned int far* pDPTSegment = (unsigned int far*)MK_FP(0, 0x1E*4+2);
  
/* Data from the DPT to be read and modified */
unsigned char far* pSectorSize = NULL;
unsigned char far* pEOT = NULL;
unsigned char far* pGapLength = NULL;
unsigned char far* pDTL = NULL;
unsigned char far* pGap3Length = NULL;
unsigned char far* pSpecifyHUT = NULL;
unsigned char far* pSpecifyHLT = NULL;
unsigned char far* pSpecifyIdleTime = NULL;

/* Data obtained from the command line
   Unfortunately, these must be uppercase as they are linked together with extrn 8TSR.ASM */
unsigned char NDRIVENUMBER;
unsigned char NTRACKS;
unsigned char NHEADS;

/* New data obtained from the command line, to be updated in the DPT, also used in 8TSR.ASM */
unsigned char NNEWSECTORSIZE;
unsigned char NNEWEOT; /* extrn for 8TSR, so uppercase */
unsigned char NNEWGAPLENGTH;
unsigned char NNEWDTL;
unsigned char NNEWGAP3LENGTH;

/* Avoiding math.h, the maximum value obtained from here is 1024 anyway */
unsigned int pow2(unsigned char to)
{
  unsigned int nResult = 2;
  unsigned char nIdx = 1;
  
  /* Skipping 2^0... :) */
  for (; nIdx < to; nIdx++)
  {
    nResult *= 2;
  }
  
  return nResult;
}

/* Retrieve exact EXE image size. */
void GetEXEImageSize(const char* pPath)
{
  FILE* pFile;

  if ((pFile = fopen(pPath, "rb")) == NULL)
  {
    printf("\n8TSR: Failed retrieving EXE image size. Aborting.\n");
    exit(-1);
  }
  
  fseek(pFile, 0, SEEK_END);
  nTSRImageSize = (unsigned int)ftell(pFile);  
  fclose(pFile);
}

/* Installation check. */
unsigned char InstallationCheck()
{     
  return *pInstallationCheck == 0x38545352; /* '8TSR' */
}

/* Determine if we run from an A: or B: already */
unsigned char RunsFromFloppy()
{ 
  return getdisk() < 2;
}

/* Update data in the resident part of the existing TSR and kill off this instance. */
void UpdateTSR()
{ 
  /* All of these need to be on stack !!! */
  unsigned int nMyDS = _DS;
  unsigned int nMyPSP = _psp;  
  unsigned int nOldPSP;
  unsigned int nOldDS = *pTSRDataSegment;
   
  /* Not installed? Nothing to update. */
  if (InstallationCheck() == 0)
  {
    return;
  }
  
  /* Disable IRQs */
  asm cli
       
  /* Set DS of the resident part, and get its PSP. */
  _DS = nOldDS;            /* DS=old TSR DS */
  nOldPSP = nTSRPSP;       /* DS:nTSRPSP (we're not resident, ours is zero - uninitialized) */
  _DS = nMyDS;             /* DS=our data segment */
  
  /* Switch current PSP to the (old) resident PSP */
  asm mov ah,50h
  asm mov bx,[nOldPSP]
  asm int 21h
    
  /* Prepare arguments for INT E8h to update the data */
  _AX = NTRACKS | ((unsigned int)NHEADS << 8);  /* _AL = NTRACKS, _AH = NHEADS */
  asm push ax /* The following assignments to registers destroy AX */
  
  /* Variables obtained from the commandline, to be updated in the resident variant */
  _BL = NNEWSECTORSIZE;
  _BH = NNEWEOT;
  _CL = NNEWGAPLENGTH;
  _CH = NNEWDTL;
  _DL = NNEWGAP3LENGTH;
  
  /* Update resident data */
  asm pop ax
  asm int 0E8h
  
  /* Switch current PSP to the (old) resident PSP */
  asm mov ah,50h
  asm mov bx,[nMyPSP]
  asm int 21h  
  asm sti
  
  /* Resident data updated. Deallocate memory of this (nonresident) instance of 8TSR and quit. */
  exit(0);
}

/* Determine where the DPT is located and copy it to a writable place in memory if need be. */
unsigned char RelocateDPT()
{
  unsigned int nDPTSeg = *pDPTSegment;
  unsigned int nDPTOfs = *pDPTOffset;
  
  unsigned long nDPTLocation = ((unsigned long)nDPTSeg << 4) + nDPTOfs;  
  if (nDPTLocation < 640L*1024L)
  {
_fail:
    return 0; /* OK, under 640K (or memory allocation fail) */
  }
  
  /* Needs relocation  
     Allocate a DOS segment for a copy of the 11byte DPT: one 16byte paragraph */
  asm mov ah,48h
  asm mov bx,1
  asm int 21h
  asm jc _fail
  
  /* Copy old DPT to the new memory location in AX:0000, and update nDPTSeg:nDPTOfs */
  asm push ds
  asm push es
  asm push ax
  asm mov si,[nDPTOfs]
  asm mov ax,[nDPTSeg]
  asm mov ds,ax
  asm pop ax
  asm mov es,ax
  asm mov [nDPTSeg],ax
  asm xor di,di
  asm mov [nDPTOfs],di
  asm mov cx,0Bh
  asm rep movsb
  asm pop es
  asm pop ds
  
  /* Point to new memory location */
  *pDPTSegment = nDPTSeg; /* AX from INT 21h */
  *pDPTOffset = nDPTOfs;  /* 0 */
  
  return 1;
}

/* New INT 8 - timer interrupt. Just attempt to sync the DPT with what we have set fix.
   Regardless of whoever, or whatever, might have changed it... and then call it a day */
void interrupt TimerInterrupt()
{
  asm cli
  *pSectorSize = NNEWSECTORSIZE;
  *pEOT = NNEWEOT;
  *pGapLength = NNEWGAPLENGTH;
  *pDTL = NNEWDTL;
  *pGap3Length = NNEWGAP3LENGTH;
  *pSpecifyHUT = 0xc7;
  *pSpecifyHLT = 0x7e;
  *pSpecifyIdleTime = 0xc8;
  asm sti

  /* Continue with the ISR cascade */
  oldINT08();
}

int main(int argc, char* argv[])
{
  unsigned char nRelocationResult = 0;
  
  /* Executed manually without proper arguments */
  if (argc != 9)
  {
    printf("\nNot to be executed manually\n");
    exit(-1);
  }
  
  /* Get EXE image size */
  GetEXEImageSize(argv[0]);
   
  /* Clear the screen */
  asm mov ax,3
  asm int 10h
      
  /* Did 8FORMAT run from a floppy? */
  if (RunsFromFloppy() == 1)
  {
    printf("WARNING !\n"
           "8FORMAT ran from a floppy itself, and the choice was to update the BIOS DPT.\n"
           "Any subsequent 5.25\" or 3.5\" floppy access may fail, or cause data corruption.\n\n");
  }
  
  /* Is the DPT somewhere over 640K (e.g. in BIOS)? */
  nRelocationResult = RelocateDPT();
   
  /* Obtain values from the command line
     8TSR drivenumber tracks heads sectorspertrack sectorsize EOT RWgap DTL GAP3 */
  NDRIVENUMBER = (unsigned char)atoi(argv[1]);
  NTRACKS = (unsigned char)atoi(argv[2]);
  NHEADS = (unsigned char)atoi(argv[3]);
  /* New values to be updated in the DPT */
  NNEWSECTORSIZE = (unsigned char)atoi(argv[4]);
  NNEWEOT = (unsigned char)atoi(argv[5]);
  NNEWGAPLENGTH = (unsigned char)atoi(argv[6]);
  NNEWDTL = (unsigned char)atoi(argv[7]);
  NNEWGAP3LENGTH = (unsigned char)atoi(argv[8]);
  
  /* Write new data into the DPT table */
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
  *pSectorSize = NNEWSECTORSIZE;
  printf(", now %u (%u bytes/sector)\n", *pSectorSize, pow2(7+(*pSectorSize)));
  
  pEOT = (unsigned char far*)MK_FP(*pDPTSegment, (*pDPTOffset)+4);
  printf("Sectors per track:   was %u", *pEOT);
  *pEOT = NNEWEOT;
  printf(", now %u\n", *pEOT);
  
  pGapLength = (unsigned char far*)MK_FP(*pDPTSegment, (*pDPTOffset)+5);
  printf("R/W gap length:      was 0x%02X", *pGapLength);
  *pGapLength = NNEWGAPLENGTH;
  printf(", now 0x%02X\n", *pGapLength);
  
  pGap3Length = (unsigned char far*)MK_FP(*pDPTSegment, (*pDPTOffset)+7);
  printf("Format gap length:   was 0x%02X", *pGap3Length);  
  *pGap3Length = NNEWGAP3LENGTH;
  printf(", now 0x%02X\n", *pGap3Length);
  
  pDTL = (unsigned char far*)MK_FP(*pDPTSegment, (*pDPTOffset)+6);
  printf("DTL (transfer len):  was 0x%02X", *pDTL);
  *pDTL = NNEWDTL;
  printf(", now 0x%02X\n", *pDTL);
    
  /* Reset floppies */
  asm mov cx,4
blip:
  asm xor ax,ax
  asm mov dx,cx
  asm dec dx
  asm int 13h
  asm loop blip
  
  printf("\n8-inch drive mechanical parameters successfully applied.\n");
  if (nRelocationResult == 1)
  {
    printf("NOTE: the original DPT was readonly - relocated to segment 0x%04X.\n", *pDPTSegment);
  }
  
  /* TSR already installed? Update the resident data then */
  UpdateTSR();
   
  /* No, not installed - establish and go resident */
  asm cli
  
  /* Store old disk and timer handlers */
  oldINT13 = getvect(0x13);
  oldINT08 = getvect(8);
    
  /* Install new interrupt handlers */
  setvect(0xE9, oldINT08);
  setvect(0xEA, oldINT13);
  setvect(8,    TimerInterrupt);
/*setvect(0x13, DiskInterrupt);
  setvect(0xE8, TSRInterrupt); */
  
  /* Compiler limitation:
     far DiskInterrupt() does not contain the "interrupt" keyword. Need to do my own setvect :) */
  {
    unsigned int far* pSegment = (unsigned int far*)MK_FP(0, 0x13*4+2);
    unsigned int far* pOffset = (unsigned int far*)MK_FP(0, 0x13*4);
    
    *pSegment = FP_SEG(&DiskInterrupt);
    *pOffset = FP_OFF(&DiskInterrupt);
  }
  
  /* The same for TSRInterrupt (INT E8h) */
    {
    unsigned int far* pSegment = (unsigned int far*)MK_FP(0, 0xE8*4+2);
    unsigned int far* pOffset = (unsigned int far*)MK_FP(0, 0xE8*4);
    
    *pSegment = FP_SEG(&TSRInterrupt);
    *pOffset = FP_OFF(&TSRInterrupt);
  }
  
  /* Mark as installed */
  *pInstallationCheck = 0x38545352; /* ASCII '8TSR' */
  *pTSRDataSegment = _DS;
  nTSRPSP = _psp;
  asm sti
   
  /* Terminate and stay resident, exitcode 0 + specify allocated resident memory space
     EXE file size in 16-byte DOS paragraphs. */
  keep(0, (nTSRImageSize / 16) + 1);
  
  /* Won't get here :) */
  return 0;
}