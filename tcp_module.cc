// You will build this in project part B - this is merely a
// stub that does nothing but integrate into the stack

// For project parts A and B, an appropriate binary will be 
// copied over as part of the build process



#include <sys/time.h>
#include <sys/types.h>
#include <unistd.h>

#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <errno.h>

#include <sstream>
#include <iostream>

#include "Minet.h"
#include "tcpstate.h"
#include "tcp.h"
#include "ip.h"

using namespace std;

#define WORD_SIZE 4

enum TYPE {
	SYN, SYNACK, ACK, PSHACK,
	FIN, FINACK, RESET
};

void handle_packet(MinetHandle &mux, MinetHandle &sock, ConnectionList<TCPState> &clist);
void make_packet(Packet &p, ConnectionToStateMapping<TCPState> &CTSM, TYPE HeaderType, int size, bool timedOut);

int main(int argc, char * argv[]) {

    MinetHandle mux;
    MinetHandle sock;
    
    ConnectionList<TCPState> clist;

    MinetInit(MINET_TCP_MODULE);

    mux = MinetIsModuleInConfig(MINET_IP_MUX) ?  
	MinetConnect(MINET_IP_MUX) : 
	MINET_NOHANDLE;
    
    sock = MinetIsModuleInConfig(MINET_SOCK_MODULE) ? 
	MinetAccept(MINET_SOCK_MODULE) : 
	MINET_NOHANDLE;

    if ( (mux == MINET_NOHANDLE) && 
	 (MinetIsModuleInConfig(MINET_IP_MUX)) ) {

		MinetSendToMonitor(MinetMonitoringEvent("Can't connect to ip_mux"));

		return -1;
    }

    if ( (sock == MINET_NOHANDLE) && 
	 (MinetIsModuleInConfig(MINET_SOCK_MODULE)) ) {

		MinetSendToMonitor(MinetMonitoringEvent("Can't accept from sock_module"));

		return -1;
    }
    
    cerr << "tcp_module STUB VERSION handling tcp traffic.......\n";

    MinetSendToMonitor(MinetMonitoringEvent("tcp_module STUB VERSION handling tcp traffic........"));

    MinetEvent event;
    double timeout = 1;

    while (MinetGetNextEvent(event, timeout) == 0) {

		if ((event.eventtype == MinetEvent::Dataflow) && 
		    (event.direction == MinetEvent::IN)) {
			
		    if (event.handle == mux) {
				// ip packet has arrived!
				handle_packet(mux, sock, clist);
		    }

		    if (event.handle == sock) {
			// socket request or response has arrived
		    }
		}

		if (event.eventtype == MinetEvent::Timeout) {
		    // timeout ! probably need to resend some packets
		}

    }

    MinetDeinit();

    return 0;
}

