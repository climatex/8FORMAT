#include <stdlib.h>
#include <dos.h>
#include <mem.h>
#include <process.h>

#include "8format.h"
#include "isadma.h"

// IRQ fired to acknowledge?
volatile unsigned char nIRQTriggered = 0;

unsigned char nISRInstalled = 0;
unsigned char nDriveReady = 0;

// "Current" CHS used for seeking
unsigned char nCurrentTrack = 0;
unsigned char nCurrentHead = 0;

// 8" floppy parameters have not yet been specified to the FDC
unsigned char nNeedsReset = 0;

// Sector size from bytes to 0-3
unsigned char ConvertSectorSize(unsigned int nSize)
{
  switch(nSize)
  {
  case 128:
  default:
    return 0;
  case 256:
    return 1;
  case 512:
    return 2;
  case 1024:
    return 3;  
  }
}

// Provide GAP and Gap3 lengths
unsigned char GetGapLength(unsigned char nFormatting)
{
  // User-provided gap length for read/write
  if ((nFormatting == 0) && (nCustomGapLength != 0))
  {
    return nCustomGapLength;
  }
  
  // User-provided GAP3 length for track format
  if ((nFormatting == 1) && (nCustomGap3Length != 0))
  {
    return nCustomGap3Length;
  }
  
  // Try to autodetect. (values per NEC uPD765 datasheet, Table 3)  
  switch(nPhysicalSectorSize)
  {
  case 128:
  default:
    return (nFormatting == 0) ? 7 : 0x1B;
    
  case 256:
  {
    if (nUseFM == 1)
    {
      return (nFormatting == 0) ? 0x0E : 0x2A;
    }
    else
    {
      return (nFormatting == 0) ? 0x0E : 0x36;
    }
  }
    
  case 512:
  {
    if (nUseFM == 1)
    {
      return (nFormatting == 0) ? 0x1B : 0x3A;
    }
    else
    {
      // EXT3: return vanilla recommended value
      if (strcmp(sFormatType, "EXT3") == 0)
      {
        return (nFormatting == 0) ? 0x1B : 0x54;
      }
      else
      {
        // These gaps a little lower, to cram a maximum of 16 512B sectors per track :)
        return (nFormatting == 0) ? 0x18 : 0x50;
      }
    }
  }
        
  case 1024:
  {
    if (nUseFM == 1)
    {
      return (nFormatting == 0) ? 0x47 : 0x8A;
    }
    else
    {
      return (nFormatting == 0) ? 0x35 : 0x74;
    } 
  }
  }
}

// The old IRQ pointer
void interrupt (*oldIrqISR)();

void interrupt newIrqISR()
{
   // Set triggered flag + inform the PIC to re-enable IRQs again 
   _asm {
      cli
      mov [nIRQTriggered],1
      mov al,20h
      out 20h, al
      sti
   }
}

// Install IRQ ISR
void InstallISR()
{
  const unsigned char nINTVector = (nUseIRQ > 7) ? nUseIRQ+0x68 : nUseIRQ+8;
  
  if (nISRInstalled != 1)
  {
    oldIrqISR = getvect(nINTVector);
    setvect(nINTVector, newIrqISR);

    nISRInstalled = 1;
  }
}

void RemoveISR()
{
  const unsigned char nINTVector = (nUseIRQ > 7) ? nUseIRQ+0x68 : nUseIRQ+8;
  
  if (nISRInstalled == 1)
  {
    setvect(nINTVector, oldIrqISR);
  }
}

// Wait for controller response, with a 6 sec timeout
unsigned char WaitForIRQ()
{
  unsigned int nTimeout = 1200;
  
  if (nISRInstalled != 1)
  {
    return 0;
  }

  while(nTimeout != 0)
  {
    if (nIRQTriggered == 1)
    {
      nIRQTriggered = 0;
      return 1;
    }
    
    nTimeout--;
    delay(5);
  }
  
  printf("\nFloppy drive controller failed to respond in time (IRQ %d timeout).\n"
         "Check proper connection of the drive, disk presence, and if the FDC works okay.\n",
         nUseIRQ);
  Quit(EXIT_FAILURE);
  return 0;
}

unsigned char FDDGetData()
{  
  unsigned int nTimeout = 1200;
  
  while(nTimeout != 0)
  {
    // Receive byte from FDC only if RQM=1 and direction is FDC->CPU
    if((inportb(nFDCBase + 4) & 0xC0) == 0xC0)
    {
      return inportb(nFDCBase + 5);
    }
    
    nTimeout--;
    delay(5);
  }
  
  printf("\nFailed to get data from the floppy drive controller (RQM / DIO timeout).\n"
         "Check proper connection of the drive, disk presence, and if the FDC works okay.\n");  
  Quit(EXIT_FAILURE);
  
  return 0;
}

