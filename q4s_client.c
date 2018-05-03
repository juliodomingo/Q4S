#include "q4s_client.h"

// GENERAL VARIABLES

// FOR Q4S SESSION MANAGING
// Q4S session
static type_q4s_session q4s_session;
// Variable to store the flags
int flags = 0;

// FOR CONNECTION MANAGING
// Structs with info for the connection
struct sockaddr_in client_TCP, client_UDP, server_TCP, server_UDP;
// Struct with host info
struct hostent *host;
// Variable for socket assignment
int socket_TCP, socket_UDP;
// Variable for TCP socket buffer
char buffer_TCP[MAXDATASIZE];
// Variable for UDP socket buffer
char buffer_UDP[MAXDATASIZE];

// FOR THREAD MANAGING
// Thread to check reception of TCP data
pthread_t receive_TCP_thread;
// Thread to check reception of UDP data
pthread_t receive_UDP_thread;
// Thread to check pressed keys on the keyboard
pthread_t keyboard_thread;
// Thread acting as timer for ping delivery
pthread_t timer_ping;
// Thread acting as timer for bwidth delivery
pthread_t timer_bwidth;
// Variable for mutual exclusion with flags
pthread_mutex_t mutex_flags;
// Variable for mutual exclusion with q4s session
pthread_mutex_t mutex_session;
// Variable for mutual exclusion with TCP buffer
pthread_mutex_t mutex_buffer_TCP;
// Variable for mutual exclusion with UDP buffer
pthread_mutex_t mutex_buffer_UDP;
// Variable for mutual exclusion with message printing
pthread_mutex_t mutex_print;
// Variable for mutual exclusion with timers for latency measure
pthread_mutex_t mutex_tm_latency;
// Variable for mutual exclusion with timers for jitter measure
pthread_mutex_t mutex_tm_jitter;


// FOR TIME
// Variables to store time when a Q4S PING is sent and its sequence number
type_latency_tm tm_latency_start1;
type_latency_tm tm_latency_start2;
type_latency_tm tm_latency_start3;
type_latency_tm tm_latency_start4;
// Variable to store time when a Q4S 200 OK is received
type_latency_tm tm_latency_end;
// Variable to store the time when a Q4S PING is received
struct timespec tm1_jitter;
// Variable to store the time when next Q4S PING is received
struct timespec tm2_jitter;

// AUXILIARY VARIABLES
// Variable used to obviate packet losses in jitter measure
int num_ping;
// Variable that indicates the number of new packet losses occurred
int num_packet_lost;
// Variable that indicates number of latency measures made by client
int num_latency_measures_client;
// Variable that indicates number of jitter measures made by client
int num_jitter_measures_client;
// Variable that indicates number of packetloss measures made by client
int num_packetloss_measures_client;
// Variable that indicates number of latency measures made by server
int num_latency_measures_server;
// Variable that indicates number of jitter measures made by server
int num_jitter_measures_server;
// Variable that indicates number of packetloss measures made by server
int num_packetloss_measures_server;
// Variable that shows number of Q4S BWIDTHs received in a period
int num_bwidth_received;


// GENERAL FUNCTIONS

// Waits for x milliseconds
void delay (int milliseconds) {
	long pause;
	clock_t now, then;
	pause = milliseconds*(CLOCKS_PER_SEC/1000);
	now = then = clock();
	while((now-then) < pause) {
		now = clock();
	}
}

// Returns interval between two moments of time in ms
int ms_elapsed(struct timespec tm1, struct timespec tm2) {
  unsigned long long t = 1000 * (tm2.tv_sec - tm1.tv_sec)
    + (tm2.tv_nsec - tm1.tv_nsec) / 1000000;
	return t;
}

// Returns current time formatted to be included in Q4S header
const char * current_time() {
  time_t currentTime;
  time(&currentTime);
  char * now = ctime(&currentTime);
  char whitespace = ' ';
	char comma = ',';
  char prepared_time[30] = {now[0], now[1], now[2], comma, whitespace,
		now[8], now[9], whitespace, now[4], now[5], now[6], whitespace, now[20],
		now[21], now[22], now[23], whitespace, now[11], now[12], now[13],
		now[14], now[15], now[16], now[17],
    now[18], whitespace, 'G', 'M', 'T'};
  memset(now, '\0', sizeof(prepared_time));
	strcpy(now, prepared_time);
  return now;
}

// Sorts int array from minimum (not 0) to maximum values
void sort_array(int samples[MAXNUMSAMPLES], int length) {
	if (length <= 1) {
		return;
	}
	int temp;
	int i, j;
	bool swapped = false;
	// Loop through all numbers
	for(i = 0; i < length-1; i++) {
		if(samples[i+1] == 0) {
			break;
		}
		swapped = false;
		// Loop through numbers falling keyboard_ahead
		for(j = 0; j < length-1-i; j++) {
			if(samples[j] > samples[j+1]) {
				temp = samples[j];
				samples[j] = samples[j+1];
				samples[j+1] = temp;
				swapped = true;
			}
		}
		if (!swapped) {
			break;
		}
	}
}

// Returns the minimum value, given 2 integers
int min (int a, int b) {
	return a < b ? a:b;
}

// INITIALITATION FUNCTIONS

// System initialitation
// Creates a thread to explore keyboard and configures mutex
int system_setup (void) {
	pthread_mutex_init(&mutex_flags, NULL);
	pthread_mutex_init(&mutex_session, NULL);
	pthread_mutex_init(&mutex_buffer_TCP, NULL);
	pthread_mutex_init(&mutex_buffer_UDP, NULL);
	pthread_mutex_init(&mutex_print, NULL);
	pthread_mutex_init(&mutex_tm_latency, NULL);
	pthread_mutex_init(&mutex_tm_jitter, NULL);
	// Throws a thread to explore PC keyboard
	pthread_create(&keyboard_thread, NULL, (void*)thread_explores_keyboard, NULL);
	return 1;
}

// State machine initialitation
// Puts every flag to 0
void fsm_setup(fsm_t* juego_fsm) {
	// Lock to guarantee mutual exclusion
	pthread_mutex_lock(&mutex_flags);
	flags = 0;
	pthread_mutex_unlock(&mutex_flags);
}


// Q4S MESSAGE RELATED FUNCTIONS

// Creation of Q4S BEGIN message
// Creates a default message unless client wants to suggest Q4S quality thresholds
void create_begin (type_q4s_message *q4s_message) {
  char input[100]; // to store user inputs
	memset(input, '\0', sizeof(input));

	char header[500]; // it will be filled with header fields
  memset(header, '\0', sizeof(header));

	char body[5000]; // it will be empty or filled with SDP parameters
	memset(body, '\0', sizeof(body)); // body is empty by default

	char h1[100];
	memset(h1, '\0', sizeof(h1));

	char h2[100];
	memset(h2, '\0', sizeof(h2));

	char h3[100];
	memset(h3, '\0', sizeof(h3));

  pthread_mutex_lock(&mutex_print);
	printf("\nDo you want to specify desired quality thresholds? (yes/no): ");
	pthread_mutex_unlock(&mutex_print);

  scanf("%s", input); // variable input stores user's answer

  // If user wants to suggest Q4S quality thresholds
	if (strstr(input, "yes")) {
		char v[100];
		memset(v, '\0', sizeof(v));
		char o[100];
		memset(o, '\0', sizeof(o));
		char s[100];
		memset(s, '\0', sizeof(s));
		char i[100];
		memset(i, '\0', sizeof(i));
		char t[100];
		memset(t, '\0', sizeof(t));

		// Default SDP parameters
		strcpy(v, "v=0\n");
		strcpy(o, "o=\n");
		strcpy(s, "s=Q4S\n");
		strcpy(i, "i=Q4S desired parameters\n");
		strcpy(t, "t=0 0\n");

		char a1[100];
		memset(a1, '\0', sizeof(a1));
		char a2[100];
		memset(a2, '\0', sizeof(a2));
		char a3[100];
		memset(a3, '\0', sizeof(a3));
		char a4[100];
		memset(a4, '\0', sizeof(a4));
		char a5[100];
		memset(a5, '\0', sizeof(a5));

    // Includes user's suggestion about QoS levels (upstream and downstream)
		strcpy(a1, "a=qos-level:");
		pthread_mutex_lock(&mutex_print);
		printf("\nEnter upstream QoS level (from 0 to 9): ");
		pthread_mutex_unlock(&mutex_print);
	  scanf("%s", input);
		strcat(a1, input);
		strcat(a1, "/");
	  memset(input, '\0', strlen(input));
		pthread_mutex_lock(&mutex_print);
		printf("Enter downstream QoS level (from 0 to 9): ");
		pthread_mutex_unlock(&mutex_print);
	  scanf("%s", input);
		strcat(a1, input);
		strcat(a1, "\n");
		memset(input, '\0', strlen(input));

    // Includes user's suggestion about latency threshold
		strcpy(a2, "a=latency:");
		pthread_mutex_lock(&mutex_print);
		printf("Enter latency threshold (in ms): ");
		pthread_mutex_unlock(&mutex_print);
	  scanf("%s", input);
		strcat(a2, input);
		strcat(a2, "\n");
	  memset(input, '\0', strlen(input));

    // Includes user's suggestion about jitter threshold (upstream and downstream)
		strcpy(a3, "a=jitter:");
		pthread_mutex_lock(&mutex_print);
		printf("Enter upstream jitter threshold (in ms): ");
		pthread_mutex_unlock(&mutex_print);
	  scanf("%s", input);
		strcat(a3, input);
		strcat(a3, "/");
	  memset(input, '\0', strlen(input));
		pthread_mutex_lock(&mutex_print);
		printf("Enter downstream jitter threshold (in ms): ");
		pthread_mutex_unlock(&mutex_print);
	  scanf("%s", input);
		strcat(a3, input);
		strcat(a3, "\n");
	  memset(input, '\0', strlen(input));

    // Includes user's suggestion about bandwidth threshold (upstream and downstream)
		strcpy(a4, "a=bandwidth:");
		pthread_mutex_lock(&mutex_print);
		printf("Enter upstream bandwidth threshold (in kbps): ");
		pthread_mutex_unlock(&mutex_print);
	  scanf("%s", input);
		strcat(a4, input);
		strcat(a4, "/");
	  memset(input, '\0', strlen(input));
		pthread_mutex_lock(&mutex_print);
		printf("Enter downstream bandwidth threshold (in kbps): ");
		pthread_mutex_unlock(&mutex_print);
	  scanf("%s", input);
		strcat(a4, input);
		strcat(a4, "\n");
	  memset(input, '\0', strlen(input));

    // Includes user's suggestion about packetloss threshold (upstream and downstream)
		strcpy(a5, "a=packetloss:");
		pthread_mutex_lock(&mutex_print);
		printf("Enter upstream packetloss threshold (percentage from 0.00 to 1.00): ");
    pthread_mutex_unlock(&mutex_print);
		scanf("%s", input);
		strcat(a5, input);
		strcat(a5, "/");
	  memset(input, '\0', strlen(input));
		pthread_mutex_lock(&mutex_print);
		printf("Enter downstream packetloss threshold (percentage from 0.00 to 1.00): ");
    pthread_mutex_unlock(&mutex_print);
		scanf("%s", input);
		strcat(a5, input);
		strcat(a5, "\n");
	  memset(input, '\0', strlen(input));

    // Prepares body with SDP parameters
		strcpy(body, v);
		strcat(body, o);
		strcat(body, s);
		strcat(body, i);
		strcat(body, t);
		strcat(body, a1);
		strcat(body, a2);
		strcat(body, a3);
		strcat(body, a4);
		strcat(body, a5);
	}

  // Prepares some header fields
	strcpy(h1, "Content-Type: application/sdp\n");
	strcpy(h2, "User-Agent: q4s-ua-experimental-1.0\n");

  // Includes body length in "Content Length" header field
	strcpy(h3, "Content Length: ");
	int body_length = strlen(body);
	char s_body_length[10];
	sprintf(s_body_length, "%d", body_length);
  strcat(h3, s_body_length);
	strcat(h3, "\n");

  // Prepares header with header fields
	strcpy(header, h1);
	strcat(header, h2);
	strcat(header, h3);

  // Delegates in a request creation function
  create_request (q4s_message,"BEGIN", header, body);
}

// Creation of Q4S READY 0 message
void create_ready0 (type_q4s_message *q4s_message) {
  char body[5000]; // it will be empty or filled with SDP parameters
	memset(body, '\0', sizeof(body)); // body is empty by default

	char header[500]; // it will be filled with header fields
  memset(header, '\0', sizeof(header));

	char h1[100];
	memset(h1, '\0', sizeof(h1));

	char h2[100];
	memset(h2, '\0', sizeof(h2));

	char h3[100];
	memset(h3, '\0', sizeof(h3));

	char h4[100];
	memset(h4, '\0', sizeof(h4));

  // Prepares some header fields
	strcpy(h1, "Stage: 0\n");
	strcpy(h2, "Content-Type: application/sdp\n");
	strcpy(h3, "User-Agent: q4s-ua-experimental-1.0\n");

  // Includes body length in "Content Length" header field
	strcpy(h4, "Content Length: ");
	int body_length = strlen(body);
	char s_body_length[10];
	sprintf(s_body_length, "%d", body_length);
  strcat(h4, s_body_length);
	strcat(h4, "\n");

  // Prepares header with header fields
	strcpy(header, h1);
	strcat(header, h2);
	strcat(header, h3);
	strcat(header, h4);

  // Delegates in a request creation function
  create_request (q4s_message,"READY", header, body);
}

// Creation of Q4S READY 1 message
void create_ready1 (type_q4s_message *q4s_message) {
  char body[5000]; // it will be empty or filled with SDP parameters
	memset(body, '\0', sizeof(body)); // body is empty by default

	char header[500]; // it will be filled with header fields
  memset(header, '\0', sizeof(header));

	char h1[100];
	memset(h1, '\0', sizeof(h1));

	char h2[100];
	memset(h2, '\0', sizeof(h2));

	char h3[100];
	memset(h3, '\0', sizeof(h3));

	char h4[100];
	memset(h4, '\0', sizeof(h4));

	// Prepares some header fields
	strcpy(h1, "Stage: 1\n");
	strcpy(h2, "Content-Type: application/sdp\n");
	strcpy(h3, "User-Agent: q4s-ua-experimental-1.0\n");

  // Includes body length in "Content Length" header field
	strcpy(h4, "Content Length: ");
	int body_length = strlen(body);
	char s_body_length[10];
	sprintf(s_body_length, "%d", body_length);
  strcat(h4, s_body_length);
	strcat(h4, "\n");

  // Prepares header with header fields
	strcpy(header, h1);
	strcat(header, h2);
	strcat(header, h3);
	strcat(header, h4);

  // Delegates in a request creation function
  create_request (q4s_message,"READY", header, body);
}

