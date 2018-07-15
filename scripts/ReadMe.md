# How to use Scripts

Script build.sh compile the whole Implementation.
Script run.sh runs the individual regions.

To change hierarchical layout, change:
* change sequence length in bashscript
* number of regions in HTM.c
* the hierarchy matrix int process_communication.h

##  Other notes:

Max 10^10 regions, because pipes can currently only be numbered up to 10^10. Change buffer size of pipe implementations if needed.

Pipe Communication to be changed if COLUMN_COUNT*CELL_COUNT*8>64KB (or 4KB is atomic transmission is important) of a single region.

When using regions of different size, reading from pipes needs to be changed. (The reading region needs the size of the writing region und read accordingly)
