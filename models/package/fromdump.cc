/*
 * fromdump.{cc,hh} -- element reads packets from tcpdump file
 * Eddie Kohler
 *
 * Copyright (c) 1999-2000 Massachusetts Institute of Technology
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
#include <config.h>

#include "fromdump.hh"
#include <click/confparse.hh>
#include <click/router.hh>
#include "elements/standard/scheduleinfo.hh"
#include <click/error.hh>
#include <click/glue.hh>
#include <click/click_ip.h>
#include "elements/userlevel/fakepcap.h"
#include <unistd.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#ifdef ALLOW_MMAP
#include <sys/mman.h>
#endif

#define	SWAPLONG(y) \
	((((y)&0xff)<<24) | (((y)&0xff00)<<8) | (((y)&0xff0000)>>8) | (((y)>>24)&0xff))
#define	SWAPSHORT(y) \
	( (((y)&0xff)<<8) | ((u_short)((y)&0xff00)>>8) )

FromDump_Fast::FromDump_Fast()
    : Element(0, 1), _fd(-1), _buffer(0), _data_packet(0), _packet(0),
      _task(this)
{
    MOD_INC_USE_COUNT;
}

FromDump_Fast::~FromDump_Fast()
{
    MOD_DEC_USE_COUNT;
    uninitialize();
}

int
FromDump_Fast::configure(const Vector<String> &conf, ErrorHandler *errh)
{
    bool timing = false, stop = false;
#ifdef __linux__
    bool mmap = false;
#else
    bool mmap = true;
#endif
    _sampling_prob = (1 << 28);
    
    if (cp_va_parse(conf, this, errh,
		    cpFilename, "dump file name", &_filename,
		    cpOptional,
		    cpBool, "use original packet timing?", &timing,
		    cpKeywords,
		    "TIMING", cpBool, "use original packet timing?", &timing,
		    "STOP", cpBool, "stop driver when done?", &stop,
		    "MMAP", cpBool, "access file with mmap()?", &mmap,
		    "SAMPLE", cpUnsignedReal2, "sampling probability", 28, &_sampling_prob,
		    0) < 0)
	return -1;
    if (_sampling_prob > (1 << 28)) {
	errh->warning("SAMPLE probability reduced to 1");
	_sampling_prob = (1 << 28);
    } else if (_sampling_prob == 0)
	errh->warning("SAMPLE probability is 0; emitting no packets");
    
    _timing = timing;
    _stop = stop;
#ifdef ALLOW_MMAP
    _mmap = mmap;
    _mmap_unit = 0;
#else
    if (mmap)
	errh->warning("`MMAP' is not supported on this platform");
#endif
    return 0;
}

static void
swap_file_header(const fake_pcap_file_header *hp, fake_pcap_file_header *outp)
{
    outp->magic = SWAPLONG(hp->magic);
    outp->version_major = SWAPSHORT(hp->version_major);
    outp->version_minor = SWAPSHORT(hp->version_minor);
    outp->thiszone = SWAPLONG(hp->thiszone);
    outp->sigfigs = SWAPLONG(hp->sigfigs);
    outp->snaplen = SWAPLONG(hp->snaplen);
    outp->linktype = SWAPLONG(hp->linktype);
}

static void
swap_packet_header(const fake_pcap_pkthdr *hp, fake_pcap_pkthdr *outp)
{
    outp->ts.tv_sec = SWAPLONG(hp->ts.tv_sec);
    outp->ts.tv_usec = SWAPLONG(hp->ts.tv_usec);
    outp->caplen = SWAPLONG(hp->caplen);
    outp->len = SWAPLONG(hp->len);
}

int
FromDump_Fast::error_helper(ErrorHandler *errh, const char *x)
{
    if (errh)
	errh->error("%s: %s", _filename.cc(), x);
    else
	click_chatter("%s: %s", declaration().cc(), x);
    return -1;
}

#ifdef ALLOW_MMAP
static void
munmap_destructor(unsigned char *data, size_t amount)
{
    if (munmap(data, amount) < 0)
	click_chatter("FromDump: munmap: %s", strerror(errno));
}

int
FromDump_Fast::read_buffer_mmap(ErrorHandler *errh)
{
    if (_mmap_unit == 0) {  
	size_t page_size = getpagesize();
	_mmap_unit = (WANT_MMAP_UNIT / page_size) * page_size;
	_mmap_off = 0;
	// don't report most errors on the first time through
	errh = ErrorHandler::silent_handler();
    }

    // get length of file
    struct stat statbuf;
    if (fstat(_fd, &statbuf) < 0)
	return error_helper(errh, String("stat: ") + strerror(errno));

    // check for end of file
    // But return -1 if we have not mmaped before: it might be a pipe, not
    // true EOF.
    if (_mmap_off >= statbuf.st_size)
	return (_mmap_off == 0 ? -1 : 0);

    // actually mmap
    _len = _mmap_unit;
    if (_mmap_off + _len > (uint32_t)statbuf.st_size)
	_len = statbuf.st_size - _mmap_off;
    
    void *mmap_data = mmap(0, _len, PROT_READ, MAP_SHARED, _fd, _mmap_off);

    if (mmap_data == MAP_FAILED)
	return error_helper(errh, String("mmap: ") + strerror(errno));

    _data_packet = Packet::make((unsigned char *)mmap_data, _len, munmap_destructor);
    _buffer = _data_packet->data();
    _mmap_off += _len;

#ifdef HAVE_MADVISE
    // don't care about errors
    (void) madvise(mmap_data, _len, MADV_SEQUENTIAL);
#endif
    
    return 1;
}
#endif

int
FromDump_Fast::read_buffer(ErrorHandler *errh)
{
    if (_data_packet)
	_data_packet->kill();
    _data_packet = 0;
    _pos = _len = 0;

#ifdef ALLOW_MMAP
    if (_mmap) {
	int result = read_buffer_mmap(errh);
	if (result >= 0)
	    return result;
	// else, try a regular read
	_mmap = false;
	(void) lseek(_fd, _mmap_off, SEEK_SET);
	_pos = _len = 0;
    }
#endif
    
    _data_packet = Packet::make(0, 0, BUFFER_SIZE, 0);
    if (!_data_packet)
	return errh->error("out of memory!");
    _buffer = _data_packet->data();
    unsigned char *data = _data_packet->data();
    assert(_data_packet->headroom() == 0);
    
    while (_len < BUFFER_SIZE) {
	ssize_t got = read(_fd, data + _len, BUFFER_SIZE - _len);
	if (got > 0)
	    _len += got;
	else if (got == 0)	// premature end of file
	    return _len;
	else if (got < 0 && errno != EINTR && errno != EAGAIN)
	    return error_helper(errh, strerror(errno));
    }
    
    return _len;
}

int
FromDump_Fast::read_into(void *vdata, uint32_t dlen, ErrorHandler *errh)
{
    unsigned char *data = reinterpret_cast<unsigned char *>(vdata);
    uint32_t dpos = 0;

    while (dpos < dlen) {
	uint32_t howmuch = dlen - dpos;
	if (howmuch > _len - _pos)
	    howmuch = _len - _pos;
	memcpy(data + dpos, _buffer + _pos, howmuch);
	dpos += howmuch;
	_pos += howmuch;
	if (dpos < dlen && read_buffer(errh) <= 0)
	    return dpos;
    }

    return dlen;
}

int
FromDump_Fast::initialize(ErrorHandler *errh)
{
    if (_filename == "-") {
	_fd = STDIN_FILENO;
	_filename = "<stdin>";
    } else
	_fd = open(_filename.cc(), O_RDONLY);
    if (_fd < 0)
	return errh->error("%s: %s", _filename.cc(), strerror(errno));

    int result = read_buffer(errh);
    if (result < 0) {
	uninitialize();
	return -1;
    } else if (result == 0) {
	uninitialize();
	return errh->error("%s: empty file", _filename.cc());
    } else if (_len < sizeof(fake_pcap_file_header)) {
	uninitialize();
	return errh->error("%s: not a tcpdump file (too short)", _filename.cc());
    }
    
    fake_pcap_file_header swapped_fh;
    const fake_pcap_file_header *fh = (const fake_pcap_file_header *)_buffer;
    
    if (fh->magic == FAKE_TCPDUMP_MAGIC)
	_swapped = false;
    else {
	swap_file_header(fh, &swapped_fh);
	_swapped = true;
	fh = &swapped_fh;
    }
    if (fh->magic != FAKE_TCPDUMP_MAGIC) {
	uninitialize();
	return errh->error("%s: not a tcpdump file (bad magic number)", _filename.cc());
    }

    if (fh->version_major != FAKE_PCAP_VERSION_MAJOR) {
	uninitialize();
	return errh->error("%s: unknown major version %d", _filename.cc(), fh->version_major);
    }
    _minor_version = fh->version_minor;
    _linktype = fh->linktype;
    _pos = sizeof(fake_pcap_file_header);

    if ((_packet = read_packet(errh))) {
	struct timeval now;
	click_gettimeofday(&now);
	timersub(&now, &_packet->timestamp_anno(), &_time_offset);

	ScheduleInfo::join_scheduler(this, &_task, errh);
    } else
	errh->warning("%s: contains no packets", _filename.cc());
  
    return 0;
}

void
FromDump_Fast::uninitialize()
{
    if (_fd >= 0 && _fd != STDIN_FILENO)
	close(_fd);
    if (_packet)
	_packet->kill();
    if (_data_packet)
	_data_packet->kill();
    _task.unschedule();
    _fd = -1;
    _packet = _data_packet = 0;
}

Packet *
FromDump_Fast::read_packet(ErrorHandler *errh)
{
    fake_pcap_pkthdr swapped_ph;
    const fake_pcap_pkthdr *ph;
    int len, caplen;

  retry:
    // we may need to read bits of the file
    if (_len - _pos >= sizeof(*ph)) {
	ph = reinterpret_cast<const fake_pcap_pkthdr *>(_buffer + _pos);
	_pos += sizeof(*ph);
    } else {
	ph = &swapped_ph;
	if (read_into(&swapped_ph, sizeof(*ph), errh) < (int)sizeof(*ph))
	    return 0;
    }

    if (_swapped) {
	swap_packet_header(ph, &swapped_ph);
	ph = &swapped_ph;
    }

    // may need to swap 'caplen' and 'len' fields at or before version 2.3
    if (_minor_version > 3 || (_minor_version == 3 && ph->caplen <= ph->len)) {
	len = ph->len;
	caplen = ph->caplen;
    } else {
	len = ph->caplen;
	caplen = ph->len;
    }

    // check for errors
    if (caplen > len || caplen > 65535) {
	error_helper(errh, "bad packet header; giving up");
	return 0;
    }

    // checking sampling probability
    if (_sampling_prob < (1 << 28) && (uint32_t)(random() & 0xFFFFFFF) >= _sampling_prob) {
	_pos += caplen;
	goto retry;
    }
    
    // create packet
    Packet *p;
    if (_pos + caplen <= _len) {
	p = _data_packet->clone();
	if (!p) {
	    error_helper(errh, "out of memory!");
	    return 0;
	}
	p->change_headroom_and_length(_pos, caplen);
	p->set_timestamp_anno(ph->ts.tv_sec, ph->ts.tv_usec);
	_pos += caplen;
	
    } else {
	WritablePacket *wp = Packet::make(0, 0, caplen, 0);
	if (!wp) {
	    error_helper(errh, "out of memory!");
	    return 0;
	}
	// set timestamp anno now: may unmap earlier memory!
	wp->set_timestamp_anno(ph->ts.tv_sec, ph->ts.tv_usec);
	
	if (read_into(wp->data(), caplen, errh) < caplen) {
	    error_helper(errh, "short packet");
	    wp->kill();
	    return 0;
	}
	
	p = wp;
    }

    if (_linktype == FAKE_DLT_RAW && caplen >= 20) {
	const click_ip *iph = reinterpret_cast<const click_ip *>(p->data());
	p->set_ip_header(iph, iph->ip_hl << 2);
    }
    
    return p;
}

void
FromDump_Fast::run_scheduled()
{
    if (_timing) {
	struct timeval now;
	click_gettimeofday(&now);
	timersub(&now, &_time_offset, &now);
	if (timercmp(&_packet->timestamp_anno(), &now, >)) {
	    _task.fast_reschedule();
	    return;
	}
    }

    output(0).push(_packet);
    _packet = read_packet(0);
    if (_packet)
	_task.fast_reschedule();
    else if (_stop)
	router()->please_stop_driver();
}

String
FromDump_Fast::read_handler(Element *e, void *thunk)
{
    FromDump_Fast *fdf = (FromDump_Fast *)e;
    switch ((int)thunk) {
      case 0:
	return cp_unparse_real2(fdf->_sampling_prob, 28) + "\n";
      default:
	return "<error>\n";
    }
}

void
FromDump_Fast::add_handlers()
{
    add_read_handler("sampling_prob", read_handler, (void *)0);
    if (output_is_push(0))
	add_task_handlers(&_task);
}

ELEMENT_REQUIRES(userlevel)
EXPORT_ELEMENT(FromDump_Fast)
