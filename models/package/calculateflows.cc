// -*- mode: c++; c-basic-offset: 4 -*-
#include <config.h>
#include <click/config.h>
#include "calculateflows.hh"
#include <click/error.hh>
#include <click/hashmap.hh>
#include <click/straccum.hh>
#include <click/confparse.hh>
#include <clicknet/ip.h>
#include <clicknet/tcp.h>
#include <clicknet/udp.h>
#include <click/packet_anno.hh>

#include <limits.h>

struct timeval
CalculateFlows::LossInfo::Search_seq_interval(tcp_seq_t start_seq, tcp_seq_t end_seq, unsigned paint)
{
    assert(paint < 2);
    timeval tbstart = time_by_firstseq[paint].find(start_seq);
    timeval tbend = time_by_lastseq[paint].find(end_seq);
    MapInterval &ibtime = inter_by_time[paint];
    
    if (!tbend.tv_sec && !tbend.tv_usec) {
	if (!tbstart.tv_sec && !tbstart.tv_usec) { // We have a partial retransmission ...
	    for (MapInterval::Iterator iter = ibtime.first(); iter; iter++){
		TimeInterval *tinter = const_cast<TimeInterval *>(&iter.value());
		if (tinter->start_byte < start_seq && tinter->end_byte > start_seq){
		    return tinter->time;
		}
		//printf("[%ld.%06ld : %u - %u ]\n",tinter->time.tv_sec, tinter->time.tv_usec, tinter->start_byte, tinter->end_byte);
	    }
	    // nothing matches (that cannot be possible unless there is
	    // reordering)
	    outoforder_pckt = 1; //set the outoforder indicator
	    printf("Cannot find packet in history of flow %u:%u!:[%u:%u], Possible reordering?\n",
		   agganno,
		   paint, 
		   start_seq,
		   end_seq);
	    timeval tv = timeval();
	    return tv;
	} else {
	    //printf("Found in Start Byte Hash\n");
	    return tbstart;
	}
	
    } else {
	//printf("Found in End Byte Hash\n");
	return tbend;
    }
    
}

void
CalculateFlows::LossInfo::calculate_loss_events(tcp_seq_t seq, unsigned seqlen, const struct timeval &time, unsigned paint)
{
    assert(paint < 2);
    double curr_diff;
    short int num_of_acks = acks[paint].find(seq);
    if ( seq < _max_seq[paint]){ // then we may have a new event.
	if ( seq < _last_seq[paint]){  //We have a new event ...
	    timeval time_last_sent  = Search_seq_interval(seq ,seq+seqlen, paint);	
	    if (prev_diff[paint] == 0){ //first time
		prev_diff[paint] = timesub(time, time_last_sent);
		curr_diff = prev_diff[paint];
	    } else {
		prev_diff[paint] = prev_diff[paint] < 0.000001 ? 0.000001 : prev_diff[paint];															
		curr_diff = timesub(time,time_last_sent);
		if (( doubling[paint] == 32) && (fabs(1-curr_diff/prev_diff[paint]) < 0.1)){
		    printf("Doubling threshold reached %ld.%06ld \n",time.tv_sec,time.tv_sec);
		} else {
		    if ((fabs(2.-curr_diff/prev_diff[paint]) < 0.1) && (!(num_of_acks > 3))){
			if (doubling[paint] < 1) {
			    doubling[paint] = prev_doubling[paint];
			}
			doubling[paint] = 2*doubling[paint];
		    }
		    if ((fabs(2.-curr_diff/prev_diff[paint]) > 0.1) && (!(num_of_acks > 3))){
			prev_doubling[paint] = doubling[paint];
			doubling[paint] = 0;
		    }
		}
	    }					
	    
	    if (num_of_acks > 3){ //triple dup.
		printf("We have a loss Event/CWNDCUT [Triple Dup] at time: [%ld.%06ld] seq:[%u], num_of_acks:%u \n",
		       time.tv_sec, 
		       time.tv_usec, 
		       seq,
		       num_of_acks);
		_loss_events[paint]++;
		acks[paint].insert(seq, -10000);
	    } else { 					
		acks[paint].insert(seq, -10000);
		doubling[paint] = doubling[paint] < 1 ? 1 : doubling[paint] ;
		printf ("We have a loss Event/CWNDCUT [Timeout] of %1.0f, at time:[%ld.%06ld] seq:[%u],num_of_acks : %hd\n",
			(log(doubling[paint])/log(2)), 
			time.tv_sec, 
			time.tv_usec, 
			seq,
			num_of_acks); 
		_loss_events[paint]++;
		prev_diff[paint] = curr_diff;
	    }
	}
    } else { // this is a first time send event
	
	if (_max_seq[paint] < _last_seq[paint]){
	    _max_seq[paint] = _last_seq[paint];
	}
    }	
    
}

