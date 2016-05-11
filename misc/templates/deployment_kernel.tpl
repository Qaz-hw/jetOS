/*
 * Institute for System Programming of the Russian Academy of Sciences
 * Copyright (C) 2016 ISPRAS
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation, Version 3.
 *
 * This program is distributed in the hope # that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 *
 * See the GNU General Public License version 3 for more details.
 */


#include <types.h>
#include <core/error.h>
#include <core/error_arinc.h>
#include <core/partition_arinc.h>
#include <core/partition.h>

/*********************** HM module tables *****************************/
/*
 * HM module selector.
 */
pok_error_level_selector_t pok_hm_module_selector = {
    .levels = {
{%for error_id in conf.error_ids_all%}
    {{conf.module_hm_table.level_selector_total(error_id)}}, /* POK_ERROR_ID_{{error_id}} */
{%endfor%}
    }
};

/*
 * HM module table.
 * 
 * SHUTDOWN for all errors.
 */
pok_error_module_action_table_t pok_hm_module_table = {
    .actions = {
{%for system_state in conf.system_states_all%}
    /* POK_SYSTEM_STATE_{{system_state}} */
    {
{%for error_id in conf.error_ids_all%}
        POK_ERROR_MODULE_ACTION_{{conf.module_hm_table.get_action(system_state, error_id)}}, /* POK_ERROR_ID_{{error_id}}*/
{%endfor%}
    },
{%endfor%}
    }
};

/********************* HM Multi-partition tables **********************/
// Default HM multi-partition selector - all errors are partition-level.
pok_error_level_selector_t hm_multi_partition_selector_default = {
    .levels = {
{%for error_id in conf.error_ids_all%}
        0x7f,   /* POK_ERROR_ID_{{error_id}} */
{%endfor%}
    }
};

// Default HM multi-partition table - shutdown for all errors.
pok_error_module_action_table_t hm_multi_partition_table_default = {
    .actions = {
{%for system_state in conf.system_states_all%}
        {POK_ERROR_MODULE_ACTION_SHUTDOWN, }, /* POK_SYSTEM_STATE_{{system_state}} */
{%endfor%}
    }
};

{%macro connection_partition(connection)%}
{%if connection.get_kind_constant() == 'Local'%}
&pok_partitions_arinc[{{connection.port.partition.part_index}}].base_part
{%-elif connection.get_kind_constant() == 'UDP'%}
error("UDP connection for the channel doesn't supported yet")
{%-endif%}
{%-endmacro%}

/**************** Setup queuing channels ****************************/
pok_channel_queuing_t pok_channels_queuing[{{ conf.channels_queueing | length }}] = {
    {%for channel_queueing in conf.channels_queueing%}
    {
        .max_message_size = {{channel_queueing.max_message_size}},
        .max_nb_message_send = {{channel_queueing.max_nb_message_send}},
        .max_nb_message_receive = {{channel_queueing.max_nb_message_receive}},
        .receiver = {{connection_partition(channel_queueing.dst)}},
        .sender = {{connection_partition(channel_queueing.src)}},
    },
    {%endfor%}
};

uint8_t pok_channels_queuing_n = {{ conf.channels_queueing | length }};

/****************** Setup sampling channels ***************************/
pok_channel_sampling_t pok_channels_sampling[{{ conf.channels_sampling | length }}] = {
    {%for channel_sampling in conf.channels_sampling%}
    {
        .max_message_size = {{channel_sampling.max_message_size}},
    },
    {%endfor%}
};

uint8_t pok_channels_sampling_n = {{ conf.channels_sampling | length }};

{%for part in conf.partitions%}
/****************** Setup partition{{loop.index0}} (auxiliary) **********************/
// HM partition level selector.
static const pok_error_level_selector_t partition_hm_selector_{{loop.index0}} = {
    .levels = {
{%for error_id in conf.error_ids_all %}
        {{part.hm_table.level_selector_total(error_id)}}, /*POK_ERROR_ID_{{error_id}}*/
{%endfor%}
    }
};
// Mapping of process-level errors information.
static const pok_thread_error_map_t partition_thread_error_info_{{loop.index0}} = {
    .map = {
{%for error_id in conf.error_ids_all %}
{%if error_id in part.hm_table.user_level_codes%}
{%set error_code, error_description = part.hm_table.user_level_codes[error_id] %}
        {POK_ERROR_KIND_{{error_code}}, "{{error_description}}"},        /* POK_ERROR_ID_{{error_id}} */
{%else%}
        {POK_ERROR_KIND_INVALID, NULL}, /*POK_ERROR_ID_{{error_id}}*/
{%endif%}
{%endfor%}
    }
};

/* 
 * Pointer to partition HM table.
 */
static const pok_error_hm_partition_t partition_hm_table_{{loop.index0}} = {
    .actions = {
{%for s in conf.system_states_all %}
    /* POK_SYSTEM_STATE_{{s}} */
    {
{%for error_id in conf.error_ids_all %}
        POK_ERROR_ACTION_{{part.hm_table.get_action(s, error_id)}}, /* POK_ERROR_ID_{{error_id}} */
{%endfor%}
    },
{%endfor%}
    }
};

// Threads array
static pok_thread_t partition_threads_{{loop.index0}}[{{part.num_threads}} + 1 /*main thread*/ + 1 /* error thread */];

