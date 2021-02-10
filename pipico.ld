/* first include your processor's MEMORY layout stuff.*/
INCLUDE cortexm/rpi/rp2040.ld


/* Rowley builder likes to reference stuff that never actually gets referenced, we dummy those to map to an actual function that does nothing */
__do_debug_operation = do_nothing ;
__vfprintf = do_nothing ;
__vfscanf = do_nothing ;
__vfprintf = do_nothing ;
__vfscanf = do_nothing ;
printf = do_nothing ;
/*__putchar = do_nothing ;  /* refed by vfprintf*/

/* these guys come and go! compiler sometimes seem to ignore our 'leave OS stuff out'*/
__aeabi_errno_addr = do_nothing ;
/*__aeabi_read_tp = do_nothing ; /**/
  abort = do_nothing ;
/* __dso_handle = do_nothing ; /* try commenting out once you have hunted down unexpected visitors in the link map */
/* end = 0 ;/* used by _sbrk, which should not be getting called */


/* then the generic "flash only" stuff, which presently is tightly tied into cstartup.cpp */
INCLUDE cortexm/cortexm.ld