// Creation of Q4S READY 2 message
void create_ready2 (type_q4s_message *q4s_message) {
  char body[5000]; // it will be empty or filled with SDP parameters
	memset(body, '\0', sizeof(body)); // body is empty by default

	char header[500]; // it will be filled with header fields
  memset(header, '\0', sizeof(header));

	char h1[100];
	memset(h1, '\0', sizeof(h1));

	char h2[100];
	memset(h2, '\0', sizeof(h2));

	char h3[100];
	memset(h3, '\0', sizeof(h3));

	char h4[100];
	memset(h4, '\0', sizeof(h4));

	// Prepares some header fields
	strcpy(h1, "Stage: 2\n");
	strcpy(h2, "Content-Type: application/sdp\n");
	strcpy(h3, "User-Agent: q4s-ua-experimental-1.0\n");

  // Includes body length in "Content Length" header field
	strcpy(h4, "Content Length: ");
	int body_length = strlen(body);
	char s_body_length[10];
	sprintf(s_body_length, "%d", body_length);
  strcat(h4, s_body_length);
	strcat(h4, "\n");

  // Prepares header with header fields
	strcpy(header, h1);
	strcat(header, h2);
	strcat(header, h3);
	strcat(header, h4);

  // Delegates in a request creation function
  create_request (q4s_message,"READY", header, body);
}


// Creation of Q4S PING message
void create_ping (type_q4s_message *q4s_message) {
  char body[5000]; // it will be empty or filled with SDP parameters
	memset(body, '\0', sizeof(body)); // body is empty by default

	char header[500]; // it will be filled with header fields
  memset(header, '\0', sizeof(header));

	char h1[100];
	memset(h1, '\0', sizeof(h1));

	char h2[100];
	memset(h2, '\0', sizeof(h2));

	char h3[100];
	memset(h3, '\0', sizeof(h3));

	char h4[100];
	memset(h4, '\0', sizeof(h4));

	char h5[100];
	memset(h5, '\0', sizeof(h5));

	char h6[100];
	memset(h6, '\0', sizeof(h6));

	// Includes session ID
	strcpy(h1, "Session-Id: ");
	char s_session_id[20];
	sprintf(s_session_id, "%d", (&q4s_session)->session_id);
	strcat(h1, s_session_id);
	strcat(h1, "\n");
	// Includes sequence number of Q4S PING to send
	strcpy(h2, "Sequence-Number: ");
	char s_seq_num[10];
	sprintf(s_seq_num, "%d", (&q4s_session)->seq_num_client);
	strcat(h2, s_seq_num);
	strcat(h2, "\n");
	// Includes user agent
	strcpy(h3, "User-Agent: q4s-ua-experimental-1.0\n");
	// Includes timestamp
	strcpy(h4, "Timestamp: ");
	const char * now = current_time();
	strcat(h4, now);
	strcat(h4, "\n");
	// Includes measures
	strcpy(h5, "Measurements: ");
	strcat(h5, "l=");
	if((&q4s_session)->latency_measure_client && (&q4s_session)->latency_th) {
		char s_latency[6];
		sprintf(s_latency, "%d", (&q4s_session)->latency_measure_client);
    strcat(h5, s_latency);
	} else {
		strcat(h5, " ");
	}
	strcat(h5, ", j=");
	if((&q4s_session)->jitter_measure_client && (&q4s_session)->jitter_th[1]) {
		char s_jitter[6];
		sprintf(s_jitter, "%d", (&q4s_session)->jitter_measure_client);
    strcat(h5, s_jitter);
	} else {
		strcat(h5, " ");
	}
	strcat(h5, ", pl=");
	if((&q4s_session)->packetloss_measure_client >= 0 && (&q4s_session)->packetloss_th[1]) {
		char s_packetloss[6];
		sprintf(s_packetloss, "%.2f", (&q4s_session)->packetloss_measure_client);
    strcat(h5, s_packetloss);
	} else {
		strcat(h5, " ");
	}
	strcat(h5, ", bw=");
	if((&q4s_session)->bw_measure_client && (&q4s_session)->bw_th[1]) {
		char s_bw[6];
		sprintf(s_bw, "%d", (&q4s_session)->bw_measure_client);
    strcat(h5, s_bw);
	} else {
		strcat(h5, " ");
	}
	strcat(h5, "\n");
  // Includes body length in "Content Length" header field
	strcpy(h6, "Content Length: ");
	int body_length = strlen(body);
	char s_body_length[10];
	sprintf(s_body_length, "%d", body_length);
  strcat(h6, s_body_length);
	strcat(h6, "\n");

  // Prepares header with header fields
	strcpy(header, h1);
	strcat(header, h2);
	strcat(header, h3);
	strcat(header, h4);
	strcat(header, h5);
	strcat(header, h6);

  // Delegates in a request creation function
  create_request (q4s_message,"PING", header, body);
}

// Creation of Q4S 200 OK message
// In respond to Q4S PING messages
void create_200 (type_q4s_message *q4s_message) {
	char header[500];  // to store header of the message
  memset(header, '\0', sizeof(header));

	char body[5000];  // to store body of the message
	memset(body, '\0', sizeof(body));

	char h1[100];
	memset(h1, '\0', sizeof(h1));

	char h2[100];
	memset(h2, '\0', sizeof(h2));

	char h3[100];
	memset(h3, '\0', sizeof(h3));

	char h4[100];
	memset(h4, '\0', sizeof(h4));

	// Includes session ID
	strcpy(h1, "Session-Id: ");
	char s_session_id[20];
	sprintf(s_session_id, "%d", (&q4s_session)->session_id);
	strcat(h1, s_session_id);
	strcat(h1, "\n");
	// Includes sequence number of Q4S PING received
	strcpy(h2, "Sequence-Number: ");
	char s_seq_num[10];
	sprintf(s_seq_num, "%d", (&q4s_session)->seq_num_server);
	strcat(h2, s_seq_num);
	strcat(h2, "\n");
	// Includes body length in "Content Length" header field
	strcpy(h3, "Content Length: ");
	int body_length = strlen(body);
	char s_body_length[10];
	sprintf(s_body_length, "%d", body_length);
	strcat(h3, s_body_length);
	strcat(h3, "\n");
	// Includes timestamp of server
	strcpy(h4, "Timestamp: ");
	strcat(h4, (&q4s_session)->server_timestamp);
	strcat(h4, "\n");

  // Prepares header with header fields
	strcpy(header, h1);
	strcat(header, h2);
	strcat(header, h3);
	strcat(header, h4);

  // Delegates in a response creation function
  create_response (q4s_message, "200", "OK", header, body);
}

// Creation of Q4S BWIDTH message
void create_bwidth (type_q4s_message *q4s_message) {
	char body[5000]; // it will be empty or filled with SDP parameters
	memset(body, '\0', sizeof(body));

  strcpy(body, "a");
	for (int i = 0; i < 998; i++) {
		strcat(body, "a");
	}
	strcat(body, "\n");

	char header[500]; // it will be filled with header fields
	memset(header, '\0', sizeof(header));

	char h1[100];
	memset(h1, '\0', sizeof(h1));

	char h2[100];
	memset(h2, '\0', sizeof(h2));

	char h3[100];
	memset(h3, '\0', sizeof(h3));

	char h4[100];
	memset(h4, '\0', sizeof(h4));

	char h5[100];
	memset(h5, '\0', sizeof(h5));

	char h6[100];
	memset(h6, '\0', sizeof(h6));

	// Includes session ID
	strcpy(h1, "Session-Id: ");
	char s_session_id[20];
	sprintf(s_session_id, "%d", (&q4s_session)->session_id);
	strcat(h1, s_session_id);
	strcat(h1, "\n");
	// Includes sequence number of Q4S PING to send
	strcpy(h2, "Sequence-Number: ");
	char s_seq_num[10];
	sprintf(s_seq_num, "%d", (&q4s_session)->seq_num_client);
	strcat(h2, s_seq_num);
	strcat(h2, "\n");
	// Includes user agent
	strcpy(h3, "User-Agent: q4s-ua-experimental-1.0\n");
	// Includes content type
	strcpy(h4, "Content-Type: text\n");
	// Includes measures
	strcpy(h5, "Measurements: ");
	strcat(h5, "l=");
	if((&q4s_session)->latency_measure_client && (&q4s_session)->latency_th) {
		char s_latency[6];
		sprintf(s_latency, "%d", (&q4s_session)->latency_measure_client);
    strcat(h5, s_latency);
	} else {
		strcat(h5, " ");
	}
	strcat(h5, ", j=");
	if((&q4s_session)->jitter_measure_client && (&q4s_session)->jitter_th[1]) {
		char s_jitter[6];
		sprintf(s_jitter, "%d", (&q4s_session)->jitter_measure_client);
    strcat(h5, s_jitter);
	} else {
		strcat(h5, " ");
	}
	strcat(h5, ", pl=");
	if((&q4s_session)->packetloss_measure_client >= 0 && (&q4s_session)->packetloss_th[1]) {
		char s_packetloss[6];
		sprintf(s_packetloss, "%.2f", (&q4s_session)->packetloss_measure_client);
    strcat(h5, s_packetloss);
	} else {
		strcat(h5, " ");
	}
	strcat(h5, ", bw=");
	if((&q4s_session)->bw_measure_client && (&q4s_session)->bw_th[1]) {
		char s_bw[6];
		sprintf(s_bw, "%d", (&q4s_session)->bw_measure_client);
    strcat(h5, s_bw);
	} else {
		strcat(h5, " ");
	}
	strcat(h5, "\n");

  // Includes body length in "Content Length" header field
	strcpy(h6, "Content Length: ");
	int body_length = strlen(body);
	char s_body_length[10];
	sprintf(s_body_length, "%d", body_length);
  strcat(h6, s_body_length);
	strcat(h6, "\n");

  // Prepares header with header fields
	strcpy(header, h1);
	strcat(header, h2);
	strcat(header, h3);
	strcat(header, h4);
	strcat(header, h5);
	strcat(header, h6);

  // Delegates in a request creation function
  create_request (q4s_message,"BWIDTH", header, body);
}

// Creation of Q4S CANCEL message
// Creates a default CANCEL message
void create_cancel (type_q4s_message *q4s_message) {
	char body[5000]; // it will be empty or filled with SDP parameters
	memset(body, '\0', sizeof(body));

	char header[500]; // it will be filled with header fields
	memset(header, '\0', sizeof(header));

	char h1[100];
	memset(h1, '\0', sizeof(h1));

	char h2[100];
	memset(h2, '\0', sizeof(h2));

	char h3[100];
	memset(h3, '\0', sizeof(h3));

	char h4[100];
	memset(h4, '\0', sizeof(h4));

	// Creates default header fields
	strcpy(h1, "User-Agent: q4s-ua-experimental-1.0\n");

	// Includes session ID
	strcpy(h2, "Session-Id: ");
	char s_session_id[20];
	sprintf(s_session_id, "%d", (&q4s_session)->session_id);
	strcat(h2, s_session_id);
	strcat(h2, "\n");

	strcpy(h3, "Content-Type: application/sdp\n");

	// Includes body length in "Content Length" header field
	strcpy(h4, "Content Length: ");
	int body_length = strlen(body);
	char s_body_length[10];
	sprintf(s_body_length, "%d", body_length);
  strcat(h4, s_body_length);
	strcat(h4, "\n");

  // Prepares header with header fields
	strcpy(header, h1);
	strcat(header, h2);
	strcat(header, h3);
	strcat(header, h4);

	// Delegates in a request creation function
  create_request (q4s_message,"CANCEL", header, body);
}

// Creation of Q4S requests
// Receives parameters to create the request line (start line)
// Receives prepared header and body from more specific functions
// Stores start line, header and body in a q4s message received as parameter
void create_request (type_q4s_message *q4s_message, char method[10],
	char header[500], char body[5000]) {
    memset((q4s_message)->start_line, '\0', sizeof((q4s_message)->start_line));
		memset((q4s_message)->header, '\0', sizeof((q4s_message)->header));
		memset((q4s_message)->body, '\0', sizeof((q4s_message)->body));
		// Establish Request URI
		char req_uri[100] = " q4s://www.example.com";
		// Prepares and stores start line
    strcpy((q4s_message)->start_line, method);
    strcat((q4s_message)->start_line, req_uri);
    strcat((q4s_message)->start_line, " Q4S/1.0");
		// Stores header
		strcpy((q4s_message)->header, header);
		// Stores body
		strcpy((q4s_message)->body, body);
}

// Fine tuning of Q4S messages before being sent
// Converts from type_q4s_mesage to char[MAXDATASIZE]
void prepare_message (type_q4s_message *q4s_message, char prepared_message[MAXDATASIZE]) {
  memset(prepared_message, '\0', MAXDATASIZE);
	// Pays special attention to Q4S message format
	strcpy(prepared_message, (q4s_message)->start_line);
  strcat(prepared_message, "\n");
  strcat(prepared_message, (q4s_message)->header);
  strcat(prepared_message, "\n");
  strcat(prepared_message, (q4s_message)->body);
}

// Creation of Q4S responses
// Receives parameters to create the status line (start line)
// Receives prepared header and body from more specific functions
// Stores start line, header and body in a q4s message received as parameter
void create_response (type_q4s_message *q4s_message, char status_code[10],
	char reason_phrase[10], char header[500], char body[5000]) {
		memset((q4s_message)->start_line, '\0', sizeof((q4s_message)->start_line));
		memset((q4s_message)->header, '\0', sizeof((q4s_message)->header));
		memset((q4s_message)->body, '\0', sizeof((q4s_message)->body));
		// Prepares and stores start line
    strcpy((q4s_message)->start_line, "Q4S/1.0 ");
    strcat((q4s_message)->start_line, status_code);
    strcat((q4s_message)->start_line, " ");
    strcat((q4s_message)->start_line, reason_phrase);
		// Stores header
		strcpy((q4s_message)->header, header);
		// Stores body
		strcpy((q4s_message)->body, body);
}

// Storage of a Q4S message just received
// Converts from char[MAXDATASIZE] to type_q4s_message
void store_message (char received_message[MAXDATASIZE], type_q4s_message *q4s_message) {
  // Auxiliary variables
	char *fragment1;
  char *fragment2;

  char start_line[200]; // to store start line
  char header[500];  // to store header
  char body[5000];  // to store body

	memset(start_line, '\0', strlen(start_line));
	memset(header, '\0', strlen(header));
	memset(body, '\0', strlen(body));

  // Copies header + body
  fragment1 = strstr(received_message, "\n");
  // Obtains start line
  strncpy(start_line, received_message, strlen(received_message)-strlen(fragment1));
  // Quits initial "\n"
  fragment1 = fragment1 + 1;

  // Copies body (if present)
	fragment2 = strstr(received_message, "\n\n");
	if (strcmp(fragment2, "\n\n") != 0 ) {
		// Obtains header
	  strncpy(header, fragment1, strlen(fragment1)-(strlen(fragment2)-1));
	  // Quits initial "\n\n"
	  fragment2 = fragment2 + 2;
	  // Obtains body
	  strncpy(body, fragment2, strlen(fragment2));
	} else {
		// Obtains header
	  strncpy(header, fragment1, strlen(fragment1));
		// Body is empty
		memset(body, '\0', strlen(body));
	}

  // Stores Q4S message
	strcpy((q4s_message)->start_line, start_line);
  strcpy((q4s_message)->header, header);
  strcpy((q4s_message)->body, body);
}

