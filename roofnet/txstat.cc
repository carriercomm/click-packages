/*
 * txstat.{cc,hh} -- extract per-packet link tx counts
 * John Bicket
 *
 * Copyright (c) 1999-2002 Massachusetts Institute of Technology
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
#include <clicknet/ether.h>
#include <click/error.hh>
#include <click/glue.hh>
#include <click/timer.hh>
#include <click/straccum.hh>
#include <clicknet/wifi.h>
#include "txstat.hh"
CLICK_DECLS

TXStat::TXStat()
{
  static unsigned char bcast_addr[] = { 0xff, 0xff, 0xff, 0xff, 0xff, 0xff };
  _bcast = EtherAddress(bcast_addr);
}

TXStat::~TXStat()
{
}


int
TXStat::configure(Vector<String> &conf, ErrorHandler *errh)
{
  int res = cp_va_kparse(conf, this, errh,
			 "SRCETH", cpkP+cpkM, cpEtherAddress, &_eth,
			 cpEnd);
  return res;
  

}

int
TXStat::initialize(ErrorHandler *errh)
{
  if (noutputs() > 0) {
    if (!_eth) 
      return errh->error("Source IP and Ethernet address must be specified to send probes");
  }

  return 0;
}


Packet *
TXStat::simple_action(Packet *p_in)
{
  click_ether *eh = (click_ether *) p_in->data();
  //click_chatter("SRCR %s: got sr packet", _ip.unparse().c_str());
  
  EtherAddress dst = EtherAddress(eh->ether_dhost);
  if (dst == _bcast) {
    //click_chatter("TXStat %s: broadcast packet", _eth.unparse().c_str());
    p_in->kill();
    return 0;
  }
  
  struct click_wifi_extra *ceh = (struct click_wifi_extra *) p_in->user_anno();
  //int long_retries = p_in->user_anno_c (TX_ANNO_LONG_RETRIES);
  bool success = !(ceh->flags & WIFI_EXTRA_TX_FAIL);
  int rate = ceh->rate;
  TXNeighborInfo *nfo = _neighbors.findp(dst);
  if (!nfo) {
    TXNeighborInfo foo = TXNeighborInfo(dst);
    _neighbors.insert(dst, foo);
    nfo = _neighbors.findp(dst);
  }
  
  nfo->_packets_sent++;
  //nfo->_long_retries += long_retries;
  //nfo->_short_retries += short_retries;
  nfo->_rate = rate;
  //click_chatter("TXStat %s: long=%d, short=%d, fail=%s", 
  //_eth.unparse().c_str(), long_retries, short_retries, failure ? "true" : "false");
  if (!success) {
    nfo->_failures++;
  }

  p_in->kill();
  return 0;
}

String
TXStat::static_print_tx_stats(Element *e, void *)
{
  TXStat *n = (TXStat *) e;
  return n->print_tx_stats();
}
String 
TXStat::print_tx_stats() 
{
  StringAccum sa;
  for (TXNIter iter = _neighbors.begin(); iter.live(); iter++) {
    TXNeighborInfo nfo = iter.value();
    sa << nfo._eth.unparse() << "\n";
    sa << " packets sent :" << nfo._packets_sent << "\n";
    sa << " failures     :" << nfo._failures << "\n";
    sa << " long_retries :" << nfo._long_retries << "\n";
    sa << " short_retries:" << nfo._short_retries << "\n";
    sa << " rate         :" << nfo._rate << "\n";
    sa << "\n";
  }
  return sa.take_string();
}


void
TXStat::add_handlers()
{
  add_read_handler("tx_stats", static_print_tx_stats, 0);
}

EXPORT_ELEMENT(TXStat)
CLICK_ENDDECLS
