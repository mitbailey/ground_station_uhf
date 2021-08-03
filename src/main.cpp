/**
 * @file main.cpp
 * @author Mit Bailey (mitbailey99@gmail.com)
 * @brief 
 * @version See Git tags for version information.
 * @date 2021.08.03
 * 
 * @copyright Copyright (c) 2021
 * 
 */

#include <pthread.h>
#include <unistd.h>
#include "txmodem.h"
#include "rxmodem.h"
#include "meb_debug.hpp"
#include "gs_uhf.hpp"

int main(int argc, char **argv)
{
    // Spawn UHF-RX thread.
    // Spawn Network-RX thread.

    // When something is received from the GS Network...
    // - Parse it.
    // - TX it (or not).

    // When something is received on UHF...
    // - Parse it.
    // - Send it to the server (or not).

    // Set up global data.
    global_data_t global_data[1] = {0};
    global_data->network_data = new NetworkData();
    global_data->network_data->rx_active = true;

    // Initialize rxmodem.
    rxmodem rx_modem = {0};
    int rxmodem_id = 0;
    int rxdma_id = 0;
    rxmodem_init(&rx_modem, rxmodem_id, rxdma_id);

    // Initialize txmodem.
    txmodem tx_modem = {0};
    int txmodem_id = 0;
    int txdma_id = 0;
    txmodem_init(&tx_modem, txmodem_id, txdma_id);

    // Arm the modem.
    rxmodem_start(&rx_modem);

    // 1 = All good, 0 = recoverable failure, -1 = fatal failure (close program)
    global_data->thread_status = 1;

    // Start Ground Station Network threads.
    pthread_t rx_thread_id, polling_thread_id;
    pthread_create(&rx_thread_id, NULL, gs_network_rx_thread, global_data);
    pthread_create(&polling_thread_id, NULL, gs_polling_thread, global_data);

    // Start the RX threads, and restart them should it be necessary.
    while (global_data->thread_status > 0)
    {
        // Initialize and begin socket communication to the server.
        if (!global_data->network_data->connection_ready)
        {
            while (gs_connect_to_server(global_data) != 1)
            {
                dbprintlf(RED_FG "Failed to establish connection to server.");
                usleep(2.5 SEC);
            }
        }

        pthread_t uhf_rx_tid, net_rx_tid;
        pthread_create(&uhf_rx_tid, NULL, gs_uhf_rx_thread, &global_data);
        pthread_create(&net_rx_tid, NULL, gs_network_rx_thread, &global_data);

        void *thread_return;
        pthread_join(uhf_rx_tid, &thread_return);
        pthread_join(net_rx_tid, &thread_return);

        if (!global_data->network_data->connection_ready)
        {
            // TODO: Re-establish connection.

            if (!global_data->network_data->connection_ready)
            {
                usleep(2.5 SEC);
            }
        }
    }

    // Finished.
    void *retval;
    pthread_cancel(rx_thread_id);
    pthread_cancel(polling_thread_id);
    pthread_join(rx_thread_id, &retval);
    retval == PTHREAD_CANCELED ? printf("Good rx_thread_id join.\n") : printf("Bad rx_thread_id join.\n");
    pthread_join(polling_thread_id, &retval);
    retval == PTHREAD_CANCELED ? printf("Good polling_thread_id join.\n") : printf("Bad polling_thread_id join.\n");

    // Disarm the modem.
    rxmodem_stop(&rx_modem);

    // Destroy modems.
    rxmodem_destroy(&rx_modem);
    txmodem_destroy(&tx_modem);

    // Destroy other things.
    close(global_data->network_data->socket);
    delete (global_data->network_data);

    return global_data->thread_status;
}