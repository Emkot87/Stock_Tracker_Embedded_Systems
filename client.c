#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <signal.h>
#include <libwebsockets.h>
#include <pthread.h>

#include "Queue.h"


#define KGRN "\033[0;32;32m"
#define KCYN "\033[0;36m"
#define KRED "\033[0;32;31m"
#define KYEL "\033[1;33m"
#define KBLU "\033[0;32;34m"
#define KCYN_L "\033[1;36m"
#define KBRN "\033[0;33m"
#define RESET "\033[0m"


static const char * const reason_names[] = {
	"LEJPCB_CONSTRUCTED",
	"LEJPCB_DESTRUCTED",
	"LEJPCB_START",
	"LEJPCB_COMPLETE",
	"LEJPCB_FAILED",
	"LEJPCB_PAIR_NAME",
	"LEJPCB_VAL_TRUE",
	"LEJPCB_VAL_FALSE",
	"LEJPCB_VAL_NULL",
	"LEJPCB_VAL_NUM_INT",
	"LEJPCB_VAL_NUM_FLOAT",
	"LEJPCB_VAL_STR_START",
	"LEJPCB_VAL_STR_CHUNK",
	"LEJPCB_VAL_STR_END",
	"LEJPCB_ARRAY_START",
	"LEJPCB_ARRAY_END",
	"LEJPCB_OBJECT_START",
	"LEJPCB_OBJECT_END",
	"LEJPCB_OBJECT_END_PRE",
};

// The JSON paths/labels that we are interested in
static const char *const tok[] = {

    "data[].s",         // symbol
    "data[].p",         // price
    "data[].t",         // timestrap
    "data[].v",         // volume

};

static int destroy_flag = 0;
static int connection_flag = 0;
static int writeable_flag = 0;



struct tradingInfo
{
    float price;
    char symbol[40];
    unsigned long int timestamp;
    float volume;
};

struct candleStick
{
    float initialPrice;
    float finalPrice;
    float minPrice;
    float maxPrice;
    float totalVolume;
    unsigned int transactions;
    float volumePrice;
    float fifteenVolume;
    int fifteenTrans;
    float fifteenMean;
};

struct tradingInfo tradingInfos[10000];

struct candleStick candleSticks[4 * 15];

queue *fifo;

pthread_mutex_t *MutA;

unsigned tradeInfoIndex = 0;

pthread_t con[8];

unsigned int candleSticksIndex = 0;

unsigned int fifteenPassed = 0;

struct timeval current_time;

double timerForTrades;

int queueFull = 0;

double minuteDelay = 0;

static signed char packetReciever(struct lejp_ctx *ctx, char reason);
void initializeCandlestick(struct candleStick *candleStick ,int size, int candleSticksIndexLoc);
void printInfoToFile(int index, double time);
void *calculateCandlesticks();
void *consumer();
void calculateFifteen(int candleIndex, double time);


static void INT_HANDLER(int signo) {
    destroy_flag = 1;
}

static int websocket_write_back(struct lws *wsi_in, char *str, int str_size_in) 
{
    if (str == NULL || wsi_in == NULL)
        return -1;

    int n;
    int len;
    char *out = NULL;

    if (str_size_in < 1) 
        len = strlen(str);
    else
        len = str_size_in;

    out = (char *)malloc(sizeof(char)*(LWS_SEND_BUFFER_PRE_PADDING + len + LWS_SEND_BUFFER_POST_PADDING));
    //* setup the buffer*/
    memcpy (out + LWS_SEND_BUFFER_PRE_PADDING, str, len );
    //* write out*/
    n = lws_write(wsi_in, out + LWS_SEND_BUFFER_PRE_PADDING, len, LWS_WRITE_TEXT);

    printf(KBLU"[websocket_write_back] %s\n"RESET, str);
    //* free the buffer*/
    free(out);

    return n;
}

const char *symbols[] = {"APPL\0", "AMZN\0", "BINANCE:BTCUSDT\0", "IC MARKETS:1\0"};

pthread_mutex_t fileMutex = PTHREAD_MUTEX_INITIALIZER;


struct lejp_ctx ctx;


