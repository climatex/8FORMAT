#include <stdlib.h>
#include <dos.h>
#include <mem.h>

#include "8format.h"
#include "isadma.h"

// 8 inch drive mechanical control data, times in ms
const unsigned char cStepRate = 8;
const unsigned char cHeadLoadTime = 16;
const unsigned char cHeadUnloadTime = 240;

// IRQ fired to acknowledge?
volatile unsigned char nIRQTriggered = 0;

unsigned char nISRInstalled = 0;
unsigned char nDriveReady = 0;

// "Current" CHS used for seeking
unsigned char nCurrentTrack = 0;
unsigned char nCurrentHead = 0;

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
  switch(nSectorSize)
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
      return (nFormatting == 0) ? 0x1B : 0x54;
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

// The old IRQ 6 (INT 0E) pointer
void interrupt (*oldIrqSix)();

void interrupt newIrqSixISR()
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

// Install IRQ6 ISR
void InstallISR()
{
  if (nISRInstalled != 1)
  {
    oldIrqSix = getvect(0x0e);
    setvect(0x0e, newIrqSixISR);

    nISRInstalled = 1;
  }
}

void RemoveISR()
{
  if (nISRInstalled == 1)
  {
    setvect(0x0e, oldIrqSix);
  }
}

// Wait for controller response, with a 6 sec timeout
unsigned char WaitForIRQ()
{
  unsigned int nTimeout = 6000;
  
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
    delay(1);
  }
  
  printf("\nFloppy drive controller failed to respond in time (IRQ6 timeout).\n"
         "Check proper connection of the drive, disk presence, and if the FDC works okay.\n");  
  Quit(EXIT_FAILURE);
  return 0;
}

unsigned char FDDGetData()
{  
  unsigned int nTimeout = 6000;
  
  while(nTimeout != 0)
  {
    // Receive byte from FDC only if RQM=1 and direction is FDC->CPU
    if((inportb(nFDCBase + 4) & 0xC0) == 0xC0)
    {
      return inportb(nFDCBase + 5);
    }
    
    nTimeout--;
    delay(1);
  }
  
  printf("\nFailed to get data from the floppy drive controller (RQM / DIO timeout).\n"
         "Check proper connection of the drive, disk presence, and if the FDC works okay.\n");  
  Quit(EXIT_FAILURE);
  
  return 0;
}

