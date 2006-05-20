/*
 * sr2print.{cc,hh} -- print sr packets, for debugging.
 * John Bicket
 *
 * Copyright (c) 1999-2000 Massachusetts Institute of Technology
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
#include <click/ipaddress.hh>
#include <click/confparse.hh>
#include <click/error.hh>
#include <click/glue.hh>
#include <click/straccum.hh>
#include <click/packet_anno.hh>
#include <clicknet/ether.h>
#include "sr2packet.hh"
#include "sr2print.hh"

CLICK_DECLS

SR2Print::SR2Print()
  : _print_anno(false),
    _print_checksum(false)
{
  _label = "";
}

SR2Print::~SR2Print()
{
}

int
SR2Print::configure(Vector<String> &conf, ErrorHandler* errh)
{
  int ret;
  ret = cp_va_parse(conf, this, errh,
                  cpOptional,
		  cpString, "label", &_label,
		  cpKeywords,
		    cpEnd);
  return ret;
}

String 
SR2Print::sr_to_string(struct sr2packet *pk) 
{
  StringAccum sa;
  String type;
  switch (pk->_type) {
  case SR2_PT_QUERY:
    type = "QUERY";
    break;
  case SR2_PT_REPLY:
    type = "REPLY";
    break;
  case SR2_PT_DATA:
    type = "DATA";
    break;
  case SR2_PT_GATEWAY:
    type = "GATEWAY";
    break;
  default:
    type = "UNKNOWN";
  }
  String flags = "";
  sa << type;
  sa << " (";
  if (pk->flag(SR2_FLAG_ERROR)) {
    sa << " ERROR ";
  }
  if (pk->flag(SR2_FLAG_UPDATE)) {
    sa << " UPDATE ";
  }
  sa << flags << ")";

  if (pk->_type == SR2_PT_DATA) {
    sa << " len " << pk->hlen_with_data();
  } else {
    sa << " len " << pk->hlen_wo_data();
  }

  if (pk->_type == SR2_PT_DATA) {
    sa << " dataseq " << pk->data_seq();
  }
  IPAddress qdst = pk->get_qdst();
  if (qdst) {
    sa << " qdst " << qdst;
  }

  if (pk->_type == SR2_PT_DATA) {
    sa << " dlen=" << pk->data_len();
  }

  sa << " seq " << pk->seq();
  sa << " nhops " << pk->num_links();
  sa << " next " << pk->next();

  sa << " [";
  for(int i = 0; i< pk->num_links(); i++) {
    sa << " "<< pk->get_link_node(i).s() << " ";
    int fwd = pk->get_link_fwd(i);
    int rev = pk->get_link_rev(i);
    int seq = pk->get_link_seq(i);
    int age = pk->get_link_age(i);
    sa << "<" << fwd << " (" << seq << "," << age << ") " << rev << ">";
  }
  sa << " "<< pk->get_link_node(pk->num_links()).s() << " ";
  sa << "]";

  return sa.take_string();

}
Packet *
SR2Print::simple_action(Packet *p)
{
  StringAccum sa;
  if (_label[0] != 0) {
    sa << _label << ": ";
  } else {
      sa << "SR2Print ";
  }

  click_ether *eh = (click_ether *) p->data();
  struct sr2packet *pk = (struct sr2packet *) (eh+1);


  sa << SR2Print::sr_to_string(pk);
  
  click_chatter("%s", sa.c_str());

  return p;
}

CLICK_ENDDECLS
EXPORT_ELEMENT(SR2Print)
