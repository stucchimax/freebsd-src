/* $FreeBSD: src/gnu/usr.bin/binutils/gdb/fbsd-kgdb-alpha.h,v 1.1 2002/07/10 06:40:03 obrien Exp $ */

#ifndef FBSD_KGDB_ALPHA_H
#define FBSD_KGDB_ALPHA_H

#undef  FRAME_SAVED_PC
#define FRAME_SAVED_PC(FRAME) \
  (kernel_debugging ? fbsd_kern_frame_saved_pc(FRAME) : \
		      alpha_saved_pc_after_call(FRAME))
 
#endif /* FBSD_KGDB_ALPHA_H */
