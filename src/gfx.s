; gfx.s - the bitmap primitives that get called enough to matter.
;
; The first version of this was all C, and it showed: a full clear is 9000
; stores, a line of text is eight stores per character plus the address
; arithmetic, and the progress bar redraws a few hundred bytes every track.
; Through cc65's code generator that adds up to visible redraw lag on a 1 MHz
; machine. The arithmetic is the same here, just done where a byte is a byte.
;
; Parameters come in through the globals below rather than the C stack, which
; keeps the call sequence to a handful of stores and avoids cc65's parameter
; pushing on every pixel.
;
; Screen layout, fixed here and in ui.c: VIC bank 1, bitmap at $6000, screen
; matrix at $5c00, a copy of the character set at $5400. All of it sits at
; the top of the bank so the program below it has room to grow.
;
; (C) 2026 Robert Mech. Licence MIT.

        .export _gfx_x, _gfx_x2, _gfx_y, _gfx_col, _gfx_ch
        .export _gfx_clear, _gfx_plot, _gfx_hline, _gfx_glyph
        .export _gfx_inv
        .export _gfx_barfill

BITMAP  = $6000
SCREEN  = $5c00
CHARSET = $c000        ; glyph source only, not read by the VIC

; cc65 has already filled its own zero page, so these use the four bytes
; at $fb that nothing in the KERNAL or BASIC holds across a call. Drawing
; never overlaps a serial call, so there is no one else to collide with.
ptr     = $fb                   ; bitmap address being written
cptr    = $fd                   ; screen matrix address being written

        .bss
cnt:    .res 1
cellx:  .res 1
srcp:   .res 2
_gfx_x:   .res 2                ; 0..319
_gfx_x2:  .res 2                ; right hand end for hline, inclusive
_gfx_y:   .res 1                ; 0..199
_gfx_col: .res 1                ; screen matrix byte: ink in the high nibble
_gfx_ch:  .res 1                ; glyph number, 0..255
_gfx_inv: .res 1                ; $00 normal, $ff reverse video

        .rodata
; Start of each cell row within the bitmap, which is 320 bytes per row.
rowlo:  .repeat 25, i
        .byte <(i * 320)
        .endrepeat
rowhi:  .repeat 25, i
        .byte >(i * 320)
        .endrepeat
; Start of each cell row within the screen matrix, 40 bytes per row.
scrlo:  .repeat 25, i
        .byte <(i * 40)
        .endrepeat
scrhi:  .repeat 25, i
        .byte >(i * 40)
        .endrepeat
; Bit within a byte for each x position inside a cell.
bitmask:.byte $80, $40, $20, $10, $08, $04, $02, $01
; leftrun[n] is the n leftmost bits of a cell, so bits a..b inclusive are
; leftrun[b+1] AND NOT leftrun[a]. Nine entries, because b+1 can be 8.
leftrun:.byte $00, $80, $c0, $e0, $f0, $f8, $fc, $fe, $ff

        .code

; ----------------------------------------------------------------------
; Work out the bitmap and screen addresses for (_gfx_x, _gfx_y).
; Leaves ptr at the byte holding the pixel, cptr at its matrix cell, and
; the bit number within the byte in X.
; ----------------------------------------------------------------------
addr:
        lda _gfx_y
        lsr
        lsr
        lsr                     ; cell row
        tay
        lda _gfx_y
        and #$07                ; line within the cell
        clc
        adc rowlo,y
        sta ptr
        lda rowhi,y
        adc #$00
        sta ptr+1

        lda scrlo,y
        sta cptr
        lda scrhi,y
        adc #>SCREEN
        sta cptr+1

        ; x contributes (x & $fff8) to the bitmap address and (x >> 3) to
        ; the matrix address.
        lda _gfx_x
        and #$f8
        clc
        adc ptr
        sta ptr
        lda _gfx_x+1
        adc ptr+1
        adc #>BITMAP
        sta ptr+1

        lda _gfx_x+1
        lsr                     ; high bit of x into carry
        lda _gfx_x
        ror
        lsr
        lsr                     ; x >> 3
        clc
        adc cptr
        sta cptr
        bcc :+
        inc cptr+1
:
        lda _gfx_x
        and #$07
        tax
        rts

; ----------------------------------------------------------------------
; void gfx_plot (void) - set one pixel and colour its cell.
; ----------------------------------------------------------------------
_gfx_plot:
        jsr addr
        ldy #$00
        lda _gfx_col
        sta (cptr),y
        lda bitmask,x
        ora (ptr),y
        sta (ptr),y
        rts

