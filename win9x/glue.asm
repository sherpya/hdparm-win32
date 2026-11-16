; DDB layout/build glue adapted from WINSIMD, see SOURCES.md.
; SPDX-License-Identifier: GPL-3.0-only
bits 32
section .text
    times 16 db 0cch
extern _vxd_init, _vxd_exit, _vxd_ioctl, _ios_completed

global _ControlProc
_ControlProc:
    cmp eax, 23h                    ; W32_DEVICEIOCONTROL, ESI -> DIOCParams
    je .ioctl
    cmp eax, 1bh                    ; SYS_DYNAMIC_DEVICE_INIT
    je .init
    cmp eax, 1ch                    ; SYS_DYNAMIC_DEVICE_EXIT
    je .exit
    clc
    ret
.init:
    pushad
    cld
    call _vxd_init
    test eax, eax
    popad
    jnz .failed
    clc
    ret
.exit:
    pushad
    cld
    call _vxd_exit
    test eax, eax
    popad
    jnz .failed
    clc
    ret
.failed:
    stc
    ret
.ioctl:
    push ebx
    push ecx
    push edx
    push esi
    push edi
    push ebp
    cld
    push esi
    call _vxd_ioctl
    add esp, 4
    pop ebp
    pop edi
    pop esi
    pop edx
    pop ecx
    pop ebx
    clc
    ret

global _ios_register
_ios_register:
    push dword [esp+4]
    int 20h
    dd 00100007h                    ; IOS_Register(DRP *)
    add esp, 4
    ret

global _ios_deregister
_ios_deregister:
    push esi
    mov esi, [esp+8]
    int 20h
    dd 00100011h                    ; IOS_deregister, ESI = DVT
    pop esi
    ret

global _ios_send
_ios_send:
    push esi
    push edi
    push ebx
    mov esi, [esp+16]               ; IOS allocated IOR
    mov edi, [esp+20]               ; BDD of physical DCB
    mov ebx, edi                    ; also required by IDE passthrough
    int 20h
    dd 00100004h                    ; IOS_SendCommand
    pop ebx
    pop edi
    pop esi
    ret

global _ios_callback
_ios_callback:
    mov dword [_ios_completed], 1
    ret 4                           ; IOS pushes IOR_req_req_handle

; C wrappers for VMM stack-based services. Preserve their argument layout.
%macro cservice3 2
 global %1
 %1:
    push dword [esp+12]
    push dword [esp+12]
    push dword [esp+12]
    int 20h
    dd %2
    add esp, 12
    ret
%endmacro
cservice3 _page_check, 00010067h
cservice3 _page_lock, 00010063h
cservice3 _page_unlock, 00010064h

global _gate_claim
_gate_claim:
    mov edx, [esp+4]
    mov eax, 1
    xchg eax, [edx]
    ret

section .data
align 16
global _HDPARM_DDB
_HDPARM_DDB:
    dd 0
    dw 0400h, 0
    db 1, 0
    dw 0
    db 'HDPARM  '
    dd 80000000h
    dd _ControlProc
    times 8 dd 0
    db 'verP'
    dd 50h
    db '1vsR', '2vsR', '3vsR'
