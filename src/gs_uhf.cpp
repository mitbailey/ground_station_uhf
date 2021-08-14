/**
 * @file gs_uhf.cpp
 * @author Mit Bailey (mitbailey99@gmail.com)
 * @brief 
 * @version See Git tags for version information.
 * @date 2021.08.03
 * 
 * @copyright Copyright (c) 2021
 * 
 */

#include <stdlib.h>
#include <pthread.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <fcntl.h>
#include <si446x.h>
#include "gs_uhf.hpp"
#include "meb_debug.hpp"

void *gs_uhf_rx_thread(void *args)
{
    // TODO: As of right now, there is no way to detect if the UHF Radio crashes, which may require a re-init. In this event, global_data->uhf_ready should be set to false and the radio should be re-init'd. However, this feature doesn't exist in the middleware.
    // TODO: Possibly just assume any uhf_read failure is because of a crash UHF?
    dbprintlf(BLUE_FG "Entered RX Thread");
    global_data_t *global = (global_data_t *)args;

    while (global->network_data->thread_status > 0)
    {
        si446x_info_t si_info[1];
        si_info->part = 0;

        // Init UHF.
        if (!global->uhf_ready)
        {
            global->uhf_initd = gs_uhf_init();
            dbprintlf(RED_FG "Init status: %d", global->uhf_initd);
            if (global->uhf_initd != 1)
            {
                dbprintlf(RED_FG "UHF Radio initialization failure (%d).", global->uhf_initd);
                usleep(5 SEC);
                continue;
            }

            // TODO: COMMENT OUT FOR DEBUGGING PURPOSES ONLY
#ifndef UHF_NOT_CONNECTED_DEBUG
            global->uhf_ready = true;
#endif
        }
        si446x_getInfo(si_info);
        dbprintlf(BLUE_FG "Read part: 0x%x", si_info->part);
        if ((si_info->part & 0x4460) != 0x4460)
        {
            global->uhf_ready = false;
            usleep(5 SEC);
            dbprintlf(FATAL "Part number mismatch: 0x%x, retrying init", si_info->part);
            continue;
        }

        char buffer[GST_MAX_PACKET_SIZE];
        memset(buffer, 0x0, sizeof(buffer));

        // Enable pipe mode.
        // gs_uhf_enable_pipe();

        // TODO: COMMENT OUT FOR DEBUGGING PURPOSES ONLY
#ifndef UHF_NOT_CONNECTED_DEBUG
        si446x_en_pipe();
#endif

        int retval = gs_uhf_read(buffer, sizeof(buffer), UHF_RSSI, &global->uhf_ready);

        if (retval < 0)
        {
            dbprintlf(RED_FG "UHF read error %d.", retval);
            continue;
        }
        else if (retval == 0)
        {
            // Timed-out.
            continue;
        }
        else
        {
            dbprintlf(BLUE_BG "Received from UHF.");
        }

        dbprintlf(BLUE_FG "UHF receive payload has a cmd_output_t.mod value of: %d", ((cmd_output_t *)buffer)->mod);
        NetFrame *network_frame = new NetFrame((unsigned char *)buffer, sizeof(cmd_output_t), NetType::DATA, NetVertex::CLIENT);
        network_frame->sendFrame(global->network_data);
        delete network_frame;
    }

    dbprintlf(FATAL "gs_uhf_rx_thread exiting!");
    if (global->network_data->thread_status > 0)
    {
        global->network_data->thread_status = 0;
    }
    return nullptr;
}

