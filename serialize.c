/*  Part of SWI-Prolog

    Author:        Jan Wielemaker
    E-mail:        J.Wielemaker@vu.nl
    WWW:           http://www.swi-prolog.org
    Copyright (c)  2006-2011, University of Amsterdam
    All rights reserved.

    Redistribution and use in source and binary forms, with or without
    modification, are permitted provided that the following conditions
    are met:

    1. Redistributions of source code must retain the above copyright
       notice, this list of conditions and the following disclaimer.

    2. Redistributions in binary form must reproduce the above copyright
       notice, this list of conditions and the following disclaimer in
       the documentation and/or other materials provided with the
       distribution.

    THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
    "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
    LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS
    FOR A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE
    COPYRIGHT OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT,
    INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING,
    BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
    LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER
    CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
    LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN
    ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
    POSSIBILITY OF SUCH DAMAGE.
*/

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <SWI-Stream.h>
#include <SWI-Prolog.h>
#include <string.h>
#include <errno.h>
#ifdef HAVE_MALLOC_H
#include <malloc.h>
#endif

static functor_t FUNCTOR_error2;
static functor_t FUNCTOR_io_error2;
static functor_t FUNCTOR_context2;
static functor_t FUNCTOR_type_error2;
static functor_t FUNCTOR_resource_error1;

#define MKFUNCTOR(n,a) \
	FUNCTOR_ ## n ## a = PL_new_functor(PL_new_atom(#n), a)

static void
init_functors()
{ MKFUNCTOR(error, 2);
  MKFUNCTOR(io_error, 2);
  MKFUNCTOR(context, 2);
  MKFUNCTOR(type_error, 2);
  MKFUNCTOR(resource_error, 1);
}

static int
io_error(term_t stream, const char *action, int eno)
{ term_t ex = PL_new_term_ref();
  char *msg = strerror(eno);

  if ( !msg )
    msg = "Unknown error";

  if ( PL_unify_term(ex,
		     PL_FUNCTOR, FUNCTOR_error2,
		       PL_FUNCTOR, FUNCTOR_io_error2,
		         PL_CHARS, action,
		         PL_TERM, stream,
		       PL_FUNCTOR, FUNCTOR_context2,
		         PL_VARIABLE,
		         PL_CHARS, msg) )
    return PL_raise_exception(ex);

  return FALSE;
}


static int
type_error(term_t actual, const char *expected)
{ term_t ex = PL_new_term_ref();

  if ( PL_unify_term(ex, PL_FUNCTOR, FUNCTOR_error2,
			   PL_FUNCTOR, FUNCTOR_type_error2,
			     PL_CHARS, expected,
			     PL_TERM, actual,
			   PL_VARIABLE) )
    return PL_raise_exception(ex);

  return FALSE;
}


static int
resource_error(const char *what)
{ term_t ex = PL_new_term_ref();

  if ( PL_unify_term(ex, PL_FUNCTOR, FUNCTOR_error2,
			   PL_FUNCTOR, FUNCTOR_resource_error1,
			     PL_CHARS, what,
			   PL_VARIABLE) )
    return PL_raise_exception(ex);

  return FALSE;
}


static int
get_stream_handle_ex(term_t t, IOSTREAM **s)
{ if ( PL_get_stream_handle(t, s) )
    return TRUE;

  return type_error(t, "stream");
}


static int
get_integer_ex(term_t t, int *val)
{ if ( PL_get_integer(t, val) )
    return TRUE;

  return type_error(t, "integer");
}


static int
get_float_ex(term_t t, double *val)
{ if ( PL_get_float(t, val) )
    return TRUE;

  return type_error(t, "float");
}


static foreign_t
write_int32(IOSTREAM *s, int i, term_t stream)
{ unsigned char b[4];

  b[0] = i>>24&0xff;
  b[1] = i>>16&0xff;
  b[2] = i>> 8&0xff;
  b[3] = i    &0xff;

  if ( Sfwrite(b, 1, 4, s) != 4 )
    return io_error(stream, "write", errno);

  return TRUE;
}


static foreign_t
pl_write_int32(term_t stream, term_t val)
{ IOSTREAM *s;
  int i;

  if ( get_stream_handle_ex(stream, &s) &&
       get_integer_ex(val, &i) )
    return write_int32(s, i, stream);

  return FALSE;
}


