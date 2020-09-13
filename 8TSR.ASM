.8086
.model small
.code

public _DiskInterrupt

extrn _NTRACKS:byte:far
extrn _NNEWEOT:byte:far
extrn _NHEADS:byte:far

; Overriden AH=8 (Get drive parameters),
;              17h (Set disk type for format),
;              18h (Set media type for format)
; Only for floppies (DL=0 to 1 / 0 to 3)
_DiskInterrupt proc far
  cli
  pushf
  cmp dl,3
  ja back
  cmp ah,8
  je get_drive_parameters
  cmp ah,17h
  je set_disk_format
  cmp ah,18h
  je set_media_format
  
; Call the old INT 13h handler if not any of these/not our drive
back:
  popf
  sti
  int 0EAh
  
; Update FLAGS in the stack unwind and return back to caller
back2:
  push bp
  mov bp,sp
  push ax
  lahf
  mov [bp+6],ah
  pop ax
  pop bp
  sti
  iret

; Fills CX and DX with tracks, sectors and sector size information
; New DPT in ES:DI
get_drive_parameters:
  mov ch,[_NTRACKS]
  mov cl,[_NNEWEOT]
  mov dh,[_NHEADS]
  mov dl,1
  xor ax,ax
  xor bx,bx
  mov es,ax
  les di,[es:78h]
  popf
  clc
  jmp back2
  
; Does not do anything, just returns success
set_disk_format:
  mov ah,0
  popf
  clc
  jmp back2
  
; Only return success and the new DPT in ES:DI
set_media_format:
  xor ax,ax
  mov es,ax
  les di,[es:78h]
  popf
  clc
  jmp back2
_DiskInterrupt endp

END