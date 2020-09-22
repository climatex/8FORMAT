#include <dos.h>
#include <stdlib.h>

#include "8format.h"
#include "isadma.h"

unsigned char* pDMABuffer = NULL; //Public pointer to the 1K DMA buffer
unsigned char* pDMABufferPrivate = NULL; //Our private pointer to an xK DMA buffer

unsigned int  nDMATransferLength = 0; // length of transfer in bytes, minus one
unsigned long nDMAAddress = 0;  //physical address

void InitializeDMABuffer()
{
  unsigned int nSize = 0;
  
  // For computing physical address in the 16bit segmentation model
  unsigned long nDataSegment = (unsigned long)_DS << 4;
  
  // Initialize 64K boundary aligned DMA buffer  
  for(;;)
  {            
    // We only need 1K
    nSize += 1024;
    
    // Was the buffer already allocated beforehand?
    if (pDMABufferPrivate != NULL)
    {
      free(pDMABufferPrivate);
    }
    
    // Allocate buffer    
    pDMABufferPrivate = (unsigned char*)malloc(nSize);    
    if (pDMABufferPrivate == NULL)
    {
      printf("\nNot enough memory or 64K-boundary problem\n");
      Quit(EXIT_FAILURE);
    }
    
    // Compute physical address of the buffer.
    // Do this only once (the very first iteration), or if the address of the bigger buffer has changed.
    if (nDMAAddress != nDataSegment + (unsigned int)pDMABufferPrivate)
    {
      nDMAAddress = nDataSegment + (unsigned int)pDMABufferPrivate;
    }
    
    // It is the 2nd iteration (or greater).
    // The buffer grows in 1024B increments to overcome the boundary. Only the last 1024B are used.
    else
    {
      nDMAAddress += nSize-1024;
    }
    
    // Check for 64K boundary crossing of the address alone && address+bufsize (1024B)
    if ((nDMAAddress >> 16) == ((nDMAAddress+1024) >> 16))
    {
      // It doesn't cross - we're all set. Set the public 1K DMA buffer pointer
      pDMABuffer = (unsigned char*)(nDMAAddress - nDataSegment);

      printf("\nDMA buffer at address: 0x%04lX (0x%04X:0x%04X)\n", nDMAAddress, _DS, (unsigned int)pDMABuffer);
      break;      
    }
  }  
}

void FreeDMABuffer()
{
  if(pDMABufferPrivate == NULL)
  {
    return;
  }
  
  free(pDMABufferPrivate);
}

void PrepareDMABufferForTransfer(unsigned char nToMemory, unsigned int nBytes)
{
  //nToMemory 1 to read from drive to memory, 0 to write from memory
  unsigned char nDMAMode = nToMemory ? 0x44 : 0x48; // Without autoinit bit, to support old systems
  unsigned char nDMABase = nUseDMA * 2; // DMA base register
  
  unsigned char nDMAPage; // DMA page register
  switch(nUseDMA)
  {
  case 0:
    nDMAPage = 0x87;
    break;
  case 1:
    nDMAPage = 0x83;
    break;
  case 2:
  default:
    nDMAPage = 0x81;
    break;
  case 3:
    nDMAPage = 0x82;
  }
  
  nDMATransferLength = nBytes - 1; // DMA transfer length (in bytes minus one)
  
  outportb(0x0a, nUseDMA | 4);   //Mask chosen channel

  //Program the address, low 8bits, high 8bits and higher 4bit nibble into the page register (20bit)
  outportb(0x0c, 0xff);   //Reset flip-flop
  outportb(nDMABase, (unsigned char)nDMAAddress);
  outportb(nDMABase, (unsigned char)(nDMAAddress >> 8));
  outportb(nDMAPage, (unsigned char)(nDMAAddress >> 16));
 
  //Program the count; low 8bits, high 8bits
  outportb(0x0c, 0xff);   //Reset flip-flop
  outportb(nDMABase+1, (unsigned char)nDMATransferLength);
  outportb(nDMABase+1, (unsigned char)(nDMATransferLength >> 8));

  //Program transfer mode for read/write
  outportb(0x0b, nUseDMA | nDMAMode);

  outportb(0x0a, nUseDMA);   //Unmask chosen channel
}