void FDDSendData(unsigned char nData)
{  
  unsigned int nTimeout = 1200;
  
  while(nTimeout != 0) 
  {
    // Transmit a byte to FDC only if RQM=1 and direction is CPU->FDC
    if((inportb(nFDCBase + 4) & 0xC0) == 0x80)
    {
      outportb(nFDCBase + 5, nData);
      return;
    }
    
    nTimeout--;
    delay(5);
  }
  
  printf("\nFailed to send data to the floppy drive controller (RQM / DIO timeout).\n"
         "Check proper connection of the drive, disk presence, and if the FDC works okay.\n");  
  Quit(EXIT_FAILURE);
}

void FDDSendCommand(unsigned char nCommand)
{
  // Use modifier to clear the MFM flag for specifying main commands
  (nUseFM == 1) ? FDDSendData(nCommand & 0xbf) : FDDSendData(nCommand);
}

void FDDHeadLoad()
{
  // "Motor on" (in 8" drives always on), select the drive and allow IRQ6 from controller
  if (nDriveReady != 1)
  {
    outportb(nFDCBase + 2, (nDriveNumber & 0x03) | (1 << (4 + nDriveNumber)) | 0x0C);
    nDriveReady = 1;
    
    // Disable IRQ0 to prevent BIOS from turning the drive motor off automatically
    // Also mask IRQ1 (keyboard interrupts)
    {
      unsigned char nIRQMask = inportb(0x21);
      outportb(0x21, nIRQMask | 3);
    }
  }
}

void FDDHeadRetract()
{
  if (nDriveReady == 1)
  {
    // "Motor off", drive unselected
    outportb(nFDCBase + 2, (nDriveNumber & 0x03) | 0x0C);
    nDriveReady = 0;
    
    // Enable IRQ0 & IRQ1
    {
      unsigned char nIRQMask = inportb(0x21);
      outportb(0x21, nIRQMask & 0xfc);
    }
  }
}

void FDDSeek(unsigned char nTrack, unsigned char nHead)
{
  unsigned char nIdx;
  unsigned char nST0;
  unsigned char nResultTrack;
  
  // Three retries
  for (nIdx = 0; nIdx < 3; nIdx++)
  {
    FDDSendCommand(0xf); //0xF Seek
    FDDSendData((nHead << 2) | nDriveNumber); //Which drive and head
    FDDSendData(nTrack);

    WaitForIRQ();
    
    // 0x8 Sense interrupt command after completion
    FDDSendCommand(8);
    nST0 = FDDGetData(); //ST0 status register byte
    nResultTrack = FDDGetData(); //Current cylinder

    // Seek ended successfully ?
    if(nST0 & 0x20)
    {
      //UC error
      if(nST0 & 0x10)
      {
        continue;
      }

      else
      {
        // Not on our track
        if (nResultTrack != nTrack)
        {
          continue;
        }

        else
        {
          // Seek success
          nCurrentTrack = nTrack;
          nCurrentHead = nHead;
          
          // A 50ms delay after each seek as a safety margin for all drives
          delay(50);          
          return;
        }
      }
    }

    // Seek did not succeed
    else
    {
      continue;
    }
  }

  printf("\nFailed seeking to track %u on head %u after 3 attempts.\n",
         nTrack, nHead);

  Quit(EXIT_FAILURE);
}

void FDDReset()
{  
  unsigned char nIdx;
   
  InstallISR();
  
  // Controller reset
  outportb(nFDCBase + 2, 0);
  delay(25);
  outportb(nFDCBase + 2, 0x0c); // IRQ allowed, motors off, no drive selected
  delay(25);
  
  WaitForIRQ();
  
  // Check interrupt status 3x (a must, drive is in polling mode)
  for(nIdx = 0; nIdx < 4; nIdx++)
  {
    FDDSendCommand(8);
    FDDGetData(); //ST0, trashed
    FDDGetData(); //current track, trashed
  }
  
  // Always set the 500kbps FDC communication data rate, with 500kHz clock timing.
  // Practical data rate on MFM (1 bit per 1 databit) is 500 kbps, and 250kbps on FM (2 bits per 1 databit)
  // This does absolutely nothing on XT without a HD-capable FDC.
  outportb(nFDCBase + 7, 0);

  delay(25);

  // 0x3 Fix drive data command - load new mechanical values
  // Table data @ https://www.isdaman.com/alsos/hardware/fdc/floppy.htm
  nNeedsReset = 1;
  FDDSendCommand(3);
  
  // 8 inch drives are old as the republic, so using very conservative stepper values:
  // 8ms track-to-track step rate, 252ms head load time, 224ms unload time
  
  // First byte is SRT (upper nibble) | HUT (lower nibble)
  // Step rate time (SRT): 8 ms (500kbps FDC comm rate: 8 << 4)
  // Head unload time (HUT): 224 ms (500kbps FDC comm rate: 14)
  FDDSendData(0x8e);
  
  // Second byte is HLT (upper 7 bits) | non-DMA mode flag (bit 0)
  // Head load time (HLT): 252 ms (500kbps FDC comm rate: 126 << 1)
  // Non-DMA mode: 0 (we use DMA)
  FDDSendData(0xfc);
}