// Q4S PARAMETER STORAGE FUNCTION

// Storage of Q4S parameters from a Q4S message
void store_parameters(type_q4s_session *q4s_session, type_q4s_message *q4s_message) {
	// Extracts start line
	char start_line[200];
	memset(start_line, '\0', strlen(start_line));
	strcpy(start_line, (q4s_message)->start_line);
	// Creates a copy of header to manipulate it
	char copy_start_line[200];
	memset(copy_start_line, '\0', strlen(copy_start_line));
  strcpy(copy_start_line, start_line);
	// Extracts header
	char header[500];
	memset(header, '\0', strlen(header));
	strcpy(header, (q4s_message)->header);
	// Creates a copy of header to manipulate it
	char copy_header[500];
	memset(copy_header, '\0', strlen(copy_header));
  strcpy(copy_header, header);
	// Extracts body
	char body[5000];
	memset(body, '\0', strlen(body));
	strcpy(body, (q4s_message)->body);
	// Creates a copy of body to manipulate it
	char copy_body[5000];
	memset(copy_body, '\0', strlen(copy_body));
  strcpy(copy_body, body);

  // Auxiliary variable
	char *fragment;

  pthread_mutex_lock(&mutex_print);
	printf("\n");
	pthread_mutex_unlock(&mutex_print);

	// If session id is not known
	if ((q4s_session)->session_id < 0) {
		// If there is a Session ID parameter in the header
		if (fragment = strstr(copy_header, "Session-Id")) {
			fragment = fragment + 12;  // moves to the beginning of the value
			char *string_id;
			string_id = strtok(fragment, "\n");  // stores string value
			(q4s_session)->session_id = atoi(string_id);  // converts into int and stores
			memset(fragment, '\0', strlen(fragment));
			strcpy(copy_header, header);  // restore copy of header
			pthread_mutex_lock(&mutex_print);
			printf("Session ID stored: %d\n", (q4s_session)->session_id);
			pthread_mutex_unlock(&mutex_print);
		} else if (fragment = strstr(copy_body, "o=")) {  // if Session ID is in the body
			fragment = strstr(fragment, " ");  // moves to the beginning of the value
			char *string_id;
			string_id = strtok(fragment, " ");  // stores string value
			(q4s_session)->session_id = atoi(string_id);  // converts into int and stores
			memset(fragment, '\0', strlen(fragment));
			strcpy(copy_body, body);  // restores copy of header
			pthread_mutex_lock(&mutex_print);
			printf("Session ID stored: %d\n", (q4s_session)->session_id);
			pthread_mutex_unlock(&mutex_print);
		}
	}

  // If there is a Expires parameter in the header
	if (fragment = strstr(copy_header, "Expires")){
		fragment = fragment + 9;  // moves to the beginning of the value
		char *s_expires;
		s_expires = strtok(fragment, "\n");  // stores string value
		(q4s_session)->expiration_time = atoi(s_expires);  // converts into int and stores
		memset(fragment, '\0', strlen(fragment));
		strcpy(copy_header, header);  // restores copy of header
		pthread_mutex_lock(&mutex_print);
		printf("Expiration time stored: %d\n", (q4s_session)->expiration_time);
		pthread_mutex_unlock(&mutex_print);
	}

  if (strcmp(copy_start_line, "PING q4s://www.example.com Q4S/1.0") == 0
    || strcmp(copy_start_line, "BWIDTH q4s://www.example.com Q4S/1.0") == 0) {
		// If there is a Date parameter in the header
		if (fragment = strstr(copy_header, "Timestamp")){
			fragment = fragment + 11;  // moves to the beginning of the value
			char *s_date;
			s_date = strtok(fragment, "\n");  // stores string value
			strcpy((q4s_session)->server_timestamp, s_date);  // stores
			memset(fragment, '\0', strlen(fragment));
			strcpy(copy_header, header);  // restores copy of header
			pthread_mutex_lock(&mutex_print);
			printf("Server timestamp stored: %s\n", (q4s_session)->server_timestamp);
			pthread_mutex_unlock(&mutex_print);
		}
		// If there is a Sequence Number parameter in the header
		if (fragment = strstr(copy_header, "Sequence-Number")){
			fragment = fragment + 17;  // moves to the beginning of the value
			char *s_seq_num;
			s_seq_num = strtok(fragment, "\n");  // stores string value
			(q4s_session)->seq_num_server = atoi(s_seq_num);  // converts into int and stores
			memset(fragment, '\0', strlen(fragment));
			strcpy(copy_header, header);  // restores copy of header
			pthread_mutex_lock(&mutex_print);
			printf("Sequence number of server stored: %d\n", (q4s_session)->seq_num_server);
			pthread_mutex_unlock(&mutex_print);
		}
		// If there is a Measurements parameter in the header
		if (fragment = strstr(copy_header, "Measurements")){
			fragment = fragment + 14;  // moves to the beginning of the value
			char *s_latency;
			strtok(fragment, "=");
			s_latency = strtok(NULL, ",");  // stores string value
			if (strcmp(s_latency," ") != 0) {
				num_latency_measures_server++;
				(q4s_session)->latency_measure_server = atoi(s_latency);  // converts into int and stores
				pthread_mutex_lock(&mutex_print);
				printf("Latency measure of server stored: %d\n", (q4s_session)->latency_measure_server);
	      pthread_mutex_unlock(&mutex_print);
			}

			char *s_jitter;
			strtok(NULL, "=");
			s_jitter = strtok(NULL, ",");  // stores string value
			if (strcmp(s_jitter," ") != 0) {
				num_jitter_measures_server++;
				(q4s_session)->jitter_measure_server = atoi(s_jitter);  // converts into int and stores
				pthread_mutex_lock(&mutex_print);
				printf("Jitter measure of server stored: %d\n", (q4s_session)->jitter_measure_server);
	      pthread_mutex_unlock(&mutex_print);
			}

			char *s_pl;
			strtok(NULL, "=");
			s_pl = strtok(NULL, ",");  // stores string value
      if (strcmp(s_pl," ") != 0) {
				num_packetloss_measures_server++;
				(q4s_session)->packetloss_measure_server = atof(s_pl);  // converts into int and stores
				pthread_mutex_lock(&mutex_print);
				printf("Packetloss measure of server stored: %.2f\n", (q4s_session)->packetloss_measure_server);
	      pthread_mutex_unlock(&mutex_print);
			}

			char *s_bw;
			strtok(NULL, "=");
			s_bw = strtok(NULL, "\n");  // stores string value
      if (strcmp(s_bw," ") != 0) {
				(q4s_session)->bw_measure_server = atoi(s_bw);  // converts into int and stores
				pthread_mutex_lock(&mutex_print);
				printf("Bandwidth measure of server stored: %d\n", (q4s_session)->bw_measure_server);
	      pthread_mutex_unlock(&mutex_print);
			}
			memset(fragment, '\0', strlen(fragment));
			strcpy(copy_header, header);  // restores copy of header
		}
		strcpy(copy_start_line, start_line);  // restores copy of start line
	}

	if (strcmp(copy_start_line, "Q4S/1.0 200 OK") == 0) {
		// If there is a Sequence Number parameter in the header
		if (fragment = strstr(copy_header, "Sequence-Number")){
			fragment = fragment + 17;  // moves to the beginning of the value
			char *s_seq_num;
			s_seq_num = strtok(fragment, "\n");  // stores string value
			(q4s_session)->seq_num_confirmed = atoi(s_seq_num);  // converts into int and stores
			memset(fragment, '\0', strlen(fragment));
			strcpy(copy_header, header);  // restores copy of header
			pthread_mutex_lock(&mutex_print);
			printf("Sequence number confirmed: %d\n", (q4s_session)->seq_num_confirmed);
			pthread_mutex_unlock(&mutex_print);
		}
	}
  // If there is a QoS level parameter in the body
	if (fragment = strstr(copy_body, "a=qos-level:")) {
    fragment = fragment + 12;  // moves to the beginning of the value
		char *qos_level_up;
		qos_level_up = strtok(fragment, "/");  // stores string value
		(q4s_session)->qos_level[0] = atoi(qos_level_up);  // converts into int and stores
		char *qos_level_down;
		qos_level_down = strtok(NULL, "\n");  // stores string value
		(q4s_session)->qos_level[1] = atoi(qos_level_down);  // converts into int and stores
		memset(fragment, '\0', strlen(fragment));
		strcpy(copy_body, body);  // restores copy of body
		pthread_mutex_lock(&mutex_print);
		printf("QoS levels stored: %d/%d\n", (q4s_session)->qos_level[0],
		  (q4s_session)->qos_level[1]);
		pthread_mutex_unlock(&mutex_print);
	}

  // If there is an alert pause parameter in the body
	if (fragment = strstr(copy_body, "a=alert-pause:")){
		fragment = fragment + 14;  // moves to the beginning of the value
		char *alert_pause;
		alert_pause = strtok(fragment, "\n");  // stores string value
		(q4s_session)->alert_pause = atoi(alert_pause);  // converts into int and stores
		memset(fragment, '\0', strlen(fragment));
		strcpy(copy_body, body);  // restores copy of body
		pthread_mutex_lock(&mutex_print);
		printf("Alert pause stored: %d\n", (q4s_session)->alert_pause);
		pthread_mutex_unlock(&mutex_print);
	}

  // If there is an latency parameter in the body
	if (fragment = strstr(copy_body, "a=latency:")){
		fragment = fragment + 10;  // moves to the beginning of the value
		char *latency;
		latency = strtok(fragment, "\n");  // stores string value
		(q4s_session)->latency_th = atoi(latency);  // converts into int and stores
		memset(fragment, '\0', strlen(fragment));
		strcpy(copy_body, body);  // restores copy of body
		pthread_mutex_lock(&mutex_print);
		printf("Latency threshold stored: %d\n", (q4s_session)->latency_th);
		pthread_mutex_unlock(&mutex_print);
	}

  // If there is an jitter parameter in the body
	if (fragment = strstr(copy_body, "a=jitter:")) {
    fragment = fragment + 9;  // moves to the beginning of the value
		char *jitter_up;
		jitter_up = strtok(fragment, "/");  // stores string value
		(q4s_session)->jitter_th[0] = atoi(jitter_up);  // converts into int and stores
		char *jitter_down;
		jitter_down = strtok(NULL, "\n");  // stores string value
		(q4s_session)->jitter_th[1] = atoi(jitter_down);  // converts into int and stores
		memset(fragment, '\0', strlen(fragment));
		strcpy(copy_body, body);  // restores copy of body
		pthread_mutex_lock(&mutex_print);
		printf("Jitter thresholds stored: %d/%d\n", (q4s_session)->jitter_th[0],
		  (q4s_session)->jitter_th[1]);
		pthread_mutex_unlock(&mutex_print);
	}

  // If there is an bandwidth parameter in the body
	if (fragment = strstr(copy_body, "a=bandwidth:")) {
    fragment = fragment + 12;  // moves to the beginning of the value
		char *bw_up;
		bw_up = strtok(fragment, "/");  // stores string value
		(q4s_session)->bw_th[0] = atoi(bw_up);  // converts into int and stores
		char *bw_down;
		bw_down = strtok(NULL, "\n");  // stores string value
		(q4s_session)->bw_th[1] = atoi(bw_down);  // converts into int and stores
		memset(fragment, '\0', strlen(fragment));
		strcpy(copy_body, body);  // restores copy of body
		pthread_mutex_lock(&mutex_print);
		printf("Bandwidth thresholds stored: %d/%d\n", (q4s_session)->bw_th[0],
		  (q4s_session)->bw_th[1]);
		pthread_mutex_unlock(&mutex_print);

		if ((q4s_session)->bw_th[0] > 0) {
			int target_bwidth = (q4s_session)->bw_th[0]; // kbps
			int message_size = MESSAGE_BWIDTH_SIZE * 8; // bits
			float messages_fract_per_ms = ((float) target_bwidth / (float) message_size);
			int messages_int_per_ms = floor(messages_fract_per_ms);

			int messages_per_s[10];
			messages_per_s[0] = (int) ((messages_fract_per_ms - (float) messages_int_per_ms) * 1000);
		  int ms_per_message[11];
			ms_per_message[0] = 1;

			int divisor;

			for (int i = 0; i < 10; i++) {
				divisor = 2;
				while ((int) (1000/divisor) > messages_per_s[i]) {
					divisor++;
				}
				ms_per_message[i+1] = divisor;
				if (messages_per_s[i] - (int) (1000/divisor) == 0) {
					break;
				} else if (messages_per_s[i] - (int) (1000/divisor) <= 1) {
          ms_per_message[i+1]--;
					break;
				} else {
					messages_per_s[i+1] = messages_per_s[i] - (int) (1000/divisor);
			  }
		  }
			(q4s_session)->bwidth_messages_per_ms = messages_int_per_ms;
			for (int j = 0; j < sizeof(ms_per_message); j++) {
				if (ms_per_message[j] > 0) {
					(q4s_session)->ms_per_bwidth_message[j] = ms_per_message[j];
				} else {
					break;
				}
			}
		}
	}

  // If there is an packetloss parameter in the body
	if (fragment = strstr(copy_body, "a=packetloss:")) {
    fragment = fragment + 13;  // moves to the beginning of the value
		char *pl_up;
		pl_up = strtok(fragment, "/");  // stores string value
		(q4s_session)->packetloss_th[0] = atof(pl_up);  // converts into int and stores
		char *pl_down;
		pl_down = strtok(NULL, "\n");  // stores string value
		(q4s_session)->packetloss_th[1] = atof(pl_down);  // converts into int and stores
		memset(fragment, '\0', strlen(fragment));
		strcpy(copy_body, body);  // restores copy of body
		pthread_mutex_lock(&mutex_print);
		printf("Packetloss thresholds stored: %.*f/%.*f\n", 2,
		  (q4s_session)->packetloss_th[0], 2, (q4s_session)->packetloss_th[1]);
		pthread_mutex_unlock(&mutex_print);
	}

	// If there is a measurement procedure parameter in the body
	if (fragment = strstr(copy_body, "a=measurement:procedure default(")) {
    fragment = fragment + 32;  // moves to the beginning of the value
		char *ping_interval_negotiation;
		ping_interval_negotiation = strtok(fragment, "/");  // stores string value
		(q4s_session)->ping_clk_negotiation = atoi(ping_interval_negotiation);  // converts into int and stores
    pthread_mutex_lock(&mutex_print);
		printf("Interval between Q4S PING stored (NEGOTIATION): %d\n", (q4s_session)->ping_clk_negotiation);
    pthread_mutex_unlock(&mutex_print);
		char *ping_interval_continuity;
		strtok(NULL, ",");
		ping_interval_continuity = strtok(NULL, "/");  // stores string value
		(q4s_session)->ping_clk_continuity = atoi(ping_interval_continuity);  // converts into int and stores
    pthread_mutex_lock(&mutex_print);
		printf("Interval between Q4S PING stored (CONTINUITY): %d\n", (q4s_session)->ping_clk_continuity);
    pthread_mutex_unlock(&mutex_print);
		char *bwidth_interval;
		strtok(NULL, ",");
		bwidth_interval = strtok(NULL, ",");  // stores string value
		(q4s_session)->bwidth_clk = atoi(bwidth_interval);  // converts into int and stores
    pthread_mutex_lock(&mutex_print);
		printf("Interval between Q4S BWIDTH stored: %d\n", (q4s_session)->bwidth_clk);
    pthread_mutex_unlock(&mutex_print);
		char *window_size_lj;
		window_size_lj = strtok(NULL, "/");  // stores string value
		(q4s_session)->window_size_latency_jitter = atoi(window_size_lj);  // converts into int and stores
    pthread_mutex_lock(&mutex_print);
		printf("Window size for latency and jitter stored: %d\n", (q4s_session)->window_size_latency_jitter);
    pthread_mutex_unlock(&mutex_print);
		char *window_size_pl;
		strtok(NULL, ",");
		window_size_pl = strtok(NULL, "/");  // stores string value
		(q4s_session)->window_size_packetloss = atoi(window_size_pl);  // converts into int and stores
    pthread_mutex_lock(&mutex_print);
		printf("Window size for packet loss stored: %d\n", (q4s_session)->window_size_packetloss);
    pthread_mutex_unlock(&mutex_print);
		memset(fragment, '\0', strlen(fragment));
		strcpy(copy_body, body);  // restores copy of body
	}
}


