
/*
 *
 * Authors:
 * Andre Naujoks (the socket server stuff)
 * Oliver Hartkopp (the rest)
 *
 * Copyright (c) 2002-2009 Volkswagen Group Electronic Research
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. Neither the name of Volkswagen nor the names of its contributors
 *    may be used to endorse or promote products derived from this software
 *    without specific prior written permission.
 *
 * Alternatively, provided that this notice is retained in full, this
 * software may be distributed under the terms of the GNU General
 * Public License ("GPL") version 2, in which case the provisions of the
 * GPL apply INSTEAD OF those given above.
 *
 * The provided data structures and external interfaces from this code
 * are not restricted to be used by modules with a GPL compatible license.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
 * A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
 * OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
 * SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
 * LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 * OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH
 * DAMAGE.
 *
 * Send feedback to <socketcan-users@lists.berlios.de>
 *
 */

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <signal.h>
#include <errno.h>
#include <pthread.h>
#include <getopt.h>

#include <sys/types.h>
#include <sys/wait.h>
#include <sys/socket.h>
#include <sys/ioctl.h>
#include <sys/uio.h>
#include <net/if.h>
#include <netinet/in.h>

#include <linux/can.h>
#include <linux/can/bcm.h>

#include "libsocketcan/libsocketcan.h"

#define MAXLEN 100
#define PORT 28600
#define BROADCAST_PORT 42000

#define BEACON_LENGTH 2048
#define BEACON_TYPE "SocketCAN"
#define BEACON_DESCRIPTION "socketcand"


void *beacon_loop(void *ptr );
void print_usage(void);
void childdied(int i) {
    wait(NULL);
}

static int verbose_flag=0;
char **interface_names;
int interface_count=0;

