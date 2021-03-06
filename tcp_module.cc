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
void make_packet(Packet &p, ConnectionToStateMapping<TCPState> &ConnState, TYPE HeaderType, int size, bool timedOut);
void handle_sock(MinetHandle &mux, MinetHandle &sock, ConnectionList<TCPState> &clist);
int send_data(const MinetHandle &mux, ConnectionToStateMapping<TCPState> &ConnState, Buffer data, bool timedOut);
void handle_timeout(const MinetHandle &mux, ConnectionList<TCPState>::iterator cs, ConnectionList<TCPState> &clist);

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
				handle_packet(mux, sock, clist);
		    }

		    if (event.handle == sock) {
				handle_sock(mux, sock, clist);
		    }
		}
		if (event.eventtype == MinetEvent::Timeout) {
		    ConnectionList<TCPState>::iterator cs = clist.FindEarliest();
			if (cs != clist.end()) {
				if (Time().operator > ((*cs).timeout)) {
					handle_timeout(mux, cs, clist);
				}
			}
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

	if (cs == clist.end()) {
		std::cout << "Connect. not in ConnectionList.\n";
	}

	//Get information from packet via headers
	tcpHead.GetFlags(flags);
	tcpHead.GetAckNum(ackNum);
	tcpHead.GetSeqNum(seqNum);
	tcpHead.GetWinSize(winSize);
	tcpHead.GetUrgentPtr(urgent);

	Packet pack;

	printf("\nCurrent State: %d\n",curr_state);
	// 0 - closed  1 - listen  2 - synrcv  3 - synsent  4 - synsent1  5 - established  6 senddata  7 - closewait  8 - finwait1  9 - closing  10 - lastacked  11 - finwait2  12 - timewait
	switch(curr_state){
		case LISTEN: {
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
			    	make_packet(pack, *cs, SYNACK, 0, false);
			    	MinetSend(mux, pack);
			}
			break;
		}
		case SYN_RCVD: {
			if(IS_ACK(flags)){
			    	//printf("3 Way Handshake Complete!");  // temporary test
			    	cs->state.SetState(ESTABLISHED);
			    	cs->state.SetLastAcked(ackNum);
			    	cs->state.SetSendRwnd(winSize);
			    	cs->state.last_sent = cs->state.last_sent + 1;

			    	// timeout (set for out SYNACK) is turned off because we got an ACK
			    	cs->bTmrActive = false;

			    	// Tell the other modules that the connection was created
			    	static SockRequestResponse * write = NULL;
			    	write = new SockRequestResponse(WRITE, cs->connection,
			    	                                data, 0, EOK);
			    	MinetSend(sock, *write);
			    	delete write;
			}
			break;
		}
		case SYN_SENT: {
			if( (IS_SYN(flags) && IS_ACK(flags)) ){
				cs->state.SetSendRwnd(winSize);
				cs->state.SetLastRecvd(seqNum + 1);
				cs->state.last_acked = ackNum;
				cs->state.last_sent = cs->state.last_sent + 1;
				make_packet(pack, *cs, ACK, 0, false);
				MinetSend(mux, pack);
				cs->state.SetState(ESTABLISHED);
				cs->bTmrActive = false;
				SockRequestResponse write (WRITE, cs->connection, data, 0, EOK);
				MinetSend(sock, write);
			}
			break;
		}
		case ESTABLISHED: {
			if (IS_FIN(flags)) {
				printf("FIN!!!!!!!!!!!!!!!!!!");
				cs->state.SetState(CLOSE_WAIT);
				cs->state.SetLastRecvd(seqNum + 1);

				cs->bTmrActive = true;
				cs->timeout=Time() + 8;

				make_packet(pack, *cs, ACK, 0, false);
				MinetSend(mux, pack);

				Packet p;
				cs->state.SetState(LAST_ACK);
				make_packet(p, *cs, FIN, 0, false);
				MinetSend(mux, p);
			}

			if (IS_PSH(flags) || content_len != 0) {
				cs->state.SetSendRwnd(winSize);
				cs->state.last_recvd = seqNum + data.GetSize();
				cs->state.RecvBuffer.AddBack(data);
				SockRequestResponse write(WRITE, cs->connection,
				                           cs->state.RecvBuffer,
				                           cs->state.RecvBuffer.GetSize(),
				                           EOK);
				MinetSend(sock, write);

				cs->state.RecvBuffer.Clear();

				make_packet(pack, *cs, ACK, 0, false);
				MinetSend(mux, pack);
			}
			if (IS_ACK(flags)) {
				if (ackNum >= cs->state.last_acked) {
					int data_acked = ackNum - cs->state.last_acked;
					cs->state.last_acked = ackNum;
					cs->state.SendBuffer.Erase(0, data_acked);

					cs->bTmrActive = false;
				}
				if (cs->state.GetState() == LAST_ACK) {
					cs->state.SetState(CLOSED);
					clist.erase(cs);
				}
			}
			break;
		}
		case LAST_ACK: {
			if (IS_ACK(flags)) {
				cs->state.SetState(CLOSED);
				clist.erase(cs);
			}
			break;
		}
		case FIN_WAIT1: {
			if (IS_ACK(flags)) {
				cs->state.SetState(FIN_WAIT2);
			}
			if (IS_FIN(flags)) {
				cs->state.SetState(TIME_WAIT);
				cs->state.SetLastRecvd(seqNum + 1);
				make_packet(pack, *cs, ACK, 0, false);
				cs->bTmrActive = true;
				cs->timeout = Time() + (2 * MSL_TIME_SECS);
				MinetSend(mux, pack);
			}
			break;
		}
		case FIN_WAIT2: {
			if (IS_FIN(flags)) {
				cs->state.SetState(TIME_WAIT);
				cs->state.SetLastRecvd(seqNum + 1);
				make_packet(pack, *cs, ACK, 0 ,false);
				cs->bTmrActive = true;
				cs->timeout = Time() + (2*MSL_TIME_SECS);
				MinetSend(mux, pack);
			}
			break;
		}
		case TIME_WAIT: {
			if (IS_FIN(flags)) {
				cs->state.SetLastRecvd(seqNum + 1);
				cs->timeout = Time() + 5;
				make_packet(pack, *cs, ACK, 0 ,false);
				MinetSend(mux, pack);
			}
			break;
		}
	}

	std::cout << ipHead << "\n";
	std::cout << tcpHead << "\n";
}

