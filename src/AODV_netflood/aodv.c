/******************************************************************************************/
/******************* NES PROJECT WS 2020/21 -- AODV ************************/
/******************************************************************************************/
//including header file aodv.h with libraries, macros and structure definitions

#include "aodv.h"

// Function declarations
static void unicast_rrep(struct unicast_conn *, const linkaddr_t *);
static void unicast_data(struct unicast_conn *, const linkaddr_t *);
static int netflood_recv(struct netflood_conn *, const linkaddr_t *, const linkaddr_t *, uint8_t, uint8_t);

static void fwdrrep(RREP_PKT* rrep, int nxt);
static void fwddata(DATA_PKT* data, int nxt);
static void fwdrreq(RREQ_PKT* rreq);

static char tblUpdt(RREP_PKT* rrep, int from);
static int nxtNode(int dst);
static void updtDisc(DT_E* rreq_info);
static void clrDisc(RREP_PKT* rrep);
static char dupReq(RREQ_PKT* rreq);
static char pckQ(DATA_PKT* data);

static void printRouteTbl();
static void printDiscTbl();

// Creating structure objects for unicast and broadcast connection
static struct unicast_conn uc_rrepC;
static struct unicast_conn uc_dataC;
static struct netflood_conn netflood;

// Assigning of callback functions
static const struct unicast_callbacks uc_rrepCB = {unicast_rrep};
static const struct unicast_callbacks uc_dataCB = {unicast_data};
static const struct netflood_callbacks nf_rreqCB = {netflood_recv};

// Creating table objects
static RT_E routeTbl[NET_NODES];
static DT_E discTbl[DISC_TAB];
static QT_E waitTbl[WAIT_QUEUE];

// Protothread declarations
PROCESS(est_conn, "Establish connection");
PROCESS(rreq_pck, "RREQ_Packet msgs");
PROCESS(data_pck, "Send Data");
PROCESS(seqno, "Sequence Number");

AUTOSTART_PROCESSES(&est_conn,
                    &rreq_pck, 
                    &data_pck,
                    &seqno);


// Initialisation Process 
PROCESS_THREAD(est_conn, ev, data)
{
    int lp=0;

    // Closing the existing connections
    PROCESS_EXITHANDLER(unicast_close(&uc_rrepC);)
    PROCESS_EXITHANDLER(unicast_close(&uc_dataC);)
    PROCESS_EXITHANDLER(netflood_close(&netflood);)
        
    PROCESS_BEGIN();

    while(lp < NET_NODES)
    {
        routeTbl[lp].dst = lp+1;
        routeTbl[lp].valid = 0;
        routeTbl[lp].hops = INF;
        
        lp++;
    }

    // Opening up the channels for communication process
    unicast_open(&uc_rrepC, RREP_CHAN, &uc_rrepCB);
    unicast_open(&uc_dataC, DATA_CHAN, &uc_dataCB);
    netflood_open(&netflood, CLOCK_SECOND * 2, 128,&nf_rreqCB); 

    printf("## Start of AODV ##\n");

    PROCESS_END();
}

// Data Process Thread
PROCESS_THREAD(data_pck, ev, data)
{
    // Creating timer objects   
    static struct etimer et;
    static int delay;
        
    static int req_ent = 1; 
    static int dst;
    static int nxt;
        
    static DT_E rreq_info;
    static DATA_PKT data_pt;
            
    PROCESS_BEGIN();
	
    // Setting random delay for data forwarding 
    delay = random_rand() % DATA_PCK_DEL;
    etimer_set(&et, (CLOCK_CONF_SECOND) * delay );

    while(1)
    {     
        PROCESS_WAIT_EVENT_UNTIL(ev != sensors_event);

        if(ev == PROCESS_EVENT_TIMER)
        {
	    // Assigning destination with random node value
            dst = 1 + random_rand() % NET_NODES;
			
	    // Checking if the destination is last node 
            if(dst==linkaddr_node_addr.u8[0])
            {
                dst = (dst!=NET_NODES)? dst+1 : 1; 
            }
			
	    // Updating the payload 
	    strcpy(data_pt.payload,"**NES_PROJECT**");
    
         etimer_set(&et, CLOCK_CONF_SECOND * DATA_PCK_DEL);
        } 

        else
        {
            dst = ((DATA_PKT*)data)->dst;
            strcpy(data_pt.payload,((DATA_PKT*)data)->payload);
        }

        data_pt.dst = dst;


        nxt = nxtNode(dst);
       
        if(nxt!=0)
        {	
	    // forwarding the data when there is next node 
            fwddata(&data_pt, nxt);
        }
        else
        {
            // Adding data package to queue
            pckQ(&data_pt);

            //Updating the route request entries 
            rreq_info.req_ent = req_ent;
            rreq_info.src = linkaddr_node_addr.u8[0]; 
            rreq_info.dst = dst;
            rreq_info.fwd = linkaddr_node_addr.u8[0]; 
			
            //Calling the rreq handler PROCESS 
            process_post(&rreq_pck, PROCESS_EVENT_CONTINUE, &rreq_info);

            req_ent = (req_ent<25) ? req_ent+1 : 1;
        }

    }
    PROCESS_END();
}

