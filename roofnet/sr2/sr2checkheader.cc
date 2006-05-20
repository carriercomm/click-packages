/*
 * sr2checkheader.{cc,hh} -- element checks SR header for correctness
 * (checksums, lengths)
 * John Bicket
 * apapted from checkwifiheader.{cc,hh} by Douglas S. J. De Couto
 * from checkipheader.{cc,hh} by Robert Morris
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
#include <click/confparse.hh>
#include <click/etheraddress.hh>
#include <click/glue.hh>
#include <clicknet/ether.h>
#include <clicknet/ip.h>
#include "sr2packet.hh"
#include "sr2checkheader.hh"


CLICK_DECLS

SR2CheckHeader::SR2CheckHeader()
  : _drops(0)
{
}

SR2CheckHeader::~SR2CheckHeader()
{
}

void
SR2CheckHeader::drop_it(Packet *p)
{
  if (_drops == 0)
    click_chatter("SR2CheckHeader %s: first drop", name().c_str());
  _drops++;
  
  if (noutputs() == 2)
    output(1).push(p);
  else
    p->kill();
}

Packet *
SR2CheckHeader::simple_action(Packet *p)
{
  click_ether *eh = (click_ether *) p->data();
  struct sr2packet *pk = (struct sr2packet *) (eh+1);
  unsigned int tlen = 0;

  if (!pk)
    goto bad;

  if (p->length() < sizeof(struct sr2packet)) { 
    click_chatter("%s: packet truncated", name().c_str());
    goto bad;
  }

  if (pk->_type & SR2_PT_DATA) {
    tlen = pk->hlen_with_data();
  } else {
    tlen = pk->hlen_wo_data();
  }

  if (pk->_version != _sr2_version) {
    static bool version_warning = false;

    _bad_table.insert(EtherAddress(eh->ether_shost), pk->_version);

    if (!version_warning) {
      version_warning = true;
      click_chatter ("%s: unknown sr version %x from %s", 
		     name().c_str(), 
		     pk->_version,
		     EtherAddress(eh->ether_shost).s().c_str());
    }

     
     goto bad;
  }
  
  if (tlen > p->length()) { 
    /* can only check inequality, as short packets are padded to a
       minimum frame size for wavelan and ethernet */
    click_chatter("%s: bad packet size, wanted %d, only got %d", name().c_str(),
		  tlen + sizeof(click_ether), p->length());
    goto bad;
  }

  if (0 && !pk->check_checksum()) {
    click_chatter("%s: bad SR checksum", name().c_str());
    click_chatter("%s: length: %d", name().c_str(), tlen);
    goto bad;
  }


  if (pk->next() > pk->num_links()){
    click_chatter("%s: data with bad next hop from %s\n", 
		  name().c_str(),
		  pk->get_link_node(0).s().c_str());
    goto bad;
  }




  return(p);
  
 bad:
  drop_it(p);
  return 0;
}



String 
SR2CheckHeader::bad_nodes() {

  StringAccum sa;
  for (BadTable::const_iterator i = _bad_table.begin(); i; i++) {
    uint8_t version = i.value();
    EtherAddress dst = i.key();
    sa << this << " eth " << dst.s().c_str() << " version " << (int) version << "\n";
  }

  return sa.take_string();
}

enum {
  H_DROPS,
  H_BAD_VERSION,
};

static String 
SR2CheckHeader_read_param(Element *e, void *thunk)
{
  SR2CheckHeader *td = (SR2CheckHeader *)e;
    switch ((uintptr_t) thunk) {
    case H_DROPS:   return String(td->drops()) + "\n";
    case H_BAD_VERSION: return td->bad_nodes();
    default:
      return String() + "\n";
    }
}
      

void
SR2CheckHeader::add_handlers()
{
  add_read_handler("drops", SR2CheckHeader_read_param, (void *) H_DROPS);
  add_read_handler("bad_version", SR2CheckHeader_read_param, (void *) H_BAD_VERSION);
}

#include <click/hashmap.cc>
#if EXPLICIT_TEMPLATE_INSTANCES
template class HashMap<EtherAddress, uint8_t>;
#endif

EXPORT_ELEMENT(SR2CheckHeader)
CLICK_ENDDECLS