void handle_packet(MinetHandle &mux, MinetHandle &sock, ConnectionList<TCPState> &clist) {

	printf("IP Address Arrival: %s", mux);

	Packet p;
	unsigned short content_len;
	unsigned char TCPHeadLen;
	unsigned char IPHeadLen;
	unsigned int curr_state;
	unsigned char flags;
	unsigned int ackNum;
	unsigned int seqNum;
	unsigned short winSize;
	unsigned short urgent;

	MinetReceive(mux,p);
	p.ExtractHeaderFromPayload<TCPHeader>(TCPHeader::EstimateTCPHeaderLength(p));
	TCPHeader tcpHead = p.FindHeader(Headers::TCPHeader);
	IPHeader ipHead = p.FindHeader(Headers::IPHeader);

	if(!tcpHead.IsCorrectChecksum(p)){
		printf("CHECKSUM ERROR!");
		return;
	}

	Connection c;
	ipHead.GetDestIP(c.src);
	ipHead.GetSourceIP(c.dest);
	ipHead.GetProtocol(c.protocol);
	tcpHead.GetDestPort(c.srcport);
	tcpHead.GetSourcePort(c.destport);

	ConnectionList<TCPState>::iterator cs = clist.FindMatching(c);

	if (cs != clist.end()) {
	  printf("HERE!!!!!!!!!!!!!!!!!!!!");
	  tcpHead.GetHeaderLen(TCPHeadLen);
	  ipHead.GetHeaderLength(IPHeadLen);
	  ipHead.GetTotalLength(content_len);
	  content_len -= WORD_SIZE * (TCPHeadLen + IPHeadLen);

	  Buffer data = p.GetPayload().ExtractFront(content_len);
	  SockRequestResponse write(WRITE,
			    (*cs).connection,
			    data,
			    content_len,
			    EOK);

		for (int i = 0; i < content_len; i++){
		  putc(isprint(data[i]) ? data[i] : '.' , stdout);
		}

		curr_state = cs->state.GetState();
	} else {
		std::cout << "Connection is not in the ConnectionList.\n";
	}

	//Get information from packet via headers
	tcpHead.GetFlags(flags);
	tcpHead.GetAckNum(ackNum);
	tcpHead.GetSeqNum(seqNum);
	tcpHead.GetWinSize(winSize);
	tcpHead.GetUrgentPtr(urgent);

	Packet p_send;
	//printf("SWITCH STATEMENT BLOCKED!!!!!!!!!!!!!!!!!!!!");
	//switch(curr_state){
		//case LISTEN:
			if(IS_SYN(flags)){
				printf("SYN RECIEVED!!!!!!!!!!!!!!!!!!!!");
				cs->connection = c;
				cs->state.SetState(SYN_RCVD);
				cs->state.last_acked = cs->state.last_sent;
				cs->state.SetLastRecvd(seqNum + 1);

				// Set timeout and send SYNACK
				cs->bTmrActive = true;
				cs->timeout=Time() + 8;
				cs->state.last_sent = cs->state.last_sent + 1;
				make_packet(p_send, *cs, SYNACK, 0, false);
				MinetSend(mux, p_send);
			}
			if(IS_ACK(flags)){
				printf("3 Way Handshake Complete!");  // temporary test 
        
        // data expected in first ack, here we print via buffer 
        
        Buffer data = p.GetPayload().ExtractFront(content_len); // UNTESTED 
        for (int i = 0; i < content_len; i++){
    		  putc(isprint(data[i]) ? data[i] : '.' , stdout);
    		}
        
			}

			if(IS_FIN(flags)){
				printf("FIN!");  // temporary test
				// send ack, then send fin (passive close), expect ack in return 
			}
			//break;
	//}

	std::cout << ipHead << "\n";	
	std::cout << tcpHead << "\n";
}

void make_packet(Packet &p, ConnectionToStateMapping<TCPState> &CTSM, TYPE HeaderType, int size, bool timedOut){

	IPHeader ipHead;
	TCPHeader tcpHead;
	unsigned char flags = 0;
	size += TCP_HEADER_BASE_LENGTH + IP_HEADER_BASE_LENGTH;

	ipHead.SetSourceIP(CTSM.connection.src);
	ipHead.SetDestIP(CTSM.connection.dest);
	ipHead.SetTotalLength(size);
	ipHead.SetProtocol(IP_PROTO_TCP);
	p.PushFrontHeader(ipHead);

	tcpHead.SetSourcePort(CTSM.connection.srcport, p);
	tcpHead.SetDestPort(CTSM.connection.destport, p);
	tcpHead.SetHeaderLen(5, p);
	tcpHead.SetAckNum(CTSM.state.GetLastRecvd(), p);
	tcpHead.SetWinSize(CTSM.state.GetN(), p);
	tcpHead.SetUrgentPtr(0, p);

	switch (HeaderType) {
		case SYN:
		  SET_SYN(flags);
		  break;

		case ACK:
		  SET_ACK(flags);
		  break;
		
		case SYNACK:
		  SET_SYN(flags);
		  SET_ACK(flags);
		  break;
		
		case PSHACK:
		  SET_PSH(flags);
		  SET_ACK(flags);
		  break;
		
		case FIN:
		  SET_FIN(flags);
		  break;
		
		case FINACK:
		  SET_FIN(flags);
		  SET_ACK(flags);
		  break;
		
		case RESET:
		  SET_RST(flags);
		  break;
		
		default:
		  break;
	}
	tcpHead.SetFlags(flags, p);

	std::cout << "\nLast Acked(): " << CTSM.state.GetLastAcked() << "\n";
	std::cout << "\nSeqNum  +  1: " << CTSM.state.GetLastSent() + 1 << "\n";

	if(timedOut){
		tcpHead.SetSeqNum(CTSM.state.GetLastAcked(), p);
	}else{
		tcpHead.SetSeqNum(CTSM.state.GetLastSent() + 1, p);
	}

	tcpHead.RecomputeChecksum(p);
	p.PushBackHeader(tcpHead);
}