void
CalculateFlows::LossInfo::calculate_loss_events2(tcp_seq_t seq, unsigned seqlen, const struct timeval &time, unsigned paint, ToIPFlowDumps *tipfdp)
{
    assert(paint < 2);
    double curr_diff;
    short int num_of_acks = acks[paint].find(seq);
    short int  num_of_rexmt = rexmt[paint].find(seq);
    short int possible_loss_event=0; //0 for loss event 1 for possible loss event
    //printf("seq:%u ,rexmt: %d\n",seq , num_of_rexmt);
    if ( ((seq+1) < _max_seq[paint]) && ((seq+seqlen) > max_ack[paint]) &&  // Change to +1 for keep alives
	 (seq >= _upper_wind_seq[paint] || ( num_of_rexmt > 0 ))){ // then we have a new event.
	//printf("last_seq[%d]=%u \n",paint,seq );
	timeval time_last_sent  = Search_seq_interval(seq ,seq+seqlen, paint);	
	if (!outoforder_pckt){
	    rexmt[paint].clear(); // clear previous retransmissions (fresh start for this window)
	    StringAccum sa;
	    String direction = paint ? " < " : " > ";
	    if (_max_wind_seq[paint] > (seq+seqlen)){
		sa << "loss" << direction << time_last_sent << " " <<
		    (seq+seqlen) << " " << time <<
		    " " << _max_wind_seq[paint] << " " << num_of_acks;
		tipfdp->add_note(agganno, sa.cc());
		if (gnuplot){
		    outfileg[paint+4] = fopen(outfilenameg[paint+4].cc(), "a");
		    fprintf(outfileg[paint+4],"%f %.1f %f %.1f\n",
			    timeadd(time,time_last_sent)/2.,
			    (_max_wind_seq[paint]+seq+seqlen)/2.,
			    timesub(time,time_last_sent)/2.,
			    (_max_wind_seq[paint]-seq-seqlen)/2.); 
		    if (fclose(outfileg[paint+4])){ 
			click_chatter("error closing file!");
		    }	
		}
	    } else {
		possible_loss_event = 1; // possible loss event
		sa << "ploss" << direction << time_last_sent << " " << seq << " " << time <<
		    " " << seqlen << " " << num_of_acks  ;
		tipfdp->add_note(agganno, sa.cc());
		
		if (gnuplot){
		    outfileg[paint+6] = fopen(outfilenameg[paint+6].cc(), "a");
		    fprintf(outfileg[paint+6],"%f %.1f %f %.1f\n",
			    timeadd(time,time_last_sent)/2.,
			    (double)(seq+seqlen/2.),
			    timesub(time,time_last_sent)/2.,
			    seqlen/2.); 
		    if (fclose(outfileg[paint+6])){ 
			click_chatter("error closing file!");
		    }
		}	
	    }						
	    if (prev_diff[paint] == 0){ //first time
		prev_diff[paint] = timesub(time, time_last_sent);
		curr_diff = prev_diff[paint];
	    } else {
		prev_diff[paint] = prev_diff[paint] < 0.000001 ? 0.000001 : prev_diff[paint];															
		curr_diff = timesub(time,time_last_sent);
		if (( doubling[paint] == 32) && (fabs(1-curr_diff/prev_diff[paint]) < 0.1)){
		    printf("Doubling threshold reached %ld.%06ld \n",time.tv_sec,time.tv_sec);
		} else {
		    if ((fabs(2.-curr_diff/prev_diff[paint]) < 0.1) && (!(num_of_acks > 3))){
			if (doubling[paint] < 1){
			    doubling[paint] = prev_doubling[paint];
			}
			doubling[paint] = 2*doubling[paint];
		    }
		    if ((fabs(2.-curr_diff/prev_diff[paint]) > 0.1) && (!(num_of_acks > 3))){
			prev_doubling[paint] = doubling[paint];
			doubling[paint] = 0;
		    }
		}
	    }					
	    
	    if (num_of_acks > 3){ //triple dup.
		if (!possible_loss_event){
		    printf("We have a loss Event/CWNDCUT [Triple Dup] in flow %u at time: [%ld.%06ld] seq:[%u], num_of_acks:%u \n",
			   agganno,
			   time.tv_sec, 
			   time.tv_usec, 
			   seq,
			   num_of_acks);
		    _loss_events[paint]++;
		} else {
		    printf("We have a POSSIBLE loss Event/CWNDCUT [Triple Dup] in flow %u at time: [%ld.%06ld] seq:[%u], num_of_acks:%u \n",
			   agganno,
			   time.tv_sec, 
			   time.tv_usec, 
			   seq,
			   num_of_acks);
		    _p_loss_events[paint]++;
		}
		//	fprintf(outfileg[paint+4],"%ld.%06ld %u\n",
		//			time.tv_sec,time.tv_usec,_max_seq[paint]); 
		acks[paint].insert(seq, -10000);
	    } else { 					
		acks[paint].insert(seq, -10000);
		doubling[paint] = doubling[paint] < 1 ? 1 : doubling[paint] ;
		if (!possible_loss_event){
		    printf ("We have a loss Event/CWNDCUT [Timeout] of %1.0f in flow %u, at time:[%ld.%06ld] seq:[%u],num_of_acks : %hd\n",
			    (log(doubling[paint])/log(2)), 
			    agganno,
			    time.tv_sec, 
			    time.tv_usec, 
			    seq,
			    num_of_acks); 
		    _loss_events[paint]++;
		} else{
		    printf ("We have a POSSIBLE loss Event/CWNDCUT [Timeout] of %1.0f in flow %u, at time:[%ld.%06ld] seq:[%u],num_of_acks : %hd\n",
			    (log(doubling[paint])/log(2)), 
			    agganno,
			    time.tv_sec, 
			    time.tv_usec, 
			    seq,
			    num_of_acks); 
		    _p_loss_events[paint]++;
		}
		//	fprintf(outfileg[paint+4],"%ld.%06ld %u\n",time.tv_sec,time.tv_usec,seq); 	
		//	prev_diff[paint] = curr_diff;
	    }
	    _max_wind_seq[paint] = seq; //reset the maximum sequence transmitted in this window
	    if (_max_seq[paint] > _upper_wind_seq[paint]){
		//printf("%u:%u",_last_wind_mseq[paint],_max_seq[paint]);
		_upper_wind_seq[paint] = _max_seq[paint]; // the window for this event loss
	    }
	}
    }
    
}