// FUNCTIONS FOR UPDATING AND STORING MEASURES

// Updates and stores latency measures
void update_latency(type_q4s_session *q4s_session, int latency_measured) {
   int num_samples = 0;
	 int window_size  = (q4s_session)->window_size_latency_jitter;
	 for (int i=0; i < sizeof((q4s_session)->latency_samples); i++) {
		 if ((q4s_session)->latency_samples[i] > 0) {
			 num_samples++;
		 } else {
			 break;
		 }
	 }
	 if (num_samples == window_size && window_size > 0) {
		 for (int j=0; j < num_samples - 1; j++) {
			 (q4s_session)->latency_samples[j] = (q4s_session)->latency_samples[j+1];
		 }
		 (q4s_session)->latency_samples[num_samples - 1] = latency_measured;
		 pthread_mutex_lock(&mutex_print);
 		 printf("\nWindow size reached for latency measure\n");
     pthread_mutex_unlock(&mutex_print);
	 } else {
		 (q4s_session)->latency_samples[num_samples] = latency_measured;
		 num_samples++;
	 }

   sort_array((q4s_session)->latency_samples, num_samples);
	 if (num_samples % 2 == 0) {
		 int median_elem1 = num_samples/2;
		 int median_elem2 = num_samples/2 + 1;
		 (q4s_session)->latency_measure_client = ((q4s_session)->latency_samples[median_elem1-1]
		   + (q4s_session)->latency_samples[median_elem2-1]) / 2;
	 } else {
		 int median_elem = (num_samples + 1) / 2;
		 (q4s_session)->latency_measure_client = (q4s_session)->latency_samples[median_elem-1];
	 }
	 pthread_mutex_lock(&mutex_print);
	 printf("Updated value of latency measure: %d\n", (q4s_session)->latency_measure_client);
	 pthread_mutex_unlock(&mutex_print);
}

// Updates and stores latency measures
void update_jitter(type_q4s_session *q4s_session, int elapsed_time) {
	int num_samples = 0;
	int window_size  = (q4s_session)->window_size_latency_jitter;
	for (int i=0; i < sizeof((q4s_session)->elapsed_time_samples); i++) {
		if ((q4s_session)->elapsed_time_samples[i] > 0) {
			num_samples++;
		} else {
			break;
		}
	}

	if (num_samples == window_size && window_size > 0) {
		for (int j=0; j < num_samples - 1; j++) {
			(q4s_session)->elapsed_time_samples[j] = (q4s_session)->elapsed_time_samples[j+1];
		}
		(q4s_session)->elapsed_time_samples[num_samples - 1] = elapsed_time;
		pthread_mutex_lock(&mutex_print);
		printf("\nWindow size reached for jitter measure\n");
		pthread_mutex_unlock(&mutex_print);
	} else {
		(q4s_session)->elapsed_time_samples[num_samples] = elapsed_time;
		num_samples++;
	}

	if (num_samples <= 1) {
		return;
	} else {
		int elapsed_time_mean = 0;
		for (int j=0; j < num_samples - 1; j++) {
			elapsed_time_mean = elapsed_time_mean + (q4s_session)->elapsed_time_samples[j];
		}
		elapsed_time_mean = elapsed_time_mean / (num_samples - 1);
		(q4s_session)->jitter_measure_client = abs(elapsed_time - elapsed_time_mean);
		pthread_mutex_lock(&mutex_print);
		printf("Updated value of jitter measure: %d\n", (q4s_session)->jitter_measure_client);
    pthread_mutex_unlock(&mutex_print);
		num_jitter_measures_client++;
	}
}

// Updates and stores packetloss measures
void update_packetloss(type_q4s_session *q4s_session, int lost_packets) {
	// Number of packets taken in account for measure
	int num_samples = 0;
	// Number of packets missed
	int num_losses = 0;
	// Maximum number of packets taken in account
	int window_size = (q4s_session)->window_size_packetloss;
	if (window_size == 0 || window_size > MAXNUMSAMPLES - 100) {
		window_size = MAXNUMSAMPLES - 100;
	}

  // Discovers current value of num_samples and num_losses
	for (int i=0; i < sizeof((q4s_session)->packetloss_samples); i++) {
		// Losses are represented with a "1"
		if ((q4s_session)->packetloss_samples[i] > 0) {
			num_losses++;
		} else if ((q4s_session)->packetloss_samples[i] == 0) {
			break;
		}
		// Packets received are represented with a "-1"
		num_samples++;
	}

  // If there are no packets lost in this measure
	if (lost_packets == 0) {
		// Next sample is set to -1
		(q4s_session)->packetloss_samples[num_samples] = -1;
		num_samples++;

		// If window_size has been overcome
		if (num_samples > window_size) {
			pthread_mutex_lock(&mutex_print);
			printf("\nWindow size reached for packetloss measure\n");
			pthread_mutex_unlock(&mutex_print);
			// If first sample is a loss, num_losses is decreased by 1
			if ((q4s_session)->packetloss_samples[0] > 0) {
				num_losses--;
			}
			// Array of samples is moved one position to the left
			for (int k=0; k < num_samples - 1; k++) {
				(q4s_session)->packetloss_samples[k] = (q4s_session)->packetloss_samples[k+1];
			}
			// The old last position is put to 0
			(q4s_session)->packetloss_samples[num_samples - 1] = 0;
			// Decreases num_samples by 1
			num_samples--;
		}

	} else {  // if there are losses
		// If window_size is to be overcome
		if (num_samples+lost_packets+1 > window_size) {
			pthread_mutex_lock(&mutex_print);
			printf("\nWindow size reached for packetloss measure\n");
			pthread_mutex_unlock(&mutex_print);
		}
		// Lost samples are set to 1
		for (int j=0; j < lost_packets; j++) {
			// If window_size has been overcome
			if (num_samples >= window_size) {
				// If first sample is a loss, num_losses is decreased by 1
				if ((q4s_session)->packetloss_samples[0] > 0) {
					num_losses--;
				}
				// Array of samples is moved one position to the left
				for (int k=0; k < num_samples - 1; k++) {
					(q4s_session)->packetloss_samples[k] = (q4s_session)->packetloss_samples[k+1];
				}
				// The old last position is put to 0
				(q4s_session)->packetloss_samples[num_samples - 1] = 0;
				// Decreases num_samples by 1
				num_samples--;
			}
      (q4s_session)->packetloss_samples[num_samples] = 1;
			num_losses++;
			num_samples++;
		}
		// If window_size has been overcome
		if (num_samples >= window_size) {
 		  // If first sample is a loss, num_losses is decreased by 1
 			if ((q4s_session)->packetloss_samples[0] > 0) {
 				num_losses--;
 			}
 			// Array of samples is moved one position to the left
 			for (int k=0; k < num_samples - 1; k++) {
 				(q4s_session)->packetloss_samples[k] = (q4s_session)->packetloss_samples[k+1];
 			}
 			// The old last position is put to 0
 			(q4s_session)->packetloss_samples[num_samples - 1] = 0;
 			// Decreases num_samples by 1
 			num_samples--;
	  }
		// Last sample is set to -1 (received packet)
		(q4s_session)->packetloss_samples[num_samples] = -1;
		num_samples++;
	}

  // Calculates updated value of packetloss
  (q4s_session)->packetloss_measure_client = ((float) num_losses / (float) num_samples);
	pthread_mutex_lock(&mutex_print);
	printf("Updated value of packetloss measure: %.2f\n", (q4s_session)->packetloss_measure_client);
  pthread_mutex_unlock(&mutex_print);
}


// CONNECTION FUNCTIONS

// Connection establishment with Q4S server
int connect_to_server() {
	host = gethostbyname("localhost"); // server assignment
	if (host == NULL) {
		pthread_mutex_lock(&mutex_print);
		printf("Incorrect host\n");
		pthread_mutex_unlock(&mutex_print);
		return -1;
	}
	if ((socket_TCP =  socket(AF_INET, SOCK_STREAM, 0)) < 0) { // socket assignment
    pthread_mutex_lock(&mutex_print);
		printf("Error when assigning the TCP socket: %s\n", strerror(errno));
    pthread_mutex_unlock(&mutex_print);
		return -1;
	}

  // Configures client for the TCP connection
	client_TCP.sin_family = AF_INET; // protocol assignment
	client_TCP.sin_port = htons(CLIENT_PORT_TCP); // port assignment
  client_TCP.sin_addr.s_addr = inet_addr("127.0.0.1"); // IP address assignment (automatic)
	memset(client_TCP.sin_zero, '\0', 8); // fills padding with 0s

	// Assigns port to the client's TCP socket
	if (bind(socket_TCP, (struct sockaddr*)&client_TCP, sizeof(client_TCP)) < 0) {
    pthread_mutex_lock(&mutex_print);
		printf("Error when associating port with TCP socket: %s\n", strerror(errno));
    pthread_mutex_unlock(&mutex_print);
		close(socket_TCP);
		return -1;
	}

  pthread_mutex_lock(&mutex_print);
	printf("\nTCP socket assigned to %s:%d\n", inet_ntoa(client_TCP.sin_addr), htons(client_TCP.sin_port));
  pthread_mutex_unlock(&mutex_print);

	if ((socket_UDP =  socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP)) < 0) { // socket assignment
    pthread_mutex_lock(&mutex_print);
		printf("Error when assigning the UDP socket: %s\n", strerror(errno));
    pthread_mutex_unlock(&mutex_print);
		return -1;
	}

	// Configures client for the UDP socket
	client_UDP.sin_family = AF_INET; // protocol assignment
	client_UDP.sin_port = htons(CLIENT_PORT_UDP); // port assignment
  client_UDP.sin_addr.s_addr = inet_addr("127.0.0.1"); // IP address assignment (automatic)
	memset(client_UDP.sin_zero, '\0', 8); // fills padding with 0s

	// Assigns port to the client's UDP socket
	if (bind(socket_UDP, (struct sockaddr*)&client_UDP, sizeof(client_UDP)) < 0) {
    pthread_mutex_lock(&mutex_print);
		printf("Error when associating port with UDP socket: %s\n", strerror(errno));
    pthread_mutex_unlock(&mutex_print);
		close(socket_TCP);
		return -1;
	}

	pthread_mutex_lock(&mutex_print);
	printf("UDP socket assigned to %s:%d\n", inet_ntoa(client_UDP.sin_addr), htons(client_UDP.sin_port));
  pthread_mutex_unlock(&mutex_print);

	// Especifies parameters of the server (UDP socket)
  server_UDP.sin_family = AF_INET; // protocol assignment
	server_UDP.sin_port = htons(HOST_PORT_UDP); // port assignment
	server_UDP.sin_addr = *((struct in_addr *)host->h_addr); // copies host IP address
  memset(server_TCP.sin_zero, '\0', 8); // fills padding with 0s

  // Especifies parameters of the server (TCP socket)
  server_TCP.sin_family = AF_INET; // protocol assignment
	server_TCP.sin_port = htons(HOST_PORT_TCP); // port assignment
	server_TCP.sin_addr = *((struct in_addr *)host->h_addr); // copies host IP address
  memset(server_TCP.sin_zero, '\0', 8); // fills padding with 0s

  pthread_mutex_lock(&mutex_print);
  printf("Willing to connect to %s:%d\n", inet_ntoa(server_TCP.sin_addr), htons(server_TCP.sin_port));
  pthread_mutex_unlock(&mutex_print);
	// Connects to the host (Q4S server)
	if (connect(socket_TCP,(struct sockaddr *)&server_TCP, sizeof(server_TCP)) < 0) {
    pthread_mutex_lock(&mutex_print);
		printf ("Error when connecting to the host: %s\n", strerror(errno));
    pthread_mutex_unlock(&mutex_print);
		close(socket_TCP);
		return -1;
	}
	pthread_mutex_lock(&mutex_print);
	printf("Connected to %s:%d\n", inet_ntoa(server_TCP.sin_addr), htons(server_TCP.sin_port));
  pthread_mutex_unlock(&mutex_print);
	return 1;
}

// Delivery of Q4S message to the server using TCP
void send_message_TCP (char prepared_message[MAXDATASIZE]) {
  pthread_mutex_lock(&mutex_buffer_TCP);
	memset(buffer_TCP, '\0', sizeof(buffer_TCP));
	// Copies the message into the buffer
	strncpy (buffer_TCP, prepared_message, MAXDATASIZE);
	pthread_mutex_unlock(&mutex_buffer_TCP);

	// Sends the message to the server using the TCP socket assigned
	if (send(socket_TCP, buffer_TCP, MAXDATASIZE, 0) < 0) {
		pthread_mutex_lock(&mutex_print);
		printf("Error when sending TCP data: %s\n", strerror(errno));
		pthread_mutex_unlock(&mutex_print);
		close(socket_TCP);
		exit(0);
		return;
	}

	pthread_mutex_lock(&mutex_print);
	printf("\nI have sent:\n%s\n", buffer_TCP);
	pthread_mutex_unlock(&mutex_print);

  pthread_mutex_lock(&mutex_buffer_TCP);
	memset(buffer_TCP, '\0', sizeof(buffer_TCP));
	pthread_mutex_unlock(&mutex_buffer_TCP);
}

