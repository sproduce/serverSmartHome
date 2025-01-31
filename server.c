#include <stdio.h>
#include <unistd.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <signal.h>
#include <mosquitto.h>

#include <pthread.h>

#include <linux/can.h>
#include <linux/can/raw.h>

#include <net/if.h>
#include <arpa/inet.h>

#include <sys/ioctl.h>
#include <sys/socket.h>
#include <sys/types.h>

#include "./ini.h"


#define bitSet(value, bit) ((value) |= (1UL << (bit)))
#define bitClear(value, bit) ((value) &= ~(1UL << (bit)))

#define MAX_HEAD 7



typedef struct main_config
{
    struct
    {
        const char* host;
        const char* login;
        const char* passwd;
        int port;
        const char* topicSub;
        const char* topicPub;
        const char* topicSystem;
    } mqttConfig;

    struct
    {
        const char* interfaceCan;
        const char* interfaceLan;
        const char* address;
        int port;//temporary ONLY FOR STARTUP ssh port
    } systemConfig;
};



typedef union channel_status
{
    __u32 channelstatus;
    __u8 byteStatus[4];
};


struct mosquitto *mosq;
struct main_config mainConfig;

//typedef struct ifreq ifrtype;

// head channel status
union channel_status headChannelStatus[7] = {0};
__u32 prevHeadChannelStatus[7] = {0};
__u32 needUpdate[7] = {0};

__u32 lastUpdateHead[7] = {0}; // AND active head if value > 0 (time())

int socketCan;

struct sockaddr_can addr;
struct ifreq ifr;
struct can_frame frame;

pthread_t thread;


char topic[200];
char tmpString[200];

char macAddress[18];
char ipAddress[15];
char dataSend[3];//max value text byte

static int handlerIni(void* user, const char* section, const char* name,
                   const char* value)
{
     struct main_config *pconfig = (struct main_config*)user;

     #define MATCH(s, n) strcmp(section, s) == 0 && strcmp(name, n) == 0


    if (MATCH("mqtt", "port")) {
        pconfig->mqttConfig.port = atoi(value);
    } else if (MATCH("mqtt", "host")) {
        pconfig->mqttConfig.host = strdup(value);
    } else if (MATCH("mqtt", "login")) {
        pconfig->mqttConfig.login = strdup(value);
    } else if (MATCH("mqtt", "passwd")) {
        pconfig->mqttConfig.passwd = strdup(value);
    } else if (MATCH("system", "can")) {
        pconfig->systemConfig.interfaceCan = strdup(value);
    } else if (MATCH("system", "lan")) {
        pconfig->systemConfig.interfaceLan = strdup(value);
    }if (MATCH("system", "port")) {
        pconfig->systemConfig.port = atoi(value);
    } else {
        return 0;  /* unknown section/name, error */
    }
    return 1;
}



void getIpAddr()
{
    int fdIp;
    struct ifreq ifrIp;

    fdIp = socket(AF_INET, SOCK_DGRAM, 0);

    ifrIp.ifr_addr.sa_family = AF_INET;

    strcpy(ifrIp.ifr_name, mainConfig.systemConfig.interfaceLan);

    ioctl(fdIp, SIOCGIFADDR, &ifrIp);

    close(fdIp);
    strcpy(ipAddress, inet_ntoa(((struct sockaddr_in *)&ifrIp.ifr_addr)->sin_addr));

}




// void on_disconnect(struct mosquitto *mosq, void *obj, int rc)
// {
//     printf("Mosquitto disconnect ...... !!!\n");
// }


void sendError(char *message){
    sprintf(tmpString, "%s/system/error", mainConfig.mqttConfig.login);
    mosquitto_publish(mosq, NULL,tmpString, strlen(message), message, 2, false);
}



void sendIpAddress(){
    getIpAddr();
    sprintf(tmpString, "%s/info/connect", mainConfig.mqttConfig.login);
    mosquitto_publish(mosq, NULL, tmpString, strlen(ipAddress), ipAddress, 2, false);
}