void
CalculateFlows::LossInfo::calculate_loss(tcp_seq_t seq, unsigned block_size, unsigned paint)
{
    assert(paint < 2);
    
    if (((_max_seq[paint]+1) < seq) && (_max_seq[paint] > 0)){
	printf("Possible gap in Byte Sequence flow %u:%u %u - %u\n",agganno,paint,_max_seq[paint],seq);
    }
    if ((seq+1) < _max_seq[paint] && !outoforder_pckt){  // we do a retransmission  (Bytes are lost...)
	MapS &m_rexmt = rexmt[paint];
	//	printf("ok:%u:%u",seq,_max_seq[paint]);
	m_rexmt.insert(seq, m_rexmt.find(seq)+1 );					
	if (seq + block_size < _max_seq[paint]){ // are we transmiting totally new bytes also?
	    _bytes_lost[paint] = _bytes_lost[paint] + block_size;
	    
	} else { // we retransmit something old but partial
	    _bytes_lost[paint] = _bytes_lost[paint] + (_max_seq[paint]-seq);
	    _last_seq[paint] = seq+block_size;  // increase our last sequence to cover new data
	    
	    if (_max_seq[paint] < _last_seq[paint]){
		_max_seq[paint] = _last_seq[paint];
	    }
	    if (_max_wind_seq[paint] < _last_seq[paint]){
		_max_wind_seq[paint] = _last_seq[paint];
	    }
	}
	_packets_lost[paint]++;
    } else { // this is a first time send event
	// no loss normal data transfer
	outoforder_pckt = 0; //reset the indicator
	_last_seq[paint] = seq+block_size;  // increase our last sequence to cover new data
	
	if (_max_seq[paint] < _last_seq[paint]){
	    _max_seq[paint] = _last_seq[paint];
	}
	if (_max_wind_seq[paint] < _last_seq[paint]){
	    _max_wind_seq[paint] = _last_seq[paint];
	}
	
    }	
    
}


