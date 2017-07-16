/**
 *   Lockfree is a set of lockfree containers for Linux and Linux kernel
 *   Copyright (C) <2017>  Arkady Miasnikov
 *
 *   This program is free software: you can redistribute it and/or modify
 *   it under the terms of the GNU General Public License as published by
 *   the Free Software Foundation, either version 3 of the License, or
 *   (at your option) any later version.
 *
 *   This program is distributed in the hope that it will be useful,
 *   but WITHOUT ANY WARRANTY; without even the implied warranty of
 *   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *   GNU General Public License for more details.
 *
 *   You should have received a copy of the GNU General Public License
 *   along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include "hashtable.h"
#include "linux_utils.h"


#define HASHTABLE_BITS 8


static hashtable_t hashtable = {"hash", HASHTABLE_BITS, hash_none};

DECLARE_HASHTABLE(uint32, uint32_t, 4, 0, 0);

/**
 *   The hashtable does 'value & ((1 << HASHTABLE_BITS)-1)'
 *   The function generates values which will cause collision
 *   in the table slot 0, the values are unique
 */
static inline uint32_t get_value_collision(int idx)
{
    return ((1 << HASHTABLE_BITS) << idx);
}

static inline uint32_t get_value(int idx)
{
    return idx;
}

static int thread_job(void *thread_arg)
{
    int idx = (int)(size_t)thread_arg;

    uint32_t value_to_store = get_value_collision(idx);


    while (1)
    {
        int rc = hashtable_uint32_insert(&hashtable, value_to_store, value_to_store);
        if (!rc)
        {
            linux_log(LINUX_LOG_ERROR, "Thread %d failed to insert entry %u",
                    idx, value_to_store);
            return 1;
        }

        uint32_t found_value;
        rc = hashtable_uint32_find(&hashtable, value_to_store, &found_value);
        if (!rc)
        {
            linux_log(LINUX_LOG_ERROR, "Thread %d failed to find entry %u",
                    idx, value_to_store);
            return 1;
        }
        if (found_value != value_to_store)
        {
            linux_log(LINUX_LOG_ERROR, "Thread %d found wrong entry %u vs %u",
                    idx, value_to_store, found_value);
            return 1;
        }

        rc = hashtable_uint32_find(&hashtable, ~value_to_store, &found_value);
        if (rc)
        {
            linux_log(LINUX_LOG_ERROR, "Thread %d found non-existing key %u",
                    idx, ~value_to_store);
            return 1;
        }

        uint32_t deleted_value;
        rc = hashtable_uint32_remove(&hashtable, value_to_store, &deleted_value);
        if (!rc)
        {
            linux_log(LINUX_LOG_ERROR, "Thread %d failed to remove entry %u",
                    idx, value_to_store);
            return 1;
        }
        if (deleted_value != value_to_store)
        {
            linux_log(LINUX_LOG_ERROR, "Thread %d removed wrong entry %u vs %u",
                    idx, value_to_store, deleted_value);
            return 1;
        }
        rc = hashtable_uint32_find(&hashtable, value_to_store, &found_value);
        if (rc)
        {
            linux_log(LINUX_LOG_ERROR, "Thread %d found non-existing key %u",
                    idx, value_to_store);
            return 1;
        }
    }


    return 1;
}

static int create_threads(int cpus)
{
    int rc;
    for (int i = 0;i < cpus;i++)
    {
        linux_task_state_t *state = (linux_task_state_t*)calloc(1, sizeof(linux_task_state_t));
        char filename[64];
        sprintf(filename, "%d", i);
        state->properties.name = strdup(filename);
        state->properties.task = thread_job;
        state->properties.task_arg = (void*)(size_t)i;
        rc = linux_thread_start(state);
        if (!rc)
        {
            break;
        }

    }
    return rc;
}

static int synchronous_access(int cpus)
{
    for (int i = 0;i < cpus;i++)
    {
        uint32_t value_to_store = get_value(i);
        int rc = hashtable_uint32_insert(&hashtable, value_to_store, value_to_store);
        if (!rc)
        {
            linux_log(LINUX_LOG_ERROR, "Thread %d failed to insert entry %u",
                    i, value_to_store);
            return 0;
        }
    }
    for (int i = 0;i < cpus;i++)
    {
        uint32_t value_to_store = get_value(i);
        uint32_t deleted_value;
        int rc = hashtable_uint32_remove(&hashtable, value_to_store, &deleted_value);
        if (!rc)
        {
            linux_log(LINUX_LOG_ERROR, "Thread %d failed to remove entry %u",
                    i, value_to_store);
            return 0;
        }
        if (deleted_value != value_to_store)
        {
            linux_log(LINUX_LOG_ERROR, "Thread %d removed wrong entry %u vs %u",
                    i, value_to_store, deleted_value);
            return 1;
        }
    }
    return 1;
}

int main()
{
    int cpus = 4; //linux_get_number_processors()
    int rc;

    do
    {

        rc = hashtable_uint32_init(&hashtable);
        if (!rc)
        {
            break;
        }

        rc = synchronous_access(cpus);
        if (!rc)
        {
            break;
        }

        rc = create_threads(cpus);
        if (!rc)
        {
            break;
        }

        while (1)
        {
            linux_ms_sleep(1000);
            char buf[4*1024];
            hashtable_show(buf, ARRAY_SIZE(buf));
            linux_log(LINUX_LOG_INFO, "%s", buf);
        }
        hashtable_close(&hashtable);
    }
    while (0);

    return 0;
}
