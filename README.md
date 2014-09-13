piglowd
=======

A daemon for PiGlow.

To install put Makefile and piglowd.c in a directory and type make. Then copy piglowd to whereever you want it. Copy piglowd.conf to /etc/piglowd and edit it to include the patterns you want.

To run the daemon, just execute piglowd.

Currently only 9 patterns are supported.

You define a pattern by a line in piglowd.conf. The line number is the pattern number.

You can select a pattern by: echo n > /tmp/piglowfifo (where n is the pattern number 0-9).
You can shut down the daemon by echo x >/tmp/piglowfifo.

The pattern language is based on up to three nested for loops with subscripts i, j and k.

LEDs are numbered by ring and leg number, the same as for winingPi. Leg numbers are 0-2 and ring numbers are 0-5 counting from the outside in.

The pattern line consists of a set of tokens separated by spaces.

The tokens are:

- For loop: <index>=<start>-<end> where index is i,j or k and start or end are integers. Omitting n goes to infinity.
- Ring identifier: r<index>=<intensity> or r<number>= intensity where index or i,j or k and intensity is 0-255
- Leg identifier: l<index>=intensity or l<number>= intensity
- LED identifier: r<index>l<index> where index is i,j or k, or can be a number.
- Delay: d<milliseconds>
  
Leg numbers are trucated modulo 3, and ring numbers odulo 6. This makes writing loops easier.

example:

i=0- j=0-5 rj=100 d100 rj=0 j=5-0 rj=100 d100 rj=0

This means for i from 0 to infinity, for j from 0 to 5 set all LEDS in the selected ring to intesity 100, delay 100 milliseconds and switch the ring off, and then do the same for j from 5 to 0 counting down. This causes concentric rings to be lit going inwards and the outwards, forever.