// Queuing ports
static pok_port_queuing_t partition_ports_queuing_{{loop.index0}}[{{part.ports_queueing | length}}] = {
{%for port_queueing in part.ports_queueing%}
    {
        .name = "{{port_queueing.name}}",
        .channel = &pok_channels_queuing[{{port_queueing.channel_id}}],
        .direction = {%if port_queueing.is_src()%}POK_PORT_DIRECTION_OUT{%else%}POK_PORT_DIRECTION_IN{%endif%},
    },
{%endfor%}
};

// Sampling ports
static pok_port_sampling_t partition_ports_sampling_{{loop.index0}}[{{part.ports_sampling | length}}] = {
{%for port_sampling in part.ports_sampling%}
    {
        .name = "{{port_sampling.name}}",
        .channel = &pok_channels_sampling[{{port_sampling.channel_id}}],
        .direction = {%if port_sampling.is_src()%}POK_PORT_DIRECTION_OUT{%else%}POK_PORT_DIRECTION_IN{%endif%},
    },
{%endfor%}
};

// Buffers array
static pok_buffer_t partition_buffers_{{loop.index0}}[{{part.num_arinc653_buffers}}];

// Blackboards array
static pok_blackboard_t partition_blackboards_{{loop.index0}}[{{part.num_arinc653_blackboards}}];

// Semaphores array
static pok_semaphore_t partition_semaphores_{{loop.index0}}[{{part.num_arinc653_semaphores}}];

// Events array
static pok_event_t partition_events_{{loop.index0}}[{{part.num_arinc653_events}}];
{%endfor%}{#partitions loop#}

/*************** Setup partitions array *******************************/
pok_partition_arinc_t pok_partitions_arinc[{{conf.partitions | length}}] = {
{%for part in conf.partitions%}
    {
        .base_part = {
            .name = "{{part.name}}",
            
            .period = {{conf.major_frame}}, {#TODO: Where it is stored in conf?#}
            
            .space_id = {{loop.index0}},
            
            .multi_partition_hm_selector = &hm_multi_partition_selector_default,
            .multi_partition_hm_table = &hm_multi_partition_table_default,
        },
        
        .size = {{part.size}},
        .nthreads = {{part.num_threads}} + 1 /*main thread*/ + 1 /* error thread */,
        .threads = partition_threads_{{loop.index0}},
        
        .main_user_stack_size = 8192, {# TODO: This should be set in config somehow. #}

        .ports_queuing = partition_ports_queuing_{{loop.index0}},
        .nports_queuing = {{part.ports_queueing | length}},

        .ports_sampling = partition_ports_sampling_{{loop.index0}},
        .nports_sampling = {{part.ports_sampling | length}}, {#TODO: ports#}


        .intra_memory_size = {{part.buffer_data_size}} + {{part.blackboard_data_size}}, // Memory for intra-partition communication

        .buffers = partition_buffers_{{loop.index0}},
        .nbuffers = {{part.num_arinc653_buffers}},

        .blackboards = partition_blackboards_{{loop.index0}},
        .nblackboards = {{part.num_arinc653_blackboards}},
        
        .semaphores = partition_semaphores_{{loop.index0}},
        .nsemaphores = {{part.num_arinc653_semaphores}},

        .events = partition_events_{{loop.index0}},
        .nevents = {{part.num_arinc653_events}},

        .partition_hm_selector = &partition_hm_selector_{{loop.index0}},
    
        .thread_error_info = &partition_thread_error_info_{{loop.index0}},

        .partition_hm_table = &partition_hm_table_{{loop.index0}},

		.partition_id = {{loop.index0}},
    },
{%endfor%}{#partitions loop#}
};

const uint8_t pok_partitions_arinc_n = {{conf.partitions | length}};

/**************************** Monitor *********************************/
pok_partition_t partition_monitor =
{
    .name = "Monitor",
    
    .period = {{conf.major_frame}}, {#TODO: Where it is stored in conf?#}
    
    .space_id = 0xff,
    
    .multi_partition_hm_selector = &hm_multi_partition_selector_default,
    .multi_partition_hm_table = &hm_multi_partition_table_default,
};

/******************************* GDB **********************************/
pok_partition_t partition_gdb =
{
    .name = "GDB",

    .period = {{conf.major_frame}}, {#TODO: Where it is stored in conf?#}

    .space_id = 0xff,

    .multi_partition_hm_selector = &hm_multi_partition_selector_default,
    .multi_partition_hm_table = &hm_multi_partition_table_default,
};


/************************* Setup time slots ***************************/
const pok_sched_slot_t pok_module_sched[{{conf.slots | length}}] = {
{%for slot in conf.slots%}
    {
        .duration = {{slot.duration}},
        .offset = 0, {#TODO: precalculate somehow#}
        {%-if slot.get_kind_constant() == 'POK_SLOT_PARTITION' %}

        .partition = &pok_partitions_arinc[{{slot.partition.part_index}}].base_part,
        .periodic_processing_start = {%if slot.periodic_processing_start%}TRUE{%else%}FALSE{%endif%},
        {%elif slot.get_kind_constant() == 'POK_SLOT_MONITOR' %}

        .partition = &partition_monitor,
        .periodic_processing_start = FALSE,
        {%elif slot.get_kind_constant() == 'POK_SLOT_GDB' %}

        .partition = &partition_gdb,
        .periodic_processing_start = FALSE,
        {%endif-%}
        .id = {{loop.index0}}
    },
{%endfor%}
};

const uint8_t pok_module_sched_n = {{conf.slots | length}};

const pok_time_t pok_config_scheduling_major_frame = {{conf.major_frame}};

/************************ Setup address spaces ************************/
struct pok_space spaces[{{conf.partitions | length}}]; // As many as partitions