// Delivery of Q4S message to the server using UDP
void send_message_UDP (char prepared_message[MAXDATASIZE]) {
  int slen = sizeof(server_UDP);
	char copy_buffer_UDP[MAXDATASIZE];
	memset(copy_buffer_UDP, '\0', sizeof(copy_buffer_UDP));
  pthread_mutex_lock(&mutex_buffer_UDP);
	memset(buffer_UDP, '\0', sizeof(buffer_UDP));
	// Copies the message into the buffer
	strncpy (buffer_UDP, prepared_message, MAXDATASIZE);
	strncpy (copy_buffer_UDP, buffer_UDP, MAXDATASIZE);
	pthread_mutex_unlock(&mutex_buffer_UDP);

	char *start_line;
	start_line = strtok(copy_buffer_UDP, "\n"); // stores first line of message

	// If it is a Q4S PING, stores the current time
	if (strcmp(start_line, "PING q4s://www.example.com Q4S/1.0") == 0) {
    pthread_mutex_lock(&mutex_tm_latency);
		int result;
		if ((&tm_latency_start1)->seq_number == -1) {
			result = clock_gettime(CLOCK_REALTIME, (&(&tm_latency_start1)->tm));
			(&tm_latency_start1)->seq_number = (&q4s_session)->seq_num_client;
		} else if ((&tm_latency_start2)->seq_number == -1) {
			result = clock_gettime(CLOCK_REALTIME, (&(&tm_latency_start2)->tm));
			(&tm_latency_start2)->seq_number = (&q4s_session)->seq_num_client;
		} else if ((&tm_latency_start3)->seq_number == -1) {
			result = clock_gettime(CLOCK_REALTIME, (&(&tm_latency_start3)->tm));
			(&tm_latency_start3)->seq_number = (&q4s_session)->seq_num_client;
		} else if ((&tm_latency_start4)->seq_number == -1) {
			result = clock_gettime(CLOCK_REALTIME, (&(&tm_latency_start4)->tm));
			(&tm_latency_start4)->seq_number = (&q4s_session)->seq_num_client;
		} else {
			int min_seq_num = min(min((&tm_latency_start1)->seq_number, (&tm_latency_start2)->seq_number),
		    min((&tm_latency_start3)->seq_number, (&tm_latency_start4)->seq_number));
				if (min_seq_num == (&tm_latency_start1)->seq_number) {
					result = clock_gettime(CLOCK_REALTIME, (&(&tm_latency_start1)->tm));
					(&tm_latency_start1)->seq_number = (&q4s_session)->seq_num_client;
				} else if (min_seq_num == (&tm_latency_start2)->seq_number) {
					result = clock_gettime(CLOCK_REALTIME, (&(&tm_latency_start2)->tm));
					(&tm_latency_start2)->seq_number = (&q4s_session)->seq_num_client;
				} else if (min_seq_num == (&tm_latency_start3)->seq_number) {
					result = clock_gettime(CLOCK_REALTIME, (&(&tm_latency_start3)->tm));
					(&tm_latency_start3)->seq_number = (&q4s_session)->seq_num_client;
				} else if (min_seq_num == (&tm_latency_start4)->seq_number) {
					result = clock_gettime(CLOCK_REALTIME, (&(&tm_latency_start4)->tm));
					(&tm_latency_start4)->seq_number = (&q4s_session)->seq_num_client;
				}
		}

		if (result < 0) {
	    pthread_mutex_lock(&mutex_print);
			printf("Error in clock_gettime(): %s\n", strerror(errno));
	    pthread_mutex_unlock(&mutex_print);
			close(socket_UDP);
	    return;
		}
    pthread_mutex_unlock(&mutex_tm_latency);
	}

	// Sends the message to the server using the UDP socket assigned
	if (sendto(socket_UDP, buffer_UDP, MAXDATASIZE, 0, (struct sockaddr *) &server_UDP, slen) < 0) {
		pthread_mutex_lock(&mutex_print);
		printf("Error when sending UDP data: %s\n", strerror(errno));
		pthread_mutex_unlock(&mutex_print);
		close(socket_UDP);
		exit(0);
		return;
	}
	pthread_mutex_lock(&mutex_buffer_UDP);
	memset(copy_buffer_UDP, '\0', sizeof(copy_buffer_UDP));
	strncpy(copy_buffer_UDP, buffer_UDP, MAXDATASIZE);
	memset(buffer_UDP, '\0', sizeof(buffer_UDP));
	pthread_mutex_unlock(&mutex_buffer_UDP);

	pthread_mutex_lock(&mutex_print);
	printf("\nI have sent:\n%s\n", copy_buffer_UDP);
	pthread_mutex_unlock(&mutex_print);
}


// Reception of Q4S messages from the server (TCP socket)
// Thread function that checks if any message has arrived
void *thread_receives_TCP() {
	// Copy of the buffer (to avoid buffer modification)
	char copy_buffer_TCP[MAXDATASIZE];
	memset(copy_buffer_TCP, '\0', sizeof(copy_buffer_TCP));
	char copy_buffer_TCP_2[MAXDATASIZE];
	memset(copy_buffer_TCP_2, '\0', sizeof(copy_buffer_TCP_2));
	while(1) {
		// If error occurs when receiving
	  if (recv(socket_TCP, buffer_TCP, MAXDATASIZE, MSG_WAITALL) < 0) {
			pthread_mutex_lock(&mutex_print);
		  printf("Error when receiving TCP data: %s\n", strerror(errno));
			pthread_mutex_unlock(&mutex_print);
		  close(socket_TCP);
			exit(0);
		  return NULL;
		// If nothing has been received
	  }
		pthread_mutex_lock(&mutex_buffer_TCP);
		if (strlen(buffer_TCP) == 0) {
			pthread_mutex_unlock(&mutex_buffer_TCP);
		  return NULL;
    }
		strcpy(copy_buffer_TCP, buffer_TCP);
		strcpy(copy_buffer_TCP_2, buffer_TCP);
		memset(buffer_TCP, '\0', sizeof(buffer_TCP));
		pthread_mutex_unlock(&mutex_buffer_TCP);
		// Auxiliary variable to identify type of Q4S message
		char *start_line;
		start_line = strtok(copy_buffer_TCP, "\n"); // stores first line of message
		// If it is a Q4S 200 OK message
		if (strcmp(start_line, "Q4S/1.0 200 OK") == 0) {
			// Stores the received message to be analized later
			pthread_mutex_lock(&mutex_session);
			store_message(copy_buffer_TCP_2, &q4s_session.message_received);
		  pthread_mutex_unlock(&mutex_session);

			pthread_mutex_lock(&mutex_print);
			printf("\nI have received a Q4S 200 OK!\n");
			pthread_mutex_unlock(&mutex_print);

			pthread_mutex_lock(&mutex_flags);
			flags |= FLAG_RECEIVE_OK;
			pthread_mutex_unlock(&mutex_flags);
		// If it is a Q4S CANCEL message
	  } else if (strcmp(start_line, "CANCEL q4s://www.example.com Q4S/1.0") == 0) {
			// Stores the received message to be analized later
			pthread_mutex_lock(&mutex_session);
			store_message(copy_buffer_TCP_2, &q4s_session.message_received);
		  pthread_mutex_unlock(&mutex_session);

			pthread_mutex_lock(&mutex_print);
			printf("\nI have received a Q4S CANCEL!\n");
			pthread_mutex_unlock(&mutex_print);

			pthread_mutex_lock(&mutex_flags);
			flags |= FLAG_RECEIVE_CANCEL;
			pthread_mutex_unlock(&mutex_flags);
		} else {
			pthread_mutex_lock(&mutex_print);
		  printf("\nI have received an unidentified message\n");
			pthread_mutex_unlock(&mutex_print);
	  }
	  memset(copy_buffer_TCP, '\0', sizeof(copy_buffer_TCP));
	  memset(copy_buffer_TCP_2, '\0', sizeof(copy_buffer_TCP_2));
  }
}

// Reception of Q4S messages from the server (UDP socket)
// Thread function that checks if any message has arrived
void *thread_receives_UDP() {
	int slen = sizeof(server_UDP);
	// Copy of the buffer (to avoid buffer modification)
	char copy_buffer_UDP[MAXDATASIZE];
	memset(copy_buffer_UDP, '\0', sizeof(copy_buffer_UDP));
	char copy_buffer_UDP_2[MAXDATASIZE];
	memset(copy_buffer_UDP_2, '\0', sizeof(copy_buffer_UDP_2));
	while(1) {
		// If error occurs when receiving
	  if (recvfrom(socket_UDP, buffer_UDP, MAXDATASIZE, MSG_WAITALL,
			(struct sockaddr *) &server_UDP, &slen) < 0) {

			pthread_mutex_lock(&mutex_print);
		  printf("Error when receiving UDP data: %s\n", strerror(errno));
			pthread_mutex_unlock(&mutex_print);
		  close(socket_UDP);
			exit(0);
		  return NULL;
		// If nothing has been received
	  }
		pthread_mutex_lock(&mutex_buffer_UDP);
		if (strlen(buffer_UDP) == 0) {
			pthread_mutex_unlock(&mutex_buffer_UDP);
		  return NULL;
    }
		strcpy(copy_buffer_UDP, buffer_UDP);
		strcpy(copy_buffer_UDP_2, buffer_UDP);
		memset(buffer_UDP, '\0', sizeof(buffer_UDP));
		pthread_mutex_unlock(&mutex_buffer_UDP);
		// Auxiliary variable to identify type of Q4S message
		char *start_line;
		start_line = strtok(copy_buffer_UDP, "\n"); // stores first line of message
		// If it is a Q4S 200 OK message
		if (strcmp(start_line, "Q4S/1.0 200 OK") == 0) {
			char field_seq_num[20];
			strcpy(field_seq_num, "Sequence-Number: ");
			char header[500];
			pthread_mutex_lock(&mutex_session);
			// Stores the received message to be analized later
			store_message(copy_buffer_UDP_2, &q4s_session.message_received);
			strcpy(header, (&q4s_session.message_received)->header);
			pthread_mutex_unlock(&mutex_session);

			if (strstr(header, field_seq_num) != NULL) {
				pthread_mutex_lock(&mutex_tm_latency);
				int result = clock_gettime(CLOCK_REALTIME, (&(&tm_latency_end)->tm));
				if (result < 0) {
			    pthread_mutex_lock(&mutex_print);
					printf("Error in clock_gettime(): %s\n", strerror(errno));
			    pthread_mutex_unlock(&mutex_print);
					close(socket_UDP);
				}
				pthread_mutex_unlock(&mutex_tm_latency);

				pthread_mutex_lock(&mutex_print);
				printf("\nI have received a Q4S 200 OK!\n");
				pthread_mutex_unlock(&mutex_print);

				pthread_mutex_lock(&mutex_session);
	      // Stores parameters of the message (included Sequence Number)
				store_parameters(&q4s_session, &(q4s_session.message_received));
				int seq_num = (&q4s_session)->seq_num_confirmed;
				pthread_mutex_unlock(&mutex_session);

        pthread_mutex_lock(&mutex_tm_latency);
				(&tm_latency_end)->seq_number = seq_num;
				pthread_mutex_unlock(&mutex_tm_latency);

				pthread_mutex_lock(&mutex_flags);
				flags |= FLAG_RECEIVE_OK;
				pthread_mutex_unlock(&mutex_flags);
			} else {
				pthread_mutex_lock(&mutex_print);
			  printf("\nI have received a Q4S 200 OK without sequence number specified\n");
				pthread_mutex_unlock(&mutex_print);
			}
		// If it is a Q4S PING message
		} else if (strcmp(start_line, "PING q4s://www.example.com Q4S/1.0") == 0) {
			char field_seq_num[20];
			strcpy(field_seq_num, "Sequence-Number: ");
			char header[500];
			pthread_mutex_lock(&mutex_session);
			// Stores the received message to be analized later
			store_message(copy_buffer_UDP_2, &q4s_session.message_received);
			strcpy(header, (&q4s_session.message_received)->header);
			pthread_mutex_unlock(&mutex_session);

			if (strstr(header, field_seq_num) != NULL) {
				pthread_mutex_lock(&mutex_print);
			  printf("\nI have received a Q4S PING!\n");
	      pthread_mutex_unlock(&mutex_print);

				int seq_num_before;
				int seq_num_after;
				int num_losses;

				pthread_mutex_lock(&mutex_session);
				seq_num_before = (&q4s_session)->seq_num_server;
	      // Stores parameters of the message (included Sequence Number)
				store_parameters(&q4s_session, &(q4s_session.message_received));
				seq_num_after = (&q4s_session)->seq_num_server;
				num_losses = seq_num_after - (seq_num_before + 1);
				num_packet_lost = num_losses;
				pthread_mutex_unlock(&mutex_session);

				pthread_mutex_lock(&mutex_tm_jitter);
				if (num_losses > 0) {
					num_ping = 0;
				}
				num_ping++;
				int result = 0;
				if (num_ping % 2 != 0) {
					result = clock_gettime(CLOCK_REALTIME, &tm1_jitter);
				} else {
					result = clock_gettime(CLOCK_REALTIME, &tm2_jitter);
				}
				if (result < 0) {
					pthread_mutex_lock(&mutex_print);
					printf("Error in clock_gettime(): %s\n", strerror(errno));
					pthread_mutex_unlock(&mutex_print);
					close(socket_UDP);
				}
				pthread_mutex_unlock(&mutex_tm_jitter);

			  pthread_mutex_lock(&mutex_flags);
			  flags |= FLAG_RECEIVE_PING;
			  pthread_mutex_unlock(&mutex_flags);

			} else {
				pthread_mutex_lock(&mutex_print);
			  printf("\nI have received a Q4S PING without sequence number specified\n");
				pthread_mutex_unlock(&mutex_print);
			}
		// If it is a Q4S BWIDTH message
	  } else if (strcmp(start_line, "BWIDTH q4s://www.example.com Q4S/1.0") == 0) {
			char field_seq_num[20];
			strcpy(field_seq_num, "Sequence-Number: ");
			char header[500];
			pthread_mutex_lock(&mutex_session);
			// Stores the received message to be analized later
			store_message(copy_buffer_UDP_2, &q4s_session.message_received);
			strcpy(header, (&q4s_session.message_received)->header);
			pthread_mutex_unlock(&mutex_session);

			if (strstr(header, field_seq_num) != NULL) {
				pthread_mutex_lock(&mutex_print);
				printf("\nI have received a Q4S BWIDTH!\n");
				pthread_mutex_unlock(&mutex_print);

				int seq_num_before;
				int seq_num_after;
				int num_losses;

				pthread_mutex_lock(&mutex_session);
				num_bwidth_received++;
				seq_num_before = (&q4s_session)->seq_num_server;
	      // Stores parameters of the message (included Sequence Number)
				store_parameters(&q4s_session, &(q4s_session.message_received));
				seq_num_after = (&q4s_session)->seq_num_server;
				num_losses = seq_num_after - (seq_num_before + 1);
				num_packet_lost = num_losses;
				pthread_mutex_unlock(&mutex_session);

				pthread_mutex_lock(&mutex_flags);
			  flags |= FLAG_RECEIVE_BWIDTH;
			  pthread_mutex_unlock(&mutex_flags);

			} else {
				pthread_mutex_lock(&mutex_print);
			  printf("\nI have received a Q4S BWIDTH without sequence number specified\n");
				pthread_mutex_unlock(&mutex_print);
			}

		} else {
			pthread_mutex_lock(&mutex_print);
		  printf("\nI have received an unidentified message\n");
			pthread_mutex_unlock(&mutex_print);
	  }
	  memset(copy_buffer_UDP, '\0', sizeof(copy_buffer_UDP));
	  memset(copy_buffer_UDP_2, '\0', sizeof(copy_buffer_UDP_2));
  }
}

