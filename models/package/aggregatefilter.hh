#ifndef CLICK_AGGREGATEFILTER_HH
#define CLICK_AGGREGATEFILTER_HH
#include <click/element.hh>
CLICK_DECLS

/*
=c

AggregateFilter(ACTION_1 AGGREGATES, ..., ACTION_N AGGREGATES)

=s classification

filters packets based on aggregate annotation

=d

Filters packets based on their aggregate annotations. AggregateFilter's
configuration string is an arbitrary number of filters, which are
ACTION-AGGREGATES pairs. Packets are tested against the filters in order and
processed according to the ACTION in the first filter that matched. A packet
matches a filter if its aggregate annotation is listed in that filter's
AGGREGATES.

Each ACTION is either a port number, which specifies that the packet should be
sent out on that port; 'allow', which is equivalent to '0'; 'drop', which
means drop the packet; or 'deny', which is equivalent to '1' if the element
has at least two outputs and 'drop' if it does not. Packets that match none of
the filters are dropped. AggregateFilter has an arbitrary number of outputs.

The AGGREGATES arguments are space-separated lists of aggregate values, which
are unsigned integers. You can also specify ranges like '0-98'. The special
AGGREGATES 'all' and '-' both correspond to all aggregates.

AggregateFilter will warn about aggregate filters that match no packets, or
AGGREGATES components that were ignored (because of an earlier filter matching
the same aggregate).

=e

This configuration filters out a couple aggregates from the output of AggregateIPFlows.

  require(aggregates)
  FromDump(~/work/traces/2x10^5.dmp, STOP true, FORCE_IP true)
	-> AggregateIPFlows(ICMP true)
	-> AggregateFilter(allow 1093 3500 972 865 1765 988 1972 1225)
	-> ...

=a

IPFilter, Classifier, IPClassifier, AggregateIP, AggregateIPFlows */

class AggregateFilter : public Element { public:
  
    AggregateFilter();
    ~AggregateFilter();
  
    const char *class_name() const	{ return "AggregateFilter"; }
    const char *processing() const	{ return PUSH; }
    AggregateFilter *clone() const	{ return new AggregateFilter; }

    void notify_noutputs(int);
    int configure(Vector<String> &, ErrorHandler *);
    void cleanup(CleanupStage);

    void push(int, Packet *);

    enum { GROUPSHIFT = 8, GROUPMASK = 0xFFFFFFFFU << GROUPSHIFT,
	   NINGROUP = 1 << GROUPSHIFT, INGROUPMASK = NINGROUP - 1,
	   NBUCKETS = 256 };
    
  private:

    struct Group {
	uint32_t groupno;
	Group *next;
	uint8_t filters[NINGROUP];
	Group(uint32_t);
    };

    Group *_groups[NBUCKETS];
    int _default_output;

    Group *find_group(uint32_t);
    
};

#endif
