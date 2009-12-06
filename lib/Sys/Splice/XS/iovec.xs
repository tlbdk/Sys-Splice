#include "EXTERN.h"
#include "perl.h"
#include "XSUB.h"

// Get iovec struck
#include "sys/uio.h"

MODULE = Sys::Splice::XS::iovec  PACKAGE = Sys::Splice::XS::iovec  PREFIX = iov


SV *
iov_new(char *class, ...)
  PREINIT:
    iovec[] iov;

  CODE:
    if(items == 0) croak "No buffers given";
    
    RETVAL = sv_newmortal(); // Set default to undef
    
    iovec = safemalloc(items * sizeof(iovec));
    sv_setref_pv(RETVAL, class, (void*) h); 
    
    RETVAL = iovec;

  OUTPUT:
    RETVAL

