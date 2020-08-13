#include <dos.h>
#include <stdlib.h>
#include <alloc.h>

#include "isadma.h"

unsigned char far* pDMABuffer = NULL; //Far pointer to the 1K DMA buffer (public)
unsigned char far* pDMABufferPrivate = NULL; //Our private pointer to an xK DMA buffer

unsigned int  nDMATransferLength = 0; // length of transfer in bytes, minus one
unsigned long nDMAAddress = 0;  //physical address

void InitializeDMABuffer()
{
  unsigned long nSize = 0;
  
  // Initialize 64K boundary aligned DMA buffer  
  for(;;)
  {        
    // We need only 1K
    nSize += 1024;
    
    // Was the buffer already allocated beforehand? If yes, free it
    if (pDMABufferPrivate != NULL)
    {
      farfree(pDMABufferPrivate);
    }
    
    // Allocate buffer
    pDMABufferPrivate = (unsigned char far*)farcalloc(nSize, sizeof(unsigned char));    
    if (pDMABufferPrivate == NULL)
    {
      printf("\nNot enough memory or 64K-boundary problem\n");
      Quit(EXIT_FAILURE);
    }
    
    // Form linear address from the segment and offset, BUT only if running on the 1st iteration.
    if (nDMAAddress == 0)
    {
      nDMAAddress = ((unsigned long)FP_SEG(pDMABufferPrivate) << 4) + FP_OFF(pDMABufferPrivate);
    }
    
    // No, it's not the first iteration - we needed to allocate 2K or more, because of the boundary crossing.
    else
    {
      nDMAAddress += nSize-1024;
    }
    
    // Check 64K boundary alignment for the DMA address AND also DMA address after 1K of data. 
    if ((nDMAAddress >> 16) == ((nDMAAddress+1024) >> 16))
    {
      // All set! Prepare the public 1K DMA memory pointer by translating the new linear address.
      pDMABuffer = (unsigned char far*)MK_FP((unsigned int)(nDMAAddress >> 4), (unsigned int)(nDMAAddress & 0xf));
      
      printf("\nDMA buffer at address: 0x%04lX (0x%04X:0x%04X)\n", nDMAAddress, FP_SEG(pDMABuffer), FP_OFF(pDMABuffer));
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
  
  farfree(pDMABufferPrivate);
}

void PrepareDMABufferForTransfer(unsigned char nToMemory, unsigned int nBytes)
{
  //nToMemory 1 to read from drive to memory, 0 to write from memory
  // TODO Reading from drive to memory not implemented yet: format with verify?
  unsigned char nDMAMode = nToMemory ? 0x46 : 0x4a; // Without autoinit bit, to support old systems
  nDMATransferLength = nBytes - 1;
  
  outportb(0x0a, 0x06);   //Mask channel 2

  //Program the address, low 8bits, high 8bits and higher 4bit nibble into the page register (20bit)
  outportb(0x0c, 0xff);   //Reset flip-flop
  outportb(0x04, (unsigned char)nDMAAddress);
  outportb(0x04, (unsigned char)(nDMAAddress >> 8));
  outportb(0x81, (unsigned char)(nDMAAddress >> 16));
 
  //Program the count; low 8bits, high 8bits
  outportb(0x0c, 0xff);   //Reset flip-flop
  outportb(0x05, (unsigned char)nDMATransferLength);
  outportb(0x05, (unsigned char)(nDMATransferLength >> 8));

  //Program transfer mode for read/write
  outportb(0x0b, nDMAMode);

  outportb(0x0a, 0x02);   //Unmask channel 2
}
