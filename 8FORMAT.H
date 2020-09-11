#ifndef _8FORMAT_H_
#define _8FORMAT_H_

extern unsigned int  nFDCBase;
extern unsigned char nUseFM;
extern unsigned char nQuickFormat;
extern unsigned char nForceSingleSided;
extern unsigned char nNoCreateFilesystem;
extern unsigned char nDriveNumber;
extern unsigned char nTracks;
extern unsigned char nHeads;
extern unsigned char nDoubleDensity;
extern unsigned int  nSectorSize;
extern unsigned char nSectorsPerTrack;
extern unsigned char nCustomGapLength;
extern unsigned char nCustomGap3Length;
extern unsigned char nOnlyReprogramBIOS;
extern unsigned char nUseIRQ;
extern unsigned char nUseDMA;
extern unsigned int  nDataRateKbps;

void Quit(int);
unsigned char IsPCXT();
void DelLine();
void PrintSplash();
void ParseCommandLine(int, char*[]);
void DoOperations();

#endif