#ifndef _Q4SSERVER_H_
#define _Q4SSERVER_H_

#include <time.h> // to use clock
#include <stdio.h> // to use printf
#include <stdlib.h> // to use NULL
#include <string.h>  // to use strcpy, strcat, etc
#include <errno.h> // to describe errors
#include <pthread.h> // to create and manage threads

// To create and manage connection
#include <sys/socket.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <netinet/in.h>
#include <netdb.h>

// To create and execute state machine
#include "fsm.h"
#include "fsm.c"

// To detect pressed keys and read them
#include "kbhit.h"
#include "kbhit.c"

#define CLK_MS 10 // state machine update period

// FLAGS of the system
#define FLAG_NEW_CONNECTION       0x01
#define FLAG_RECEIVE_BEGIN        0x02
#define FLAG_RECEIVE_READY0    		0x04
#define FLAG_RECEIVE_PING        	0x08
#define FLAG_TEMP_PING_0          0x10
#define FLAG_RECEIVE_OK     			0x20
#define FLAG_ALERT          			0x40
#define FLAG_RECEIVE_READY1  			0x80
#define FLAG_TEMP_BWIDTH       		0x100
#define FLAG_RECEIVE_BWIDTH    	  0x200
#define FLAG_TEMP_PING_2		      0x400
#define FLAG_RECEIVE_READY2   		0x800
#define FLAG_RECOVER              0x1000
#define FLAG_RECEIVE_CANCEL       0x2000
#define FLAG_RELEASED             0x4000

// Q4S open ports
#define HOST_PORT_TCP 56001
#define HOST_PORT_UDP 56000
#define CLIENT_PORT_TCP 55001
#define CLIENT_PORT_UDP 55000

#define MAX_CONNECTIONS 4
#define MAXDATASIZE 6000 // maximum size of a Q4S message in bytes

// Type defined for Q4S messages
typedef struct {
  char start_line[200];
	char header[500];
  char body[5000];
} type_q4s_message;

// State machine type
typedef enum {
  WAIT_CONNECTION,
	WAIT_START,
	HANDSHAKE,
	STAGE_0,
	PING_MEASURE_0,
	STAGE_1,
  BWIDTH_MEASURE,
  STAGE_2,
  PING_MEASURE_2,
  TERMINATION
} type_state_machine;

// Type defined for a q4s session
typedef struct {
	int session_id;
  int expiration_time;
  int qos_level[2]; // {upstream, downstream}
  int alert_pause;
  int latency_th;
  int jitter_th[2]; // {upstream, downstream}
  int bw_th[2];  // {upstream, downstream}
  float packetloss_th[2];  // {upstream, downstream}
	type_q4s_message message_to_send;
	char prepared_message[MAXDATASIZE];
	type_q4s_message message_received;
	type_state_machine state;
} type_q4s_session;

// Prototype of check functions
int check_new_connection (fsm_t* this);
int check_receive_begin (fsm_t* this);
int check_receive_ready0 (fsm_t* this);
int check_receive_ping (fsm_t* this);
int check_temp_ping_0 (fsm_t* this);
int check_receive_ok (fsm_t* this);
int check_alert (fsm_t* this);
int check_receive_ready1 (fsm_t* this);
int check_temp_bwidth (fsm_t* this);
int check_receive_bwidth (fsm_t* this);
int check_temp_ping_2 (fsm_t* this);
int check_receive_ready_2 (fsm_t* this);
int check_recover (fsm_t* this);
int check_receive_cancel (fsm_t* this);
int check_released (fsm_t* this);


// Prototypes of action functions
void Setup (fsm_t* fsm);
void Respond_ok (fsm_t* fsm);
void Ready0 (fsm_t* fsm);
void Ping (fsm_t* fsm);
void Update (fsm_t* fsm);
void Alert (fsm_t* fsm);
void Bwidth (fsm_t* fsm);
void Recover (fsm_t* fsm);
void Release (fsm_t* fsm);
void Cancel (fsm_t* fsm);

// Prototype of Q4S message related functions
void create_200 (type_q4s_message *q4s_message);
void create_cancel (type_q4s_message *q4s_message);
void create_response (type_q4s_message *q4s_message, char status_code[10],
	char reason_phrase[10], char header[500], char body[5000]);
void create_request (type_q4s_message *q4s_message, char method[10],
  char header[500], char body[5000]);
void prepare_message (type_q4s_message *q4s_message, char prepared_message[MAXDATASIZE]);
void store_message (char received_message[MAXDATASIZE], type_q4s_message *q4s_message);

// Prototype of Q4S parameter storage function
void store_parameters(type_q4s_session *q4s_session, type_q4s_message *q4s_message);

// Prototype of connection functions
void start_listening();
void wait_new_connection();
void send_message (char prepared_message[MAXDATASIZE]);
void *thread_receives();

// Prototipe of function exploring the keyboard
void *thread_explores_keyboard();

// Prototype of initialization function
int system_setup();

// Other protoypes of functions
void delay (int milliseconds);
const char * current_time();

#endif /* _Q4SSERVER_H_ */