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
#include <signal.h>
#include "meb_debug.hpp"
#include "gs_uhf.hpp"

int main(int argc, char **argv)
{
    // Ignores broken pipe signal, which is sent to the calling process when writing to a nonexistent socket (
    // see: https://www.linuxquestions.org/questions/programming-9/how-to-detect-broken-pipe-in-c-linux-292898/
    // and 
    // https://github.com/sunipkmukherjee/example_imgui_server_client/blob/master/guimain.cpp
    // Allows manual handling of a broken pipe signal using 'if (errno == EPIPE) {...}'.
    // Broken pipe signal will crash the process, and it caused by sending data to a closed socket.
    signal(SIGPIPE, SIG_IGN);

    // Spawn UHF-RX thread.
    // Spawn Network-RX thread.

    // When something is received from the GS Network...
    // - Parse it.
    // - TX it (or not).

    // When something is received on UHF...
    // - Parse it.
    // - Send it to the server (or not).

    // Set up global data.
    global_data_t global[1] = {0};
    global->network_data = new NetDataClient(NetPort::ROOFUHF, SERVER_POLL_RATE);
    global->network_data->recv_active = true;

    // Create Ground Station Network thread IDs.
    pthread_t net_polling_tid, net_rx_tid, uhf_rx_tid;

    // Start the RX threads, and restart them should it be necessary.
    // Only gets-out if a thread declares an unrecoverable emergency and sets its status to -1.
    while (global->network_data->thread_status > -1)
    {
        // 1 = All good, 0 = recoverable failure, -1 = fatal failure (close program)
        global->network_data->thread_status = 1;

        // Initialize and begin socket communication to the server.
        if (!global->network_data->connection_ready)
        {
            // TODO: Check if this is the desired functionality.
            // Currently, the program will not proceed past this point if it cannot connect to the server.
            // _polling_thread will handle disconnections from the server.
            while (gs_connect_to_server(global->network_data) != 1)
            {
                dbprintlf(RED_FG "Failed to establish connection to server.");
                usleep(5 SEC);
            }
        }

        // Start the threads.
        pthread_create(&net_polling_tid, NULL, gs_polling_thread, global->network_data);
        pthread_create(&net_rx_tid, NULL, gs_network_rx_thread, global);
        // pthread_create(&uhf_rx_tid, NULL, gs_uhf_rx_thread, global);
        
        void *thread_return;
        pthread_join(net_polling_tid, &thread_return);
        pthread_join(net_rx_tid, &thread_return);
        pthread_join(uhf_rx_tid, &thread_return);

        // Loop will begin, restarting the threads.
    }

    // Finished.
    void *thread_return;
    pthread_cancel(net_polling_tid);
    pthread_cancel(net_rx_tid);
    pthread_cancel(uhf_rx_tid);
    pthread_join(net_polling_tid, &thread_return);
    thread_return == PTHREAD_CANCELED ? printf("Good net_polling_tid join.\n") : printf("Bad net_polling_tid join.\n");
    pthread_join(net_rx_tid, &thread_return);
    thread_return == PTHREAD_CANCELED ? printf("Good net_rx_tid join.\n") : printf("Bad net_rx_tid join.\n");
    pthread_join(uhf_rx_tid, &thread_return);
    thread_return == PTHREAD_CANCELED ? printf("Good uhf_rx_tid join.\n") : printf("Bad uhf_rx_tid join.\n");

    // Put radio to sleep.
    si446x_sleep();

    // Destroy other things.
    close(global->network_data->socket);

    int retval = global->network_data->thread_status;
    delete global->network_data;
    return retval;
}
