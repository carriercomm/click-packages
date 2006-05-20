/*
 * SR2SetSourceRoute.{cc,hh} -- DSR implementation
 * John Bicket
 *
 * Copyright (c) 1999-2001 Massachussetsourceroutes Institute of Technology
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
#include <click/ipaddress.hh>
#include <click/straccum.hh>
#include <clicknet/ether.h>
#include <elements/wifi/linktable.hh>
#include <elements/wifi/arptable.hh>
#include <elements/wifi/path.hh>
#include "sr2setsourceroute.hh"
#include "sr2forwarder.hh"

CLICK_DECLS


SR2SetSourceRoute::SR2SetSourceRoute()
  :  _sr_forwarder(0)
{
}

SR2SetSourceRoute::~SR2SetSourceRoute()
{
}

int
SR2SetSourceRoute::configure (Vector<String> &conf, ErrorHandler *errh)
{
  int ret;
  ret = cp_va_parse(conf, this, errh,
		    cpKeywords,
                    "IP", cpIPAddress, "IP address", &_ip,
		    "SR", cpElement, "SRForwarder element", &_sr_forwarder,
                    cpEnd);

  if (!_sr_forwarder || _sr_forwarder->cast("SRForwarder") == 0) 
    return errh->error("SRForwarder element is not a SRForwarder or not specified");
  if (!_ip) 
    return errh->error("IP Address must be specified");

  return ret;
}

int
SR2SetSourceRoute::initialize (ErrorHandler *)
{
  return 0;
}

Packet *
SR2SetSourceRoute::simple_action(Packet *p_in)
{

  
  IPAddress dst = p_in->dst_ip_anno();

  if (!dst) {
	  click_chatter("SR2SetSourceRoute %s: got invalid dst %s\n",
			name().c_str(),
			dst.s().c_str());
	  p_in->kill();
	  return 0;
  }
  
  Path *p = _routes.findp(dst);
  if (p) {
	  Packet *p_out = _sr_forwarder->encap(p_in, *p, 0);
	  return p_out;
  } else {
	  /* no route */
	  checked_output_push(1, p_in);
	  return 0;
  }
}


int
SR2SetSourceRoute::static_set_route(const String &arg, Element *e,
			void *, ErrorHandler *errh) 
{
  SR2SetSourceRoute *n = (SR2SetSourceRoute *) e;
  Vector<String> args;
  Path p;

  cp_spacevec(arg, args);
  for (int x = 0; x < args.size(); x++) {
    IPAddress ip;
    if (!cp_ip_address(args[x], &ip)) {
      return errh->error("Couldn't read arg %d to ip: %s",
			 x,
			 args[x].c_str());
    }
    p.push_back(ip);
  }
  if (p[0] != n->_ip) {
    return errh->error("First hop %s doesn't match my ip %s",
		       p[0].s().c_str(),
		       n->_ip.s().c_str());
  }
  n->set_route(p);
  return 0;
}

void
SR2SetSourceRoute::set_route(Path p) 
{
  if (p.size() < 1) {
    click_chatter("SR2SetSourceRoute %s: Path must be longer than 0\n",
		  name().c_str());
  }
  if (p[0] != _ip) {
    click_chatter("SR2SetSourceRoute %s: First node must be me (%s) not %s!\n",
		  name().c_str(),
		  _ip.s().c_str(),
		  p[0].s().c_str());
  }

  _routes.insert(p[p.size()-1], p);
  
}
String
SR2SetSourceRoute::static_print_routes(Element *f, void *)
{
  SR2SetSourceRoute *d = (SR2SetSourceRoute *) f;
  return d->print_routes();
}

String
SR2SetSourceRoute::print_routes()
{
  StringAccum sa;
  for (RouteTable::iterator iter = _routes.begin(); iter; iter++) {
    IPAddress dst = iter.key();
    Path p = iter.value();
    sa << dst << " : " << path_to_string(p) << "\n";
  }
  return sa.take_string();
}

int
SR2SetSourceRoute::static_clear(const String &arg, Element *e,
			void *, ErrorHandler *errh) 
{
  SR2SetSourceRoute *n = (SR2SetSourceRoute *) e;
  bool b;

  if (!cp_bool(arg, &b))
    return errh->error("`clear' must be a boolean");

  if (b) {
    n->clear();
  }
  return 0;
}
void
SR2SetSourceRoute::clear() 
{
  _routes.clear();
}

void
SR2SetSourceRoute::add_handlers()
{
  add_read_handler("routes", static_print_routes, 0);
  add_write_handler("clear", static_clear, 0);
  add_write_handler("set_route", static_set_route, 0);
}

// generate Vector template instance
#include <click/vector.cc>
#include <click/bighashmap.cc>
#include <click/hashmap.cc>
#include <click/dequeue.cc>
#if EXPLICIT_TEMPLATE_INSTANCES
template class HashMap<IPAddress, Path>;
#endif

CLICK_ENDDECLS
EXPORT_ELEMENT(SR2SetSourceRoute)
