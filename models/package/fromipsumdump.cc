/*
 * fromipsumdump.{cc,hh} -- element reads packets from IP summary dump file
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

#include "fromipsumdump.hh"
#include "toipsumdump.hh"
#include <click/confparse.hh>
#include <click/router.hh>
#include <click/standard/scheduleinfo.hh>
#include <click/error.hh>
#include <click/glue.hh>
#include <click/click_ip.h>
#include <click/click_udp.h>
#include <click/click_tcp.h>
#include <click/packet_anno.hh>
#include <unistd.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>

static uint8_t flag_mapping[256];

FromIPSummaryDump::FromIPSummaryDump()
    : Element(0, 1), _fd(-1), _pos(0), _len(0), _task(this), _pipe(0)
{
    MOD_INC_USE_COUNT;
    if (!flag_mapping[(uint8_t)'A']) {
	const char *x = ToIPSummaryDump::tcp_flags_word;
	for (int i = 0; *x; x++, i++)
	    flag_mapping[(uint8_t)(*x)] = i + 1;
    }
}

FromIPSummaryDump::~FromIPSummaryDump()
{
    MOD_DEC_USE_COUNT;
    uninitialize();
}

int
FromIPSummaryDump::configure(const Vector<String> &conf, ErrorHandler *errh)
{
    bool stop = false, active = true, zero = false;
    uint8_t default_proto = IP_PROTO_TCP;
    _sampling_prob = (1 << SAMPLING_SHIFT);
    
    if (cp_va_parse(conf, this, errh,
		    cpFilename, "dump file name", &_filename,
		    cpKeywords,
		    "STOP", cpBool, "stop driver when done?", &stop,
		    "ACTIVE", cpBool, "start active?", &active,
		    "ZERO", cpBool, "zero packet data?", &zero,
		    "SAMPLE", cpUnsignedReal2, "sampling probability", SAMPLING_SHIFT, &_sampling_prob,
		    "PROTO", cpByte, "default IP protocol", &default_proto,
		    0) < 0)
	return -1;
    if (_sampling_prob > (1 << SAMPLING_SHIFT)) {
	errh->warning("SAMPLE probability reduced to 1");
	_sampling_prob = (1 << SAMPLING_SHIFT);
    } else if (_sampling_prob == 0)
	errh->warning("SAMPLE probability is 0; emitting no packets");

    _default_proto = default_proto;
    _stop = stop;
    _active = active;
    _zero = zero;
    return 0;
}

int
FromIPSummaryDump::error_helper(ErrorHandler *errh, const char *x)
{
    if (errh)
	errh->error("%s: %s", _filename.cc(), x);
    else
	click_chatter("%s: %s", id().cc(), x);
    return -1;
}

int
FromIPSummaryDump::read_buffer(ErrorHandler *errh)
{
    if (_pos == 0 && _len == _buffer.length())
	_buffer.append_garbage(BUFFER_SIZE);

    unsigned char *data = (unsigned char *)_buffer.mutable_data();
    int buffer_len = _buffer.length();

    if (_len == buffer_len) {
	memmove(data, data + _pos, _len - _pos);
	_len -= _pos;
	_pos = 0;
    }
    int initial_len = _len;
    
    while (_len < buffer_len) {
	ssize_t got = read(_fd, data + _len, buffer_len - _len);
	if (got > 0)
	    _len += got;
	else if (got == 0)	// premature end of file
	    return _len - initial_len;
	else if (got < 0 && errno != EINTR && errno != EAGAIN)
	    return error_helper(errh, strerror(errno));
    }
    
    return _len - initial_len;
}

int
FromIPSummaryDump::read_line(String &result, ErrorHandler *errh)
{
    int epos = _pos;

    while (1) {
	bool done = false;
	
	if (epos >= _len) {
	    int delta = epos - _pos;
	    int errcode = read_buffer(errh);
	    if (errcode < 0 || (errcode == 0 && delta == 0))	// error
		return errcode;
	    else if (errcode == 0)
		done = true;
	    epos = _pos + delta;
	}

	const char *d = _buffer.data();
	while (epos < _len && d[epos] != '\n' && d[epos] != '\r')
	    epos++;

	if (epos < _len || done) {
	    result = _buffer.substring(_pos, epos - _pos);
	    if (epos < _len && d[epos] == '\r')
		epos++;
	    if (epos < _len && d[epos] == '\n')
		epos++;
	    _pos = epos;
	    return 1;
	}
    }
}

int
FromIPSummaryDump::initialize(ErrorHandler *errh)
{
    _pipe = 0;
    if (_filename == "-") {
	_fd = STDIN_FILENO;
	_filename = "<stdin>";
    } else
	_fd = open(_filename.cc(), O_RDONLY);

  retry_file:
    if (_fd < 0)
	return errh->error("%s: %s", _filename.cc(), strerror(errno));

    _pos = _len = 0;
    _buffer = String();
    int result = read_buffer(errh);
    if (result < 0) {
	uninitialize();
	return -1;
    } else if (result == 0) {
	uninitialize();
	return errh->error("%s: empty file", _filename.cc());
    }

    // check for a gziped or bzip2d dump
    if (_fd == STDIN_FILENO || _pipe)
	/* cannot handle gzip or bzip2 */;
    else if (_len >= 3
	     && ((_buffer[0] == '\037' && _buffer[1] == '\213')
		 || (_buffer[0] == 'B' && _buffer[1] == 'Z' && _buffer[2] == 'h'))) {
	close(_fd);
	_fd = -1;
	String command = (_buffer[0] == '\037' ? "zcat " : "bzcat ") + _filename;
	_pipe = popen(command.cc(), "r");
	if (!_pipe)
	    return errh->error("%s while executing `%s'", strerror(errno), command.cc());
	_fd = fileno(_pipe);
	goto retry_file;
    }

    String line;
    if (read_line(line, errh) < 0) {
	uninitialize();
	return -1;
    } else if (line.substring(0, 14) != "!IPSummaryDump"
	       && line.substring(0, 8) != "!creator") {
	errh->warning("%s: missing banner line; is this an IP summary dump?", _filename.cc());
	_pos = 0;
    }
    
    _format_complaint = false;
    if (output_is_push(0))
	ScheduleInfo::initialize_task(this, &_task, _active, errh);
    return 0;
}