static int ws_service_callback(
                         struct lws *wsi,
                         enum lws_callback_reasons reason, void *user,
                         void *in, size_t len)
{
    
    char *msg;

    switch (reason) {

        case LWS_CALLBACK_CLIENT_CLOSED:
            printf("Timed out\n");
            destroy_flag = 1;
            lejp_destruct(&ctx);
            break;

        case LWS_CALLBACK_CLIENT_ESTABLISHED:
            printf(KYEL"[Main Service] Connect with server success.\n"RESET);

            connection_flag = 1;

            lws_callback_on_writable(wsi);
	        
            break;

        case LWS_CALLBACK_CLIENT_CONNECTION_ERROR:
            printf(KRED"[Main Service] Connect with server error: %s.\n"RESET, in);
            destroy_flag = 1;
            connection_flag = 0;
            break;

        case LWS_CALLBACK_CLOSED:
            printf(KYEL"[Main Service] LWS_CALLBACK_CLOSED\n"RESET);
            destroy_flag = 1;
            connection_flag = 0;
            lejp_destruct(&ctx);    //when the callback ends destroy destructor
            break;

        case LWS_CALLBACK_CLIENT_RECEIVE:
            msg = (char *)in;

            lejp_construct(&ctx, packetReciever, NULL, tok, LWS_ARRAY_SIZE(tok));   // of parser
            int m = lejp_parse(&ctx, (uint8_t *)msg, strlen(msg));
            if (m < 0 && m != LEJP_CONTINUE)
            {
                lwsl_err("parse failed %d\n", m);
            }
        
            break;

        case LWS_CALLBACK_CLIENT_WRITEABLE :
            printf(KYEL"[Main Service] On writeable is called.\n"RESET);

            
	    char* out = NULL;
	    
	    int sym_num = 2;
	    char symb_arr[4][20] = {"APPL\0", "AMZN\0", "BINANCE:BTCUSDT\0", "IC MARKETS:1\0"};
	    char str[50];
	    for(int i = 0; i < 4; i++)
	    {
		sprintf(str, "{\"type\":\"subscribe\",\"symbol\":\"%s\"}", symb_arr[i]);

		int len = strlen(str);
		
		out = (char *)malloc(sizeof(char)*(LWS_SEND_BUFFER_PRE_PADDING + len + LWS_SEND_BUFFER_POST_PADDING ));
		memcpy(out + LWS_SEND_BUFFER_PRE_PADDING, str, len);

		
		lws_write(wsi, out+LWS_SEND_BUFFER_PRE_PADDING, len, LWS_WRITE_TEXT);
        
	    }
	    free(out);
            writeable_flag = 1;
            break;

        default:
            break;
    }

    return 0;
}


static struct lws_protocols protocols[] =
{
	{
		"example-protocol",
		ws_service_callback,
	},
	{ NULL, NULL, 0, 0 } /* terminator */
};