void make_packet(Packet &p, ConnectionToStateMapping<TCPState> &ConnState, TYPE HeaderType, int size, bool timedOut){

	IPHeader ipHead;
	TCPHeader tcpHead;
	unsigned char flags = 0;
	size += TCP_HEADER_BASE_LENGTH + IP_HEADER_BASE_LENGTH;

	ipHead.SetSourceIP(ConnState.connection.src);
	ipHead.SetDestIP(ConnState.connection.dest);
	ipHead.SetTotalLength(size);
	ipHead.SetProtocol(IP_PROTO_TCP);
	p.PushFrontHeader(ipHead);

	tcpHead.SetSourcePort(ConnState.connection.srcport, p);
	tcpHead.SetDestPort(ConnState.connection.destport, p);
	tcpHead.SetHeaderLen(5, p);
	tcpHead.SetAckNum(ConnState.state.GetLastRecvd(), p);
	tcpHead.SetWinSize(ConnState.state.GetN(), p);
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

	std::cout << "\nLast Acked(): " << ConnState.state.GetLastAcked() << "\n";
	std::cout << "\nSeqNum  +  1: " << ConnState.state.GetLastSent() + 1 << "\n";

	if(timedOut){
		tcpHead.SetSeqNum(ConnState.state.GetLastAcked(), p);
	}else{
		tcpHead.SetSeqNum(ConnState.state.GetLastSent() + 1, p);
	}

	tcpHead.RecomputeChecksum(p);
	p.PushBackHeader(tcpHead);
}

