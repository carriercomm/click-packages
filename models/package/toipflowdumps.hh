// -*- c-basic-offset: 4 -*-
#ifndef CLICK_TOIPFLOWDUMPS_HH
#define CLICK_TOIPFLOWDUMPS_HH
#include <click/element.hh>
#include <click/ipflowid.hh>
#include <click/straccum.hh>
#include <click/task.hh>
#include <click/timer.hh>
#include <click/notifier.hh>
#include <clicknet/tcp.h>
#include "aggregatenotifier.hh"
CLICK_DECLS

/*
=c

ToIPFlowDumps(OUTPUT_PATTERN [, I<KEYWORDS>])

=s measurement

creates separate trace files for each flow

=d

Writes summary information, in the style of ToIPSummaryDump, about incoming
packets to several files. ToIPFlowDumps writes one file per flow. It
distinguishes flows by their aggregate annotations. You usually will run
ToIPFlowDumps downstream of an AggregateFlows element.

The OUTPUT_PATTERN argument gives the pattern used by ToIPSummaryDump to
generate filenames. Printf-like `C<%>' escapes in the pattern are expanded
differently for each flow. Available escapes are:

    %n      Aggregate annotation in decimal.
    %x, %X  Aggregate annotation in hex.
    %s      Source IP address.
    %.0s, %.1s, %.2s, %.3s
            First through fourth bytes of source IP address.
    %d      Destination IP address.
    %.0d, %.1d, %.2d, %.3d
            First through fourth bytes of destination IP address.
    %S      Source port.
    %D	    Destination port.
    %p      Protocol ('T' for TCP, 'U' for UDP).
    %%      A single % sign.

You may also use the `C<0>' flag and an optional field width, so `C<%06n>'
expands to the aggregate annotation, padded on the left with enough zeroes to
make at least 6 digits.

Keyword arguments are:

=over 8

=item NOTIFIER

The name of an AggregateNotifier element, like AggregateFlows. If given, then
ToIPFlowDumps will ask the element for notification when flows are deleted. It
uses that notification to free its state early. It's a very good idea to
supply a NOTIFIER.

=back

=n

Only available in user-level processes.

=e

This element

  ... -> ToIPFlowDumps(/tmp/flow%03d);

might create a file C</tmp/flow001> with the following contents.

  !IPSummaryDump 1.1
  !data 'timestamp' 'direction' 'tcp flags' 'tcp seq' 'tcp ack' 'payload len'
  !flowid 192.150.187.37 3153 18.26.4.44 21 T
  !first_seq > 2195313811
  !first_seq < 2484225252
  1018330170.887166 > S 0 0 0
  1018330170.962704 < SA 0 1 0

Note that sequence numbers have been renumbered, so that the first sequence
numbers seen by ToIPFlowDumps are output as 0. The `C<!first_seq>' comments
let you reconstruct actual sequence numbers if necessary.

=a

FromIPSummaryDump, ToIPSummaryDump, AggregateFlows */

class ToIPFlowDumps : public Element, public AggregateListener { public:
  
    ToIPFlowDumps();
    ~ToIPFlowDumps();
  
    const char *class_name() const	{ return "ToIPFlowDumps"; }
    const char *processing() const	{ return AGNOSTIC; }
    ToIPFlowDumps *clone() const	{ return new ToIPFlowDumps; }

    void notify_noutputs(int);
    int configure(Vector<String> &, ErrorHandler *);
    int initialize(ErrorHandler *);
    void cleanup(CleanupStage);

    void push(int, Packet *);
    Packet *pull(int);
    void run_scheduled();

    void aggregate_notify(uint32_t, AggregateEvent, const Packet *);

    virtual void add_note(uint32_t, const String &, ErrorHandler * = 0);

    struct Pkt {
	struct timeval timestamp;
	tcp_seq_t th_seq;
	tcp_seq_t th_ack;
	uint8_t direction;
	uint8_t th_flags;
	uint16_t payload_len;
    };
    struct Note {
	int before_pkt;
	uint32_t pos;
    };
    
  private:

    class Flow { public:

	Flow(const Packet *, const String &);

	uint32_t aggregate() const	{ return _aggregate; }
	Flow *next() const		{ return _next; }
	void set_next(Flow *f)		{ _next = f; }

	int output(bool done, ErrorHandler *);
	int add_pkt(const Packet *, ErrorHandler *);
	int add_note(const String &, ErrorHandler *);

      private:

	enum { NPKT = 1024, NNOTE = 256 };
	
	Flow *_next;
	IPFlowID _flowid;
	int _ip_p;
	uint32_t _aggregate;
	String _filename;
	bool _outputted;
	int _npkt;
	int _nnote;
	int _pkt_off;
	tcp_seq_t _first_seq[2];
	bool _have_first_seq[2];
	Pkt _pkt[NPKT];
	Note _note[NNOTE];
	StringAccum _note_text;

	int create_directories(const String &, ErrorHandler *);
	
    };

    enum { FLOWMAP_BITS = 10, NFLOWMAP = 1 << FLOWMAP_BITS };
    Flow *_flowmap[NFLOWMAP];

    String _filename_pattern;
    String _output_banner;

    uint32_t _nnoagg;
    uint32_t _nagg;
    AggregateNotifier *_agg_notifier;
    
    Task _task;
    NotifierSignal _signal;

    Timer _gc_timer;
    Vector<uint32_t> _gc_aggs;
    
    String expand_filename(const Packet *, ErrorHandler *) const;
    Flow *find_aggregate(uint32_t, const Packet * = 0);
    inline void smaction(Packet *);
    static void gc_hook(Timer *, void *);
    
};

CLICK_ENDDECLS
#endif