int main(void)
{
    //* register the signal SIGINT handler */
    struct sigaction act;
    act.sa_handler = INT_HANDLER;
    act.sa_flags = 0;
    sigemptyset(&act.sa_mask);
    sigaction( SIGINT, &act, 0);

    struct stat sb;
    int i =0;

    system("rm -r ./Results");

    mkdir("Results", 0755);

    for(i = 0; i < 4; i++){
        char folderName[30] = "./Results/" ;
        strcat(folderName,symbols[i]);
        mkdir(folderName , 0755);
    }

    MutA = (pthread_mutex_t *) malloc (sizeof (pthread_mutex_t));

    pthread_mutex_init(MutA,NULL);
    fifo = queueInit ();

    initializeCandlestick(candleSticks,4,candleSticksIndex);

    pthread_t PrintToFileThread;
            

    pthread_create(&PrintToFileThread, NULL, calculateCandlesticks, NULL);

    for(int i = 0 ; i < 8 ; i++){
        pthread_create(&con[i], NULL, consumer, NULL);
    } 

    while(1){

    struct lws_context *context = NULL;
    struct lws_context_creation_info info;
    struct lws *wsi = NULL;
    struct lws_protocols protocol;

    memset(&info, 0, sizeof info);

    info.port = CONTEXT_PORT_NO_LISTEN;
    info.protocols = protocols;
    info.gid = -1;
    info.uid = -1;
    info.options = LWS_SERVER_OPTION_DO_SSL_GLOBAL_INIT;

    
    char inputURL[300] = 
	"ws.finnhub.io/?token=ccd00dqad3ibmia7s5i0";
    const char *urlProtocol, *urlTempPath;
	char urlPath[300];
    context = lws_create_context(&info);
    printf(KRED"[Main] context created.\n"RESET);

    if (context == NULL) {
        printf(KRED"[Main] context is NULL.\n"RESET);
        return -1;
    }

    struct lws_client_connect_info clientConnectionInfo;
    memset(&clientConnectionInfo, 0, sizeof(clientConnectionInfo));
    clientConnectionInfo.context = context;

    if (lws_parse_uri(inputURL, &urlProtocol, &clientConnectionInfo.address,
	    &clientConnectionInfo.port, &urlTempPath))
    {
	    printf("Couldn't parse URL\n");
    }

    urlPath[0] = '/';
    strncpy(urlPath + 1, urlTempPath, sizeof(urlPath) - 2);
    urlPath[sizeof(urlPath)-1] = '\0';
    clientConnectionInfo.port = 443;
    clientConnectionInfo.path = urlPath;
    clientConnectionInfo.ssl_connection = LCCSCF_USE_SSL | LCCSCF_ALLOW_SELFSIGNED | LCCSCF_SKIP_SERVER_CERT_HOSTNAME_CHECK;
    
    
    clientConnectionInfo.host = clientConnectionInfo.address;
    clientConnectionInfo.origin = clientConnectionInfo.address;
    clientConnectionInfo.ietf_version_or_minus_one = -1;
    clientConnectionInfo.protocol = protocols[0].name;

    printf("Testing %s\n\n", clientConnectionInfo.address);
    printf("Connecticting to %s://%s:%d%s \n\n", urlProtocol, 
    clientConnectionInfo.address, clientConnectionInfo.port, urlPath);

    wsi = lws_client_connect_via_info(&clientConnectionInfo);
    if (wsi == NULL) {
        printf(KRED"[Main] wsi create error.\n"RESET);
        return -1;
    }

    printf(KGRN"[Main] wsi create success.\n"RESET);


    while(!destroy_flag)
    {
        lws_service(context, 50);
        
    }

    lws_context_destroy(context);

    destroy_flag = 0;

    //return 0;
    }
}
static signed char packetReciever(struct lejp_ctx *ctx, char reason)
{
  
    static int counter = 0;
    
    if (reason & LEJP_FLAG_CB_IS_VALUE && (ctx->path_match > 0))
    {

        switch (counter)
        {

        case 0:
            tradingInfos[tradeInfoIndex].price = atof(ctx->buf);
            counter++;
            break;

        case 1:
            strcpy(tradingInfos[tradeInfoIndex].symbol ,ctx->buf);
            counter++;
            break;

        case 2:
            tradingInfos[tradeInfoIndex].timestamp = strtoul(ctx->buf,NULL,10);
            counter++;
            break;

        case 3:
            tradingInfos[tradeInfoIndex].volume = atof(ctx->buf);

            pthread_mutex_lock(fifo->mut);

            while (fifo->full) {
                queueFull++;
                pthread_cond_wait (fifo->notFull, fifo->mut);
            }
            
        
            gettimeofday (&current_time, NULL);

            timerForTrades = (double)(current_time.tv_usec/1.0e6 + current_time.tv_sec);

            workFunc workpoint;
            workpoint.work = &(printInfoToFile);
            workpoint.arg = tradeInfoIndex;
            workpoint.timer = timerForTrades;


            queueAdd (fifo, workpoint);

            pthread_mutex_unlock(fifo->mut);

            pthread_cond_signal(fifo->notEmpty);

            tradeInfoIndex = (tradeInfoIndex+1)%10000;

            counter = 0;

            break;

        default:
            
            break;
            
        }

    } 
    
    if (reason == LEJPCB_COMPLETE)
    {   
        struct timeval tv;
    
        gettimeofday(&tv, NULL);
        
    }  
    
}