void
FromIPSummaryDump::uninitialize()
{
    if (_pipe)
	pclose(_pipe);
    else if (_fd >= 0 && _fd != STDIN_FILENO)
	close(_fd);
    _fd = -1;
    _pipe = 0;
    _buffer = String();
    _task.unschedule();
}

void
FromIPSummaryDump::bang_data(const String &line, ErrorHandler *errh)
{
    Vector<String> words;
    cp_spacevec(line, words);

    _contents.clear();
    for (int i = 1; i < words.size(); i++) {
	String word = cp_unquote(words[i]);
	int what = ToIPSummaryDump::parse_content(word);
	if (what > W_NONE && what < W_LAST)
	    _contents.push_back(what);
	else {
	    error_helper(errh, "warning: unknown content type `" + word + "'");
	    _contents.push_back(W_NONE);
	}
    }

    if (_contents.size() == 0)
	error_helper(errh, "no contents specified");
}

Packet *
FromIPSummaryDump::read_packet(ErrorHandler *errh)
{
    WritablePacket *q = Packet::make((const char *)0, sizeof(click_ip) + sizeof(click_tcp));
    if (!q) {
	error_helper(errh, "out of memory!");
	return 0;
    }
    if (_zero)
	memset(q->data(), 0, q->length());
    q->set_ip_header((click_ip *)q->data(), sizeof(click_ip));
    click_ip *iph = q->ip_header();
    iph->ip_v = 4;
    iph->ip_hl = sizeof(click_ip) >> 2;
    iph->ip_p = _default_proto;
    
    String line;
    Vector<String> words;
    uint32_t j;
    
    while (1) {

	if (read_line(line, errh) <= 0) {
	    q->kill();
	    return 0;
	}

	const char *data = line.data();
	int len = line.length();

	if (len >= 6 && memcmp(data, "!data", 5) == 0 && isspace(data[5])) {
	    bang_data(line, errh);
	    continue;
	} else if (len >= 7 && memcmp(data, "!proto", 6) == 0 && isspace(data[6])) {
	    //handle_proto_line(line, errh);
	    continue;
	} else if (len == 0 || data[0] == '!' || data[0] == '#')
	    continue;

	words.clear();
	cp_spacevec(line, words);
	if (words.size() != _contents.size() || _contents.size() == 0)
	    break;		// bad format

	// checking sampling probability
	if (_sampling_prob < (1 << SAMPLING_SHIFT)
	    && (uint32_t)(random() & ((1 << SAMPLING_SHIFT) - 1)) >= _sampling_prob)
	    continue;
    
	int ok = 0;
	for (int i = 0; i < _contents.size(); i++)
	    switch (_contents[i]) {

	      case W_TIMESTAMP:
		ok += cp_timeval(words[i], &q->timestamp_anno());
		break;

	      case W_TIMESTAMP_SEC:
		ok += cp_integer(words[i], (int32_t *)&q->timestamp_anno().tv_sec);
		break;

	      case W_TIMESTAMP_USEC:
		ok += cp_integer(words[i], (int32_t *)&q->timestamp_anno().tv_usec);
		break;
		
	      case W_SRC:
		ok += cp_ip_address(words[i], (unsigned char *)&iph->ip_src);
		break;

	      case W_DST:
		ok += cp_ip_address(words[i], (unsigned char *)&iph->ip_dst);
		break;
		
	      case W_LENGTH:
		ok += (cp_unsigned(words[i], &j) && j <= 0xFFFF);
		iph->ip_len = htons(j);
		if (j > q->length())
		    SET_EXTRA_LENGTH_ANNO(q, j - q->length());
		break;
		
	      case W_PROTO: {
		  const String &w = words[i];
		  if (w.length() == 1) {
		      if (w[0] == 'T')
			  j = IP_PROTO_TCP;
		      else if (w[0] == 'U')
			  j = IP_PROTO_UDP;
		      else if (w[0] == 'I')
			  j = IP_PROTO_ICMP;
		      else if (w[0] >= '0' && w[0] <= '9')
			  j = w[0] - '0';
		      else
			  break;
		  } else if (cp_unsigned(words[i], &j) && j <= 255)
		      /* nada */;
		  else
		      break;
		  iph->ip_p = j;
		  ok++;
		  break;
	      }

	      case W_IPID:
		if (cp_unsigned(words[i], &j) && j <= 0xFFFF)
		    iph->ip_id = htons(j), ok++;
		break;

	      case W_SPORT:
		if (cp_unsigned(words[i], &j) && j <= 0xFFFF)
		    q->udp_header()->uh_sport = htons(j), ok++;
		break;

	      case W_DPORT:
		if (cp_unsigned(words[i], &j) && j <= 0xFFFF)
		    q->udp_header()->uh_dport = htons(j), ok++;
		break;

	      case W_TCP_SEQ:
		if (cp_unsigned(words[i], &j))
		    q->tcp_header()->th_seq = htonl(j), ok++;
		break;

	      case W_TCP_ACK:
		if (cp_unsigned(words[i], &j))
		    q->tcp_header()->th_ack = htonl(j), ok++;
		break;

	      case W_TCP_FLAGS:
		if (cp_unsigned(words[i], &j) && j <= 0xFF)
		    q->tcp_header()->th_flags = j, ok++;
		else {
		    const uint8_t *data = (const uint8_t *)words[i].data();
		    int len = words[i].length();
		    j = 0;
		    for (int i = 0; i < len; i++)
			if (flag_mapping[data[i]])
			    j |= 1 << (flag_mapping[data[i]] - 1);
			else if (data[i] == (uint8_t)'.' && len == 1)
			    ;
			else
			    goto bad_flags;
		    q->tcp_header()->th_flags = j, ok++;
		  bad_flags:
		    /* nothing to do */;
		}
		break;

	      case W_COUNT:
		if (cp_unsigned(words[i], &j))
		    SET_PACKET_COUNT_ANNO(q, j), ok++;
		break;

	    }

	if (!ok)
	    break;
	return q;
    }

    // bad format if we get here
    if (!_format_complaint) {
	error_helper(errh, "bad format");
	_format_complaint = true;
    }
    if (q)
	q->kill();
    return 0;
}