; ----------------------------------------------------------------------
; void gfx_hline (void) - _gfx_x to _gfx_x2 inclusive on row _gfx_y.
;
; Whole cells in the middle are a single store each rather than eight
; shifts, which is most of the win on long runs like the progress bar.
; ----------------------------------------------------------------------
_gfx_hline:
        jsr addr
        ldy #$00

        ; Number of cells spanned: (x2 >> 3) - (x >> 3).
        lda _gfx_x2+1
        lsr
        lda _gfx_x2
        ror
        lsr
        lsr
        sta cnt                 ; x2 cell
        lda _gfx_x+1
        lsr
        lda _gfx_x
        ror
        lsr
        lsr
        sta cellx               ; x cell
        lda cnt
        sec
        sbc cellx
        sta cnt                 ; cells to cross

        lda _gfx_col
        sta (cptr),y

        lda cnt
        bne hl_multi

        ; Both ends inside one cell: bits from x to x2.
        ldy _gfx_x2
        tya
        and #$07
        tay
        iny
        lda leftrun,y           ; bits 0..x2
        sta cnt
        lda leftrun,x           ; bits left of x
        eor #$ff
        and cnt                 ; bits x..x2
        ldy #$00
        ora (ptr),y
        sta (ptr),y
        rts

hl_multi:
        ; First cell: from x to its right hand edge.
        lda leftrun,x
        eor #$ff
        ldy #$00
        ora (ptr),y
        sta (ptr),y

hl_next:
        ; Step one cell right: eight bytes on, one matrix byte on.
        lda ptr
        clc
        adc #$08
        sta ptr
        bcc :+
        inc ptr+1
:
        inc cptr
        bne :+
        inc cptr+1
:
        lda _gfx_col
        sta (cptr),y

        dec cnt
        beq hl_last

        lda #$ff                ; a whole cell of pixels
        sta (ptr),y
        jmp hl_next

hl_last:
        lda _gfx_x2
        and #$07
        tax
        inx
        lda leftrun,x           ; bits 0..x2
        ldy #$00
        ora (ptr),y
        sta (ptr),y
        rts

; ----------------------------------------------------------------------
; void gfx_glyph (void) - blit one 8x8 character set glyph.
; _gfx_x and _gfx_y are expected to be cell aligned.
; ----------------------------------------------------------------------
_gfx_glyph:
        jsr addr

        lda #$00
        sta srcp+1
        lda _gfx_ch
        asl
        rol srcp+1
        asl
        rol srcp+1
        asl
        rol srcp+1              ; glyph * 8, carry collected as we go
        sta glyph_src+1
        lda srcp+1
        clc
        adc #>CHARSET
        sta glyph_src+2

        ldy #$00
        lda _gfx_col
        sta (cptr),y

        ldy #$07
glyph_src:
:       lda $ffff,y             ; operand written just above
        eor _gfx_inv            ; $ff turns the cell into a reverse video
        sta (ptr),y             ; block with the letter knocked out of it
        dey
        bpl :-
        rts

; ----------------------------------------------------------------------
; void gfx_clear (void) - blank the bitmap, flood the matrix with _gfx_col.
; ----------------------------------------------------------------------
_gfx_clear:
        lda #<BITMAP
        sta ptr
        lda #>BITMAP
        sta ptr+1
        ldx #$20                ; 32 pages
        lda #$00
        ldy #$00
clr_page:
        sta (ptr),y
        iny
        bne clr_page
        inc ptr+1
        dex
        bne clr_page

        lda #<SCREEN
        sta ptr
        lda #>SCREEN
        sta ptr+1
        lda _gfx_col
        ldx #$04
        ldy #$00
clr_scr:
        sta (ptr),y
        iny
        bne clr_scr
        inc ptr+1
        dex
        bne clr_scr
        rts

; ----------------------------------------------------------------------
; void gfx_barfill (void) - fill whole cells from _gfx_x to _gfx_x2 on the
; cell row holding _gfx_y, eight pixel rows at a time.
;
; The progress bar is on a cell row of its own precisely so that filling it
; is this: one store per byte, no masks, no read and merge.
; ----------------------------------------------------------------------
_gfx_barfill:
        jsr addr
        lda _gfx_x2+1
        lsr
        lda _gfx_x2
        ror
        lsr
        lsr
        sta cnt                 ; last cell
        lda _gfx_x+1
        lsr
        lda _gfx_x
        ror
        lsr
        lsr
        sta cellx
        lda cnt
        sec
        sbc cellx
        tax                     ; cells to do, minus one
        inx

bar_cell:
        ldy #$00
        lda _gfx_col
        sta (cptr),y
        lda #$ff
        ldy #$07
:       sta (ptr),y
        dey
        bpl :-

        lda ptr
        clc
        adc #$08
        sta ptr
        bcc :+
        inc ptr+1
:
        inc cptr
        bne :+
        inc cptr+1
:
        dex
        bne bar_cell
        rts
