// Read first 4 sectors of track 0 to check 8ISR applied sector size
// 1KB buffer filler byte 0xCC for unused/unread data

#include <stdio.h>
#include <stdlib.h>
#include <dos.h>
#include <ctype.h>
#include <string.h>

unsigned char nDriveNumber = 0;
const unsigned char nSectorInfo[] = "\n%s likely contains %s";

void ReadSector(unsigned char nSector)
{
  union REGS regs;
  struct SREGS sregs;
  FILE* pFile;
  char sFileName[12] = {0};
  unsigned char* pBuffer = malloc(1024);
  
  if (!pBuffer)
  {
    printf("\nMemory allocation error\n");
    exit(-1);
  }
  memset(pBuffer, 0xCC, 1024);
  
  regs.x.ax = 0;
  regs.h.dl = nDriveNumber;
  int86(0x13, &regs, &regs);
    
  regs.h.ah = 2;
  regs.h.al = 1;
  regs.h.ch = 0;
  regs.h.cl = nSector;
  regs.h.dh = 0;
  regs.h.dl = nDriveNumber;
  regs.x.bx = FP_OFF(pBuffer);
  sregs.es = FP_SEG(pBuffer);
  int86x(0x13, &regs, &regs, &sregs);
  
  if (regs.x.cflag > 0)
  {
    printf("\nINT 13h AH=02 Read sector %d failed for drive %u (%c:)", 
           nSector, nDriveNumber, nDriveNumber+0x41);
    free(pBuffer);
    return;
  }
  
  // Filler byte 0xCC for unread data
  sprintf(sFileName, "SECTOR%u.BIN", nSector);
  pFile = fopen(sFileName, "wb");
  if (!pFile)
  {
    printf("\nCannot create file %s", sFileName);
    free(pBuffer);
    return;
  }

  if ((pBuffer[0] == 0xCC) && (pBuffer[1023] == 0xCC))
  {
    printf(nSectorInfo, sFileName, "no valid data");
  }
  else if (pBuffer[128] == 0xCC)
  {
    printf(nSectorInfo, sFileName, "a 128-byte sector");
  }
  else if (pBuffer[256] == 0xCC)
  {
    printf(nSectorInfo, sFileName, "a 256-byte sector");
  }
  else if (pBuffer[512] == 0xCC)
  {
    printf(nSectorInfo, sFileName, "a 512-byte sector");
  }
  else
  {
    printf(nSectorInfo, sFileName, "a 1024-byte sector");
  }

  fwrite(pBuffer, 1024, 1, pFile);  
  fclose(pFile);
  free(pBuffer);
}

int main(int argc, char* argv[])
{ 
  const unsigned char* pArgument;  
  if (argc != 2)
  {
    printf("\nSpecify floppy drive (A: to D:)\n");
    return 0;
  }
  
  pArgument = strupr(argv[1]);
  if ((strlen(pArgument) > 2) || (pArgument[0] < 0x41) || (pArgument[0] > 0x44))
  {
    printf("\nInvalid drive specified\n");
    return -1;
  }
  
  nDriveNumber = pArgument[0] - 0x41;
  
  ReadSector(1);
  ReadSector(2);
  ReadSector(3);
  ReadSector(4);
  
  printf("\n");  
  return 0;  
}