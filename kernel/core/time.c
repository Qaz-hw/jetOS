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

/**
 * \file    core/time.c
 * \author  François Goudal
 * \author  Julien Delange
 * \date    2008-2009
 */

#include <config.h>

#if defined (POK_NEEDS_TIME) || defined (POK_NEEDS_SCHED) || defined (POK_NEEDS_THREADS)

#include <bsp_common.h>
#include <types.h>
#include <errno.h>

#include <core/time.h>
#include <core/thread.h>
#include <core/sched.h>
#include <core/partition.h>
#include <core/uaccess.h>

/**
 * A global variable that contains the number
 * of elapsed ticks since the beginning
 */
uint64_t pok_tick_counter = 0;

/**
 * \brief Init the timing service.
 */
void pok_time_init (void)
{
   pok_tick_counter = 0;
}


#ifdef POK_NEEDS_GETTICK
/**
 * Get the current ticks value, store it in
 * \a clk_val
 * Returns POK_ERRNO_OK
 * Need the GETTICK service (POK_NEEDS_GETTICKS maccro)
 */
pok_ret_t pok_gettick_by_pointer (pok_time_t* __user clk_val)
{
   pok_time_t* __kuser k_clk_val = jet_user_to_kernel_typed(clk_val);
   if(!k_clk_val) return POK_ERRNO_EFAULT;
   
   *k_clk_val = POK_GETTICK();
   return POK_ERRNO_OK;
}
#endif

#endif /* POK_NEEDS_... */
