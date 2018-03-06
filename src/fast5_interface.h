#pragma once
#ifndef EVENTS_H
#    define EVENTS_H

#    include <hdf5.h>
#    include <stdbool.h>
#    include "scrappie_structures.h"

void disable_openmp_if_hdf5_not_threaded(void);

raw_table read_raw(const char *filename, bool scale_to_pA);

void write_annotated_events(hid_t hdf5file, const char *readname,
                            const event_table ev, hsize_t chunk_size,
                            int compression_level);
void write_annotated_raw(hid_t hdf5file, const char *readname,
                         const raw_table rt, hsize_t chunk_size,
                         int compression_level);

#if defined(_OPENMP)
void disable_openmp_if_hdf5_not_threaded(void);
#endif  /* _OPENMP */

#endif                          /* EVENTS_H */
