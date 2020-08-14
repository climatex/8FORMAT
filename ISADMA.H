#ifndef _ISADMA_H_
#define _ISADMA_H_

extern unsigned char* pDMABuffer;

void InitializeDMABuffer();
void FreeDMABuffer();
void PrepareDMABufferForTransfer(unsigned char, unsigned int);

#endif