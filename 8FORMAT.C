#include <stdlib.h>
#include <string.h>
#include <dos.h>

#include "8format.h"
#include "8floppy.h"
#include "isadma.h"

// Obtained from the command line later
unsigned int  nFDCBase = 0x3f0;
unsigned char nUseFM = 0;
unsigned char nFormatWithVerify = 0;
unsigned char nQuickFormat = 0;
unsigned char nNoCreateFilesystem = 1;
unsigned char nDriveNumber = 0;
unsigned char nTracks = 77;
unsigned char nHeads = 2;
unsigned int  nPhysicalSectorSize = 0;
unsigned int  nLogicalSectorSize = 0;
unsigned char nSectorsPerTrack = 0;
unsigned char nLogicalSectorsPerTrack = 0;
unsigned char nCustomGapLength = 0;
unsigned char nCustomGap3Length = 0;
unsigned char nOnlyReprogramBIOS = 0;
unsigned char nUseIRQ = 6;
unsigned char nUseDMA = 2;
unsigned int  nDataRateKbps = 500;
unsigned char sLaunch8TSR[255] = {0};
unsigned char sFormatType[5] = {0};
unsigned char nFormatByte = 0xF6;

// Terminate with exit code, do cleanup beforehand
void Quit(int nStatus)
{
  RemoveISR();
  FDDHeadRetract();
  FreeDMABuffer();
  
  // Important before passing control to DOS
  FDDResetBIOS();
  
  // Execute 8TSR if need be
  if (strlen(sLaunch8TSR) > 0)
  {
    unsigned nFileAttributes;

    // 8TSR.EXE must exist
    if (_dos_getfileattr("8tsr.exe", &nFileAttributes) == 0)
    {
      system(sLaunch8TSR);
    }
    
    else
    {
      printf("8TSR.EXE not found, cannot set the BIOS diskette parameter table.\n");
    }
  }
  
  // Exit to OS
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
         "8FORMAT X: TYPE [/1] [/V] [/500K] [/MFM] [/512PH] [/FAT12] [/Q]\n"
         "                [/FDC port] [/IRQ num] [/DMA num] [/G len] [/G3 len] [/DPT]\n\n"
         " X:     the drive letter where the 77-track 8\" disk drive is installed; A to D,\n"
         " TYPE   sets geometry, density (bitrate, data encoding) & physical sector size:\n"
         "        DSSD: 500kB 2-sided, single-density (250 kbps  FM), 26  128B sectors,\n"
         "        DSDD: 1.2MB 2-sided, double-density (500 kbps MFM),  8 1024B sectors,\n"
         "        EXT1: 1.2MB 2-sided, double-density (500 kbps MFM), 16  512B sectors,\n"
         "        EXT2: 1.0MB 2-sided, double-density (500 kbps MFM), 26  256B sectors,\n"         
         "        EXT3: 616kB 2-sided, single-density (250 kbps  FM),  8  512B sectors.\n"
         " /1     (optional): Single-sided format for the chosen TYPE. Capacity halved.\n"
         " /V     (optional): Format and verify. Much slower.\n"         
         " /500K  (optional): Use 500 kbps data bitrate (with types DSSD or EXT3).\n"
         " /MFM   (optional): Use MFM data encoding (with types DSSD or EXT3).\n"
         " /512PH (optional): Set 512B physical sectors, TYPE treated as logical format.\n"
         " /FAT12 (optional): Format and try writing the DOS boot sector and filesystem.\n"
         " /Q     (optional): Do not format, just write the boot sector and filesystem.\n"
         " /FDC   (optional): Use a different FD controller base hex port; default 0x3f0.\n"
         "        Use /IRQ (3..15) and /DMA (0..3) to override default IRQ 6 and DMA 2.\n"
         " /G,/G3 (optional): Custom GAP (write) or Gap3 (format) sizes in hex. Max 0xff.\n"
         " /DPT   (optional): Do nothing; just update the BIOS DPT for all floppy drives.\n");         

  Quit(EXIT_SUCCESS);
}

