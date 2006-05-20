/*
 * CountErrors.{cc,hh} -- sets wifi txrate annotation on a packet
 * John Bicket
 *
 * Copyright (c) 2003 Massachusetts Institute of Technology
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, subject to the conditions
 * listed in the Click LICENSE file. These conditions include: you must
 * preserve this copyright notice, and you cannot mention the copyright
 * holders in advertising related to the Software without their permission.
 * The Software is provided WITHOUT ANY WARRANTY, EXPRESS OR IMPLIED. This
 * notice is a summary of the Click LICENSE file; the license in that file is
 * legally binding.
 */

#include <click/config.h>
#include <click/confparse.hh>
#include <click/error.hh>
#include <click/glue.hh>
#include <click/packet_anno.hh>
#include <click/straccum.hh>
#include <clicknet/ether.h>
#include "counterrors.hh"
CLICK_DECLS

CountErrors::CountErrors()
{
  static unsigned char bcast_addr[] = { 0xff, 0xff, 0xff, 0xff, 0xff, 0xff };
  _bcast = EtherAddress(bcast_addr);
}

CountErrors::~CountErrors()
{
}

int
CountErrors::configure(Vector<String> &conf, ErrorHandler *errh)
{
  _length = 0;
  _runs = true;
  _ok_bytes = 0;
  _error_bytes = 0;

  if (cp_va_parse(conf, this, errh,
		  cpKeywords, 
		  "LENGTH", cpUnsigned, "", &_length,
		  "RUNS", cpBool, "", &_runs,
		  cpEnd) < 0) {
    return -1;
  }

  return 0;
}

void
CountErrors::push (int port, Packet *p_in)
{
  int errors = 0;
  unsigned const char *ptr = p_in->data();
  int ok = 0;
  int bad = 0;
  int bad_runs = 0;

  int ok_bytes = 0;

  StringAccum sa;
  if (_runs) {
    sa << " [\n";
  }
  for (unsigned int x = 0; x < _length; x++) {
    bool error = false;
    if (x < p_in->length()) {
      error = (ptr[x] != 0xff);
    } else {
      error = true;
    }
    if (error) {
      if (_runs && !bad && ok) {
	sa << " ok " << ok << "\n";;
      }

      bad++;
      errors++;
      ok = 0;


    } else {
      ok_bytes++;
      if (_runs && bad && !ok) {
	sa << " bad " << bad << "\n";
	bad_runs++;
      }

      ok++;
      bad = 0;

    }
  }


  if (_runs && ok) {
    sa << " ok " << ok << "\n";
  }
  
  if (_runs &&bad) {
    sa << " bad " << bad << "\n";
    bad_runs++;
  }

  if (_runs) {
    sa << " ]\n";
  }

  _error_bytes += errors;
  _ok_bytes += ok_bytes;
  sa << "ok_bytes " << ok_bytes << "\n";
  sa << "errors " << errors << "\n";
  sa << "bad_runs " << bad_runs << "\n";
  click_chatter("%s", sa.take_string().c_str());

  output(port).push(p_in);
  return;
}

enum {H_STATS, H_SIGNAL, H_NOISE, H_ERROR_BYTES, H_CORRECT_BYTES};

static String
CountErrors_read_param(Element *e, void *thunk)
{
  CountErrors *td = (CountErrors *)e;
  switch ((uintptr_t) thunk) {
  case H_ERROR_BYTES: return String(td->_error_bytes) + "\n";
  case H_CORRECT_BYTES: return String(td->_ok_bytes) + "\n";
  default:
    return String();
  }
  
}  
	  
void
CountErrors::add_handlers()
{
  add_read_handler("error_bytes", CountErrors_read_param, (void *) H_ERROR_BYTES);
  add_read_handler("correct_bytes", CountErrors_read_param, (void *) H_CORRECT_BYTES);

}
CLICK_ENDDECLS
EXPORT_ELEMENT(CountErrors)

