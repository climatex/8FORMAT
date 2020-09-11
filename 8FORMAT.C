#include <stdlib.h>
#include <string.h>
#include <dos.h>

#include "8format.h"
#include "8floppy.h"
#include "isadma.h"

// Obtained from the command line later
unsigned int  nFDCBase = 0x3f0;
unsigned char nUseFM = 0;
unsigned char nQuickFormat = 0;
unsigned char nForceSingleSided = 0;
unsigned char nNoCreateFilesystem = 0;
unsigned char nDriveNumber = 0;
unsigned char nTracks = 77;
unsigned char nHeads = 0;
unsigned char nDoubleDensity = 255;
unsigned int  nSectorSize = 0;
unsigned char nSectorsPerTrack = 0;
unsigned char nCustomGapLength = 0;
unsigned char nCustomGap3Length = 0;
unsigned char nOnlyReprogramBIOS = 0;
unsigned char nUseIRQ = 6;
unsigned char nUseDMA = 2;
unsigned int  nDataRateKbps = 0;

// Terminate with exit code, do cleanup beforehand
void Quit(int nStatus)
{
  RemoveISR();
  FDDHeadRetract();
  FreeDMABuffer();
  
  // Important before passing control to DOS
  FDDResetBIOS();
  
  exit(nStatus);
}

// Determine if the machine is an IBM 5150 or XT.
unsigned char IsPCXT()
{
  unsigned char nPcByte = *((unsigned char far*)MK_FP(0xf000, 0xfffe));
  return (nPcByte > 0xfc) || (nPcByte == 0xfb);
}

// Redraw line
void DelLine()
{
  printf("\r                                                                              \r");
}

// Print splash
void PrintSplash()
{
  printf("8FORMAT - 8\" floppy format utility for DOS, (c) J. Bogin\n");
}

// Printed on incorrect or no command line arguments
void PrintUsage()
{
  printf("\n"
         "8FORMAT X: TYPE [/500K] [/512B] [/1] [/N] [/Q] [/FDC port] [/IRQ num]\n"
         "                [/DMA num] [/G len] [/G3 len] [/R]\n\n"
         " X:     the drive letter where the 77-track 8\" disk drive is installed; A to D,\n"
         " TYPE   specifies media geometry, density (data encoding) and data bitrate:\n"
         "        SSSD: 250kB 1-sided, 250 kbps  FM encoding, 26 128B sectors, FAT12,\n"
         "        SSDD: 500kB 1-sided, 500 kbps MFM encoding, 26 256B sectors, FAT12,\n"
         "        DSSD: 500kB 2-sided, 250 kbps  FM encoding, 26 128B sectors, FAT12,\n"
         "        DSDD: 1.2MB 2-sided, 500 kbps MFM encoding, 8 1024B sectors, FAT12.\n"
         " /500K  (optional): Force 500 kbps data bit rate for the chosen TYPE.\n"
         " /512B  (optional): Force 512 byte sectors for the chosen TYPE.\n"         
         " /1     (optional): 1-sided format only. Use with TYPE DSDD to get 616K SSDD.\n"
         " /N     (optional): Format only, do not create boot sector and file system.\n"
         " /Q     (optional): Do not format, only create boot sector and file system.\n"
         " /FDC   (optional): Use a different FD controller base hex port; default 0x3f0.\n"
         "        Use /IRQ (3..15) and /DMA (0..3) to override default IRQ 6 and DMA 2.\n"
         " /G,/G3 (optional): Custom GAP (write) or Gap3 (format) sizes in hex. Max 0xff.\n"
         " /R     (optional): Don't do anything - just apply chosen geometry into BIOS.\n"
         "                    Applies for ALL floppy drives! Reboot to load BIOS default.\n\n");
         

  Quit(EXIT_SUCCESS);
}