// CALCULATEFLOWS PROPER

CalculateFlows::CalculateFlows()
    : Element(1, 1)
{
    MOD_INC_USE_COUNT;
}

CalculateFlows::~CalculateFlows()
{
    MOD_DEC_USE_COUNT;
    for (MapLoss::Iterator iter = loss_map.first(); iter; iter++){
	LossInfo *losstmp = const_cast<LossInfo *>(iter.value());
	delete losstmp;
    }
}

void
CalculateFlows::notify_noutputs(int n)
{
    set_noutputs(n <= 1 ? 1 : 2);
}

int 
CalculateFlows::configure(Vector<String> &conf, ErrorHandler *errh)
{
    if (cp_va_parse(conf, this, errh,
                    cpElement,  "AggregateFlows element pointer (notifier)", &af,
		    cpElement,  "ToIPFlowDumps element pointer (notifier)", &tipfd,
		    cpOptional,
		    cpFilename, "filename for output flow1",&outfilename[0],
		    cpFilename, "filename for output flow2",&outfilename[1],
		    0) < 0)
        return -1;
    af->add_listener(this); // this is a handler to AggregateFlows Element
    return 0;

}
int
CalculateFlows::initialize(ErrorHandler *)
{
    return 0;
}

Packet *
CalculateFlows::simple_action(Packet *p)
{
    const click_ip *iph = p->ip_header();
    if (!iph || (iph->ip_p != IP_PROTO_TCP && iph->ip_p != IP_PROTO_UDP) // Sanity check copied from Aggregateflows
	|| !IP_FIRSTFRAG(iph)
	|| p->transport_length() < (int)sizeof(click_udp)) {
	checked_output_push(1, p);
	return 0;
    }
  
    unsigned aggp = AGGREGATE_ANNO(p);
    unsigned paint = PAINT_ANNO(p); // Our Paint
    unsigned cpaint = paint^1;	 // and its complement
  
    IPAddress src(iph->ip_src.s_addr); //for debugging
    IPAddress dst(iph->ip_dst.s_addr); //for debugging
  
    int ip_len = ntohs(iph->ip_len);
    int payload_len = ip_len - (iph->ip_hl << 2);
    timeval ts = p->timestamp_anno(); //the packet timestamp	
    
    StringAccum sa; // just for debugging
    sa << p->timestamp_anno() << ": ";
    sa << "ttl " << (int)iph->ip_ttl << ' ';
    sa << "tos " << (int)iph->ip_tos << ' ';
    sa << "length " << ip_len << ' ';
	 
    switch (iph->ip_p) { 
	 
      case IP_PROTO_TCP: {
	  // if (aggp == 1765){
	  int type = 0;// 0 ACK or 1 DACK
	  loss = loss_map.find(aggp);
	  MapS &m_acks = loss->acks[cpaint];
	  MapT &m_tbfirst = loss->time_by_firstseq[paint];
	  MapT &m_tblast = loss->time_by_lastseq[paint];
	  MapInterval &m_ibtime = loss->inter_by_time[paint];
	  
	   
	  const click_tcp *tcph = p->tcp_header(); 
	  tcp_seq_t seq = ntohl(tcph->th_seq); // sequence number of the current packet
	  tcp_seq_t ack = ntohl(tcph->th_ack); // Acknoledgement sequence number
	  unsigned win = ntohs(tcph->th_win); // requested window size
	  unsigned seqlen = payload_len - (tcph->th_off << 2); // sequence length 
	  int ackp = tcph->th_flags & TH_ACK; // 1 if the packet has the ACK bit

	  if ( (loss->init_time.tv_usec == 0) && (loss->init_time.tv_sec == 0) ){ 
	      unsigned short sport = ntohs(tcph->th_sport);
	      unsigned short dport = ntohs(tcph->th_dport);
	      String outfilenametmp;
	      outfilenametmp = loss->outputdir+"/flowhnames.info";
	      loss->outfile[4] = fopen(outfilenametmp.cc(), "w");		 
	      fprintf(loss->outfile[4],"flow%u: %s:%d <-> %s:%d'\n",aggp,src.unparse().cc(),sport,dst.unparse().cc(),dport);
	      fclose(loss->outfile[4]);
	      loss->init_time = ts;
	      ts.tv_usec = 1;
	      ts.tv_sec = 0;
	  } else {
	      ts.tv_usec++;
	      ts = ts - loss->init_time;
	  }
	  //printf("%u,%u[%ld.%06ld]:[%ld.%06ld] \n",aggp,paint,loss->init_time.tv_sec,loss->init_time.tv_usec,ts.tv_sec,ts.tv_usec);
	   
	  // converting the Sequences from Absolute to Relative
	  if (!loss->init_seq[paint]) { //first time case 
	      loss->init_seq[paint] = seq;
	      seq = loss->has_syn[paint];
	  } else {
	      if (seq < loss->init_seq[paint]){//hmm we may have a "wrap around" case
		  seq = seq + (UINT_MAX - loss->init_seq[paint]);
	      } else { //normal case no "wrap around"
		  seq = seq - loss->init_seq[paint];
	      }
	  }
	  
	  if (tcph->th_flags & TH_SYN){ // Is this a SYN packet?
	      loss->has_syn[paint] = 1;
	      return p;
	  }
	  if (tcph->th_flags & TH_FIN){	// Is this a FIN packet?
	      loss->has_fin[paint] = 1;
	      return p;
	  }
	  if (seqlen > 0) {
	      type=1;
	      loss->calculate_loss_events2(seq,seqlen,ts,paint, tipfd); //calculate loss if any
	      loss->calculate_loss(seq, seqlen, paint); //calculate loss if any
	      if (loss->eventfiles) {
		  print_send_event(paint, ts, seq, (seq+seqlen));
	      }
	      if (loss->gnuplot) {
		  gplotp_send_event(paint, ts, (seq+seqlen));
	      }
	      m_tbfirst.insert(seq, ts);
	      m_tblast.insert((seq+seqlen), ts);
	      TimeInterval ti;
	      ti.start_byte = seq;
	      ti.end_byte = seq+seqlen;
	      ti.time = ts;
	      m_ibtime.insert(loss->packets(paint),ti);
	  }
	  
	  if (ackp){ // check for ACK and update as necessary
	      // converting the Sequences from Absolute to Relative (we need
	      // that for acks also!)
	      if (!loss->init_seq[cpaint]) { //first time case
		  loss->init_seq[cpaint] = ack;
		  ack = loss->has_syn[cpaint];
	      } else {
		  if (ack < loss->init_seq[cpaint]){//hmm we may have a "wrap around" case
		      ack = ack  + (UINT_MAX - loss->init_seq[cpaint]);
		  } else { //normal case no "wrap around"
		      ack = ack - loss->init_seq[cpaint];
		  }
	      }
	      
	      if (loss->max_ack[cpaint] < ack){
		  loss->max_ack[cpaint] = ack;
	      }
	      
	      loss->set_last_ack(ack,cpaint);
	      m_acks.insert(ack, m_acks.find(ack)+1 );
	      if (loss->eventfiles){
		  print_ack_event(cpaint, type, ts, ack);	
	      }
	      if (loss->gnuplot){
		  gplotp_ack_event(cpaint, type, ts, ack);	
		  //printf("[%u, %u]",ack,m_acks[ack]);
	      }
	  }
	  
	  /* for (MapS::Iterator iter = m_acks.first(); iter; iter++){
	     short int *value = const_cast<short int *>(&iter.value());
	     const unsigned *temp = &m_acks.key_of_value(value);
	     printf("%u:%hd \n",*temp,*value);
	     
	     }
	     timeval tv2 = loss->Search_seq_interval(27 ,600, paint);
	     printf("RESULT:[%ld.%06ld]: %u - %u \n",tv2.tv_sec, tv2.tv_usec,27, 600);*/
	   
	  loss->inc_packets(paint); // Increment the packets for this flow (forward or reverse)
	  loss->set_total_bytes((loss->total_bytes(paint)+seqlen),paint); //Increase the number bytes transmitted
	  //  printf("[%u] %u:%u:%u\n",paint,loss->packets(paint),loss->total_bytes(paint),seq);
	  // }  
	  break;
      }
      
      case IP_PROTO_UDP: { // For future use...
	  const click_udp *udph = p->udp_header();
	  unsigned short srcp = ntohs(udph->uh_sport);
	  unsigned short dstp = ntohs(udph->uh_dport);
	  unsigned len = ntohs(udph->uh_ulen);
	  sa << src << '.' << srcp << " > " << dst << '.' << dstp << ": udp " << len;
	  printf("%s",sa.cc());
	  break;
      }
	
      default :{ // All other packets are not processed
	  printf("The packet is not a TCP or UDP");
	  sa << src << " > " << dst << ": ip-proto-" << (int)iph->ip_p;
	  printf("%s",sa.cc());
	  break;
	  
      }
    }
    
    /*if (aggp == 1){
      printf("Timestamp Anno = [%ld.%06ld] " , ts.tv_sec,ts.tv_usec);
      printf("Sequence Number =[%u,%u]", loss->last_seq(0),loss->last_seq(1));
      printf("ACK Number =[%u,%u]", loss->last_ack(0),loss->last_ack(1));
      printf("Total Packets =[%u,%u]", loss->packets(0),loss->packets(1));
      printf("Total Bytes =[%u,%u]", loss->total_bytes(0),loss->total_bytes(1));
      printf("Total Bytes Lost=[%u,%u]\n\n", loss->bytes_lost(0),loss->bytes_lost(1));
      }*/
    return p;
}