int main(int argc, char **argv)
{
    int sl, sa, sc;
    int i, ret;
    int idx = 0;
    struct sockaddr_in  saddr, clientaddr;
    struct sockaddr_can caddr;
    socklen_t caddrlen = sizeof(caddr);
    struct ifreq ifr;
    fd_set readfds;
    socklen_t sin_size = sizeof(clientaddr);
    struct sigaction signalaction;
    sigset_t sigset;
        pthread_t beacon_thread;

    char buf[MAXLEN];
    char rxmsg[50];

    struct {
        struct bcm_msg_head msg_head;
        struct can_frame frame;
    } msg;

    int c;

    /* Parse commandline arguments */
    while (1) {
        /* getopt_long stores the option index here. */
        int option_index = 0;
        static struct option long_options[] = {
            {"verbose", no_argument, &verbose_flag, 1},
            {"interfaces",  required_argument, 0, 'i'},
            {0, 0, 0, 0}
        };
    
        c = getopt_long (argc, argv, "vi:", long_options, &option_index);
    
        if (c == -1)
            break;
    
        switch (c) {
            case 0:
            /* If this option set a flag, do nothing else now. */
            if (long_options[option_index].flag != 0)
                break;
            printf ("option %s", long_options[option_index].name);
            if (optarg)
                printf (" with arg %s", optarg);
            printf ("\n");
            break;
    
    
            case 'v':
            puts ("Verbose output activated\n");
            break;
    
    
            case 'i':
            for(i=0;;i++) {
                if(optarg[i] == '\0')
                    break;
                if(optarg[i] == ',')
                    interface_count++;
            }
            interface_count++;

            interface_names = malloc(sizeof(char *) * interface_count);

            interface_names[0] = strtok(optarg, ",");

            for(i=1;i<interface_count;i++) {
                interface_names[i] = strtok(NULL, ",");
            }
            break; 
            case '?':
            break;
    
            default:
            abort ();
        }
    }

    sigemptyset(&sigset);
    signalaction.sa_handler = &childdied;
    signalaction.sa_mask = sigset;
    signalaction.sa_flags = 0;
    sigaction(SIGCHLD, &signalaction, NULL);  /* signal for dying child */

    if((sl = socket(PF_INET, SOCK_STREAM, 0)) < 0) {
        perror("inetsocket");
        exit(1);
    }

    saddr.sin_family = AF_INET;
    saddr.sin_addr.s_addr = htonl(INADDR_ANY);
    saddr.sin_port = htons(PORT);

        beacon_thread = pthread_create(&beacon_thread, NULL, beacon_loop, (void*) NULL);

    while(bind(sl,(struct sockaddr*)&saddr, sizeof(saddr)) < 0) {
        printf(".");fflush(NULL);
        usleep(100000);
    }

    if (listen(sl,3) != 0) {
        perror("listen");
        exit(1);
    }

    while (1) { 
        sa = accept(sl,(struct sockaddr *)&clientaddr, &sin_size);
        if (sa > 0 ){

            if (fork())
                close(sa);
            else
                break;
        }
        else {
            if (errno != EINTR) {
                /*
                 * If the cause for the error was NOT the
                 * signal from a dying child => give an error
                 */
                perror("accept");
                exit(1);
            }
        }
    }

    /* open BCM socket */

    if ((sc = socket(PF_CAN, SOCK_DGRAM, CAN_BCM)) < 0) {
        perror("bcmsocket");
        return 1;
    }

    memset(&caddr, 0, sizeof(caddr));
    caddr.can_family = PF_CAN;
    /* can_ifindex is set to 0 (any device) => need for sendto() */

    if (connect(sc, (struct sockaddr *)&caddr, sizeof(caddr)) < 0) {
        perror("connect");
        return 1;
    }

    while (1) {

        FD_ZERO(&readfds);
        FD_SET(sc, &readfds);
        FD_SET(sa, &readfds);

        ret = select((sc > sa)?sc+1:sa+1, &readfds, NULL, NULL, NULL);

        if (FD_ISSET(sc, &readfds)) {

            ret = recvfrom(sc, &msg, sizeof(msg), 0,
                       (struct sockaddr*)&caddr, &caddrlen);

            ifr.ifr_ifindex = caddr.can_ifindex;
            ioctl(sc, SIOCGIFNAME, &ifr);

            sprintf(rxmsg, "< %s f %03X %d ", ifr.ifr_name,
                msg.msg_head.can_id, msg.frame.can_dlc);

            for ( i = 0; i < msg.frame.can_dlc; i++)
                sprintf(rxmsg + strlen(rxmsg), "%02X ",
                    msg.frame.data[i]);

            /* delimiter '\0' for Adobe(TM) Flash(TM) XML sockets */
            strcat(rxmsg, ">\0");

            send(sa, rxmsg, strlen(rxmsg) + 1, 0);
        }


        if (FD_ISSET(sa, &readfds)) {

            char cmd;
                        char bus_name[6];
            int items;

            if (read(sa, buf+idx, 1) < 1)
                exit(1);

            if (!idx) {
                if (buf[0] == '<')
                    idx = 1;

                continue;
            }

            if (idx > MAXLEN-2) {
                idx = 0;
                continue;
            }

            if (buf[idx] != '>') {
                idx++;
                continue;
            }

            buf[idx+1] = 0;
            idx = 0;

            if(verbose_flag)
                printf("Received '%s'\n", buf);

            /* Extract busname and command */
            sscanf(buf, "< %6s %c ", bus_name, &cmd);

            /* prepare bcm message settings */
            memset(&msg, 0, sizeof(msg));
            msg.msg_head.nframes = 1;

            switch (cmd) {
            case 'S': /* Send a single frame */
                items = sscanf(buf, "< %6s %c %lu %lu %x %hhu "
                    "%hhx %hhx %hhx %hhx %hhx %hhx "
                    "%hhx %hhx >",
                    ifr.ifr_name,
                    &cmd, 
                    &msg.msg_head.ival2.tv_sec,
                    &msg.msg_head.ival2.tv_usec,
                    &msg.msg_head.can_id,
                    &msg.frame.can_dlc,
                    &msg.frame.data[0],
                    &msg.frame.data[1],
                    &msg.frame.data[2],
                    &msg.frame.data[3],
                    &msg.frame.data[4],
                    &msg.frame.data[5],
                    &msg.frame.data[6],
                    &msg.frame.data[7]);

                if (items < 6)
                    break;
                if (msg.frame.can_dlc > 8)
                    break;
                if (items != 6 + msg.frame.can_dlc)
                    break;

                msg.msg_head.opcode = TX_SEND;
                msg.frame.can_id = msg.msg_head.can_id;

                if (!ioctl(sc, SIOCGIFINDEX, &ifr)) {
                    caddr.can_ifindex = ifr.ifr_ifindex;
                    sendto(sc, &msg, sizeof(msg), 0,
                           (struct sockaddr*)&caddr, sizeof(caddr));
                }

                break;
            case 'A': /* Add a send job */
                items = sscanf(buf, "< %6s %c %lu %lu %x %hhu "
                    "%hhx %hhx %hhx %hhx %hhx %hhx "
                    "%hhx %hhx >",
                    ifr.ifr_name,
                    &cmd, 
                    &msg.msg_head.ival2.tv_sec,
                    &msg.msg_head.ival2.tv_usec,
                    &msg.msg_head.can_id,
                    &msg.frame.can_dlc,
                    &msg.frame.data[0],
                    &msg.frame.data[1],
                    &msg.frame.data[2],
                    &msg.frame.data[3],
                    &msg.frame.data[4],
                    &msg.frame.data[5],
                    &msg.frame.data[6],
                    &msg.frame.data[7]);

                if (items < 6)
                    break;
                if (msg.frame.can_dlc > 8)
                    break;
                if (items != 6 + msg.frame.can_dlc)
                    break;

                msg.msg_head.opcode = TX_SETUP;
                msg.msg_head.flags |= SETTIMER | STARTTIMER;
                msg.frame.can_id = msg.msg_head.can_id;

                if (!ioctl(sc, SIOCGIFINDEX, &ifr)) {
                    caddr.can_ifindex = ifr.ifr_ifindex;
                    sendto(sc, &msg, sizeof(msg), 0,
                           (struct sockaddr*)&caddr, sizeof(caddr));
                }

                break;
            case 'U': /* Update send job */
                items = sscanf(buf, "< %6s %c %lu %lu %x %hhu "
                    "%hhx %hhx %hhx %hhx %hhx %hhx "
                    "%hhx %hhx >",
                    ifr.ifr_name,
                    &cmd, 
                    &msg.msg_head.ival2.tv_sec,
                    &msg.msg_head.ival2.tv_usec,
                    &msg.msg_head.can_id,
                    &msg.frame.can_dlc,
                    &msg.frame.data[0],
                    &msg.frame.data[1],
                    &msg.frame.data[2],
                    &msg.frame.data[3],
                    &msg.frame.data[4],
                    &msg.frame.data[5],
                    &msg.frame.data[6],
                    &msg.frame.data[7]);

                if (items < 6)
                    break;
                if (msg.frame.can_dlc > 8)
                    break;
                if (items != 6 + msg.frame.can_dlc)
                    break;

                msg.msg_head.opcode = TX_SETUP;
                msg.msg_head.flags  = 0;
                msg.frame.can_id = msg.msg_head.can_id;

                if (!ioctl(sc, SIOCGIFINDEX, &ifr)) {
                    caddr.can_ifindex = ifr.ifr_ifindex;
                    sendto(sc, &msg, sizeof(msg), 0,
                           (struct sockaddr*)&caddr, sizeof(caddr));
                }

                break;
            case 'D': /* Delete a send job */
                items = sscanf(buf, "< %6s %c %lu %lu %x %hhu "
                    "%hhx %hhx %hhx %hhx %hhx %hhx "
                    "%hhx %hhx >",
                    ifr.ifr_name,
                    &cmd, 
                    &msg.msg_head.ival2.tv_sec,
                    &msg.msg_head.ival2.tv_usec,
                    &msg.msg_head.can_id,
                    &msg.frame.can_dlc,
                    &msg.frame.data[0],
                    &msg.frame.data[1],
                    &msg.frame.data[2],
                    &msg.frame.data[3],
                    &msg.frame.data[4],
                    &msg.frame.data[5],
                    &msg.frame.data[6],
                    &msg.frame.data[7]);

                if (items < 6)
                    break;
                if (msg.frame.can_dlc > 8)
                    break;
                if (items != 6 + msg.frame.can_dlc)
                    break;

                msg.msg_head.opcode = TX_DELETE;
                msg.frame.can_id = msg.msg_head.can_id;

                if (!ioctl(sc, SIOCGIFINDEX, &ifr)) {
                    caddr.can_ifindex = ifr.ifr_ifindex;
                    sendto(sc, &msg, sizeof(msg), 0,
                           (struct sockaddr*)&caddr, sizeof(caddr));
                }

                break;

            case 'R': /* Receive CAN ID with content matching */
                items = sscanf(buf, "< %6s %c %lu %lu %x %hhu "
                    "%hhx %hhx %hhx %hhx %hhx %hhx "
                    "%hhx %hhx >",
                    ifr.ifr_name,
                    &cmd, 
                    &msg.msg_head.ival2.tv_sec,
                    &msg.msg_head.ival2.tv_usec,
                    &msg.msg_head.can_id,
                    &msg.frame.can_dlc,
                    &msg.frame.data[0],
                    &msg.frame.data[1],
                    &msg.frame.data[2],
                    &msg.frame.data[3],
                    &msg.frame.data[4],
                    &msg.frame.data[5],
                    &msg.frame.data[6],
                    &msg.frame.data[7]);

                if (items < 6)
                    break;
                if (msg.frame.can_dlc > 8)
                    break;
                if (items != 6 + msg.frame.can_dlc)
                    break;

                msg.msg_head.opcode = RX_SETUP;
                msg.msg_head.flags  = SETTIMER;
                msg.frame.can_id = msg.msg_head.can_id;

                if (!ioctl(sc, SIOCGIFINDEX, &ifr)) {
                    caddr.can_ifindex = ifr.ifr_ifindex;
                    sendto(sc, &msg, sizeof(msg), 0,
                           (struct sockaddr*)&caddr, sizeof(caddr));
                }

                break;
            case 'F': /* Add a filter */
                items = sscanf(buf, "< %6s %c %lu %lu %x %hhu "
                    "%hhx %hhx %hhx %hhx %hhx %hhx "
                    "%hhx %hhx >",
                    ifr.ifr_name,
                    &cmd, 
                    &msg.msg_head.ival2.tv_sec,
                    &msg.msg_head.ival2.tv_usec,
                    &msg.msg_head.can_id,
                    &msg.frame.can_dlc,
                    &msg.frame.data[0],
                    &msg.frame.data[1],
                    &msg.frame.data[2],
                    &msg.frame.data[3],
                    &msg.frame.data[4],
                    &msg.frame.data[5],
                    &msg.frame.data[6],
                    &msg.frame.data[7]);

                if (items < 6)
                    break;
                if (msg.frame.can_dlc > 8)
                    break;
                if (items != 6 + msg.frame.can_dlc)
                    break;

                msg.msg_head.opcode = RX_SETUP;
                msg.msg_head.flags  = RX_FILTER_ID | SETTIMER;
                msg.frame.can_id = msg.msg_head.can_id;

                if (!ioctl(sc, SIOCGIFINDEX, &ifr)) {
                    caddr.can_ifindex = ifr.ifr_ifindex;
                    sendto(sc, &msg, sizeof(msg), 0,
                           (struct sockaddr*)&caddr, sizeof(caddr));
                }

                break;
            case 'X': /* Delete filter */
                items = sscanf(buf, "< %6s %c %lu %lu %x %hhu "
                    "%hhx %hhx %hhx %hhx %hhx %hhx "
                    "%hhx %hhx >",
                    ifr.ifr_name,
                    &cmd, 
                    &msg.msg_head.ival2.tv_sec,
                    &msg.msg_head.ival2.tv_usec,
                    &msg.msg_head.can_id,
                    &msg.frame.can_dlc,
                    &msg.frame.data[0],
                    &msg.frame.data[1],
                    &msg.frame.data[2],
                    &msg.frame.data[3],
                    &msg.frame.data[4],
                    &msg.frame.data[5],
                    &msg.frame.data[6],
                    &msg.frame.data[7]);

                if (items < 6)
                    break;
                if (msg.frame.can_dlc > 8)
                    break;
                if (items != 6 + msg.frame.can_dlc)
                    break;

                msg.msg_head.opcode = RX_DELETE;
                msg.frame.can_id = msg.msg_head.can_id;

                if (!ioctl(sc, SIOCGIFINDEX, &ifr)) {
                    caddr.can_ifindex = ifr.ifr_ifindex;
                    sendto(sc, &msg, sizeof(msg), 0,
                           (struct sockaddr*)&caddr, sizeof(caddr));
                }

                break;
            default:
                printf("unknown command '%c'.\n", cmd);
                exit(1);
            }
        }
    }

    close(sc);
    close(sa);

    return 0;
}

