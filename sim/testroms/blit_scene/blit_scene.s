; blit_scene — M4 blitter lockstep cart.
;
; Every frame, on the back framebuffer page: clear via four 64x64 colorfill
; blits (fill value derived from the frame counter), draw a 16x16 GRAM sprite
; (transparency ON, writes all), the same sprite X-mirrored with transparency
; OFF (skips its $00 pixel), positions animated by the frame counter; then a
; 4-byte tag signature at (0..3, 0) via the CPU window, flip VID_OUT_PAGE,
; and wait for vsync NMI. Blit completion is consumed via the blitter IRQ
; (handler clears TRIGGER and flags done).
;
; GRAM quadrant 0 is selected with a dummy 1x1 blit before loading the sprite
; through the window (gram_mid_bits — see HARDWARE.md §Blitter).
;
; Deterministic per TESTING.md lockstep rules: every register and every
; consumed memory cell is initialized; every displayed pixel is drawn.

.setcpu "65C02"

BANKING = $2005
DMACTL  = $2007

; zero page
NMICNT  = $00           ; incremented by NMI handler
BLITDONE = $01          ; set by IRQ handler
FRAME   = $02           ; current frame number
PAGE    = $03           ; FRAME & 1
DISPBIT = $04           ; VID_OUT_PAGE bit value for the *old* (displayed) page
PTRLO   = $06
PTRHI   = $07
LASTNMI = $08
TMP     = $09
SPRROW  = $0A

; DMACTL bit values
;   $01 COPY_ENABLE, $04 VSYNC_NMI, $08 COLORFILL, $40 COPY_IRQ, $80 TRANSPARENCY

.segment "CODE"

reset:
        sei
        cld
        ldx #$FF
        txs

        stz BANKING             ; deterministic banking (power-on undefined)
        stz NMICNT
        stz BLITDONE
        stz FRAME
        stz LASTNMI
        stz SPRROW

        ; ---- select GRAM quadrant 0 with a dummy 1x1 blit ----------------
        lda #$C1                ; COPY_ENABLE|COPY_IRQ|TRANSPARENCY
        sta DMACTL
        stz $4000               ; VX
        stz $4001               ; VY
        stz $4002               ; GX  (high bits 0 -> quadrant 0)
        stz $4003               ; GY
        lda #$01
        sta $4004               ; W = 1
        sta $4005               ; H = 1
        sta $4006               ; TRIGGER
        ldx #$10                ; a 1x1 blit is long done after 16 spins
dw:     dex
        bne dw
        stz $4006               ; clear pending IRQ before enabling CLI use

        ; ---- load 16x16 sprite into GRAM (0,0)..(15,15) -------------------
        ; sprite[y][x] = (y<<4)|x  — includes a $00 pixel at (0,0)
        stz DMACTL              ; window -> GRAM (CPU_TO_VRAM=0, COPY_ENABLE=0)
        stz PTRLO
        lda #$40
        sta PTRHI
        stz TMP                 ; TMP = y
gsrow:  ldy #$00
gspx:   tya
        ora SPRROW              ; (y<<4) | x
        sta (PTRLO),y
        iny
        cpy #$10
        bne gspx
        lda PTRLO               ; ptr += 128
        clc
        adc #$80
        sta PTRLO
        bcc gsnc
        inc PTRHI
gsnc:   lda SPRROW              ; row value += $10
        clc
        adc #$10
        sta SPRROW
        inc TMP
        lda TMP
        cmp #$10
        bne gsrow

        cli                     ; blitter IRQ -> handler

; ---- main loop --------------------------------------------------------
mainloop:
        inc FRAME
        lda FRAME
        and #$01
        sta PAGE
        eor #$01
        asl a
        sta DISPBIT             ; keep displaying the old page while drawing

        lda PAGE                ; banking[3] = back page (CPU + blit dest)
        asl a
        asl a
        asl a
        sta BANKING

        ; ---- clear back page: four 64x64 colorfill blits ------------------
        lda #$CD                ; COPY|NMI|COLORFILL|IRQ|TRANSP
        ora DISPBIT
        sta DMACTL
        lda FRAME
        sta $4007               ; COLOR (hardware fills with ~COLOR)
        ldx #$00                ; quadrant index 0..3
clq:    txa
        and #$01                ; VX = (i&1)*64
        beq clx0
        lda #$40
clx0:   sta $4000
        txa
        and #$02                ; VY = (i>>1)*64
        beq cly0
        lda #$40
        bne cly1
cly0:   lda #$00
cly1:   sta $4001
        stz $4002
        stz $4003
        lda #$40                ; W = H = 64
        sta $4004
        sta $4005
        lda #$01
        sta $4006               ; TRIGGER
        jsr blitwait
        inx
        cpx #$04
        bne clq

        ; ---- sprite blit, transparency ON (writes all pixels) -------------
        lda #$C5                ; COPY|NMI|IRQ|TRANSP
        ora DISPBIT
        sta DMACTL
        lda FRAME
        and #$07
        clc
        adc #$14                ; VX = 20 + (f&7)
        sta $4000
        lda #$28                ; VY = 40
        sta $4001
        stz $4002
        stz $4003
        lda #$10
        sta $4004               ; W = 16
        sta $4005               ; H = 16
        lda #$01
        sta $4006
        jsr blitwait

        ; ---- sprite blit, X-mirrored, transparency OFF ($00 px skipped) ----
        lda #$45                ; COPY|NMI|IRQ (no TRANSP bit)
        ora DISPBIT
        sta DMACTL
        lda #$50                ; VX = 80
        sta $4000
        lda FRAME
        and #$03
        clc
        adc #$28                ; VY = 40 + (f&3)
        sta $4001
        lda #$F0                ; mirrored source: GX = ~(rightmost column)
        sta $4002               ; = ~15 for the sprite at columns 0..15
        stz $4003
        lda #$90                ; W = 16 | mirror
        sta $4004
        lda #$10
        sta $4005
        lda #$01
        sta $4006
        jsr blitwait

        ; ---- tag signature via CPU window ---------------------------------
        lda #$24                ; CPU_TO_VRAM|VSYNC_NMI, COPY off
        ora DISPBIT
        sta DMACTL
        lda FRAME
        sta $4000               ; (0,0) = f
        eor #$A5
        sta $4001               ; (1,0) = f^$A5
        lda #$5A
        sta $4002
        lda #$C3
        sta $4003

        ; ---- flip: display the page we just drew ---------------------------
        lda PAGE
        asl a
        ora #$24
        sta DMACTL

wnmi:   .byte $CB       ; WAI (WDC-only; old ca65 lacks the mnemonic)
        lda NMICNT
        cmp LASTNMI
        beq wnmi
        sta LASTNMI
        jmp mainloop

; ---- helpers -----------------------------------------------------------
blitwait:
bwl:    lda BLITDONE
        beq bwl
        stz BLITDONE
        rts

nmi:    inc NMICNT
        rti

irq:    pha
        stz $4006               ; TRIGGER write clears the pending IRQ
        inc BLITDONE
        pla
        rti

.segment "VECTORS"
        .word nmi
        .word reset
        .word irq
