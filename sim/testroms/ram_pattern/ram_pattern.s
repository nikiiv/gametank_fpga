; ram_pattern — first GameTank test cart (M2).
;
; Exercises the CPU + address decode + banked system RAM end to end:
;   1. fills $0040-$013F (bank 0) with (index XOR $5A)
;   2. writes a distinct marker at $0000 of each of the four RAM banks
;      via the $2005 banking register ([7:6] = bank)
;   3. writes a completion marker at $1FFF of bank 0
;
; The Verilator integration test asserts the physical BRAM contents.
; Assembled with ca65, linked with gametank.cfg (32 KB EEPROM-style cart,
; vectors in the top 6 bytes).

BANKING = $2005

.segment "CODE"

reset:
        sei
        cld
        ldx #$FF
        txs

        ; pattern fill: $0040-$013F <- X xor $5A
        ldx #$00
fill:   txa
        eor #$5A
        sta a:$0040,x   ; a: forces absolute,X (zp,X would wrap at $FF)
        inx
        bne fill
        ldx #$00
fill2:  txa
        eor #$5A
        sta $0140,x     ; second page keeps it out of zero page addressing
        inx
        bne fill2

        ; bank markers
        lda #$40        ; bank 1
        sta BANKING
        lda #$B1
        sta $0000
        lda #$80        ; bank 2
        sta BANKING
        lda #$B2
        sta $0000
        lda #$C0        ; bank 3
        sta BANKING
        lda #$B3
        sta $0000
        lda #$00        ; back to bank 0
        sta BANKING
        lda #$B0
        sta $0000

        ; completion marker at top of bank 0 window
        lda #$C3
        sta $1FFF

done:   jmp done

nmi:
irq:    rti

.segment "VECTORS"
        .word nmi       ; $FFFA
        .word reset     ; $FFFC
        .word irq       ; $FFFE