// Route Request - RREQ Process
PROCESS_THREAD(rreq_pck, ev, data)
{
    static DT_E* rreq_info;
    static RREQ_PKT rreq;

    PROCESS_BEGIN();
        
    while(1)
    {
        PROCESS_WAIT_EVENT_UNTIL(ev != sensors_event);
        
	// Waiting till Sensor event to copy request info to discovery table data
        rreq_info = (DT_E*)data;
		
       
        rreq.req_ent = rreq_info->req_ent;
        rreq.src = rreq_info->src;
        rreq.dst = rreq_info->dst;
        
	//Entry made in discovery table
        updtDisc(rreq_info);    
        
        fwdrreq(&rreq);    //netflood the ROUTE_REQUEST
    }

    PROCESS_END();
}

// Sequence Process Thread 
PROCESS_THREAD(seqno, ev, data)
{
    static struct etimer et;
    static int lp, flag;
    static int dst, nxt;
    
    PROCESS_BEGIN();
    
    while(1)
    {
        etimer_set(&et, CLOCK_CONF_SECOND);
        
        PROCESS_WAIT_EVENT_UNTIL(ev != sensors_event);
       
        flag = 0;
	// Updating the routing table entries when route expires
        for(lp=0; lp<NET_NODES; lp++)
        {
            if(routeTbl[lp].seqno < 120 && routeTbl[lp].valid ==1)
            {
                routeTbl[lp].seqno ++;
          
                if(routeTbl[lp].seqno == 120)
                {
                    routeTbl[lp].valid = 0;
                    routeTbl[lp].nxt= 0;
                    routeTbl[lp].hops = INF;
                    printf("# Old Route for :  %d !\n",lp+1);
                    flag++;
                }
            }
        }
        if (flag != 0)
            printRouteTbl();
		
	// Updating the discovery table entries when route expires
        flag = 0;
        for(lp=0; lp<DISC_TAB; lp++)
        {
            if(discTbl[lp].seqno > 0 && discTbl[lp].valid ==1)
            {
				
				
                if(discTbl[lp].seqno == 0)
                {
                    discTbl[lp].valid = 0;
                    printf("# ROUTE_REQUEST from %d to %d \n\n# | Entry : %d | has expired!\n",
                            discTbl[lp].src, discTbl[lp].dst, discTbl[lp].req_ent);
                    flag++;
                }
                discTbl[lp].seqno --;
                
            }
        }
        if (flag != 0)
            printDiscTbl();
		
	// Checking for pending data in Waiting table to be forwarded
        flag = 0;
        for(lp=0; lp<WAIT_QUEUE; lp++)
        {
            if(waitTbl[lp].valid != 0)
            {
                dst = waitTbl[lp].data_pt.dst;
                nxt = nxtNode(dst);
                if (nxt != 0)
                {
                    fwddata(&waitTbl[lp].data_pt, nxt);
                    flag++;
                }
                else
                {
                    waitTbl[lp].seqno--;
                    if (waitTbl[lp].seqno < 0)
                    {
                        waitTbl[lp].valid = 0;
                        flag++;
                    }
                }
            }
        }
    }
    PROCESS_END();
}