void FDDCalibrate()
{
  unsigned int nIdx;
  unsigned char nST0;
  unsigned char nTrack;
  
  unsigned char nCalibrationError = 0;
  
  FDDHeadLoad();
  
  // Three retries
  for (nIdx = 0; nIdx < 3; nIdx++)
  {
    if (nCalibrationError == 1)
    {
      FDDHeadRetract();
      FDDReset();
      FDDHeadLoad();
      nCalibrationError = 0;
    }
    
    FDDSendCommand(7); //0x7 Recalibrate command
    FDDSendData(nDriveNumber); //Which drive (0 to 3)
    
    WaitForIRQ();
   
    // 0x8 Sense interrupt command after completion
    FDDSendCommand(8);
    nST0 = FDDGetData(); //ST0 status register byte
    nTrack = FDDGetData(); //Current track

    // Seek ended successfully ?
    if(nST0 & 0x20)
    {
      //UC error
      if(nST0 & 0x10)
      {
        nCalibrationError = 1;
        continue;
      }
      
      else
      {
        //Track not 0 after recal
        if (nTrack > 0) 
        {
          nCalibrationError = 1;
          continue;
        }
        
        else
        {
          //Success
          return;
        }            
      }
    }
    
    // Seek did not succeed for whatever reason
    else
    {
      nCalibrationError = 1;     
      continue;
    }
  }
  
  printf("\nFailed drive recalibration after 3 attempts.\n");
  Quit(EXIT_FAILURE);
}

// Called upon application exit
void FDDResetBIOS()
{
  // Use BIOS INT13h to recalibrate the FDC before passing control to the OS.
  // This is important, as it loads BIOS defaults (from INT 1Eh) into the FDC.
  if (nNeedsReset != 1)
  {
    return;
  }
  
  // If the previous operation failed, assume the FDC seized up dead!
  outportb(nFDCBase + 2, 0); // Hard reset of the controller
  delay(25);
  outportb(nFDCBase + 2, 0x0c);
    
  // Now use BIOS to recalibrate and load defaults to all drives (3 to 0)
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
}

unsigned char DetectErrors(unsigned char nST0, unsigned char nST1, unsigned char nST2)
{
  unsigned char nError = 0;

  // Any errors in the ST0 status register?
  if ((nST0 & 0xC0) != 0)
  {
    nError = 1;
    printf("\n!ST0 0x%x", nST0);
      
    // Drive not ready error
    if ( ((nST0 & 0xC0) == 0xC0) || ((nST0 & 8) == 8) )
    {
      printf(" Not ready");
    }
      
    // UC/equipment error
    if(nST0 & 0x10)
    {
      printf(" Drive fault");
    }
  }
    
  //Any errors in the ST1 status register ?
  if (nST1 > 0)
  {
    nError = 1;
    printf("\n!ST1 0x%x", nST1);
      
    if (nST1 & 0x80)
    {
      printf(" Sector count per track exceeded");
    }
    if (nST1 & 0x20)
    {
      printf(" Error in data");
    }
    if (nST1 & 0x10)
    {
      printf(" Timeout or data overrun");
    }
    if (nST1 & 4)
    {
      printf(" No data (sector not found)");
    }
    if (nST1 & 2)
    {
      printf("\nWrite protect error\n");
      Quit(EXIT_FAILURE); //No point of retrying the command, quit now
    }
    if (nST1 & 1)
    {
      printf(" No address mark");
    }
  }
    
  //Errors in ST2 status register
  if (nST2 > 0)
  {
    nError = 1;
    printf("\n!ST2 0x%x", nST2);
    
    if (nST2 & 0x20)
    {
      printf(" CRC error in data field");
    }
    if (nST2 & 0x10)
    {
      printf(" Wrong track in ID address mark");
    }
    if (nST2 & 2)
    {
      printf(" Bad track or defective media");
    }
    if (nST2 & 1)
    {
      printf(" No deleted address mark");
    }    
  }
  
  // Reset and recalibrate on error
  if (nError)
  {
    FDDHeadRetract();
    FDDReset();
    FDDCalibrate();
    FDDSeek(nCurrentTrack, nCurrentHead);
  }
  
  return nError;
}

