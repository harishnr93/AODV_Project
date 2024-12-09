/******************************************************************************************/
/******************* NES PROJECT WS 2020/21 -- AODV ***********************/
/******************************************************************************************/

/*-------------------------INCLUDES---------------------------*/
// libraries
#include "contiki.h"
#include "net/rime/rime.h"
#include "dev/leds.h"
#include "dev/button-sensor.h"
#include "string.h"
#include "stdio.h"
#include "stdlib.h"
#include "random.h"

//Macros
#define RREP_CHAN		126
#define DATA_CHAN		127
#define RREQ_CHAN		130  

#define ROUTE_DISC		1   
#define ROUTE_EXP		120   
#define DATA_PCK_DEL		30
#define QUE_MAX			5

#define INF			88						 
#define PKT_LEN			16 

#define  TRUE			1						 
#define  FALSE			0						 

#define NET_NODES		5      
#define WAIT_QUEUE		5    
#define DISC_TAB		25

//#define NEIGHBOR_TIMEOUT 20 * CLOCK_SECOND

// Structure declarations

// data packet
typedef struct dataPacket{
    int dst;
    char payload[PKT_LEN];
}DATA_PKT;

// route request packet
typedef struct routeReqPacket{
    int req_ent;
    int dst;
    int src;
}RREQ_PKT;

// route reply packet
typedef struct routeReplyPacket{
    int req_ent;
    int dst;
    int src;
    int hops;
}RREP_PKT;
/*--------------------TABLES-----------------*/
// routing table 
typedef struct routeTblEntry{
    int dst;
    int nxt;
    int hops;       
    int seqno;        
    int valid;      
}RT_E;

// waiting table 
typedef struct discTblEntry{
    int req_ent;
    int src;
    int dst;
    int fwd;
    int valid;
    int seqno;
}DT_E;

// queue entry data packages to be sent
typedef struct queTblEntry{
    struct dataPacket data_pt;
    int seqno;
    int valid;
}QT_E;