void handle_sock(MinetHandle &mux, MinetHandle &sock, ConnectionList<TCPState> &clist) {
	SockRequestResponse request;
	SockRequestResponse request1;

	MinetReceive(sock, request);
	Packet p;
	ConnectionList<TCPState>::iterator cs = clist.FindMatching(request.connection);

	if (cs == clist.end()) {
		switch (request.type) {

			case CONNECT: {
					TCPState state(1, SYN_SENT, 5);
					ConnectionToStateMapping<TCPState> ConnState(request.connection, Time()+2, state, true);
					ConnState.state.last_acked = 0;
					clist.push_back(ConnState);
					make_packet(p, ConnState, SYN, 0, false);
					cs->bTmrActive = true;
					cs->timeout=Time() + 2;
					MinetSend(mux, p);
					request1.type = STATUS;
					request1.connection = request.connection;
					request1.bytes = 0;
					request1.error = EOK;
					MinetSend(sock, request1);
					break;
				      }

			 case ACCEPT: {
					TCPState state(1, LISTEN, 5);
					ConnectionToStateMapping<TCPState> ConnState(request.connection, Time(), state, false);
					clist.push_back(ConnState);
					request1.type = STATUS;
					request1.bytes = 0;
					request1.connection = request.connection;
					request1.error = EOK;
					MinetSend(sock, request1);
					break;
				      }

			case FORWARD:{
					break;
				     }

		 case STATUS: {
					break;
				     }

	  case WRITE: {
				request1.type = STATUS;
				request1.connection = request.connection;
				request1.bytes = 0;
				request1.error = ENOMATCH;
				MinetSend(sock, request1);
				break;
					 }

			 case CLOSE: {
					request1.type = STATUS;
					request1.connection = request.connection;
					request1.bytes = 0;
					request1.error = ENOMATCH;
					MinetSend(sock, request1);
					break;
				      }

			default: {
					break;
				 }
		}
	} else {
		unsigned int state = cs->state.GetState();
		Buffer buf;
		switch (request.type) {
			case CONNECT: {
					break;
				      }

			case WRITE: {
					if (state == ESTABLISHED) {
					  if (cs->state.SendBuffer.GetSize() + request.data.GetSize()
						   > cs->state.TCP_BUFFER_SIZE) {
						   request1.type = STATUS;
						   request1.connection = request.connection;
						   request1.bytes = 0;
						   request1.error = EBUF_SPACE;
						   MinetSend(sock, request1);
					  }
					  else {
						  Buffer copy = request.data;
						  cs->bTmrActive = true;
						  cs->timeout=Time() + 8;
						  int return_value = send_data(mux, *cs, copy, false);
						  if (return_value == 0) {
							  request1.type = STATUS;
							  request1.connection = request.connection;
							  request1.bytes = copy.GetSize();
							  request1.error = EOK;
							  MinetSend(sock, request1);
						  }
					  }
					}
					break;
				    }

			case ACCEPT: {
					break;
				     }

			case FORWARD: {
					break;
				      }

			case CLOSE: {
					if (state == ESTABLISHED) {
					  cs->state.SetState(FIN_WAIT1);
					  cs->state.last_acked = cs->state.last_acked + 1;
					  cs->bTmrActive = true; // begin timeout
					  cs->timeout=Time() + 8;
					  make_packet(p, *cs, FIN, 0, false);
					  MinetSend(mux, p);
					  request1.type = STATUS;
					  request1.connection = request.connection;
					  request1.bytes = 0;
					  request1.error = EOK;
					  MinetSend(sock, request1);
					}
					break;
				   }

			case STATUS: {
					break;
				     }

			default:
				break;
		}
	}
}

int send_data(const MinetHandle &mux, ConnectionToStateMapping<TCPState> &ConnState, Buffer data, bool timeOut) {
	  Packet p;
	  unsigned int l;
	  unsigned int rem_bytes; // bytes that remain
	  int i = 0;

	  if (timeOut) {
		  l = 0;
		  rem_bytes = ConnState.state.SendBuffer.GetSize();
	  }
	  else {
		  l = ConnState.state.SendBuffer.GetSize();
		  ConnState.state.SendBuffer.AddBack(data);
		  rem_bytes = data.GetSize();
	  }

	  while (rem_bytes) {
		  unsigned int send_bytes = min(rem_bytes, TCP_MAXIMUM_SEGMENT_SIZE);
		  char str_data[send_bytes + 1];
		  int data_size = ConnState.state.SendBuffer.GetData(str_data, send_bytes, l);
		  str_data[data_size + 1] = '\0';
		  Buffer send_buf;
		  send_buf.SetData(str_data, data_size, 0);
		  cerr << "Buffer Version: \n" << send_buf;
		  p = send_buf.Extract(0, data_size);

		  if (i > 0) {
		  	make_packet(p, ConnState, PSHACK, data_size, false);
		  }
		  else {
			make_packet(p, ConnState, PSHACK, data_size, timeOut);
		  }

		  MinetSend(mux, p);
		  ConnState.state.last_sent = ConnState.state.last_sent + send_bytes;
		  rem_bytes = rem_bytes - send_bytes;
		  l += data_size;
		  i++;
	  }

  return rem_bytes;
}

void handle_timeout(const MinetHandle &mux, ConnectionList<TCPState>::iterator cs, ConnectionList<TCPState> &list) {

	Packet p;
	Buffer data;
	unsigned int curr_state = cs->state.GetState();

	switch (curr_state) {
		case SYN_SENT: {
			make_packet(p, *cs, SYN, 0, false);
			MinetSend(mux, p);
			break;
		}
		case ESTABLISHED: {
		  data = cs->state.SendBuffer;
		  send_data(mux, *cs, data, true);
		  break;
		}
		case SYN_RCVD: {
			make_packet(p, *cs, SYNACK, 0, true);
			MinetSend(mux, p);
			break;
		}
		case LAST_ACK: {
			make_packet(p, *cs, FIN, 0, true);
			MinetSend(mux, p);
			break;
		}
		case TIME_WAIT: {
			cs->state.SetState(CLOSED);
			list.erase(cs);
		}
		case FIN_WAIT1: {

		}
		default:
			break;
	}
}
