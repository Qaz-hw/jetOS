#include <stdio.h>
#include <string.h>
#include <arinc653/buffer.h>
#include <arinc653/partition.h>
#include <arinc653/time.h>
#include <arinc653/queueing.h>
#include <arinc653/sampling.h>

#include <net/network.h>
#include <sysconfig.h>
#include <port_info.h>

#define SECOND 1000000000LL

static void queuing_send_to_partition(unsigned channel_idx, MESSAGE_ADDR_TYPE payload, size_t length)
{
    RETURN_CODE_TYPE ret;
    sys_channel_t channel = sys_queuing_channels[channel_idx];
    sys_queuing_port_t *port = &sys_queuing_ports[channel.port_index];

    SEND_QUEUING_MESSAGE(
            port->id,
            payload,
            length,
            0,
            &ret);

    if (ret == NOT_AVAILABLE) {
        printf("Buffer is full, drop packet\n");
    } else if (ret != NO_ERROR) {
        printf("SYSNET %s port error: %u\n", port->header.name, ret);
    }
}

static void sampling_send_to_partition(unsigned channel_idx, MESSAGE_ADDR_TYPE payload, size_t length)
{
    sys_channel_t channel = sys_sampling_channels[channel_idx];
    sys_sampling_port_t *port = &sys_sampling_ports[channel.port_index];
    RETURN_CODE_TYPE ret;

    WRITE_SAMPLING_MESSAGE(
            port->id,
            payload, 
            length, 
            &ret);

    if (ret != NO_ERROR) {
        printf("error: %u\n", ret);
    }

}

static pok_bool_t udp_received_callback(
        uint32_t ip, 
        uint16_t udp_port, 
        const char *payload, 
        size_t length) 
{
    for (int i = 0; i<sys_sampling_channels_nb; i++) {
        sys_channel_t *s_channel = &sys_sampling_channels[i];
        //TODO
        //if (s_channel->protocol != UDP)
        //    continue;

        sys_sampling_port_t *port = &sys_sampling_ports[s_channel->port_index];
        udp_data_t *udp_data = s_channel->driver_data;

        if (port->header.direction != SOURCE)
            continue;
        if (udp_data->ip != ip || udp_data->port != udp_port)
            continue;

        sampling_send_to_partition(i, (MESSAGE_ADDR_TYPE) payload, length);
    }

    for (int i = 0; i<sys_queuing_channels_nb; i++) {
        sys_channel_t *q_channel = &sys_queuing_channels[i];
        //TODO
        //if (q_channel->protocol != UDP)
        //    continue;

        sys_queuing_port_t *port = &sys_queuing_ports[q_channel->port_index];
        udp_data_t *udp_data = q_channel->driver_data;

        if (port->header.direction != SOURCE)
            continue;
        if (udp_data->ip != ip || udp_data->port != udp_port)
            continue;

        queuing_send_to_partition(i, (MESSAGE_ADDR_TYPE) payload, length);
    }

    return FALSE;
}

static void queuing_packet_sent_callback(void *arg)
{
    sys_queuing_data_t *qdata = arg;
    qdata->status = QUEUING_STATUS_SENT;
    sys_queuing_port_t *port = &sys_queuing_ports[qdata->port_index];

    for (int i = 0; i < port->nb_message; i++) {
        sys_queuing_data_t *cur_data = utils_queue_head(port);

        if (cur_data->status == QUEUING_STATUS_SENT) {
            port->queue_head = (port->queue_head + 1) % port->max_nb_messages;
            port->nb_message--;

            cur_data->status = QUEUING_STATUS_NONE;

        } else {
            // ignore messages not at the head of the queue
            // even if they're already sent, and buffers are free to use
            break;
        }
    }
}

static void sampling_packet_sent_callback(void *arg) {
    pok_bool_t *var = (pok_bool_t*) arg;
    if (!*var)
        printf("error: buffer is no busy in callback\n");
    *var = FALSE;
}

static void queuing_send_outside(unsigned channel_idx)
{
    sys_channel_t channel = sys_queuing_channels[channel_idx];
    RETURN_CODE_TYPE ret;

    sys_queuing_port_t *port = &sys_queuing_ports[channel.port_index];

    while (!utils_queue_full(port)) {

        if (port->nb_message > port->max_nb_messages/2) {
            channel.driver_ptr->reclaim_send_buffers();
        }

        sys_queuing_data_t *dst_place = utils_queue_tail(port);
        if (dst_place->status != QUEUING_STATUS_NONE) {
            printf("SYSNET error: status is not NONE\n");
            STOP_SELF();
        }

        RECEIVE_QUEUING_MESSAGE(
                port->id,
                0,
                (MESSAGE_ADDR_TYPE ) (dst_place->data + port->header.overhead),
                &dst_place->message_size,
                &ret
                );

        if (ret != NO_ERROR) {
            if (ret != NOT_AVAILABLE)
                printf("SYSNET: %s port error: %u\n", port->header.name, ret);
            break;
        }

        port->nb_message++;

        pok_bool_t res = channel.driver_ptr->send(
                dst_place->data,
                dst_place->message_size,
                channel.driver_data,
                queuing_packet_sent_callback,
                (void*) dst_place
                );

        if (!res)
            printf("SYSNET: Error in send_udp\n");

        dst_place->status = QUEUING_STATUS_PENDING;
        dst_place->port_index = channel.port_index;
        pok_network_flush_send();
    }
}

