#ifndef CLICK_LOCATIONHISTOGRAM_HH
#define CLICK_LOCATIONHISTOGRAM_HH
#include <click/element.hh>
#include <click/etheraddress.hh>
#include <click/bighashmap.hh>
#include <click/glue.hh>
CLICK_DECLS

class LocationHistogram : public Element { public:
  
  LocationHistogram();
  ~LocationHistogram();
  
  const char *class_name() const		{ return "LocationHistogram"; }
  const char *port_count() const		{ return PORTS_1_1; }
  const char *processing() const		{ return PUSH; }
  
  int configure(Vector<String> &, ErrorHandler *);
  bool can_live_reconfigure() const		{ return true; }

  void push (int port, Packet *p_in);

  void add_handlers();


  unsigned _length;
  Vector<int> _byte_errors;
};

CLICK_ENDDECLS
#endif
