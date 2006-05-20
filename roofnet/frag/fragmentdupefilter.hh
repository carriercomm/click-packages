#ifndef CLICK_FRAGMENTDUPEFILTER_HH
#define CLICK_FRAGMENTDUPEFILTER_HH
#include <click/element.hh>
#include <clicknet/ether.h>
#include <click/etheraddress.hh>
#include <click/hashmap.hh>
#include <click/timer.hh>
#include <click/dequeue.hh>
#include "frag.hh"
CLICK_DECLS

class FragmentDupeFilter : public Element { public:
  
  FragmentDupeFilter();
  ~FragmentDupeFilter();

  const char *class_name() const	{ return "FragmentDupeFilter"; }
  const char *port_count() const	{ return "1/1-2"; }
  const char *processing() const	{ return "a/ah"; }

  int configure(Vector<String> &, ErrorHandler *);
  bool can_live_reconfigure() const	{ return true; }

  void send_dupefilter(EtherAddress src);

  Packet * simple_action(Packet *p);

  struct DstInfo {
    EtherAddress src;
    DEQueue<struct fragid> frags;
    struct timeval last;
    DstInfo() { }
    DstInfo(EtherAddress s) { src = s; }
    
  };

  

  typedef HashMap<EtherAddress, DstInfo> FragTable;
  typedef FragTable::const_iterator FIter;

  FragTable _frags;

  EtherAddress _en;
  uint16_t _et;
  unsigned _window_size;


  bool _debug;
 private:

};

CLICK_ENDDECLS
#endif