void
FromIPSummaryDump::run_scheduled()
{
    if (!_active)
	return;

    Packet *p = read_packet(0);
    if (!p) {
	if (_stop)
	    router()->please_stop_driver();
	return;
    }
    
    output(0).push(p);
    _task.fast_reschedule();
}

Packet *
FromIPSummaryDump::pull(int)
{
    if (!_active)
	return 0;

    Packet *p = read_packet(0);
    if (!p && _stop)
	router()->please_stop_driver();
    return p;
}

String
FromIPSummaryDump::read_handler(Element *e, void *thunk)
{
    FromIPSummaryDump *fd = static_cast<FromIPSummaryDump *>(e);
    switch ((int)thunk) {
      case 0:
	return cp_unparse_real2(fd->_sampling_prob, SAMPLING_SHIFT) + "\n";
      case 1:
	return cp_unparse_bool(fd->_active) + "\n";
      default:
	return "<error>\n";
    }
}

int
FromIPSummaryDump::write_handler(const String &s_in, Element *e, void *thunk, ErrorHandler *errh)
{
    FromIPSummaryDump *fd = static_cast<FromIPSummaryDump *>(e);
    String s = cp_uncomment(s_in);
    switch ((int)thunk) {
      case 1: {
	  bool active;
	  if (cp_bool(s, &active)) {
	      fd->_active = active;
	      if (active && fd->output_is_push(0) && !fd->_task.scheduled())
		  fd->_task.reschedule();
	      return 0;
	  } else
	      return errh->error("`active' should be Boolean");
      }
      default:
	return -EINVAL;
    }
}

void
FromIPSummaryDump::add_handlers()
{
    add_read_handler("sampling_prob", read_handler, (void *)0);
    add_read_handler("active", read_handler, (void *)1);
    add_write_handler("active", write_handler, (void *)1);
    if (output_is_push(0))
	add_task_handlers(&_task);
}

ELEMENT_REQUIRES(userlevel ToIPSummaryDump)
EXPORT_ELEMENT(FromIPSummaryDump)