static void sampling_send_outside(unsigned channel_idx)
{
    RETURN_CODE_TYPE ret;
    VALIDITY_TYPE validity;

    sys_channel_t channel = sys_sampling_channels[channel_idx];
    sys_sampling_port_t *port = &sys_sampling_ports[channel.port_index];
    sys_port_data_t *dst_place = port->data;

    if (!SYS_SAMPLING_PORT_CHECK_IS_NEW_DATA(port->id))
        return;

    if (dst_place->busy) {
        printf("SYSNET error: sampling buffer is busy\n");
        STOP_SELF();
        return;
    }

    READ_SAMPLING_MESSAGE(
            port->id,
            (MESSAGE_ADDR_TYPE ) (dst_place->data + port->header.overhead),
            &dst_place->message_size,
            &validity,
            &ret
            );

    if (ret != NO_ERROR) {
        if (ret != NOT_AVAILABLE)
            printf("SYSNET: %s port error: %u\n", port->header.name, ret);
        return;
    }

    dst_place->busy = TRUE;

    pok_bool_t res = channel.driver_ptr->send(
            dst_place->data,
            dst_place->message_size,
            channel.driver_data,
            sampling_packet_sent_callback,
            &dst_place->busy
            );

    if (!res)
        printf("SYSNET: Error in send_udp\n");

    pok_network_flush_send();
}

static void first_process(void)
{
    while(1) {
        for (int i = 0; i<sys_sampling_channels_nb; i++) {
            sys_channel_t *channel = &sys_sampling_channels[i];
            sys_sampling_port_t *port = &sys_sampling_ports[channel->port_index];

            if (port->header.direction != DESTINATION)
                break;

            sampling_send_outside(i);
            channel->driver_ptr->reclaim_send_buffers();
        }

        for (int i = 0; i<sys_queuing_channels_nb; i++) {
            sys_channel_t *channel = &sys_queuing_channels[i];
            sys_queuing_port_t *port = &sys_queuing_ports[channel->port_index];

            if (port->header.direction != DESTINATION)
                break;

            queuing_send_outside(i);
            channel->driver_ptr->reclaim_send_buffers();
        }

        for (int i = 0; i < channel_drivers_nb; i++) {
            channel_driver_t *driver_ptr = channel_drivers[i];
            driver_ptr->receive();
        }
    }
}

static int real_main(void)
{
    RETURN_CODE_TYPE ret;
    PROCESS_ID_TYPE pid;
    PROCESS_ATTRIBUTE_TYPE process_attrs = {
        .PERIOD = INFINITE_TIME_VALUE,
        .TIME_CAPACITY = INFINITE_TIME_VALUE,
        .STACK_SIZE = 8096, // the only accepted stack size!
        .BASE_PRIORITY = MIN_PRIORITY_VALUE,
        .DEADLINE = SOFT,
    };

    // create process 1
    process_attrs.ENTRY_POINT = first_process;
    strncpy(process_attrs.NAME, "process 1", sizeof(PROCESS_NAME_TYPE));

    CREATE_PROCESS(&process_attrs, &pid, &ret);
    if (ret != NO_ERROR) {
        printf("couldn't create process 1: %d\n", (int) ret);
        return 1;
    } else {
        printf("process 1 created\n");
    }
    
    START(pid, &ret);
    if (ret != NO_ERROR) {
        printf("couldn't start process 1: %d\n", (int) ret);
        return 1;
    } else {
        printf("process 1 \"started\" (it won't actually run until operating mode becomes NORMAL)\n");
    }

    for (int i = 0; i<sys_sampling_ports_nb; i++) {
        sys_sampling_port_t *port = &sys_sampling_ports[i];
        CREATE_SAMPLING_PORT(
                port->header.name,
                port->max_message_size,
                port->header.direction,
                0,
                &port->id,
                &ret);

        if (ret != NO_ERROR)
            printf("error %d creating %s port\n", ret, port->header.name);
    }

    for (int i = 0; i<sys_queuing_ports_nb; i++) {
        sys_queuing_port_t *port = &sys_queuing_ports[i];
        CREATE_QUEUING_PORT(
                port->header.name,
                port->max_message_size,
                port->max_nb_messages,
                port->header.direction,
                FIFO,
                &port->id,
                &ret);
        if (ret != NO_ERROR)
            printf("error %d creating %s port\n", ret, port->header.name);

    }

    drivers_init();

    static pok_network_udp_receive_callback_t udp_callback = {udp_received_callback, NULL};
    pok_network_register_udp_receive_callback(&udp_callback);

    // transition to NORMAL operating mode
    // N.B. if everything is OK, this never returns
    printf("going to NORMAL mode...\n");
    SET_PARTITION_MODE(NORMAL, &ret);

    if (ret != NO_ERROR) {
        printf("couldn't transit to normal operating mode: %d\n", (int) ret);
    } 

    STOP_SELF();
    return 0;
}

void main(void) {
    real_main();
    STOP_SELF();
}  
