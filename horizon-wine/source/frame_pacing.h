#ifndef WINE_NX_FRAME_PACING_H
#define WINE_NX_FRAME_PACING_H

#include <stddef.h>

/* Stutter: the time between the frames a program shows, and what took the
 * time in the slowest of them. win32u calls wine_nx_frame_presented() after
 * each OpenGL swap and Vulkan present, with the Windows thread that presented
 * and the time the present itself took, in 100 ns. The thread report then
 * prints a [FRAMES] line (wine_nx_frame_report): the median frame, the 99th
 * percentile, the frames that took over twice the median and over 100 ms,
 * and for the three slowest the time within them that FEX spent translating
 * code (on the presenting thread, and on all), the card spent reading, the
 * GPU's shader compiler spent compiling, and the present took; with Mesa's
 * GL thread also the time the presenting thread spent handing it calls or
 * waiting for it, what the GL thread and the driver's thread ran, and the
 * faults handed to the program; with the profiler, where the presenting
 * thread was sampled within the frame. A gap of a
 * second or more is a loading screen or a menu, counted apart. */
void wine_nx_frame_presented( unsigned int tid, unsigned long long present_100ns );
/* Writes the line for the frames since the last one, and after it one line
 * for each of the slowest ("\n[FRAMES] slow frame ..."), and starts counting
 * again. 0 when no frame was shown since. */
int wine_nx_frame_report( char *line, size_t size );

#endif