// Parse the command line
void ParseCommandLine(int argc, char* argv[])
{
  int indexArgs = 0;
  unsigned char nForce512Sectors = 0;

  // Incorrect number of arguments
  if ((argc < 3) || (argc > 12))
  {
    PrintUsage();
  }

  for (indexArgs = 1; indexArgs < argc; indexArgs++)
  {
    // Case insensitive comparison
    const char* pArgument = strupr(argv[indexArgs]);

    if (indexArgs == 1)
    {
      // Convert drive letter to drive number
      if ((strlen(pArgument) > 2) || ((strlen(pArgument) == 2) && pArgument[1] != ':'))
      {
        PrintUsage();
      }

      // Must be A: to D:
      nDriveNumber = pArgument[0] - 65;
      if (nDriveNumber > 3)
      {
        PrintUsage();
      }
      
      continue;      
    }
    
    else if (indexArgs == 2)
    {
      // Determine density and sides
      if ((strlen(pArgument) != 4) || (pArgument[1] != 'S') || (pArgument[3] != 'D'))
      {
        PrintUsage();
      }
      
      if (pArgument[0] == 'S')
      {
        nHeads = 1;
      }
      else if (pArgument[0] == 'D')
      {
        nHeads = 2;
      }
      
      if (pArgument[2] == 'S')
      {
        nDoubleDensity = 0;
        nDataRateKbps = 250;
        nUseFM = 1;
      }
      else if (pArgument[2] == 'D')
      {
        nDoubleDensity = 1;
        nDataRateKbps = 500;
      }

      if ((nHeads == 0) || (nDoubleDensity > 1))
      {
        PrintUsage();
      }

      continue;
    }
    
    // Force 500K data rate
    if (strcmp(pArgument, "/500K") == 0)
    {
      nDataRateKbps = 500;
    }
        
    // Force 512-byte sectors on the chosen drive geometry
    if (strcmp(pArgument, "/512B") == 0)
    {
      nForce512Sectors = 1;
    }
    
    // Force single-sided operation
    if (strcmp(pArgument, "/1") == 0)
    {
      nForceSingleSided = 1;
    }
    
    // Quick format (create FAT12 only)
    if (strcmp(pArgument, "/Q") == 0)
    {
      nQuickFormat = 1;
    }
    
    // No filesystem    
    if (strcmp(pArgument, "/N") == 0)
    {
      nNoCreateFilesystem = 1;
    }
    
    // Custom floppy drive controller port has been specified
    if ((strcmp(pArgument, "/FDC") == 0) && (argc > indexArgs + 1))
    {
      char* pEndPointer;
      unsigned long nPort = strtoul(argv[indexArgs+1], &pEndPointer, 16);
      
      if ((nPort != 0) && (nPort < 0xffff))
      {
        nFDCBase = (unsigned int)nPort;
      }
      else
      {
        PrintUsage();
      }
    }
    
    // Custom write gap length has been specified
    if ((strcmp(pArgument, "/G") == 0) && (argc > indexArgs + 1))
    {
      char* pEndPointer;
      unsigned long nGapLen = strtoul(argv[indexArgs+1], &pEndPointer, 16);
      
      if ((nGapLen != 0) && (nGapLen <= 0xff))
      {
        nCustomGapLength = (unsigned char)nGapLen;
      }
      else
      {
        PrintUsage();
      }
    }
    
    // Custom format gap3 length has been specified
    if ((strcmp(pArgument, "/G3") == 0) && (argc > indexArgs + 1))
    {
      char* pEndPointer;
      unsigned long nGapLen = strtoul(argv[indexArgs+1], &pEndPointer, 16);
      
      if ((nGapLen != 0) && (nGapLen <= 0xff))
      {
        nCustomGap3Length = (unsigned char)nGapLen;
      }
      else
      {
        PrintUsage();
      }
    }
    
    // Just update the BIOS INT 1Eh floppy parameter table?
    if (strcmp(pArgument, "/R") == 0)
    {
      nOnlyReprogramBIOS = 1;
    }
    
    // Custom IRQ number (dec.)
    if ((strcmp(pArgument, "/IRQ") == 0) && (argc > indexArgs + 1))
    {
      char* pEndPointer;
      unsigned long nIrq = strtoul(argv[indexArgs+1], &pEndPointer, 10);
      
      if ((nIrq > 2) && (nIrq < 16))
      {
        nUseIRQ = (unsigned char)nIrq;
      }
      else
      {
        PrintUsage();
      }
    }
    
    // Custom 8-bit DMA channel
    if ((strcmp(pArgument, "/DMA") == 0) && (argc > indexArgs + 1))
    {
      char* pEndPointer;
      unsigned long nDma = strtoul(argv[indexArgs+1], &pEndPointer, 10);
      
      if (nDma <= 3)
      {
        nUseDMA = (unsigned char)nDma;
      }
      else
      {
        PrintUsage();
      }
    }
  }
  
  // Both quick format and no filesystem options specified?
  if ((nQuickFormat == 1) && (nNoCreateFilesystem == 1))
  {
    PrintUsage();
  }
   
  // Running on IBM PC/XT with 500kbps bitrate specified - just warn and run
  if ((nDataRateKbps == 500) && (IsPCXT() == 1))
  {
    printf("\nPC/XT detected. Unless there's a special FDC, the bitrate is capped to 250kbps.\n");
  }
  
  // Custom FDC port, IRQ or DMA specified ?
  if ((nFDCBase != 0x3f0) || (nUseIRQ != 6) || (nUseDMA != 2))
  {
    printf("\nUsing floppy controller base address at 0x%03X, IRQ %d and DMA channel %d.\n", nFDCBase, nUseIRQ, nUseDMA);
  }
  
  // Construct sector size and sectors per track information
  nSectorsPerTrack = (nDoubleDensity == 0) ? 26 : 8;
  nSectorSize = (nDoubleDensity == 0) ? 128 : 1024;
  
  // Special case: Experimental 500K SSDD with FAT12 (unless /N specified)
  if ((nHeads == 1) && (nDoubleDensity == 1))
  {
    nSectorSize = 256;
    nSectorsPerTrack = 26;
  }
  
  // Special case 2: Experimental: use 512 bytes per sector
  if (nForce512Sectors == 1)
  {
    nSectorSize = 512;
    
    // Show warning message if we are in single density mode
    if (nDoubleDensity == 0)
    {
      printf("\nAttempting to format 512 byte sectors in a single density mode.\n");
    }
  }
}