void FDDSendData(unsigned char nData)
{  
  unsigned int nTimeout = 6000;
  
  while(nTimeout != 0) 
  {
    // Transmit a byte to FDC only if RQM=1 and direction is CPU->FDC
    if((inportb(nFDCBase + 4) & 0xC0) == 0x80)
    {
      outportb(nFDCBase + 5, nData);
      return;
    }
    
    nTimeout--;
    delay(1);
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
    delay(250);
    nDriveReady = 1;
    
    // Disable IRQ0 to prevent BIOS from turning the drive motor off automatically
    {
      unsigned char nIRQMask = inportb(0x21);
      outportb(0x21, nIRQMask | 1);
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
    
    // Enable IRQ0
    {
      unsigned char nIRQMask = inportb(0x21);
      outportb(0x21, nIRQMask & 0xfe);
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

void FDDCalibrate()
{
  unsigned int nIdx;
  unsigned char nST0;
  unsigned char nTrack;
  
  FDDHeadLoad();
  
  // Three retries
  for (nIdx = 0; nIdx < 3; nIdx++)
  {
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
        continue;
      }
      
      else
      {
        //Track not 0 after recal
        if (nTrack > 0) 
        {
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
      continue;
    }
  }
  
  printf("\nFailed drive recalibration after 3 attempts.\n");
  Quit(EXIT_FAILURE);
}

void FDDReset()
{  
  unsigned char nIdx;
  
  InstallISR();
  
  // Controller reset
  outportb(nFDCBase + 2, 0);
  delay(1);
  outportb(nFDCBase + 2, 0x0c); // IRQ allowed, motors off, no drive selected
  
  WaitForIRQ();
  
  // Check interrupt status 3x (a must, drive is in polling mode)
  for(nIdx = 0; nIdx < 4; nIdx++)
  {
    FDDSendCommand(8);
    FDDGetData(); //ST0, trashed
    FDDGetData(); //current track, trashed
  }

  //0x3 Fix drive data command - load new mechanical values
  FDDSendCommand(3);
  FDDSendData((cStepRate << 4) | (cHeadUnloadTime & 0x07));
  FDDSendData(cHeadLoadTime << 1);
  
  if (nDoubleDensity == 1)
  {
    // Set 500 kbps data rate for a DD 8" floppy. 3F7h only in AT
    outportb(nFDCBase + 7, 0);
  }
  else if (IsPCXT() == 0)
  {
    // If single-density 8" floppy, set 250 kbps data rate. But only on an AT and newer, XTs have it default
    outportb(nFDCBase + 7, 2);
  }
  
  FDDCalibrate();
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
      printf(" Not Ready");
    }
      
    // UC/equipment error
    if(nST0 & 0x10)
    {
      printf(" Drive Fault");
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
      printf(" No Data (Sector not found)");
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
      printf(" No address mark DAM");
    }    
  }
  
  return nError;
}

// Formats a whole track on the active (seeked) track and head number
void FDDFormat()
{ 
  unsigned char nIdx;
  
  printf("\rHead: %u Track: %u  \r", nCurrentHead, nCurrentTrack);
  
  // Clear 1K DMA buffer
  memset(pDMABuffer, 0, 1024);

  // Three retries
  for (nIdx = 0; nIdx < 3; nIdx++)
  {
    unsigned char nIdx2 = 0;
    
    unsigned char nST0;
    unsigned char nST1;
    unsigned char nST2;
    
    // Prepare 4-byte 'CHSN' format buffer for all sectors on track
    for(nIdx = 0; nIdx < nSectorsPerTrack; nIdx++)
    {
      pDMABuffer[nIdx2++] = nCurrentTrack;
      pDMABuffer[nIdx2++] = nCurrentHead;
      pDMABuffer[nIdx2++] = nIdx+1; //Sector number (1-based)
      pDMABuffer[nIdx2++] = ConvertSectorSize(nSectorSize); //Sector size 0 to 3
    }
    
    // Setup DMA
    PrepareDMABufferForTransfer(0, 4*nSectorsPerTrack);
   
    // Now send command    
    FDDSendCommand(0x4D); //0x4d Format track
    FDDSendData((nCurrentHead << 2) | nDriveNumber); //Which drive and head
    FDDSendData(ConvertSectorSize(nSectorSize)); //Sector size, 0 to 3
    FDDSendData(nSectorsPerTrack); //Last sector on track
    FDDSendData(GetGapLength(1)); //Format GAP3 length
    FDDSendData(0xf6); //Format fill byte 0xf6
      
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

// Writes a single sector on the active (seeked) track and head number
// Input: pre-set DMA buffer (with a max size of 1 sector)
//        sector number (1-based !)
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
    PrepareDMABufferForTransfer(0, nSectorSize);
   
    // Now send command    
    FDDSendCommand(0x45); //0x45 Write sector
    FDDSendData((nCurrentHead << 2) | nDriveNumber); //Which drive and head
    FDDSendData(nCurrentTrack); //currently seeked track and head
    FDDSendData(nCurrentHead); //lol redundant but needed
    FDDSendData(nSectorNo); //Which sector to write
    FDDSendData(ConvertSectorSize(nSectorSize)); //Sector size, 0 to 3
    FDDSendData(nSectorNo); //Write only one sector
    FDDSendData(GetGapLength(0)); //Gap length
    FDDSendData((nSectorSize == 128) ? (unsigned char)nSectorSize : 0xff); //Data transfer length
      
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

  printf("\nWrite operation of track %u, head %u, sector %u failed after 3 attempts.\n",
         nCurrentTrack, nCurrentHead, nSectorNo);
  Quit(EXIT_FAILURE);
}

