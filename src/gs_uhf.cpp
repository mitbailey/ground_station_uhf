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

// SegFault
// is gs_uhf_rx_thread

void *gs_uhf_rx_thread(void *args)
{
    // TODO: As of right now, there is no way to detect if the UHF Radio crashes, which may require a re-init. In this event, global_data->uhf_ready should be set to false and the radio should be re-init'd. However, this feature doesn't exist in the middleware.
    // TODO: Possibly just assume any uhf_read failure is because of a crash UHF?

    global_data_t *global_data = (global_data_t *)args;

    while (global_data->network_data->thread_status > 0)
    {
        // Init UHF.
        if (!global_data->uhf_ready)
        {
            global_data->uhf_initd = gs_uhf_init();
            if (global_data->uhf_initd != 1)
            {
                dbprintlf(RED_FG "UHF Radio initialization failure (%d).", global_data->uhf_initd);
                usleep(5 SEC);
                continue;
            }

            // TODO: COMMENT OUT FOR DEBUGGING PURPOSES ONLY
#ifndef UHF_NOT_CONNECTED_DEBUG
            global_data->uhf_ready = true;
#endif
        }

        char buffer[GST_MAX_PACKET_SIZE];
        memset(buffer, 0x0, sizeof(buffer));

        // Enable pipe mode.
        // gs_uhf_enable_pipe();

        // TODO: COMMENT OUT FOR DEBUGGING PURPOSES ONLY
#ifndef UHF_NOT_CONNECTED_DEBUG
        si446x_en_pipe();
#endif

        int retval = gs_uhf_read(buffer, sizeof(buffer), UHF_RSSI, &global_data->uhf_ready);

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
            dbprintlf("Received from UHF.");
        }

        gs_network_transmit(global_data->network_data, CS_TYPE_DATA, CS_ENDPOINT_CLIENT, buffer, sizeof(buffer));
    }

    if (global_data->network_data->thread_status > 0)
    {
        global_data->network_data->thread_status = 0;
    }
    return nullptr;
}

void *gs_network_rx_thread(void *args)
{
    global_data_t *global_data = (global_data_t *)args;
    network_data_t *network_data = global_data->network_data;

    // Similar, if not identical, to the network functionality in ground_station.
    // Roof UHF is a network client to the GS Server, and so should be very similar in socketry to ground_station.

    while (network_data->rx_active)
    {
        if (!network_data->connection_ready)
        {
            usleep(5 SEC);
            continue;
        }

        int read_size = 0;

        while (read_size >= 0 && network_data->rx_active)
        {
            char buffer[sizeof(NetworkFrame) * 2];
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
                NetworkFrame *network_frame = (NetworkFrame *)buffer;

                // Check if we've received data in the form of a NetworkFrame.
                if (network_frame->checkIntegrity() < 0)
                {
                    dbprintlf("Integrity check failed (%d).", network_frame->checkIntegrity());
                    continue;
                }
                dbprintlf("Integrity check successful.");

                global_data->netstat = network_frame->getNetstat();

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

                // Extract the payload into a buffer.
                int payload_size = network_frame->getPayloadSize();
                unsigned char *payload = (unsigned char *)malloc(payload_size);
                if (network_frame->retrievePayload(payload, payload_size) < 0)
                {
                    dbprintlf(RED_FG "Error retrieving data.");
                    continue;
                }

                NETWORK_FRAME_TYPE type = network_frame->getType();
                switch (type)
                {
                case CS_TYPE_ACK:
                {
                    dbprintlf(BLUE_FG "Received an ACK frame!");
                    break;
                }
                case CS_TYPE_NACK:
                {
                    dbprintlf(BLUE_FG "Received a NACK frame!");
                    break;
                }
                case CS_TYPE_CONFIG_UHF:
                {
                    dbprintlf(BLUE_FG "Received an UHF CONFIG frame!");
                    // TODO: Configure yourself.
                    break;
                }
                case CS_TYPE_DATA:
                {
                    dbprintlf(BLUE_FG "Received a DATA frame!");
                    // TODO: Send to SPACE-HAUC.
                    if (global_data->uhf_ready)
                    {
                        // ssize_t retval = uhf_write(global_data->modem, (char *)payload, payload_size);

                        // TODO: Find a better spot for this. Perhaps detecting if its been more than 2 minutes since last TX.
                        // gs_uhf_enable_pipe();

                        si446x_en_pipe();

                        dbprintlf(BLUE_FG "Attempting to transmit %d bytes to SPACE-HAUC.", payload_size);
                        ssize_t retval = gs_uhf_write((char *)payload, payload_size, &global_data->uhf_ready);
                        dbprintlf(BLUE_FG "Transmitted with value: %d (note: this is not the number of bytes sent).", retval);
                    }
                    else
                    {
                        dbprintlf(RED_FG "Cannot send received data, UHF radio is not ready!");
                        cs_ack_t nack[1];
                        nack->ack = 0;
                        nack->code = NACK_NO_UHF;
                        gs_network_transmit(network_data, CS_TYPE_NACK, CS_ENDPOINT_CLIENT, nack, sizeof(nack));
                    }
                    break;
                }
                case CS_TYPE_POLL_XBAND_CONFIG:
                case CS_TYPE_XBAND_COMMAND:
                case CS_TYPE_CONFIG_XBAND:
                case CS_TYPE_NULL:
                case CS_TYPE_ERROR:
                default:
                {
                    break;
                }
                }
                free(payload);
            }
            else
            {
                break;
            }
        }
        if (read_size == 0)
        {
            dbprintlf(RED_BG "Connection forcibly closed by the server.");
            strcpy(network_data->discon_reason, "SERVER-FORCED");
            network_data->connection_ready = false;
            continue;
        }
        else if (errno == EAGAIN)
        {
            dbprintlf(YELLOW_BG "Active connection timed-out (%d).", read_size);
            strcpy(network_data->discon_reason, "TIMED-OUT");
            network_data->connection_ready = false;
            continue;
        }
        erprintlf(errno);
    }

    network_data->rx_active = false;
    dbprintlf(FATAL "DANGER! NETWORK RECEIVE THREAD IS RETURNING!");

    if (global_data->network_data->thread_status > 0)
    {
        global_data->network_data->thread_status = 0;
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
    int cond = (info->chipRev == 0x22) && (info->partBuild == 0x00) && (info->id == 0x8600) && (info->customer == 0x00) && (info->romId == 0x6);
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
        eprintf("Read in %d bytes, not a valid packet", retval);
        return -GST_PACKET_INCOMPLETE;
    }

    if (frame->guid != GST_GUID)
    {
        eprintf("GUID 0x%04x", frame->guid);
        return -GST_GUID_ERROR;
    }
    else if (frame->crc != frame->crc1)
    {
        eprintf("0x%x != 0x%x", frame->crc, frame->crc1);
        return -GST_CRC_MISMATCH;
    }
    else if (frame->crc != internal_crc16(frame->payload, GST_MAX_PAYLOAD_SIZE))
    {
        eprintf("CRC %d", frame->crc);
        return -GST_CRC_ERROR;
    }
    else if (frame->termination != GST_TERMINATION)
    {
        eprintf("TERMINATION 0x%x", frame->termination);
    }

    memcpy(buf, frame->payload, GST_MAX_PAYLOAD_SIZE);

    return retval;
}

ssize_t gs_uhf_write(char *buf, ssize_t buffer_size, bool *gst_done)
{
    if (buffer_size < GST_MAX_PAYLOAD_SIZE)
    {
        eprintf("Payload size incorrect.");
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