void *gs_network_rx_thread(void *args)
{
    global_data_t *global = (global_data_t *)args;
    NetDataClient *network_data = global->network_data;

    // Similar, if not identical, to the network functionality in ground_station.
    // Roof UHF is a network client to the GS Server, and so should be very similar in socketry to ground_station.

    while (network_data->recv_active && network_data->thread_status > 0)
    {
        if (!network_data->connection_ready)
        {
            usleep(5 SEC);
            continue;
        }

        int read_size = 0;

        while (read_size >= 0 && network_data->recv_active && network_data->thread_status > 0)
        {
            char buffer[sizeof(NetFrame) * 2];
            memset(buffer, 0x0, sizeof(buffer));

            dbprintlf(BLUE_BG "Waiting to receive...");
            read_size = recv(network_data->socket, buffer, sizeof(buffer), 0);
            dbprintlf("Read %d bytes.", read_size);

            if (read_size > 0)
            {
                dbprintf("RECEIVED (hex): ");
                for (int i = 0; i < read_size; i++)
                {
                    printf("%02x", buffer[i]);
                }
                printf("(END)\n");

                // Parse the data by mapping it to a NetworkFrame.
                NetFrame *network_frame = (NetFrame *)buffer;

                // Check if we've received data in the form of a NetworkFrame.
                if (network_frame->validate() < 0)
                {
                    dbprintlf("Integrity check failed (%d).", network_frame->validate());
                    continue;
                }
                dbprintlf("Integrity check successful.");

                global->netstat = network_frame->getNetstat();

                // For now, just print the Netstat.
                uint8_t netstat = network_frame->getNetstat();
                dbprintlf(BLUE_FG "NETWORK STATUS");
                dbprintf("GUI Client ----- ");
                ((netstat & 0x80) == 0x80) ? printf(GREEN_FG "ONLINE" RESET_ALL "\n") : printf(RED_FG "OFFLINE" RESET_ALL "\n");
                dbprintf("Roof UHF ------- ");
                ((netstat & 0x40) == 0x40) ? printf(GREEN_FG "ONLINE" RESET_ALL "\n") : printf(RED_FG "OFFLINE" RESET_ALL "\n");
                dbprintf("Roof X-Band ---- ");
                ((netstat & 0x20) == 0x20) ? printf(GREEN_FG "ONLINE" RESET_ALL "\n") : printf(RED_FG "OFFLINE" RESET_ALL "\n");
                dbprintf("Haystack ------- ");
                ((netstat & 0x10) == 0x10) ? printf(GREEN_FG "ONLINE" RESET_ALL "\n") : printf(RED_FG "OFFLINE" RESET_ALL "\n");
                dbprintf("Track ---------- ");
                ((netstat & 0x10) == 0x8) ? printf(GREEN_FG "ONLINE" RESET_ALL "\n") : printf(RED_FG "OFFLINE" RESET_ALL "\n");

                // Extract the payload into a buffer.
                int payload_size = network_frame->getPayloadSize();
                unsigned char *payload = (unsigned char *)malloc(payload_size);
                if (payload == nullptr)
                {
                    dbprintlf(FATAL "Memory for payload failed to allocate, packet lost.");
                    continue;
                }

                if (network_frame->retrievePayload(payload, payload_size) < 0)
                {
                    dbprintlf(RED_FG "Error retrieving data.");
                    if (payload != nullptr)
                    {
                        free(payload);
                        payload = nullptr;
                    }
                    continue;
                }

                switch (network_frame->getType())
                {
                case NetType::UHF_CONFIG:
                {
                    dbprintlf(BLUE_FG "Received an UHF CONFIG frame!");
                    // TODO: Configure yourself.
                    break;
                }
                case NetType::DATA:
                {
                    dbprintlf(BLUE_FG "Received a DATA frame!");

                    if (global->uhf_ready)
                    {
                        si446x_info_t si_info[1];
                        si_info->part = 0;
                        si446x_getInfo(si_info);
                        if ((si_info->part & 0x4460) != 0x4460)
                        {
                            dbprintlf(RED_FG "UHF Radio not available");
                            if (payload != nullptr)
                            {
                                free(payload);
                                payload = nullptr;
                            }
                            continue;
                            // TODO: DO we let the client know this failed?
                        }

                        // Activate pipe mode.
                        si446x_en_pipe();

                        dbprintlf(BLUE_FG "Attempting to transmit %d bytes to SPACE-HAUC.", payload_size);
                        ssize_t retval = gs_uhf_write((char *)payload, payload_size, &global->uhf_ready);
                        dbprintlf(BLUE_FG "Transmitted with value: %d (note: this is not the number of bytes sent).", retval);
                    }
                    else
                    {
                        dbprintlf(RED_FG "Cannot send received data, UHF radio is not ready!");
                        cs_ack_t nack[1];
                        nack->ack = 0;
                        nack->code = NACK_NO_UHF;
                        
                        NetFrame *nack_frame = new NetFrame((unsigned char *)nack, sizeof(nack), NetType::NACK, NetVertex::CLIENT);
                        nack_frame->sendFrame(network_data);
                        delete nack_frame;
                    }
                    break;
                }
                case NetType::POLL:
                {
                    dbprintlf(BLUE_FG "Received status poll response.");
                    global->netstat = network_frame->getNetstat();
                }
                default:
                {
                    break;
                }
                }
                if (payload != nullptr)
                {
                    free(payload);
                    payload = nullptr;
                }
            }
            else
            {
                break;
            }
        }
        if (read_size == 0)
        {
            dbprintlf(RED_BG "Connection forcibly closed by the server.");
            strcpy(network_data->disconnect_reason, "SERVER-FORCED");
            network_data->connection_ready = false;
            continue;
        }
        else if (errno == EAGAIN)
        {
            dbprintlf(YELLOW_BG "Active connection timed-out (%d).", read_size);
            strcpy(network_data->disconnect_reason, "TIMED-OUT");
            network_data->connection_ready = false;
            continue;
        }
        erprintlf(errno);
    }

    network_data->recv_active = false;

    dbprintlf(FATAL "DANGER! NETWORK RECEIVE THREAD IS RETURNING!");
    if (global->network_data->thread_status > 0)
    {
        global->network_data->thread_status = 0;
    }
    return nullptr;
}