void *beacon_loop(void *ptr) {
    int i, n, chars_left;
    int udp_socket;
    struct sockaddr_in s;
    size_t len;
    int optval;
    char buffer[BEACON_LENGTH];
    char hostname[32];

    if ((udp_socket = socket(PF_INET, SOCK_DGRAM, IPPROTO_UDP)) < 0) {
        printf("Failed to create socket");
        return NULL;
    }
    
    /* Construct the server sockaddr_in structure */
    memset(&s, 0, sizeof(s));
    s.sin_family = AF_INET;
    s.sin_addr.s_addr = htonl(INADDR_BROADCAST);
    s.sin_port = htons(BROADCAST_PORT);

    /* Activate broadcast option */
    optval = 1;
    setsockopt(udp_socket, SOL_SOCKET, SO_BROADCAST, &optval, sizeof(int));

    /* Bind the socket */
    len = sizeof(s);
    if (bind(udp_socket, (struct sockaddr *) &s, len) < 0) {
        return NULL;    
    }

    while(1) {
        /* Build the beacon */
        gethostname((char *) &hostname, (size_t)  32);
        snprintf(buffer, BEACON_LENGTH, "<CANBeacon name=\"%s\" type=\"%s\" description=\"%s\">\n<URL>can://0.0.0.0:%d</URL>", 
                hostname, BEACON_TYPE, BEACON_DESCRIPTION, PORT);

        for(i=0;i<interface_count;i++) {
            /* Find \0 in beacon buffer */
            for(n=0;;n++) {
                if(buffer[n] == '\0')
                    break;
            }
            chars_left = BEACON_LENGTH - n;

            snprintf(buffer+(n*sizeof(char)), chars_left, "<Bus name=\"%s\"/>", interface_names[i]);
        }
        
        /* Find \0 in beacon buffer */
        for(n=0;;n++) {
            if(buffer[n] == '\0')
                break;
        }
        chars_left = BEACON_LENGTH - n;

        snprintf(buffer+(n*sizeof(char)), chars_left, "</CANBeacon>");

        sendto(udp_socket, buffer, strlen(buffer), 0, (struct sockaddr *) &s, sizeof(struct sockaddr_in));
        sleep(3);
    }

    return NULL;
}

void print_usage(void) {
}