void on_connect(struct mosquitto *mosq, void *obj, int rc)
{
    if (rc != MOSQ_ERR_SUCCESS){
        mosquitto_disconnect(mosq);
        //printf("Login Passwd ERROR\n");
        //printf("MQTT DISCONNECT ...... !!!\n");
    } else {
        //printf("Mosquitto connect ...... !!!\n");

        sendIpAddress();
        
        mosquitto_subscribe(mosq, NULL, mainConfig.mqttConfig.topicSub, 1);
        mosquitto_subscribe(mosq, NULL, mainConfig.mqttConfig.topicSystem, 1);
    }

}









// send message from mqtt to can
void sendCanMessage(int canChannel, int payload)
{
    __u8 headNumber, bitNumber, value, headIndex;

    headNumber = (int)(canChannel + 16) / 32;
    headIndex = headNumber - 1;
    bitNumber = canChannel - (headNumber * 2 - 1) * 16;
    
    bitSet(needUpdate[headIndex], bitNumber);
    
    if (canChannel > 15 && canChannel < 240){
        frame.can_id =canChannel;
        frame.can_dlc = 1;
        frame.data[0] = payload;
        write(socketCan, &frame, sizeof(struct can_frame)) != sizeof(struct can_frame);
    }

}



void destroyTunnel(){
    system("killall ssh > /dev/null 2>&1");
}



void createTunnel(){
    destroyTunnel();
    sprintf(tmpString, "ssh -NR %i:localhost:22 rentdocument.ru -p 2323 -l %s -i /home/%s/.ssh/id_rsa > /dev/null 2>&1 &", mainConfig.systemConfig.port, mainConfig.mqttConfig.login, mainConfig.mqttConfig.login);
    system(tmpString);
}




void sendInfo(){

}




void systemCommand(char *commandText) {
    
    if (strcmp(commandText, "open") == 0)    {
        createTunnel();
    }
    if (strcmp(commandText, "close") == 0)    {
        destroyTunnel();
    }
    if (strcmp(commandText, "info") == 0)    {
        sendIpAddress();
    }

}




void on_message(struct mosquitto *mosq, void *obj, const struct mosquitto_message *msg)
{
    int canChannel, payload;
    char *canChannelText;
    char *topicParse[3];

    topicParse[0] = strtok(msg->topic, "/");
    topicParse[1] = strtok(NULL, "/");
    topicParse[2] = strtok(NULL, "/");

    if (strcmp(topicParse[1], "system") == 0){
        systemCommand(topicParse[2]);
    } else {
        
        if(strcmp(topicParse[1], "channel") == 0){
            
            canChannel = (int)strtol(topicParse[2], NULL, 16);
            payload = atoi(msg->payload);
    
            if (canChannel)
            {
                sendCanMessage(canChannel, payload);
            }
        }
    }
    
}



void differentStatus(const __u32 *prevStatus, const __u32 *currentStatus, __u32 *needUpdate)
{
    if (*prevStatus != *currentStatus){
        for(__u8 i = 0; i < 32; i++){
            if ( ((*prevStatus >> i) & 1) != ((*currentStatus >> i) & 1)){
                bitSet(*needUpdate, i);
            }
        }
    }
}