int gs_uhf_init(void)
{
    // (void) gst_error_str; // suppress unused warning

    // WARNING: This function will call exit() on failure.
    dbprintlf(RED_BG "WARNING: si446x_init() calls exit() on failure!");
    // TODO: COMMENT OUT FOR DEBUGGING PURPOSES ONLY
#ifndef UHF_NOT_CONNECTED_DEBUG
    si446x_init();
#endif

    dbprintlf(GREEN_FG "si446x_init() successful!");
    /*
     * chipRev: 0x22
     * partBuild: 0x0
     * id: 0x8600
     * customer: 0x0
     * romId: 0x6
     * revExternal: 0x6
     * revBranch: 0x0
     * revInternal: 0x2
     * patch: 0x0
     * func: 0x1
     */
    // TODO: COMMENT OUT FOR DEBUGGING PURPOSES ONLY
#ifndef UHF_NOT_CONNECTED_DEBUG
    si446x_info_t info[1];
    memset(info, 0x0, sizeof(si446x_info_t));
    si446x_getInfo(info);
    int cond = (info->part & 0x4460) == 0x4460;
    return cond ? 1 : 0;
#endif
#ifdef UHF_NOT_CONNECTED_DEBUG
    return 1;
#endif
}

ssize_t gs_uhf_read(char *buf, ssize_t buffer_size, int16_t *rssi, bool *gst_done)
{
    if (buffer_size < GST_MAX_PAYLOAD_SIZE)
    {
        eprintf("Payload size incorrect.");
        return GST_ERROR;
    }

    gst_frame_t frame[1];
    memset(frame, 0x0, sizeof(gst_frame_t));

    ssize_t retval = 0;
    while (((retval = si446x_read(frame, sizeof(gst_frame_t), rssi)) <= 0) && (!(*gst_done)))
        ;

    if (retval != sizeof(gst_frame_t))
    {
        dbprintlf(RED_FG "Read in %d bytes, not a valid packet", retval);
        return -GST_PACKET_INCOMPLETE;
    }

    if (frame->guid != GST_GUID)
    {
        dbprintlf(RED_FG "GUID 0x%04x", frame->guid);
        return -GST_GUID_ERROR;
    }
    else if (frame->crc != frame->crc1)
    {
        dbprintlf(RED_FG "0x%x != 0x%x", frame->crc, frame->crc1);
        return -GST_CRC_MISMATCH;
    }
    else if (frame->crc != internal_crc16(frame->payload, GST_MAX_PAYLOAD_SIZE))
    {
        dbprintlf(RED_FG "CRC %d", frame->crc);
        return -GST_CRC_ERROR;
    }
    else if (frame->termination != GST_TERMINATION)
    {
        dbprintlf(RED_FG "TERMINATION 0x%x", frame->termination);
    }

    memcpy(buf, frame->payload, GST_MAX_PAYLOAD_SIZE);

    return retval;
}

ssize_t gs_uhf_write(char *buf, ssize_t buffer_size, bool *gst_done)
{
    if (buffer_size < GST_MAX_PAYLOAD_SIZE)
    {
        dbprintlf(RED_FG "Payload size incorrect.");
        return -1;
    }

    gst_frame_t frame[1];
    memset(frame, 0x0, sizeof(gst_frame_t));

    frame->guid = GST_GUID;
    memcpy(frame->payload, buf, buffer_size);
    frame->crc = internal_crc16(frame->payload, GST_MAX_PAYLOAD_SIZE);
    frame->crc1 = frame->crc;
    frame->termination = GST_TERMINATION;

    ssize_t retval = 0;
    while (retval == 0)
    {
        retval = si446x_write(frame, sizeof(gst_frame_t));
        if (retval == 0)
        {
            dbprintlf(RED_FG "Sent zero bytes.");
        }
    }

    dbprintlf(BLUE_FG "Transmitted with value: %d (note: this is not the number of bytes sent).", retval);

    return retval;
}
