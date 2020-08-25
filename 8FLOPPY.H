#ifndef _8FLOPPY_H_
#define _8FLOPPY_H_

unsigned char ConvertSectorSize(unsigned int);
void RemoveISR();

void FDDHeadRetract();

void FDDReset();
void FDDResetBIOS();

void FDDFormat();
void FDDWrite(unsigned char);

#endif
