/*
 * sr2ettmetric.{cc,hh} -- estimated transmission count (`ETT') metric
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
 * legally binding.  */

#include <click/config.h>
#include <click/confparse.hh>
#include <click/error.hh>
#include <click/straccum.hh>
#include "sr2ettmetric.hh"
#include "sr2ettstat.hh"
#include <elements/wifi/linktable.hh>
CLICK_DECLS 

SR2ETTMetric::SR2ETTMetric()
  : SR2LinkMetric()
{
}

SR2ETTMetric::~SR2ETTMetric()
{
}

void
SR2ETTMetric::update_link(IPAddress from, IPAddress to, 
		       Vector<SR2RateSize> rs, 
		       Vector<int> fwd, Vector<int> rev, 
		       uint32_t seq)
{

  if (!from || !to) {
    click_chatter("%{element}::update_link called with %s %s\n",
		  this,
		  from.unparse().c_str(),
		  to.unparse().c_str());
    return;
  }


  int one_ack_fwd = 0;
  int one_ack_rev = 0;
  int six_ack_fwd = 0;
  int six_ack_rev = 0;


  /* 
   * if we don't have a few probes going out, just pick
   * the smallest size for fwd rate
  */
  int one_ack_size = 0;
  int six_ack_size = 0;


  for (int x = 0; x < rs.size(); x++) {
    if (rs[x]._rate == 2 && 
	(!one_ack_size ||
	 one_ack_size > rs[x]._size)) {
      one_ack_size = rs[x]._size;
      one_ack_fwd = fwd[x];
      one_ack_rev = rev[x];
    } else if (rs[x]._rate == 12 && 
	       (!six_ack_size ||
		six_ack_size > rs[x]._size)) {
      six_ack_size = rs[x]._size;
      six_ack_fwd = fwd[x];
      six_ack_rev = rev[x];
    }
  }
  
  
  if (!one_ack_fwd && !six_ack_fwd &&
      !one_ack_rev && !six_ack_rev) {
    return;
  }
  int rev_metric = 0;
  int fwd_metric = 0;
  int best_rev_rate = 0;
  int best_fwd_rate = 0;
  
  for (int x = 0; x < rs.size(); x++) {
    if (rs[x]._size >= 100) {
      int ack_fwd = 0;
      int ack_rev = 0;
      if ((rs[x]._rate == 2) ||
	  (rs[x]._rate == 4) ||
	  (rs[x]._rate == 11) ||
	  (rs[x]._rate == 22)) {
	ack_fwd = one_ack_fwd;
	ack_rev = one_ack_rev;
      } else {
	ack_fwd = six_ack_fwd;
	ack_rev = six_ack_rev;
      }
      int metric = sr2_ett_metric(ack_rev,               
			      fwd[x],
			      rs[x]._rate);
      if (ack_rev < 30 || fwd[x] < 30)
	      metric = 999999;

      if (!fwd_metric || (metric && metric < fwd_metric)) {
	best_fwd_rate = rs[x]._rate;
	fwd_metric = metric;
      }
      
      metric = sr2_ett_metric(ack_fwd,               
			  rev[x],
			  rs[x]._rate);
      if (ack_rev < 30 || fwd[x] < 30)
	      metric = 999999;

      if (!rev_metric || (metric && metric < rev_metric)) {
	rev_metric = metric;
	best_rev_rate= rs[x]._rate;
      }
    }
  }

  /* update linktable */
  if (fwd_metric && 
      _link_table && 
      !_link_table->update_link(from, to, seq, 0, fwd_metric)) {
    click_chatter("%{element} couldn't update link %s > %d > %s\n",
		  this,
		  from.unparse().c_str(),
		  fwd_metric,
		  to.unparse().c_str());
  }
  if (rev_metric && 
      _link_table && 
      !_link_table->update_link(to, from, seq, 0, rev_metric)){
    click_chatter("%{element} couldn't update link %s < %d < %s\n",
		  this,
		  from.unparse().c_str(),
		  rev_metric,
		  to.unparse().c_str());
  }
}

EXPORT_ELEMENT(SR2ETTMetric)
ELEMENT_REQUIRES(bitrate)
ELEMENT_REQUIRES(SR2LinkMetric)
CLICK_ENDDECLS
