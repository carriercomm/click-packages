#ifndef CLICK_TOIPSUMDUMP_HH
#define CLICK_TOIPSUMDUMP_HH
#include <click/element.hh>
#include <click/task.hh>
#include <click/straccum.hh>

/*
=c

ToIPSummaryDump(FILENAME [, I<KEYWORDS>])

=s sinks

writes packet summary information

=d

Writes summary information about incoming packets to FILENAME in a simple
ASCII format---each line corresponds to a packet. The CONTENTS keyword
argument determines what information is written. Writes to standard output if
FILENAME is a single dash `C<->'.

Keyword arguments are:

=over 8

=item CONTENTS

Space-separated list of field names. Each line of the summary dump will
contain those fields. Valid field names, with examples, are:

   timestamp    Packet timestamp: `996033261.451094'
   ts sec       Seconds portion of timestamp: `996033261'
   ts usec      Microseconds portion of timestamp: `451094'
   ip src       IP source address: `192.150.187.37'
   ip dst       IP destination address: `192.168.1.100'
   len          Packet length: `132'
   proto        IP protocol: `10', or `I' for ICMP, `T' for TCP, `U' for UDP
   ip id        IP ID: `48759'
   sport        TCP/UDP source port: `22'
   dport        TCP/UDP destination port: `2943'
   tcp seq      TCP sequence number: `93167339'
   tcp ack      TCP acknowledgement number: `93178192'
   tcp flags    TCP flags: `SA', `.'
   payload len  Payload length (not including IP/TCP/UDP headers): `34'
   count        Number of packets: `1'

If a field does not apply to a particular packet -- for example, `C<sport>' on
an ICMP packet -- ToIPSummaryDump prints a single dash for that value.

Default CONTENTS is `src dst'. You must quote field names that contain a
space -- for example, `C<src dst "tcp seq">'.

=item VERBOSE

Boolean. If true, then print out a couple comments at the beginning of the
dump describing the hostname and starting time, in addition to the `C<!data>' line describing the log contents.

=item BANNER

String. If supplied, prints a `C<!creator "BANNER">' comment at the beginning
of the dump.

=item MULTIPACKET

Boolean. If true, and the CONTENTS option doesn't contain `C<count>', then
generate multiple summary entries for packets with nonzero packet count
annotations. For example, if MULTIPACKET is true, and a packet has packet
count annotation 2, then ToIPSummaryDump will generate 2 identical lines for
that packet in the dump. False by default.

=back

=n

The characters corresponding to TCP flags are as follows:

   Flag name  Character  Value
   ---------  ---------  -----
   FIN        F          0x01
   SYN        S          0x02
   RST        R          0x04
   PSH        P          0x08
   ACK        A          0x10
   URG        U          0x20
   -          X          0x40
   -          Y          0x80

Some old IP summary dumps might contain an unsigned integer, representing the
flags byte, instead.

=e

Here are a couple lines from the start of a sample verbose dump.

  !creator "aciri-ipsumdump -i wvlan0"
  !host no.lcdf.org
  !starttime 996022410.322317 (Tue Jul 24 17:53:30 2001)
  !data 'ip src' 'ip dst'
  63.250.213.167 192.150.187.106
  63.250.213.167 192.150.187.106

=n

The `C<len>' and `C<payload len>' content types use the extra length
annotation. The `C<count>' content type uses the packet count annotation.

=a

FromDump, ToDump */

class ToIPSummaryDump : public Element { public:
  
    ToIPSummaryDump();
    ~ToIPSummaryDump();
  
    const char *class_name() const	{ return "ToIPSummaryDump"; }
    const char *processing() const	{ return AGNOSTIC; }
    const char *flags() const		{ return "S2"; }
    ToIPSummaryDump *clone() const	{ return new ToIPSummaryDump; }
  
    int configure(const Vector<String> &, ErrorHandler *);
    int initialize(ErrorHandler *);
    void uninitialize();
    void add_handlers();

    void push(int, Packet *);
    void run_scheduled();

    enum Content {		// must agree with FromIPSummaryDump
	W_NONE, W_TIMESTAMP, W_TIMESTAMP_SEC, W_TIMESTAMP_USEC,
	W_SRC, W_DST, W_LENGTH, W_PROTO, W_IPID,
	W_SPORT, W_DPORT, W_TCP_SEQ, W_TCP_ACK, W_TCP_FLAGS,
	W_PAYLOAD_LENGTH, W_COUNT,
	W_LAST
    };
    static int parse_content(const String &);
    static const char *unparse_content(int);

    static const char * const tcp_flags_word = "FSRPAUXY";
    
  private:

    String _filename;
    FILE *_f;
    StringAccum _sa;
    Vector<unsigned> _contents;
    bool _multipacket;
    bool _active;
    Task _task;
    bool _verbose : 1;
    String _banner;

    bool ascii_summary(Packet *, StringAccum &) const;
    void write_packet(Packet *, bool multipacket = false);
    
};

#endif
