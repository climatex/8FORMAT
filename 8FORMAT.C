#include <stdlib.h>
#include <string.h>

#include "8format.h"
#include "isadma.h"

// Obtained from the command line later
unsigned int  nFDCBase = 0x3f0;
unsigned char nUseFM = 0;
unsigned char nQuickFormat = 0;
unsigned char nNoCreateFilesystem = 0;
unsigned char nDriveNumber = 0;
unsigned char nTracks = 77;
unsigned char nHeads = 0;
unsigned char nDoubleDensity = 255;
unsigned int  nSectorSize = 0;
unsigned char nSectorsPerTrack = 0;
unsigned char nCustomGapLength = 0;
unsigned char nCustomGap3Length = 0;

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
  static unsigned char nPcByte = 0;
  
  if (nPcByte == 0)
  {
    _asm {
      push ds
      mov ax,0f000h
      mov ds,ax
      mov al,[ds:0fffeh]
      mov [nPcByte],al
      pop ds
    }   
  }

  return (nPcByte == 0xff) || (nPcByte == 0xfe) || (nPcByte == 0xfb);
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
  printf("\nUsage:\n"
         "8FORMAT drv: TYPE [/512] [/FM] [/N] [/Q] [/FDC (port)] [/G (len)] [/G3 (len)]\n\n"
         "where:\n"
         " drv:   specify letter where the 77-track 8\" disk drive is installed,\n"
         " TYPE   specifies media geometry and density. Can be one of the following:\n"
         "        SSSD: 250K single sided, single density, 26 spt, 128B sectors, FAT12,\n"
         "        SSDD: 500K single sided, double density, 26 spt, 256B sectors, FAT12,\n"
         "        DSSD: 500K double sided, single density, 26 spt, 128B sectors, FAT12,\n"
         "        DSDD: 1.2M double sided, double density, 8 spt, 1024B sectors, FAT12.\n"
         " /512   (optional): Force 512B sectors regardless of chosen media or density.\n"
         " /FM    (optional): Use FM encoding instead of the default MFM for all types.\n"
         " /N     (optional): Format only, do not create boot sector and file system.\n"
         " /Q     (optional): Do not format, only create boot sector and file system.\n"
         " /FDC   (optional): Use a different floppy controller; base-port is in hex.\n"
         "                    The default is 0x3f0, the first FDC in the system.\n"
         " /G,/G3 (optional): Specify custom GAP (write) and Gap3 (format) lengths in hex\n"
         "                    Maximum: 0xff. The default is to autodetect based on TYPE.\n\n"
         "Note that the usage of 8\" DD media requires an HD-capable (500kbit/s) FDC.\n"
         "Formatting with /512, or using type SSDD without /N, is experimental.\n");

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
      }
      else if (pArgument[2] == 'D')
      {
        nDoubleDensity = 1;
      }

      if ((nHeads == 0) || (nDoubleDensity > 1))
      {
        PrintUsage();
      }

      continue;
    }
    
    // Force 512-byte sectors on the chosen drive geometry
    if (strcmp(pArgument, "/512") == 0)
    {
      nForce512Sectors = 1;
    }

    // Use frequency modulation on FDC
    if (strcmp(pArgument, "/FM") == 0)
    {
      nUseFM = 1;
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
  }
  
  // Both quick format and no filesystem options specified?
  if ((nQuickFormat == 1) && (nNoCreateFilesystem == 1))
  {
    PrintUsage();
  }
  
  // Third or fourth floppy drive on a newer machine?
  // (Do not display this error if a custom floppy FDC port has been specified.)
  if ((nFDCBase == 0x3F0) && ((nDriveNumber > 1) && (IsPCXT() == 0)))
  {
    printf("\nOnly two floppy drives are supported on this machine.\n");
    Quit(EXIT_FAILURE);
  }
  
  // Running on IBM PC/XT with DD media specified - just warn and run
  if ((nDoubleDensity == 1) && (IsPCXT() == 1))
  {
    printf("\nPC or XT detected. 8\" DD floppies are only supported with an HD-capable FDC.\n");
  }
  
  // Double density format using FM ?
  if ((nDoubleDensity == 1) && (nUseFM == 1))
  {
    printf("\nAttempting to use FM encoding in a double density mode.\n");
  }
  
  // Construct sector size and sectors per track information
  nSectorsPerTrack = (nDoubleDensity == 0) ? 26 : 8;
  nSectorSize = (nDoubleDensity == 0) ? 128 : 1024;
  
  // Special case: Experimental SSDD with FAT12 (unless /N specified)
  if ((nHeads == 1) && (nDoubleDensity == 1))
  {
    nSectorSize = 256;
    nSectorsPerTrack = 26;
  }
  
  // Special case 2: Experimental: use 512 bytes per sector
  if (nForce512Sectors == 1)
  {
    nSectorSize = 512;
    
    // Show warning message if we are in single density mode (250kbit/s)
    if (nDoubleDensity == 0)
    {
      printf("\nAttempting to format 512 byte sectors in a single density mode.\n");
    }
  }
}

void AskToContinue()
{
  unsigned char nScanCode = 0;
  
  printf("\nInsert a disk into drive %c: and press ENTER to continue; ESC to quit...",
         nDriveNumber + 65);
         
  while (nScanCode != 0x1C)
  {
    _asm {
      xor ax,ax
      int 16h
      mov nScanCode,ah
    }
    
    // ESC
    if (nScanCode == 1)
    {
      printf(" ESC\n");
      Quit(EXIT_SUCCESS);
    }
  }
  
  DelLine();
}

void DoOperations()
{  
  // Allocate buffer for DMA transfers
  InitializeDMABuffer();
  
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
      while (nHeadIndex != nHeads)
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
    printf("Creating file system...\n");
    WriteFAT12();
  }
  
  printf("Format completed.\n");  
  Quit(EXIT_SUCCESS);
}