void
CalculateFlows::
print_ack_event(unsigned paint, int type, timeval tstamp, unsigned ackseq)
{
	
	loss->outfile[paint] = fopen(loss->outfilename[paint].cc(), "a");
	if (type == 0){	
		fprintf(loss->outfile[paint],"%ld.%06ld PACK %u\n",tstamp.tv_sec,tstamp.tv_usec,ackseq); 
	}
	else {
		fprintf(loss->outfile[paint],"%ld.%06ld ACK %u\n",tstamp.tv_sec,tstamp.tv_usec,ackseq); 
	}
	//fflush(loss->outfile[paint]);
	if (fclose(loss->outfile[paint])){ 
   		click_chatter("error closing file!");
	}
	
}

void
CalculateFlows::
print_send_event(unsigned paint, timeval tstamp, unsigned startseq, unsigned endseq)
{
	loss->outfile[paint] = fopen(loss->outfilename[paint].cc(), "a");
	fprintf(loss->outfile[paint],"%ld.%06ld SEND %u %u\n",tstamp.tv_sec,tstamp.tv_usec,startseq,endseq); 
//	fflush(loss->outfile[paint]);
	if (fclose(loss->outfile[paint])){ 
   		click_chatter("error closing file!");
	}

}

void
CalculateFlows::
gplotp_ack_event(unsigned paint, int type, timeval tstamp, unsigned ackseq)
{
	if (type == 0){	
		loss->outfileg[paint] = fopen(loss->outfilenameg[paint].cc(), "a");
		fprintf(loss->outfileg[paint],"%ld.%06ld %u\n",tstamp.tv_sec,tstamp.tv_usec,ackseq); 
		if (fclose(loss->outfileg[paint])){ 
   			click_chatter("error closing file!");
		}
	}
	else{
		loss->outfileg[paint+8] = fopen(loss->outfilenameg[paint+8].cc(), "a");
		fprintf(loss->outfileg[paint+8],"%ld.%06ld %u\n",tstamp.tv_sec,tstamp.tv_usec,ackseq); 
		if (fclose(loss->outfileg[paint+8])){ 
   			click_chatter("error closing file!");
		}
	}
}

