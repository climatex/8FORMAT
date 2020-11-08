#ifndef _8FORMAT_H_
#define _8FORMAT_H_

extern unsigned int  nFDCBase;
extern unsigned char nUseFM;
extern unsigned char nFormatWithVerify;
extern unsigned char nQuickFormat;
extern unsigned char nNoCreateFilesystem;
extern unsigned char nDriveNumber;
extern unsigned char nTracks;
extern unsigned char nHeads;
extern unsigned int  nPhysicalSectorSize;
extern unsigned int  nLogicalSectorSize;
extern unsigned char nSectorsPerTrack;
extern unsigned char nLogicalSectorsPerTrack;
extern unsigned char nCustomGapLength;
extern unsigned char nCustomGap3Length;
extern unsigned char nOnlyReprogramBIOS;
extern unsigned char nOnlyRecalibrateFDD;
extern unsigned char nUseIRQ;
extern unsigned char nUseDMA;
extern unsigned int  nDataRateKbps;
extern unsigned char sLaunch8TSR[255];
extern unsigned char sFormatType[5];
extern unsigned char nFormatByte;

void Quit(int);
unsigned char IsPCXT();
void DelLine();
void PrintSplash();
void ParseCommandLine(int, char*[]);
void DoOperations();

#endif