static int
read_int32(IOSTREAM *s, int *vp, term_t stream)
{ unsigned char b[4];
  int v;

  if ( Sfread(b, 1, 4, s) != 4 )
    return io_error(stream, "read", errno);

  v = b[0]<<24 | b[1]<<16 | b[2] << 8 | b[3];
  *vp = v;

  return TRUE;
}


static foreign_t
pl_read_int32(term_t stream, term_t val)
{ IOSTREAM *s;

  if ( get_stream_handle_ex(stream, &s) )
  { int v;

    if ( !read_int32(s, &v, stream) )
      return FALSE;

    return PL_unify_integer(val, v);
  }

  return FALSE;
}


static foreign_t
pl_read_atom(term_t stream, term_t atom)
{ IOSTREAM *s;

  if ( get_stream_handle_ex(stream, &s) )
  { int len;
    char buf[1024];
    char *b;
    char *q;
    int i;
    IOENC oenc = s->encoding;
    int rc;

    if ( !read_int32(s, &len, stream) )
      return FALSE;
    if ( len < sizeof(buf) )
      b = buf;
    else if ( !(b = malloc(len)) )
      return resource_error("memory");

    s->encoding = ENC_UTF8;
    for(q=b,i=0; i<len; i++)
    { int c = Sgetcode(s);

      if ( c < 0 )
      { s->encoding = oenc;
	if ( b != buf )
	  free(b);
	return io_error(stream, "read", errno);
      }
      *q++ = c;
    }
    s->encoding = oenc;

    rc = PL_unify_atom_nchars(atom, len, b);
    if ( b != buf )
      free(b);

    return rc;
  }

  return FALSE;
}


static foreign_t
pl_write_atom(term_t stream, term_t atom)
{ IOSTREAM *s;
  char *str;
  size_t len;

  if ( get_stream_handle_ex(stream, &s) &&
       PL_get_nchars(atom, &len, &str, CVT_ATOMIC|CVT_EXCEPTION) )
  { const unsigned char *q = (const unsigned char *)str;
    IOENC oenc = s->encoding;
    size_t i;

    if ( !write_int32(s, len, stream) )
      return FALSE;

    s->encoding = ENC_UTF8;
    for(i=0; i<len; i++, q++)
    { if ( Sputcode(*q, s) < 0 )
      { s->encoding = oenc;
	return io_error(stream, "write", errno);
      }
    }
    s->encoding = oenc;

    return TRUE;
  }

  return FALSE;
}


#ifdef WORDS_BIGENDIAN
static const int double_byte_order[] = { 7,6,5,4,3,2,1,0 };
#else
static const int double_byte_order[] = { 0,1,2,3,4,5,6,7 };
#endif


static foreign_t
pl_write_float(term_t stream, term_t val)
{ IOSTREAM *fd;
  double f;

  if ( get_stream_handle_ex(stream, &fd) &&
       get_float_ex(val, &f) )
  { unsigned char *cl = (unsigned char *)&f;
    unsigned int i;

    for(i=0; i<sizeof(double); i++)
    { if ( Sputc(cl[double_byte_order[i]], fd) < 0 )
	return io_error(stream, "write", errno);
    }

    return TRUE;
  }

  return FALSE;
}


static foreign_t
pl_read_float(term_t stream, term_t val)
{ IOSTREAM *fd;

  if ( get_stream_handle_ex(stream, &fd) )
  { double f;
    unsigned char *cl = (unsigned char *)&f;
    unsigned int i;

    for(i=0; i<sizeof(double); i++)
    { int c = Sgetc(fd);

      if ( c == -1 )
	return io_error(stream, "read", errno);
      cl[double_byte_order[i]] = c;
    }

    return PL_unify_float(val, f);
  }

  return FALSE;
}


install_t
install_serialize()
{ init_functors();

  PL_register_foreign("write_int32", 2, pl_write_int32, 0);
  PL_register_foreign("read_int32",  2, pl_read_int32,  0);
  PL_register_foreign("read_atom",   2, pl_read_atom,   0);
  PL_register_foreign("write_atom",  2, pl_write_atom,  0);
  PL_register_foreign("read_float",  2, pl_read_float,  0);
  PL_register_foreign("write_float", 2, pl_write_float, 0);
}
