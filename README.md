# Firefly-synchrony-in-C
A unique way of demonstrating synchronous oscillation using a multithreaded program

"In Thailand, male Pteroptyx malaccae fireflies, congregated in trees, flash in
rhythmic synchrony with a period of about 560 ± 6 msec (at 28° C). Photometric 
and cinematographic records indicate that the range of flash coincidence is of
the order of ± 20 msec. This interval is considerably shorter than the minimum
eye-lantern response latency and suggests that the Pteroptyx synchrony is regulated
by central nervous feedback from preceding activity cycles, as in the human
"sense of rhythm," rather than by direct contemporaneous response to the flashes
of other individuals. Observations on the development of synchrony among Thai fireflies
indoors, the results of experiments on phase-shifting in the American Photinus pyralis
and comparisons with synchronization between crickets and between human beings are
compatible with the suggestion."

There are two different demos contained in the program:

1: Here we create 64 fireflies threads. Each firefly is slightly different, and is an independently executing
 program thread. Each firefly is started at a random time and has a cycle time that is slighty different from
 the others. An individual firefly is unaware of how many other fireflies there are and can only see if others
 are flashing or not. The master thread starts all the fireflies and then displays when they are blinking\
 on the terminal. 
 
 This program was to demonstrate & test the syncing ability. Each one flashes for 25 0ms +/-5ms and
 have an overall cycle of 1 second +/- 1ms\n\nThey tend to become synchronous within 5 cycles.
 
 2: Fireflies with distance and flash strength: In this instance we have 64 identical fireflies placed at random positions
  and are started at random times from 0 to 500ms. In this case however an individual firefly will see the light
  from every other firefly with a delay proportional to their individual distances apart and 
  a strength inversely proportional to their distances.
 
  These fireflies take a bit longer to develop their pattern synchrony.
  
  Notice how several occasionally fall out of sync, the reason for this is not known.
  They are numbered so that their flash order can be examined later.
