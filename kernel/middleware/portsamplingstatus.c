/*
 *                               POK header
 * 
 * The following file is a part of the POK project. Any modification should
 * made according to the POK licence. You CANNOT use this file or a part of
 * this file is this part of a file for your own project
 *
 * For more information on the POK licence, please see our LICENCE FILE
 *
 * Please follow the coding guidelines described in doc/CODING_GUIDELINES
 *
 *                                      Copyright (c) 2007-2009 POK team 
 *
 * Created by julien on Thu Jan 15 23:34:13 2009 
 */


#ifdef POK_NEEDS_PORTS_SAMPLING

/**
 * Generated code does not use this syscall
 */

#ifndef POK_GENERATED_CODE

#include <errno.h>
#include <types.h>
#include <core/sched.h>
#include <core/time.h>
#include <middleware/port.h>

extern pok_port_t pok_ports[POK_CONFIG_NB_PORTS];

pok_ret_t pok_port_sampling_status (pok_port_id_t         id,
                                    pok_port_sampling_status_t* status)
{
   if (id >= POK_CONFIG_NB_PORTS)
   {
      return POK_ERRNO_EINVAL;
   }

   if (! pok_own_port (POK_SCHED_CURRENT_PARTITION, id))
   {
      return POK_ERRNO_PORT;
   }

   if (pok_ports[id].ready == FALSE)
   {
      return POK_ERRNO_EINVAL;
   }

   if (pok_ports[id].kind != POK_PORT_KIND_SAMPLING)
   {
      return POK_ERRNO_EINVAL;
   }

   if (pok_ports[id].partition != POK_SCHED_CURRENT_PARTITION)
   {
      return POK_ERRNO_EINVAL;
   }

   status->size      = pok_ports[id].size;
   status->direction = pok_ports[id].direction;
   status->refresh   = pok_ports[id].refresh; 

   if (pok_ports[id].empty == TRUE || (pok_ports[id].last_receive + pok_ports[id].refresh) < POK_GETTICK())
   {
      status->validity = FALSE;
   }
   else
   {
      status->validity = TRUE;
   }

   return POK_ERRNO_OK;
}

#endif

#endif
