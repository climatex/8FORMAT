#include <stdlib.h>
#include <string.h>
#include <dos.h>
#include <dir.h>

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
unsigned char nOnlyRecalibrateFDD = 0;
unsigned char nUseIRQ = 6;
unsigned char nUseDMA = 2;
unsigned char nLaunch8TSR = 0;
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
  
  // EXEC 8TSR if need be
  if (nLaunch8TSR == 1)
  {   
    // 8TSR.EXE must exist
    char* pSearchPath = searchpath("8tsr.exe");
    
    // Not in current directory, not in PATH
    if (pSearchPath == NULL)
    {
      printf("\n8TSR.EXE not found, cannot set the BIOS diskette parameter table.\n");
    }
    
    // Nested too much in 255byte PATH?
    else if (strlen(pSearchPath) > 200)
    {
      printf("\n8TSR.EXE nested in too many subdirectories. Aborting.\n");
    }
    
    // Overlay process
    else
    {     
      FDDWriteINT1Eh(pSearchPath);
    }
    
    printf("\nError launching 8TSR.EXE\n");
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

// Discard crappy systems right off the bat
void DetectMemory()
{
  asm int 0x12
  
  if (_AX < 256)
  {
    printf("\nNot enough memory\n");
    Quit(EXIT_FAILURE);
  }
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
  
  // And quit rightaway :)
  DetectMemory();
}

// Printed on incorrect or no command line arguments
void PrintUsage()
{
  printf("\nUse either 8FORMAT X: TYPE [/1] [/V] [/500K] [/MFM] [/FAT12] [/Q]\n"
         "                           [/FDC port] [/IRQ num] [/DMA num] [/G len] [/G3 len]\n"
         "        or 8FORMAT /USE TYPE [/1] [/G len] [/G3 len], where:\n\n"
         " X:     the drive letter where the 77-track 8\" disk drive is installed; A to D,\n"
         " TYPE   sets geometry, density (data encoding) and physical sector size:\n"
         "        DSSD: 500kB double-sided, single-density (FM),  26  128B sectors,\n"
         "        DSDD: 1.2MB double-sided, double-density (MFM),  8 1024B sectors,\n"
         "        EXT1: 1.2MB double-sided, double-density (MFM), 16  512B sectors,\n"
         "        EXT2: 1.0MB double-sided, double-density (MFM), 26  256B sectors,\n"         
         "        EXT3: 693kB double-sided, double-density (MFM),  9  512B sectors.\n"
         " /USE   for BIOS to use new Diskette Parameter Table for given TYPE geometry.\n"
         "        Applied for ALL floppy drives until reboot. Disk stays untouched.\n"
         " /1     (optional): Single-sided format for the chosen TYPE. Capacity halved.\n"
         " /V     (optional): Format and verify. MUCH slower.\n"         
         " /MFM   (optional): Use MFM data encoding with type DSSD.\n"
         " /FAT12 (optional): Format and try writing the DOS boot sector and filesystem.\n"
         " /Q     (optional): Do not format, just write the boot sector and filesystem.\n"
         " /FDC   (optional): Use a different FD controller base hex port; default 0x3f0.\n"
         "        Use /IRQ (3..15) and /DMA (0..3) to override default IRQ 6 and DMA 2.\n"
         " /G,/G3 (optional): Custom GAP (write) or Gap3 (format) sizes in hex. Max 0xff.\n");         

  Quit(EXIT_SUCCESS);
}

// Parse the command line
void ParseCommandLine(int argc, char* argv[])
{
  int indexArgs = 0;
  unsigned char nForce512Physical = 0;

  // Incorrect number of arguments
  if ((argc < 3) || (argc > 20))
  {
    PrintUsage();
  }

  for (indexArgs = 1; indexArgs < argc; indexArgs++)
  {
    // Case insensitive comparison
    const char* pArgument = strupr(argv[indexArgs]);

    // Determine drive letter
    if (indexArgs == 1)
    {
      // Only update the global BIOS Diskette Parameter Table, /USE TYPE
      // Optional: /1, /G, /G3, /512PH (UNDOCUMENTED)
      if (strcmp(pArgument, "/USE") == 0)
      {
        nOnlyReprogramBIOS = 1;
        continue;
      }
      
      // Determine drive letter (physical drive number)
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
    
    // Determine TYPE (density and geometry combo)
    else if (indexArgs == 2)
    {
      // ! UNDOCUMENTED: Only try to fix an out-of-alignment drive caused by BIOS POST seek
      // Requires drive letter as a first argument
      if ((strcmp(pArgument, "/UNFUCK") == 0) && (nOnlyReprogramBIOS == 0))
      {
        nOnlyRecalibrateFDD = 1;
        break;
      }
      
      // Determine TYPE      
      if (strlen(pArgument) != 4)
      {
        PrintUsage();
      }
      
      // Copy TYPE string.
      strncpy(sFormatType, pArgument, 4);
      
      // 500kB DSSD
      if (strcmp(pArgument, "DSSD") == 0)
      {
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
      
      // 693kB EXT3
      else if (strcmp(pArgument, "EXT3") == 0)
      {
        nSectorsPerTrack = 9;
        nLogicalSectorsPerTrack = 9;
        nPhysicalSectorSize = 512;
        nLogicalSectorSize = 512;
      }
      
      // 1.3MB CRAM UNDOCUMENTED
      else if (strcmp(pArgument, "CRAM") == 0)
      {
        nTracks = 78;
        nSectorsPerTrack = 17;
        nLogicalSectorsPerTrack = 17;
        nPhysicalSectorSize = 512;
        nLogicalSectorSize = 512;
        nCustomGapLength = 16;
        nCustomGap3Length = 32;
      }
      
      // Unrecognized
      else
      {
        PrintUsage();
      }
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
    
    // UNDOCUMENTED: Recalculate geometry with 512-byte physical sectors, treat TYPE as logical
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
    else if ((strcmp(pArgument, "/FDC") == 0) && (argc > indexArgs+1))
    {
      char* pEndPointer;
      unsigned long nPort = strtoul(argv[++indexArgs], &pEndPointer, 16);
      
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
    else if ((strcmp(pArgument, "/G") == 0) && (argc > indexArgs+1))
    {
      char* pEndPointer;
      unsigned long nGapLen = strtoul(argv[++indexArgs], &pEndPointer, 16);
      
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
    else if ((strcmp(pArgument, "/G3") == 0) && (argc > indexArgs+1))
    {
      char* pEndPointer;
      unsigned long nGapLen = strtoul(argv[++indexArgs], &pEndPointer, 16);
      
      if ((nGapLen != 0) && (nGapLen <= 0xff))
      {
        nCustomGap3Length = (unsigned char)nGapLen;
      }
      else
      {
        PrintUsage();
      }
    }
    
    // Custom IRQ number (dec.)
    else if ((strcmp(pArgument, "/IRQ") == 0) && (argc > indexArgs+1))
    {
      int nIrq = atoi(argv[++indexArgs]);
      
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
    else if ((strcmp(pArgument, "/DMA") == 0) && (argc > indexArgs+1))
    {
      int nDma = atoi(argv[++indexArgs]);
      
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
    
  // Force 512B physical sectors and logical TYPE - change the physical geometry
  if (nForce512Physical == 1)
  {
    unsigned int nTotalBytesPerTrack = nSectorsPerTrack * nPhysicalSectorSize;
        
    nPhysicalSectorSize = 512;
    
    // Recalculate new (physical) sectors per track count, keep the logical count as is.
    nSectorsPerTrack = (unsigned char)(nTotalBytesPerTrack / nPhysicalSectorSize);
    if ((nTotalBytesPerTrack % nPhysicalSectorSize) > 0)
    {
      nSectorsPerTrack++;
    }
  }
  
  // Only update the DPT or recal the drive - do not show any warnings following
  if ((nOnlyReprogramBIOS == 1) || (nOnlyRecalibrateFDD == 1))
  {
    return;
  }
  
  // Info or warning messages follow.
     
  // Running on IBM PC/XT
  if (IsPCXT() == 1)
  {
    printf("\nWARNING: IBM 5150 or XT detected. Unless there's a high-density capable FDC,"
           "\n8\" floppy access won't work (500kHz clock rate%sis required).\n",
           (nUseFM == 1) ? " with FM support " : " ");
  }
     
  // Custom FDC port, IRQ or DMA specified ?
  if ((nFDCBase != 0x3f0) || (nUseIRQ != 6) || (nUseDMA != 2))
  {
    printf("\nUsing floppy controller base address at 0x%03X, IRQ %d and DMA channel %d.\n", nFDCBase, nUseIRQ, nUseDMA);
  }
  
  // FAT on a non-standard physical sector size
  if ((nNoCreateFilesystem == 0) && (nPhysicalSectorSize != 512))
  {
    printf("\nWARNING: non-standard physical sector size for a FAT12 floppy (%u bytes).\n"
           "The disk might not be DOS-accessible.\n", nPhysicalSectorSize);
  }
  
  // FAT on a non-standard 512B geometry
  if ((nNoCreateFilesystem == 0) && (nForce512Physical == 1))
  {
    printf("\nWARNING: non-standard 512B sector geometry for a FAT12 floppy.\n"
           "Using FAT media ID 0xf0, the disk might not be universally accessible however.\n");
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
  printf("Insert a disk into drive %c: and press ENTER to continue; ESC to quit...",
         nDriveNumber + 65);

  // ESC
  if (WaitEnterOrEscape() == 1)
  {
    printf(" ESC\n");
    Quit(EXIT_SUCCESS);
  }
  
  DelLine();
}

void DoOperations()
{    
  // Only reprogram the BIOS floppy geometry ?
  if (nOnlyReprogramBIOS == 1)
  {
    FDDWriteINT1Eh(NULL);
    Quit(EXIT_SUCCESS);
  }
  
  // UNDOCUMENTED: Only try to fix a drive out of alignment ?
  if (nOnlyRecalibrateFDD == 1)
  {
    FDDReset();
    FDDCalibrate();
  
    // Seek to the last 77th track, and back to track 0 again
    FDDSeek(76, 0);
    delay(500);
    FDDCalibrate();

    Quit(EXIT_SUCCESS);
  }
  
  // Inform about the drive geometry
  printf("\nUsing%s TYPE %s with the following physical geometry and parameters:\n"
         "%u tracks, %u %s, %u %uB sectors, R/W gap 0x%02X, Gap3 0x%02X, 500kHz%s %s%s.\n\n",
         ((nPhysicalSectorSize != nLogicalSectorSize) && (nNoCreateFilesystem == 0)) ? " logical" : "",
         sFormatType,
         nTracks,
         nHeads,
         (nHeads > 1) ? "heads" : "head",
         nSectorsPerTrack,
         nPhysicalSectorSize,
         GetGapLength(0),
         GetGapLength(1),
         (IsPCXT() == 1) ? "(?)" : "",
         (nUseFM == 1) ? "FM" : "MFM",
         ((IsPCXT() == 1) && (nUseFM == 1)) ? "(?)" : "");
  
  // Ask to put the disk into drive
  AskToContinue();
  
  // Initialize DMA
  InitializeDMABuffer();
  
  // Prepare FDC and drive
  printf("Initializing floppy drive controller...");
  FDDReset();
  FDDCalibrate();
  
  // Seek to the last 77th track, and back to track 0 again
  FDDSeek(76, 0);
  delay(500);
  FDDCalibrate();
  DelLine();

  // Format 77 tracks
  if (nQuickFormat == 0)
  {
    unsigned char nTrackIndex = 0;
    unsigned char nHeadIndex = 0;
    unsigned int nBadsOccurence = 0;
    
    randomize();
    printf("Formatting...\n");
    
    for (; nTrackIndex < nTracks; nTrackIndex++)
    {
      while (nHeadIndex != nHeads)
      {       
        printf("\rHead: %u Track: %u  ", nHeadIndex, nTrackIndex);        
        FDDSeek(nTrackIndex, nHeadIndex);
        
        // Format without verify?
        if (nFormatWithVerify == 0)
        {
          printf("\r");
          FDDFormat();
        }
        
        else
        {
          // Format with verify
          unsigned char nVerifyResult;
          unsigned char nRandomIndex;
          const unsigned char nTestWriteByte = (unsigned char)random(256);
          
          printf(" Format");
          FDDFormat(); //Whole track at once
          
          // Write random byte on all sectors on track
          printf(" Write");
          memset(pDMABuffer, nTestWriteByte, nPhysicalSectorSize*nSectorsPerTrack);
          FDDWrite(0xff); //whole track
          
          // Read the sectors. Compare the last byte of each (sector buffer is filled with nTestWriteByte)
          printf(" Verify");
          memset(pDMABuffer, 0, nPhysicalSectorSize*nSectorsPerTrack);
          FDDRead(0xff); //whole track
          
          nRandomIndex = random(nPhysicalSectorSize*nSectorsPerTrack); //Random buffer position
          nVerifyResult = pDMABuffer[nRandomIndex] == nTestWriteByte; //Verify buffer contents
          
          if (nVerifyResult != 1)
          {
            printf(" - did not format correctly\n");
            nBadsOccurence++;
          }
          
          // Verify success
          else
          {
            DelLine();
            
            // Format the track again
            FDDFormat();
          }        
        }

        nHeadIndex++;
      }

      nHeadIndex = 0;
    }
    
    // Skip creation of filesystem if any bad occurences were found during verify
    if (nBadsOccurence > 0)
    {
      printf("\n\nA total of %d tracks failed to format properly.", nBadsOccurence);
      
      if (nNoCreateFilesystem == 0)
      {
        printf("\nSkipping filesystem creation. To force it, run 8FORMAT without /V.");
        nNoCreateFilesystem = 1;
      }
      
      printf("\n");
    }
  }

  DelLine();

  // Write FAT12
  if (nNoCreateFilesystem == 0)
  {   
    printf("Creating file system...\n");
    WriteFAT12();
  }
  
  printf("Finished.\n\n");
  FDDHeadRetract();
  
  // Ask to update the BIOS INT 1Eh diskette parameter table  
  printf("Apply a new BIOS Diskette Parameter Table (DPT) to reflect the new 8\" geometry?\n"
         "The BIOS/DOS will then try to use the new sector size, sector and track counts.\n"
         "Be warned that other floppy drives will not work until you reboot.\n"
         "Do not proceed if you run 8FORMAT from an another floppy drive.\n"
         "ENTER: continue, ESC: skip.\n");

  // Update BIOS floppy parameter table
  if (WaitEnterOrEscape() == 0x1C)
  {
    FDDWriteINT1Eh(NULL);
  }

  Quit(EXIT_SUCCESS);
}