// Formats a whole track on the active (seeked) track and head number
void FDDFormat()
{ 
  unsigned char nIdx;
    
  // Clear 8K DMA buffer
  memset(pDMABuffer, 0, 8*1024);

  // Three retries
  for (nIdx = 0; nIdx < 3; nIdx++)
  {
    unsigned char nIdx2 = 0;
    unsigned char nIdx3 = 0;
    
    unsigned char nST0;
    unsigned char nST1;
    unsigned char nST2;
    
    // Prepare 4-byte 'CHSN' format buffer for all sectors on track
    for(nIdx2 = 0; nIdx2 < nSectorsPerTrack; nIdx2++)
    {
      pDMABuffer[nIdx3++] = nCurrentTrack;
      pDMABuffer[nIdx3++] = nCurrentHead;
      pDMABuffer[nIdx3++] = nIdx2+1; //Sector number (1-based)
      pDMABuffer[nIdx3++] = ConvertSectorSize(nPhysicalSectorSize); //Sector size 0 to 3
    }
    
    // Setup DMA
    PrepareDMABufferForTransfer(0, 4*nSectorsPerTrack);
   
    // Now send command    
    FDDSendCommand(0x4D); //0x4d Format track
    FDDSendData((nCurrentHead << 2) | nDriveNumber); //Which drive and head
    FDDSendData(ConvertSectorSize(nPhysicalSectorSize)); //Sector size, 0 to 3
    FDDSendData(nSectorsPerTrack); //Last sector on track
    FDDSendData(GetGapLength(1)); //Format GAP3 length
    FDDSendData(nFormatByte); //Format fill byte
      
    WaitForIRQ();
    
    // Get status information   
    nST0 = FDDGetData();
    nST1 = FDDGetData();
    nST2 = FDDGetData();
    
    // Track, head, sector number and size trashed
    FDDGetData();
    FDDGetData();
    FDDGetData();
    FDDGetData();
    
    if (DetectErrors(nST0, nST1, nST2) == 0)
    {
      // No errors, success
      return;  
    }
    
    // Errors detected - next attempt
    printf("\n");
  }

  printf("\nFormatting failed after 3 attempts.\n");
  Quit(EXIT_FAILURE);
}

// Writes either a single sector on the active (seeked) track and head number, or whole track
// Input: pre-set DMA buffer with proper size
//        sector number (1-based !), or 255 (0xff) for whole track
void FDDWrite(unsigned char nSectorNo)
{ 
  unsigned char nIdx;
  
  // Three write retries
  for (nIdx = 0; nIdx < 3; nIdx++)
  {
    unsigned char nST0;
    unsigned char nST1;
    unsigned char nST2;
    
    // Setup DMA
    PrepareDMABufferForTransfer(0, (nSectorNo != 0xff) ? 
                                   nPhysicalSectorSize : 
                                   nPhysicalSectorSize*nSectorsPerTrack);
   
    // Now send command    
    FDDSendCommand(0x45); //0x45 Write sector
    FDDSendData((nCurrentHead << 2) | nDriveNumber); //Which drive and head
    FDDSendData(nCurrentTrack); //currently seeked track and head
    FDDSendData(nCurrentHead); //lol redundant but needed
    FDDSendData((nSectorNo != 0xff) ? nSectorNo : 1); //Which sector to write, or whole track from sector 1
    FDDSendData(ConvertSectorSize(nPhysicalSectorSize)); //Sector size, 0 to 3
    FDDSendData((nSectorNo != 0xff) ? nSectorNo : nSectorsPerTrack); //Write only one sector or whole track
    FDDSendData(GetGapLength(0)); //Gap length
    FDDSendData((nPhysicalSectorSize == 128) ? (unsigned char)nPhysicalSectorSize : 0xff); //Data transfer length
      
    WaitForIRQ();
    
    // Get status information   
    nST0 = FDDGetData();
    nST1 = FDDGetData();
    nST2 = FDDGetData();
    
    // Track, head, sector number and size trashed
    FDDGetData();
    FDDGetData();
    FDDGetData();
    FDDGetData();
    
    if (DetectErrors(nST0, nST1, nST2) == 0)
    {
      // No errors, success
      return;  
    }
    
    // Errors detected - next attempt
    printf("\n");
  }
  
  // Format error message for single sector/whole track operation
  if (nSectorNo != 0xff)
  {
    printf("\nWrite operation of track %u, head %u, sector %u failed after 3 attempts.\n",
           nCurrentTrack, nCurrentHead, nSectorNo);
  }
  
  else
  {
    printf("\nWrite operation of track %u on head %u failed after 3 attempts.\n",
           nCurrentTrack, nCurrentHead);
  }
  
  Quit(EXIT_FAILURE);
}