// Netflooding callback of RREQ from source to destination
static int netflood_recv(struct netflood_conn *c, const linkaddr_t * from,
			 const linkaddr_t * originator, uint8_t seqno, uint8_t hops)
{
    static DT_E rreq_info;
    static RREQ_PKT *rreq;
    static RREP_PKT rrep;
    
    rreq = (char *)packetbuf_dataptr();

        // RREQ packet recieved destination
    	printf("# RREQ received from %d \n\n# | Entry : %d | Node : %d | Dest : %d |\n",
                        from->u8[0], (*rreq).req_ent, (*rreq).src,(*rreq).dst);
						
        printf("\n# NetFlood packet received from %u.%u through %u.%u, hops=%u\n",
	     originator->u8[0], originator->u8[1], from->u8[0], from->u8[1],(hops+1) );

        
        if((*rreq).dst == linkaddr_node_addr.u8[0])
        {                               
            //memcpy(&rreq, &rrep, sizeof(rreq));
	        rrep.req_ent= (*rreq).req_ent;
            rrep.src = (*rreq).src;
            rrep.dst = (*rreq).dst;
            rrep.hops = 0;

            //ROUTE_REPLY to the source of RREQ 
            fwdrrep(&rrep, from->u8[0]);
                        
        }
        else if(dupReq(rreq)==0) // RREQ not reached the destination 
            
	    //memcpy(&rreq, &rreq_info, sizeof(rreq));
	    rreq_info.req_ent = (*rreq).req_ent;
            rreq_info.src = (*rreq).src;
            rreq_info.dst = (*rreq).dst;
            rreq_info.fwd = from->u8[0];
			    
            
            process_post(&rreq_pck, PROCESS_EVENT_CONTINUE, &rreq_info);
        }
        else  // RREQ not reached the destination and same RREQ encountered
        {
            printf("# Dropping duplicated RREQ !!\n");
        }
        return;	

	
}

// Unicast Callback of RREP from destination to source
static void unicast_rrep(struct unicast_conn *c, const linkaddr_t *from)
{
    RREP_PKT *rrep;
    int lp;
    rrep = (char *)packetbuf_dataptr();

    	printf("# RREP received from %d \n\n# | Entry : %d | Node : %d | Dest : %d | Dist : %d | \n",
                        from->u8[0], (*rrep).req_ent,(*rrep).src,(*rrep).dst, ((*rrep).hops + 1));

        if(tblUpdt(rrep, from->u8[0]))
        {
            if((*rrep).src != linkaddr_node_addr.u8[0])
            {
                (*rrep).hops = (*rrep).hops + 1;
                for(lp=0; lp<DISC_TAB; lp++){
                    if(discTbl[lp].valid != 0
                        && discTbl[lp].req_ent == (*rrep).req_ent
                        && discTbl[lp].dst == (*rrep).dst){
                            fwdrrep(rrep, discTbl[lp].fwd); 
                    }
                }
            }
            clrDisc(rrep);
        }  
}

// Unicast Callback of data from source to destination
static void unicast_data(struct unicast_conn *c, const linkaddr_t *from)
{
    static DATA_PKT *data;
    
    data = (char *)packetbuf_dataptr();
    
        // Packet reached destination
        if((*data).dst == linkaddr_node_addr.u8[0])
        {
            printf("# DATA RECEIVED: {%s} \n", (*data).payload);
        }   
        else
        {
           process_post(&data_pck, PROCESS_EVENT_CONTINUE, data);
        }
}
// Netflooding the RREQ packet
static void fwdrreq(RREQ_PKT* rreq)
{        
    packetbuf_clear();
    packetbuf_copyfrom(rreq, sizeof(RREQ_PKT));
    netflood_send(&netflood,0);
    	printf("# Flooding RREQ toward %d \n\n# | Entry : %d, Node : %d | Dest : %d | \n",
            rreq->dst, rreq->req_ent, rreq->src, rreq->dst);
}

// Unicast the RREP packet from destination
static void fwdrrep(RREP_PKT* rrep, int nxt)
{          
    static linkaddr_t to_rimeaddr;
    to_rimeaddr.u8[0]=nxt;
    to_rimeaddr.u8[1]=0;
    
    packetbuf_clear();
    packetbuf_copyfrom(rrep, sizeof (RREP_PKT));
     
    unicast_send(&uc_rrepC, &to_rimeaddr);
    
        printf("# Sending RREP toward %d via %d \n\n# | Entry : %d | Node : %d | Dest : %d | Dist : %d | \n", 
            rrep->src, nxt, rrep->req_ent, rrep->src, rrep->dst, (rrep->hops + 1));
}
// Unicast the data from source
static void fwddata(DATA_PKT* data, int nxt)
{        
    static linkaddr_t to_rimeaddr;
    to_rimeaddr.u8[0]=nxt;
    to_rimeaddr.u8[1]=0;
	
    packetbuf_clear();
    packetbuf_copyfrom(data, sizeof(DATA_PKT)); 
	
    unicast_send(&uc_dataC, &to_rimeaddr);
    
	if(data->dst == nxt)
	{
	     printf("# Sending DATA {%s} to %d \n", 
            data->payload, data->dst);
	}	
	else
	{	
            printf("# Sending DATA {%s} to %d via %d \n", 
            data->payload, data->dst, nxt);
        }
}

