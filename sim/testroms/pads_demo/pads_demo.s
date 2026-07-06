; pads_demo — M5 input test cart / hardware pad checker.
;
; Every frame: read both pads through both select phases (a $2009 read first
; cross-resets pad 1's select FF so phases are deterministic), decode the 8
; buttons per pad, and draw two rows of 10x10 indicator blocks (pad 1 at
; y=48, pad 2 at y=80; order Right, Left, Down, Up, A, B, C, Start), bright
; when pressed. Drawing is direct CPU framebuffer writes on page 0.
;
; The sim integration test injects scripted input and checks block colors;
; on hardware this is the manual pad check (bootcart). M6 addition: a saw
; tone (~424 Hz) plays from the audio coprocessor while any button is held —
; the audible check for the ACP path.

.setcpu "65C02"

BANKING = $2005
DMACTL  = $2007

NMICNT  = $00
LASTNMI = $01
P1A     = $02           ; pad1 select-LOW byte
P1B     = $03           ; pad1 select-HIGH byte
P2A     = $04
P2B     = $05
PTRLO   = $06
PTRHI   = $07
PRESS   = $08           ; decoded pressed bits (R,L,D,U,A,B,C,Start = 0..7)
ROWS    = $09
COLOR   = $0A
BLKS    = $0B           ; block counter

BRIGHT  = $1D
DIM     = $03

.segment "CODE"

reset:
        sei
        cld
        ldx #$FF
        txs
        stz BANKING
        stz NMICNT
        stz LASTNMI
        lda #$24        ; CPU_TO_VRAM | VSYNC_NMI, page 0 displayed
        sta DMACTL

        ; ---- upload ACP saw firmware + vectors, hold it stopped ----------
        ldx #$00
fwcp:   lda acpfw,x
        sta $3000,x
        inx
        cpx #$0D        ; firmware length (13 bytes)
        bne fwcp
        stz $3FFB       ; vector high bytes all $00
        stz $3FFD
        stz $3FFF
        lda #$05
        sta $3FFA       ; NMI -> irq handler (harmless)
        sta $3FFE       ; IRQ -> $0005
        stz $3FFC       ; RESET -> $0000
        sta $2000       ; ACP reset request
        lda #$10        ; rate value, run bit clear = silent
        sta $2006
        cli

frame:
        lda $2009       ; cross-reset pad1's select FF
        lda $2008
        sta P1A
        lda $2008
        sta P1B
        lda $2009       ; (pad1 reads cross-reset pad2 -> select LOW first)
        sta P2A
        lda $2009
        sta P2B

        ; ---- decode pad 1 into PRESS ------------------------------------
        lda P1B
        eor #$FF
        and #$0F        ; Right,Left,Down,Up
        sta PRESS
        lda P1A
        eor #$FF
        and #$10        ; A -> bit 4
        ora PRESS
        sta PRESS
        lda P1B
        eor #$FF
        and #$10        ; B: bit4 -> bit 5
        asl a
        ora PRESS
        sta PRESS
        lda P1B
        eor #$FF
        and #$20        ; C: bit5 -> bit 6
        asl a
        ora PRESS
        sta PRESS
        lda P1A
        eor #$FF
        and #$20        ; Start: bit5 -> bit 7
        asl a
        asl a
        ora PRESS
        sta PRESS

        lda #$58        ; row base $5808 = $4000 + 48*128 + 8
        sta PTRHI
        jsr drawrow

        ; ---- decode pad 2 ------------------------------------------------
        lda P2B
        eor #$FF
        and #$0F
        sta PRESS
        lda P2A
        eor #$FF
        and #$10
        ora PRESS
        sta PRESS
        lda P2B
        eor #$FF
        and #$10
        asl a
        ora PRESS
        sta PRESS
        lda P2B
        eor #$FF
        and #$20
        asl a
        ora PRESS
        sta PRESS
        lda P2A
        eor #$FF
        and #$20
        asl a
        asl a
        ora PRESS
        sta PRESS

        lda #$68        ; row base $6808 = $4000 + 80*128 + 8
        sta PTRHI
        jsr drawrow

        ; ---- tone while any button held -----------------------------------
        lda P1A
        and P1B
        and P2A
        and P2B
        and #$7F        ; ignore select bits; pressed = any 0 among bits 0-6
        eor #$7F
        beq quiet
        lda #$90        ; run | value $10 -> ~424 Hz saw
        bne setrt
quiet:  lda #$10        ; run bit clear: ACP frozen (silent)
setrt:  sta $2006

wnmi:   .byte $CB       ; WAI
        lda NMICNT
        cmp LASTNMI
        beq wnmi
        sta LASTNMI
        jmp frame

acpfw:  .byte $58, $CB, $4C, $01, $00                   ; CLI; loop: WAI; JMP
        .byte $E6, $20, $A5, $20, $8D, $00, $80, $40    ; irq: saw -> DAC

; ---- draw 8 blocks; PTRHI = page-aligned-ish base high byte --------------
drawrow:
        lda #$08
        sta PTRLO
        stz BLKS
drb:    lsr PRESS       ; carry = this button pressed
        lda #DIM
        bcc drc
        lda #BRIGHT
drc:    sta COLOR
        lda #$0A        ; 10 rows
        sta ROWS
        ; save row base in x/y (PTRLO/PTRHI advance down the block)
        ldx PTRLO
        ldy PTRHI
drr:    phy
        ldy #$09
        lda COLOR
drp:    sta (PTRLO),y
        dey
        bpl drp
        ply
        lda PTRLO       ; next scan row
        clc
        adc #$80
        sta PTRLO
        bcc drnc
        inc PTRHI
drnc:   dec ROWS
        bne drr
        ; restore base, step x by 14
        stx PTRLO
        sty PTRHI
        lda PTRLO
        clc
        adc #$0E
        sta PTRLO
        inc BLKS
        lda BLKS
        cmp #$08
        bne drb
        rts

nmi:    inc NMICNT
        rti
irq:    rti

.segment "VECTORS"
        .word nmi
        .word reset
        .word irq
