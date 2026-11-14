/* Ordinary Edax initial iterative-deepening limits. GPL-3.0-or-later. */
#ifndef EDAX_ROOT_ITERATION_CORE_H
#define EDAX_ROOT_ITERATION_CORE_H
static inline void edax_iteration_limits(int requested,int empties,int *first,int *last){
 int end=requested;if(end>=empties){end=empties-ITERATIVE_MIN_EMPTIES+2;if(end<=0)end=2-(empties&1);}
 int start=6-(end&1);if(start>end-2)start=end-2;if(start<=0)start=2-(end&1);
 *first=start;*last=end;
}
static inline int edax_iteration_start(int start,int end,int requested,int empties){
 if(start>requested)start=requested;if(start>empties)start=empties;
 if(start<empties){start+=((start^end)&1);if(start<=0)start=2-(end&1);if(start>end)start=end;}
 return start;
}
#endif