// Queueing of Data packet to avoid loss 
static char pckQ(DATA_PKT* data)
{
    int lp;
    for(lp=0; lp<QUE_MAX; lp++) 
    {
        if(waitTbl[lp].valid == 0) 
        {
            waitTbl[lp].data_pt = *data;
            waitTbl[lp].seqno = QUE_MAX;
            return TRUE;
        }
    }
    return FALSE;
}

// Returns next node associated with destination
static int nxtNode(int dst)
{
    if (routeTbl[dst-1].valid != 0)
    {
        return routeTbl[dst-1].nxt;    
    }
    return FALSE;
}

// Inserting entry and printing discovery table.
static void updtDisc(DT_E* rreq_info)
{
    int lp;
    for(lp=0; lp<DISC_TAB; lp++)
    {
        if(discTbl[lp].valid == 0)
        {
            discTbl[lp].req_ent = rreq_info->req_ent;
            discTbl[lp].src = rreq_info->src;
            discTbl[lp].dst = rreq_info->dst;
            discTbl[lp].fwd = rreq_info->fwd;
            discTbl[lp].valid = 1;
            discTbl[lp].seqno = ROUTE_DISC;
            break;
        }
    }
    printDiscTbl();
}

// updating discovery table entry
static void clrDisc(RREP_PKT* rrep)
{
    int lp;
    for(lp=0; lp<DISC_TAB; lp++)
    {
        if(discTbl[lp].valid != 0
            && discTbl[lp].req_ent == rrep->req_ent
            && discTbl[lp].dst == rrep->dst)
                discTbl[lp].valid = 0;
                return;
    }
    return;
}

// Duplicate RREQ check in the discovery Table
static char dupReq(RREQ_PKT* rreq)
{        
    int lp;
    for(lp=0; lp<DISC_TAB; lp++)
    {
        if(discTbl[lp].valid != 0
            && discTbl[lp].req_ent == rreq->req_ent
            && discTbl[lp].src == rreq->src
            && discTbl[lp].dst == rreq->dst)
                return TRUE;
    }
    return FALSE;        
}

// Routing table update
static char tblUpdt(RREP_PKT* rrep, int from)
{
    int d = rrep->dst - 1;
	
    if(rrep->hops < routeTbl[d].hops)
    {
        //UPDATES the routing discovery table!
        routeTbl[d].dst = rrep->dst;
        routeTbl[d].hops = rrep->hops;             
        routeTbl[d].nxt = from;
        routeTbl[d].seqno = 1;
        routeTbl[d].valid = 1;
		
        printRouteTbl();
        return TRUE;
    }
    return FALSE;
}

// Display the routing table
static void printRouteTbl()
{
    int lp;
    char flag = 0;

    printf("** Routing Table ** \n");
    for(lp=0; lp<NET_NODES;lp++)
    {
        if(routeTbl[lp].valid!= 0)
        {
            printf("\n# | Next : %d | Dest : %d | Dist : %d | SeqNo : %d | \n",
			routeTbl[lp].nxt, routeTbl[lp].dst,(routeTbl[lp].hops + 1), routeTbl[lp].seqno);
            
            flag ++;
        }
    }
    if(flag==0)
        printf("# Empty\n");
    else
        printf("\n");
}

// Display the discovery table
static void printDiscTbl()
{
    int lp, flag = 0;
 
    printf("** Discovery Table **\n");
    for(lp=0; lp<DISC_TAB;lp++)
    {
        if(discTbl[lp].valid!= 0)
        {
            printf("\n# | Entry : %d | Node : %d | Dest : %d | Fwd : %d |\n",
			discTbl[lp].req_ent, discTbl[lp].src, discTbl[lp].dst, discTbl[lp].fwd);
            
            flag ++;
        }
    }
    if(flag==0)
    {    
    	printf("# Empty \n");
    }
    else
    {
        printf("\n");
    }
}