unsigned char WaitEnterOrEscape()
{
  unsigned char nScanCode = 0;
  
  // While not ENTER (or ESC)
  while ((nScanCode != 0x1C) && (nScanCode != 1))
  {
    _asm {
      xor ax,ax
      int 16h
      mov nScanCode,ah
    }
  }
  
  return nScanCode;
}

void AskToContinue()
{
  unsigned char nScanCode = 0;
  printf("Insert a disk into drive %c: and press ENTER to continue; ESC to quit...",
         nDriveNumber + 65);
	 
  nScanCode = WaitEnterOrEscape();
  
  // ESC
  if (nScanCode == 1)
  {
    printf(" ESC\n");
    Quit(EXIT_SUCCESS);
  }
  
  DelLine();
}

void DoOperations()
{  
  const unsigned char nHeadCount = (nForceSingleSided == 1) ? 1 : nHeads;
  
  // Allocate buffer for DMA transfers
  InitializeDMABuffer();
  
  // Inform about the drive geometry
  printf("\nUsing the following drive geometry and parameters:\n"
         "%u tracks, %u %s, %u %uB sectors, R/W gap 0x%02X, Gap3 0x%02X, %ukbps%s %s.\n\n",
         nTracks,
         nHeadCount,
         (nHeadCount > 1) ? "heads" : "head",
         nSectorsPerTrack,
         nSectorSize,
         GetGapLength(0),
         GetGapLength(1),
         nDataRateKbps,
         ((IsPCXT() == 1) && (nDataRateKbps > 250)) ? "??" : "",
         (nUseFM == 1) ? "FM" : "MFM");
         
  /// Only reprogram the BIOS floppy geometry ?
  if (nOnlyReprogramBIOS == 1)
  {
    FDDWriteINT1Eh();
    Quit(EXIT_SUCCESS);
  }
  
  // Ask to put the disk into drive
  AskToContinue();
  
  // Prepare FDC and drive
  FDDReset();

  // Format 77 tracks
  if (nQuickFormat == 0)
  {
    unsigned char nTrackIndex = 0;
    unsigned char nHeadIndex = 0;
    
    printf("Formatting...\n");
    
    for (; nTrackIndex < nTracks; nTrackIndex++)
    {    
      while (nHeadIndex != nHeadCount)
      {
        FDDSeek(nTrackIndex, nHeadIndex);
        FDDFormat();
        nHeadIndex++;
      }

      nHeadIndex = 0;
    }
  }

  DelLine();

  // Write FAT12
  if (nNoCreateFilesystem == 0)
  {
    unsigned char nScanCode = 0;
    
    printf("Creating file system...\n");
    WriteFAT12();
    
    // Ask to update the BIOS INT 1Eh diskette parameter table    
    printf("Do you want to set the new diskette parameter table, to make the disk readable?\n"
           "Attention: Applies to all floppy drives. Reboot to load BIOS defaults.\n"
	         "ENTER to continue, ESC to skip (the drive might be not readable under DOS).\n");
    
    nScanCode = WaitEnterOrEscape();
    printf("\n");
    
    // Update BIOS floppy parameter table
    if (nScanCode == 0x1C)
    {
      FDDWriteINT1Eh();
    }
  }
  
  printf("Format completed.\n");  
  Quit(EXIT_SUCCESS);
}