// Parse the command line
void ParseCommandLine(int argc, char* argv[])
{
  int indexArgs = 0;
  unsigned char nForce512Physical = 0;

  // Incorrect number of arguments
  if ((argc < 3) || (argc > 16))
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
    }
    
    else if (indexArgs == 2)
    {
      // Determine TYPE.
      if (strlen(pArgument) != 4)
      {
        PrintUsage();
      }
      
      // Copy TYPE string.
      strncpy(sFormatType, pArgument, 4);
      
      // 500kB DSSD
      if (strcmp(pArgument, "DSSD") == 0)
      {
        nDataRateKbps = 250;
        nUseFM = 1;
        nSectorsPerTrack = 26;
        nLogicalSectorsPerTrack = 26;
        nPhysicalSectorSize = 128;
        nLogicalSectorSize = 128;
      }
    
      // 1.2MB DSDD
      else if (strcmp(pArgument, "DSDD") == 0)
      {
        nSectorsPerTrack = 8;
        nLogicalSectorsPerTrack = 8;
        nPhysicalSectorSize = 1024;
        nLogicalSectorSize = 1024;
      }
      
      // 1.2MB EXT1
      else if (strcmp(pArgument, "EXT1") == 0)
      {
        nSectorsPerTrack = 16;
        nLogicalSectorsPerTrack = 16;
        nPhysicalSectorSize = 512;
        nLogicalSectorSize = 512;
      }
      
      // 1.0MB EXT2
      else if (strcmp(pArgument, "EXT2") == 0)
      {
        nSectorsPerTrack = 26;
        nLogicalSectorsPerTrack = 26;
        nPhysicalSectorSize = 256;
        nLogicalSectorSize = 256;
      }
      
      // 616kB EXT3
      else if (strcmp(pArgument, "EXT3") == 0)
      {
        nDataRateKbps = 250;
        nUseFM = 1;
        nSectorsPerTrack = 8;
        nLogicalSectorsPerTrack = 8;
        nPhysicalSectorSize = 512;
        nLogicalSectorSize = 512;
      }
      
      // Unrecognized
      else
      {
        PrintUsage();
      }
    }
    
    // Force 500K data rate
    else if (strcmp(pArgument, "/500K") == 0)
    {
      nDataRateKbps = 500;
    }
    
    // Force MFM encoding
    else if (strcmp(pArgument, "/MFM") == 0)
    {
      nUseFM = 0;
    }
    
    // Force single-sided operation
    else if (strcmp(pArgument, "/1") == 0)
    {
      nHeads = 1;
    }
    
    // Force 512-byte sectors
    else if (strcmp(pArgument, "/512PH") == 0)
    {
      nForce512Physical = 1;
    }
    
    // Format with verify
    else if (strcmp(pArgument, "/V") == 0)
    {
      nFormatWithVerify = 1;
    }
    
    // Format and create filesystem  
    else if (strcmp(pArgument, "/FAT12") == 0)
    {
      nNoCreateFilesystem = 0;
    }
    
    // Create FAT12 only
    else if (strcmp(pArgument, "/Q") == 0)
    {
      nQuickFormat = 1;
      nNoCreateFilesystem = 0;
    }
    
    // Custom floppy drive controller port has been specified
    else if ((strcmp(pArgument, "/FDC") == 0) && (argc > indexArgs + 1))
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
    else if ((strcmp(pArgument, "/G") == 0) && (argc > indexArgs + 1))
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
    else if ((strcmp(pArgument, "/G3") == 0) && (argc > indexArgs + 1))
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
    else if (strcmp(pArgument, "/DPT") == 0)
    {
      nOnlyReprogramBIOS = 1;
    }
    
    // Custom IRQ number (dec.)
    else if ((strcmp(pArgument, "/IRQ") == 0) && (argc > indexArgs + 1))
    {
      int nIrq = atoi(argv[indexArgs+1]);
      
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
    else if ((strcmp(pArgument, "/DMA") == 0) && (argc > indexArgs + 1))
    {
      int nDma = atoi(argv[indexArgs+1]);
      
      if ((nDma >= 0) && (nDma < 4))
      {
        nUseDMA = (unsigned char)nDma;
      }
      else
      {
        PrintUsage();
      }
    }
    
    // Invalid or misspelled command
    else
    {
      PrintUsage();
    }
  }
    
  // Force 512B physical sectors and logical TYPE - also force 500k MFM
  if (nForce512Physical == 1)
  {
    unsigned int nTotalBytesPerTrack = nSectorsPerTrack * nPhysicalSectorSize;
    
    nPhysicalSectorSize = 512;
    nUseFM = 0;
    nDataRateKbps = 500;
    
    // Recalculate new (physical) sectors per track count, keep the logical count as is.
    nSectorsPerTrack = (unsigned char)(nTotalBytesPerTrack / nPhysicalSectorSize);
    if ((nTotalBytesPerTrack % nPhysicalSectorSize) > 0)
    {
      nSectorsPerTrack++;
    }
  }
  
  // Only update the DPT - do not show any warnings following
  if (nOnlyReprogramBIOS == 1)
  {
    return;
  }
   
  // Running on IBM PC/XT with 500kbps bitrate specified - just warn and run
  if ((nDataRateKbps == 500) && (IsPCXT() == 1))
  {
    printf("\nPC/XT detected. Unless there's a special FDC, the bitrate is capped to 250kbps.\n");
  }
  
  // Running on IBM PC/XT with FM encoding specified - just warn and run
  if ((nUseFM == 1) && (IsPCXT() == 1))
  {
    printf("\nPC/XT detected. Unless there's a special FDC, the encoding is always MFM.\n");
  }
  
  // 512B physical sector size overriden for TYPE, without creating filesystem
  if ((nForce512Physical == 1) && (nNoCreateFilesystem == 1))
  {
    printf("\nTYPE %s with 512B sectors not treated logical, as FAT12 creation is skipped.\n", sFormatType);
  }
  
  // Custom FDC port, IRQ or DMA specified ?
  if ((nFDCBase != 0x3f0) || (nUseIRQ != 6) || (nUseDMA != 2))
  {
    printf("\nUsing floppy controller base address at 0x%03X, IRQ %d and DMA channel %d.\n", nFDCBase, nUseIRQ, nUseDMA);
  }
  
  // FAT on a non-standard physical sector size
  if ((nNoCreateFilesystem == 0) && (nPhysicalSectorSize != 512))
  {
    printf("\nAttempting to write FAT12 on a nonstandard (%uB) floppy physical sector size.\n", nPhysicalSectorSize);
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
  // Allocate buffer for DMA transfers
  InitializeDMABuffer();
  
  // Inform about the drive geometry
  printf("\nUsing%s TYPE %s with the following physical geometry and parameters:\n"
         "%u tracks, %u %s, %u %uB sectors, R/W gap 0x%02X, Gap3 0x%02X, %ukbps%s %s%s.\n\n",
         ((nPhysicalSectorSize != nLogicalSectorSize) && (nNoCreateFilesystem == 0)) ? " logical" : "",
         sFormatType,
         nTracks,
         nHeads,
         (nHeads > 1) ? "heads" : "head",
         nSectorsPerTrack,
         nPhysicalSectorSize,
         GetGapLength(0),
         GetGapLength(1),
         nDataRateKbps,
         ((IsPCXT() == 1) && (nDataRateKbps > 250)) ? "(?)" : "",
         (nUseFM == 1) ? "FM" : "MFM",
         ((IsPCXT() == 1) && (nUseFM == 1)) ? "(?)" : "");
         
  /// Only reprogram the BIOS floppy geometry ?
  if (nOnlyReprogramBIOS == 1)
  {
    FDDWriteINT1Eh();
    Quit(EXIT_SUCCESS);
  }
  
  // Ask to put the disk into drive
  AskToContinue();
  
  // Prepare FDC and drive
  printf("Initializing floppy drive controller...");
  FDDReset();
  FDDCalibrate();
  DelLine();

  // Format 77 tracks
  if (nQuickFormat == 0)
  {
    unsigned char nTrackIndex = 0;
    unsigned char nHeadIndex = 0;
    unsigned int nBadsOccurence = 0;
    
    printf("Formatting...\n");
    
    for (; nTrackIndex < nTracks; nTrackIndex++)
    {
      while (nHeadIndex != nHeads)
      {
        unsigned char nVerifyResult;
        
        printf("\rHead: %u Track: %u  ", nHeadIndex, nTrackIndex);        
        FDDSeek(nTrackIndex, nHeadIndex);
        
        // Format without verify?
        if (nFormatWithVerify == 0)
        {
          printf("\r");
          FDDFormat();
          
          nHeadIndex++;
          continue;
        }        
        
        // Simple format with verify: write and read the first and last sectors on track   
        printf(" Format");
        FDDFormat(); //Whole track at once
        
        // Write 0xAA's on the first and last sectors
        printf(" Write");
        memset(pDMABuffer, 0xaa, nPhysicalSectorSize);
        FDDWrite(1);
        FDDWrite(nSectorsPerTrack);
        
        // Read the sectors. Compare the last byte of each to check for 0xAA
        printf(" Verify");
        FDDRead(1);
        nVerifyResult = pDMABuffer[nPhysicalSectorSize-1] == 0xAA;
        
        if (nVerifyResult == 1)
        {
          FDDRead(nSectorsPerTrack);
          nVerifyResult = pDMABuffer[nPhysicalSectorSize-1] == 0xAA;
        }
                
        // Check the result
        if (nVerifyResult != 1)
        {
          printf(" - did not format correctly\n");
          nBadsOccurence++;
        }
        
        // If passed, write the 0xF6 format byte back again to those 2 sectors.
        else
        {
          memset(pDMABuffer, nFormatByte, nPhysicalSectorSize);
          FDDWrite(1);
          FDDWrite(nSectorsPerTrack);        
          
          DelLine();
        }
        
        nHeadIndex++;
      }

      nHeadIndex = 0;
    }
    
    // Skip creation of filesystem if any bad occurences were found during verify
    if (nBadsOccurence > 0)
    {
      printf("\n\nA total of %d tracks failed to format properly. ", nBadsOccurence);
      
      if (nNoCreateFilesystem == 0)
      {
        printf("Skipping filesystem creation.\nTo force it, run 8FORMAT without /V.");
        nNoCreateFilesystem = 1;
      }
      
      printf("\n");
    }
  }

  DelLine();

  // Write FAT12
  if (nNoCreateFilesystem == 0)
  {
    unsigned char nScanCode = 0;
    
    printf("Creating file system...\n");
    WriteFAT12();
    
    // Ask to update the BIOS INT 1Eh diskette parameter table, if sector size wasn't 512B    
    if (nPhysicalSectorSize != 512)
    {
      printf("Finished, with a nonstandard physical sector size that the BIOS might not read.\n\n"
             "Apply a new BIOS Diskette Parameter Table (DPT) to reflect the new sector size?\n"
             "Warning: After that, other floppy drives might not work until you reboot.\n"
             "ENTER: continue, ESC: skip.\n");
    
      nScanCode = WaitEnterOrEscape();

      // Update BIOS floppy parameter table
      if (nScanCode == 0x1C)
      {
        printf("\n");    
        FDDWriteINT1Eh();
      }

      Quit(EXIT_SUCCESS);
    }
  }
  
  printf("Finished.\n");  
  Quit(EXIT_SUCCESS);
}