// TIMER FUNCTIONS

// Activates a flag when ping timeout has occurred
void *ping_timeout() {
	int ping_clk;
	while(1) {
		pthread_mutex_lock(&mutex_session);
		ping_clk = q4s_session.ping_clk_negotiation;
		pthread_mutex_unlock(&mutex_session);

    delay(ping_clk);

		pthread_mutex_lock(&mutex_flags);
		flags |= FLAG_TEMP_PING_0;
		pthread_mutex_unlock(&mutex_flags);
	}
}


// Activates a flag when bwidth timeout has occurred
void *bwidth_timeout() {
	int bwidth_clk; // time established for BWIDTH delivery
	int messages_per_ms; // BWIDTH messages to send each 1 ms
	int ms_per_message[11]; // intervals of ms for sending BWIDTH messages
	int ms_delayed; // ms passed

  while(1) {
		// Initializes parameters needed
		ms_delayed = 0;
		pthread_mutex_lock(&mutex_session);
		bwidth_clk = q4s_session.bwidth_clk;
		messages_per_ms = q4s_session.bwidth_messages_per_ms;
		for (int i = 0; i < sizeof(ms_per_message); i++) {
			if (q4s_session.ms_per_bwidth_message[i] > 0) {
				ms_per_message[i] = q4s_session.ms_per_bwidth_message[i];
			} else {
				break;
			}
		}
		pthread_mutex_unlock(&mutex_session);

	  pthread_mutex_lock(&mutex_print);
		printf("\nBwidth clk: %d", bwidth_clk);
		printf("\nMessages per ms: %d", messages_per_ms);
		for (int i = 0; i < sizeof(ms_per_message); i++) {
			if (ms_per_message[i] > 0) {
				printf("\nMs per message (%d): %d\n", i+1, ms_per_message[i]);
			} else {
				break;
			}
		}
		pthread_mutex_unlock(&mutex_print);

		// Start of Q4S BWIDTH delivery
		while(1) {
			// Sends a number specified of 1kB BWIDTH messages per 1 ms
			int j = 0;
			while (j < messages_per_ms) {
				pthread_mutex_lock(&mutex_flags);
				flags |= FLAG_TEMP_BWIDTH;
				pthread_mutex_unlock(&mutex_flags);
				j++;
			}
			// Sends (when appropiate) 1 or more extra BWIDTH message(s) of 1kB
			for (int k = 1; k < sizeof(ms_per_message); k++) {
				if (ms_per_message[k] > 0 && ms_delayed % ms_per_message[k] == 0) {
					pthread_mutex_lock(&mutex_flags);
					flags |= FLAG_TEMP_BWIDTH;
					pthread_mutex_unlock(&mutex_flags);
				}
			}
			// When the interval bwidth_clk has finished, delivery is completed
			// Now we update the value of bandwidth measured and send a last Q4S BWIDTH
			if (ms_delayed == bwidth_clk) {
				pthread_mutex_lock(&mutex_session);
				(&q4s_session)->bw_measure_client = (num_bwidth_received * 8000 / bwidth_clk);
        pthread_mutex_unlock(&mutex_session);
				num_bwidth_received = 0;
				pthread_mutex_lock(&mutex_flags);
				flags |= FLAG_TEMP_BWIDTH;
				pthread_mutex_unlock(&mutex_flags);
				break;
			}
			// Delays 1 ms
			delay(ms_per_message[0]);
			ms_delayed++;
		}
	}
}


// CHECK FUNCTIONS OF STATE MACHINE

// Checks if client wants to connect server
int check_connect (fsm_t* this) {
	int result;
	// Lock to guarantee mutual exclusion
	pthread_mutex_lock(&mutex_flags);
	result = (flags & FLAG_CONNECT);
  pthread_mutex_unlock(&mutex_flags);
  return result;
}

// Checks if a client wants to send a Q4S BEGIN to the server
int check_begin (fsm_t* this) {
	int result;
	// Lock to guarantee mutual exclusion
	pthread_mutex_lock(&mutex_flags);
	result = (flags & FLAG_BEGIN);
  pthread_mutex_unlock(&mutex_flags);
  return result;
}

// Checks if a Q4S 200 OK message has been received from the server
int check_receive_ok (fsm_t* this) {
	int result;
	// Lock to guarantee mutual exclusion
  pthread_mutex_lock(&mutex_flags);
	result = (flags & FLAG_RECEIVE_OK);
  pthread_mutex_unlock(&mutex_flags);
	return result;
}

// Checks if client wants to go to Stage 0
int check_go_to_0 (fsm_t* this) {
	int result;
	// Lock to guarantee mutual exclusion
	pthread_mutex_lock(&mutex_flags);
	result = (flags & FLAG_GO_TO_0);
  pthread_mutex_unlock(&mutex_flags);
	return result;
}

// Checks if client wants to go to Stage 1
int check_go_to_1 (fsm_t* this) {
	int result;
	// Lock to guarantee mutual exclusion
	pthread_mutex_lock(&mutex_flags);
	result = (flags & FLAG_GO_TO_1);
  pthread_mutex_unlock(&mutex_flags);
	return result;
}

// Checks if client wants to go to Stage 2
int check_go_to_2 (fsm_t* this) {
	int result;
	// Lock to guarantee mutual exclusion
	pthread_mutex_lock(&mutex_flags);
	result = (flags & FLAG_GO_TO_2);
  pthread_mutex_unlock(&mutex_flags);
	return result;
}

// Checks if ping timeout has occurred in Stage 0
int check_temp_ping_0 (fsm_t* this) {
	int result;
	// Lock to guarantee mutual exclusion
	pthread_mutex_lock(&mutex_flags);
	result = (flags & FLAG_TEMP_PING_0);
  pthread_mutex_unlock(&mutex_flags);
	return result;
}

// Checks if ping timeout has occurred in Stage 2
int check_temp_ping_2 (fsm_t* this) {
	int result;
	// Lock to guarantee mutual exclusion
	pthread_mutex_lock(&mutex_flags);
	result = (flags & FLAG_TEMP_PING_2);
  pthread_mutex_unlock(&mutex_flags);
	return result;
}

// Checks if q4s client has received a Q4S PING from server
int check_receive_ping (fsm_t* this) {
	int result;
	// Lock to guarantee mutual exclusion
	pthread_mutex_lock(&mutex_flags);
	result = (flags & FLAG_RECEIVE_PING);
  pthread_mutex_unlock(&mutex_flags);
	return result;
}

// Checks if ping measure is to be finished
int check_finish_ping (fsm_t* this) {
	int result;
	// Lock to guarantee mutual exclusion
	pthread_mutex_lock(&mutex_flags);
	result = (flags & FLAG_FINISH_PING);
  pthread_mutex_unlock(&mutex_flags);
	return result;
}

// Checks if bwidth timeout has occurred
int check_temp_bwidth (fsm_t* this) {
	int result;
	// Lock to guarantee mutual exclusion
	pthread_mutex_lock(&mutex_flags);
	result = (flags & FLAG_TEMP_BWIDTH);
  pthread_mutex_unlock(&mutex_flags);
	return result;
}

// Checks if q4s client has received a Q4S BWIDTH from server
int check_receive_bwidth (fsm_t* this) {
	int result;
	// Lock to guarantee mutual exclusion
	pthread_mutex_lock(&mutex_flags);
	result = (flags & FLAG_RECEIVE_BWIDTH);
  pthread_mutex_unlock(&mutex_flags);
	return result;
}

// Checks if bwidth measure is to be finished
int check_finish_bwidth (fsm_t* this) {
	int result;
	// Lock to guarantee mutual exclusion
	pthread_mutex_lock(&mutex_flags);
	result = (flags & FLAG_FINISH_BWIDTH);
  pthread_mutex_unlock(&mutex_flags);
	return result;
}

// Checks if client wants to cancel Q4S session
int check_cancel (fsm_t* this) {
	int result;
	// Lock to guarantee mutual exclusion
	pthread_mutex_lock(&mutex_flags);
	result = (flags & FLAG_CANCEL);
  pthread_mutex_unlock(&mutex_flags);
	return result;
}

// Checks if q4s client has received a Q4S CANCEL from server
int check_receive_cancel (fsm_t* this) {
	int result;
	// Lock to guarantee mutual exclusion
	pthread_mutex_lock(&mutex_flags);
	result = (flags & FLAG_RECEIVE_CANCEL);
  pthread_mutex_unlock(&mutex_flags);
	return result;
}


// ACTION FUNCTIONS OF STATE MACHINE

// Prepares for Q4S session
void Setup (fsm_t* fsm) {
	// Lock to guarantee mutual exclusion
	pthread_mutex_lock(&mutex_flags);
	// Puts every FLAG to 0
	flags = 0;
	pthread_mutex_unlock(&mutex_flags);
	// Tries to connect to Q4S server
	if (connect_to_server() < 0) {
		pthread_mutex_lock(&mutex_print);
		printf("Error when connecting to server\n");
    pthread_mutex_unlock(&mutex_print);
		exit(0);
	} else {
		// Initialize auxiliary variables
		num_packet_lost = 0;
		num_ping = 0;
		// Initialize session variables
		pthread_mutex_lock(&mutex_session);
	  q4s_session.session_id = -1;
		q4s_session.seq_num_server = -1;
		q4s_session.seq_num_client = 0;
		q4s_session.qos_level[0] = -1;
		q4s_session.qos_level[1] = -1;
		q4s_session.alert_pause = -1;
		q4s_session.latency_th = -1;
		q4s_session.jitter_th[0] = -1;
		q4s_session.jitter_th[1] = -1;
		q4s_session.bw_th[0] = -1;
		q4s_session.bw_th[1] = -1;
		q4s_session.packetloss_th[0] = -1;
		q4s_session.packetloss_th[1] = -1;
		q4s_session.packetloss_measure_client = -1;
		pthread_mutex_unlock(&mutex_session);

		pthread_mutex_lock(&mutex_print);
		printf("\nPress 'b' to send a Q4S BEGIN\n");
		pthread_mutex_unlock(&mutex_print);
		// Throws a thread to check the arrival of Q4S messages using TCP
		pthread_create(&receive_TCP_thread, NULL, (void*)thread_receives_TCP, NULL);
	}
}

// Creates and sends a Q4S BEGIN message to the server
void Begin (fsm_t* fsm) {
	// Lock to guarantee mutual exclusion
	pthread_mutex_lock(&mutex_flags);
  flags &= ~FLAG_BEGIN;
  pthread_mutex_unlock(&mutex_flags);


	pthread_mutex_lock(&mutex_session);
	// Fills q4s_session.message_to_send with the Q4S BEGIN parameters
	create_begin(&q4s_session.message_to_send);
  // Converts q4s_session.message_to_send into a message with correct format (prepared_message)
	prepare_message(&(q4s_session.message_to_send), q4s_session.prepared_message);
	// Sends the prepared message
  send_message_TCP(q4s_session.prepared_message);
	pthread_mutex_unlock(&mutex_session);
}

// Stores parameters received in the first 200 OK message from server
void Store (fsm_t* fsm) {
	// Lock to guarantee mutual exclusion
	pthread_mutex_lock(&mutex_flags);
  flags &= ~FLAG_RECEIVE_OK;
  pthread_mutex_unlock(&mutex_flags);

	// Stores parameters of message received
	pthread_mutex_lock(&mutex_session);
  store_parameters(&q4s_session, &(q4s_session.message_received));

	// If there are latency or jitter thresholds established
	if ((&q4s_session)->latency_th > 0 || (&q4s_session)->jitter_th[0] > 0
	  || (&q4s_session)->jitter_th[1] > 0) {
		pthread_mutex_unlock(&mutex_session);

		pthread_mutex_lock(&mutex_print);
		printf("\nGoing to Stage 0 (measure of latency, jitter and packetloss)\n");
		pthread_mutex_unlock(&mutex_print);
		// Lock to guarantee mutual exclusion
		pthread_mutex_lock(&mutex_flags);
		flags |= FLAG_GO_TO_0;
		pthread_mutex_unlock(&mutex_flags);
	// If there is a bandwidth threshold established
  } else if ((&q4s_session)->bw_th[0] > 0 ||  (&q4s_session)->bw_th[1] > 0) {
	  pthread_mutex_unlock(&mutex_session);

		pthread_mutex_lock(&mutex_print);
		printf("\nGoing to Stage 1 (measure of bandwidth and packetloss)\n");
		pthread_mutex_unlock(&mutex_print);

		// Lock to guarantee mutual exclusion
		pthread_mutex_lock(&mutex_flags);
		flags |= FLAG_GO_TO_1;
		pthread_mutex_unlock(&mutex_flags);
		// If there is a bandwidth threshold established
	} else if ((&q4s_session)->packetloss_th[0] > 0 ||  (&q4s_session)->packetloss_th[1] > 0) {
		pthread_mutex_unlock(&mutex_session);

		pthread_mutex_lock(&mutex_print);
		printf("\nGoing to Stage 0 (measure of latency, jitter and packetloss)\n");
		pthread_mutex_unlock(&mutex_print);

		// Lock to guarantee mutual exclusion
		pthread_mutex_lock(&mutex_flags);
		flags |= FLAG_GO_TO_0;
		pthread_mutex_unlock(&mutex_flags);
	} else {
		pthread_mutex_unlock(&mutex_session);

		pthread_mutex_lock(&mutex_print);
		printf("\nThere are no thresholds established for QoS parameters\n");
		printf("Press 'c' to send a Q4S CANCEL to the server\n");
		pthread_mutex_unlock(&mutex_print);
	}
}

// Creates and sends a Q4S READY 0 message to the server
void Ready0 (fsm_t* fsm) {
	// Lock to guarantee mutual exclusion
	pthread_mutex_lock(&mutex_flags);
  flags &= ~FLAG_GO_TO_0;
  pthread_mutex_unlock(&mutex_flags);

  pthread_mutex_lock(&mutex_session);
	// Fills q4s_session.message_to_send with the Q4S READY 0 parameters
	create_ready0(&q4s_session.message_to_send);
  // Converts q4s_session.message_to_send into a message with correct format (prepared_message)
	prepare_message(&(q4s_session.message_to_send), q4s_session.prepared_message);
	// Sends the prepared message
	send_message_TCP(q4s_session.prepared_message);
	pthread_mutex_unlock(&mutex_session);
}

