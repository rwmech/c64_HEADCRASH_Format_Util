; watchdog.s - a way out of a KERNAL serial call that is never coming back.
;
; The problem this exists to solve is in docs/DESIGN.md: a 1541 writing a
; track answers nothing on the serial bus, but acknowledges ATN in hardware,
; so the KERNAL's byte sender waits at $ED5A for a listener that is not
; listening, with no timeout. The machine hangs.
;
; Version 1 dealt with that by never asking: wait out a measured worst case
; per track and only then speak to the drive. Safe, and slow, because the
; worst case has to be set well above the typical one and every track pays
; it whether it needed it or not.
;
; This is the better answer. Arm a timer on CIA 2 to raise NMI, make the
; call, and if the timer fires first, come back out of it. NMI is used
; rather than IRQ because the KERNAL's serial routines run with interrupts
; masked, and NMI does not care.
;
; wd_arm() behaves like setjmp: it returns 0 when it is called, and appears
; to return 1 a second time if the watchdog fires. Both the 6502 stack
; pointer and cc65's software stack pointer are put back, so the C code that
; called it is intact either way.
;
;   if (wd_arm(ticks)) {        timed out, the drive is still busy
;   } else {
;       ... KERNAL calls ...
;       wd_disarm();
;   }
;
; (C) 2026 Robert Mech. Licence GPL-3.0-or-later.

        .export _wd_arm, _wd_disarm, _wd_release_bus
        .importzp sp                    ; cc65 software stack pointer
        .importzp sreg

CIA2_PRA = $dd00
CIA2_TAL = $dd04
CIA2_TAH = $dd05
CIA2_ICR = $dd0d
CIA2_CRA = $dd0e
NMIVEC   = $0318                ; KERNAL dispatches NMI through here

        .bss
have_nmi:   .res 1              ; set once the old NMI vector is saved
slices:     .res 1              ; 50 ms slices left before giving up
zpcopy:     .res 26             ; cc65's zero page scratch, $02..$1b
saved_sp:   .res 1              ; 6502 stack pointer at the arm point
saved_csp:  .res 2              ; cc65 software stack pointer
ret_addr:   .res 2              ; where wd_arm was called from
old_nmi:    .res 2
armed:      .res 1

        .code

; ----------------------------------------------------------------------
; unsigned char __fastcall__ wd_arm (unsigned char slices);
;
; Each slice is 50 ms. A CIA timer is sixteen bits, so 65 ms is all one
; pass can measure, and a normal serial round trip is already tens of
; milliseconds on a standard bus: the timer free runs and the handler
; counts underflows instead, which puts the timeout where it belongs
; without pretending the bus is faster than it is.
; Returns 0 on the way in, 1 if the watchdog fires.
; ----------------------------------------------------------------------
TICKS   = 50000

_wd_arm:
        sta slices              ; slice count arrives in A under fastcall
        lda #<TICKS
        sta CIA2_TAL
        lda #>TICKS
        sta CIA2_TAH

        ; Remember where to come back to. The return address is on the
        ; stack; leave it there and copy it, so a normal return still works.
        tsx
        stx saved_sp
        lda $0101,x
        sta ret_addr
        lda $0102,x
        sta ret_addr+1

        lda sp
        sta saved_csp
        lda sp+1
        sta saved_csp+1

        ; cc65 keeps its working registers in zero page and saves them on
        ; the hardware stack across calls. Walking out of a call tree
        ; throws those saves away, so take a copy and put it back on the
        ; way out; without this the C code resumes with pointers full of
        ; whatever the abandoned routine was using.
        ldx #25
:       lda $02,x
        sta zpcopy,x
        dex
        bpl :-

        ; Take over NMI.
        lda NMIVEC
        sta old_nmi
        lda NMIVEC+1
        sta old_nmi+1
        lda #<nmi_handler
        sta NMIVEC
        lda #>nmi_handler
        sta NMIVEC+1
        lda #$01
        sta have_nmi

        lda #$01
        sta armed

        ; One shot, load the latch and start counting.
        lda #$81                ; enable timer A interrupt -> NMI
        sta CIA2_ICR
        lda #$11                ; load latch, continuous, start
        sta CIA2_CRA

        lda #$00                ; first return
        ldx #$00
        rts

; ----------------------------------------------------------------------
; void wd_disarm (void);
; ----------------------------------------------------------------------
_wd_disarm:
        lda #$00
        sta armed
        lda #$01                ; stop timer A
        sta CIA2_CRA
        lda #$01                ; disable its interrupt
        sta CIA2_ICR
        bit CIA2_ICR            ; acknowledge anything pending
        lda have_nmi
        beq :+
        lda old_nmi
        sta NMIVEC
        lda old_nmi+1
        sta NMIVEC+1
:       rts

; ----------------------------------------------------------------------
; The handler. If this is our watchdog, unwind to the arm point; if it is
; anything else (RESTORE, say), hand it to whoever had the vector.
; ----------------------------------------------------------------------
nmi_handler:
        pha
        lda CIA2_ICR            ; reading clears the flag; bit 0 = timer A
        and #$01
        beq not_ours
        lda armed
        beq not_ours

        dec slices              ; not out of patience yet?
        beq wd_fire
        pla
        rti

wd_fire:
        ; Ours, and the time is up. Stop the timer and put the vector back before unwinding,
        ; because the unwind does not return through here.
        lda #$01
        sta CIA2_CRA
        lda #$01
        sta CIA2_ICR
        lda #$00
        sta armed
        lda old_nmi
        sta NMIVEC
        lda old_nmi+1
        sta NMIVEC+1

        jsr _wd_release_bus

        ldx #25
:       lda zpcopy,x
        sta $02,x
        dex
        bpl :-

        ; Unwind: the 6502 stack back to where wd_arm was called, cc65's
        ; stack with it, then return to the caller with 1 in A.
        ldx saved_sp
        txs
        lda saved_csp
        sta sp
        lda saved_csp+1
        sta sp+1

        lda ret_addr+1
        pha
        lda ret_addr
        pha
        cli                     ; the KERNAL call we abandoned had masked
                                ; interrupts; normal code wants them back
        lda #$01
        ldx #$00
        rts                     ; "returns" from wd_arm a second time

not_ours:
        pla
        jmp (old_nmi)

; ----------------------------------------------------------------------
; void wd_release_bus (void);
;
; Let go of ATN, CLK and DATA by hand. The KERNAL was abandoned part way
; through a transfer, so its own tidy up routines are not safe to call;
; these three lines are all it was holding.
; ----------------------------------------------------------------------
_wd_release_bus:
        lda CIA2_PRA
        and #$c7                ; clear ATN out, CLK out, DATA out
        sta CIA2_PRA
        lda #$00
        sta $0090               ; KERNAL status
        sta $0094               ; deferred character flag
        sta $00a3
        rts
