/*
 * toipsummarydump.{cc,hh} -- element writes packet summary in ASCII
 * Eddie Kohler
 *
 * Copyright (c) 2001 International Computer Science Institute
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
#include "toipsumdump.hh"
#include <click/standard/scheduleinfo.hh>
#include <click/confparse.hh>
#include <click/error.hh>
#include <click/click_ip.h>
#include <click/click_udp.h>
#include <click/click_tcp.h>
#include <unistd.h>
#include <time.h>
#include <sys/time.h>

ToIPSummaryDump::ToIPSummaryDump()
    : Element(1, 0), _f(0), _task(this)
{
    MOD_INC_USE_COUNT;
}

ToIPSummaryDump::~ToIPSummaryDump()
{
    MOD_DEC_USE_COUNT;
}

int
ToIPSummaryDump::configure(const Vector<String> &conf, ErrorHandler *errh)
{
    int before = errh->nerrors();
    String save = "timestamp 'ip src'";
    bool verbose = false;

    if (cp_va_parse(conf, this, errh,
		    cpFilename, "dump filename", &_filename,
		    cpKeywords,
		    "CONTENTS", cpArgument, "log contents", &save,
		    "VERBOSE", cpBool, "be verbose?", &verbose,
		    "BANNER", cpString, "banner", &_banner,
		    0) < 0)
	return -1;

    Vector<String> v;
    cp_spacevec(save, v);
    for (int i = 0; i < v.size(); i++) {
	String word = cp_unquote(v[i]);
	int what = parse_content(word);
	if (what > W_NONE && what < W_LAST)
	    _contents.push_back(what);
	else
	    errh->error("unknown content type `%s'", word.cc());
    }
    if (_contents.size() == 0)
	errh->error("no contents specified");

    _verbose = verbose;

    return (before == errh->nerrors() ? 0 : -1);
}

int
ToIPSummaryDump::initialize(ErrorHandler *errh)
{
    assert(!_f);
    if (_filename != "-") {
	_f = fopen(_filename, "wb");
	if (!_f)
	    return errh->error("%s: %s", _filename.cc(), strerror(errno));
    } else {
	_f = stdout;
	_filename = "<stdout>";
    }

    if (input_is_pull(0))
	ScheduleInfo::join_scheduler(this, &_task, errh);
    _active = true;

    // magic number
    fprintf(_f, "!IPSummaryDump 1.0\n");

    if (_banner)
	fprintf(_f, "!creator %s\n", cp_quote(_banner).cc());
    
    // host and start time
    if (_verbose) {
	char buf[BUFSIZ];
	buf[BUFSIZ - 1] = '\0';	// ensure NUL-termination
	if (gethostname(buf, BUFSIZ - 1) >= 0)
	    fprintf(_f, "!host %s\n", buf);

	time_t when = time(0);
	const char *cwhen = ctime(&when);
	struct timeval tv;
	if (gettimeofday(&tv, 0) >= 0)
	    fprintf(_f, "!starttime %ld.%ld (%.*s)\n", (long)tv.tv_sec,
		    (long)tv.tv_usec, (int)(strlen(cwhen) - 1), cwhen);
    }

    // data description
    fprintf(_f, "!data ");
    for (int i = 0; i < _contents.size(); i++)
	fprintf(_f, (i ? " '%s'" : "'%s'"), unparse_content(_contents[i]));
    fprintf(_f, "\n");
    
    return 0;
}

void
ToIPSummaryDump::uninitialize()
{
    if (_f && _f != stdout)
	fclose(_f);
    _f = 0;
    _task.unschedule();
}

static const char *content_names[] = {
    "??", "timestamp", "ts sec", "ts usec",
    "ip src", "ip dst", "ip len", "ip proto", "ip id",
    "sport", "dport", "tcp seq", "tcp ack", "tcp flags",
    "payload len"
};

const char *
ToIPSummaryDump::unparse_content(int what)
{
    if (what < 0 || what >= (int)(sizeof(content_names) / sizeof(content_names[0])))
	return "??";
    else
	return content_names[what];
}

int
ToIPSummaryDump::parse_content(const String &word)
{
    if (word == "timestamp" || word == "ts")
	return W_TIMESTAMP;
    else if (word == "sec" || word == "ts sec")
	return W_TIMESTAMP_SEC;
    else if (word == "usec" || word == "ts usec")
	return W_TIMESTAMP_USEC;
    else if (word == "src" || word == "ip src")
	return W_SRC;
    else if (word == "dst" || word == "ip dst")
	return W_DST;
    else if (word == "sport")
	return W_SPORT;
    else if (word == "dport")
	return W_DPORT;
    else if (word == "len" || word == "length" || word == "ip len")
	return W_LENGTH;
    else if (word == "id" || word == "ip id")
	return W_IPID;
    else if (word == "proto" || word == "ip proto")
	return W_PROTO;
    else if (word == "tcp seq" || word == "tcp seqno")
	return W_TCP_SEQ;
    else if (word == "tcp ack" || word == "tcp ackno")
	return W_TCP_ACK;
    else if (word == "tcp flags")
	return W_TCP_FLAGS;
    else if (word == "payload len" || word == "payload length")
	return W_PAYLOAD_LENGTH;
    else
	return W_NONE;
}

bool
ToIPSummaryDump::ascii_summary(Packet *p, StringAccum &sa) const
{
    // Not all of these will be valid, but we calculate them just once.
    const click_ip *iph = p->ip_header();
    const click_tcp *tcph = p->tcp_header();
    const click_udp *udph = p->udp_header();
    
    for (int i = 0; i < _contents.size(); i++) {
	if (i)
	    sa << ' ';
	
	switch (_contents[i]) {

	  case W_TIMESTAMP:
	    sa << p->timestamp_anno();
	    break;
	  case W_TIMESTAMP_SEC:
	    sa << p->timestamp_anno().tv_sec;
	    break;
	  case W_TIMESTAMP_USEC:
	    sa << p->timestamp_anno().tv_usec;
	    break;
	  case W_SRC:
	    if (!iph) goto no_data;
	    sa << IPAddress(iph->ip_src);
	    break;
	  case W_DST:
	    if (!iph) goto no_data;
	    sa << IPAddress(iph->ip_dst);
	    break;
	  case W_SPORT:
	    if (!iph || (iph->ip_p != IP_PROTO_TCP && iph->ip_p != IP_PROTO_UDP))
		goto no_data;
	    sa << ntohs(udph->uh_sport);
	    break;
	  case W_DPORT:
	    if (!iph || (iph->ip_p != IP_PROTO_TCP && iph->ip_p != IP_PROTO_UDP))
		goto no_data;
	    sa << ntohs(udph->uh_dport);
	    break;
	  case W_LENGTH:
	    if (!iph) goto no_data;
	    sa << ntohs(iph->ip_len);
	    break;
	  case W_IPID:
	    if (!iph) goto no_data;
	    sa << ntohs(iph->ip_id);
	    break;
	  case W_PROTO:
	    if (!iph) goto no_data;
	    switch (iph->ip_p) {
	      case IP_PROTO_TCP:	sa << 'T'; break;
	      case IP_PROTO_UDP:	sa << 'U'; break;
	      case IP_PROTO_ICMP:	sa << 'I'; break;
	      default:			sa << (int)(iph->ip_p); break;
	    }
	    break;
	  case W_TCP_SEQ:
	    if (!iph || iph->ip_p != IP_PROTO_TCP)
		goto no_data;
	    sa << ntohl(tcph->th_seq);
	    break;
	  case W_TCP_ACK:
	    if (!iph || iph->ip_p != IP_PROTO_TCP)
		goto no_data;
	    sa << ntohl(tcph->th_ack);
	    break;
	  case W_TCP_FLAGS: {
	      if (!iph || iph->ip_p != IP_PROTO_TCP)
		  goto no_data;
	      int flags = tcph->th_flags;
	      for (int i = 0; i < 7; i++)
		  if (flags & (1 << i))
		      sa << tcp_flags_word[i];
	      if (!flags)
		  sa << '.';
	      break;
	  }
	  case W_PAYLOAD_LENGTH: {
	      uint16_t len;
	      if (const click_ip *iph = p->ip_header()) {
		  len = ntohs(iph->ip_len) - p->ip_header_length();
		  if (iph->ip_p == IP_PROTO_TCP)
		      len -= (tcph->th_off << 2);
		  else if (iph->ip_p == IP_PROTO_UDP)
		      len -= sizeof(click_udp);
	      } else
		  len = p->length();
	      sa << len;
	      break;
	  }
	  no_data:
	  default:
	    sa << '-';
	    break;
	}
    }
    sa << '\n';
    return true;
}

void
ToIPSummaryDump::write_packet(Packet *p)
{
    _sa.clear();
    if (ascii_summary(p, _sa))
	fwrite(_sa.data(), 1, _sa.length(), _f);
}

void
ToIPSummaryDump::push(int, Packet *p)
{
    if (_active)
	write_packet(p);
    p->kill();
}

void
ToIPSummaryDump::run_scheduled()
{
    if (!_active)
	return;
    Packet *p = input(0).pull();
    if (p) {
	write_packet(p);
	p->kill();
    }
    _task.fast_reschedule();
}

void
ToIPSummaryDump::add_handlers()
{
    if (input_is_pull(0))
	add_task_handlers(&_task);
}

ELEMENT_REQUIRES(userlevel)
EXPORT_ELEMENT(ToIPSummaryDump)