void
CalculateFlows::
gplotp_send_event(unsigned paint, timeval tstamp, unsigned endseq)
{
	loss->outfileg[paint+2] = fopen(loss->outfilenameg[paint+2].cc(), "a");
	fprintf(loss->outfileg[paint+2],"%ld.%06ld %u\n",tstamp.tv_sec,tstamp.tv_usec,endseq); 
	//fflush(loss->outfileg[paint+2]);
	if (fclose(loss->outfileg[paint+2])){ 
   		click_chatter("error closing file!");
	}

}

void 
CalculateFlows::
aggregate_notify(uint32_t aggregate_ID,
                 AggregateEvent event /* can be NEW_AGG or DELETE_AGG */,
                 const Packet * /* null for DELETE_AGG */){
//	printf("ok1 ---->%d %d\n", aggregate_ID, event);
	if (event == NEW_AGG){	
		LossInfo *tmploss = new LossInfo(outfilename,aggregate_ID, 1, 1) ;
		loss_map.insert(aggregate_ID, tmploss);
	}
	
	else if (event == DELETE_AGG){
		LossInfo *tmploss = loss_map.find(aggregate_ID);	
		loss_map.remove(aggregate_ID);
		delete tmploss;
	}
}	


ELEMENT_REQUIRES(userlevel)
EXPORT_ELEMENT(CalculateFlows)


#include <click/bighashmap.cc>
