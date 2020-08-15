#include <stdlib.h>
#include <string.h>

#include "8format.h"
#include "isadma.h"

// Obtained from the command line later
unsigned char nUseFM = 0;
unsigned char nNoCreateFilesystem = 0;
unsigned char nDriveNumber = 0;
unsigned char nTracks = 77;
unsigned char nHeads = 0;
unsigned char nDoubleDensity = 255;
unsigned int  nSectorSize = 0;
unsigned char nSectorsPerTrack = 0;

// Terminate with exit code, do cleanup beforehand
void Quit(int nStatus)
{
  RemoveISR();
  FDDHeadRetract();
  FreeDMABuffer();
  
  // Important: before passing control to DOS, use BIOS to blip the FDC
  // to get all the custom format configuration out, and all...
  _asm {
    xor ax,ax
    xor dx,dx
    int 13h
  }
  
  exit(nStatus);
}

// Determine if the machine is an IBM 5150 or XT.
unsigned char IsPCXT()
{
  unsigned char nPcByte = 0;
  _asm {
    push ds
    mov ax,0f000h
    mov ds,ax
    mov al,[ds:0fffeh]
    mov [nPcByte],al
    pop ds
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
         "8FORMAT drive: TYPE [/FM] [/N]\n\n"
         "where:\n"
         " drive: specify where the 77-track 8\" disk drive is installed; A: or B:\n"
         "        (on the IBM PC or XT, it can also be connected externally at C: or D:)\n"
         " TYPE   specifies media and density. Can be one of the following:\n"
         "        SSSD: 250K single sided, single density, 26 spt, 128B sectors, FAT12,\n"
         "        DSSD: 500K double sided, single density, 26 spt, 128B sectors, FAT12,\n"
         "        DSDD: 1.2M double sided, double density, 8 spt, 1024B sectors, FAT12,\n"
         "        SSDD: 500K (as in TRS-80 Model 2). 26 spt, 256B sectors. Without FAT.\n"
         " /FM    (optional): Uses FM encoding instead of the default MFM.\n"
         " /N     (optional): Format only, don't create filesystem. Ignored with SSDD.\n\n"
         "Note that the usage of 8\" DD media requires an HD-capable (500kbit/s) FDC.\n");

  Quit(EXIT_SUCCESS);
}

// Parse the command line
void ParseCommandLine(int argc, char* argv[])
{
  int indexArgs = 0;

  // Incorrect number of arguments
  if ((argc < 3) || (argc > 5))
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

    // Use frequency modulation on FDC
    if (strcmp(pArgument, "/FM") == 0)
    {
      nUseFM = 1;
    }
    
    // No filesystem    
    if (strcmp(pArgument, "/N") == 0)
    {
      nNoCreateFilesystem = 1;
    }
  }
  
  // Running on IBM PC/XT with DD media specified - just warn and run
  if ((nDoubleDensity == 1) && (IsPCXT() == 1))
  {
    printf("\nPC or XT detected. 8\" DD floppies are only supported with an HD-capable FDC.\n");
  }
  
  // Third or fourth floppy drive on a newer machine?
  if ((nDriveNumber > 1) && (IsPCXT() == 0))
  {
    printf("\nOnly two floppy drives are supported on this machine.\n");
    Quit(EXIT_FAILURE);
  }
  
  // Construct sector size and sectors per track information
  nSectorsPerTrack = (nDoubleDensity == 0) ? 26 : 8;
  nSectorSize = (nDoubleDensity == 0) ? 128 : 1024;
  
  // Special case: TRS-80 Mod. II SSDD-compatible (one side, double density), without FAT12
  if ((nHeads == 1) && (nDoubleDensity == 1))
  {
    nSectorSize = 256;
    nSectorsPerTrack = 26;
    nNoCreateFilesystem = 1;
  }
}

void DoOperations()
{  
  // Allocate buffer for DMA transfers
  InitializeDMABuffer();
  
  // Prepare FDC and drive
  FDDReset();

  // Format 77 tracks
  printf("\nFormatting...\n");  
  {
    unsigned char nTrackIndex = 0;
    unsigned char nHeadIndex = 0;
    
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