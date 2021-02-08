#ifndef _8FLOPPY_H_
#define _8FLOPPY_H_

unsigned char ConvertSectorSize(unsigned int);
unsigned char GetGapLength(unsigned char);
void RemoveISR();

void FDDHeadRetract();

void FDDReset();
void FDDResetBIOS();

void FDDFormat();
void FDDWrite(unsigned char);
void FDDRead(unsigned char);

void FDDWriteINT1Eh(unsigned char*);

#endif