// Creates and sends a Q4S READY 1 message to the server
void Ready1 (fsm_t* fsm) {
	// Lock to guarantee mutual exclusion
	pthread_mutex_lock(&mutex_flags);
  flags &= ~FLAG_GO_TO_1;
  pthread_mutex_unlock(&mutex_flags);


	pthread_mutex_lock(&mutex_session);
  // Fills q4s_session.message_to_send with the Q4S READY 1 parameters
	create_ready1(&q4s_session.message_to_send);
	// Converts q4s_session.message_to_send into a message with correct format (prepared_message)
	prepare_message(&(q4s_session.message_to_send), q4s_session.prepared_message);
	// Sends the prepared message
	send_message_TCP(q4s_session.prepared_message);
	pthread_mutex_unlock(&mutex_session);
}

// Creates and sends a Q4S READY 2 message to the server
void Ready2 (fsm_t* fsm) {
	// Lock to guarantee mutual exclusion
	pthread_mutex_lock(&mutex_flags);
  flags &= ~FLAG_GO_TO_2;
  pthread_mutex_unlock(&mutex_flags);


	pthread_mutex_lock(&mutex_session);
  // Fills q4s_session.message_to_send with the Q4S READY 1 parameters
	create_ready2(&q4s_session.message_to_send);
	// Converts q4s_session.message_to_send into a message with correct format (prepared_message)
	prepare_message(&(q4s_session.message_to_send), q4s_session.prepared_message);
	// Sends the prepared message
	send_message_TCP(q4s_session.prepared_message);
	pthread_mutex_unlock(&mutex_session);
}

// Stores parameters received in the 200 OK message from the server
// Starts the timer for ping delivery
// Sends a Q4S PING message
void Ping_Init (fsm_t* fsm) {
	// Lock to guarantee mutual exclusion
	pthread_mutex_lock(&mutex_flags);
  flags &= ~FLAG_RECEIVE_OK;
  pthread_mutex_unlock(&mutex_flags);

	// Stores parameters of message received (if present)
  pthread_mutex_lock(&mutex_session);
	store_parameters(&q4s_session, &(q4s_session.message_received));
	pthread_mutex_unlock(&mutex_session);

	// Throws a thread to check the arrival of Q4S messages using UDP
	pthread_create(&receive_UDP_thread, NULL, (void*)thread_receives_UDP, NULL);

	// Initialize auxiliary variables
	num_packet_lost = 0;
	num_ping = 0;
	num_latency_measures_client = 0;
	num_jitter_measures_client = 0;
	num_packetloss_measures_client = 0;
	num_latency_measures_server = 0;
	num_jitter_measures_server = 0;
	num_packetloss_measures_server = 0;
	num_bwidth_received = 0;

  pthread_mutex_lock(&mutex_tm_latency);
	(&tm_latency_start1)->seq_number = -1;
	(&tm_latency_start2)->seq_number = -1;
	(&tm_latency_start3)->seq_number = -1;
	(&tm_latency_start4)->seq_number = -1;
	(&tm_latency_end)->seq_number = -1;
	pthread_mutex_unlock(&mutex_tm_latency);

	pthread_mutex_lock(&mutex_session);
	// Initialize session variables
	q4s_session.seq_num_server = -1;
	q4s_session.seq_num_client = 0;
	q4s_session.packetloss_measure_client = -1;
	memset((&q4s_session)->latency_samples, 0, MAXNUMSAMPLES);
	memset((&q4s_session)->elapsed_time_samples, 0, MAXNUMSAMPLES);
	memset((&q4s_session)->packetloss_samples, 0, MAXNUMSAMPLES);
	memset((&q4s_session)->bw_samples, 0, MAXNUMSAMPLES);
	// Fills q4s_session.message_to_send with the Q4S PING parameters
	create_ping(&q4s_session.message_to_send);
	// Converts q4s_session.message_to_send into a message with correct format (prepared_message)
	prepare_message(&(q4s_session.message_to_send), q4s_session.prepared_message);
	// Sends the prepared message
	send_message_UDP(q4s_session.prepared_message);

	// Initialize ping period if necessary
	if (q4s_session.ping_clk_negotiation <= 0) {
		q4s_session.ping_clk_negotiation = 200;
	}
	// Initialize ping period if necessary
	if (q4s_session.ping_clk_continuity <= 0) {
		q4s_session.ping_clk_continuity = 200;
	}
	pthread_mutex_unlock(&mutex_session);
	// Starts timer for ping delivery
	pthread_create(&timer_ping, NULL, (void*)ping_timeout, NULL);

  // Simulates 1 packet loss
	pthread_mutex_lock(&mutex_session);
  (&q4s_session)->seq_num_client++;
	pthread_mutex_unlock(&mutex_session);
}

// Sends a Q4S PING message
void Ping (fsm_t* fsm) {
	// Lock to guarantee mutual exclusion
	pthread_mutex_lock(&mutex_flags);
  flags &= ~FLAG_TEMP_PING_0;
  pthread_mutex_unlock(&mutex_flags);

	pthread_mutex_lock(&mutex_session);
  (&q4s_session)->seq_num_client++;
	// Fills q4s_session.message_to_send with the Q4S PING parameters
	create_ping(&q4s_session.message_to_send);
	// Converts q4s_session.message_to_send into a message with correct format (prepared_message)
	prepare_message(&(q4s_session.message_to_send), q4s_session.prepared_message);
	// Sends the prepared message
	send_message_UDP(q4s_session.prepared_message);
	pthread_mutex_unlock(&mutex_session);
}

// Updates Q4S measures
void Update (fsm_t* fsm) {
	pthread_mutex_lock(&mutex_flags);
	if (flags & FLAG_RECEIVE_PING) {
		flags &= ~FLAG_RECEIVE_PING;
	  pthread_mutex_unlock(&mutex_flags);

		pthread_mutex_lock(&mutex_session);
		int num_losses = num_packet_lost;
		num_packet_lost = 0;
		pthread_mutex_unlock(&mutex_session);

		pthread_mutex_lock(&mutex_tm_jitter);
		pthread_mutex_lock(&mutex_session);
    if ((&q4s_session)->jitter_th[1] > 0) {
			pthread_mutex_unlock(&mutex_session);
			int elapsed_time;

			if (num_ping % 2 == 0 && num_ping > 0) {
				elapsed_time = ms_elapsed(tm1_jitter, tm2_jitter);

				pthread_mutex_lock(&mutex_print);
				printf("Elapsed time stored: %d\n", elapsed_time);
				pthread_mutex_unlock(&mutex_print);

	      pthread_mutex_lock(&mutex_session);
				update_jitter(&q4s_session, elapsed_time);
				pthread_mutex_unlock(&mutex_session);

			} else if (num_ping % 2 != 0 && num_ping > 1) {
				elapsed_time = ms_elapsed(tm2_jitter, tm1_jitter);

				pthread_mutex_lock(&mutex_print);
				printf("Elapsed time stored: %d\n",elapsed_time);
				pthread_mutex_unlock(&mutex_print);

				pthread_mutex_lock(&mutex_session);
				update_jitter(&q4s_session, elapsed_time);
				pthread_mutex_unlock(&mutex_session);
			}
		} else {
			pthread_mutex_unlock(&mutex_session);
		}
		pthread_mutex_unlock(&mutex_tm_jitter);

		pthread_mutex_lock(&mutex_session);
		if ((&q4s_session)->jitter_th[0] > 0 && (&q4s_session)->jitter_measure_server > (&q4s_session)->jitter_th[0]) {
			pthread_mutex_lock(&mutex_print);
			printf("Upstream jitter exceeds the threshold\n");
			pthread_mutex_unlock(&mutex_print);
			num_jitter_measures_server = 0;
		} else if ((&q4s_session)->jitter_th[1] > 0 && (&q4s_session)->jitter_measure_client > (&q4s_session)->jitter_th[1]) {
			pthread_mutex_lock(&mutex_print);
			printf("Downstream jitter exceeds the threshold\n");
			pthread_mutex_unlock(&mutex_print);
			num_jitter_measures_client = 0;
		}
		pthread_mutex_unlock(&mutex_session);

    pthread_mutex_lock(&mutex_session);
		if ((&q4s_session)->packetloss_th[1] > 0) {
			if (num_losses > 0) {
				pthread_mutex_lock(&mutex_print);
				printf("\nLoss of %d Q4S PING(s) detected\n", num_losses);
				pthread_mutex_unlock(&mutex_print);
			}
			update_packetloss(&q4s_session, num_losses);
			num_packetloss_measures_client++;
		}
		pthread_mutex_unlock(&mutex_session);

    pthread_mutex_lock(&mutex_session);
		if ((&q4s_session)->packetloss_th[0] > 0 && (&q4s_session)->packetloss_measure_server > (&q4s_session)->packetloss_th[0]) {
			pthread_mutex_lock(&mutex_print);
			printf("Upstream packetloss exceeds the threshold\n");
			pthread_mutex_unlock(&mutex_print);
			num_packetloss_measures_server = 0;
		} else if ((&q4s_session)->packetloss_th[1] > 0 && (&q4s_session)->packetloss_measure_client > (&q4s_session)->packetloss_th[1]) {
			pthread_mutex_lock(&mutex_print);
			printf("Downstream packetloss exceeds the threshold\n");
			pthread_mutex_unlock(&mutex_print);
			num_packetloss_measures_client = 0;
		}
		pthread_mutex_unlock(&mutex_session);


    pthread_mutex_lock(&mutex_session);
		// Fills q4s_session.message_to_send with the Q4S PING parameters
		create_200(&q4s_session.message_to_send);
		// Converts q4s_session.message_to_send into a message with correct format (prepared_message
		prepare_message(&(q4s_session.message_to_send), q4s_session.prepared_message);
		// Sends the prepared message
		send_message_UDP(q4s_session.prepared_message);
		pthread_mutex_unlock(&mutex_session);
		return;
	} else if (flags & FLAG_RECEIVE_OK) {
		flags &= ~FLAG_RECEIVE_OK;
		pthread_mutex_unlock(&mutex_flags);

    pthread_mutex_lock(&mutex_session);
    if ((&q4s_session)->latency_th > 0) {
			pthread_mutex_unlock(&mutex_session);

			pthread_mutex_lock(&mutex_tm_latency);
			if ((&tm_latency_start1)->seq_number == (&tm_latency_end)->seq_number) {
				int rtt = ms_elapsed((&tm_latency_start1)->tm, (&tm_latency_end)->tm);

				pthread_mutex_lock(&mutex_print);
				printf("\nRTT is: %d ms, and latency is: %d ms\n", rtt, rtt/2);
				pthread_mutex_unlock(&mutex_print);

				pthread_mutex_lock(&mutex_session);
				update_latency(&q4s_session, rtt/2);
				pthread_mutex_unlock(&mutex_session);

				num_latency_measures_client++;
			} else if ((&tm_latency_start2)->seq_number == (&tm_latency_end)->seq_number) {
				int rtt = ms_elapsed((&tm_latency_start2)->tm, (&tm_latency_end)->tm);

				pthread_mutex_lock(&mutex_print);
				printf("\nRTT is: %d ms, and latency is: %d ms\n", rtt, rtt/2);
				pthread_mutex_unlock(&mutex_print);

				pthread_mutex_lock(&mutex_session);
				update_latency(&q4s_session, rtt/2);
				pthread_mutex_unlock(&mutex_session);

				num_latency_measures_client++;
			} else if ((&tm_latency_start3)->seq_number == (&tm_latency_end)->seq_number) {
				int rtt = ms_elapsed((&tm_latency_start3)->tm, (&tm_latency_end)->tm);

				pthread_mutex_lock(&mutex_print);
				printf("\nRTT is: %d ms, and latency is: %d ms\n", rtt, rtt/2);
				pthread_mutex_unlock(&mutex_print);

				pthread_mutex_lock(&mutex_session);
				update_latency(&q4s_session, rtt/2);
				pthread_mutex_unlock(&mutex_session);

				num_latency_measures_client++;
			} else if ((&tm_latency_start4)->seq_number == (&tm_latency_end)->seq_number) {
				int rtt = ms_elapsed((&tm_latency_start4)->tm, (&tm_latency_end)->tm);

				pthread_mutex_lock(&mutex_print);
				printf("\nRTT is: %d ms, and latency is: %d ms\n", rtt, rtt/2);
				pthread_mutex_unlock(&mutex_print);

				pthread_mutex_lock(&mutex_session);
				update_latency(&q4s_session, rtt/2);
				pthread_mutex_unlock(&mutex_session);

				num_latency_measures_client++;
			}
			pthread_mutex_unlock(&mutex_tm_latency);
		} else {
			  pthread_mutex_unlock(&mutex_session);
		}

		pthread_mutex_lock(&mutex_session);
		if ((&q4s_session)->latency_th > 0 && (&q4s_session)->latency_measure_server > (&q4s_session)->latency_th) {
			pthread_mutex_lock(&mutex_print);
			printf("Latency measured by server exceeds the threshold\n");
			pthread_mutex_unlock(&mutex_print);
			num_latency_measures_server = 0;
		} else if ((&q4s_session)->latency_th > 0 && (&q4s_session)->latency_measure_client > (&q4s_session)->latency_th) {
			pthread_mutex_lock(&mutex_print);
			printf("Latency measured by client exceeds the threshold\n");
			pthread_mutex_unlock(&mutex_print);
			num_latency_measures_client = 0;
		}
		pthread_mutex_unlock(&mutex_session);

	} else if (flags & FLAG_RECEIVE_BWIDTH) {
	  flags &= ~FLAG_RECEIVE_BWIDTH;
    pthread_mutex_unlock(&mutex_flags);

		pthread_mutex_lock(&mutex_session);
		int num_losses = num_packet_lost;
		num_packet_lost = 0;
		pthread_mutex_unlock(&mutex_session);

		pthread_mutex_lock(&mutex_session);
		if ((&q4s_session)->packetloss_th[1] > 0) {
			if (num_losses > 0) {
				pthread_mutex_lock(&mutex_print);
				printf("\nLoss of %d Q4S BWIDTH(s) detected\n", num_losses);
				pthread_mutex_unlock(&mutex_print);
			}
			update_packetloss(&q4s_session, num_losses);
			num_packetloss_measures_client++;
		}
		pthread_mutex_unlock(&mutex_session);

    pthread_mutex_lock(&mutex_session);
		if ((&q4s_session)->packetloss_th[0] > 0 && (&q4s_session)->packetloss_measure_server > (&q4s_session)->packetloss_th[0]) {
			pthread_mutex_lock(&mutex_print);
			printf("Upstream packetloss exceeds the threshold\n");
			pthread_mutex_unlock(&mutex_print);
			num_packetloss_measures_server = 0;
		} else if ((&q4s_session)->packetloss_th[1] > 0 && (&q4s_session)->packetloss_measure_client > (&q4s_session)->packetloss_th[1]) {
			pthread_mutex_lock(&mutex_print);
			printf("Downstream packetloss exceeds the threshold\n");
			pthread_mutex_unlock(&mutex_print);
			num_packetloss_measures_client = 0;
		}
		pthread_mutex_unlock(&mutex_session);

		pthread_mutex_lock(&mutex_session);
		if ((&q4s_session)->bw_th[0] > 0 && (&q4s_session)->bw_measure_server > 0
		  && (&q4s_session)->bw_measure_server < (&q4s_session)->bw_th[0]) {
			pthread_mutex_lock(&mutex_print);
			printf("Upstream bandwidth doesn't reach the threshold\n");
			pthread_mutex_unlock(&mutex_print);
			num_bwidth_received = 0;
		} else if ((&q4s_session)->bw_th[1] > 0 && (&q4s_session)->bw_measure_client > 0
		  && (&q4s_session)->bw_measure_client < (&q4s_session)->bw_th[1]) {
			pthread_mutex_lock(&mutex_print);
			printf("Downstream bandwidth doesn't reach the threshold\n");
			pthread_mutex_unlock(&mutex_print);
		}
		pthread_mutex_unlock(&mutex_session);

	} else {
		pthread_mutex_unlock(&mutex_flags);
	}

  pthread_mutex_lock(&mutex_session);
	bool stage_0_finished = ((&q4s_session)->latency_th <= 0 ||
	  (num_latency_measures_client >= NUMSAMPLESTOSUCCEED + 1 && num_latency_measures_server >= NUMSAMPLESTOSUCCEED))
		&& ((&q4s_session)->jitter_th[0] <= 0 || num_jitter_measures_server >= NUMSAMPLESTOSUCCEED)
		&& ((&q4s_session)->jitter_th[1] <= 0 || num_jitter_measures_client >= NUMSAMPLESTOSUCCEED + 1)
		&& ((&q4s_session)->packetloss_th[0] <= 0 || num_packetloss_measures_server >= NUMSAMPLESTOSUCCEED)
		&& ((&q4s_session)->packetloss_th[1] <= 0 || num_packetloss_measures_client >= NUMSAMPLESTOSUCCEED + 1);

	bool no_stage_0 = (&q4s_session)->latency_th <= 0 && (&q4s_session)->jitter_th[0] <= 0
	  && (&q4s_session)->jitter_th[1] <= 0;
	pthread_mutex_unlock(&mutex_session);

  pthread_mutex_lock(&mutex_flags);
	if (stage_0_finished && !no_stage_0 && !(flags & FLAG_FINISH_PING)) {
		pthread_cancel(timer_ping);
		pthread_cancel(receive_UDP_thread);
		pthread_mutex_lock(&mutex_print);
		printf("\nStage 0 has finished succesfully\n");
		pthread_mutex_unlock(&mutex_print);
		flags |= FLAG_FINISH_PING;
		pthread_mutex_unlock(&mutex_flags);
	} else {
		pthread_mutex_unlock(&mutex_flags);
	}

	bool stage_1_finished = ((&q4s_session)->bw_th[0] <= 0 ||
	  (&q4s_session)->bw_measure_server >= (&q4s_session)->bw_th[0])
		&& ((&q4s_session)->bw_th[1] <= 0 || (&q4s_session)->bw_measure_client >= (&q4s_session)->bw_th[1])
		&& ((&q4s_session)->packetloss_th[0] <= 0 || num_packetloss_measures_server > NUMSAMPLESTOSUCCEED)
		&& ((&q4s_session)->packetloss_th[1] <= 0 || num_packetloss_measures_client > NUMSAMPLESTOSUCCEED + 1);

	bool no_stage_1 = (&q4s_session)->bw_th[0] <= 0 && (&q4s_session)->bw_th[1] <= 0;
	if (stage_1_finished && !no_stage_1 && !(flags & FLAG_FINISH_BWIDTH)) {
		pthread_cancel(timer_bwidth);
		pthread_cancel(receive_UDP_thread);
		pthread_mutex_lock(&mutex_print);
		printf("\nStage 1 has finished succesfully\n");
		pthread_mutex_unlock(&mutex_print);
		pthread_mutex_unlock(&mutex_print);
		flags |= FLAG_FINISH_BWIDTH;
		pthread_mutex_unlock(&mutex_flags);
	}
}