// To update the INT 1Eh BIOS diskette parameter table, execute 8TSR.
void FDDWriteINT1Eh(unsigned char* pPath)
{  
  static unsigned char* pArguments[] = { NULL, "255", "255", "255", "255", "255", "255", "255", "255", NULL};
    
  // Here, just prepare the arguments to execute 8TSR just before the end in Quit()  
  if (pPath == NULL)
  {
    // Command line arguments: 
    // drivenumber tracks heads sectorsize EOT RWgap DTL GAP3
    // All values decadic and unsigned char (0-255)
    sprintf(pArguments[1], "%u", nDriveNumber);
    sprintf(pArguments[2], "%u", nTracks);
    sprintf(pArguments[3], "%u", nHeads);
    sprintf(pArguments[4], "%u", ConvertSectorSize(nPhysicalSectorSize));
    sprintf(pArguments[5], "%u", nSectorsPerTrack);
    sprintf(pArguments[6], "%u", GetGapLength(0));
    sprintf(pArguments[7], "%u", (nPhysicalSectorSize == 128) ? 0x80 : 0xff);
    sprintf(pArguments[8], "%u", GetGapLength(1));
    
    nLaunch8TSR = 1;
    return;
  }
    
  // Overlay 8FORMAT with 8TSR. Called in Quit()
  pArguments[0] = pPath;
  execv(pPath, pArguments);
}

// Reads either a single sector from the active (seeked) track and head number, or whole track
// Input: sector number (1-based !), or 255 (0xff) for whole track
void FDDRead(unsigned char nSectorNo)
{ 
  unsigned char nIdx;
  
  // Clear 8K DMA buffer
  memset(pDMABuffer, 0, 8*1024);
   
  // Three read retries
  for (nIdx = 0; nIdx < 3; nIdx++)
  {
    unsigned char nST0;
    unsigned char nST1;
    unsigned char nST2;
    
    // Setup DMA
    PrepareDMABufferForTransfer(1, (nSectorNo != 0xff) ? 
                                   nPhysicalSectorSize : 
                                   nPhysicalSectorSize*nSectorsPerTrack);
       
    // Now send command    
    FDDSendCommand(0x46); //0x46 Read sector
    FDDSendData((nCurrentHead << 2) | nDriveNumber); //Which drive and head
    FDDSendData(nCurrentTrack); //currently seeked track and head
    FDDSendData(nCurrentHead); //lol redundant but needed
    FDDSendData((nSectorNo != 0xff) ? nSectorNo : 1); //Which sector to read, or whole track from sector 1
    FDDSendData(ConvertSectorSize(nPhysicalSectorSize)); //Sector size, 0 to 3
    FDDSendData((nSectorNo != 0xff) ? nSectorNo : nSectorsPerTrack); //Read only one sector or whole track
    FDDSendData(GetGapLength(0)); //Gap length
    FDDSendData((nPhysicalSectorSize == 128) ? (unsigned char)nPhysicalSectorSize : 0xff); //Data transfer length
      
    WaitForIRQ();
    
    // Get status information   
    nST0 = FDDGetData();
    nST1 = FDDGetData();
    nST2 = FDDGetData();
    
    // Track, head, sector number and size trashed
    FDDGetData();
    FDDGetData();
    FDDGetData();
    FDDGetData();
    
    if (DetectErrors(nST0, nST1, nST2) == 0)
    {
      // No errors, success
      return;  
    }
    
    // Errors detected - next attempt
    printf("\n");
  }
  
  // Format error message for single sector/whole track operation
  if (nSectorNo != 0xff)
  {
    printf("\nRead of track %u, head %u, sector %u failed after 3 attempts.\n",
           nCurrentTrack, nCurrentHead, nSectorNo);
  }
  
  else
  {
    printf("\nRead of track %u on head %u failed after 3 attempts.\n",
           nCurrentTrack, nCurrentHead);
  }

  Quit(EXIT_FAILURE);
}