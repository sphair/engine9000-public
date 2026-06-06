	section text,code
	xdef main
	xdef _main

console_addr	equ	$00fc0000

main:
_main:
	lea welcomeMsg,a0
	bsr printString

mainLoop:
	addq.l #1,counter
	move.l counter,d0
	lea hexBuffer,a0
	bsr u32ToHex

	lea hexBuffer,a0
	bsr printString
	lea newlineStr,a0
	bsr printString
	bra mainLoop

printString:
	movea.l #console_addr,a1

printCharLoop:
	move.b (a0)+,d0
	beq printDone
	move.b d0,(a1)
	bra printCharLoop

printDone:
	rts

u32ToHex:
	moveq #7,d1

hexLoop:
	rol.l #4,d0
	move.l d0,d2
	andi.b #$0f,d2
	cmpi.b #9,d2
	ble hexDigit
	addi.b #'A'-10,d2
	bra hexStore

hexDigit:
	addi.b #'0',d2

hexStore:
	move.b d2,(a0)+
	dbra d1,hexLoop
	clr.b (a0)
	rts

	section data,data
welcomeMsg:
	dc.b "Welcome to vasm test",10,0
newlineStr:
	dc.b 10,0

	section bss,bss
counter:
	ds.l 1
hexBuffer:
	ds.b 9
