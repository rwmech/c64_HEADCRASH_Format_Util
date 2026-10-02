; drivecode_1541.s - track gate and busy signal for the 1541 ROM formatter.
;
; Two jobs, both of them small.
;
; 1. Format one track instead of the whole disk.
;
; The 1541 has no "format one track" job, which is the whole problem: a
; format gives no progress and no way to retry a single bad track. Its N:
; command runs the
; ROM formatter as job $E0 in buffer 3, whose execute address is $0600, and
; the formatter is a state machine: each pass does one step and returns to
; the controller with JMP $F99C, so the controller re-enters it at $0600
; over and over until the whole disk is done. That re-entry is the hook.
; The gate ends the job as soon as the formatter has moved past the track we
; asked for, which turns "format the disk" into "format one track" without
; touching the GCR code at all.
;
; Per track the host sets FTNUM ($51) to the track to do, ENDTRK (below) to
; the track after it, and the ROM's retry counter at $0620 to 1, so a bad
; track comes straight back as a job error instead of being retried out of
; sight.
;
; The gate deliberately touches nothing but the formatter's own state. An
; earlier version raised a busy flag on the serial CLK line from in here so
; the host could tell when the drive was deaf, and it wedged the drive: the
; serial port shares VIA 1 port B with the drive's own bus code, and a
; read-modify-write from inside the controller interrupt lands on top of
; whatever that code was in the middle of. The host waits the clock out
; instead. See docs/DESIGN.md.
;
; Assembled to a flat binary at $0600 by tools/build_drivecode.py.
;
; (C) 2026 Robert Mech. Licence MIT.

FTNUM   = $51               ; ROM: track the formatter is working on
WRTMOD  = $50               ; ROM: non-zero means "a write is in progress"
ROM_END = $F969             ; ROM: end current job, A = status code
ROM_FMT = $FAC7             ; ROM: formatter state machine entry

        .segment "CODE"

entry:  lda FTNUM
        bmi run             ; $FF means "not started": let the ROM init run
        cmp endtrk
        bcc run             ; still on or before our track: keep going

        lda #$ff            ; past it: this track is done. Leave the
        sta FTNUM           ; formatter's state exactly as the ROM's own
        lda #$00            ; exit at $FD96 leaves it: the job exit runs a
        sta WRTMOD          ; GCR decode over drive RAM, stack included,
                            ; unless WRTMOD says the write is finished.
        lda #$01
        jmp ROM_END

run:    jmp ROM_FMT

endtrk: .byte $00           ; patched by the host before each track