void initializeCandlestick(struct candleStick *candleStick ,int size, int candleSticksIndexLoc)
{
    for(int i = 0 ; i < size ; i++ ){
        candleStick[candleSticksIndexLoc + i*15].initialPrice = 0;
        candleStick[candleSticksIndexLoc + i*15].finalPrice = 0;
        candleStick[candleSticksIndexLoc + i*15].maxPrice = __FLT_MIN__;
        candleStick[candleSticksIndexLoc + i*15].minPrice = __FLT_MAX__;
        candleStick[candleSticksIndexLoc + i*15].totalVolume = 0;
        candleStick[candleSticksIndexLoc + i*15].transactions = 0;
        candleStick[candleSticksIndexLoc + i*15].volumePrice = 0;
        candleStick[candleSticksIndexLoc + i*15].fifteenVolume = 0;
        candleStick[candleSticksIndexLoc + i*15].fifteenMean = 0;
    }
}


void printInfoToFile(int index, double time)
{    
    struct tradingInfo data = tradingInfos[index];
    char fileTrailing[20] = "_trading_info.txt";
    char fileName[90] = "./Results/";
    strcat(fileName,data.symbol);
    strcat(fileName, "/");              
    strcat(fileName,data.symbol);
    strcat(fileName,fileTrailing);
    FILE *fptr = fopen(fileName,"a");
    fprintf(fptr,"Symbol Price: %f \t Symbol: %s \t Timestamp: %lu \t Volume: %f TradingIndex: %d\n",data.price, data.symbol, data.timestamp, data.volume, index);
    fclose(fptr);

    struct timeval printTime;
    gettimeofday(&printTime, NULL);

    pthread_mutex_lock(MutA);

    minuteDelay += (double)(printTime.tv_usec/1.0e6 + printTime.tv_sec) - time ;

    pthread_mutex_unlock (MutA);

}

void *calculateCandlesticks(){

    struct tradingInfo data;
    static int backIndex = 0;
    struct timeval cur_time;
    double timer;
    double timerAfter;
    int totalTransactions = 0;

    while(1){

        while((int)time(NULL)%60){
            sleep(1);
        }
        
        gettimeofday(&cur_time, NULL);
        timer = (double)(cur_time.tv_usec/1.0e6 + cur_time.tv_sec);

        int index = tradeInfoIndex;
        
        while(backIndex != index){
            data = tradingInfos[backIndex];
            for(int j = 0 ; j < 4 ; j++){
                if(strcmp(data.symbol, symbols[j]) == 0){
                    if(candleSticks[candleSticksIndex + j*15].initialPrice == 0){
                        candleSticks[candleSticksIndex + j*15].initialPrice = data.price;
                    }

                    if(data.price > candleSticks[candleSticksIndex + j*15].maxPrice){
                        candleSticks[candleSticksIndex + j*15].maxPrice = data.price;
                    }

                    if(data.price < candleSticks[candleSticksIndex + j*15].minPrice){
                        candleSticks[candleSticksIndex + j*15].minPrice = data.price;
                    }

                    candleSticks[candleSticksIndex + j*15].totalVolume += data.volume;
                    candleSticks[candleSticksIndex + j*15].transactions++;
                    candleSticks[candleSticksIndex + j*15].volumePrice += data.volume * data.price;
                    candleSticks[candleSticksIndex + j*15].finalPrice =  data.price;
                    totalTransactions++;

                    backIndex = (backIndex + 1)%10000;
                    
                }
            }
        }

        pthread_mutex_lock(fifo->mut);

        while (fifo->full) {
            queueFull++;
            pthread_cond_wait (fifo->notFull, fifo->mut);
        }

        workFunc workpoint;
        workpoint.work = &(calculateFifteen);
        workpoint.arg = candleSticksIndex;
        workpoint.timer = timer;
        

        queueAdd (fifo, workpoint);

        pthread_mutex_unlock(fifo->mut);

        pthread_cond_signal(fifo->notEmpty);

        for(int j = 0; j < 4; j++){
            char fileTrailing[30] = "_candleSticks_info.txt";
            char fileName[90] = "./Results/";
            strcat(fileName,symbols[j]);
            strcat(fileName, "/");              
            strcat(fileName,symbols[j]);
            strcat(fileName,fileTrailing);
            FILE *fptr = fopen(fileName,"a");
            fprintf(fptr,"Initial Price: %f \t Final Price: %f \t Max Price: %f \t Min Price: %f \t Total Volume: %f\n", candleSticks[candleSticksIndex + j*15].initialPrice, candleSticks[candleSticksIndex + j*15].finalPrice,
            candleSticks[candleSticksIndex + j*15].maxPrice, candleSticks[candleSticksIndex + j*15].minPrice, candleSticks[candleSticksIndex + j*15].totalVolume);
            fclose(fptr);   
        }

        gettimeofday(&cur_time, NULL);

        FILE *fptr = fopen("./Results/candleStickDelay.txt","a");
        fprintf(fptr,"%lf\n",(double)(cur_time.tv_usec/1.0e6 + cur_time.tv_sec) - timer);
        fclose(fptr);

        fptr = fopen("./Results/tradeDelay.txt","a");
        fprintf(fptr,"%lf,%d\n",minuteDelay/totalTransactions, totalTransactions);
        fclose(fptr);

        pthread_mutex_lock(MutA);

        minuteDelay = 0 ;

        pthread_mutex_unlock (MutA);

        totalTransactions = 0;

        
        if(candleSticksIndex == 14)
            fifteenPassed = 1;

        candleSticksIndex = (candleSticksIndex+1)%15;
        initializeCandlestick(candleSticks, 4, candleSticksIndex);

        printf(" queueFulls until now: %d\n",queueFull);
        sleep(50);
    }

}