int initCan()
{

    if ((socketCan = socket(PF_CAN, SOCK_RAW, CAN_RAW)) < 0) {
	    perror("Socket");
	    return 1;
	}

	strcpy(ifr.ifr_name, mainConfig.systemConfig.interfaceCan);

    if (ioctl(socketCan, SIOCGIFINDEX, &ifr) < 0) {
        //printf("Interface %s ", mainConfig.systemConfig.interfaceCan);
        //perror("Error: ");
        return 2;
    }
	
	memset(&addr, 0, sizeof(addr));
	addr.can_family = AF_CAN;
	addr.can_ifindex = ifr.ifr_ifindex;

	if (bind(socketCan, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
	    //perror("Bind");
	    return 3;
	}


    return 0;
}



void sendChannelStatus(__u8 headNumber, const __u32 *currentStatus, const __u32 *needUpdate)
{
    __u8 currentValue, channel;
    
        for (int i = 0; i < 32; i++){
            if (((*needUpdate >> i) & 1)){
                channel = 0x10 * (headNumber * 2 -1) + i;
                sprintf(topic,"%s/0x%X", mainConfig.mqttConfig.topicPub, channel);
                //printf("Topic %s\n", topic);
                sprintf(dataSend, "%i", ((*currentStatus >> i) & 1));
                mosquitto_publish(mosq, NULL, topic, 1, dataSend, 2, false);
            }
        }
}




void on_can_message(const struct can_frame *receivedFrame)
{
    __u32 currentTime; 
    __u8 headIndex;
    headIndex = receivedFrame->data[0] - 1;
    currentTime = time(NULL);

    headChannelStatus[headIndex].byteStatus[0] = receivedFrame->data[1];
    headChannelStatus[headIndex].byteStatus[1] = receivedFrame->data[2];
    headChannelStatus[headIndex].byteStatus[2] = receivedFrame->data[3];
    headChannelStatus[headIndex].byteStatus[3] = receivedFrame->data[4];

    if (currentTime - lastUpdateHead[headIndex] > 100){
        needUpdate[headIndex] = ~0u;
    } else {
        differentStatus(&prevHeadChannelStatus[headIndex], &headChannelStatus[headIndex].channelstatus, &needUpdate[headIndex]);
    }

    lastUpdateHead[headIndex] = currentTime;
                //printf("update %i\n",needUpdate[headIndex]);
    sendChannelStatus(headIndex + 1, &headChannelStatus[headIndex].channelstatus, &needUpdate[headIndex]);
    needUpdate[headIndex] = 0;
    prevHeadChannelStatus[headIndex] = headChannelStatus[headIndex].channelstatus;

}




void startFork()
{
    pid_t pid;
    pid = fork();
    if ( pid > 0){
        exit(EXIT_SUCCESS);
    }
    setsid();
    signal(SIGCHLD, SIG_IGN);
    signal(SIGHUP, SIG_IGN);
   
}



void generateTopic()
{
    sprintf(tmpString, "%s/channel/+", mainConfig.mqttConfig.login);
    mainConfig.mqttConfig.topicSub = strdup(tmpString);

    sprintf(tmpString, "%s/system/+", mainConfig.mqttConfig.login);
    mainConfig.mqttConfig.topicSystem = strdup(tmpString);

    sprintf(tmpString, "%s/channelStatus", mainConfig.mqttConfig.login);
    mainConfig.mqttConfig.topicPub = strdup(tmpString);

}






void setup(void)
{
    generateTopic();
    getIpAddr();
}


int main(int argc, char* argv[])
{
    int rc;
      
    
    if (argc == 1){
        strcpy(tmpString, "./config.ini");
    } else {
        startFork();
        strcpy(tmpString, argv[1]);
    }

    if (ini_parse(tmpString, handlerIni, &mainConfig) < 0) {
        //printf("Can't load 'test.ini'\n");
        return 1;
    }

    setup();

        
    mosquitto_lib_init();
    mosq = mosquitto_new(NULL, true, NULL);

    mosquitto_connect_callback_set(mosq, on_connect);
    //mosquitto_disconnect_callback_set(mosq, on_disconnect);
    mosquitto_message_callback_set(mosq, on_message);
    rc = mosquitto_username_pw_set(mosq, mainConfig.mqttConfig.login, mainConfig.mqttConfig.passwd);
    
    //rc = mosquitto_connect(mosq, mainConfig.mqttConfig.host, mainConfig.mqttConfig.port, 60);

    while (mosquitto_connect(mosq, mainConfig.mqttConfig.host, mainConfig.mqttConfig.port, 60)){
        //printf("try connect mqtt \n");
        sleep(10);
    }



    // if (rc) {
    //     printf("Server  ERROR\n");
    //     return 0;
    // }

    //mosquitto_loop_forever(mosq, -1, 1);
    mosquitto_loop_start(mosq);

     if ( initCan(&ifr)) {
        for(;;){
            sendError("Error CAN");
            sleep(10);
        }
         
     }



    int tmpValue = 0;
    // command for test "cansend can0 0f1#00"
    for(;;){
        read(socketCan, &frame, sizeof(struct can_frame));
        if (frame.can_id == 0x100 && frame.can_dlc == 5){
            if (frame.data[0] && (frame.data[0] <= MAX_HEAD)){
                on_can_message(&frame);
            }
        }
    }


    mosquitto_lib_cleanup();

    return 0;
}