// Decides the next Stage to go
void Decide (fsm_t* fsm) {
	pthread_mutex_lock(&mutex_flags);
	if (flags & FLAG_FINISH_PING) {
		// Puts every FLAG to 0
		flags = 0;
		pthread_mutex_unlock(&mutex_flags);

    pthread_mutex_lock(&mutex_session);
		if ((&q4s_session)->bw_th[0] > 0 ||  (&q4s_session)->bw_th[1] > 0) {
			  pthread_mutex_unlock(&mutex_session);

				pthread_mutex_lock(&mutex_print);
				printf("\nGoing to Stage 1 (measure of bandwidth and packetloss)\n");
				pthread_mutex_unlock(&mutex_print);

				// Lock to guarantee mutual exclusion
				pthread_mutex_lock(&mutex_flags);
				flags |= FLAG_GO_TO_1;
				pthread_mutex_unlock(&mutex_flags);
	  } else {
			pthread_mutex_unlock(&mutex_session);

			pthread_mutex_lock(&mutex_print);
		  printf("\nThere are no thresholds established for bandwidth\n");
			printf("\nNegotiation phase has finished\n");
			printf("\nProceeding to send a Q4S CANCEL to the server\n");
			pthread_mutex_unlock(&mutex_print);
			pthread_mutex_lock(&mutex_flags);
			flags |= FLAG_CANCEL;
			pthread_mutex_unlock(&mutex_flags);
	  }
	} else if (flags & FLAG_FINISH_BWIDTH) {
		// Puts every FLAG to 0
		flags = 0;
		pthread_mutex_unlock(&mutex_flags);

		pthread_mutex_lock(&mutex_print);
		printf("\nStage 1 has finished succesfully\n");
		printf("\nNegotiation phase has finished\n");
		printf("\nProceeding to send a Q4S CANCEL to the server\n");
		pthread_mutex_unlock(&mutex_print);
		pthread_mutex_lock(&mutex_flags);
		flags |= FLAG_CANCEL;
		pthread_mutex_unlock(&mutex_flags);
	}

}

// Sends a Q4S BWIDTH message and starts timer for BWIDTH delivery
void Bwidth_Init (fsm_t* fsm) {
	// Lock to guarantee mutual exclusion
	pthread_mutex_lock(&mutex_flags);
  flags &= ~FLAG_RECEIVE_OK;
  pthread_mutex_unlock(&mutex_flags);

	// Stores parameters of message received (if present)
  pthread_mutex_lock(&mutex_session);
	store_parameters(&q4s_session, &(q4s_session.message_received));
	pthread_mutex_unlock(&mutex_session);

	// Throws a thread to check the arrival of Q4S messages using UDP
	pthread_create(&receive_UDP_thread, NULL, (void*)thread_receives_UDP, NULL);

	// Initialize auxiliary variables
	num_packet_lost = 0;
	num_ping = 0;
	num_latency_measures_client = 0;
	num_jitter_measures_client = 0;
	num_packetloss_measures_client = 0;
	num_latency_measures_server = 0;
	num_jitter_measures_server = 0;
	num_packetloss_measures_server = 0;
	num_bwidth_received = 0;

	pthread_mutex_lock(&mutex_session);
	// Initialize session variables
	q4s_session.seq_num_server = -1;
	q4s_session.seq_num_client = 0;
	q4s_session.packetloss_measure_client = -1;
	memset((&q4s_session)->latency_samples, 0, MAXNUMSAMPLES);
	memset((&q4s_session)->elapsed_time_samples, 0, MAXNUMSAMPLES);
	memset((&q4s_session)->packetloss_samples, 0, MAXNUMSAMPLES);
	memset((&q4s_session)->bw_samples, 0, MAXNUMSAMPLES);

	// Initialize bwidth period if necessary
	if (q4s_session.bwidth_clk <= 0) {
		q4s_session.bwidth_clk = 1000;
	}
	pthread_mutex_unlock(&mutex_session);
	// Starts timer for ping delivery
	pthread_create(&timer_bwidth, NULL, (void*)bwidth_timeout, NULL);
}

// Sends a Q4S BWIDTH message
void Bwidth (fsm_t* fsm) {
	// Lock to guarantee mutual exclusion
	pthread_mutex_lock(&mutex_flags);
  flags &= ~FLAG_TEMP_BWIDTH;
  pthread_mutex_unlock(&mutex_flags);

	pthread_mutex_lock(&mutex_session);
	// Fills q4s_session.message_to_send with the Q4S BWIDTH parameters
	create_bwidth(&q4s_session.message_to_send);
	// Converts q4s_session.message_to_send into a message with correct format (prepared_message)
	prepare_message(&(q4s_session.message_to_send), q4s_session.prepared_message);
	// Sends the prepared message
	send_message_UDP(q4s_session.prepared_message);
	(&q4s_session)->seq_num_client++;
	pthread_mutex_unlock(&mutex_session);
}

// Creates and sends a Q4S CANCEL message to the server
void Cancel (fsm_t* fsm) {
  // Lock to guarantee mutual exclusion
	pthread_mutex_lock(&mutex_flags);
  flags &= ~FLAG_CANCEL;
  pthread_mutex_unlock(&mutex_flags);


  pthread_mutex_lock(&mutex_session);
	// Fills q4s_session.message_to_send with the Q4S CANCEL parameters
	create_cancel(&q4s_session.message_to_send);
	// Converts q4s_session.message_to_send into a message with correct format (prepared_message)
	prepare_message(&(q4s_session.message_to_send), q4s_session.prepared_message);
	// Sends the prepared message
	send_message_TCP(q4s_session.prepared_message);
	pthread_mutex_unlock(&mutex_session);
}

// Exits Q4S session
void Exit (fsm_t* fsm) {
	// Lock to guarantee mutual exclusion
	pthread_mutex_lock(&mutex_flags);
	// Puts every FLAG to 0
	flags = 0;
	pthread_mutex_unlock(&mutex_flags);
  // Cancels timers
	pthread_cancel(timer_ping);
	pthread_cancel(timer_bwidth);
  // Cancels the threads receiving Q4S messages
	pthread_cancel(receive_TCP_thread);
	pthread_cancel(receive_UDP_thread);
	// Closes connection with Q4S server
  close(socket_TCP);
	close(socket_UDP);
	pthread_mutex_lock(&mutex_print);
	printf("\nConnection has been closed\n");
	printf("Press 'q' to connect to the Q4S server\n");
	pthread_mutex_unlock(&mutex_print);
}


// FUNCTION EXPLORING THE KEYBOARD

// Thread function for keystrokes detection and interpretation
void *thread_explores_keyboard () {
	int pressed_key;
	while(1) {
		// Pauses program execution for 10 ms
		delay(10);
		// Checks if a key has been pressed
		if(kbhit()) {
			// Stores pressed key
			pressed_key = kbread();
			switch(pressed_key) {
				// If "q" (of "q4s") has been pressed, FLAG_CONNECT is activated
				case 'q':
				  // Lock to guarantee mutual exclusion
					pthread_mutex_lock(&mutex_flags);
					flags |= FLAG_CONNECT;
					pthread_mutex_unlock(&mutex_flags);
					break;
				// If "b" (of "begin") has been pressed, FLAG_BEGIN is activated
				case 'b':
				  // Lock to guarantee mutual exclusion
					pthread_mutex_lock(&mutex_flags);
					flags |= FLAG_BEGIN;
					pthread_mutex_unlock(&mutex_flags);
					break;
				// If "c" (of "cancel") has been pressed, FLAG_CANCEL is activated
        case 'c':
          // Lock to guarantee mutual exclusion
          pthread_mutex_lock(&mutex_flags);
          flags |= FLAG_CANCEL;
          pthread_mutex_unlock(&mutex_flags);
          break;
        // If any other key has been pressed, nothing happens
				default:
					break;
			}
		}
	}
}

// EXECUTION OF MAIN PROGRAM
int main () {
	// System configuration
	system_setup();

	// State machine: list of transitions
	// {OriginState, CheckFunction, DestinationState, ActionFunction}
	fsm_trans_t q4s_table[] = {
		  { WAIT_CONNECT, check_connect, WAIT_START, Setup },
			{ WAIT_START, check_begin,  HANDSHAKE, Begin },
			{ HANDSHAKE, check_receive_ok,  HANDSHAKE, Store },
			{ HANDSHAKE, check_go_to_0,  STAGE_0, Ready0 },
			{ HANDSHAKE, check_go_to_1,  STAGE_1, Ready1 },
			{ HANDSHAKE, check_cancel, TERMINATION, Cancel },
			{ STAGE_0, check_receive_ok, PING_MEASURE_0, Ping_Init },
			{ PING_MEASURE_0, check_temp_ping_0, PING_MEASURE_0, Ping },
			{ PING_MEASURE_0, check_receive_ok, PING_MEASURE_0, Update },
			{ PING_MEASURE_0, check_receive_ping, PING_MEASURE_0, Update },
			{ PING_MEASURE_0, check_finish_ping, WAIT_NEXT, Decide },
			{ WAIT_NEXT, check_go_to_1, STAGE_1, Ready1 },
			{ STAGE_1, check_receive_ok, BWIDTH_MEASURE, Bwidth_Init },
			{ BWIDTH_MEASURE, check_temp_bwidth, BWIDTH_MEASURE, Bwidth },
			{ BWIDTH_MEASURE, check_receive_bwidth, BWIDTH_MEASURE, Update },
			{ BWIDTH_MEASURE, check_finish_bwidth, WAIT_NEXT, Decide },
			{ WAIT_NEXT, check_cancel, TERMINATION, Cancel},
			{ WAIT_NEXT, check_go_to_2, STAGE_2, Ready2 },
			{ STAGE_2, check_receive_ok, PING_MEASURE_2,  Ping_Init },
			{ PING_MEASURE_2, check_temp_ping_2, PING_MEASURE_2, Ping },
			{ PING_MEASURE_2, check_receive_ok, PING_MEASURE_2, Update },
			{ PING_MEASURE_2, check_receive_ping, PING_MEASURE_2, Update },
			{ PING_MEASURE_2, check_cancel, TERMINATION, Cancel},
			{ TERMINATION, check_receive_cancel, WAIT_CONNECT,  Exit },
			{ -1, NULL, -1, NULL }
	};

  // State machine creation
	fsm_t* q4s_fsm = fsm_new (WAIT_CONNECT, q4s_table, NULL);

	// State machine initialitation
	fsm_setup (q4s_fsm);

	pthread_mutex_lock(&mutex_print);
	printf("Press 'q' to connect to the Q4S server\n");
	pthread_mutex_unlock(&mutex_print);

	while (1) {
		// State machine operation
		fsm_fire (q4s_fsm);
		// Waits for CLK_MS milliseconds
		delay (CLK_MS);
	}

	// State machine destruction
	fsm_destroy (q4s_fsm);
	// Threads destruction
	pthread_cancel(receive_TCP_thread);
	pthread_cancel(receive_UDP_thread);
	pthread_cancel(keyboard_thread);
	pthread_cancel(timer_ping);
	pthread_cancel(timer_bwidth);
	return 0;
}