void calculateFifteen(int candleIndex, double time){

    for(int symbol = 0 ; symbol < 4 ; symbol++)
    {
        float fifVolume = 0;
        float mean = 0;
        float trans = 0;

        if(fifteenPassed){
            for(int i = 0 ; i < 15 ; i++)
            {
                fifVolume += candleSticks[symbol*15 + i].totalVolume;
                mean += candleSticks[symbol*15 + i].volumePrice / candleSticks[symbol*15 + i].transactions;
            }
            candleSticks[symbol*15 + candleIndex].fifteenVolume = fifVolume;
            candleSticks[symbol*15 + candleIndex].fifteenMean = mean/15;
        }

        else{
            for(int i = 0 ; i < candleIndex + 1 ; i++)
            {
                fifVolume += candleSticks[symbol*15 + i].totalVolume;
                mean += candleSticks[symbol*15 + i].volumePrice / candleSticks[symbol*15 + i].transactions;;
            }
            candleSticks[symbol*15 + candleIndex].fifteenVolume = fifVolume;
            candleSticks[symbol*15 + candleIndex].fifteenMean = mean/15;
        }
    }

    for(int j = 0 ; j < 4 ; j++){
        char fileTrailing2[30] = "_15minuteAverage.txt";
        char fileName2[90] = "./Results/";
        strcat(fileName2,symbols[j]);
        strcat(fileName2, "/");              
        strcat(fileName2,symbols[j]);
        strcat(fileName2,fileTrailing2);
        FILE *fptr = fopen(fileName2,"a");
        fprintf(fptr,"Average trade cost : %f, Total volume %f, candlleSticksIndex : %d\n", candleSticks[j*15 + candleIndex].fifteenMean,  candleSticks[j*15 + candleIndex].fifteenVolume,candleIndex);
        fclose(fptr);  
    }

    struct timeval printTime;
    gettimeofday(&printTime, NULL);

    FILE *fptr = fopen("./Results/fifteenAverage.txt","a");
    fprintf(fptr,"%lf\n",(double)(printTime.tv_usec/1.0e6 + printTime.tv_sec) - time);
    fclose(fptr);

}


void *consumer ()
{
  workFunc workpoint;
	
  while(1){
    pthread_mutex_lock (fifo->mut);
    while (fifo->empty) {
      pthread_cond_wait (fifo->notEmpty, fifo->mut);
    }
  

  queueDel (fifo, &workpoint);
  workpoint.work(workpoint.arg,workpoint.timer);
	
  pthread_mutex_unlock (fifo->mut);
  pthread_cond_signal (fifo->notFull);

  }
  
  return (NULL);
}
