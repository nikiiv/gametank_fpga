; fb_pattern — M3 video test cart.
;
; Fills framebuffer page 0 with pixel(x,y) = x XOR y via direct CPU writes
; through the $4000 VDMA window (CPU_TO_VRAM=1), then enables the vsync NMI
; and parks in a WAI loop counting frames at $10.
;
; The Verilator integration test captures the scanned-out frame and compares
; it pixel-exactly against the same pattern pushed through the capture-based
; palette.

.setcpu "W65C02"

BANKING = $2005
DMACTL  = $2007

.segment "CODE"

reset:
        sei
        cld
        ldx #$FF
        txs

        lda #$20        ; CPU_TO_VRAM=1, COPY_ENABLE=0, page flags 0
        sta DMACTL

        ; fill loop: $00/$01 = row pointer into the window, $02 = row
        lda #$00
        sta $00
        sta $02
        lda #$40
        sta $01
rowloop:
        ldy #$00
pxloop: tya
        eor $02
        sta ($00),y
        iny
        bpl pxloop      ; runs y = 0..127

        lda $00         ; row pointer += 128
        clc
        adc #$80
        sta $00
        bcc nocarry
        inc $01
nocarry:
        inc $02
        lda $02
        cmp #$80
        bne rowloop

        lda #$00        ; frame counter
        sta $10
        lda #$24        ; VSYNC_NMI=1 (bit 2), CPU_TO_VRAM=1 (bit 5)
        sta DMACTL

mainloop:
        wai
        inc $10
        jmp mainloop

nmi:
irq:    rti

.segment "VECTORS"
        .word nmi       ; $FFFA
        .word reset     ; $FFFC
        .word irq       ; $FFFE
